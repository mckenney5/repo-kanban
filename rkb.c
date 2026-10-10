/* See LICENSE file for copyright and license details. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

#include <cJSON.h>

#define BOARD_FILE ".kanban-board"
#define CSV_BUF_SIZE 255
#define EXIT_USAGE   1
#define EXIT_BOARD   2
#define EXIT_NOTFOUND 3

/* Writes the board atomically: the JSON goes to a temp file that is fully written and
 * fsync'd, then renamed over the board. rename(2) is atomic within a filesystem, so an
 * interrupted save or a concurrent reader sees either the whole old board or the whole
 * new one - never a truncated file.
 *
 * The temp name is unique per process. A fixed ".kanban-board.tmp" let two writers
 * truncate and interleave the same inode, so one could rename a file the other was still
 * writing. This does NOT make concurrent rkb safe: both writers still read the board
 * before either renames, so the last rename wins and the earlier ticket is lost. That is
 * documented as out of contract - see the single-writer note in README.md. */
/* Every path that fails to write the board reports it, then cleans up. errno is still
 * valid at all six call sites, which is the point: without this, a full disk and a
 * permissions problem produce byte-identical, contentless output, and the operator
 * cannot tell them apart. `tmpl` is NULL when no temp file was created.
 *
 * Closing the descriptor stays at the call site rather than here: the
 * write/flush/fsync path has already closed it via fclose(), so a helper that closed
 * unconditionally would double-close. */
static int
board_write_fail(const char *tmpl)
{
	fprintf(stderr, MSG_BOARD_WRITE, BOARD_FILE, strerror(errno));
	if (tmpl)
		unlink(tmpl);
	return EXIT_BOARD;
}

static int
board_write(const char *json)
{
	FILE *f;
	char tmpl[] = BOARD_FILE ".XXXXXX";
	mode_t mask;
	int fd;

	fd = mkstemp(tmpl);
	if (fd < 0)
		return board_write_fail(NULL);
	/* mkstemp creates 0600; the board has always been 0666 masked by umask, so
	 * restore that rather than silently making it owner-only. */
	mask = umask(0);
	umask(mask);
	if (fchmod(fd, (mode_t)(0666 & ~mask)) != 0) {
		close(fd);
		return board_write_fail(tmpl);
	}
	f = fdopen(fd, "w");
	if (!f) {
		close(fd);
		return board_write_fail(tmpl);
	}
	if (fprintf(f, "%s\n", json) < 0 || fflush(f) != 0 || fsync(fd) != 0) {
		fclose(f);
		return board_write_fail(tmpl);
	}
	if (fclose(f) != 0)
		return board_write_fail(tmpl);
	if (rename(tmpl, BOARD_FILE) != 0)
		return board_write_fail(tmpl);
	return 0;
}

