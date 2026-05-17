. ./helpers.sh

setup

run build init
assert_exit_code 0 $exit_code

run build add \
  --title "full ticket" \
  --description "desc here" \
  --priority 2 \
  --assignee "agent-42" \
  --tags "bug,urgent" \
  --blocked-by "2"
assert_exit_code 0 $exit_code

assert_board_contains '"title":.*"full ticket"'
assert_board_contains '"description":.*"desc here"'
assert_board_contains '"priority":.*2'
assert_board_contains '"assignee":.*"agent-42"'
assert_board_contains '"tags"'
assert_board_contains '"bug"'
assert_board_contains '"urgent"'
assert_board_contains '"blocked_by"'

# add another ticket so blocked-by validates (no error for ticket 2 not existing yet)
run build add --title "blocker"
assert_exit_code 0 $exit_code

teardown
