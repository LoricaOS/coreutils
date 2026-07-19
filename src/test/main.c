/* test (and `[`) — POSIX file/string/integer tests.
 *   test OP ARG
 *   test ARG1 OP ARG2
 *   test EXPR -a EXPR -o EXPR
 *   test ! EXPR
 *   [ ... ]
 *
 * Supported file tests: -e -r -w -x -f -d -L -h -s -b -c -S -p -u -g -k
 * String tests: -n STR  -z STR  STR1 = STR2  STR1 != STR2
 * Integer tests: N1 -eq N2, -ne, -lt, -le, -gt, -ge
 * File age: F1 -nt F2  F1 -ot F2
 * Combining: ! (not)  -a (and)  -o (or)
 *
 * If invoked as `[`, requires final argument to be `]`. */
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int
file_test(char op, const char *path)
{
    struct stat st;
    int rc;
    if (op == 'L' || op == 'h')
        rc = lstat(path, &st);
    else
        rc = stat(path, &st);
    if (rc != 0) return 1;

    switch (op) {
    case 'e': return 0;
    case 'r': return access(path, R_OK) == 0 ? 0 : 1;
    case 'w': return access(path, W_OK) == 0 ? 0 : 1;
    case 'x': return access(path, X_OK) == 0 ? 0 : 1;
    case 'f': return S_ISREG(st.st_mode) ? 0 : 1;
    case 'd': return S_ISDIR(st.st_mode) ? 0 : 1;
    case 'L': /* fallthrough */
    case 'h': return S_ISLNK(st.st_mode) ? 0 : 1;
    case 's': return st.st_size > 0 ? 0 : 1;
    case 'b': return S_ISBLK(st.st_mode) ? 0 : 1;
    case 'c': return S_ISCHR(st.st_mode) ? 0 : 1;
    case 'S': return S_ISSOCK(st.st_mode) ? 0 : 1;
    case 'p': return S_ISFIFO(st.st_mode) ? 0 : 1;
    case 'u': return (st.st_mode & S_ISUID) ? 0 : 1;
    case 'g': return (st.st_mode & S_ISGID) ? 0 : 1;
    case 'k': return (st.st_mode & S_ISVTX) ? 0 : 1;
    default:  return 1;
    }
}

/* Compare two files by mtime.  Returns 0 (true) if f1 is newer than f2
 * (-nt), or older (-ot).  Follows bash semantics for a missing operand:
 *   f1 -nt f2  is true if f1 exists and (f2 does not exist, or f1 is newer)
 *   f1 -ot f2  is true if f2 exists and (f1 does not exist, or f1 is older)
 * so the common `[ src -nt out ]` (out not yet built) correctly yields true. */
static int
age_test(const char *f1, const char *f2, int newer)
{
    struct stat s1, s2;
    int e1 = stat(f1, &s1) == 0;
    int e2 = stat(f2, &s2) == 0;
    if (newer) {
        if (!e1) return 1;   /* f1 missing -> never newer */
        if (!e2) return 0;   /* f1 present, f2 missing -> newer */
    } else {
        if (!e2) return 1;   /* f2 missing -> f1 never older */
        if (!e1) return 0;   /* f2 present, f1 missing -> older */
    }
    if (s1.st_mtim.tv_sec == s2.st_mtim.tv_sec)
        return newer ? (s1.st_mtim.tv_nsec > s2.st_mtim.tv_nsec ? 0 : 1)
                     : (s1.st_mtim.tv_nsec < s2.st_mtim.tv_nsec ? 0 : 1);
    if (newer)
        return s1.st_mtim.tv_sec > s2.st_mtim.tv_sec ? 0 : 1;
    else
        return s1.st_mtim.tv_sec < s2.st_mtim.tv_sec ? 0 : 1;
}

/* Evaluate a unary or binary test expression from argv[fi..(la-1)].
 * Returns 0 (true), 1 (false), or 2 (error). */