static cJSON *
board_load(void)
{
	FILE *f;
	long len;
	char *buf;
	size_t n;
	cJSON *root;

	f = fopen(BOARD_FILE, "r");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	rewind(f);
	buf = malloc(len + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	n = fread(buf, 1, len, f);
	buf[n] = 0;
	fclose(f);
	root = cJSON_Parse(buf);
	free(buf);
	return root;
}

static int
board_save(cJSON *root)
{
	char *json;
	int ret;

	json = cJSON_Print(root);
	if (!json)
		return EXIT_BOARD;
	ret = board_write(json);
	free(json);
	return ret;
}


static int
is_valid_column(const char *s)
{
	size_t i;

	for (i = 0; i < NUM_COLUMNS; i++)
		if (strcmp(s, columns[i]) == 0)
			return 1;
	return 0;
}

/* The ticket schema, in one place. Validation, the `build edit` allow-list and
 * `plan lint`'s known-key set are all derived from this table so they cannot drift
 * apart. `status` is required because the old code dereferenced it without a check, so
 * its absence was a segfault rather than an error. */
enum field_type {
	FIELD_NUMBER,
	FIELD_STRING,
	FIELD_STRING_ARRAY,
	FIELD_NUMBER_ARRAY
};

static const struct ticket_field {
	const char *name;
	enum field_type type;
	int required;
} ticket_schema[] = {
	{"id",          FIELD_NUMBER,       1},
	{"title",       FIELD_STRING,        1},
	{"status",      FIELD_STRING,        1},
	{"created",     FIELD_STRING,        1},
	{"updated",     FIELD_STRING,        1},
	{"description", FIELD_STRING,        0},
	{"assignee",    FIELD_STRING,        0},
	{"priority",    FIELD_NUMBER,        0},
	{"tags",        FIELD_STRING_ARRAY,  0},
	{"blocked_by",  FIELD_NUMBER_ARRAY,  0}
};
#define NUM_SCHEMA (sizeof(ticket_schema) / sizeof(ticket_schema[0]))

/* `id` is the primary key, `created`/`updated` are timestamps rkb maintains itself, and
 * `status` belongs to `build move`, which owns its column validation, the QA gate and
 * the blocked-by warning. None of them are writable via `build edit`. */
static const char *protected_fields[] = {
	"id", "created", "updated", "status"
};
#define NUM_PROTECTED (sizeof(protected_fields) / sizeof(protected_fields[0]))

static const struct ticket_field *
find_schema_field(const char *name)
{
	size_t i;

	for (i = 0; i < NUM_SCHEMA; i++)
		if (strcmp(ticket_schema[i].name, name) == 0)
			return &ticket_schema[i];
	return NULL;
}

static int
is_protected_field(const char *name)
{
	size_t i;

	for (i = 0; i < NUM_PROTECTED; i++)
		if (strcmp(protected_fields[i], name) == 0)
			return 1;
	return 0;
}

/* Checks the JSON type of a value against what the schema declares. */
static int
type_matches(enum field_type want, cJSON *v)
{
	switch (want) {
	case FIELD_NUMBER:       return cJSON_IsNumber(v);
	case FIELD_STRING:       return cJSON_IsString(v);
	case FIELD_STRING_ARRAY: return cJSON_IsArray(v);
	case FIELD_NUMBER_ARRAY: return cJSON_IsArray(v);
	}
	return 0;
}

static long
ticket_id_of(cJSON *t)
{
	cJSON *id;

	id = cJSON_GetObjectItem(t, "id");
	return id ? id->valueint : 0;
}

/* The board envelope's keys, used by lint to spot fields it does not know. Unknown
 * envelope keys are reported but not refused, matching how unknown *ticket* keys are
 * treated: a board may legitimately carry more than rkb requires. */
static const char *root_fields[] = {
	"version", "columns", "next_id", "tickets"
};
#define NUM_ROOT_FIELDS (sizeof(root_fields) / sizeof(root_fields[0]))

static int
is_root_field(const char *name)
{
	size_t i;

	for (i = 0; i < NUM_ROOT_FIELDS; i++)
		if (strcmp(name, root_fields[i]) == 0)
			return 1;
	return 0;
}

/* An id has to be a positive integer an int can hold. Checking only cJSON_IsNumber is
 * not enough: cJSON saturates valueint on overflow (lib/cJSON/cJSON.c:388-392), so a
 * next_id of 99999999999999 arrives as INT_MAX, id + 1 overflows, and
 * cJSON_SetIntValue writes both fields - leaving a negative next_id that also passes a
 * type check. One number permanently corrupts the board that way. */
static int
id_value_ok(cJSON *v)
{
	double d;

	if (!cJSON_IsNumber(v))
		return 0;
	d = v->valuedouble;
	return d >= 1 && d <= INT_MAX && d == (double)(int)d;
}


/* Optional fields are type-checked when present, so no command can be handed a
 * non-string where it expects one and silently no-op a write. */
/* Checks every present field against its declared type. Required fields are handled by
 * validate_tickets; this covers both required and optional ones so a non-string where
 * a string is expected can never be silently accepted. */
static int
check_field_types(cJSON *t, long id)
{
	cJSON *v;
	size_t i;

	for (i = 0; i < NUM_SCHEMA; i++) {
		v = cJSON_GetObjectItem(t, ticket_schema[i].name);
		if (v && !type_matches(ticket_schema[i].type, v)) {
			fprintf(stderr, MSG_TICKET_BAD_TYPE, id,
			    ticket_schema[i].name);
			return 1;
		}
	}
	return 0;
}

/* Returns the name of the first duplicated key in t, or NULL. cJSON's parser accepts
 * duplicate object keys and GetObjectItem returns the first match, so a wrong-typed or
 * contradictory value placed after a valid one is invisible to every field check - and
 * to every command. rkb never writes duplicates itself (all writes go through cJSON
 * setters), so this only fires on a board something else edited. */
static const char *
find_duplicate_key(cJSON *t)
{
	cJSON *a, *b;

	for (a = t->child; a; a = a->next) {
		if (!a->string)
			continue;
		for (b = a->next; b; b = b->next)
			if (b->string && strcmp(a->string, b->string) == 0)
				return a->string;
	}
	return NULL;
}

static int
check_duplicate_keys(cJSON *t, long id)
{
	const char *dup;

	dup = find_duplicate_key(t);
	if (!dup)
		return 0;
	fprintf(stderr, MSG_TICKET_DUP_KEY, id, dup);
	return 1;
}

/* True if the ticket at index `i` shares its id with a later ticket. find_ticket
 * returns whichever match it reaches first, so a duplicated id makes every edit, move,
 * rm and show target an arbitrary one of the two. Validation refuses such a board;
 * plan lint reports it. One predicate so the two cannot disagree. */
static int
id_duplicated_at(cJSON *tickets, int i, long *id)
{
	cJSON *t, *other;
	long this_id;
	int j, n;

	n = cJSON_GetArraySize(tickets);
	t = cJSON_GetArrayItem(tickets, i);
	this_id = ticket_id_of(t);
	if (!this_id)
		return 0;
	for (j = i + 1; j < n; j++) {
		other = cJSON_GetArrayItem(tickets, j);
		if (ticket_id_of(other) == this_id) {
			*id = this_id;
			return 1;
		}
	}
	return 0;
}

static int
validate_tickets(cJSON *tickets)
{
	cJSON *t, *v;
	long id, dup;
	size_t i, j;
	int n;

	n = cJSON_GetArraySize(tickets);
	for (i = 0; i < (size_t)n; i++) {
		t = cJSON_GetArrayItem(tickets, i);
		id = ticket_id_of(t);
		if (check_duplicate_keys(t, id))
			return 1;
		if (id_duplicated_at(tickets, (int)i, &dup)) {
			fprintf(stderr, MSG_TICKET_DUP_ID, dup);
			return 1;
		}
		for (j = 0; j < NUM_SCHEMA; j++) {
			v = cJSON_GetObjectItem(t, ticket_schema[j].name);
			if ((!v || cJSON_IsNull(v))
			    && ticket_schema[j].required) {
				fprintf(stderr, MSG_TICKET_MISSING_FIELD,
				    id, ticket_schema[j].name);
				return 1;
			}
		}
		if (check_field_types(t, id))
			return 1;
		/* A ticket id that is a number but not a usable one still passes
		 * the type check, and find_ticket would happily match it. */
		v = cJSON_GetObjectItem(t, "id");
		if (v && !id_value_ok(v)) {
			fprintf(stderr, MSG_TICKET_BAD_ID, v->valuedouble);
			return 1;
		}
		v = cJSON_GetObjectItem(t, "status");
		if (!is_valid_column(v->valuestring)) {
			fprintf(stderr, MSG_TICKET_BAD_STATUS, id,
			    v->valuestring);
			return 1;
		}
	}
	return 0;
}

/* Root-level keys. `next_id` matters most: board_add used to dereference it with no
 * check, so a board missing it passed ticket validation and then segfaulted. */
static int
check_root_keys(cJSON *root)
{
	cJSON *v;

	v = cJSON_GetObjectItem(root, "version");
	if (!v || !cJSON_IsNumber(v)) {
		fprintf(stderr, MSG_BOARD_BAD_FIELD, "version");
		return 1;
	}
	v = cJSON_GetObjectItem(root, "next_id");
	if (!v || !cJSON_IsNumber(v)) {
		fprintf(stderr, MSG_BOARD_BAD_FIELD, "next_id");
		return 1;
	}
	if (!id_value_ok(v)) {
		fprintf(stderr, MSG_BOARD_BAD_RANGE, "next_id",
		    v->valuedouble);
		return 1;
	}
	/* only presence and type here: whether the contents match this build is a
	 * lint finding, so an existing board is never refused over it */
	v = cJSON_GetObjectItem(root, "columns");
	if (!v || !cJSON_IsArray(v)) {
		fprintf(stderr, MSG_BOARD_BAD_FIELD, "columns");
		return 1;
	}
	return 0;
}

static cJSON *
check_board(cJSON *root)
{
	cJSON *tickets;
	const char *dup;

	/* find_duplicate_key walks an object's children, so it works on the envelope
	 * as well as on a ticket. cJSON keeps the first of a repeated key and the
	 * validation below only ever looked at the first, so `"next_id":5,
	 * "next_id":"abc"` was accepted, written to, and reported clean. */
	dup = find_duplicate_key(root);
	if (dup) {
		fprintf(stderr, MSG_BOARD_DUP_KEY, dup);
		return NULL;
	}
	if (check_root_keys(root))
		return NULL;
	tickets = cJSON_GetObjectItem(root, "tickets");
	if (!tickets || !cJSON_IsArray(tickets)) {
		fprintf(stderr, MSG_INVALID_BOARD);
		return NULL;
	}
	if (validate_tickets(tickets))
		return NULL;
	return tickets;
}

/* Loads the board, distinguishing "no board file at all" from "board file present but
 * unparseable". Callers report those very differently - the second is a corrupt board,
 * not an empty one. On a parse failure the error is printed here. *missing is set only
 * when no board file exists. */
static cJSON *
board_load_classified(int *missing)
{
	FILE *f;
	cJSON *root;

	*missing = 0;
	root = board_load();
	if (root)
		return root;
	f = fopen(BOARD_FILE, "r");
	if (f) {
		fclose(f);
		fprintf(stderr, MSG_INVALID_BOARD);
		return NULL;
	}
	*missing = 1;
	return NULL;
}

/* Loads the board and validates it. Returns the tickets array, or NULL after
 * reporting the problem; the caller owns *root_out and must cJSON_Delete it. */
static cJSON *
board_open(cJSON **root_out)
{
	cJSON *root, *tickets;

	root = board_load();
	if (!root) {
		fprintf(stderr, MSG_NO_BOARD);
		return NULL;
	}
	tickets = check_board(root);
	if (!tickets) {
		cJSON_Delete(root);
		return NULL;
	}
	*root_out = root;
	return tickets;
}

/* Creates the board, or validates the existing one. Returning 0 for a board that exists
 * but cannot be read would leave `build init` unable to tell a usable board from a
 * broken one. */
static int
board_init(void)
{
	FILE *f;
	cJSON *root, *cols, *tickets;
	char *json;
	int ret;

	f = fopen(BOARD_FILE, "r");
	if (f) {
		fclose(f);
		root = board_load();
		if (!root) {
			fprintf(stderr, MSG_INVALID_BOARD);
			return EXIT_BOARD;
		}
		tickets = check_board(root);
		cJSON_Delete(root);
		return tickets ? 0 : EXIT_BOARD;
	}

	root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "version", 1);

	cols = cJSON_CreateStringArray(columns, NUM_COLUMNS);
	cJSON_AddItemToObject(root, "columns", cols);

	cJSON_AddNumberToObject(root, "next_id", 1);
	cJSON_AddItemToObject(root, "tickets", cJSON_CreateArray());

	json = cJSON_Print(root);
	ret = json ? board_write(json) : EXIT_BOARD;
	free(json);
	cJSON_Delete(root);
	if (ret)
		return ret;
	printf("%s\n", SUCCESS_MSG);
	return 0;
}

