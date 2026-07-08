/* cksum — write a POSIX CRC checksum and byte count for each input.
 *   cksum [FILE...]   (stdin if no file)
 * Output: "<crc> <bytes> [name]", matching the POSIX/GNU cksum algorithm. */
#include <stdio.h>
#include <stdint.h>

static uint32_t crctab[256];

static void
init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i << 24;
        for (int k = 0; k < 8; k++)
            c = (c & 0x80000000u) ? (c << 1) ^ 0x04c11db7u : (c << 1);
        crctab[i] = c;
    }
}

static void
cksum_stream(FILE *f, const char *name)
{
    uint32_t crc = 0;
    uint64_t len = 0;
    int c;
    while ((c = getc(f)) != EOF) {
        crc = (crc << 8) ^ crctab[((crc >> 24) ^ (uint32_t)c) & 0xff];
        len++;
    }
    for (uint64_t n = len; n; n >>= 8)        /* fold in the length, LSB first */
        crc = (crc << 8) ^ crctab[((crc >> 24) ^ (uint32_t)(n & 0xff)) & 0xff];
    crc = ~crc;
    if (name) printf("%u %llu %s\n", crc, (unsigned long long)len, name);
    else      printf("%u %llu\n", crc, (unsigned long long)len);
}

int
main(int argc, char **argv)
{
    init();
    if (argc < 2) { cksum_stream(stdin, NULL); return 0; }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) { perror(argv[i]); rc = 1; continue; }
        cksum_stream(f, argv[i]);
        fclose(f);
    }
    return rc;
}
