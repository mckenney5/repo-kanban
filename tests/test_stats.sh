. ./helpers.sh

setup

cat > .kanban-board << 'EOF'
{
  "version": 1,
  "columns": ["backlog", "ready", "in-progress", "QA", "done", "archive"],
  "next_id": 5,
  "tickets": [
    { "id": 1, "title": "a", "status": "backlog", "created": "X", "updated": "X" },
    { "id": 2, "title": "b", "status": "in-progress", "created": "X", "updated": "X", "blocked_by": [4] },
    { "id": 3, "title": "c", "status": "done", "created": "X", "updated": "X", "assignee": "bot" },
    { "id": 4, "title": "d", "status": "backlog", "created": "X", "updated": "X" }
  ]
}
EOF

run plan stats
assert_exit_code 0 $exit_code
assert_stdout_contains '"total":.*4'
assert_stdout_contains '"backlog":.*2'
assert_stdout_contains '"in-progress":.*1'
assert_stdout_contains '"done":.*1'
assert_stdout_contains '"blocked":.*1'
assert_stdout_contains '"unassigned":.*3'

teardown
