. ./helpers.sh

setup

run build init
assert_exit_code 0 $exit_code

run build add --title "original title"
assert_exit_code 0 $exit_code

# edit appends, so replacing a single-valued field is `overwrite`
run build overwrite 1 title "new title"
assert_exit_code 0 $exit_code
assert_board_contains '"title":.*"new title"'

# edit assignee
run build overwrite 1 assignee "agent-1"
assert_exit_code 0 $exit_code
assert_board_contains '"assignee":.*"agent-1"'

# edit nonexistent ticket
run build edit 99 title "nope"
assert_exit_code 3 $exit_code

# missing args
run build edit
assert_exit_code 1 $exit_code

# tool-managed fields cannot be edited (rkb: ticket 002)
run build edit 1 id 42
assert_exit_code 1 $exit_code
assert_stderr_contains "cannot be edited"
assert_board_contains '"id":.*1'

run build edit 1 created "1999-01-01T00:00:00Z"
assert_exit_code 1 $exit_code
assert_board_not_contains '1999-01-01'

# status is owned by build move, which applies the QA gate (ticket 002)
run build edit 1 status done
assert_exit_code 1 $exit_code
assert_stderr_contains "build move"
assert_board_contains '"status":.*"backlog"'

run build edit 1 status notacolumn
assert_exit_code 1 $exit_code
assert_board_contains '"status":.*"backlog"'

# unknown fields are rejected, never created (ticket 001)
run build edit 1 blocked-by 3
assert_exit_code 1 $exit_code
assert_stderr_contains "unknown field"
assert_board_not_contains '"blocked-by"'

run build edit 1 dependencies 3
assert_exit_code 1 $exit_code
assert_board_not_contains '"dependencies"'

# absent optional fields are created with their schema type
run build overwrite 1 priority 2
assert_exit_code 0 $exit_code
assert_board_contains '"priority":.*2'

run build edit 1 tags "bug,cli"
assert_exit_code 0 $exit_code
assert_board_contains '"tags":.*\["bug",\s*"cli"\]'

run build edit 1 blocked_by 1,2
assert_exit_code 0 $exit_code
assert_board_contains '"blocked_by":.*\[1,\s*2\]'

# numeric fields still reject non-numeric values
run build overwrite 1 priority abc
assert_exit_code 1 $exit_code

# edit appends to an existing array (union-add); overwrite replaces it
run build edit 1 tags "x,y,z"
assert_exit_code 0 $exit_code
assert_board_contains '"tags":.*\["bug",\s*"cli",\s*"x",\s*"y",\s*"z"\]'

run build overwrite 1 tags "x,y,z"
assert_exit_code 0 $exit_code
assert_board_contains '"tags":.*\["x",\s*"y",\s*"z"\]'

# clearing is overwrite's job: edit appends, and "" adds nothing
run build overwrite 1 blocked_by ""
assert_exit_code 0 $exit_code
assert_board_contains '"blocked_by":\s*\[\]'

# --- append vs overwrite (ticket 005) ----------------------------------------

# appending to prose keeps what was already there
run build overwrite 1 description "line one"
assert_exit_code 0 $exit_code
run build edit 1 description "line two"
assert_exit_code 0 $exit_code
run build edit 1 description "line three"
assert_exit_code 0 $exit_code
assert_board_contains 'line one'
assert_board_contains 'line two'
assert_board_contains 'line three'

# appending to an absent field must not leave a leading newline
run build add --title "fresh"
assert_exit_code 0 $exit_code
run build edit 2 description "only line"
assert_exit_code 0 $exit_code
assert_board_not_contains 'only line\\n'

# overwrite replaces rather than accumulating
run build overwrite 2 description "replaced"
assert_exit_code 0 $exit_code
assert_board_contains 'replaced'
assert_board_not_contains 'only line'

# appending a tag that is already there changes nothing, and says so
run build edit 2 tags "alpha"
assert_exit_code 0 $exit_code
assert_stdout_not_contains "no change"
run build edit 2 tags "alpha"
assert_exit_code 0 $exit_code
assert_stdout_contains "no change"

# a long description must survive an append intact - a fixed buffer would truncate
LONG=$(printf 'x%.0s' $(seq 1 900))
run build add --title "long" --description "$LONG"
assert_exit_code 0 $exit_code
run build edit 3 description "appended tail"
assert_exit_code 0 $exit_code
run plan show 3
assert_stdout_contains "$LONG"
assert_stdout_contains "appended tail"

