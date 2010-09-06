#ifdef HAS_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "xml_utils.h"
// Tempus includes
#include "structs.h"
#include "utils.h"
#include "db.h"
#include "interpreter.h"
#include "comm.h"
#include "security.h"
#include "screen.h"
#include "players.h"
#include "accstr.h"

/*
 * The Security Namespace Role function definitions.
 */
/** The global container of Role objects. **/
GList *roles;

void
security_log(const char* msg, const char* name)
{
    slog("<SECURITY> %s : %s", msg, name);
}

struct role *
role_by_name(const char *name)
{
    gint role_matches(struct role *role, const char *name) {
        return strcasecmp(role->name, name);
    }
    
    return (struct role *)g_list_find_custom(roles,
                                             (GCompareFunc)role_matches,
                                             (gconstpointer)name);
}

/* sets this role's description */
void
set_role_description(struct role *role, const char *desc)
{
    if (role->description != NULL)
        free(role->description);
    role->description = strdup(desc);
}

// These membership checks should be binary searches.
bool
is_role_member(struct role *role, long player)
{
    return g_list_find(role->members, GINT_TO_POINTER(player));
}

bool
is_role_command(struct role *role, const struct command_info *command)
{
    return g_list_find(role->commands, command);
}

bool
role_gives_access(struct role *role,
                  struct creature *ch,
                  const struct command_info *command)
{
    return (is_role_member(role, GET_IDNUM(ch)) && is_role_command(role, command));
}

/* sets the name of the role that can admin this role. */
void
set_role_admin_role(struct role *role, const char *role_name)
{
    if (role->admin_role != NULL)
        free(role->admin_role);
    role->admin_role = strdup(role_name);
}

/* sprintf's a one line desc of this role into out */
void
send_role_linedesc(struct role *role,struct creature *ch)
{
    const char *nrm = CCNRM(ch,C_NRM);
    const char *cyn = CCCYN(ch,C_NRM);
    const char *grn = CCGRN(ch,C_NRM);

    send_to_char(ch,
                 "%s%15s %s[%s%4d%s] [%s%4d%s]%s - %s\r\n",
                 grn, role->name, cyn,
                 nrm, g_list_length(role->commands), cyn,
                 nrm, g_list_length(role->members), cyn,
                 nrm, role->description);
}

/* Sends a list of this role's members to the given character. */
bool
send_role_members(struct role *role, struct creature *ch)
{
    int pos = 1;
    send_to_char(ch, "Members:\r\n");
    for(GList *it = role->members; it; it = it->next) {
        send_to_char(ch,
                     "%s[%s%6d%s] %s%-15s%s",
                     CCCYN(ch,C_NRM),
                     CCNRM(ch,C_NRM),
                     GPOINTER_TO_INT(it->data),
                     CCCYN(ch,C_NRM),
                     CCGRN(ch,C_NRM),
                     player_name_by_idnum(GPOINTER_TO_INT(it->data)),
                     CCNRM(ch,C_NRM)
           );
        if (pos++ % 3 == 0) {
            pos = 1;
            send_to_char(ch, "\r\n");
        }
    }
    if (pos != 1)
        send_to_char(ch, "\r\n");
    return true;
}

/* Sends a list of this role's commands to the given character. */
bool
send_role_commands(struct role *role, struct creature *ch, bool prefix)
{
    GList *it = role->commands;
    int i = 1;
    int pos = 1;
    if (prefix)
        send_to_char(ch, "Commands:\r\n");
    for (; it; it = it->next, ++i) {
        send_to_char(ch,
                     "%s[%s%4d%s] %s%-15s",
                     CCCYN(ch,C_NRM),
                     CCNRM(ch,C_NRM),
                     i,
                     CCCYN(ch,C_NRM),
                     CCGRN(ch,C_NRM),
                     ((struct command_info *)it->data)->command);
        if (pos++ % 3 == 0) {
            pos = 1;
            send_to_char(ch, "\r\n");
        }
    }
    if (pos != 1)
        send_to_char(ch, "\r\n");
    send_to_char(ch, "%s", CCNRM(ch,C_NRM));
    return true;
}

/* sends a multi-line status of this role to ch */
void
send_role_status(struct role *role, struct creature *ch)
{
    const char *nrm = CCNRM(ch,C_NRM);
    const char *cyn = CCCYN(ch,C_NRM);
    const char *grn = CCGRN(ch,C_NRM);

    send_to_char(ch,
                 "Name: %s%s%s [%s%4d%s][%s%4d%s]%s\r\n",
                 grn, role->name, cyn,
                 nrm, g_list_length(role->commands), cyn,
                 nrm, g_list_length(role->members), cyn,
                 nrm);
    send_to_char(ch,
                 "Admin Role: %s%s%s\r\n",
                 grn,
                 role->admin_role ? role->admin_role : "None",
                 nrm);
    send_to_char(ch,
                 "Description: %s\r\n",
                 role->description);
    send_role_commands(role, ch, false);
    send_role_members(role, ch);
}

/* Adds a command to this role. Fails if already added. */
bool
add_role_command(struct role *role, struct command_info *command)
{
    if (is_role_command(role, command))
        return false;
    command->role_count += 1;
    role->commands = g_list_prepend(role->commands, command);
    return true;
}