/* Fields `build edit` may write: any schema field that is not tool-managed. `status` is
 * excluded because `build move` owns it, along with its column validation, the QA gate
 * and the blocked-by warning. */
static int
is_editable_field(const char *s)
{
	return find_schema_field(s) != NULL && !is_protected_field(s);
}

/* Fields `build edit` appends to: prose that accumulates, and sets that union.
 * `title`, `assignee` and `priority` hold a single value, where appending would
 * concatenate nonsense, so `edit` refuses those and points at `build overwrite`. */
static int
is_appendable_field(const char *s)
{
	return strcmp(s, "description") == 0
	    || strcmp(s, "tags") == 0
	    || strcmp(s, "blocked_by") == 0;
}
/* Parses a non-negative integer that fits an int. A plain `*end` check is not enough:
 * strtoul accepts a leading '-' and returns the negation, so "-1" would pass, and an
 * overflowing value truncates to -1 on the way into an int. Returns 0 on success. */
static int
parse_uint(const char *s, long *out)
{
	char *end;
	long n;

	if (!s || !*s || s[0] == '-')
		return -1;
	errno = 0;
	n = strtoul(s, &end, 10);
	/* errno must be checked, not the value: on overflow strtoul returns
	 * ULONG_MAX, which converts to -1 in a signed long and so slips past a
	 * range comparison. */
	if (*end || errno == ERANGE || n > INT_MAX)
		return -1;
	*out = n;
	return 0;
}

/* Comma-separated ids. Returns NULL and copies the first offending token into `bad`
 * rather than dropping it: "1,junk" must not quietly become [1]. An empty string is
 * valid and yields an empty array, which is how a field gets cleared. */
/* Both CSV parsers copy through a fixed buffer, so an over-long list used to store a
 * silent 255-byte prefix - cutting the last tag to whatever fitted - and still report
 * "ok". *toolong distinguishes that from a bad token so the caller can say which. */