# single-valued fields reject append and name the replacement command
for f in title assignee priority; do
  run build edit 1 $f "nope"
  assert_exit_code 1 $exit_code
  assert_stderr_contains "build overwrite"
  assert_stderr_contains "holds a single value"
done
assert_board_contains '"title":.*"new title"'

# ...and overwrite takes them
run build overwrite 1 priority 3
assert_exit_code 0 $exit_code
assert_board_contains '"priority":.*3'

# protected fields stay rejected under both verbs
run build overwrite 1 id 7
assert_exit_code 1 $exit_code
run build overwrite 1 status done
assert_exit_code 1 $exit_code
assert_stderr_contains "build move"
assert_board_contains '"status":.*"backlog"'

# --- literal \n in an edited value (ticket 004) -------------------------------
# An agent piping a jq'd string hands over a backslash and an 'n' rather than a real
# newline. rkb stores it faithfully, so it warns instead of rewriting.

run build overwrite 1 description "clean"
assert_exit_code 0 $exit_code

run build edit 1 description 'line1\nline2'
assert_exit_code 0 $exit_code
assert_stdout_contains "contains 1 literal"
assert_stdout_contains "'description'"
# ok stays the first stdout line, so existing output parsers still work
assert_stdout_contains '^ok$'

# stored verbatim: cJSON escapes the backslash, so the file holds TWO of them.
# One backslash would mean the value had been decoded, so its absence is the guard.
assert_board_contains 'line1\\\\nline2'
assert_board_not_contains 'line1\\nline2'

# several in one value are counted
run build edit 1 description 'a\nb\nc'
assert_exit_code 0 $exit_code
assert_stdout_contains "contains 2 literal"

# overwrite warns the same way
run build overwrite 1 description 'x\ny'
assert_exit_code 0 $exit_code
assert_stdout_contains "contains 1 literal"

# and so does build add
run build add --title "escaped" --description 'p\nq'
assert_exit_code 0 $exit_code
assert_stdout_contains "contains 1 literal"

# --- no false positives -------------------------------------------------------

assert_no_warning() {
  if echo "$stdout" | grep -q "Warning:"; then
    echo "FAIL: unexpected warning for '$1'"
    echo "stdout: $stdout"
    exit 1
  fi
}

run build overwrite 1 description "plain text with no escapes"
assert_exit_code 0 $exit_code
assert_no_warning "plain text"

run build overwrite 1 description "bare n and letters"
assert_exit_code 0 $exit_code
assert_no_warning "bare n"

run build overwrite 1 description 'trailing backslash\'
assert_exit_code 0 $exit_code
assert_no_warning "trailing backslash"

# a doubled backslash is an escaped backslash, not the start of an escape
run build overwrite 1 description 'already\\nescaped'
assert_exit_code 0 $exit_code
assert_no_warning "escaped backslash then n"

# build add without --description must stay quiet
run build add --title "no description"
assert_exit_code 0 $exit_code
assert_no_warning "add without --description"

# --- strict numeric input on the edit/overwrite paths ------------------------

# the id the next `build add` will use, so this block does not depend on how many
# tickets earlier blocks created
next_id() {
  echo "$($RKB plan export | tr -d ' \t\n' | grep -o '"next_id":[0-9]*' | grep -o '[0-9]*')"
}

T=$(next_id)
run build add --title "fresh target"
assert_exit_code 0 $exit_code
cp .kanban-board .kanban-board.orig

# a bad token is rejected outright rather than partially applied
run build overwrite "$T" blocked_by 1,junk
assert_exit_code 1 $exit_code
assert_stderr_contains "'junk' is not a valid number"
# the array is parsed before it is cleared, so a rejected value leaves it intact
run plan show "$T"
assert_stdout_not_contains '"blocked_by"'

run build edit "$T" blocked_by junk
assert_exit_code 1 $exit_code
assert_stderr_contains "'junk' is not a valid number"

run build overwrite "$T" blocked_by "2,,3"
assert_exit_code 0 $exit_code
run plan show "$T"
assert_stdout_contains '"blocked_by"'

# -1 reaches blocked_by via the -- escape hatch as well as --blocked-by
run build edit -- "$T" blocked_by -5
assert_exit_code 1 $exit_code
assert_stderr_contains "'-5' is not a valid number"

