/* id — print user and group identity.  id [-u|-g|-G] [-n]
 *
 * Even under the capability model uid/gid still exist and scripts read them
 * (`id -u`, `id -gn`); this reports the real ids and their /etc/passwd names. */
#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>

int
main(int argc, char **argv)
{
    int want_u = 0, want_g = 0, want_G = 0, want_n = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') {
            fprintf(stderr, "id: operand not supported\n");
            return 1;
        }
        for (const char *p = argv[i] + 1; *p; p++) {
            switch (*p) {
            case 'u': want_u = 1; break;
            case 'g': want_g = 1; break;
            case 'G': want_G = 1; break;
            case 'n': want_n = 1; break;
            case 'r': break; /* real id — we only report real ids anyway */
            default: fprintf(stderr, "id: bad option -%c\n", *p); return 1;
            }
        }
    }

    uid_t uid = getuid();
    gid_t gid = getgid();
    struct passwd *pw = getpwuid(uid);
    struct group  *gr = getgrgid(gid);

    if (want_u) {
        if (want_n && pw) printf("%s\n", pw->pw_name);
        else printf("%u\n", (unsigned)uid);
        return 0;
    }
    if (want_g || want_G) {
        if (want_n && gr) printf("%s\n", gr->gr_name);
        else printf("%u\n", (unsigned)gid);
        return 0;
    }

    printf("uid=%u", (unsigned)uid);
    if (pw) printf("(%s)", pw->pw_name);
    printf(" gid=%u", (unsigned)gid);
    if (gr) printf("(%s)", gr->gr_name);
    printf("\n");
    return 0;
}
