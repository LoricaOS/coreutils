/* mv — move (rename) files and directories.
 *   mv [-f] [-i] [-v] SRC... DEST
 *
 * If DEST is a directory, each SRC is moved into it (mv file dir/ → dir/file).
 * Otherwise SRC (one) is renamed to DEST.  -f suppresses the overwrite prompt
 * (the default), -i prompts before overwriting, -v prints the move.
 *
 * Falls back to copy+unlink when rename() fails with EXDEV (cross-device) so
 * `mv /tmp/file /mnt/disk/` works instead of erroring.  Directory trees are
 * copied recursively on that path (cp -r style). */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

static int o_f = 1, o_i, o_v;

static const char *
base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void
join_path(char *out, size_t cap, const char *a, const char *b)
{
    size_t la = strlen(a);
    if (la && a[la - 1] == '/')
        snprintf(out, cap, "%s%s", a, b);
    else
        snprintf(out, cap, "%s/%s", a, b);
}

static int
is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ---- copy fallback (cross-device) ---- */

static int copy_any(const char *src, const char *dst);

static int
copy_reg(const char *src, const char *dst, const struct stat *st)
{
    int s = open(src, O_RDONLY);
    if (s < 0) { fprintf(stderr, "mv: %s: %s\n", src, strerror(errno)); return 1; }
    int d = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st->st_mode & 0777);
    if (d < 0) { fprintf(stderr, "mv: %s: %s\n", dst, strerror(errno)); close(s); return 1; }
    char buf[65536];
    ssize_t n;
    int rc = 0;
    while ((n = read(s, buf, sizeof buf)) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(d, buf + off, (size_t)(n - off));
            if (w <= 0) { rc = 1; break; }
            off += w;
        }
        if (rc) break;
    }
    if (n < 0) rc = 1;
    close(s);
    close(d);
    return rc;
}

static int
copy_dir(const char *src, const char *dst, const struct stat *st)
{
    if (mkdir(dst, st->st_mode & 0777) < 0 && errno != EEXIST) {
        fprintf(stderr, "mv: %s: %s\n", dst, strerror(errno));
        return 1;
    }
    DIR *dir = opendir(src);
    if (!dir) { fprintf(stderr, "mv: %s: %s\n", src, strerror(errno)); return 1; }
    struct dirent *de;
    int rc = 0;
    while ((de = readdir(dir))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        char sp[4096], dp[4096];
        join_path(sp, sizeof sp, src, de->d_name);
        join_path(dp, sizeof dp, dst, de->d_name);
        rc |= copy_any(sp, dp);
    }
    closedir(dir);
    return rc;
}

static int
copy_any(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(src, &st) != 0) {
        fprintf(stderr, "mv: %s: %s\n", src, strerror(errno));
        return 1;
    }
    if (S_ISLNK(st.st_mode)) {
        char tgt[4096];
        ssize_t n = readlink(src, tgt, sizeof tgt - 1);
        if (n < 0) { fprintf(stderr, "mv: %s: %s\n", src, strerror(errno)); return 1; }
        tgt[n] = '\0';
        unlink(dst);
        if (symlink(tgt, dst) != 0) { fprintf(stderr, "mv: %s: %s\n", dst, strerror(errno)); return 1; }
        return 0;
    }
    if (S_ISDIR(st.st_mode))
        return copy_dir(src, dst, &st);
    return copy_reg(src, dst, &st);
}

/* Recursively remove a directory tree (used after a successful copy to
 * complete the move). */
static int
rm_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return 0;
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir))) {
                if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                    continue;
                char cp[4096];
                join_path(cp, sizeof cp, path, de->d_name);
                rm_tree(cp);
            }
            closedir(dir);
        }
        rmdir(path);
    } else {
        unlink(path);
    }
    return 0;
}

/* Move src to dst, using copy+unlink if rename hits EXDEV. */
static int
do_move(const char *src, const char *dst)
{
    if (o_i && access(dst, F_OK) == 0) {
        fprintf(stderr, "mv: overwrite '%s'? ", dst);
        int c = getchar(), yes = (c == 'y' || c == 'Y');
        while (c != '\n' && c != EOF)
            c = getchar();
        if (!yes)
            return 0;
    }

    if (rename(src, dst) == 0) {
        if (o_v)
            printf("'%s' -> '%s'\n", src, dst);
        return 0;
    }

    /* EXDEV = cross-device; fall back to copy + remove. */
    if (errno != EXDEV) {
        fprintf(stderr, "mv: %s -> %s: %s\n", src, dst, strerror(errno));
        return 1;
    }

    if (copy_any(src, dst) != 0)
        return 1;
    rm_tree(src);
    if (o_v)
        printf("'%s' -> '%s'\n", src, dst);
    return 0;
}

int
main(int argc, char **argv)
{
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] && strcmp(argv[i], "--"); i++)
        for (const char *f = argv[i] + 1; *f; f++)
            switch (*f) {
            case 'f': o_f = 1; break;
            case 'i': o_i = 1; o_f = 0; break;
            case 'v': o_v = 1; break;
            default:
                fprintf(stderr, "mv: invalid option -- '%c'\n", *f);
                return 1;
            }
    if (i < argc && !strcmp(argv[i], "--"))
        i++;

    if (argc - i < 2) {
        fprintf(stderr, "usage: mv [-fiv] src... dest\n");
        return 1;
    }

    const char *dst = argv[argc - 1];
    int dstdir = is_dir(dst);

    /* Multiple sources require the destination to be a directory. */
    if (argc - i > 2 && !dstdir) {
        fprintf(stderr, "mv: target '%s' is not a directory\n", dst);
        return 1;
    }

    int rc = 0;
    for (; i < argc - 1; i++) {
        const char *src = argv[i];
        const char *t = dst;
        char path[4096];
        if (dstdir) {
            join_path(path, sizeof path, dst, base_name(src));
            t = path;
        }
        rc |= do_move(src, t);
    }
    return rc;
}