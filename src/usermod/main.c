/* usermod — modify a user account.
 *   usermod [-s shell] [-c comment] [-L] [-U] <user>
 *     -s shell   change the login shell           -L  lock the password
 *     -c comment change the GECOS/comment field   -U  unlock the password
 *
 * Requires an admin session. Edits the target's /etc/passwd and/or /etc/shadow
 * record in place, leaving all other records and fields intact. Holds
 * CAP_KIND_AUTH via caps.d.
 */
#include "../../include/acct.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Split `rec` (a colon record) into up to `max` fields (pointers into a copy in
 * `store`). Returns the field count. */
static int split(char *store, const char *rec, char **f, int max)
{
    strncpy(store, rec, 511); store[511] = 0;
    int n = 0; f[n++] = store;
    for (char *p = store; *p && n < max; p++)
        if (*p == ':') { *p = 0; f[n++] = p + 1; }
    return n;
}

int main(int argc, char **argv)
{
    const char *shell = NULL, *comment = NULL, *user = NULL;
    int lock = 0, unlock = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") && i + 1 < argc) shell = argv[++i];
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) comment = argv[++i];
        else if (!strcmp(argv[i], "-L")) lock = 1;
        else if (!strcmp(argv[i], "-U")) unlock = 1;
        else if (argv[i][0] != '-') user = argv[i];
        else { fprintf(stderr, "usermod: unknown option %s\n", argv[i]); return 1; }
    }
    if (!user) { fprintf(stderr, "usage: usermod [-s shell] [-c comment] [-L|-U] <user>\n"); return 1; }
    if (lock && unlock) { fprintf(stderr, "usermod: -L and -U are mutually exclusive\n"); return 1; }
    if (!acct_admin_active()) { fprintf(stderr, "usermod: requires an admin session\n"); return 1; }

    /* passwd edits (shell / comment) */
    if (shell || comment) {
        char *rec = acct_get_record(ACCT_PASSWD, user);
        if (!rec) { fprintf(stderr, "usermod: no such user '%s'\n", user); return 1; }
        char store[512]; char *f[8];
        int n = split(store, rec, f, 8); free(rec);
        if (n < 7) { fprintf(stderr, "usermod: malformed passwd record\n"); return 1; }
        if (comment) f[4] = (char *)comment;
        if (shell)   f[6] = (char *)shell;
        char line[512];
        snprintf(line, sizeof line, "%s:%s:%s:%s:%s:%s:%s",
                 f[0], f[1], f[2], f[3], f[4], f[5], f[6]);
        if (acct_edit_record(ACCT_PASSWD, user, line, 'r', 0644) != 0) {
            fprintf(stderr, "usermod: could not write /etc/passwd\n"); return 1; }
    }

    /* shadow edits (lock / unlock) */
    if (lock || unlock) {
        char *rec = acct_get_record(ACCT_SHADOW, user);
        if (!rec) { fprintf(stderr, "usermod: no shadow entry for '%s'\n", user); return 1; }
        char store[512]; char *f[10];
        int n = split(store, rec, f, 10); free(rec);
        if (n < 2) { fprintf(stderr, "usermod: malformed shadow record\n"); return 1; }
        char newhash[300];
        if (lock) {
            if (f[1][0] == '!') snprintf(newhash, sizeof newhash, "%s", f[1]);   /* already */
            else snprintf(newhash, sizeof newhash, "!%s", f[1]);
        } else {
            snprintf(newhash, sizeof newhash, "%s", f[1][0] == '!' ? f[1] + 1 : f[1]);
        }
        char line[600];
        int o = snprintf(line, sizeof line, "%s:%s", f[0], newhash);
        for (int i = 2; i < n; i++) o += snprintf(line + o, sizeof line - o, ":%s", f[i]);
        if (acct_edit_record(ACCT_SHADOW, user, line, 'r', 0600) != 0) {
            fprintf(stderr, "usermod: could not write /etc/shadow (need CAP_KIND_AUTH)\n"); return 1; }
    }

    if (!shell && !comment && !lock && !unlock)
        fprintf(stderr, "usermod: nothing to do (no options given)\n");
    return 0;
}
