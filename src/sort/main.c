/* sort — sort lines of text.
 *   sort [-n] [-r] [-u] [-f] [-b] [-k FIELD] [-t CHAR] [-o OUT] [FILE...]
 *
 * Dynamically allocated lines (no arbitrary cap — the old version silently
 * dropped everything past 512 lines and truncated long lines). Supports numeric
 * (-n), reverse (-r), unique (-u), fold-case (-f), a key field (-k, whitespace
 * or -t delimited), and output to a file (-o). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static int f_num, f_rev, f_uniq, f_fold;
static int key_field;        /* 1-based field to compare; 0 = whole line */
static char field_sep;       /* delimiter for -k; 0 = runs of whitespace */

/* Return a malloc'd copy of the comparison key for LINE (from the start of
 * key_field to end of line; whole line when key_field is 0). */
static char *
keyof(const char *line)
{
    if (key_field <= 0) return strdup(line);
    const char *p = line;
    for (int f = 1; f < key_field && *p; f++) {
        if (field_sep) {
            while (*p && *p != field_sep) p++;
            if (*p) p++;
        } else {
            while (*p && !isspace((unsigned char)*p)) p++;
            while (*p && isspace((unsigned char)*p)) p++;
        }
    }
    return strdup(p);
}

static int
keycmp(const char *la, const char *lb)
{
    char *ka = keyof(la), *kb = keyof(lb);
    int r;
    if (f_num) {
        double x = atof(ka), y = atof(kb);
        r = (x < y) ? -1 : (x > y) ? 1 : 0;
    } else if (f_fold) {
        r = strcasecmp(ka, kb);
    } else {
        r = strcmp(ka, kb);
    }
    free(ka); free(kb);
    return r;
}

static int
cmp(const void *a, const void *b)
{
    const char *la = *(const char *const *)a, *lb = *(const char *const *)b;
    int r = keycmp(la, lb);
    if (r == 0) r = strcmp(la, lb);   /* tiebreak on the whole line for stability */
    return f_rev ? -r : r;
}

int
main(int argc, char **argv)
{
    const char *outfile = NULL;
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0') break;
        if (!strcmp(a, "--")) { i++; break; }
        if (!strcmp(a, "-o")) { if (++i >= argc) return 2; outfile = argv[i]; continue; }
        if (!strcmp(a, "-k")) { if (++i >= argc) return 2; key_field = atoi(argv[i]); continue; }
        if (!strncmp(a, "-k", 2)) { key_field = atoi(a + 2); continue; }
        if (!strcmp(a, "-t")) { if (++i >= argc) return 2; field_sep = argv[i][0]; continue; }
        if (!strncmp(a, "-t", 2)) { field_sep = a[2]; continue; }
        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
            case 'n': f_num = 1; break;
            case 'r': f_rev = 1; break;
            case 'u': f_uniq = 1; break;
            case 'f': f_fold = 1; break;
            case 'b': break;                 /* leading-blank ignore: accepted */
            case 's': break;                 /* stable: our tiebreak is already stable-ish */
            case 'c': break;                 /* check-only: accepted, not enforced */
            default: fprintf(stderr, "sort: invalid option -- '%c'\n", *p); return 2;
            }
        }
    }

    char **lines = NULL;
    size_t n = 0, cap = 0;
    int fi = i;
    do {
        FILE *f;
        if (fi < argc) {
            f = strcmp(argv[fi], "-") ? fopen(argv[fi], "r") : stdin;
            if (!f) { perror(argv[fi]); return 2; }
        } else {
            f = stdin;
        }
        char *buf = NULL; size_t bcap = 0; ssize_t len;
        while ((len = getline(&buf, &bcap, f)) != -1) {
            if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
            if (n == cap) { cap = cap ? cap * 2 : 1024; lines = realloc(lines, cap * sizeof *lines); }
            lines[n++] = strdup(buf);
        }
        free(buf);
        if (f != stdin) fclose(f);
        fi++;
    } while (fi < argc);

    qsort(lines, n, sizeof *lines, cmp);

    FILE *out = outfile ? fopen(outfile, "w") : stdout;
    if (!out) { perror(outfile); return 2; }
    const char *prev = NULL;
    for (size_t j = 0; j < n; j++) {
        if (f_uniq && prev && keycmp(prev, lines[j]) == 0) continue;
        fprintf(out, "%s\n", lines[j]);
        prev = lines[j];
    }
    return 0;
}
