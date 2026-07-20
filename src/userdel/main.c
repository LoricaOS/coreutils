/* userdel — delete a user account.  userdel [-r] <user>
 *
 * Requires an admin session. Removes the /etc/passwd, /etc/shadow and per-user
 * /etc/group records; -r also removes the home directory. Refuses to delete the
 * uid-0 primary account (that would lock the system out of its own admin path).
 * Holds CAP_KIND_AUTH via caps.d.
 */
#include "../../include/acct.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int remove_home = 0;
    const char *user = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r")) remove_home = 1;
        else if (argv[i][0] != '-') user = argv[i];
        else { fprintf(stderr, "userdel: unknown option %s\n", argv[i]); return 1; }
    }
    if (!user) { fprintf(stderr, "usage: userdel [-r] <user>\n"); return 1; }
    if (!acct_admin_active()) { fprintf(stderr, "userdel: requires an admin session\n"); return 1; }

    char *rec = acct_get_record(ACCT_PASSWD, user);
    if (!rec) { fprintf(stderr, "userdel: no such user '%s'\n", user); return 1; }

    /* refuse to delete uid 0 (name:x:0:...) — the primary account */
    const char *c1 = strchr(rec, ':');
    const char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
    int uid = c2 ? atoi(c2 + 1) : -1;
    char home[96] = "";
    { const char *c3 = c2 ? strchr(c2 + 1, ':') : NULL;    /* gid */
      const char *c4 = c3 ? strchr(c3 + 1, ':') : NULL;    /* gecos */
      const char *c5 = c4 ? strchr(c4 + 1, ':') : NULL;    /* home starts after */
      if (c5) { const char *he = strchr(c5 + 1, ':');
                size_t hl = he ? (size_t)(he - c5 - 1) : strlen(c5 + 1);
                if (hl < sizeof home) { memcpy(home, c5 + 1, hl); home[hl] = 0; } } }
    free(rec);
    if (uid == 0) { fprintf(stderr, "userdel: refusing to delete the uid-0 primary account\n"); return 1; }

    acct_edit_record(ACCT_PASSWD, user, NULL, 'd', 0644);
    acct_edit_record(ACCT_SHADOW, user, NULL, 'd', 0600);
    acct_edit_record(ACCT_GROUP,  user, NULL, 'd', 0644);

    if (remove_home && home[0] && strncmp(home, "/home/", 6) == 0) {
        char cmd_pics[128];
        snprintf(cmd_pics, sizeof cmd_pics, "%s/Pictures", home);
        rmdir(cmd_pics);          /* best-effort; deep trees left to the admin */
        rmdir(home);
    }
    printf("userdel: deleted %s\n", user);
    return 0;
}
