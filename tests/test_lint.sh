. ./helpers.sh

setup

# `plan lint` is the diagnostic for boards rkb did not write: the old `build edit`
# created shadow keys silently, and every read path looks up known keys by name, so
# nothing else can see them. Detection only - it must never touch the board.

COLS='["backlog","ready","in-progress","QA","done","archive"]'

# write_board <columns> <next_id> <tickets-array-contents>
write_board() {
  printf '{"version":%s,"columns":%s,"next_id":%s,"tickets":[%s]}' \
    "${4:-1}" "$1" "$2" "$3" > .kanban-board
}

# ticket <id> <title> <status> [extra-fields]
ticket() {
  printf '{"id":%s,"title":"%s","status":"%s","created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"%s}' \
    "$1" "$2" "$3" "$4"
}

assert_clean() {
  if [ "$exit_code" -ne 0 ]; then
    echo "FAIL: expected a clean board, got exit $exit_code"
    echo "stdout: $stdout"
    exit 1
  fi
  assert_stdout_contains "no issues found"
}

# asserts a finding mentioning <needle> was reported
assert_finding() {
  assert_exit_code 1 $exit_code
  if ! echo "$stdout" | grep -Fq -- "$1"; then
    echo "FAIL: expected a finding containing '$1'"
    echo "stdout: $stdout"
    exit 1
  fi
}

# --- a board rkb wrote itself is clean ----------------------------------------

run build init
assert_exit_code 0 $exit_code
run build add --title "one"
assert_exit_code 0 $exit_code
run build add --title "two" --blocked-by 1
assert_exit_code 0 $exit_code
run build move 1 QA
assert_exit_code 0 $exit_code
run build move 1 done
assert_exit_code 0 $exit_code
run plan lint
assert_clean

# --- the headline case: the five shadow keys from the bug report --------------

write_board "$COLS" 31 "$(ticket 8 real backlog ',"blocked_by":[1],"blocked-by":"8","blockedby":"8","blockees":"8","deps":"8","dependencies":"8"'),$(ticket 30 bad notacolumn)"
cp .kanban-board .kanban-board.orig
run plan lint
assert_exit_code 1 $exit_code
for key in blocked-by blockedby blockees deps dependencies; do
  assert_finding "unknown key '$key'"
done
assert_finding "not a board column"
assert_finding "references missing ticket 1"
# lint is read-only
if ! cmp -s .kanban-board .kanban-board.orig; then
  echo "FAIL: plan lint modified the board"
  exit 1
fi
rm -f .kanban-board.orig

# --- individual checks --------------------------------------------------------

write_board "$COLS" 3 "$(ticket 5 a backlog)"
run plan lint
assert_finding "id 5 is not below next_id 3"

write_board "$COLS" 20 "$(ticket 1 a backlog),$(ticket 1 b ready)"
run plan lint
assert_finding "duplicate ticket id 1"
# unlike a self-reference, a duplicated id misdirects every command that names it,
# so validation refuses the board as well as lint reporting it
run build overwrite 1 title "must not be allowed"
assert_exit_code 2 $exit_code
assert_stderr_contains "duplicate ticket id 1"

write_board "$COLS" 20 '{"id":1,"title":"a","title":"b","status":"backlog","created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"}'
run plan lint
assert_finding "duplicate key 'title'"

write_board "$COLS" 20 '{"id":1,"title":"a","created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"}'
run plan lint
assert_finding "missing field 'status'"

write_board "$COLS" 20 "$(ticket 1 a backlog ',"priority":"high"')"
run plan lint
assert_finding "field 'priority' has the wrong type"

write_board "$COLS" 20 "$(ticket 1 a backlog ',"tags":[1,2]')"
run plan lint
assert_finding "'tags' has an element that is not a string"

write_board "$COLS" 20 "$(ticket 1 a backlog ',"blocked_by":["x"]')"
run plan lint
assert_finding "'blocked_by' has an element that is not a number"

