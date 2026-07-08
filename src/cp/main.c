/* cp — copy files and directories.
 *   cp [-r|-R] [-a] [-p] [-d] [-f] [-i] [-v] SRC... DEST
 *
 * -r/-R recurse, -p preserve mode/timestamps/owner, -a archive (= -rp + keep
 * symlinks as symlinks), -d keep symlinks, -f force-overwrite, -i prompt,
 * -v verbose. The old cp followed symlinks and could not preserve metadata. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

static int o_r, o_p, o_f, o_i, o_v, o_sym;

static const char *base_name(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

static void
join_path(char *out, size_t cap, const char *a, const char *b)
{
    size_t la = strlen(a);
    if (la && a[la - 1] == '/') snprintf(out, cap, "%s%s", a, b);
    else snprintf(out, cap, "%s/%s", a, b);
}

static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }

static void
preserve(const char *dst, const struct stat *st)
{
    if (!o_p) return;
    chown(dst, st->st_uid, st->st_gid);          /* best-effort (may lack privilege) */
    chmod(dst, st->st_mode & 07777);
    struct timespec ts[2] = {
        { st->st_atim.tv_sec, st->st_atim.tv_nsec },
        { st->st_mtim.tv_sec, st->st_mtim.tv_nsec },
    };
    utimensat(AT_FDCWD, dst, ts, 0);
}

static int
copy_reg(const char *src, const char *dst, const struct stat *st)
{
    int s = open(src, O_RDONLY);
    if (s < 0) { fprintf(stderr, "cp: %s: %s\n", src, strerror(errno)); return 1; }
    int d = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st->st_mode & 0777);
    if (d < 0 && o_f) { unlink(dst); d = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st->st_mode & 0777); }
    if (d < 0) { fprintf(stderr, "cp: %s: %s\n", dst, strerror(errno)); close(s); return 1; }
    char buf[65536]; ssize_t n; int rc = 0;
    while ((n = read(s, buf, sizeof buf)) > 0) {
        ssize_t off = 0;
        while (off < n) { ssize_t w = write(d, buf + off, (size_t)(n - off)); if (w <= 0) { rc = 1; break; } off += w; }
        if (rc) break;
    }
    if (n < 0) rc = 1;
    close(s); close(d);
    if (!rc) { preserve(dst, st); if (o_v) printf("'%s' -> '%s'\n", src, dst); }
    return rc;
}

static int copy_any(const char *src, const char *dst);

static int
copy_dir(const char *src, const char *dst, const struct stat *st)
{
    if (mkdir(dst, st->st_mode & 0777) < 0 && errno != EEXIST) {
        fprintf(stderr, "cp: %s: %s\n", dst, strerror(errno)); return 1;
    }
    DIR *dir = opendir(src);
    if (!dir) { fprintf(stderr, "cp: %s: %s\n", src, strerror(errno)); return 1; }
    struct dirent *de; int rc = 0;
    while ((de = readdir(dir))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char sp[4096], dp[4096];
        join_path(sp, sizeof sp, src, de->d_name);
        join_path(dp, sizeof dp, dst, de->d_name);
        rc |= copy_any(sp, dp);
    }
    closedir(dir);
    if (!rc) preserve(dst, st);
    return rc;
}

static int
copy_any(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(src, &st) != 0) { fprintf(stderr, "cp: %s: %s\n", src, strerror(errno)); return 1; }

    if (o_i && access(dst, F_OK) == 0) {
        fprintf(stderr, "cp: overwrite '%s'? ", dst);
        int c = getchar(), yes = (c == 'y' || c == 'Y');
        while (c != '\n' && c != EOF) c = getchar();
        if (!yes) return 0;
    }

    if (S_ISLNK(st.st_mode) && o_sym) {
        char tgt[4096];
        ssize_t n = readlink(src, tgt, sizeof tgt - 1);
        if (n < 0) { fprintf(stderr, "cp: %s: %s\n", src, strerror(errno)); return 1; }
        tgt[n] = 0;
        unlink(dst);
        if (symlink(tgt, dst) != 0) { fprintf(stderr, "cp: %s: %s\n", dst, strerror(errno)); return 1; }
        if (o_v) printf("'%s' -> '%s'\n", src, dst);
        return 0;
    }
    if (S_ISDIR(st.st_mode)) {
        if (!o_r) { fprintf(stderr, "cp: -r not specified; omitting directory '%s'\n", src); return 1; }
        return copy_dir(src, dst, &st);
    }
    struct stat rst;                              /* follow symlink for the real file's mode */
    if (stat(src, &rst) == 0) st = rst;
    return copy_reg(src, dst, &st);
}

int
main(int argc, char **argv)
{
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] && strcmp(argv[i], "--"); i++)
        for (const char *f = argv[i] + 1; *f; f++)
            switch (*f) {
            case 'r': case 'R': o_r = 1; break;
            case 'a': o_r = o_p = o_sym = 1; break;
            case 'p': o_p = 1; break;
            case 'd': o_sym = 1; break;
            case 'f': o_f = 1; break;
            case 'i': o_i = 1; break;
            case 'v': o_v = 1; break;
            default: fprintf(stderr, "cp: invalid option -- '%c'\n", *f); return 1;
            }
    if (i < argc && !strcmp(argv[i], "--")) i++;

    if (argc - i < 2) { fprintf(stderr, "usage: cp [-raRpdfiv] src... dest\n"); return 1; }
    const char *dst = argv[argc - 1];
    int dstdir = is_dir(dst);
    if ((argc - i) > 2 && !dstdir) { fprintf(stderr, "cp: target '%s' is not a directory\n", dst); return 1; }

    int rc = 0;
    for (; i < argc - 1; i++) {
        const char *src = argv[i], *t = dst;
        char path[4096];
        if (dstdir) { join_path(path, sizeof path, dst, base_name(src)); t = path; }
        rc |= copy_any(src, t);
    }
    return rc;
}
