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
- **Use `--tags` for cross-cutting concerns.** e.g. `--tags bug,parser`
  or `--tags refactor,api`. Filter with `rkb plan list | jq
  '.tickets | map(select(.tags | index("bug")))'`.
- **Assign yourself with `--assignee` so other agents know who owns
  what.** Omit `--assignee` to leave a ticket in the pool.
- **Blocked-by is advisory.** `rkb build move` warns if a blocking
  ticket isn't done but lets the move succeed — use judgment.
- **Re-read the board before adding.** `rkb plan list` emits clean
  JSON — pipe it to `jq` or parse directly.

## Error codes

| Code | Meaning | Common cause |
|------|---------|-------------|
| 0 | Success | |
| 1 | Usage / bad args | Missing `--title`, invalid status, bad flags |
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
