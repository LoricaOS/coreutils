/* passwd — change a user's password.
 *
 *   passwd            change your own password (prompts for the current one)
 *   passwd <user>     change <user>'s password — requires an admin session
 *
 * No setuid: passwd holds CAP_KIND_AUTH via its caps.d policy (that is what
 * lets it touch /etc/shadow at all, since the kernel inode-gates shadow on
 * AUTH). Changing another account requires a live admin session; changing your
 * own requires proving the current password. Writes only the target's shadow
 * hash field, leaving every other record and field intact.
 */
#include "../../include/acct.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <termios.h>

/* Read a line from the tty with echo off. Returns 0 on success. */
static int read_secret(const char *prompt, char *out, size_t outsz)
{
    fputs(prompt, stdout); fflush(stdout);
    struct termios old, no;
    int have_tty = (tcgetattr(0, &old) == 0);
    if (have_tty) { no = old; no.c_lflag &= ~(tcflag_t)ECHO; tcsetattr(0, TCSANOW, &no); }
    int ok = (fgets(out, (int)outsz, stdin) != NULL);
    if (have_tty) { tcsetattr(0, TCSANOW, &old); fputc('\n', stdout); }
    if (!ok) return -1;
    out[strcspn(out, "\n")] = '\0';
    return 0;
}

/* Replace field 1 (the hash) of a "user:hash:rest..." shadow record. */
static int build_shadow_line(const char *rec, const char *newhash,
                             char *out, size_t outsz)
{
    const char *c1 = strchr(rec, ':');
    if (!c1) return -1;
    const char *c2 = strchr(c1 + 1, ':');           /* end of old hash field */
    int n;
    if (c2)
        n = snprintf(out, outsz, "%.*s:%s%s", (int)(c1 - rec), rec, newhash, c2);
    else                                            /* no aging fields present */
        n = snprintf(out, outsz, "%.*s:%s:19814:0:99999:7:::",
                     (int)(c1 - rec), rec, newhash);
    return (n > 0 && (size_t)n < outsz) ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *target = (argc > 1) ? argv[1] : NULL;

    struct passwd *me = getpwuid(getuid());
    const char *myname = me ? me->pw_name : NULL;
    if (!target) target = myname;
    if (!target) { fprintf(stderr, "passwd: cannot determine current user\n"); return 1; }
    if (!acct_name_valid(target)) { fprintf(stderr, "passwd: invalid user name\n"); return 1; }

    int changing_other = !myname || strcmp(target, myname) != 0;
    if (changing_other && !acct_admin_active()) {
        fprintf(stderr, "passwd: only an admin session may change another user's password\n");
        return 1;
    }

    char *rec = acct_get_record(ACCT_SHADOW, target);
    if (!rec) { fprintf(stderr, "passwd: no shadow entry for '%s'\n", target); return 1; }

    /* Changing your own password: prove the current one (admins skip). */
    if (!changing_other && !acct_admin_active()) {
        const char *c1 = strchr(rec, ':');
        char oldhash[256] = "";
        if (c1) { const char *c2 = strchr(c1 + 1, ':');
                  size_t hl = c2 ? (size_t)(c2 - c1 - 1) : strlen(c1 + 1);
                  if (hl < sizeof oldhash) { memcpy(oldhash, c1 + 1, hl); oldhash[hl] = 0; } }
        char cur[256];
        if (read_secret("Current password: ", cur, sizeof cur) != 0) { free(rec); return 1; }
        if (oldhash[0] && !acct_verify_password(cur, oldhash)) {
            fprintf(stderr, "passwd: authentication failed\n"); free(rec); return 1;
        }
    }

    char p1[256], p2[256];
    if (read_secret("New password: ", p1, sizeof p1) != 0) { free(rec); return 1; }
    if (!p1[0]) { fprintf(stderr, "passwd: empty password rejected\n"); free(rec); return 1; }
    if (read_secret("Retype new password: ", p2, sizeof p2) != 0) { free(rec); return 1; }
    if (strcmp(p1, p2) != 0) { fprintf(stderr, "passwd: passwords do not match\n"); free(rec); return 1; }

    char hash[256];
    if (acct_hash_password(p1, hash, sizeof hash) != 0) {
        fprintf(stderr, "passwd: could not hash password (no secure salt)\n"); free(rec); return 1;
    }
    char line[512];
    if (build_shadow_line(rec, hash, line, sizeof line) != 0) {
        fprintf(stderr, "passwd: malformed shadow record\n"); free(rec); return 1;
    }
    free(rec);

    if (acct_edit_record(ACCT_SHADOW, target, line, 'r', 0600) != 0) {
        fprintf(stderr, "passwd: could not write /etc/shadow (need CAP_KIND_AUTH)\n");
        return 1;
    }
    printf("passwd: password updated for %s\n", target);
    return 0;
}
