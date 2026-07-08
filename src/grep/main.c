/* grep — print lines matching a pattern.
 *   grep [-E|-F] [-i] [-v] [-n] [-c] [-l] [-q] [-o] [-w] [-r|-R] [-H|-h]
 *        [-e PATTERN]... [-f FILE] [PATTERN] [FILE...]
 *
 * Real POSIX regex via regcomp (BRE by default, ERE with -E; -F = fixed
 * strings). The old grep was a literal substring scan that also mis-parsed
 * flags — `grep -i foo` searched for the string "-i". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <regex.h>
#include <dirent.h>
#include <sys/stat.h>

static int o_E, o_F, o_i, o_v, o_n, o_c, o_l, o_q, o_o, o_w, o_r, o_H, o_h;
static int g_show;                 /* prefix output lines with the filename */
static int g_found;                /* any line matched anywhere */

struct pat { int is_re; regex_t re; char *lit; };
static struct pat *pats;
static int npats;
static char **raw;                 /* pattern strings, compiled after flags parse */
static int nraw;

static void addraw(const char *s) { raw = realloc(raw, (nraw + 1) * sizeof *raw); raw[nraw++] = strdup(s); }

static void
add_pattern(const char *s)
{
    pats = realloc(pats, (npats + 1) * sizeof *pats);
    struct pat *p = &pats[npats++];
    p->lit = NULL; p->is_re = 0;
    if (o_F) { p->lit = strdup(s); return; }
    int fl = REG_NEWLINE | (o_E ? REG_EXTENDED : 0) | (o_i ? REG_ICASE : 0);
    if (regcomp(&p->re, s, fl) != 0) { fprintf(stderr, "grep: invalid pattern: %s\n", s); exit(2); }
    p->is_re = 1;
}

static const char *
ci_strstr(const char *h, const char *n)
{
    size_t nl = strlen(n);
    if (!nl) return h;
    for (; *h; h++) if (strncasecmp(h, n, nl) == 0) return h;
    return NULL;
}

/* First match of pattern P in S at or after offset OFF; fills ms and me. */
static int
pat_at(struct pat *p, const char *s, int off, int *ms, int *me)
{
    if (p->is_re) {
        regmatch_t m;
        if (regexec(&p->re, s + off, 1, &m, off ? REG_NOTBOL : 0) != 0) return 0;
        *ms = off + (int)m.rm_so; *me = off + (int)m.rm_eo; return 1;
    }
    const char *f = o_i ? ci_strstr(s + off, p->lit) : strstr(s + off, p->lit);
    if (!f) return 0;
    *ms = (int)(f - s); *me = *ms + (int)strlen(p->lit); return 1;
}

static int
wbound(const char *s, int ms, int me)
{
    int b = (ms == 0) ? 0 : (unsigned char)s[ms - 1];
    int a = (unsigned char)s[me];
    int okb = (ms == 0) || !(isalnum(b) || b == '_');
    int oka = (s[me] == '\0') || !(isalnum(a) || a == '_');
    return okb && oka;
}

/* Leftmost match across all patterns from OFF (respecting -w). */
static int
leftmost(const char *line, int off, int *ms, int *me)
{
    int best = -1, bs = 0, be = 0;
    for (int i = 0; i < npats; i++) {
        int o = off, s, e;
        while (pat_at(&pats[i], line, o, &s, &e)) {
            if (!o_w || wbound(line, s, e)) { if (best < 0 || s < best) { best = s; bs = s; be = e; } break; }
            o = (e > s) ? e : s + 1;
            if ((size_t)o > strlen(line)) break;
        }
    }
    if (best < 0) return 0;
    *ms = bs; *me = be; return 1;
}

static void
grep_stream(FILE *f, const char *name)
{
    char *buf = NULL; size_t bcap = 0; ssize_t len;
    long lineno = 0, count = 0;
    while ((len = getline(&buf, &bcap, f)) != -1) {
        lineno++;
        if (len > 0 && buf[len - 1] == '\n') buf[--len] = 0;
        int ms, me, m = leftmost(buf, 0, &ms, &me);
        if (o_v) m = !m;
        if (!m) continue;
        g_found = 1; count++;
        if (o_q) exit(0);
        if (o_l) { printf("%s\n", name); break; }
        if (o_c) continue;
        if (o_o && !o_v) {
            int off = 0, s, e;
            while (leftmost(buf, off, &s, &e)) {
                if (g_show) printf("%s:", name);
                if (o_n) printf("%ld:", lineno);
                fwrite(buf + s, 1, (size_t)(e - s), stdout); putchar('\n');
                off = (e > s) ? e : s + 1;
                if ((size_t)off > strlen(buf)) break;
            }
            continue;
        }
        if (g_show) printf("%s:", name);
        if (o_n) printf("%ld:", lineno);
        printf("%s\n", buf);
    }
    if (o_c && !o_l) { if (g_show) printf("%s:", name); printf("%ld\n", count); }
    free(buf);
}

static void grep_path(const char *path);

static void
grep_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) { perror(path); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char c[4096];
        snprintf(c, sizeof c, "%s/%s", path, e->d_name);
        grep_path(c);
    }
    closedir(d);
}

static void
grep_path(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (o_r) grep_dir(path);
        else fprintf(stderr, "grep: %s: Is a directory\n", path);
        return;
    }
    FILE *f = strcmp(path, "-") ? fopen(path, "r") : stdin;
    if (!f) { perror(path); return; }
    grep_stream(f, path);
    if (f != stdin) fclose(f);
}

int
main(int argc, char **argv)
{
    int need_pat = 1, i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0') break;
        if (!strcmp(a, "--")) { i++; break; }
        if (!strcmp(a, "-e")) { if (++i >= argc) return 2; addraw(argv[i]); need_pat = 0; continue; }
        if (!strcmp(a, "-f")) {
            if (++i >= argc) return 2;
            FILE *pf = fopen(argv[i], "r");
            if (!pf) { perror(argv[i]); return 2; }
            char *l = NULL; size_t c = 0; ssize_t n;
            while ((n = getline(&l, &c, pf)) != -1) { if (n && l[n - 1] == '\n') l[n - 1] = 0; addraw(l); }
            free(l); fclose(pf); need_pat = 0; continue;
        }
        for (const char *p = a + 1; *p; p++)
            switch (*p) {
            case 'E': o_E = 1; break; case 'F': o_F = 1; break; case 'i': o_i = 1; break;
            case 'v': o_v = 1; break; case 'n': o_n = 1; break; case 'c': o_c = 1; break;
            case 'l': o_l = 1; break; case 'q': o_q = 1; break; case 'o': o_o = 1; break;
            case 'w': o_w = 1; break; case 'r': case 'R': o_r = 1; break;
            case 'H': o_H = 1; break; case 'h': o_h = 1; break;
            default: fprintf(stderr, "grep: invalid option -- '%c'\n", *p); return 2;
            }
    }
    if (need_pat) {
        if (i >= argc) { fprintf(stderr, "usage: grep [OPTIONS] PATTERN [FILE...]\n"); return 2; }
        addraw(argv[i++]);
    }
    if (nraw == 0) { fprintf(stderr, "grep: no pattern\n"); return 2; }
    for (int k = 0; k < nraw; k++) add_pattern(raw[k]);

    int nf = argc - i;
    char **files = argv + i;
    g_show = !o_h && (o_H || nf > 1 || o_r);
    if (nf == 0) grep_stream(stdin, "(standard input)");
    else for (int k = 0; k < nf; k++) grep_path(files[k]);

    return g_found ? 0 : 1;
}
