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
#define MSG_NOT_IN_GATE          "Error: ticket %ld must be in '%s' before moving to '%s'\n" \
                                  "Before trying to override this, ask the user.\n"
#define MSG_INVALID_NUMBER       "Error: '%s' is not a valid number\n"
#define MSG_INVALID_PRIORITY     "Error: priority must be 1-5, got '%s'\n"
#define MSG_CANT_EDIT_FIELD      "Error: cannot edit field '%s'\n"
#define MSG_FIELD_PROTECTED      "Error: field '%s' is managed by rkb and cannot be edited\n"
#define MSG_USE_MOVE_STATUS      "Error: field 'status' cannot be set with 'edit'\n" \
                                  "Use: rkb build move %ld %s\n"
#define MSG_UNKNOWN_FIELD        "Error: unknown field '%s'\n" \
                                  "Valid fields: title, description, assignee, priority, tags, blocked_by\n"
#define MSG_INVALID_BOARD        "Error: invalid board file\n"
#define MSG_BOARD_BAD_FIELD      "Error: board field '%s' is missing or the wrong type\n"
#define MSG_BOARD_WRITE          "Error: could not write %s (%s)\n"
#define MSG_BOARD_BAD_RANGE      "Error: board field '%s' is out of range: %g\n"
#define MSG_TICKET_BAD_ID        "Error: ticket id is out of range: %g\n"
#define MSG_ID_SPACE_EXHAUSTED   "Error: id space exhausted (next_id is %d); refusing to wrap\n"
#define MSG_TICKET_MISSING_FIELD "Error: ticket %ld: missing required field '%s'\n"
#define MSG_TICKET_BAD_STATUS    "Error: ticket %ld: status '%s' is not a board column\n"
#define MSG_TICKET_BAD_TYPE      "Error: ticket %ld: field '%s' has the wrong type\n"
#define MSG_TICKET_DUP_KEY       "Error: ticket %ld: duplicate key '%s'\n"
#define MSG_TICKET_DUP_ID        "Error: duplicate ticket id %ld; every ticket must have a unique id\n"
#define MSG_BOARD_DUP_KEY        "Error: board field '%s' is duplicated\n"
#define MSG_BOARD_UNKNOWN_KEY    "Error: unknown board field '%s'\n"
#define MSG_VALUE_TOO_LONG       "Error: %s is longer than %d bytes; refusing to truncate\n"
#define MSG_BLOCKED_WARNING      "Warning: ticket %d is blocked by ticket %d (status: %s)\n"

/* Usage messages */
#define MSG_USAGE_MOVE           "Usage: rkb build move <id> <status> [--force <reason>]\n"
#define MSG_USAGE_EDIT           "Usage: rkb build edit <id> <field> <value>\n" \
                                  "       Appends to description, tags and blocked_by.\n" \
                                  "       Put -- before a value that starts with '-'\n"
#define MSG_USAGE_OVERWRITE      "Usage: rkb build overwrite <id> <field> <value>\n" \
                                  "       Replaces the field's value. Use \"\" to clear it.\n" \
                                  "       Put -- before a value that starts with '-'\n"
#define MSG_USAGE_RM             "Usage: rkb build rm <id>\n"
#define MSG_USAGE_SHOW           "Usage: rkb plan show <id>\n"
#define MSG_USAGE_ADD            "Usage: rkb build add --title <title> [--description <text>]\n" \
                                  "       [--priority <1-5>] [--assignee <name>]\n" \
                                  "       [--tags <a,b>] [--blocked-by <id,id>]\n"
#define MSG_USAGE_PLAN_LIST      "Usage: rkb plan list [--pretty]\n"
#define MSG_USAGE_PLAN_STATS     "Usage: rkb plan stats\n"
#define MSG_USAGE_PLAN_EXPORT    "Usage: rkb plan export\n"
#define MSG_USAGE_PLAN_LINT      "Usage: rkb plan lint [--json]\n"
#define MSG_USAGE_INIT           "Usage: rkb build init\n"
#define MSG_USAGE_ARCHIVE_DONE   "Usage: rkb build archive-done\n"
#define MSG_UNKNOWN_OPTION       "Error: unknown option '%s'\n"
#define MSG_MISSING_VALUE        "Error: option '%s' requires a value\n"
#define MSG_LOOKS_LIKE_FLAG      "Error: '%s' looks like an option, not a value\n" \
                                  "Put -- before it to pass a value starting with '-'\n"
#define MSG_USE_OVERWRITE        "Error: 'build edit' appends, but '%s' holds a single value\n" \
                                  "Use: rkb build overwrite %ld %s %s\n"
#define MSG_NO_CHANGE            "ok (no change)\n"
#define MSG_LITERAL_NEWLINE      "Warning: '%s' contains %d literal \\n escape(s); " \
                                  "stored verbatim\n"

#define MSG_USAGE_HEADER         "Usage: rkb build <command> [options]\n       rkb plan  <command> [options]\n"
#define MSG_USAGE_COMMANDS       "Commands:\n" \
"  build init          create board file\n" \
"  build add           add a ticket\n" \
"  build move <id> <s> [--force <r>] change ticket status\n" \
"  build edit <i> <f> <v> append to a ticket field\n" \
"  build overwrite <i> <f> <v> replace a ticket field\n" \
"  build rm <id>       remove ticket\n" \
"  build archive-done  move done -> archive\n" \
"  plan list           list tickets\n" \
"  plan show <id>     show ticket\n" \
"  plan stats         column counts\n" \
"  plan lint          report board problems\n" \
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
