/* rm — remove files and directories.  rm [-r|-R] [-f] [-i] [-v] FILE...
 *
 * -r/-R recurse into directories, -f ignores missing files and never errors,
 * -i prompts before each removal, -v reports what was removed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static int opt_r, opt_f, opt_i, opt_v;
static int had_error;

static int
confirm(const char *what, const char *path)
{
    if (!opt_i) return 1;
    fprintf(stderr, "rm: remove %s '%s'? ", what, path);
    int c = getchar(), ans = (c == 'y' || c == 'Y');
    while (c != '\n' && c != EOF) c = getchar();
    return ans;
}

static void rm_path(const char *path);

static void
rm_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) { if (!opt_f) { perror(path); had_error = 1; } return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char child[4096];
        snprintf(child, sizeof child, "%s/%s", path, e->d_name);
        rm_path(child);
    }
    closedir(d);
    if (!confirm("directory", path)) return;
    if (rmdir(path) != 0) { if (!opt_f) { perror(path); had_error = 1; } }
    else if (opt_v) printf("removed directory '%s'\n", path);
}

static void
rm_path(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (!opt_f) { perror(path); had_error = 1; }
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        if (!opt_r) {
            fprintf(stderr, "rm: cannot remove '%s': Is a directory\n", path);
            had_error = 1;
            return;
        }
        rm_dir(path);
    } else {
        if (!confirm("file", path)) return;
        if (unlink(path) != 0) { if (!opt_f) { perror(path); had_error = 1; } }
        else if (opt_v) printf("removed '%s'\n", path);
    }
}

int
main(int argc, char **argv)
{
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0') break;
        if (!strcmp(a, "--")) { i++; break; }
        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
            case 'r': case 'R': opt_r = 1; break;
            case 'f': opt_f = 1; opt_i = 0; break;
            case 'i': opt_i = 1; opt_f = 0; break;
            case 'v': opt_v = 1; break;
            default: fprintf(stderr, "rm: invalid option -- '%c'\n", *p); return 1;
            }
        }
    }
    if (i >= argc) {
        if (opt_f) return 0;
        fprintf(stderr, "usage: rm [-rfiv] FILE...\n");
        return 1;
    }
    for (; i < argc; i++) rm_path(argv[i]);
    return had_error ? 1 : 0;
}
