. ./helpers.sh

setup

run build init
assert_exit_code 0 $exit_code

run build add --title "hello"
assert_exit_code 0 $exit_code

# --pretty output is a table, not JSON
run plan list --pretty
assert_exit_code 0 $exit_code
assert_stdout_contains "ID"
assert_stdout_contains "Title"
assert_stdout_contains "hello"
assert_stdout_not_contains '"tickets"'

# without --pretty, output is JSON
run plan list
assert_exit_code 0 $exit_code
assert_stdout_contains '"tickets"'

teardown
