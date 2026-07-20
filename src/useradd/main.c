/* useradd — create a user account.  useradd [-s shell] [-c comment] <user>
 *
 * Requires an admin session. Creates the /etc/passwd, /etc/shadow (locked — run
 * passwd to set one) and /etc/group records plus the home directory. LoricaOS
 * has no "root": uid 0 is the primary user, additional accounts start at uid 1,
 * each with its own group. Holds CAP_KIND_AUTH via caps.d to touch /etc/shadow.
 */
#include "../../include/acct.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char **argv)
{
    const char *shell = "/bin/stsh", *comment = NULL, *user = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") && i + 1 < argc) shell = argv[++i];
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) comment = argv[++i];
        else if (argv[i][0] != '-') user = argv[i];
        else { fprintf(stderr, "useradd: unknown option %s\n", argv[i]); return 1; }
    }
    if (!user) { fprintf(stderr, "usage: useradd [-s shell] [-c comment] <user>\n"); return 1; }
    if (!acct_name_valid(user)) { fprintf(stderr, "useradd: invalid user name\n"); return 1; }
    if (!acct_admin_active()) { fprintf(stderr, "useradd: requires an admin session\n"); return 1; }

    char *existing = acct_get_record(ACCT_PASSWD, user);
    if (existing) { free(existing); fprintf(stderr, "useradd: user '%s' already exists\n", user); return 1; }

    int uid = acct_next_id(ACCT_PASSWD, 1);
    int gid = uid;                                  /* per-user primary group */
    if (!comment) comment = user;

    char pw[512];
    snprintf(pw, sizeof pw, "%s:x:%d:%d:%s:/home/%s:%s", user, uid, gid, comment, user, shell);
    char sh[128];
    snprintf(sh, sizeof sh, "%s:!:19814:0:99999:7:::", user);   /* locked until passwd */
    char gr[128];
    snprintf(gr, sizeof gr, "%s:x:%d:%s", user, gid, user);

    if (acct_edit_record(ACCT_PASSWD, user, pw, 'a', 0644) != 0) {
        fprintf(stderr, "useradd: could not write /etc/passwd\n"); return 1; }
    if (acct_edit_record(ACCT_SHADOW, user, sh, 'a', 0600) != 0) {
        fprintf(stderr, "useradd: could not write /etc/shadow (need CAP_KIND_AUTH)\n");
        acct_edit_record(ACCT_PASSWD, user, NULL, 'd', 0644); return 1; }   /* roll back */
    if (acct_edit_record(ACCT_GROUP, user, gr, 'a', 0644) != 0) {
        fprintf(stderr, "useradd: could not write /etc/group\n"); return 1; }

    char home[96];
    snprintf(home, sizeof home, "/home/%s", user);
    mkdir("/home", 0755);
    if (mkdir(home, 0755) != 0) { /* may exist; not fatal */ }
    chown(home, uid, gid);

    printf("useradd: created %s (uid %d) — set a password with: passwd %s\n", user, uid, user);
    return 0;
}
