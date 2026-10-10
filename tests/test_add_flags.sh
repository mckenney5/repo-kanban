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

# --- strict numeric input (ticket 001/003 loose end) --------------------------
# A bad token used to be dropped silently, so "1,junk" wrote [1] and said ok.

run build add --title "partial" --blocked-by 1,junk
assert_exit_code 1 $exit_code
assert_stderr_contains "'junk' is not a valid number"
assert_board_not_contains '"partial"'

# priority is documented as 1-5; 0, 9 and junk were all accepted
for p in 0 9 abc; do
  run build add --title "bad prio" --priority "$p"
  assert_exit_code 1 $exit_code
  assert_stderr_contains "priority must be 1-5"
  assert_board_not_contains '"bad prio"'
done

# strtoul accepts a leading '-' and returns the negation, so -1 used to be stored.
# An overflowing value truncates to -1 on the way into an int.
run build add --title "negative" --priority -1
assert_exit_code 1 $exit_code
run build add --title "overflow" --priority 99999999999999999999
assert_exit_code 1 $exit_code

run build add --title "neg dep" --blocked-by -1
assert_exit_code 1 $exit_code
assert_stderr_contains "'-1' is not a valid number"

# overflow truncates to -1 as an int; priority's 1-5 check catches it, blocked_by has
# no range check so parse_uint's own cap is what rejects it
run build add --title "ovf dep" --blocked-by 99999999999999999999
assert_exit_code 1 $exit_code
assert_board_not_contains '"ovf dep"'

# the boundaries themselves are accepted
run build add --title "lowest" --priority 1
assert_exit_code 0 $exit_code
run build add --title "highest" --priority 5
assert_exit_code 0 $exit_code

teardown
