/* acct.h — shared account-database backend for passwd/useradd/userdel/usermod.
 *
 * Header-only (static fns) so each util compiles it in without a shared-lib
 * build step. Handles the colon-delimited /etc/{passwd,shadow,group} files
 * record-by-record (unlike libinstall's credentials.c, which truncates and
 * writes a single primary user), plus the crypt()/salt/validation the utils
 * share. Writes are atomic (temp + rename) and re-chmod after create (the Aegis
 * VFS hardcodes 0644 on O_CREAT, so modes must be set explicitly — see
 * credentials.c). All of these paths need CAP_KIND_AUTH to touch /etc/shadow;
 * that authority comes from each util's caps.d policy, and the mutating utils
 * gate themselves on an admin session (see acct_require_admin).
 */
#ifndef LORICA_ACCT_H
#define LORICA_ACCT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>
#include <crypt.h>

#ifndef O_TRUNC
#define O_TRUNC 0x200
#endif

#define ACCT_PASSWD "/etc/passwd"
#define ACCT_SHADOW "/etc/shadow"
#define ACCT_GROUP  "/etc/group"

/* ── validation ─────────────────────────────────────────────────────────── */

/* A name is interpolated raw into colon/newline-delimited records, so it must
 * contain neither (which would inject fields or whole lines). Mirrors
 * libinstall install_username_valid. */
static int acct_name_valid(const char *name)
{
    if (!name || !name[0]) return 0;
    int len = 0;
    for (const char *p = name; *p; p++, len++) {
        char c = *p;
        int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                 c == '_' || c == '-';
        if (p == name && !((c >= 'a' && c <= 'z') || c == '_')) return 0;
        if (!ok) return 0;
    }
    return len <= 31;
}

/* ── crypto ─────────────────────────────────────────────────────────────── */

/* Crypto-random $6$ salt. FAILS CLOSED (returns -1) if /dev/urandom is
 * unavailable or short — a predictable salt is worse than a hard error. */
static int acct_gen_salt(char *buf, int bufsize)
{
    static const char b64[] =
        "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    uint8_t rb[12];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    int got = 0, r;
    while (got < (int)sizeof(rb) &&
           (r = (int)read(fd, rb + got, sizeof(rb) - (size_t)got)) > 0)
        got += r;
    close(fd);
    if (got != (int)sizeof(rb)) return -1;
    int pos = 0;
    buf[pos++] = '$'; buf[pos++] = '6'; buf[pos++] = '$';
    for (int i = 0; i < 12 && pos < bufsize - 2; i++)
        buf[pos++] = b64[rb[i] % 64];
    buf[pos++] = '$'; buf[pos] = '\0';
    return 0;
}

/* Hash `password` into out (>=128 bytes). Returns 0 / -1 (fail-closed). */
static int acct_hash_password(const char *password, char *out, int outsz)
{
    if (!password || !out || outsz < 128) return -1;
    char salt[32];
    if (acct_gen_salt(salt, sizeof salt) != 0) return -1;
    char *h = crypt(password, salt);
    if (!h) return -1;
    snprintf(out, (size_t)outsz, "%s", h);
    return 0;
}

/* Verify `password` against a stored crypt hash. Returns 1 match / 0 no. */
static int acct_verify_password(const char *password, const char *stored)
{
    if (!password || !stored || !stored[0]) return 0;
    char *h = crypt(password, stored);   /* stored hash is its own salt */
    return h && strcmp(h, stored) == 0;
}

/* ── record file editing (atomic, first-field keyed) ────────────────────── */

/* Read `path` into a malloc'd buffer (NUL-terminated). Caller frees. *len set
 * to byte count (excl. NUL). Returns NULL if the file is absent/unreadable. */
static char *acct_slurp(const char *path, size_t *len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    if (!buf) { close(fd); return NULL; }
    for (;;) {
        if (n + 4096 + 1 > cap) { cap *= 2; char *nb = realloc(buf, cap);
                                  if (!nb) { free(buf); close(fd); return NULL; } buf = nb; }
        ssize_t r = read(fd, buf + n, 4096);
        if (r < 0) { free(buf); close(fd); return NULL; }
        if (r == 0) break;
        n += (size_t)r;
    }
    close(fd);
    buf[n] = '\0';
    if (len) *len = n;
    return buf;
}

