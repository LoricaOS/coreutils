/* touch — update file timestamps (or create empty files).
 *   touch [-c] [-a] [-m] [-r REF] [-d TIME] [-t TIME] FILE...
 *
 * Creates files that don't exist (unless -c).  Updates access and/or
 * modification time to the current time (or a reference/stamp time).  By
 * default both atime and mtime are set.  -a only atime, -m only mtime,
 * -c don't create, -r REF use REF's timestamps, -d/-t parse a time spec
 * (the common YYYYMMDDHHMM.SS form for -t; -d accepts the same or "now").
 *
 * The old version only handled argv[1] and never called utimensat, so
 * re-touching an existing file left its mtime unchanged — which breaks
 * make(1) incremental builds that stamp .o files. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

static int cflag, aflag, mflag;
static struct timespec set_ts[2];
static int have_time;

/* Parse a -t [[CC]YY]MMDDhhmm[.SS] stamp (POSIX touch -t format).  Sets
 * have_time and fills set_ts both entries with the resulting time. */
static int
parse_t(const char *s)
{
    /* The POSIX -t format is [[CC]YY]MMDDhhmm[.SS]. We accept it by
     * decomposing from the right: mm is always last 2 before any .SS,
     * hh before that, etc.  This is pragmatic, not strict. */
    char buf[32];
    strncpy(buf, s, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';

    /* Split optional .SS fraction. */
    int ss = 0;
    char *dot = strchr(buf, '.');
    if (dot) {
        *dot = '\0';
        ss = atoi(dot + 1);
    }

    int len = (int)strlen(buf);
    if (len < 10) return -1;        /* need at least MMDDhhmm */

    /* Rightmost fields: MM (2), DD (2), hh (2), mm (2) — from the end. */
    int mm = atoi(buf + len - 2);
    buf[len - 2] = '\0';
    int hh = atoi(buf + len - 4);
    buf[len - 4] = '\0';
    int DD = atoi(buf + len - 6);
    buf[len - 6] = '\0';
    int MM = atoi(buf + len - 8);
    buf[len - 8] = '\0';

    int YY, CC;
    if (len - 8 >= 2) {
        YY = atoi(buf + len - 10);
        buf[len - 10] = '\0';
        if (len - 10 >= 2) {
            CC = atoi(buf);
            YY += CC * 100;
        } else {
            YY += (YY >= 69 ? 1900 : 2000);
        }
    } else {
        /* No year given — use current. */
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        YY = tm->tm_year + 1900;
    }

    struct tm tm = {0};
    tm.tm_year = YY - 1900;
    tm.tm_mon  = MM - 1;
    tm.tm_mday = DD;
    tm.tm_hour = hh;
    tm.tm_min  = mm;
    tm.tm_sec  = ss;
    tm.tm_isdst = -1;

    time_t t = mktime(&tm);
    if (t == (time_t)-1) return -1;

    set_ts[0].tv_sec = t; set_ts[0].tv_nsec = 0;
    set_ts[1].tv_sec = t; set_ts[1].tv_nsec = 0;
    have_time = 1;
    return 0;
}

/* Load timestamps from a reference file (-r REF). */
static int
load_ref(const char *ref)
{
    struct stat st;
    if (stat(ref, &st) != 0)
        return -1;
    set_ts[0] = st.st_atim;
    set_ts[1] = st.st_mtim;
    have_time = 1;
    return 0;
}

static int
do_touch(const char *path)
{
    int exists = (access(path, F_OK) == 0);
    if (!exists) {
        if (cflag)
            return 0;
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { fprintf(stderr, "touch: %s: %s\n", path, strerror(errno)); return 1; }
        close(fd);
        /* If no explicit time, the create already set both times to now. */
        if (!have_time)
            return 0;
    }

    /* Determine the timestamps to set. */
    struct timespec ts[2];
    if (have_time) {
        ts[0] = set_ts[0];
        ts[1] = set_ts[1];
    } else {
        ts[0].tv_sec = ts[1].tv_sec = UTIME_NOW;
        ts[0].tv_nsec = ts[1].tv_nsec = UTIME_NOW;
    }

    /* -a only atime, -m only mtime; default both. */
    if (aflag && !mflag) ts[1] = (struct timespec){ .tv_sec = 0, .tv_nsec = UTIME_OMIT };
    if (mflag && !aflag) ts[0] = (struct timespec){ .tv_sec = 0, .tv_nsec = UTIME_OMIT };

    if (utimensat(AT_FDCWD, path, ts, 0) != 0) {
        fprintf(stderr, "touch: %s: %s\n", path, strerror(errno));
        return 1;
    }
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
        /* Walk the flag cluster; -r/-t/-d take the rest of the arg or the
         * next argv. */
        const char *f = a + 1;
        while (*f) {
            switch (*f) {
            case 'c': cflag = 1; f++; break;
            case 'a': aflag = 1; f++; break;
            case 'm': mflag = 1; f++; break;
            case 'r': {
                const char *arg = f[1] ? f + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!arg) { fprintf(stderr, "touch: -r requires a reference file\n"); return 1; }
                if (load_ref(arg) != 0) { fprintf(stderr, "touch: %s: %s\n", arg, strerror(errno)); return 1; }
                goto done_opts;
            }
            case 't': {
                const char *arg = f[1] ? f + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!arg) { fprintf(stderr, "touch: -t requires a time argument\n"); return 1; }
                if (parse_t(arg) != 0) { fprintf(stderr, "touch: invalid time spec\n"); return 1; }
                goto done_opts;
            }
            case 'd': {
                const char *arg = f[1] ? f + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!arg) { fprintf(stderr, "touch: -d requires a time argument\n"); return 1; }
                if (strcmp(arg, "now") == 0) { have_time = 0; goto done_opts; }
                if (parse_t(arg) != 0) { fprintf(stderr, "touch: invalid -d time\n"); return 1; }
                goto done_opts;
            }
            default:
                fprintf(stderr, "touch: invalid option -- '%c'\n", *f);
                return 1;
            }
        }
    }
done_opts:
    if (i < argc && !strcmp(argv[i], "--"))
        i++;

    if (i >= argc) { fprintf(stderr, "usage: touch [-camm] [-r REF] [-t TIME] file...\n"); return 1; }

    int rc = 0;
    for (; i < argc; i++)
        rc |= do_touch(argv[i]);
    return rc;
}