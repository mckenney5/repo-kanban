/* See LICENSE file for copyright and license details. */
#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

#include <cJSON.h>

#define BOARD_FILE ".kanban-board"
#define EXIT_USAGE   1
#define EXIT_BOARD   2
#define EXIT_NOTFOUND 3

static int
board_init(void)
{
	FILE *f;
	cJSON *root, *cols;
	char *json;

	f = fopen(BOARD_FILE, "r");
	if (f) {
		fclose(f);
		return 0;
	}

	root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "version", 1);

	cols = cJSON_CreateStringArray(columns, NUM_COLUMNS);
	cJSON_AddItemToObject(root, "columns", cols);

	cJSON_AddNumberToObject(root, "next_id", 1);
	cJSON_AddItemToObject(root, "tickets", cJSON_CreateArray());

	json = cJSON_Print(root);
	f = fopen(BOARD_FILE, "w");
	if (!f) {
		cJSON_Delete(root);
		free(json);
		return EXIT_BOARD;
	}
	fprintf(f, "%s\n", json);
	fclose(f);
	cJSON_Delete(root);
	free(json);
	printf("%s\n", SUCCESS_MSG);
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
	FILE *f;
	char *json;

	f = fopen(BOARD_FILE, "w");
	if (!f)
		return EXIT_BOARD;
	json = cJSON_Print(root);
	fprintf(f, "%s\n", json);
	fclose(f);
	free(json);
	return 0;
}

