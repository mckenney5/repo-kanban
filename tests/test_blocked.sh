. ./helpers.sh

setup

cat > .kanban-board << 'EOF'
{
  "version": 1,
  "columns": ["backlog", "ready", "in-progress", "QA", "done", "archive"],
  "next_id": 3,
  "tickets": [
    {
      "id": 1,
      "title": "blocked ticket",
      "status": "QA",
      "created": "2026-01-01T00:00:00Z",
      "updated": "2026-01-01T00:00:00Z",
      "blocked_by": [2]
    },
    {
      "id": 2,
      "title": "blocker",
      "status": "in-progress",
      "created": "2026-01-01T00:00:00Z",
      "updated": "2026-01-01T00:00:00Z"
    }
  ]
}
EOF

run build move 1 done
assert_exit_code 0 $exit_code
assert_stderr_contains "Warning"
assert_board_contains '"status":.*"done"'

cat > .kanban-board << 'EOF'
{
  "version": 1,
  "columns": ["backlog", "ready", "in-progress", "QA", "done", "archive"],
  "next_id": 3,
  "tickets": [
    {
      "id": 1,
      "title": "blocked ticket",
      "status": "QA",
      "created": "2026-01-01T00:00:00Z",
      "updated": "2026-01-01T00:00:00Z",
      "blocked_by": [2]
    },
    {
      "id": 2,
      "title": "blocker",
      "status": "done",
      "created": "2026-01-01T00:00:00Z",
      "updated": "2026-01-01T00:00:00Z"
    }
  ]
}
EOF

run build move 1 done
assert_exit_code 0 $exit_code
assert_stderr_not_contains "Warning"

teardown
