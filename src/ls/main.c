/* ls — list directory contents.
 *   ls [-a|-A] [-l] [-h] [-1] [-F] [-R] [-d] [-t] [-S] [-r] [--color[=WHEN]] [FILE...]
 *
 * Entries are collected and SORTED (the old ls printed raw readdir order and
 * only understood -l). Dotfiles are hidden unless -a/-A. Short output lays out
 * vertical columns to the terminal width. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

static int o_a, o_A, o_l, o_h, o_1, o_F, o_R, o_d, o_t, o_S, o_r, o_color;

struct ent { char *name; char *path; struct stat st; };

static void
mode_string(unsigned m, char out[11])
{
    static const char rwx[] = "rwxrwxrwx";
    if      (S_ISDIR(m))  out[0] = 'd';
    else if (S_ISLNK(m))  out[0] = 'l';
    else if (S_ISCHR(m))  out[0] = 'c';
    else if (S_ISBLK(m))  out[0] = 'b';
    else if (S_ISFIFO(m)) out[0] = 'p';
    else if (S_ISSOCK(m)) out[0] = 's';
    else                  out[0] = '-';
    for (int i = 0; i < 9; i++) out[i + 1] = (m & (1u << (8 - i))) ? rwx[i] : '-';
    if (m & S_ISUID) out[3] = (out[3] == 'x') ? 's' : 'S';
    if (m & S_ISGID) out[6] = (out[6] == 'x') ? 's' : 'S';
    if (m & S_ISVTX) out[9] = (out[9] == 'x') ? 't' : 'T';
    out[10] = '\0';
}

static void
human(long long n, char *out, size_t sz)
{
    if (!o_h) { snprintf(out, sz, "%lld", n); return; }
    static const char u[] = "BKMGTP";
    double d = (double)n;
    int i = 0;
    while (d >= 1024 && i < 5) { d /= 1024; i++; }
    if (i == 0) snprintf(out, sz, "%lld", n);
    else if (d < 10) snprintf(out, sz, "%.1f%c", d, u[i]);
    else snprintf(out, sz, "%.0f%c", d, u[i]);
}

static char
type_suffix(const struct stat *st)
{
    if (S_ISDIR(st->st_mode)) return '/';
    if (S_ISLNK(st->st_mode)) return '@';
    if (S_ISFIFO(st->st_mode)) return '|';
    if (S_ISSOCK(st->st_mode)) return '=';
    if (st->st_mode & 0111) return '*';
    return 0;
}

static const char *
color_of(const struct stat *st)
{
    if (!o_color) return NULL;
    if (S_ISDIR(st->st_mode)) return "\033[1;34m";
    if (S_ISLNK(st->st_mode)) return "\033[1;36m";
    if (S_ISFIFO(st->st_mode) || S_ISSOCK(st->st_mode)) return "\033[33m";
    if (st->st_mode & 0111) return "\033[1;32m";
    return NULL;
}

static void
put_name(const struct ent *e)
{
    const char *c = color_of(&e->st);
    if (c) fputs(c, stdout);
    fputs(e->name, stdout);
    if (c) fputs("\033[0m", stdout);
    if (o_F) { char s = type_suffix(&e->st); if (s) putchar(s); }
}

static int
cmp(const void *a, const void *b)
{
    const struct ent *x = a, *y = b;
    int r;
    if (o_t) r = (x->st.st_mtime < y->st.st_mtime) - (x->st.st_mtime > y->st.st_mtime);
    else if (o_S) r = (x->st.st_size < y->st.st_size) - (x->st.st_size > y->st.st_size);
    else r = strcmp(x->name, y->name);
    return o_r ? -r : r;
}

static void
print_long(const struct ent *e)
{
    char mode[11], sz[24], tbuf[32], ubuf[16], gbuf[16];
    mode_string((unsigned)e->st.st_mode, mode);
    human((long long)e->st.st_size, sz, sizeof sz);
    struct tm tm; time_t now = time(NULL);
    gmtime_r(&e->st.st_mtime, &tm);
    if (e->st.st_mtime > now + 60 || now - e->st.st_mtime > 15552000L)
        strftime(tbuf, sizeof tbuf, "%b %e  %Y", &tm);
    else strftime(tbuf, sizeof tbuf, "%b %e %H:%M", &tm);
    struct passwd *pw = getpwuid(e->st.st_uid);
    struct group  *gr = getgrgid(e->st.st_gid);
    if (pw) snprintf(ubuf, sizeof ubuf, "%s", pw->pw_name); else snprintf(ubuf, sizeof ubuf, "%u", (unsigned)e->st.st_uid);
    if (gr) snprintf(gbuf, sizeof gbuf, "%s", gr->gr_name); else snprintf(gbuf, sizeof gbuf, "%u", (unsigned)e->st.st_gid);
    printf("%s %2lu %-8s %-8s %6s %s ", mode, (unsigned long)e->st.st_nlink, ubuf, gbuf, sz, tbuf);
    put_name(e);
    if (S_ISLNK(e->st.st_mode)) {
        char target[512];
        ssize_t n = readlink(e->path, target, sizeof target - 1);
        if (n > 0) { target[n] = 0; printf(" -> %s", target); }
    }
    putchar('\n');
}

static void
print_columns(struct ent *es, size_t n)
{
    if (o_1 || o_l || !isatty(1)) {
        for (size_t i = 0; i < n; i++) { put_name(&es[i]); putchar('\n'); }
        return;
    }
    size_t maxlen = 1;
    for (size_t i = 0; i < n; i++) { size_t l = strlen(es[i].name) + (o_F ? 1 : 0); if (l > maxlen) maxlen = l; }
    struct winsize ws;
    int width = (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col) ? ws.ws_col : 80;
    size_t colw = maxlen + 2;
    size_t cols = width / colw; if (cols < 1) cols = 1;
    size_t rows = (n + cols - 1) / cols;
    for (size_t rr = 0; rr < rows; rr++) {
        for (size_t cc = 0; cc < cols; cc++) {
            size_t idx = cc * rows + rr;
            if (idx >= n) continue;
            put_name(&es[idx]);
            if (cc + 1 < cols && idx + rows < n) {
                size_t pad = colw - (strlen(es[idx].name) + (o_F && type_suffix(&es[idx].st) ? 1 : 0));
                for (size_t s = 0; s < pad; s++) putchar(' ');
            }
        }
        putchar('\n');
    }
}

static void list_dir(const char *path, int header);

static void
emit(struct ent *es, size_t n, const char *dirpath)
{
    qsort(es, n, sizeof *es, cmp);
    if (o_l) {
        long long blocks = 0;
        for (size_t i = 0; i < n; i++) blocks += es[i].st.st_blocks;
        printf("total %lld\n", blocks / 2);
        for (size_t i = 0; i < n; i++) print_long(&es[i]);
    } else {
        print_columns(es, n);
    }
    if (o_R && dirpath) {
        for (size_t i = 0; i < n; i++)
            if (S_ISDIR(es[i].st.st_mode) && strcmp(es[i].name, ".") && strcmp(es[i].name, ".."))
                { putchar('\n'); list_dir(es[i].path, 1); }
    }
}

static void
list_dir(const char *path, int header)
{
    DIR *d = opendir(path);
    if (!d) { perror(path); return; }
    if (header) printf("%s:\n", path);
    struct ent *es = NULL; size_t n = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        const char *nm = de->d_name;
        if (nm[0] == '.') { if (!o_a && !o_A) continue; if (o_A && (!strcmp(nm, ".") || !strcmp(nm, ".."))) continue; }
        if (n == cap) { cap = cap ? cap * 2 : 64; es = realloc(es, cap * sizeof *es); }
        es[n].name = strdup(nm);
        char full[4096]; snprintf(full, sizeof full, "%s/%s", path, nm);
        es[n].path = strdup(full);
        if (lstat(full, &es[n].st) != 0) memset(&es[n].st, 0, sizeof es[n].st);
        n++;
    }
    closedir(d);
    emit(es, n, path);
    for (size_t i = 0; i < n; i++) { free(es[i].name); free(es[i].path); }
    free(es);
}

int
main(int argc, char **argv)
{
    char **paths = NULL; int np = 0;
    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (!strcmp(a, "--color") || !strcmp(a, "--color=always") || !strcmp(a, "--color=auto")) { o_color = isatty(1) || strstr(a, "always"); continue; }
        if (!strcmp(a, "--color=never")) { o_color = 0; continue; }
        if (a[0] == '-' && a[1] && strcmp(a, "--")) {
            for (char *p = a + 1; *p; p++)
                switch (*p) {
                case 'a': o_a = 1; break; case 'A': o_A = 1; break;
                case 'l': o_l = 1; break; case 'h': o_h = 1; break;
                case '1': o_1 = 1; break; case 'F': o_F = 1; break;
                case 'R': o_R = 1; break; case 'd': o_d = 1; break;
                case 't': o_t = 1; break; case 'S': o_S = 1; break;
                case 'r': o_r = 1; break;
                default: fprintf(stderr, "ls: invalid option -- '%c'\n", *p); return 2;
                }
            continue;
        }
        paths = realloc(paths, (np + 1) * sizeof *paths);
        paths[np++] = a;
    }
    if (np == 0) { static char *dot[] = { "." }; paths = dot; np = 1; }

    /* Split into non-dir operands (listed together) and dir operands. */
    struct ent *files = NULL; size_t nf = 0;
    int rc = 0;
    for (int i = 0; i < np; i++) {
        struct stat st;
        if (lstat(paths[i], &st) != 0) { perror(paths[i]); rc = 2; continue; }
        if (S_ISDIR(st.st_mode) && !o_d) continue;   /* handled below */
        files = realloc(files, (nf + 1) * sizeof *files);
        files[nf].name = paths[i]; files[nf].path = paths[i]; files[nf].st = st; nf++;
    }
    if (nf) emit(files, nf, NULL);
    free(files);

    int printed_dir = 0;
    for (int i = 0; i < np; i++) {
        struct stat st;
        if (lstat(paths[i], &st) != 0) continue;
        if (!S_ISDIR(st.st_mode) || o_d) continue;
        if (nf || printed_dir) putchar('\n');
        list_dir(paths[i], np > 1);
        printed_dir = 1;
    }
    return rc;
}
