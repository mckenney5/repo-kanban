. ./helpers.sh

setup

run build init
assert_exit_code 0 $exit_code

run build add --title "test ticket"
assert_exit_code 0 $exit_code
assert_board_contains '"title":.*"test ticket"'
assert_board_contains '"status":.*"backlog"'
assert_board_contains '"next_id":.*2'

run build add --title "second"
assert_exit_code 0 $exit_code
assert_board_contains '"next_id":.*3'

run build add
assert_exit_code 1 $exit_code

teardown
