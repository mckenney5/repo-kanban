. ./helpers.sh

setup

run build init
assert_exit_code 0 $exit_code

run plan list
assert_exit_code 0 $exit_code
assert_stdout_contains '"tickets"'
assert_stdout_contains '"next_id"'

run build add --title "first"
assert_exit_code 0 $exit_code

run plan list
assert_exit_code 0 $exit_code
assert_stdout_contains '"title":.*"first"'
assert_stdout_contains '"id":.*1'

teardown
