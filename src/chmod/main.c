/*
 * chmod — change file mode, octal or symbolic.
 *
 * Octal:    chmod 755 f      chmod 0644 f
 * Symbolic: chmod +x f       chmod u+w,go-r f    chmod a=rx f
 *   clauses are comma-separated; each is [ugoa]*[+-=][rwxX]* applied to the
 *   file's current mode. 'X' sets execute only if the file is a dir or already
 *   has some execute bit. Multiple files supported.
 *
 * Was an octal-only stub — `chmod +x` parsed as octal 0 and wiped every
 * permission bit, so build scripts couldn't mark anything executable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

/* Apply one symbolic clause to *m (current mode). Returns 0 on parse error. */
static int apply_clause(const char *c, mode_t *m, int is_dir)
{
    mode_t who = 0;                 /* affected bits mask, in owner position */
    int who_set = 0;
    for (; *c && !strchr("+-=", *c); c++) {
        switch (*c) {
        case 'u': who |= 04700; who_set = 1; break;
        case 'g': who |= 02070; who_set = 1; break;
        case 'o': who |= 01007; who_set = 1; break;
        case 'a': who |= 07777; who_set = 1; break;
        default: return 0;
        }
    }
    if (!*c) return 0;
    char op = *c++;
    if (!who_set) who = 07777;      /* no who → all (umask ignored — fine here) */

    mode_t perm = 0;                /* permission bits in all three positions */
    for (; *c; c++) {
        switch (*c) {
        case 'r': perm |= 0444; break;
        case 'w': perm |= 0222; break;
        case 'x': perm |= 0111; break;
        case 'X': if (is_dir || (*m & 0111)) perm |= 0111; break;
        case 's': perm |= 06000; break;
        case 't': perm |= 01000; break;
        default: return 0;
        }
    }
    mode_t bits = perm & who;       /* only the requested who-columns */
    if (op == '+')      *m |= bits;
    else if (op == '-') *m &= ~bits;
    else                *m = (*m & ~who) | bits;   /* '=' : clear who, then set */
    return 1;
}

/* Parse argv[1] into the new mode for a file with current mode cur. */
static int resolve_mode(const char *spec, mode_t cur, int is_dir, mode_t *out)
{
    if (isdigit((unsigned char)spec[0])) {         /* octal */
        char *end;
        unsigned long v = strtoul(spec, &end, 8);
        if (*end) return 0;
        *out = (mode_t)(v & 07777);
        return 1;
    }
    mode_t m = cur & 07777;
    char work[256];
    strncpy(work, spec, sizeof work - 1); work[sizeof work - 1] = 0;
    for (char *cl = strtok(work, ","); cl; cl = strtok(NULL, ","))
        if (!apply_clause(cl, &m, is_dir)) return 0;
    *out = m;
    return 1;
}

int main(int argc, char **argv)
{
    int i = 1;
    if (i < argc && !strcmp(argv[i], "-R")) i++;   /* accepted, non-recursive here */
    if (argc - i < 2) {
        fprintf(stderr, "usage: chmod MODE FILE...\n");
        return 1;
    }
    const char *spec = argv[i++];
    int rc = 0;
    for (; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) != 0) { perror(argv[i]); rc = 1; continue; }
        mode_t nm;
        if (!resolve_mode(spec, st.st_mode, S_ISDIR(st.st_mode), &nm)) {
            fprintf(stderr, "chmod: invalid mode: %s\n", spec);
            return 1;
        }
        if (chmod(argv[i], nm) != 0) { perror(argv[i]); rc = 1; }
    }
    return rc;
}
