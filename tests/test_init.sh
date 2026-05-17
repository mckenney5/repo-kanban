. ./helpers.sh

setup

# init creates .kanban-board
run build init
assert_exit_code 0 $exit_code
assert_board_exists
assert_board_contains '"version"'
assert_board_contains '"columns"'
assert_board_contains '"backlog"'
assert_board_contains '"in-progress"'

# re-init is a no-op (doesn't crash)
run build init
assert_exit_code 0 $exit_code

teardown
