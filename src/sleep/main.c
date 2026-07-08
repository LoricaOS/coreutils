/* sleep — pause for a duration.  sleep NUMBER[smhd]...
 * Accepts fractional values and s/m/h/d suffixes; multiple args are summed. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

static double
parse_dur(const char *s, int *ok)
{
    char *end;
    double v = strtod(s, &end);
    double mult = 1;
    if (*end) {
        switch (*end) {
        case 's': mult = 1; break;
        case 'm': mult = 60; break;
        case 'h': mult = 3600; break;
        case 'd': mult = 86400; break;
        default: *ok = 0; return 0;
        }
        if (end[1]) { *ok = 0; return 0; }
    }
    if (end == s || v < 0) { *ok = 0; return 0; }
    *ok = 1;
    return v * mult;
}

int
main(int argc, char **argv)
{
    if (argc < 2) { fputs("usage: sleep NUMBER[smhd]...\n", stderr); return 1; }
    double total = 0;
    for (int i = 1; i < argc; i++) {
        int ok;
        double d = parse_dur(argv[i], &ok);
        if (!ok) { fprintf(stderr, "sleep: invalid duration: %s\n", argv[i]); return 1; }
        total += d;
    }
    struct timespec req = {
        .tv_sec = (time_t)total,
        .tv_nsec = (long)((total - (double)(time_t)total) * 1e9),
    };
    struct timespec rem;
    while (nanosleep(&req, &rem) == -1 && errno == EINTR) req = rem;
    return 0;
}
