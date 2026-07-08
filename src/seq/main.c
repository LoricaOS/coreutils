/* seq — print a sequence of numbers.  seq [-s SEP] [-w] [FIRST [INCR]] LAST
 *
 * Integers print as integers; any non-integral operand switches to %g output.
 * -s sets the separator (default newline), -w zero-pads to equal width. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int integral(double d) { return d == (double)(long long)d; }

int
main(int argc, char **argv)
{
    const char *sep = "\n";
    int wflag = 0, i = 1;

    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0') break;
        if (a[1] == '.' || (a[1] >= '0' && a[1] <= '9')) break; /* a negative operand */
        if (!strcmp(a, "--")) { i++; break; }
        if (!strcmp(a, "-w")) wflag = 1;
        else if (!strcmp(a, "-s")) { if (++i >= argc) return 1; sep = argv[i]; }
        else if (!strncmp(a, "-s", 2)) sep = a + 2;
        else { fprintf(stderr, "seq: unknown option %s\n", a); return 1; }
    }

    int n = argc - i;
    if (n < 1 || n > 3) { fprintf(stderr, "usage: seq [-s sep] [-w] [first [incr]] last\n"); return 1; }
    double first = 1, incr = 1, last;
    if (n == 1) last = atof(argv[i]);
    else if (n == 2) { first = atof(argv[i]); last = atof(argv[i + 1]); }
    else { first = atof(argv[i]); incr = atof(argv[i + 1]); last = atof(argv[i + 2]); }
    if (incr == 0) { fprintf(stderr, "seq: increment must not be zero\n"); return 1; }

    int isint = integral(first) && integral(incr) && integral(last);
    int width = 0;
    if (wflag) {
        char b[64];
        for (double v = first; incr > 0 ? v <= last + 1e-9 : v >= last - 1e-9; v += incr) {
            int l = isint ? snprintf(b, sizeof b, "%lld", (long long)v)
                          : snprintf(b, sizeof b, "%g", v);
            if (l > width) width = l;
        }
    }

    int out = 0;
    for (double v = first; incr > 0 ? v <= last + 1e-9 : v >= last - 1e-9; v += incr) {
        if (out++) fputs(sep, stdout);
        if (isint) printf("%0*lld", width, (long long)v);
        else       printf("%0*g", width, v);
    }
    if (out) putchar('\n');
    return 0;
}