static int
eval_expr(char **argv, int fi, int la)
{
    int n = la - fi;
    if (n == 0) return 1;   /* empty: false */

    /* Binary combiners -a/-o have LOWER precedence than unary `!`, so they
     * are split off first: `! X -a Y` must parse as `(!X) -a Y`, not
     * `!(X -a Y)`.  Split on -o (lowest precedence) before -a.  We require a
     * token on each side (i in [fi+1, la-2]) so a leading/trailing -a/-o is
     * treated as a plain string operand instead of a dangling operator. */
    if (n >= 3) {
        for (int i = fi + 1; i < la - 1; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                int r1 = eval_expr(argv, fi, i);
                if (r1 == 2) return 2;
                if (r1 == 0) return 0;             /* short-circuit true */
                return eval_expr(argv, i + 1, la);
            }
        }
        for (int i = fi + 1; i < la - 1; i++) {
            if (strcmp(argv[i], "-a") == 0) {
                int r1 = eval_expr(argv, fi, i);
                if (r1 != 0) return r1 == 2 ? 2 : 1; /* short-circuit false/err */
                return eval_expr(argv, i + 1, la);
            }
        }
    }

    /* ! expr — negate.  Any -a/-o has already been split above, so `!` here
     * binds only to the primary that follows it (`test ! -f x` => !(-f x)). */
    if (strcmp(argv[fi], "!") == 0) {
        int r = eval_expr(argv, fi + 1, la);
        if (r == 2) return 2;
        return r == 0 ? 1 : 0;
    }

    /* Single-arg form: true iff non-empty string. */
    if (n == 1) return argv[fi][0] ? 0 : 1;

    /* Two-arg form: -OP ARG */
    if (n == 2 && argv[fi][0] == '-' && argv[fi][1] && !argv[fi][2]) {
        char op = argv[fi][1];
        if (op == 'n') return argv[fi + 1][0] ? 0 : 1;
        if (op == 'z') return argv[fi + 1][0] ? 1 : 0;
        return file_test(op, argv[fi + 1]);
    }

    /* Three-arg form: A OP B (comparisons; -a/-o already handled above). */
    if (n == 3) {
        const char *a = argv[fi], *op = argv[fi + 1], *b = argv[fi + 2];

        /* File age comparison. */
        if (strcmp(op, "-nt") == 0) return age_test(a, b, 1);
        if (strcmp(op, "-ot") == 0) return age_test(a, b, 0);

        /* String comparison. */
        if (strcmp(op, "=")  == 0) return strcmp(a, b) == 0 ? 0 : 1;
        if (strcmp(op, "!=") == 0) return strcmp(a, b) != 0 ? 0 : 1;

        /* Integer comparison. */
        long la_i = atol(a), lb_i = atol(b);
        if (strcmp(op, "-eq") == 0) return la_i == lb_i ? 0 : 1;
        if (strcmp(op, "-ne") == 0) return la_i != lb_i ? 0 : 1;
        if (strcmp(op, "-lt") == 0) return la_i <  lb_i ? 0 : 1;
        if (strcmp(op, "-le") == 0) return la_i <= lb_i ? 0 : 1;
        if (strcmp(op, "-gt") == 0) return la_i >  lb_i ? 0 : 1;
        if (strcmp(op, "-ge") == 0) return la_i >= lb_i ? 0 : 1;
    }

    dprintf(2, "test: unsupported expression\n");
    return 2;
}

int
main(int argc, char **argv)
{
    /* If invoked as `[`, last arg must be `]`. */
    int prog_is_bracket = 0;
    {
        const char *prog = argv[0];
        const char *base = prog;
        for (const char *q = prog; *q; q++) if (*q == '/') base = q + 1;
        if (strcmp(base, "[") == 0) prog_is_bracket = 1;
    }
    if (prog_is_bracket) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            dprintf(2, "[: missing ']'\n");
            return 2;
        }
        argc--;  /* drop trailing ] */
    }

    return eval_expr(argv, 1, argc);
}