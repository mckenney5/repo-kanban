# rkb — Agent Skill

Two modes. **Never confuse them.**

```
rkb build <cmd>   # MUTATES the board file — use when changing state
rkb plan  <cmd>   # READ-ONLY, emits JSON to stdout — use to inspect
```

`plan` commands are safe to call any time (no side effects).
`build` commands change `.kanban-board` — call deliberately.

## Workflow

```
1.  rkb build add --title "Goal"
    # creates ticket 1 in "backlog"

2.  rkb build edit 1 description "..."
    # capture full context

3.  rkb build add --title "Sub-task A" --blocked-by 1
    rkb build add --title "Sub-task B" --blocked-by 1
    # decompose into sequenced work

4.  rkb plan list
    # read the board as JSON — see what's ready

5.  rkb build move 2 ready
    # pull a sub-task into ready

6.  ... work on ticket 2 ...
    rkb build move 2 done
    # mark complete

7.  rkb build archive-done
    # sweep done tickets to archive
```

## Guidelines

- **One ticket, one concern.** If a ticket needs sub-steps, create
  dependent tickets with `--blocked-by` rather than writing paragraphs.
- **Use `--priority` to signal order.** 1 = urgent, 5 = nice-to-have.
  Sort with `rkb plan list | jq '.tickets | sort_by(.priority)'`.
  The range is **enforced**: 0, 6, `-1` or `high` are rejected, and the
  ticket is not created. Pass 1-5 or omit the flag.
- **Use `--tags` for cross-cutting concerns.** e.g. `--tags bug,parser`
  or `--tags refactor,api`. Filter with `rkb plan list | jq
  '.tickets | map(select(.tags | index("bug")))'`.
  **Tags and dependencies have a 255-byte limit** — roughly 20 tags of
  `tag001,tag002,…`. A longer `--tags` or `--blocked-by` value is
  **rejected**, not truncated: the command errors and nothing is
  written, so a value can never be cut short mid-tag. `description`
  has no such limit.
- **Assign yourself with `--assignee` so other agents know who owns
  what.** Omit `--assignee` to leave a ticket in the pool.
- **Blocked-by is advisory.** `rkb build move` warns if a blocking
  ticket isn't done but lets the move succeed — use judgment.
- **Ids in `--blocked-by` must be real numbers.** `2,3` is fine;
  `2,junk`, `-1` or an empty slot is rejected outright and nothing is
  written, rather than partially applied as it used to be. A blocker
  that does not exist yet is still accepted — dependencies may point
  forward. Clear them with `build overwrite <id> blocked_by ""`.
- **`build edit` appends; `build overwrite` replaces.** Since 2.0.0,
  `edit` never discards what is already there. It appends to
  `description` (new line), `tags` and `blocked_by` (adds only missing
  elements). `title`, `assignee` and `priority` hold one value each, so
  `edit` refuses them and names `overwrite`:
  ```bash
  rkb build edit 1 description "second finding"   # keeps the first
  rkb build overwrite 1 title "corrected title"  # replaces it
  rkb build overwrite 1 blocked_by ""             # clears it
  ```
  Appends that add nothing print `ok (no change)` and still exit 0.
  To clear a field use `overwrite <field> ""` — `edit` with an empty
  value adds nothing rather than clearing.
- **Do not run `rkb build` concurrently against one board.** `rkb`
  takes no locks. Saves are atomic so the file is never half-written,
  but two processes reading the same board and writing at once will
  lose whichever ticket is written second, and both will print `ok`.
  **One `rkb` command at a time per repository.** If you need
  parallelism, wait for each command to finish first.
- **Gaps in ticket ids are normal.** `next_id` only ever increases and
  is never reused, even after `build rm`, because surviving tickets
  hold `blocked_by` references to those ids. Missing numbers mean a
  ticket was deleted, not that anything is wrong — do not "fix" them.
  `rkb plan lint` will tell you if an id is genuinely inconsistent.
  Two tickets sharing one id is **not** recoverable with `build`
  commands — the board is refused, because every command naming that id
  would target an arbitrary one of the two. `rkb plan export` and fix
  the JSON by hand.
- **Re-read the board before adding.** `rkb plan list` emits clean
  JSON — pipe it to `jq` or parse directly.
- **Literal `\n` in a value is stored as typed.** `rkb` never rewrites
  a value, so a description containing `\n` stays two characters. When
  it sees one it adds a `Warning:` line after `ok` on stdout — that
  second line is expected, and exit status is still 0. For a real
  newline, pass one:
  ```bash
  rkb build edit 1 description "$(printf 'line1\nline2')"
  ```
  Code snippets containing `\n` are stored correctly and warned about
  harmlessly; the value is never touched.
- **Run `rkb plan lint` if the board looks wrong.** It is read-only and
  finds damage nothing else reports: mistyped field names such as
  `blocked-by` next to a real `blocked_by`, duplicate ticket ids, or
  dependencies on tickets that no longer exist. Exit 0 means clean, exit 1
  means issues found. If you edited the board with `jq`, `sed`, or a
  heredoc, run it — that is the way those keys get in.
  Use `rkb plan export` to get the raw JSON for repair; never guess which
  of two contradictory values is the right one.

## Error codes

| Code | Meaning | Common cause |
|------|---------|-------------|
| 0 | Success | |
| 1 | Usage / bad args, **or `plan lint` found issues** | Missing `--title`, invalid status, bad flags |
| 2 | Board error | No board, corrupt JSON, I/O failure |
| 3 | Not found | Ticket id doesn't exist |

## Typical agent loop

```bash
# 1. Orient — read board as JSON
rkb plan list

# 2. Pick a ready ticket by id
rkb build move 3 in-progress

# 3. Work the ticket ...
# 4. Move to QA for review
rkb build move 3 QA
```
