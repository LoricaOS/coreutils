/* cut — select fields or characters from each line.
 *   cut -f LIST [-d DELIM] [-s] [FILE...]
 *   cut -c LIST [FILE...]
 * LIST is comma-separated numbers and ranges (1-based): N, N-M, N-, -M. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXR 256
static struct { int lo, hi; } ranges[MAXR];   /* hi < 0 = open-ended */
static int nranges;
static int mode_c;                            /* 1 = -c (chars), else -f (fields) */
static char delim = '\t';
static int suppress;                          /* -s: drop lines with no delimiter */

static void
parse_list(char *spec)
{
    for (char *tok = strtok(spec, ","); tok; tok = strtok(NULL, ",")) {
        int lo, hi;
        char *dash = strchr(tok, '-');
        if (!dash) { lo = hi = atoi(tok); }
        else if (dash == tok) { lo = 1; hi = atoi(dash + 1); }
        else if (dash[1] == '\0') { lo = atoi(tok); hi = -1; }
        else { *dash = 0; lo = atoi(tok); hi = atoi(dash + 1); }
        if (nranges < MAXR) { ranges[nranges].lo = lo; ranges[nranges].hi = hi; nranges++; }
    }
}

static int
selected(int n)
{
    for (int i = 0; i < nranges; i++)
        if (n >= ranges[i].lo && (ranges[i].hi < 0 || n <= ranges[i].hi)) return 1;
    return 0;
}

static void
cut_line(char *line)
{
    if (mode_c) {
        int len = (int)strlen(line);
        for (int i = 1; i <= len; i++) if (selected(i)) putchar(line[i - 1]);
        putchar('\n');
        return;
    }
    if (!strchr(line, delim)) {               /* no delimiter present */
        if (!suppress) { fputs(line, stdout); putchar('\n'); }
        return;
    }
    int field = 1, out = 0;
    char *start = line;
    for (char *p = line;; p++) {
        if (*p == delim || *p == '\0') {
            if (selected(field)) {
                if (out++) putchar(delim);
                fwrite(start, 1, (size_t)(p - start), stdout);
            }
            field++;
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    putchar('\n');
}

int
main(int argc, char **argv)
{
    int i = 1, got = 0;
    for (; i < argc; i++) {
        char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0') break;
        if (!strcmp(a, "--")) { i++; break; }
        if (!strcmp(a, "-d")) { if (++i >= argc) return 1; delim = argv[i][0]; }
        else if (!strncmp(a, "-d", 2)) delim = a[2];
        else if (!strcmp(a, "-s")) suppress = 1;
        else if (!strcmp(a, "-f")) { mode_c = 0; if (++i >= argc) return 1; parse_list(argv[i]); got = 1; }
        else if (!strncmp(a, "-f", 2)) { mode_c = 0; parse_list(a + 2); got = 1; }
        else if (!strcmp(a, "-c")) { mode_c = 1; if (++i >= argc) return 1; parse_list(argv[i]); got = 1; }
        else if (!strncmp(a, "-c", 2)) { mode_c = 1; parse_list(a + 2); got = 1; }
        else { fprintf(stderr, "cut: invalid option %s\n", a); return 1; }
    }
    if (!got) { fprintf(stderr, "usage: cut (-f LIST [-d C] [-s] | -c LIST) [FILE...]\n"); return 1; }

    char *buf = NULL; size_t bcap = 0; ssize_t len;
    do {
        FILE *f = (i < argc) ? (strcmp(argv[i], "-") ? fopen(argv[i], "r") : stdin) : stdin;
        if (!f) { perror(argv[i]); return 1; }
        while ((len = getline(&buf, &bcap, f)) != -1) {
            if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = 0;
            cut_line(buf);
        }
        if (f != stdin) fclose(f);
        i++;
    } while (i < argc);
    free(buf);
    return 0;
}
