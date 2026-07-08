/* printf — format and print arguments, POSIX.
 *
 * Handles C backslash escapes in the format (\n \t \r \a \b \f \v \\ \NNN \xHH
 * and \c to stop), the conversions %d %i %u %o %x %X %c %s %e %E %f %g %G %%
 * with flags/width/precision, and %b (a string argument interpreted for
 * escapes). The format string is reused until all arguments are consumed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int exit_status = 0;

/* Resolve a backslash escape. *pf points at the char AFTER the backslash and is
 * advanced past the escape. Sets *stop for \c. Returns the byte to emit, or -1
 * to emit nothing (\c). */
static int
esc_char(const char **pf, int *stop)
{
    const char *s = *pf;
    int c = (unsigned char)*s;
    switch (c) {
    case 'n': c = '\n'; s++; break;
    case 't': c = '\t'; s++; break;
    case 'r': c = '\r'; s++; break;
    case 'a': c = '\a'; s++; break;
    case 'b': c = '\b'; s++; break;
    case 'f': c = '\f'; s++; break;
    case 'v': c = '\v'; s++; break;
    case '\\': c = '\\'; s++; break;
    case 'c': *stop = 1; *pf = s + 1; return -1;
    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7': {
        int v = 0, n = 0;
        while (n < 3 && *s >= '0' && *s <= '7') { v = v * 8 + (*s++ - '0'); n++; }
        c = v & 0xff; break;
    }
    case 'x': {
        s++; int v = 0, n = 0;
        while (n < 2 && isxdigit((unsigned char)*s)) {
            int d = tolower((unsigned char)*s);
            v = v * 16 + (d <= '9' ? d - '0' : d - 'a' + 10); s++; n++;
        }
        c = v & 0xff; break;
    }
    case '\0': c = '\\'; break;              /* trailing backslash → literal */
    default: *pf = s; return '\\';           /* unknown escape: emit the backslash */
    }
    *pf = s;
    return c;
}

/* Print S with backslash escapes interpreted (used for %b and never for the
 * literal format walk, which handles escapes inline). Returns 1 on \c. */
static int
print_esc_string(const char *s)
{
    int stop = 0;
    while (*s) {
        if (*s == '\\') { s++; int c = esc_char(&s, &stop); if (stop) return 1; if (c >= 0) putchar(c); }
        else putchar(*s++);
    }
    return 0;
}

/* Apply one conversion spec (leading '%' … up to but excluding the conv char)
 * to ARG, using the conversion character CONVC. */
static void
conv(const char *spec, char convc, const char *arg)
{
    char fmt[64];
    size_t sl = strlen(spec);
    if (sl + 4 >= sizeof fmt) return;
    memcpy(fmt, spec, sl);
    char *p = fmt + sl;
    switch (convc) {
    case 'd': case 'i':
        *p++ = 'l'; *p++ = 'l'; *p++ = 'd'; *p = 0;
        printf(fmt, arg ? strtoll(arg, NULL, 0) : 0LL);
        break;
    case 'u': case 'o': case 'x': case 'X':
        *p++ = 'l'; *p++ = 'l'; *p++ = convc; *p = 0;
        printf(fmt, arg ? strtoull(arg, NULL, 0) : 0ULL);
        break;
    case 'c':
        *p++ = 'c'; *p = 0;
        printf(fmt, arg && arg[0] ? arg[0] : 0);
        break;
    case 's':
        *p++ = 's'; *p = 0;
        printf(fmt, arg ? arg : "");
        break;
    case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
        *p++ = convc; *p = 0;
        printf(fmt, arg ? strtod(arg, NULL) : 0.0);
        break;
    default:
        fprintf(stderr, "printf: %%%c: invalid conversion\n", convc);
        exit_status = 1;
        break;
    }
}

int
main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: printf FORMAT [ARG...]\n"); return 1; }
    const char *format = argv[1];
    char **args = argv + 2;
    int nargs = argc - 2;
    int ai = 0, consumed;

    do {
        consumed = 0;
        const char *f = format;
        int stop = 0;
        while (*f) {
            if (*f == '\\') {
                f++;
                int c = esc_char(&f, &stop);
                if (stop) { fflush(stdout); return exit_status; }
                if (c >= 0) putchar(c);
                continue;
            }
            if (*f == '%') {
                if (f[1] == '%') { putchar('%'); f += 2; continue; }
                const char *start = f;
                char spec[48]; int si = 0;
                spec[si++] = *f++;                       /* % */
                while (*f && strchr("-+ #0", *f) && si < 40) spec[si++] = *f++;
                while (*f && isdigit((unsigned char)*f) && si < 40) spec[si++] = *f++;
                if (*f == '.') { spec[si++] = *f++; while (*f && isdigit((unsigned char)*f) && si < 44) spec[si++] = *f++; }
                if (!*f) { fwrite(start, 1, (size_t)(f - start), stdout); break; }
                char convc = *f++;
                spec[si] = 0;
                const char *arg = (ai < nargs) ? args[ai++] : NULL;
                if (arg) consumed = 1;
                if (convc == 'b') { if (print_esc_string(arg ? arg : "")) { fflush(stdout); return exit_status; } }
                else conv(spec, convc, arg);
                continue;
            }
            putchar(*f++);
        }
    } while (consumed && ai < nargs);

    return exit_status;
}