# an overflowing token truncates to -1 on the way into an int. priority is saved by its
# 1-5 range check; blocked_by has no range check, so parse_uint's own cap is the only
# thing rejecting this.
run build overwrite "$T" blocked_by 99999999999999999999
assert_exit_code 1 $exit_code
assert_stderr_contains "is not a valid number"
# the previous value is still intact - the rejected token did not land
run plan show "$T"
assert_stdout_contains '"blocked_by":\s*\[2,\s*3\]'

# empty still means clear, which is how 005's documented contract works
run build overwrite "$T" blocked_by ""
assert_exit_code 0 $exit_code
run plan show "$T"
assert_stdout_contains '"blocked_by":\s*\[\]'

# priority bounds. Only `overwrite` reaches these: `edit` refuses priority outright
# as a single-value field, which is already covered above.
for p in 0 9 junk; do
  run build overwrite "$T" priority "$p"
  assert_exit_code 1 $exit_code
  assert_stderr_contains "priority must be 1-5"
done

# and past the positional flag check, where the value itself must be rejected
run build overwrite -- "$T" priority -1
assert_exit_code 1 $exit_code
assert_stderr_contains "priority must be 1-5"

run build overwrite "$T" priority 4
assert_exit_code 0 $exit_code
run plan show "$T"
assert_stdout_contains '"priority":\s*4'
rm -f .kanban-board.orig

# an out-of-range priority already in a board is reported but does NOT lock the
# board out: writes enforce the rule, reads only report it.
write_board_with_priority() {
  printf '{"version":1,"columns":["backlog","ready","in-progress","QA","done","archive"],"next_id":5,"tickets":[{"id":1,"title":"legacy","status":"backlog","created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z","priority":%s}]}' "$1" \
    > .kanban-board
}
write_board_with_priority 9
run plan lint
assert_exit_code 1 $exit_code
assert_stdout_contains "outside the documented 1-5"
run build overwrite 1 title "still editable"
assert_exit_code 0 $exit_code
run build overwrite 1 priority 2
assert_exit_code 0 $exit_code
assert_board_contains '"priority":.*2'

# --- id range (QA finding on ticket 011) -------------------------------------
# next_id is type-checked but was not range-checked: cJSON saturates valueint, so
# 99999999999999 arrived as INT_MAX, id + 1 overflowed, and cJSON_SetIntValue wrote the
# result back to both fields - leaving a negative next_id that also passed a type check.
# One number permanently corrupted the board and no build command could repair it.

write_board_with_next_id() {
  printf '{"version":1,"columns":["backlog","ready","in-progress","QA","done","archive"],"next_id":%s,"tickets":[{"id":1,"title":"a","status":"backlog","created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"}]}' "$1" \
    > .kanban-board
}

for bad in 99999999999999 0 -5 1.5; do
  write_board_with_next_id "$bad"
  cp .kanban-board .kanban-board.orig
  run build add --title "must not be created"
  assert_exit_code 2 $exit_code
  assert_stderr_contains "next_id"
  assert_board_not_contains "must not be created"
  if ! cmp -s .kanban-board .kanban-board.orig; then
    echo "FAIL: board was rewritten despite the refusal (next_id=$bad)"
    exit 1
  fi
  # lint must agree with validation rather than calling it clean
  run plan lint
  assert_exit_code 1 $exit_code
  rm -f .kanban-board.orig
done

# a ticket id that is a number but not usable is refused too
printf '{"version":1,"columns":["backlog","ready","in-progress","QA","done","archive"],"next_id":2,"tickets":[{"id":-1,"title":"bad","status":"backlog","created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"}]}' \
  > .kanban-board
run build add --title "nope"
assert_exit_code 2 $exit_code
assert_stderr_contains "id is out of range"
run plan lint
assert_exit_code 1 $exit_code

# at INT_MAX the id cannot be incremented, so add refuses rather than wrapping
write_board_with_next_id 2147483647
cp .kanban-board .kanban-board.orig
run build add --title "must not wrap"
assert_exit_code 2 $exit_code
assert_stderr_contains "refusing to wrap"
if ! cmp -s .kanban-board .kanban-board.orig; then
  echo "FAIL: next_id was incremented past INT_MAX"
  exit 1
fi

# regression: a normal board still works
rm -f .kanban-board
run build init
assert_exit_code 0 $exit_code
run build add --title "fresh"
assert_exit_code 0 $exit_code
run plan show 1
assert_stdout_contains '"id":\s*1'

teardown
