/* groupadd — create a group.  groupadd <group>
 *
 * Requires an admin session. Adds an /etc/group record with the next free gid
 * (standalone groups start at 100, above the per-user groups useradd assigns).
 * Does not need CAP_KIND_AUTH (touches only /etc/group, not /etc/shadow), but
 * ships the same admin gate as the other account tools.
 */
#include "../../include/acct.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *group = (argc > 1 && argv[1][0] != '-') ? argv[1] : NULL;
    if (!group) { fprintf(stderr, "usage: groupadd <group>\n"); return 1; }
    if (!acct_name_valid(group)) { fprintf(stderr, "groupadd: invalid group name\n"); return 1; }
    if (!acct_admin_active()) { fprintf(stderr, "groupadd: requires an admin session\n"); return 1; }

    char *existing = acct_get_record(ACCT_GROUP, group);
    if (existing) { free(existing); fprintf(stderr, "groupadd: group '%s' already exists\n", group); return 1; }

    int gid = acct_next_id(ACCT_GROUP, 100);
    char line[128];
    snprintf(line, sizeof line, "%s:x:%d:", group, gid);
    if (acct_edit_record(ACCT_GROUP, group, line, 'a', 0644) != 0) {
        fprintf(stderr, "groupadd: could not write /etc/group\n"); return 1; }
    printf("groupadd: created %s (gid %d)\n", group, gid);
    return 0;
}
