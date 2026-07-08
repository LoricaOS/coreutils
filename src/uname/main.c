/* uname — print system information.  uname [-asnrvmo]  (default: -s)
 * -a all, -s sysname, -n nodename, -r release, -v version, -m machine, -o OS. */
#include <stdio.h>
#include <sys/utsname.h>

int
main(int argc, char **argv)
{
    int s = 0, n = 0, r = 0, v = 0, m = 0, o = 0, any = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') {
            fprintf(stderr, "uname: unexpected operand %s\n", argv[i]);
            return 1;
        }
        for (const char *p = argv[i] + 1; *p; p++, any = 1)
            switch (*p) {
            case 'a': s = n = r = v = m = o = 1; break;
            case 's': s = 1; break;
            case 'n': n = 1; break;
            case 'r': r = 1; break;
            case 'v': v = 1; break;
            case 'p': case 'm': m = 1; break;
            case 'o': o = 1; break;
            default: fprintf(stderr, "uname: invalid option -- '%c'\n", *p); return 1;
            }
    }
    if (!any) s = 1;

    struct utsname u;
    if (uname(&u) != 0) { puts("Aegis"); return 0; }

    int first = 1;
#define OUT(x) do { if (!first) putchar(' '); fputs((x), stdout); first = 0; } while (0)
    if (s) OUT(u.sysname);
    if (n) OUT(u.nodename);
    if (r) OUT(u.release);
    if (v) OUT(u.version);
    if (m) OUT(u.machine);
    if (o) OUT("LoricaOS");
    putchar('\n');
    return 0;
}
