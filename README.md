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
| `rkb build edit <id> <field> <value>` | Modify a ticket field |
| `rkb build rm <id>` | Delete a ticket |
| `rkb build archive-done` | Move all `done` tickets to `archive` |

### Plan Commands (Read-Only)
These commands display board information without modification:

| Command | Description |
|---------|-------------|
| `rkb plan list [--status <col>] [--pretty]` | List tickets, optionally filtered by column |
| `rkb plan show <id> [--pretty]` | Show full ticket details |
| `rkb plan stats [--pretty]` | Column counts and summary |
| `rkb plan export` | Dump the entire board as JSON |

**Note**: Add `--pretty` to any plan command for human-readable table output instead of JSON.

## Board File Format
The kanban board is stored as a single JSON file at `./.kanban-board`:

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
| `id` | unsigned int | yes | Sequential, starts at 1 |
| `title` | string | yes | Short description |
| `description` | string | no | Full details |
| `status` | string | yes | One of the 6 columns |
| `priority` | int (1-5) | no | Sort order (lower numbers = higher priority) |
| `assignee` | string | no | Agent name |
| `created` | string (ISO 8601) | yes | Creation timestamp |
| `updated` | string (ISO 8601) | yes | Last modified timestamp |
| `tags` | array of strings | no | Labels for grouping |
| `blocked_by` | array of ints | no | Ticket IDs this ticket depends on |

## Behavior
- **Implicit init**: If `./.kanban-board` does not exist and a `build` command is issued, it is created automatically with empty columns
- **Blocked-by**: When moving a ticket, if any blocking tickets are not `done`, a warning is printed to stderr but the move proceeds
- **Output**: JSON to stdout by default, `--pretty` flag for human-readable table
- **Errors/warnings**: Printed to stderr
- **Exit code**: 0 on success, non-zero on error

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