/* Removes a command from this role. Fails if not a member. */
bool
remove_role_command(struct role *role, struct command_info *command)
{
    if (!is_role_command(role, command))
        return false;

    role->commands = g_list_remove(role->commands, command);
    command->role_count -= 1;
    return true;
}

/* Removes a member from this role by player id. Fails if not a member. */
bool
remove_role_member(struct role *role, long player)
{
    if (!is_role_member(role, player))
        return false;

    role->members = g_list_remove(role->members, GINT_TO_POINTER(player));

    return true;
}

/* Adds a member to this role by player id. Fails if already added. */
bool
add_role_member(struct role *role, long player)
{
    if (is_role_member(role, player))
        return false;

    role->members = g_list_prepend(role->members, GINT_TO_POINTER(player));

    return true;
}

/* Sends a list of this role's members to the given character. */
void
send_role_member_list(struct role *role,
                      struct creature *ch,
                      const char *title,
                      const char *admin_role_name)
{
    int pos = 0;
    const char *name;
    struct role *admin_role = NULL;

    if (!role->members)
        return;

    admin_role = role_by_name(admin_role_name);

    acc_sprintf("\r\n\r\n        %s%s%s\r\n",
				CCYEL(ch,C_NRM), title, CCNRM(ch,C_NRM));
    acc_sprintf("    %so~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~%s\r\n",

                CCCYN(ch,C_NRM), CCNRM(ch,C_NRM));
    acc_strcat("        ", NULL);


    for(GList *it = role->members; it; it = it->next) {
        name = player_name_by_idnum(GPOINTER_TO_INT(it->data));
        if (!name)
            continue;
        if (role && is_role_member(role, GPOINTER_TO_INT(it->data)))
            acc_sprintf("%s%-15s%s",
                        CCYEL_BLD(ch,C_NRM), name, CCNRM(ch,C_NRM));
        else
            acc_sprintf("%-15s", name);
        pos++;
        if (pos > 3) {
            pos = 0;
            acc_strcat("\r\n        ", NULL);
        }
    }
}

struct role *
make_role(const char *name,
          const char *description,
          const char *admin_role)
{
    struct role *role;

    CREATE(role, struct role, 1);
    if (name)
        role->name = strdup(name);
    if (description)
        role->description = strdup(description);
    if (admin_role)
        role->admin_role = strdup(admin_role);

    return role;
}

void
free_role(struct role *role)
{
    free(role->name);
    free(role->description);
    free(role->admin_role);
    g_list_free(role->commands);
    g_list_free(role->members);
    free(role);
}

/*
 *  Loads a role from the given xmlnode;
 *  Intended for reading from a file
 */
struct role *
load_role_from_xml(xmlNodePtr node)
{
    struct role *role;

    role = make_role(NULL, NULL, NULL);

    // properties
    role->name = (char *)xmlGetProp(node, (xmlChar *)"Name");
    role->description = (char *)xmlGetProp(node, (xmlChar *)"Description");
    role->admin_role = (char *)xmlGetProp(node, (xmlChar *)"Admin");

    long member;
    char *command;
    // commands & members
    node = node->xmlChildrenNode;
    while (node != NULL) {
        if ((xmlMatches(node->name, "Member"))) {
            member = xmlGetLongProp(node, "ID", 0);
            if (member == 0 || !player_idnum_exists(member)) {
                errlog("Invalid PID %ld not loaded.", member);
            } else {
                add_role_member(role, member);
            }
        }
        if ((xmlMatches(node->name, "Command"))) {
            command = (char *)xmlGetProp(node, (xmlChar *)"Name");
            int index = find_command(command);
            if (index == -1) {
                errlog("Role(xmlNodePtr): command %s not found", command);
            } else {
                add_role_command(role, &cmd_info[index]);
            }
            free(command);
        }
        node = node->next;
    }

    return role;
}

/*
 * Create the required xmlnodes to recreate this role
 */
bool
save_role_to_xml(struct role *role, xmlNodePtr parent)
{
    xmlNodePtr node = NULL;

    parent = xmlNewChild(parent, NULL, (const xmlChar *)"Role", NULL);
    xmlSetProp(parent, (xmlChar *)"Name", (xmlChar *)role->name);
    xmlSetProp(parent, (xmlChar *)"Description", (xmlChar *)role->description);
    xmlSetProp(parent, (xmlChar *)"Admin", (xmlChar *)role->admin_role);

    for(GList *cit = role->commands; cit; cit = cit->next) {
        node = xmlNewChild(parent, NULL, (const xmlChar *)"Command", NULL);
        struct command_info *cmd = (struct command_info *)cit->data;
        xmlSetProp(node, (xmlChar *)"Name", (const xmlChar *)cmd->command);
    }
    for(GList *cit = role->members; cit; cit = cit->next) {
        node = xmlNewChild(parent, NULL, (const xmlChar *)"Member", NULL);
        char *name = player_name_by_idnum(GPOINTER_TO_INT(cit->data));
        char *id_str = tmp_sprintf("%d", GPOINTER_TO_INT(cit->data));
        if (name) {
            xmlSetProp(node, (xmlChar *)"Name", (xmlChar *)name);
            xmlSetProp(node, (xmlChar *)"ID", (xmlChar *)id_str);
        }
    }
        
    return true;
}