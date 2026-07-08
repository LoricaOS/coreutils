/* ln — make links.  ln [-s] [-f] [-v] TARGET... [LINKNAME|DIR]
 *
 * Default is a HARD link (link()); -s makes a symbolic link. The old version
 * called symlink() even for hardlink requests — a silent data-model bug. With
 * more than one target the final argument must be a directory. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>

static int sflag, fflag, vflag;

static int
do_link(const char *target, const char *linkpath)
{
    if (fflag) unlink(linkpath);                 /* best-effort replace */
    int r = sflag ? symlink(target, linkpath) : link(target, linkpath);
    if (r != 0) { perror(linkpath); return 1; }
    if (vflag) printf("'%s' %s '%s'\n", linkpath, sflag ? "->" : "=>", target);
    return 0;
}

int
main(int argc, char **argv)
{
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0') break;
        if (!strcmp(a, "--")) { i++; break; }
        for (const char *p = a + 1; *p; p++)
            switch (*p) {
            case 's': sflag = 1; break;
            case 'f': fflag = 1; break;
            case 'v': vflag = 1; break;
            default: fprintf(stderr, "ln: invalid option -- '%c'\n", *p); return 1;
            }
    }
    int n = argc - i;
    if (n < 1) { fprintf(stderr, "usage: ln [-sfv] target... [link|dir]\n"); return 1; }

    if (n == 1) {                                /* link into cwd as basename(target) */
        char *t = strdup(argv[i]);
        int r = do_link(argv[i], basename(t));
        free(t);
        return r;
    }

    const char *last = argv[argc - 1];
    struct stat st;
    int lastdir = (stat(last, &st) == 0 && S_ISDIR(st.st_mode));
    if (n == 2 && !lastdir) return do_link(argv[i], argv[i + 1]);

    if (!lastdir) { fprintf(stderr, "ln: target '%s' is not a directory\n", last); return 1; }
    int rc = 0;
    for (int j = i; j < argc - 1; j++) {
        char *t = strdup(argv[j]);
        char dest[4096];
        snprintf(dest, sizeof dest, "%s/%s", last, basename(t));
        rc |= do_link(argv[j], dest);
        free(t);
    }
    return rc;
}
