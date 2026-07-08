/* base64 — encode (default) or decode (-d) base64.  base64 [-d] [-w COLS] [FILE]
 *
 * Reads a file argument or stdin ("-"). Encoding wraps at COLS (default 76,
 * 0 = no wrapping); decoding ignores whitespace and padding. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char E[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static unsigned char *
slurp(FILE *f, size_t *outn)
{
    size_t cap = 1u << 16, n = 0;
    unsigned char *b = malloc(cap);
    if (!b) return NULL;
    for (;;) {
        if (n == cap) {
            cap *= 2;
            unsigned char *t = realloc(b, cap);
            if (!t) { free(b); return NULL; }
            b = t;
        }
        size_t r = fread(b + n, 1, cap - n, f);
        n += r;
        if (r == 0) break;
    }
    *outn = n;
    return b;
}

int
main(int argc, char **argv)
{
    int decode = 0, cols = 76, i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') break;
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--decode")) decode = 1;
        else if (!strcmp(argv[i], "-w")) { if (++i >= argc) return 1; cols = atoi(argv[i]); }
        else if (!strncmp(argv[i], "-w", 2)) cols = atoi(argv[i] + 2);
        else { fprintf(stderr, "base64: bad option %s\n", argv[i]); return 1; }
    }

    FILE *f = stdin;
    if (i < argc && strcmp(argv[i], "-")) {
        f = fopen(argv[i], "rb");
        if (!f) { perror(argv[i]); return 1; }
    }
    size_t n;
    unsigned char *b = slurp(f, &n);
    if (!b) { fprintf(stderr, "base64: out of memory\n"); return 1; }

    if (!decode) {
        int col = 0;
        for (size_t j = 0; j < n; j += 3) {
            unsigned v = (unsigned)b[j] << 16;
            int k = 1;
            if (j + 1 < n) { v |= (unsigned)b[j + 1] << 8; k = 2; }
            if (j + 2 < n) { v |= (unsigned)b[j + 2]; k = 3; }
            char o[4] = {
                E[(v >> 18) & 63], E[(v >> 12) & 63],
                k >= 2 ? E[(v >> 6) & 63] : '=',
                k >= 3 ? E[v & 63] : '=',
            };
            for (int m = 0; m < 4; m++) {
                putchar(o[m]);
                if (cols > 0 && ++col == cols) { putchar('\n'); col = 0; }
            }
        }
        if (cols == 0 || col != 0) putchar('\n');
    } else {
        int d[256];
        for (int j = 0; j < 256; j++) d[j] = -1;
        for (int j = 0; j < 64; j++) d[(unsigned char)E[j]] = j;
        unsigned v = 0; int bits = 0;
        for (size_t j = 0; j < n; j++) {
            int c = d[b[j]];
            if (c < 0) continue;            /* skip whitespace / '=' / stray bytes */
            v = (v << 6) | (unsigned)c; bits += 6;
            if (bits >= 8) { bits -= 8; putchar((v >> bits) & 0xff); }
        }
    }
    free(b);
    return 0;
}
