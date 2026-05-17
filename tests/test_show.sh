. ./helpers.sh

setup

run build init
assert_exit_code 0 $exit_code

run build add --title "show me"
assert_exit_code 0 $exit_code

run plan show 1
assert_exit_code 0 $exit_code
assert_stdout_contains '"id":.*1'
assert_stdout_contains '"title":.*"show me"'
assert_stdout_contains '"status":.*"backlog"'

run plan show 99
assert_exit_code 3 $exit_code

teardown
