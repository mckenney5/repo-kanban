# rkb — Repository Kanban Board

An offline, repo-level kanban board CLI tool designed for AI agents. It reads/writes `./.kanban-board`, a single JSON file stored in the repository root (added to `.gitignore`). The purpose is to break complex programming challenges into smaller tickets that AI agents can pick up and work on.

## Overview
`rkb` is a lightweight, dependency-minimal kanban board implemented in C99. It operates entirely on a single JSON file (`.kanban-board`) in your repository root, making it perfect for tracking AI agent workflows without external dependencies.

## Features
- **Offline-first**: No network dependencies, everything stored locally
- **Single JSON board**: Human-readable and editable `.kanban-board` file
- **Dual-mode CLI**: Separate `build` (mutating) and `plan` (read-only) commands
- **Pretty output**: Optional `--pretty` flag for table-formatted output
- **Blocked-by tracking**: Dependency management with warnings
- **Archiving**: Move completed tickets to archive
- **Lightweight**: ~50KB binary, no frameworks, minimal allocations

## Installation

### From Source
```bash
git submodule update --init          # Fetch cJSON submodule
make                                 # Compile rkb binary
make install                         # Installs to /usr/local/bin/rkb and man page
```

### Build Targets
- `make` or `make all` - Build the `rkb` binary
- `make init-submodules` - Initialize/update git submodules
- `make test` - Run the shell test suite
- `make memcheck` - Run tests under valgrind for leak detection
- `make lint` - Run static analysis with cppcheck
- `make clean` - Remove build artifacts
- `make install` - Install binary and man page
- `make uninstall` - Remove installed files

## Usage

### Build Commands (Mutate State)
These commands modify the kanban board:

| Command | Description |
|---------|-------------|
| `rkb build init` | Create board file if missing (no-op otherwise) |
| `rkb build add --title "..." [--description] [--priority N] [--assignee] [--tags a,b] [--blocked-by 1,3]` | Add ticket to backlog |
| `rkb build move <id> <status>` | Change ticket status (warns if blocked) |
| `rkb build edit <id> <field> <value>` | Append to `description`, `tags` or `blocked_by` (never discards) |
| `rkb build overwrite <id> <field> <value>` | Replace any editable field; `""` clears it |
| `rkb build rm <id>` | Delete a ticket |
| `rkb build archive-done` | Move all `done` tickets to `archive` |

### Plan Commands (Read-Only)
These commands display board information without modification:

| Command | Description |
|---------|-------------|
| `rkb plan list [--pretty]` | List all tickets as JSON (or a table with `--pretty`) |
| `rkb plan show <id>` | Show full ticket details as JSON |
| `rkb plan stats` | Column counts and summary |
| `rkb plan lint [--json]` | Report board problems (read-only; exit 1 if issues found) |
| `rkb plan export` | Dump the entire board as JSON |

**Note**: Add `--pretty` to any plan command for human-readable table output instead of JSON.

## Board File Format
The kanban board is stored as a single JSON file at `./.kanban-board`:

> **One writer at a time.** `rkb` takes no locks, so two processes mutating the same
> board concurrently is **not supported** and can lose a ticket: each reads the board
> before either writes, and the last save wins. Saves are atomic, so the board file is
> never left half-written or corrupt — but a ticket added by the slower process may
> simply not be there. If several agents may work at once, serialise their board writes
> yourself; do not run `rkb build` in parallel against one board.

```json
{
  "version": 1,
  "columns": ["backlog", "ready", "in-progress", "QA", "done", "archive"],
  "next_id": 1,
  "tickets": []
}
```

This file is automatically created when you run `rkb build init` or any build command that requires the board to exist.

## Ticket Schema
Each ticket in the `tickets` array follows this schema:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | unsigned int | yes | Monotonically increasing from 1; never reused (see [Ticket ids](#ticket-ids)) |
| `title` | string | yes | Short description |
| `description` | string | no | Full details |
| `status` | string | yes | One of the 6 columns |
| `priority` | int (1-5) | no | Sort order (lower numbers = higher priority) |
| `assignee` | string | no | Agent name |
| `created` | string (ISO 8601) | yes | Creation timestamp |
| `updated` | string (ISO 8601) | yes | Last modified timestamp |
| `tags` | array of strings | no | Labels for grouping |
| `blocked_by` | array of ints | no | Ticket IDs this ticket depends on |

## Ticket ids

`next_id` increments monotonically and is **never** decremented by `rkb build rm`. Once a
number has been used for a ticket it is never handed out again, so removing tickets leaves
gaps in the sequence:

```bash
rkb build add --title a    # id 1
rkb build add --title b    # id 2
rkb build add --title c    # id 3
rkb build rm 2             # remaining ids: 1, 3
rkb build add --title d    # id 4, not 2
```

A gap is expected and is not a sign of lost tickets. Ids have to stay stable because
surviving tickets hold `blocked_by` references to them — reusing a number would silently
repoint every one of those dependencies at a different ticket.

Run `rkb plan lint` if you want the board's ids checked for problems; it also reports an
id that is at or past `next_id`, which is the genuinely inconsistent case.

## Behavior
- **Implicit init**: If `./.kanban-board` does not exist and a `build` command is issued, it is created automatically with the default column set
- **Ids are never reused**: `next_id` only ever increases, so gaps in the id sequence are normal — see [Ticket ids](#ticket-ids)
- **Numeric input is strict**: `priority` must be 1-5 and every `blocked_by` entry must be a positive integer. A bad value is rejected outright rather than skipped or partially applied
- **Tag and dependency lists have a 255-byte limit**: a longer `--tags` or `--blocked-by` value is rejected, not truncated, so a list is never cut short mid-tag. `description` is unlimited
- **Two tickets may not share an id**: such a board is refused by every `build` command, because each one would target an arbitrary one of them. Fix it with `rkb plan export`
- **Blocked-by**: When moving a ticket, if any blocking tickets are not `done`, a warning is printed to stderr but the move proceeds
- **Output**: JSON to stdout by default, `--pretty` flag for human-readable table
- **Errors/warnings**: Printed to stderr, with two exceptions that go to stdout: the `Warning:` line when a written value contains a literal `\n`, and `plan lint`'s findings
- **Exit code**: 0 on success, non-zero on error; `plan lint` exits 1 when it finds issues

## Code Style
- **Naming**: `snake_case`, no prefix
- **No comments**: Unless absolutely necessary
- **Suck-less philosophy**:
  - Minimal lines of code
  - Stack allocation, minimal `malloc`/`free`
  - Fixed-size buffers, linear scans
  - Single pass to understand the whole program
  - No frameworks, no bloat

See [Suckless Coding Style](https://suckless.org/coding_style/)

## Testing
- Shell scripts in `tests/` directory
- `make test` runs all test scripts
- `make memcheck` runs all tests under valgrind to detect leaks
- Tests cover all major functionality including edge cases

## Documentation
- `rkb.1` — Classic groff man page
- Installed to `/usr/local/share/man/man1/` via `make install`
- View with `man rkb` after installation

## Contributing
Please follow the existing code style

## License
Distributed under the MIT License. See `LICENSE` file for details.