/* Atomically replace `path` with `data` (len bytes), then chmod to `mode`. */
static int acct_write_atomic(const char *path, const char *data, size_t len, int mode)
{
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.acct-tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w <= 0) { close(fd); unlink(tmp); return -1; }
        off += (size_t)w;
    }
    close(fd);
    chmod(tmp, mode);                 /* VFS hardcodes 0644 on O_CREAT */
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* Does `line` (a record) have first colon-field == name? */
static int acct_line_is(const char *line, const char *name)
{
    size_t nl = strlen(name);
    return strncmp(line, name, nl) == 0 && (line[nl] == ':' );
}

/* Return the record line for `name` in `path` (malloc'd, no newline), or NULL. */
static char *acct_get_record(const char *path, const char *name)
{
    size_t len;
    char *buf = acct_slurp(path, &len);
    if (!buf) return NULL;
    char *out = NULL;
    for (char *p = buf; *p; ) {
        char *nl = strchr(p, '\n');
        size_t ll = nl ? (size_t)(nl - p) : strlen(p);
        if (acct_line_is(p, name)) { out = strndup(p, ll); break; }
        if (!nl) break;
        p = nl + 1;
    }
    free(buf);
    return out;
}

/* op: 'r' replace the record for `name` with `newrec` (no newline); 'd' delete
 * it; 'a' append `newrec` (fails if `name` already present). `mode` is the file
 * mode after write. Returns 0, -1 error, 1 not-found (r/d) / already-exists (a). */
static int acct_edit_record(const char *path, const char *name,
                            const char *newrec, char op, int mode)
{
    size_t len;
    char *buf = acct_slurp(path, &len);
    if (!buf) { buf = strdup(""); len = 0; }
    size_t cap = len + (newrec ? strlen(newrec) : 0) + 4;
    char *out = malloc(cap);
    if (!out) { free(buf); return -1; }
    size_t o = 0;
    int found = 0;
    for (char *p = buf; *p; ) {
        char *nl = strchr(p, '\n');
        size_t ll = nl ? (size_t)(nl - p + 1) : strlen(p);   /* incl newline */
        if (acct_line_is(p, name)) {
            found = 1;
            if (op == 'r') { o += (size_t)sprintf(out + o, "%s\n", newrec); }
            /* op=='d': skip the line */
        } else {
            memcpy(out + o, p, ll); o += ll;
        }
        if (!nl) break;
        p = nl + 1;
    }
    free(buf);
    int rc = 0;
    if (op == 'a') {
        if (found) rc = 1;                      /* already exists */
        else { o += (size_t)sprintf(out + o, "%s\n", newrec); }
    } else if (!found) {
        rc = 1;                                 /* replace/delete: not found */
    }
    if (rc == 0) rc = acct_write_atomic(path, out, o, mode) == 0 ? 0 : -1;
    free(out);
    return rc;
}

/* Next free numeric id (field index 2, colon-delimited) at or above `floor`,
 * scanning `path`. Used for uid (passwd) and gid (group): the primary user is
 * uid 0, additional accounts start at 1. Returns floor if the file is empty. */
static int acct_next_id(const char *path, int floor)
{
    size_t len;
    char *buf = acct_slurp(path, &len);
    int next = floor;
    if (!buf) return next;
    for (char *p = buf; *p; ) {
        char *nl = strchr(p, '\n');
        /* field 2 is after the 2nd colon: name:x:ID:... */
        char *c1 = strchr(p, ':');
        char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
        if (c2) {
            int id = atoi(c2 + 1);
            if (id >= next) next = id + 1;
        }
        if (!nl) break;
        p = nl + 1;
    }
    free(buf);
    return next;
}

/* ── admin gate ─────────────────────────────────────────────────────────── */

/* Mutating account ops require an active admin session (sudo-style elevation
 * via /bin/login -elevate against /etc/aegis/admin). sys_admin_session_active
 * (Aegis) returns nonzero when the caller holds one. Falls back to allow if the
 * syscall is absent (older kernels) so the first-boot configurator still works.
 */
#ifndef SYS_admin_session_active
#define SYS_admin_session_active 519   /* Aegis-private; see kernel/syscall */
#endif
static int acct_admin_active(void)
{
    long r = syscall(SYS_admin_session_active);
    if (r < 0) return 1;                 /* ENOSYS on old kernels → don't block */
    return r != 0;
}

#endif /* LORICA_ACCT_H */
