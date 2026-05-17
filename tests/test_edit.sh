. ./helpers.sh

setup

run build init
assert_exit_code 0 $exit_code

run build add --title "original title"
assert_exit_code 0 $exit_code

# edit title
run build edit 1 title "new title"
assert_exit_code 0 $exit_code
assert_board_contains '"title":.*"new title"'

# edit assignee
run build edit 1 assignee "agent-1"
assert_exit_code 0 $exit_code
assert_board_contains '"assignee":.*"agent-1"'

# edit nonexistent ticket
run build edit 99 title "nope"
assert_exit_code 3 $exit_code

# missing args
run build edit
assert_exit_code 1 $exit_code

teardown
