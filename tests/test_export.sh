. ./helpers.sh

setup

run build init
assert_exit_code 0 $exit_code

run build add --title "export test"
assert_exit_code 0 $exit_code

run plan export
assert_exit_code 0 $exit_code
assert_stdout_contains '"version"'
assert_stdout_contains '"columns"'
assert_stdout_contains '"tickets"'
assert_stdout_contains '"export test"'

teardown
