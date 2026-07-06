#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

/* mkdir [-p] dir...
 * -p: create parent components as needed; existing directories are not an
 * error (POSIX). Every `make` recipe's `@mkdir -p $(dir $@)` depends on this —
 * the old stub treated "-p" itself as the directory name. */

static int mkdir_p(char *path)
{
    char *s;
    for (s = path + 1; *s; s++) {
        if (*s != '/')
            continue;
        *s = '\0';
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            perror(path);
            *s = '/';
            return 1;
        }
        *s = '/';
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        perror(path);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int p = 0, i = 1, rc = 0;
    if (i < argc && strcmp(argv[i], "-p") == 0) {
        p = 1;
        i++;
    }
    if (i >= argc) {
        fprintf(stderr, "usage: mkdir [-p] dir...\n");
        return 1;
    }
    for (; i < argc; i++) {
        if (p) {
            rc |= mkdir_p(argv[i]);
        } else if (mkdir(argv[i], 0755) != 0) {
            perror(argv[i]);
            rc = 1;
        }
    }
    return rc;
}
