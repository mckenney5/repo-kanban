. ./helpers.sh

setup

run build init
assert_exit_code 0 $exit_code

run build add --title "to remove"
assert_exit_code 0 $exit_code

run build rm 1
assert_exit_code 0 $exit_code
assert_board_not_contains '"id":.*1'

# removing again fails
run build rm 1
assert_exit_code 3 $exit_code

# no id fails
run build rm
assert_exit_code 1 $exit_code

teardown