static cJSON *
parse_csv_int(const char *s, char *bad, size_t badsz, int *toolong)
{
	cJSON *arr;
	char buf[CSV_BUF_SIZE + 1], *tok;
	long n;

	*toolong = 0;
	if (strlen(s) >= sizeof(buf)) {
		*toolong = 1;
		return NULL;
	}
	arr = cJSON_CreateArray();
	strncpy(buf, s, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	tok = strtok(buf, ",");
	while (tok) {
		if (parse_uint(tok, &n) != 0) {
			snprintf(bad, badsz, "%s", tok);
			cJSON_Delete(arr);
			return NULL;
		}
		cJSON_AddItemToArray(arr, cJSON_CreateNumber((int)n));
		tok = strtok(NULL, ",");
	}
	return arr;
}

/* Returns NULL and sets *toolong when the input will not fit; it never needs `bad`,
 * since a string token has nothing to reject. An empty string is valid and yields an
 * empty array, which is how a field gets cleared. */
static cJSON *
parse_csv_str(const char *s, char *bad, size_t badsz, int *toolong)
{
	cJSON *arr;
	char buf[CSV_BUF_SIZE + 1], *tok;

	(void)bad;
	(void)badsz;
	*toolong = 0;
	if (strlen(s) >= sizeof(buf)) {
		*toolong = 1;
		return NULL;
	}
	arr = cJSON_CreateArray();
	strncpy(buf, s, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	tok = strtok(buf, ",");
	while (tok) {
		cJSON_AddItemToArray(arr, cJSON_CreateString(tok));
		tok = strtok(NULL, ",");
	}
	return arr;
}

/* Reports whichever way a CSV parse failed. One place, so the two parsers cannot
 * disagree about what an over-long value is called. */
static void
report_csv_error(const char *field, const char *bad, int toolong)
{
	if (toolong)
		fprintf(stderr, MSG_VALUE_TOO_LONG, field, CSV_BUF_SIZE);
	else
		fprintf(stderr, MSG_INVALID_NUMBER, bad);
}

/* Counts literal \n sequences - a backslash and an 'n', rather than a real newline.
 * An agent piping a jq-produced string hands these over constantly and never finds out,
 * because rkb stores them faithfully and prints "ok". A doubled backslash is an escaped
 * backslash, not the start of an escape, so it is skipped along with its partner. */
static int
count_literal_newlines(const char *s)
{
	int n, i;

	n = 0;
	for (i = 0; s[i]; i++) {
		if (s[i] != '\\')
			continue;
		if (s[i + 1] == '\\') {
			i++;
			continue;
		}
		if (s[i + 1] == 'n')
			n++;
	}
	return n;
}

/* The value is never rewritten - decoding it would break pasting code snippets into a
 * ticket, which is the whole reason this warns instead of fixing. */
static void
warn_literal_newlines(const char *field, const char *value)
{
	int n;

	n = count_literal_newlines(value);
	if (n)
		printf(MSG_LITERAL_NEWLINE, field, n);
}

static int
board_add(int argc, char *argv[])
{
	static struct option long_opts[] = {
		{"title", required_argument, 0, 't'},
		{"description", required_argument, 0, 'd'},
		{"priority", required_argument, 0, 'p'},
		{"assignee", required_argument, 0, 'a'},
		{"tags", required_argument, 0, 'g'},
		{"blocked-by", required_argument, 0, 'b'},
		{0, 0, 0, 0}
	};
	const char *title = NULL, *desc = NULL, *prio_str = NULL;
	const char *assignee = NULL, *tags_str = NULL, *blocked_str = NULL;
	int c;
	cJSON *root, *tickets, *next_id_obj, *ticket, *arr;
	int id;
	time_t now;
	char ts[64], badtok[CSV_BUF_SIZE + 1];
	long p;
	int ret, toolong;
	int created = 0;

	optind = 3;
	opterr = 0;
	/* The leading ':' makes getopt return ':' rather than '?' for a missing
	 * argument, so it can be told apart from an unknown option. */
	while ((c = getopt_long(argc, argv, ":t:d:p:a:g:b:", long_opts, 0))
	    != -1) {
		switch (c) {
		case 't': title = optarg; break;
		case 'd': desc = optarg; break;
		case 'p': prio_str = optarg; break;
		case 'a': assignee = optarg; break;
		case 'g': tags_str = optarg; break;
		case 'b': blocked_str = optarg; break;
		case ':':
			fprintf(stderr, MSG_MISSING_VALUE,
			    argv[optind - 1]);
			return EXIT_USAGE;
		default:
			fprintf(stderr, MSG_UNKNOWN_OPTION,
			    argv[optind - 1]);
			return EXIT_USAGE;
		}
	}

	if (!title) {
		fprintf(stderr, MSG_TITLE_REQUIRED);
		return EXIT_USAGE;
	}

	root = board_load();
	if (!root) {
		if (board_init())
			return EXIT_BOARD;
		created = 1;	/* board_init already reported success */
		root = board_load();
		if (!root)
			return EXIT_BOARD;
	}

	tickets = check_board(root);
	if (!tickets) {
		cJSON_Delete(root);
		return EXIT_BOARD;
	}

	next_id_obj = cJSON_GetObjectItem(root, "next_id");
	id = next_id_obj->valueint;
	if (id == INT_MAX) {
		fprintf(stderr, MSG_ID_SPACE_EXHAUSTED, id);
		cJSON_Delete(root);
		return EXIT_BOARD;
	}
	cJSON_SetIntValue(next_id_obj, id + 1);

	now = time(0);
	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

	ticket = cJSON_CreateObject();
	cJSON_AddNumberToObject(ticket, "id", id);
	cJSON_AddStringToObject(ticket, "title", title);
	cJSON_AddStringToObject(ticket, "status", "backlog");
	cJSON_AddStringToObject(ticket, "created", ts);
	cJSON_AddStringToObject(ticket, "updated", ts);

	if (desc)
		cJSON_AddStringToObject(ticket, "description", desc);
	if (prio_str) {
		/* was silently dropped when unparseable: the ticket was created with no
		 * priority and the command still said "ok" */
		if (parse_uint(prio_str, &p) != 0 || p < 1 || p > 5) {
			fprintf(stderr, MSG_INVALID_PRIORITY, prio_str);
			cJSON_Delete(root);
			return EXIT_USAGE;
		}
		cJSON_AddNumberToObject(ticket, "priority", (int)p);
	}
	if (assignee)
		cJSON_AddStringToObject(ticket, "assignee", assignee);
	if (tags_str) {
		arr = parse_csv_str(tags_str, badtok, sizeof(badtok),
		    &toolong);
		if (!arr) {
			report_csv_error("tags", badtok, toolong);
			cJSON_Delete(ticket);
			cJSON_Delete(root);
			return EXIT_USAGE;
		}
		cJSON_AddItemToObject(ticket, "tags", arr);
	}
	if (blocked_str) {
		arr = parse_csv_int(blocked_str, badtok, sizeof(badtok),
		    &toolong);
		if (!arr) {
			report_csv_error("blocked_by", badtok, toolong);
			cJSON_Delete(ticket);
			cJSON_Delete(root);
			return EXIT_USAGE;
		}
		cJSON_AddItemToObject(ticket, "blocked_by", arr);
	}

	cJSON_AddItemToArray(tickets, ticket);

	ret = board_save(root);
	cJSON_Delete(root);
	if (ret)
		return ret;	/* EXIT_BOARD, not EXIT_USAGE: a failed save is
				 * not a bad argument, and the message now says so */
	if (!created)
		printf("%s\n", SUCCESS_MSG);
	if (desc)
		warn_literal_newlines("description", desc);
	return 0;
}

static cJSON *
find_ticket(cJSON *tickets, int id)
{
	int n, i;
	cJSON *t, *id_item;

	n = cJSON_GetArraySize(tickets);
	for (i = 0; i < n; i++) {
		t = cJSON_GetArrayItem(tickets, i);
		id_item = cJSON_GetObjectItem(t, "id");
		if (id_item && id_item->valueint == id)
			return t;
	}
	return NULL;
}

/* Replaces `node`'s string with its old value plus one more line. Length is computed
 * rather than using a fixed buffer: a fixed buffer truncates silently, which is the
 * defect this change exists to stop. */
static int
append_line(cJSON *node, const char *prefix, const char *old, const char *line)
{
	char *joined;
	size_t need;

	need = strlen(old) + strlen(prefix) + strlen(line) + 3;
	joined = malloc(need);
	if (!joined)
		return EXIT_BOARD;
	if (prefix && *prefix)
		snprintf(joined, need, "%s\n%s%s", old, prefix, line);
	else
		snprintf(joined, need, "%s\n%s", old, line);
	cJSON_SetValuestring(node, joined);
	free(joined);
	return 0;
}

static int
board_move(int argc, char *argv[])
{
	char *end;
	long id;
	const char *status, *force_reason;
	cJSON *root, *tickets, *ticket, *blocked_by, *b, *blocker, *bs;
	cJSON *status_item, *updated, *desc_item;
	int i, n, blocker_id;
	time_t now;
	char ts[64], new_desc[512];
	int ret;

	force_reason = NULL;

	if (argc < 5) {
		fprintf(stderr, MSG_USAGE_MOVE);
		return 1;
	}

	for (i = 3; i < argc - 1; i++)
		if (strcmp(argv[i], "--force") == 0) {
			force_reason = argv[i + 1];
			break;
		}

	id = strtoul(argv[3], &end, 10);
	if (*end || id < 1) {
		fprintf(stderr, MSG_INVALID_ID);
		return 1;
	}

	status = argv[4];
	if (!is_valid_column(status)) {
		fprintf(stderr, MSG_INVALID_STATUS, status);
		return 1;
	}

	tickets = board_open(&root);
	if (!tickets)
		return EXIT_BOARD;
	ticket = find_ticket(tickets, (int)id);
	if (!ticket) {
		fprintf(stderr, MSG_TICKET_NOT_FOUND, id);
		cJSON_Delete(root);
		return EXIT_NOTFOUND;
	}

	blocked_by = cJSON_GetObjectItem(ticket, "blocked_by");
	if (blocked_by && cJSON_IsArray(blocked_by)) {
		n = cJSON_GetArraySize(blocked_by);
		for (i = 0; i < n; i++) {
			b = cJSON_GetArrayItem(blocked_by, i);
			blocker_id = b->valueint;
			blocker = find_ticket(tickets, blocker_id);
			if (blocker) {
				bs = cJSON_GetObjectItem(blocker, "status");
				if (bs && strcmp(bs->valuestring,
				    "done") != 0) {
					fprintf(stderr, MSG_BLOCKED_WARNING,
					    (int)id, blocker_id,
					    bs->valuestring);
				}
			}
		}
	}

	if (strcmp(status, "done") == 0
	    || strcmp(status, "archive") == 0) {
		status_item = cJSON_GetObjectItem(ticket, "status");
		if (strcmp(status_item->valuestring, GATE_COLUMN) != 0) {
			if (!force_reason) {
				fprintf(stderr, MSG_NOT_IN_GATE,
				    id, GATE_COLUMN, status);
				cJSON_Delete(root);
				return 1;
			}
			desc_item = cJSON_GetObjectItem(ticket,
			    "description");
			if (desc_item) {
				ret = append_line(desc_item, "FORCE: ",
				    desc_item->valuestring,
				    force_reason);
				if (ret) {
					cJSON_Delete(root);
					return ret;
				}
			} else {
				snprintf(new_desc, sizeof(new_desc),
				    "FORCE: %s", force_reason);
				cJSON_AddStringToObject(ticket,
				    "description", new_desc);
			}
		}
	}

	status_item = cJSON_GetObjectItem(ticket, "status");
	cJSON_SetValuestring(status_item, status);

	now = time(0);
	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
	updated = cJSON_GetObjectItem(ticket, "updated");
	cJSON_SetValuestring(updated, ts);

	ret = board_save(root);
	cJSON_Delete(root);
	if (!ret)
		printf("%s\n", SUCCESS_MSG);
	return ret ? EXIT_BOARD : 0;
}

/* True if arg looks like an option rather than a value. */
static int
looks_like_flag(const char *s)
{
	return s[0] == '-' && s[1] != 0;
}

/* Gathers the positional arguments for the subcommands that take them, so an option
 * can never be silently stored as data (`build edit 1 title --pretty` used to set the
 * title to the literal string "--pretty"). "--" ends option parsing, which is the
 * escape hatch for a value that really does start with a dash.
 * Returns the count stored, -1 with *bad set to the offending argument if an option
 * appeared where a value was expected, or -2 if there were more positionals than fit. */
static int
collect_positionals(int argc, char *argv[], char **pos, int max,
    const char **bad)
{
	int i, n;

	*bad = NULL;
	for (i = 3, n = 0; i < argc; i++) {
		if (strcmp(argv[i], "--") == 0) {
			for (i++; i < argc; i++) {
				if (n == max)
					return -2;
				pos[n++] = argv[i];
			}
			break;
		}
		if (looks_like_flag(argv[i])) {
			*bad = argv[i];
			return -1;
		}
		if (n == max)
			return -2;
		pos[n++] = argv[i];
	}
	return n;
}

/* True if an array already holds this value. Used by append so re-adding a tag does
 * not duplicate it. */
static int
array_contains(cJSON *arr, cJSON *probe, int is_number)
{
	cJSON *e;
	int i, n;

	n = cJSON_GetArraySize(arr);
	for (i = 0; i < n; i++) {
		e = cJSON_GetArrayItem(arr, i);
		if (is_number) {
			if (cJSON_IsNumber(e) && cJSON_IsNumber(probe)
			    && e->valueint == probe->valueint)
				return 1;
		} else if (cJSON_IsString(e) && cJSON_IsString(probe)
		    && e->valuestring && probe->valuestring
		    && strcmp(e->valuestring, probe->valuestring) == 0)
			return 1;
	}
	return 0;
}

/* The one place a ticket field is written, shared by `build edit` (append) and
 * `build overwrite` (replace) so their validation cannot drift apart.
 * Returns 0 when the board was written, 1 when an append added nothing, or an
 * EXIT_* code on error. */
static int
board_set_field(int argc, char *argv[], int append, const char *usage)
{
	char *end;
	char *pos[3];
	long id;
	const char *field, *value, *bad;
	cJSON *root, *tickets, *ticket, *existing, *updated;
	cJSON *parsed, *item, *arr;
	const struct ticket_field *sf;
	long num;
	time_t now;
	char ts[64], badtok[CSV_BUF_SIZE + 1];
	int ret, n, i, is_num, added, toolong;

	n = collect_positionals(argc, argv, pos, 3, &bad);
	if (n == -1) {
		fprintf(stderr, MSG_LOOKS_LIKE_FLAG, bad);
		return EXIT_USAGE;
	}
	if (n < 3) {
		fprintf(stderr, "%s", usage);
		return EXIT_USAGE;
	}

	id = strtoul(pos[0], &end, 10);
	if (*end || id < 1) {
		fprintf(stderr, MSG_INVALID_ID);
		return EXIT_USAGE;
	}

	field = pos[1];
	value = pos[2];

	if (!is_editable_field(field)) {
		if (strcmp(field, "id") == 0
		    || strcmp(field, "created") == 0
		    || strcmp(field, "updated") == 0)
			fprintf(stderr, MSG_FIELD_PROTECTED, field);
		else if (strcmp(field, "status") == 0)
			fprintf(stderr, MSG_USE_MOVE_STATUS, id, value);
		else
			fprintf(stderr, MSG_UNKNOWN_FIELD, field);
		return EXIT_USAGE;
	}

	tickets = board_open(&root);
	if (!tickets)
		return EXIT_BOARD;
	ticket = find_ticket(tickets, (int)id);
	if (!ticket) {
		fprintf(stderr, MSG_TICKET_NOT_FOUND, id);
		cJSON_Delete(root);
		return EXIT_NOTFOUND;
	}

	/* `edit` only appends to fields that accumulate. For a single-valued field
	 * appending would concatenate nonsense ("alice bob"), so point at overwrite.
	 * Checked after the board and ticket are known, so an unreadable board still
	 * reports itself rather than blaming the argument. */
	if (append && !is_appendable_field(field)) {
		fprintf(stderr, MSG_USE_OVERWRITE, field, id, field, value);
		cJSON_Delete(root);
		return EXIT_USAGE;
	}

	added = 1;
	sf = find_schema_field(field);
	is_num = sf->type == FIELD_NUMBER_ARRAY;

	existing = cJSON_GetObjectItem(ticket, field);
	if (existing) {
		if (cJSON_IsNumber(existing)) {
			/* the only numeric field is priority, so the 1-5 range is
			 * the only range that can apply here */
			if (parse_uint(value, &num) != 0 || num < 1 || num > 5) {
				fprintf(stderr, MSG_INVALID_PRIORITY, value);
				cJSON_Delete(root);
				return EXIT_USAGE;
			}
			cJSON_SetIntValue(existing, (int)num);
		} else if (cJSON_IsString(existing)) {
			if (append) {
				ret = append_line(existing, "",
				    existing->valuestring, value);
				if (ret) {
					cJSON_Delete(root);
					return ret;
				}
			} else {
				cJSON_SetValuestring(existing, value);
			}
		} else if (cJSON_IsArray(existing)) {
			/* Parse before touching `existing`. Overwriting used to empty the
			 * array first and parse second, which was only safe because a
			 * parse failure could not yet happen. */
			parsed = is_num
			    ? parse_csv_int(value, badtok, sizeof(badtok),
			      &toolong)
			    : parse_csv_str(value, badtok, sizeof(badtok),
			      &toolong);
			if (parsed == NULL) {
				report_csv_error(field, badtok, toolong);
				cJSON_Delete(root);
				return EXIT_USAGE;
			}
			if (append) {
				/* union-add: skip elements already present */
				added = 0;
				for (;;) {
					item = cJSON_DetachItemFromArray(
					    parsed, 0);
					if (!item)
						break;
					if (array_contains(existing, item,
					    is_num)) {
						cJSON_Delete(item);
						continue;
					}
					cJSON_AddItemToArray(existing,
					    item);
					added++;
				}
				cJSON_Delete(parsed);
			} else {
				n = cJSON_GetArraySize(existing);
				for (i = 0; i < n; i++)
					cJSON_DeleteItemFromArray(existing,
					    0);
				for (;;) {
					item = cJSON_DetachItemFromArray(
					    parsed, 0);
					if (!item)
						break;
					cJSON_AddItemToArray(existing,
					    item);
				}
				cJSON_Delete(parsed);
			}
		} else {
			fprintf(stderr, MSG_CANT_EDIT_FIELD, field);
			cJSON_Delete(root);
			return EXIT_USAGE;
		}
	} else {
		/* Absent field: create it with the type the schema declares, so a
		 * number never lands where an array belongs. */
		switch (sf->type) {
		case FIELD_NUMBER:
			if (parse_uint(value, &num) != 0 || num < 1 || num > 5) {
				fprintf(stderr, MSG_INVALID_PRIORITY, value);
				cJSON_Delete(root);
				return EXIT_USAGE;
			}
			cJSON_AddNumberToObject(ticket, field, (int)num);
			break;
		case FIELD_STRING_ARRAY:
			arr = parse_csv_str(value, badtok, sizeof(badtok),
			    &toolong);
			if (!arr) {
				report_csv_error(field, badtok, toolong);
				cJSON_Delete(root);
				return EXIT_USAGE;
			}
			cJSON_AddItemToObject(ticket, field, arr);
			break;
		case FIELD_NUMBER_ARRAY:
			arr = parse_csv_int(value, badtok, sizeof(badtok),
			    &toolong);
			if (!arr) {
				report_csv_error(field, badtok, toolong);
				cJSON_Delete(root);
				return EXIT_USAGE;
			}
			cJSON_AddItemToObject(ticket, field, arr);
			break;
		default:
			cJSON_AddStringToObject(ticket, field, value);
			break;
		}
	}

	now = time(0);
	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
	updated = cJSON_GetObjectItem(ticket, "updated");
	cJSON_SetValuestring(updated, ts);

	ret = board_save(root);
	cJSON_Delete(root);
	if (ret)
		return EXIT_BOARD;
	/* An append that added nothing is not a failure, but saying plain "ok" would
	 * claim work that did not happen. */
	if (append && !added)
		printf("%s", MSG_NO_CHANGE);
	else
		printf("%s\n", SUCCESS_MSG);
	warn_literal_newlines(field, value);
	return 0;
}

static int
board_edit(int argc, char *argv[])
{
	return board_set_field(argc, argv, 1, MSG_USAGE_EDIT);
}

static int
board_overwrite(int argc, char *argv[])
{
	return board_set_field(argc, argv, 0, MSG_USAGE_OVERWRITE);
}

static int
board_rm(int argc, char *argv[])
{
	char *end;
	long id;
	cJSON *root, *tickets, *t, *id_item;
	int n, i, found;
	int ret;

	if (argc < 4) {
		fprintf(stderr, MSG_USAGE_RM);
		return 1;
	}

	id = strtoul(argv[3], &end, 10);
	if (*end || id < 1) {
		fprintf(stderr, MSG_INVALID_ID);
		return 1;
	}

	tickets = board_open(&root);
	if (!tickets)
		return EXIT_BOARD;
	n = cJSON_GetArraySize(tickets);
	found = -1;
	for (i = 0; i < n; i++) {
		t = cJSON_GetArrayItem(tickets, i);
		id_item = cJSON_GetObjectItem(t, "id");
		if (id_item && id_item->valueint == (int)id) {
			found = i;
			break;
		}
	}

	if (found < 0) {
		fprintf(stderr, MSG_TICKET_NOT_FOUND, id);
		cJSON_Delete(root);
		return EXIT_NOTFOUND;
	}

	cJSON_DeleteItemFromArray(tickets, found);
	ret = board_save(root);
	cJSON_Delete(root);
	if (!ret)
		printf("%s\n", SUCCESS_MSG);
	return ret ? EXIT_BOARD : 0;
}

static int
plan_show(int argc, char *argv[])
{
	char *end;
	long id;
	cJSON *root, *tickets, *ticket;
	char *json;

	if (argc < 4) {
		fprintf(stderr, MSG_USAGE_SHOW);
		return 1;
	}

	id = strtoul(argv[3], &end, 10);
	if (*end || id < 1) {
		fprintf(stderr, MSG_INVALID_ID);
		return 1;
	}

	tickets = board_open(&root);
	if (!tickets)
		return EXIT_BOARD;
	ticket = find_ticket(tickets, (int)id);
	if (!ticket) {
		fprintf(stderr, MSG_TICKET_NOT_FOUND, id);
		cJSON_Delete(root);
		return EXIT_NOTFOUND;
	}

	json = cJSON_Print(ticket);
	printf("%s\n", json);
	free(json);
	cJSON_Delete(root);
	return 0;
}

static int
plan_stats(void)
{
	cJSON *root, *tickets, *t, *s, *a, *bb, *b, *blocker, *bs;
	cJSON *out, *bc;
	int total, bn, is_blocked;
	size_t i, j, k;
	int counts[NUM_COLUMNS];
	int blocked = 0, unassigned = 0;
	int missing;
	char *json;

	root = board_load_classified(&missing);
	if (!root) {
		if (!missing)
			return EXIT_BOARD;
		printf(PLAN_STATS_EMPTY "\n");
		return 0;
	}

	tickets = check_board(root);
	if (!tickets) {
		cJSON_Delete(root);
		return EXIT_BOARD;
	}

	total = cJSON_GetArraySize(tickets);
	for (i = 0; i < NUM_COLUMNS; i++)
		counts[i] = 0;

	for (i = 0; i < (size_t)total; i++) {
		t = cJSON_GetArrayItem(tickets, i);
		s = cJSON_GetObjectItem(t, "status");
		for (j = 0; j < NUM_COLUMNS; j++)
			if (strcmp(s->valuestring, columns[j]) == 0) {
				counts[j]++;
				break;
			}

		a = cJSON_GetObjectItem(t, "assignee");
		if (!a || !a->valuestring
		    || strlen(a->valuestring) == 0)
			unassigned++;

		bb = cJSON_GetObjectItem(t, "blocked_by");
		if (bb && cJSON_IsArray(bb)
		    && cJSON_GetArraySize(bb) > 0) {
			is_blocked = 0;
			bn = cJSON_GetArraySize(bb);
			for (k = 0; k < (size_t)bn; k++) {
				b = cJSON_GetArrayItem(bb, k);
				blocker = find_ticket(tickets,
				    b->valueint);
				if (blocker) {
					bs = cJSON_GetObjectItem(
					    blocker, "status");
					if (strcmp(bs->valuestring,
					    "done") != 0) {
						is_blocked = 1;
						break;
					}
				}
			}
			if (is_blocked)
				blocked++;
		}
	}

	out = cJSON_CreateObject();
	cJSON_AddNumberToObject(out, "total", total);

	bc = cJSON_CreateObject();
	for (i = 0; i < NUM_COLUMNS; i++)
		cJSON_AddNumberToObject(bc, columns[i], counts[i]);
	cJSON_AddItemToObject(out, "by_column", bc);

	cJSON_AddNumberToObject(out, "blocked", blocked);
	cJSON_AddNumberToObject(out, "unassigned", unassigned);

	json = cJSON_Print(out);
	printf("%s\n", json);
	free(json);
	cJSON_Delete(out);
	cJSON_Delete(root);
	return 0;
}

#define MAX_FINDINGS 256
#define BOARD_TICKET (-1L)	/* a finding about the board, not a ticket */

struct finding {
	long ticket;
	char message[192];
};

static void
add_finding(struct finding *f, int *n, long ticket, const char *fmt, ...)
{
	va_list ap;

	/* A silent cap reported "issues": 256 for a board with 400 problems, which
	 * reads as "there are 256 things wrong" rather than "there are more than I
	 * can show". Callers report the truncation separately. */
	if (*n >= MAX_FINDINGS)
		return;
	f[*n].ticket = ticket;
	va_start(ap, fmt);
	vsnprintf(f[*n].message, sizeof(f[*n].message), fmt, ap);
	va_end(ap);
	(*n)++;
}

/* Reports every array element of the wrong kind, e.g. a tag that is not a string. The
 * container check in check_field_types cannot see these. */
static void
lint_array_elements(struct finding *f, int *n, cJSON *arr, long id,
    const char *name, int want_number)
{
	cJSON *e;
	int k, cnt;

	cnt = cJSON_GetArraySize(arr);
	for (k = 0; k < cnt; k++) {
		e = cJSON_GetArrayItem(arr, k);
		if (want_number ? !cJSON_IsNumber(e) : !cJSON_IsString(e))
			add_finding(f, n, id,
			    "'%s' has an element that is not %s", name,
			    want_number ? "a number" : "a string");
	}
}

static void
lint_root(struct finding *f, int *n, cJSON *root)
{
	cJSON *v, *e, *tickets, *t, *child;
	int i, cnt, nt;
	long next_id;
	const char *dup;

	dup = find_duplicate_key(root);
	if (dup)
		add_finding(f, n, BOARD_TICKET, "duplicate board key '%s'", dup);

	for (child = root->child; child; child = child->next)
		if (child->string && !is_root_field(child->string))
			add_finding(f, n, BOARD_TICKET,
			    "unknown board field '%s'", child->string);

	v = cJSON_GetObjectItem(root, "version");
	if (!v || !cJSON_IsNumber(v))
		add_finding(f, n, BOARD_TICKET,
		    "board field 'version' is missing or not a number");
	v = cJSON_GetObjectItem(root, "next_id");
	if (!v || !cJSON_IsNumber(v))
		add_finding(f, n, BOARD_TICKET,
		    "board field 'next_id' is missing or not a number");
	else if (!id_value_ok(v))
		add_finding(f, n, BOARD_TICKET,
		    "board field 'next_id' is out of range: %g",
		    v->valuedouble);
	else {
		/* next_id is the id the *next* add will use, so it normally has no
		 * ticket. The inconsistency is a ticket already at or past it,
		 * which build add would hand out a second time. */
		next_id = v->valueint;
		tickets = cJSON_GetObjectItem(root, "tickets");
		if (tickets && cJSON_IsArray(tickets)) {
			nt = cJSON_GetArraySize(tickets);
			for (i = 0; i < nt; i++) {
				t = cJSON_GetArrayItem(tickets, i);
				if (ticket_id_of(t) >= next_id)
					add_finding(f, n, ticket_id_of(t),
					    "id %ld is not below next_id %ld",
					    ticket_id_of(t), next_id);
			}
		}
	}

	v = cJSON_GetObjectItem(root, "columns");
	if (!v || !cJSON_IsArray(v)) {
		add_finding(f, n, BOARD_TICKET,
		    "board field 'columns' is missing or not an array");
		return;
	}
	/* The board's own column list is written but never read; is_valid_column uses the
	 * compiled-in list. A board from a different rkb build would have every ticket's
	 * status rejected, so say so plainly instead of reporting 40 bad statuses. */
	cnt = cJSON_GetArraySize(v);
	if (cnt != (int)NUM_COLUMNS) {
		add_finding(f, n, BOARD_TICKET,
		    "board declares %d columns, this rkb expects %d", cnt,
		    (int)NUM_COLUMNS);
		return;
	}
	for (i = 0; i < (int)NUM_COLUMNS; i++) {
		e = cJSON_GetArrayItem(v, i);
		if (!cJSON_IsString(e)) {
			add_finding(f, n, BOARD_TICKET,
			    "board column %d is not a string", i);
			return;
		}
		if (strcmp(e->valuestring, columns[i]) != 0) {
			add_finding(f, n, BOARD_TICKET,
			    "board column %d is '%s', this rkb expects '%s'",
			    i, e->valuestring, columns[i]);
			return;
		}
	}
}

static void
lint_ticket(struct finding *f, int *n, cJSON *tickets, cJSON *t, long id)
{
	cJSON *child, *v, *e;
	const char *dup;
	size_t i;
	int k, cnt;

	dup = find_duplicate_key(t);
	if (dup)
		add_finding(f, n, id, "duplicate key '%s'", dup);

	/* Every key not in the schema. This is the finding nothing else can produce:
	 * the old `build edit` created these silently, and every read path looks up
	 * known keys by name, so a shadow key is invisible until now. */
	for (child = t->child; child; child = child->next) {
		if (child->string && !find_schema_field(child->string))
			add_finding(f, n, id, "unknown key '%s'", child->string);
	}

	for (i = 0; i < NUM_SCHEMA; i++) {
		v = cJSON_GetObjectItem(t, ticket_schema[i].name);
		if (!v || cJSON_IsNull(v)) {
			if (ticket_schema[i].required)
				add_finding(f, n, id, "missing field '%s'",
				    ticket_schema[i].name);
			continue;
		}
		if (!type_matches(ticket_schema[i].type, v)) {
			add_finding(f, n, id, "field '%s' has the wrong type",
			    ticket_schema[i].name);
			continue;
		}
		if (ticket_schema[i].type == FIELD_STRING_ARRAY)
			lint_array_elements(f, n, v, id,
			    ticket_schema[i].name, 0);
		else if (ticket_schema[i].type == FIELD_NUMBER_ARRAY) {
			lint_array_elements(f, n, v, id,
			    ticket_schema[i].name, 1);
			cnt = cJSON_GetArraySize(v);
			for (k = 0; k < cnt; k++) {
				e = cJSON_GetArrayItem(v, k);
				if (!cJSON_IsNumber(e))
					continue;
				/* a ticket blocking itself can never resolve,
				 * so it warns as blocked forever */
				if (e->valueint == id) {
					add_finding(f, n, id,
					    "'%s' contains this ticket's own id",
					    ticket_schema[i].name);
					continue;
				}
				if (find_ticket(tickets, e->valueint))
					continue;
				add_finding(f, n, id,
				    "'%s' references missing ticket %d",
				    ticket_schema[i].name, e->valueint);
			}
		}
	}

	v = cJSON_GetObjectItem(t, "id");
	if (v && cJSON_IsNumber(v) && !id_value_ok(v))
		add_finding(f, n, id, "ticket id is out of range: %g",
		    v->valuedouble);

	v = cJSON_GetObjectItem(t, "status");
	if (v && cJSON_IsString(v) && !is_valid_column(v->valuestring))
		add_finding(f, n, id, "'status' is not a board column: \"%s\"",
		    v->valuestring);

	/* Writes reject these, but a board written before that rule existed can
	 * still hold one, and it is not a lockout - just worth reporting. */
	v = cJSON_GetObjectItem(t, "priority");
	if (v && cJSON_IsNumber(v)
	    && (v->valuedouble < 1 || v->valuedouble > 5))
		add_finding(f, n, id, "'priority' is %d, outside the documented 1-5",
		    (int)v->valuedouble);
}

static int
plan_lint(int json)
{
	struct finding f[MAX_FINDINGS];
	cJSON *root, *tickets, *t;
	int nf = 0, n, i;
	int missing, truncated;
	long id;

	root = board_load_classified(&missing);
	if (!root) {
		if (missing)
			fprintf(stderr, MSG_NO_BOARD);
		return EXIT_BOARD;	/* else the message is already reported */
	}

	tickets = cJSON_GetObjectItem(root, "tickets");
	if (!tickets || !cJSON_IsArray(tickets)) {
		add_finding(f, &nf, BOARD_TICKET,
		    "board field 'tickets' is missing or not an array");
	} else {
		lint_root(f, &nf, root);
		n = cJSON_GetArraySize(tickets);
		for (i = 0; i < n; i++) {
			t = cJSON_GetArrayItem(tickets, i);
			lint_ticket(f, &nf, tickets, t, ticket_id_of(t));
		}
		/* two tickets sharing an id: find_ticket returns whichever it hits
		 * first, so every later edit or move targets the wrong one */
		for (i = 0; i < n; i++)
			if (id_duplicated_at(tickets, i, &id))
				add_finding(f, &nf, id,
				    "duplicate ticket id %ld", id);
	}

	/* If the array filled up, say so rather than letting the count imply the
	 * list is complete. */
	truncated = (nf == MAX_FINDINGS);

	if (json) {
		cJSON *out, *arr, *o;
		char *text;
		int k;

		out = cJSON_CreateObject();
		cJSON_AddStringToObject(out, "board", BOARD_FILE);
		cJSON_AddNumberToObject(out, "ok", nf == 0);
		cJSON_AddNumberToObject(out, "issues", nf);
		cJSON_AddNumberToObject(out, "truncated", truncated);
		arr = cJSON_CreateArray();
		for (k = 0; k < nf; k++) {
			o = cJSON_CreateObject();
			cJSON_AddNumberToObject(o, "ticket", (double)f[k].ticket);
			cJSON_AddStringToObject(o, "message", f[k].message);
			cJSON_AddItemToArray(arr, o);
		}
		cJSON_AddItemToObject(out, "findings", arr);
		text = cJSON_PrintUnformatted(out);
		if (text) {
			printf("%s\n", text);
			free(text);
		}
		cJSON_Delete(out);
	} else if (nf == 0) {
		printf("%s: no issues found\n", BOARD_FILE);
	} else {
		printf("%s: %d issue%s\n\n", BOARD_FILE, nf,
		    nf == 1 ? "" : "s");
		for (i = 0; i < nf; i++) {
			if (f[i].ticket == BOARD_TICKET)
				printf("board: %s\n", f[i].message);
			else
				printf("ticket %ld: %s\n", f[i].ticket,
				    f[i].message);
		}
		if (truncated)
			printf("\nWarning: stopped at %d findings; "
			    "more may exist\n", MAX_FINDINGS);
	}
	cJSON_Delete(root);
	return nf ? 1 : 0;
}

static int
board_archive_done(void)
{
	cJSON *root, *tickets, *t, *s, *u;
	int n, i, changed;
	time_t now;
	char ts[64];
	int ret, missing;

	/* This was the one mutating command that never adopted board_load_classified,
	 * so an absent board and an unreadable one both resolved to success. */
	root = board_load_classified(&missing);
	if (!root) {
		if (missing)
			fprintf(stderr, MSG_NO_BOARD);
		return EXIT_BOARD;	/* else the message is already reported */
	}

	tickets = check_board(root);
	if (!tickets) {
		cJSON_Delete(root);
		return EXIT_BOARD;
	}

	n = cJSON_GetArraySize(tickets);
	changed = 0;

	now = time(0);
	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

	for (i = 0; i < n; i++) {
		t = cJSON_GetArrayItem(tickets, i);
		s = cJSON_GetObjectItem(t, "status");
		if (strcmp(s->valuestring, "done") == 0) {
			cJSON_SetValuestring(s, "archive");
			u = cJSON_GetObjectItem(t, "updated");
			cJSON_SetValuestring(u, ts);
			changed = 1;
		}
	}

	if (changed) {
		ret = board_save(root);
		cJSON_Delete(root);
		if (!ret)
			printf("%s\n", SUCCESS_MSG);
		return ret ? EXIT_BOARD : 0;
	}
	cJSON_Delete(root);
	return 0;
}

static const char *
ticket_str(cJSON *t, const char *field, const char *def)
{
	cJSON *v;
	v = cJSON_GetObjectItem(t, field);
	return (v && v->valuestring) ? v->valuestring : def;
}

static int
plan_list(int pretty)
{
	cJSON *root, *tickets, *t, *id_j;
	int n, i, missing;
	const char *title, *status, *assignee;
	char *json;

	root = board_load_classified(&missing);
	if (!root) {
		if (!missing)
			return EXIT_BOARD;
		if (pretty)
			printf(PRETTY_HEADER_FMT,
			    PRETTY_HEADER_ID, PRETTY_HEADER_TITLE,
			    PRETTY_HEADER_STATUS, PRETTY_HEADER_ASSIGNEE);
		else
			printf(PLAN_LIST_EMPTY "\n");
		return 0;
	}

	tickets = check_board(root);
	if (!tickets) {
		cJSON_Delete(root);
		return EXIT_BOARD;
	}

	if (pretty) {
		n = cJSON_GetArraySize(tickets);
		printf(PRETTY_HEADER_FMT,
		    PRETTY_HEADER_ID, PRETTY_HEADER_TITLE,
		    PRETTY_HEADER_STATUS, PRETTY_HEADER_ASSIGNEE);
		for (i = 0; i < n; i++) {
			t = cJSON_GetArrayItem(tickets, i);
			id_j = cJSON_GetObjectItem(t, "id");
			title = ticket_str(t, "title", "");
			status = ticket_str(t, "status", "");
			assignee = ticket_str(t, "assignee", "-");
			printf(PRETTY_HEADER_TICKET_FMT,
			    id_j ? id_j->valueint : 0,
			    title, status, assignee);
		}
	} else {
		json = cJSON_Print(root);
		printf("%s\n", json);
		free(json);
	}
	cJSON_Delete(root);
	return 0;
}

static int
plan_export(void)
{
	FILE *f;
	long len;
	char *buf;
	size_t n;

	f = fopen(BOARD_FILE, "r");
	if (!f) {
		fprintf(stderr, MSG_NO_BOARD);
		return EXIT_BOARD;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	rewind(f);
	buf = malloc(len + 1);
	if (!buf) {
		fclose(f);
		return 1;
	}
	n = fread(buf, 1, len, f);
	buf[n] = 0;
	fclose(f);
	printf("%s", buf);
	free(buf);
	return 0;
}

static void
print_usage(void)
{
	fprintf(stderr, MSG_USAGE_HEADER);
	fprintf(stderr, MSG_USAGE_COMMANDS);
}

/* Per-subcommand usage, so `rkb build add --help` describes `add` instead of falling
 * through to the top-level block. Only subcommands that take arguments need an entry;
 * the rest are self-evident from the command list. */
static const char *
subcommand_usage(const char *group, const char *cmd)
{
	static const struct {
		const char *group;
		const char *cmd;
		const char *usage;
	} table[] = {
		{"build", "add", MSG_USAGE_ADD},
		{"build", "init", MSG_USAGE_INIT},
		{"build", "archive-done", MSG_USAGE_ARCHIVE_DONE},
		{"build", "move", MSG_USAGE_MOVE},
		{"build", "edit", MSG_USAGE_EDIT},
		{"build", "overwrite", MSG_USAGE_OVERWRITE},
		{"build", "rm", MSG_USAGE_RM},
		{"plan", "list", MSG_USAGE_PLAN_LIST},
		{"plan", "show", MSG_USAGE_SHOW},
		{"plan", "stats", MSG_USAGE_PLAN_STATS},
		{"plan", "lint", MSG_USAGE_PLAN_LINT},
		{"plan", "export", MSG_USAGE_PLAN_EXPORT}
	};
	size_t i;

	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++)
		if (strcmp(table[i].group, group) == 0
		    && strcmp(table[i].cmd, cmd) == 0)
			return table[i].usage;
	return NULL;
}

int
main(int argc, char *argv[])
{
	int is_build, is_plan, i;
	const char *cmd, *usage;
	int pretty = 0;
	int as_json = 0;

	if (argc < 2) {
		print_usage();
		return 1;
	}

	if (strcmp(argv[1], "--help") == 0
	    || strcmp(argv[1], "-h") == 0) {
		print_usage();
		return 0;
	}

	if (argc < 3) {
		print_usage();
		return 1;
	}

	is_build = (strcmp(argv[1], "build") == 0);
	is_plan  = (strcmp(argv[1], "plan") == 0);

	if (!is_build && !is_plan) {
		print_usage();
		return 1;
	}

	cmd = argv[2];

	/* Help is answered before any other argument is looked at, so it works even
	 * where the other arguments are wrong. */
	usage = subcommand_usage(argv[1], cmd);
	if (usage)
		for (i = 2; i < argc; i++)
			if (strcmp(argv[i], "--help") == 0
			    || strcmp(argv[i], "-h") == 0) {
				fprintf(stderr, "%s", usage);
				return 0;
			}

	for (i = 3; i < argc; i++)
		if (strcmp(argv[i], "--pretty") == 0) {
			pretty = 1;
			break;
		}

	for (i = 3; i < argc; i++)
		if (strcmp(argv[i], "--json") == 0) {
			as_json = 1;
			break;
		}

	if (is_build && strcmp(cmd, "init") == 0)
		return board_init();

	if (is_build && strcmp(cmd, "add") == 0)
		return board_add(argc, argv);

	if (is_build && strcmp(cmd, "move") == 0)
		return board_move(argc, argv);

	if (is_build && strcmp(cmd, "edit") == 0)
		return board_edit(argc, argv);

	if (is_build && strcmp(cmd, "overwrite") == 0)
		return board_overwrite(argc, argv);

	if (is_build && strcmp(cmd, "rm") == 0)
		return board_rm(argc, argv);

	if (is_build && strcmp(cmd, "archive-done") == 0)
		return board_archive_done();

	if (is_plan && strcmp(cmd, "list") == 0)
		return plan_list(pretty);

	if (is_plan && strcmp(cmd, "show") == 0)
		return plan_show(argc, argv);

	if (is_plan && strcmp(cmd, "stats") == 0)
		return plan_stats();

	if (is_plan && strcmp(cmd, "lint") == 0)
		return plan_lint(as_json);

	if (is_plan && strcmp(cmd, "export") == 0)
		return plan_export();

	print_usage();
	return 1;
}