static cJSON *
parse_csv_int(const char *s)
{
	cJSON *arr;
	char buf[256], *tok, *end;
	long n;

	arr = cJSON_CreateArray();
	strncpy(buf, s, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	tok = strtok(buf, ",");
	while (tok) {
		n = strtoul(tok, &end, 10);
		if (!*end)
			cJSON_AddItemToArray(arr, cJSON_CreateNumber((int)n));
		tok = strtok(NULL, ",");
	}
	return arr;
}

static cJSON *
parse_csv_str(const char *s)
{
	cJSON *arr;
	char buf[256], *tok;

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
	cJSON *root, *tickets, *next_id_obj, *ticket;
	int id;
	time_t now;
	char ts[64], *end;
	long p;
	int ret;

	optind = 3;
	while ((c = getopt_long(argc, argv, "t:d:p:a:g:b:", long_opts, 0))
	    != -1) {
		if (!optarg)
			continue;
		switch (c) {
		case 't': title = optarg; break;
		case 'd': desc = optarg; break;
		case 'p': prio_str = optarg; break;
		case 'a': assignee = optarg; break;
		case 'g': tags_str = optarg; break;
		case 'b': blocked_str = optarg; break;
		}
	}

	if (!title) {
		fprintf(stderr, MSG_TITLE_REQUIRED);
		return 1;
	}

	root = board_load();
	if (!root) {
		if (board_init())
			return EXIT_BOARD;
		root = board_load();
		if (!root)
			return EXIT_BOARD;
	}

	tickets = cJSON_GetObjectItem(root, "tickets");
	next_id_obj = cJSON_GetObjectItem(root, "next_id");
	id = next_id_obj->valueint;
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
		p = strtoul(prio_str, &end, 10);
		if (!*end)
			cJSON_AddNumberToObject(ticket, "priority", (int)p);
	}
	if (assignee)
		cJSON_AddStringToObject(ticket, "assignee", assignee);
	if (tags_str)
		cJSON_AddItemToObject(ticket, "tags",
		    parse_csv_str(tags_str));
	if (blocked_str)
		cJSON_AddItemToObject(ticket, "blocked_by",
		    parse_csv_int(blocked_str));

	cJSON_AddItemToArray(tickets, ticket);

	ret = board_save(root);
	cJSON_Delete(root);
	if (!ret)
		printf("%s\n", SUCCESS_MSG);
	return ret ? 1 : 0;
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

	root = board_load();
	if (!root) {
		fprintf(stderr, MSG_NO_BOARD);
		return EXIT_BOARD;
	}

	tickets = cJSON_GetObjectItem(root, "tickets");
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
				snprintf(new_desc, sizeof(new_desc),
				    "%s\nFORCE: %s",
				    desc_item->valuestring,
				    force_reason);
				cJSON_SetValuestring(desc_item,
				    new_desc);
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

static int
board_edit(int argc, char *argv[])
{
	char *end, *end2;
	long id;
	const char *field, *value;
	cJSON *root, *tickets, *ticket, *existing, *updated;
	long num;
	time_t now;
	char ts[64];
	int ret;

	if (argc < 6) {
		fprintf(stderr, MSG_USAGE_EDIT);
		return 1;
	}

	id = strtoul(argv[3], &end, 10);
	if (*end || id < 1) {
		fprintf(stderr, MSG_INVALID_ID);
		return 1;
	}

	field = argv[4];
	value = argv[5];

	root = board_load();
	if (!root) {
		fprintf(stderr, MSG_NO_BOARD);
		return EXIT_BOARD;
	}

	tickets = cJSON_GetObjectItem(root, "tickets");
	ticket = find_ticket(tickets, (int)id);
	if (!ticket) {
		fprintf(stderr, MSG_TICKET_NOT_FOUND, id);
		cJSON_Delete(root);
		return EXIT_NOTFOUND;
	}

	existing = cJSON_GetObjectItem(ticket, field);
	if (existing) {
		if (cJSON_IsNumber(existing)) {
			num = strtoul(value, &end2, 10);
			if (*end2) {
				fprintf(stderr, MSG_INVALID_NUMBER, value);
				cJSON_Delete(root);
				return 1;
			}
			cJSON_SetIntValue(existing, (int)num);
		} else if (cJSON_IsString(existing)) {
			cJSON_SetValuestring(existing, value);
		} else {
			fprintf(stderr, MSG_CANT_EDIT_FIELD, field);
			cJSON_Delete(root);
			return 1;
		}
	} else {
		cJSON_AddStringToObject(ticket, field, value);
	}

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

	root = board_load();
	if (!root) {
		fprintf(stderr, MSG_NO_BOARD);
		return EXIT_BOARD;
	}

	tickets = cJSON_GetObjectItem(root, "tickets");
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

	root = board_load();
	if (!root) {
		fprintf(stderr, MSG_NO_BOARD);
		return EXIT_BOARD;
	}

	tickets = cJSON_GetObjectItem(root, "tickets");
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
	char *json;

	root = board_load();
	if (!root) {
		printf(PLAN_STATS_EMPTY "\n");
		return 0;
	}

	tickets = cJSON_GetObjectItem(root, "tickets");
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

static int
board_archive_done(void)
{
	cJSON *root, *tickets, *t, *s, *u;
	int n, i, changed;
	time_t now;
	char ts[64];
	int ret;

	root = board_load();
	if (!root)
		return 0;

	tickets = cJSON_GetObjectItem(root, "tickets");
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
	FILE *f;
	int n, i;
	const char *title, *status, *assignee;
	char *json;

	root = board_load();
	if (!root) {
		f = fopen(BOARD_FILE, "r");
		if (f) {
			fclose(f);
			fprintf(stderr, MSG_INVALID_BOARD);
			return EXIT_BOARD;
		}
		if (pretty)
			printf(PRETTY_HEADER_FMT,
			    PRETTY_HEADER_ID, PRETTY_HEADER_TITLE,
			    PRETTY_HEADER_STATUS, PRETTY_HEADER_ASSIGNEE);
		else
			printf(PLAN_LIST_EMPTY "\n");
		return 0;
	}

	if (pretty) {
		tickets = cJSON_GetObjectItem(root, "tickets");
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

int
main(int argc, char *argv[])
{
	int is_build, is_plan, i;
	const char *cmd;
	int pretty = 0;

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

	for (i = 3; i < argc; i++)
		if (strcmp(argv[i], "--pretty") == 0) {
			pretty = 1;
			break;
		}

	if (is_build && strcmp(cmd, "init") == 0)
		return board_init() ? 1 : 0;

	if (is_build && strcmp(cmd, "add") == 0)
		return board_add(argc, argv);

	if (is_build && strcmp(cmd, "move") == 0)
		return board_move(argc, argv);

	if (is_build && strcmp(cmd, "edit") == 0)
		return board_edit(argc, argv);

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

	if (is_plan && strcmp(cmd, "export") == 0)
		return plan_export();

	print_usage();
	return 1;
}
