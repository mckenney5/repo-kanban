. ./helpers.sh

setup

run build init
assert_exit_code 0 $exit_code

run build add --title "a"
assert_exit_code 0 $exit_code

run build add --title "b"
assert_exit_code 0 $exit_code

run build move 1 QA
assert_exit_code 0 $exit_code

run build move 1 done
assert_exit_code 0 $exit_code

run build archive-done
assert_exit_code 0 $exit_code
assert_board_contains '"status":.*"archive"'
assert_board_not_contains '"status":.*"done"'

run build archive-done
assert_exit_code 0 $exit_code

teardown
