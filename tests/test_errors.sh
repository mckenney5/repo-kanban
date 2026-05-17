. ./helpers.sh

setup

# no board → plan list returns empty
run plan list
assert_exit_code 0 $exit_code
assert_stdout_contains '"tickets":'

# no board → plan export fails
run plan export
assert_exit_code 2 $exit_code

# no board → plan show fails
run plan show 1
assert_exit_code 2 $exit_code

# invalid JSON board
echo "not json" > .kanban-board
run plan list
assert_exit_code 2 $exit_code

# invalid command
run rkb build invalid
assert_exit_code 1 $exit_code

# plan with invalid subcommand
run rkb plan invalid
assert_exit_code 1 $exit_code

teardown
