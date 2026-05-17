#ifndef CONFIG_H
#define CONFIG_H

/* Columns - must be in order from first to last */
/* WARNING: Changing the columns can break compatibility with your current boards */
static const char *columns[] = {
	"backlog", "ready", "in-progress", "QA", "done", "archive"
};
#define NUM_COLUMNS (sizeof(columns) / sizeof(columns[0]))

/* Gate column - required before moving to done/archive */
/* This is to stop clankers from blowing past QA when they make changes */
#define GATE_COLUMN "QA"

/* Success confirmation - printed on successful build commands */
#define SUCCESS_MSG "ok"

/* Error messages */
#define MSG_TITLE_REQUIRED       "Error: --title is required\n"
#define MSG_INVALID_ID           "Error: invalid ticket id\n"
#define MSG_INVALID_STATUS       "Error: invalid status '%s'\n"
#define MSG_NO_BOARD             "Error: no board found\n"
#define MSG_TICKET_NOT_FOUND     "Error: ticket %ld not found\n"
#define MSG_NOT_IN_GATE          "Error: ticket %ld must be in '%s' before moving to '%s'\nBefore trying to overide this, ask the user."
#define MSG_INVALID_NUMBER       "Error: '%s' is not a valid number\n"
#define MSG_CANT_EDIT_FIELD      "Error: cannot edit field '%s'\n"
#define MSG_INVALID_BOARD        "Error: invalid board file\n"
#define MSG_BLOCKED_WARNING      "Warning: ticket %d is blocked by ticket %d (status: %s)\n"

/* Usage messages */
#define MSG_USAGE_MOVE           "Usage: rkb build move <id> <status> [--force <reason>]\n"
#define MSG_USAGE_EDIT           "Usage: rkb build edit <id> <field> <value>\n"
#define MSG_USAGE_RM             "Usage: rkb build rm <id>\n"
#define MSG_USAGE_SHOW           "Usage: rkb plan show <id>\n"

#define MSG_USAGE_HEADER         "Usage: rkb build <command> [options]\n       rkb plan  <command> [options]\n"
#define MSG_USAGE_COMMANDS       "Commands:\n" \
"  build init          create board file\n" \
"  build add           add a ticket\n" \
"  build move <id> <s> [--force <r>] change ticket status\n" \
"  build edit <i> <f> <v> edit ticket field\n" \
"  build rm <id>       remove ticket\n" \
"  build archive-done  move done -> archive\n" \
"  plan list           list tickets\n" \
"  plan show <id>     show ticket\n" \
"  plan stats         column counts\n" \
"  plan export        dump board JSON\n"

/* Pretty print */
#define PRETTY_HEADER_TICKET_FMT         "%-3d %-40s %-12s %s\n"
#define PRETTY_HEADER_FMT         "%-3s %-40s %-12s %s\n"
#define PRETTY_HEADER_ID          "ID"
#define PRETTY_HEADER_TITLE       "Title"
#define PRETTY_HEADER_STATUS      "Status"
#define PRETTY_HEADER_ASSIGNEE    "Assignee"

/* Plan command defaults (JSON output) */
#define PLAN_STATS_EMPTY         "{ \"total\": 0, \"by_column\": {}, \"blocked\": 0, \"unassigned\": 0 }"
#define PLAN_LIST_EMPTY          "{ \"tickets\": [] }"

#endif
