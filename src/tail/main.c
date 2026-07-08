/* tail — output the last part of files.  tail [-n N] [-c N] [-f] [FILE]
 * -n last N lines (default 10), -c last N bytes, -f follow (poll for growth).
 * No fixed line-length cap (the old version truncated at 4096). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static long nlines = 10;
static long nbytes = -1;      /* -1 = line mode */
static int follow;

static void
tail_lines(FILE *f)
{
    char **ring = calloc((size_t)nlines, sizeof *ring);
    size_t head = 0, count = 0;
    char *buf = NULL; size_t bcap = 0; ssize_t len;
    while ((len = getline(&buf, &bcap, f)) != -1) {
        free(ring[head]);
        ring[head] = strndup(buf, (size_t)len);
        head = (head + 1) % (size_t)nlines;
        if (count < (size_t)nlines) count++;
    }
    free(buf);
    size_t start = (head + (size_t)nlines - count) % (size_t)nlines;
    for (size_t k = 0; k < count; k++) fputs(ring[(start + k) % (size_t)nlines], stdout);
    for (size_t k = 0; k < (size_t)nlines; k++) free(ring[k]);
    free(ring);
}

static void
tail_bytes(FILE *f)
{
    char *data = NULL; size_t cap = 0, n = 0;
    for (;;) {
        if (n == cap) { cap = cap ? cap * 2 : 65536; data = realloc(data, cap); }
        size_t r = fread(data + n, 1, cap - n, f);
        n += r;
        if (!r) break;
    }
    size_t off = (n > (size_t)nbytes) ? n - (size_t)nbytes : 0;
    fwrite(data + off, 1, n - off, stdout);
    free(data);
}

int
main(int argc, char **argv)
{
    int i = 1;
    for (; i < argc; i++) {
        char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0') break;
        if (!strcmp(a, "--")) { i++; break; }
        if (!strcmp(a, "-f") || !strcmp(a, "-F")) follow = 1;
        else if (!strcmp(a, "-n")) { if (++i >= argc) return 1; nlines = atol(argv[i]); }
        else if (!strncmp(a, "-n", 2)) nlines = atol(a + 2);
        else if (!strcmp(a, "-c")) { if (++i >= argc) return 1; nbytes = atol(argv[i]); }
        else if (!strncmp(a, "-c", 2)) nbytes = atol(a + 2);
        else { fprintf(stderr, "tail: invalid option %s\n", a); return 1; }
    }
    if (nlines < 1) nlines = 1;

    const char *path = (i < argc) ? argv[i] : NULL;
    FILE *f = (path && strcmp(path, "-")) ? fopen(path, "r") : stdin;
    if (!f) { perror(path); return 1; }

    if (nbytes >= 0) tail_bytes(f);
    else tail_lines(f);
    fflush(stdout);

    if (follow && f != stdin) {               /* poll the file for appended data */
        long pos = ftell(f);
        for (;;) {
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 400000000 };
            nanosleep(&ts, NULL);
            fseek(f, 0, SEEK_END);
            long end = ftell(f);
            if (end > pos) {
                fseek(f, pos, SEEK_SET);
                char b[8192]; size_t r;
                while ((r = fread(b, 1, sizeof b, f)) > 0) fwrite(b, 1, r, stdout);
                fflush(stdout);
                pos = end;
            } else {
                clearerr(f);
            }
        }
    }
    if (f != stdin) fclose(f);
    return 0;
}
