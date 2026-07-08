/* tar — create, list, and extract POSIX ustar archives (uncompressed).
 *   tar -c[v]f ARCHIVE FILE...   create
 *   tar -t[v]f ARCHIVE           list
 *   tar -x[v]f ARCHIVE           extract
 * No -z yet (pipe through gzip once it lands). Extraction rejects absolute and
 * ".." paths. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

typedef struct {
    char name[100], mode[8], uid[8], gid[8], size[12], mtime[12], chksum[8];
    char typeflag, linkname[100], magic[6], version[2], uname[32], gname[32];
    char devmajor[8], devminor[8], prefix[155], pad[12];
} ustar;                                          /* 512 bytes */

static int verbose;

static void octal(char *f, int w, unsigned long v) { snprintf(f, w, "%0*lo", w - 1, v); }

static void
set_checksum(ustar *h)
{
    memset(h->chksum, ' ', 8);
    unsigned sum = 0;
    unsigned char *p = (unsigned char *)h;
    for (int i = 0; i < 512; i++) sum += p[i];
    snprintf(h->chksum, 7, "%06o", sum);
    h->chksum[6] = 0; h->chksum[7] = ' ';
}

static int add_path(int afd, const char *path);

static int
add_one(int afd, const char *path, struct stat *st)
{
    ustar h; memset(&h, 0, sizeof h);
    snprintf(h.name, sizeof h.name, "%s%s", path, S_ISDIR(st->st_mode) ? "/" : "");
    octal(h.mode, 8, st->st_mode & 07777);
    octal(h.uid, 8, st->st_uid);
    octal(h.gid, 8, st->st_gid);
    octal(h.mtime, 12, (unsigned long)st->st_mtime);
    memcpy(h.magic, "ustar", 5); h.version[0] = '0'; h.version[1] = '0';
    if (S_ISDIR(st->st_mode)) { h.typeflag = '5'; octal(h.size, 12, 0); }
    else if (S_ISLNK(st->st_mode)) { h.typeflag = '2'; octal(h.size, 12, 0);
        ssize_t n = readlink(path, h.linkname, sizeof h.linkname - 1); if (n < 0) n = 0; h.linkname[n] = 0; }
    else { h.typeflag = '0'; octal(h.size, 12, (unsigned long)st->st_size); }
    set_checksum(&h);
    if (write(afd, &h, 512) != 512) return 1;
    if (verbose) fprintf(stderr, "%s\n", h.name);

    if (h.typeflag == '0') {
        int fd = open(path, O_RDONLY);
        if (fd < 0) { perror(path); return 1; }
        char buf[65536]; ssize_t r; unsigned long total = 0;
        while ((r = read(fd, buf, sizeof buf)) > 0) { write(afd, buf, (size_t)r); total += (unsigned long)r; }
        close(fd);
        unsigned long rem = total % 512;
        if (rem) { char z[512] = {0}; write(afd, z, 512 - rem); }
    }
    return 0;
}

static int
add_path(int afd, const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) { perror(path); return 1; }
    int rc = add_one(afd, path, &st);
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return 1;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char c[4096]; snprintf(c, sizeof c, "%s/%s", path, e->d_name);
            rc |= add_path(afd, c);
        }
        closedir(d);
    }
    return rc;
}

static unsigned long
from_octal(const char *s, int n)
{
    unsigned long v = 0;
    for (int i = 0; i < n && s[i]; i++) { if (s[i] < '0' || s[i] > '7') break; v = v * 8 + (s[i] - '0'); }
    return v;
}

static void
mkdirs(const char *path)
{
    char tmp[4096]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
}

static int is_safe(const char *n) { return n[0] != '/' && !strstr(n, ".."); }

static int
read_archive(int afd, int extract)
{
    ustar h;
    for (;;) {
        if (read(afd, &h, 512) != 512) break;
        if (h.name[0] == 0) break;                /* zero block = end of archive */
        unsigned long size = from_octal(h.size, 12);
        unsigned long blocks = ((size + 511) / 512) * 512;

        if (!extract) printf("%s\n", h.name);
        else if (verbose) fprintf(stderr, "%s\n", h.name);

        if (extract) {
            if (!is_safe(h.name)) { fprintf(stderr, "tar: unsafe path skipped: %s\n", h.name); }
            else if (h.typeflag == '5') { mkdirs(h.name); mkdir(h.name, 0755); }
            else if (h.typeflag == '2') { mkdirs(h.name); unlink(h.name); symlink(h.linkname, h.name); }
            else {
                mkdirs(h.name);
                int fd = open(h.name, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)(from_octal(h.mode, 8) & 0777));
                unsigned long left = size; char buf[65536];
                while (left) { size_t k = left < sizeof buf ? left : sizeof buf; ssize_t g = read(afd, buf, k); if (g <= 0) break; if (fd >= 0) write(fd, buf, (size_t)g); left -= (unsigned long)g; }
                if (fd >= 0) close(fd);
                unsigned long rem = size % 512;
                if (rem) { char z[512]; if (read(afd, z, 512 - rem) < 0) break; }
                continue;                         /* file data already consumed */
            }
        }
        char z[512];                              /* skip data blocks (list mode / size==0) */
        while (blocks) { ssize_t g = read(afd, z, blocks < 512 ? blocks : 512); if (g <= 0) break; blocks -= (unsigned long)g; }
    }
    return 0;
}

int
main(int argc, char **argv)
{
    int mode = 0, i = 1;
    const char *archive = NULL;
    char *flags = (i < argc) ? argv[i++] : (char *)"";
    if (flags[0] == '-') flags++;
    for (char *p = flags; *p; p++)
        switch (*p) {
        case 'c': mode = 'c'; break;
        case 'x': mode = 'x'; break;
        case 't': mode = 't'; break;
        case 'v': verbose = 1; break;
        case 'f': if (i < argc) archive = argv[i++]; break;
        case 'z': fprintf(stderr, "tar: -z not supported yet (pipe through gzip)\n"); return 1;
        default: fprintf(stderr, "tar: unknown option -%c\n", *p); return 1;
        }
    if (!mode) { fprintf(stderr, "usage: tar -c|-x|-t [-v] -f ARCHIVE [FILE...]\n"); return 1; }

    if (mode == 'c') {
        int afd = (archive && strcmp(archive, "-")) ? open(archive, O_WRONLY | O_CREAT | O_TRUNC, 0644) : 1;
        if (afd < 0) { perror(archive); return 1; }
        int rc = 0;
        for (; i < argc; i++) rc |= add_path(afd, argv[i]);
        char z[1024] = {0}; write(afd, z, 1024);  /* two zero blocks terminate the archive */
        if (afd != 1) close(afd);
        return rc;
    }
    int afd = (archive && strcmp(archive, "-")) ? open(archive, O_RDONLY) : 0;
    if (afd < 0) { perror(archive); return 1; }
    int rc = read_archive(afd, mode == 'x');
    if (afd != 0) close(afd);
    return rc;
}