write_board "$COLS" 20 "$(ticket 1 a backlog ',"blocked_by":[1]')"
run plan lint
assert_exit_code 1 $exit_code
assert_finding "contains this ticket's own id"
# a self-reference never misdirects anything, so it must not lock the board out
run build overwrite 1 title "still editable"
assert_exit_code 0 $exit_code

write_board '["a","b"]' 20 ""
run plan lint
assert_finding "board declares 2 columns, this rkb expects 6"

# --- the board envelope gets the same scrutiny as a ticket (ticket 008) --------

printf '{"version":1,"columns":%s,"next_id":5,"next_id":"abc","tickets":[]}' "$COLS" \
  > .kanban-board
run plan lint
assert_finding "duplicate board key 'next_id'"

printf '{"version":1,"columns":%s,"next_id":2,"surprise":true,"tickets":[]}' "$COLS" \
  > .kanban-board
run plan lint
assert_finding "unknown board field 'surprise'"

# an unknown envelope key is reported but does not lock the board out, matching how
# unknown *ticket* keys are treated - a board may carry more than rkb requires
run build add --title "still works"
assert_exit_code 0 $exit_code

# --- a full findings array is reported as truncated (ticket 008) ---------------
# MAX_FINDINGS was hit silently: 400 problems reported as "issues":256

printf '{"version":1,"columns":%s,"next_id":900,"tickets":[' "$COLS" > .kanban-board
for t in $(seq 1 60); do
  printf '{"id":%s,"title":"t","status":"backlog","created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"' "$t" >> .kanban-board
  for k in $(seq 1 7); do printf ',"bogus%d":1' "$k" >> .kanban-board; done
  [ "$t" = 60 ] && printf '}]}' >> .kanban-board || printf '},' >> .kanban-board
done
run plan lint
assert_exit_code 1 $exit_code
assert_stdout_contains "stopped at 256 findings"
run plan lint --json
assert_exit_code 1 $exit_code
assert_stdout_contains '"truncated":1'

write_board "$COLS" 20 "$(ticket 1 a backlog)"
printf '%s' "$(cat .kanban-board | sed 's/"next_id":20,//')" > .kanban-board.tmp
mv .kanban-board.tmp .kanban-board
run plan lint
assert_finding "'next_id' is missing or not a number"

# a missing `next_id` must not be allowed to segfault lint or anything else
run build add --title "still works"
assert_exit_code 2 $exit_code

# --- --json -------------------------------------------------------------------

write_board "$COLS" 31 "$(ticket 8 real backlog ',"blocked-by":"8"')"
cp .kanban-board .kanban-board.orig
run plan lint --json
assert_exit_code 1 $exit_code
assert_stdout_contains '"ok":0'
assert_stdout_contains '"issues":1'
assert_stdout_contains "unknown key 'blocked-by'"
# --json must be a single line, so an agent can parse it with one read
if [ "$(printf '%s\n' "$stdout" | wc -l)" -ne 1 ]; then
  echo "FAIL: --json output is not a single line"
  echo "stdout: $stdout"
  exit 1
fi
if ! cmp -s .kanban-board .kanban-board.orig; then
  echo "FAIL: plan lint --json modified the board"
  exit 1
fi
rm -f .kanban-board.orig

rm -f .kanban-board
run build init
assert_exit_code 0 $exit_code
run plan lint --json
assert_exit_code 0 $exit_code
assert_stdout_contains '"ok":1'
assert_stdout_contains '"issues":0'

# --- error paths --------------------------------------------------------------

echo "not json at all" > .kanban-board
run plan lint
assert_exit_code 2 $exit_code
assert_stderr_contains "invalid board file"

rm -f .kanban-board
run plan lint
assert_exit_code 2 $exit_code
assert_stderr_contains "no board found"

# --- help ---------------------------------------------------------------------

run plan lint --help
assert_exit_code 0 $exit_code
assert_stderr_contains "plan lint"

teardown