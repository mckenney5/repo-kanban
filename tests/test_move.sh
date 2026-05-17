. ./helpers.sh

setup

run build init
assert_exit_code 0 $exit_code

run build add --title "first"
assert_exit_code 0 $exit_code

run build move 1 ready
assert_exit_code 0 $exit_code
assert_board_contains '"status":.*"ready"'

run build move 1 QA
assert_exit_code 0 $exit_code

run build move 1 done
assert_exit_code 0 $exit_code
assert_board_contains '"status":.*"done"'

# invalid status fails
run build move 1 invalid_status
assert_exit_code 1 $exit_code

# missing id fails
run build move
assert_exit_code 1 $exit_code

teardown
