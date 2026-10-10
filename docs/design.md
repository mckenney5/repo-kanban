# rkb — Repository Kanban Board

## Overview

`rkb` is an offline, repo-level kanban board CLI tool designed for AI agents. It reads/writes `./.kanban-board`, a single JSON file stored in the repository root (added to `.gitignore`). The purpose is to break complex programming challenges into smaller tickets that AI agents can pick up and work on.

## Language

- **C99** with **gcc**
- Single file: `rkb.c`
- Dynamically linked (small binary = faster invocation)
- Snapshot: about ~50KB binary

## Build System

- **Makefile** with targets:
  - `all` — build `rkb` binary
  - `init-submodules` — `git submodule update --init`
  - `clean` — remove build artifacts
  - `install` — copy binary to `/usr/local/bin/rkb`, man page to `/usr/local/share/man/man1/`
  - `uninstall` — remove both
  - `test` — run shell test suite
  - `memcheck` — run tests under valgrind
  - `lint` — static analysis

- Compiler flags: `-std=c99 -Wall -Wextra -pedantic -Werror -O2`

## Dependencies

- **cJSON** — vendored as a git submodule at `lib/cJSON` (https://github.com/DaveGamble/cJSON.git)
- **POSIX** — `getopt`, `strftime`, `time`, `open`, `read`, `write`

## File Format

Single JSON file at `./.kanban-board`:

```json
{
  "version": 1,
  "columns": ["backlog", "ready", "in-progress", "QA", "done", "archive"],
  "next_id": 1,
  "tickets": []
}
```

### Ticket Schema

| Field | Type | Required | Description |
|---|---|---|---|
| `id` | unsigned int | yes | Sequential, starts at 1 |
| `title` | string | yes | Short description |
| `description` | string | no | Full details |
| `status` | string | yes | One of the 6 columns |
| `priority` | int (1-5) | no | Sort order |
| `assignee` | string | no | Agent name |
| `created` | string (ISO 8601) | yes | Creation timestamp |
| `updated` | string (ISO 8601) | yes | Last modified timestamp |
| `tags` | array of strings | no | Labels for grouping |
| `blocked_by` | array of ints | no | Ticket IDs this ticket depends on |

## Behavior

- **Implicit init** — if `./.kanban-board` does not exist and a `build` command is issued, it is created automatically with empty columns
- **Blocked-by** — when moving a ticket, if any blocking tickets are not `done`, a warning is printed to stderr but the move proceeds
- **Output** — JSON to stdout by default, `--pretty` flag for human-readable table
- **Errors/warnings** — printed to stderr
- **Exit code** — 0 on success, non-zero on error

## CLI

### build subcommands (mutate state)

| Command | Description |
|---|---|
| `rkb build init` | Create board file if missing (no-op otherwise) |
| `rkb build add --title "..." [--description] [--priority N] [--assignee] [--tags a,b] [--blocked-by 1,3]` | Add ticket to backlog |
| `rkb build move <id> <status>` | Change ticket status (warns if blocked) |
| `rkb build edit <id> <field> <value>` | Append to a ticket field (`description`, `tags`, `blocked_by`) |
| `rkb build overwrite <id> <field> <value>` | Replace a ticket field; `""` clears it |
| `rkb build rm <id>` | Delete a ticket |
| `rkb build archive-done` | Move all `done` tickets to `archive` |

### plan subcommands (read-only)

| Command | Description |
|---|---|
| `rkb plan list [--pretty]` | List all tickets as JSON (or a table with `--pretty`) |
| `rkb plan show <id>` | Show full ticket details as JSON |
| `rkb plan stats` | Column counts and summary |
| `rkb plan lint [--json]` | Report board problems (read-only; exit 1 if issues found) |
| `rkb plan export` | Dump the entire board as JSON |

Note: `--pretty` is a table renderer, not a filter. Column filtering is left to `jq`:
`rkb plan list | jq '[.tickets[] | select(.status == "ready")]'`.

## Code Style

- **Naming:** `snake_case`, no prefix
- **No comments** unless absolutely necessary
- **Suck-less philosophy:**
  - Minimal LOC
  - Stack allocation, minimal `malloc`/`free`
  - Fixed-size buffers, linear scans
  - Single pass to understand the whole program
  - No frameworks, no bloat

## Testing

- Shell scripts in `tests/` directory
- `make test` runs all test scripts
- `make memcheck` runs all tests under valgrind to detect leaks

## Documentation

- `kanban.1` — classic groff man page
- Installed to `/usr/local/share/man/man1/` via `make install`
