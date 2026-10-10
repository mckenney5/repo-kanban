. ./helpers.sh

setup

# rkb must never crash on a board it did not write. Before the schema validator these
# commands dereferenced a NULL status and died with SIGSEGV (exit 139).
COLS='["backlog","ready","in-progress","QA","done","archive"]'

# write_board takes the complete contents of the "tickets" array: a comma-separated
# list of whole ticket objects, braces included.
write_board() {
  printf '{"version":1,"columns":%s,"next_id":99,"tickets":[%s]}' "$COLS" "$1" \
    > .kanban-board
}

# valid_ticket emits a schema-valid ticket. $4 is an optional tail such as
# ',"priority":3', inserted before the closing brace.
valid_ticket() {
  printf '{"id":%s,"title":"%s","status":"%s","created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"%s}' \
    "$1" "$2" "$3" "$4"
}

# Exit 139 is SIGSEGV. Checked explicitly so the failure mode is named rather than
# just "some nonzero code".
assert_no_crash() {
  if [ "$exit_code" -ge 128 ]; then
    echo "FAIL: 'rkb $1' died from a signal (exit $exit_code)"
    exit 1
  fi
}

assert_unchanged() {
  if ! cmp -s .kanban-board .kanban-board.orig; then
    echo "FAIL: board was modified by a rejected operation"
    diff .kanban-board.orig .kanban-board
    exit 1
  fi
}

# --- ticket missing a required field -----------------------------------------

write_board '{"id":1,"title":"no-status","created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"}'
cp .kanban-board .kanban-board.orig

for cmd in "plan stats" "build archive-done" "build move 1 ready" \
           "build rm 1" "plan list" "plan show 1" "build edit 1 title y"; do
  run $cmd
  assert_no_crash "$cmd"
  assert_exit_code 2 $exit_code
  assert_stderr_contains "missing required field 'status'"
  assert_unchanged
done
rm -f .kanban-board.orig

# --- missing 'updated' (the field a silent no-op write would skip) ------------

write_board '{"id":1,"title":"no-updated","status":"backlog","created":"2026-01-01T00:00:00Z"}'
run plan stats
assert_no_crash "plan stats"
assert_exit_code 2 $exit_code
assert_stderr_contains "missing required field 'updated'"

# --- status is not a board column --------------------------------------------

write_board "$(valid_ticket 1 notacolumn)"
run plan stats
assert_no_crash "plan stats"
assert_exit_code 2 $exit_code
assert_stderr_contains "is not a board column"

run build move 1 ready
assert_no_crash "build move"
assert_exit_code 2 $exit_code

# --- optional fields with the wrong type --------------------------------------

write_board "$(valid_ticket 1 ok backlog ',"priority":"high"')"
run plan list
assert_no_crash "plan list"
assert_exit_code 2 $exit_code
assert_stderr_contains "field 'priority' has the wrong type"

write_board "$(valid_ticket 1 ok backlog ',"blocked_by":"1"')"
run plan show 1
assert_no_crash "plan show"
assert_exit_code 2 $exit_code
assert_stderr_contains "field 'blocked_by' has the wrong type"

# status present but not a string. Written out rather than built from valid_ticket,
# which would emit a second "status" key - cJSON resolves duplicates first-wins, so
# the wrong-typed one would never be looked at.
write_board '{"id":1,"title":"ok","status":42,"created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"}'
run plan stats
assert_no_crash "plan stats"
assert_exit_code 2 $exit_code
assert_stderr_contains "field 'status' has the wrong type"

# --- a ticket blocked by a corrupt ticket ------------------------------------
# the offender must be named, not the innocent ticket that references it

write_board "$(valid_ticket 1 victim backlog ',"blocked_by":[42]'),{\"id\":42,\"title\":\"broken\",\"created\":\"2026-01-01T00:00:00Z\",\"updated\":\"2026-01-01T00:00:00Z\"}"
run plan stats
assert_no_crash "plan stats"
assert_exit_code 2 $exit_code
assert_stderr_contains "ticket 42"

# --- duplicated ticket id -----------------------------------------------------
# find_ticket returns whichever match it reaches first, so every command naming the
# id would target an arbitrary one of the two.

write_board "$(valid_ticket 1 a backlog),$(valid_ticket 1 b ready)"
cp .kanban-board .kanban-board.orig
for cmd in "build overwrite 1 title x" "build edit 1 title x" "build move 1 ready" \
           "plan stats" "plan list" "plan show 1"; do
  run $cmd
  assert_no_crash "$cmd"
  assert_exit_code 2 $exit_code
  assert_stderr_contains "duplicate ticket id 1"
  assert_unchanged
done
rm -f .kanban-board.orig

# --- root-level duplicate keys (ticket 010, QA warning) ------------------------
# cJSON keeps the first of a repeated key and validation only ever looked at the
# first, so `"next_id":5,"next_id":"abc"` was accepted, written to, and clean.

printf '{"version":1,"columns":["backlog","ready","in-progress","QA","done","archive"],"next_id":5,"next_id":"abc","tickets":[]}' \
  > .kanban-board
cp .kanban-board .kanban-board.orig
run build add --title "must not be created"
assert_no_crash "build add"
assert_exit_code 2 $exit_code
assert_stderr_contains "board field 'next_id' is duplicated"
assert_board_not_contains "must not be created"
assert_unchanged
run plan lint
assert_exit_code 1 $exit_code
assert_stdout_contains "duplicate board key 'next_id'"

# --- over-long CSV input is refused, not truncated (ticket 003, QA warning) ----
# a 419-byte --tags used to store its first 255 bytes with the last tag cut to "tag"

printf '{"version":1,"columns":["backlog","ready","in-progress","QA","done","archive"],"next_id":5,"tickets":[]}' \
  > .kanban-board
long_tags=$(printf 'tag%03d,' $(seq 1 60) | sed 's/,$//')
run build add --title "many tags" --tags "$long_tags"
assert_exit_code 1 $exit_code
assert_stderr_contains "refusing to truncate"
assert_board_not_contains '"many tags"'

long_ids=$(printf '%s,' $(seq 1 90) | sed 's/,$//')
run build add --title "many deps" --blocked-by "$long_ids"
assert_exit_code 1 $exit_code
assert_stderr_contains "refusing to truncate"
assert_board_not_contains '"many deps"'

# the boundary is exact: 255 fits, 256 does not
run build add --title "fits" --tags "$(printf 'a%.0s' $(seq 1 255))"
assert_exit_code 0 $exit_code
run build add --title "does not fit" --tags "$(printf 'a%.0s' $(seq 1 256))"
assert_exit_code 1 $exit_code

# a short but invalid list still reports the token, not the length
run build add --title "bad token" --blocked-by "1,bad,2"
assert_exit_code 1 $exit_code
assert_stderr_contains "'bad' is not a valid number"

# ...and a long one is reported whole. The token buffer used to be 64 bytes, so a
# 200-character copy-paste mistake was cut at 63 with no ellipsis, which reads as a
# typo that was not the one made.
bad_token=$(printf 'j%.0s' $(seq 1 200))
run build add --title "long bad token" --blocked-by "$bad_token"
assert_exit_code 1 $exit_code
case "$stderr" in
  *"$bad_token"*) ;;
  *)
    echo "FAIL: the offending token was truncated out of the error message"
    exit 1
    ;;
esac

# --- plan export is the repair hatch: it must still work ----------------------

write_board '{"id":1,"title":"no-status","created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"}'
run plan export
assert_exit_code 0 $exit_code
assert_stdout_contains '"no-status"'

# --- a corrupt board is not an empty board (ticket 010, defect A) -------------
# `plan stats` used to print the empty-board output and exit 0, so an agent concluded
# there was no work rather than that the board was broken.

echo "not json at all" > .kanban-board
cp .kanban-board .kanban-board.orig
# `build archive-done` was left off this list when 010 landed, which is the trap the
# ticket's own closing note warned about: the loop decides what gets covered.
for cmd in "plan stats" "plan list" "build init" "build archive-done"; do
  run $cmd
  assert_no_crash "$cmd"
  assert_exit_code 2 $exit_code
  assert_stderr_contains "invalid board file"
  assert_unchanged
done

# export is the repair hatch and must still dump a corrupt board verbatim
run plan export
assert_exit_code 0 $exit_code
assert_stdout_contains "not json"
rm -f .kanban-board.orig

# a genuinely absent board is still not an error
rm -f .kanban-board
run plan stats
assert_exit_code 0 $exit_code
assert_stdout_contains '"total"'
run plan list
assert_exit_code 0 $exit_code
assert_stdout_contains '"tickets"'

# --- build init on a board it cannot use (ticket 010, defect B) ---------------

echo "not json at all" > .kanban-board
run build init
assert_no_crash "build init"
assert_exit_code 2 $exit_code
assert_stderr_contains "invalid board file"

# schema-invalid board: init must not report success either
write_board '{"id":1,"title":"no-status","created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"}'
run build init
assert_no_crash "build init"
assert_exit_code 2 $exit_code
assert_stderr_contains "missing required field 'status'"

# a valid board: init succeeds and leaves the file alone
rm -f .kanban-board
run build init
assert_exit_code 0 $exit_code
cp .kanban-board .kanban-board.orig
run build add --title "keeper"
assert_exit_code 0 $exit_code
rm -f .kanban-board.orig
cp .kanban-board .kanban-board.orig
run build init
assert_exit_code 0 $exit_code
assert_unchanged

# --- duplicate keys hide a bad value (ticket 010, defect C) ------------------
# cJSON resolves duplicates first-wins, so the wrong-typed second "status" was
# invisible to every field check.

write_board '{"id":1,"title":"dup","status":"backlog","status":42,"created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"}'
cp .kanban-board .kanban-board.orig
for cmd in "plan stats" "plan list" "build move 1 ready" "build edit 1 title y"; do
  run $cmd
  assert_no_crash "$cmd"
  assert_exit_code 2 $exit_code
  assert_stderr_contains "duplicate key 'status'"
  assert_unchanged
done
rm -f .kanban-board.orig

# two contradictory but individually valid columns: still a duplicate
write_board '{"id":1,"title":"dup","status":"done","status":"backlog","created":"2026-01-01T00:00:00Z","updated":"2026-01-01T00:00:00Z"}'
run plan stats
assert_no_crash "plan stats"
assert_exit_code 2 $exit_code
assert_stderr_contains "duplicate key 'status'"

# --- a failed save says so (tickets 014 and 010) -----------------------------
# board_write had six failure paths and no diagnostic, and build add collapsed the
# result to exit 1, which SKILL.md defines as "usage / bad args". An agent meeting a
# full disk was told it had passed a bad argument.

write_board "$(valid_ticket 1 ok backlog)"
run build overwrite 1 title "written by rkb"
assert_exit_code 0 $exit_code

# Permission bits do not stop root, so verify the directory is genuinely unwritable
# rather than trusting the chmod.
chmod 500 .
if ( : > .rkb-write-probe ) 2>/dev/null; then
  rm -f .rkb-write-probe
  chmod 755 .
  echo "  (skipped: this user can write to a read-only directory)"
else
  # The capture file must live outside the directory we just locked, or the redirect
  # itself fails. Capture and restore first; assert only afterwards.
  errfile=$(mktemp)
  "$RKB" build add --title doomed >/dev/null 2>"$errfile"
  save_exit=$?
  chmod 755 .
  save_err=$(cat "$errfile")
  rm -f "$errfile"

  # the property that matters is not silence, not the specific errno string
  if [ -z "$save_err" ]; then
    echo "FAIL: a failed board save printed nothing to stderr"
    exit 1
  fi
  if [ "$save_exit" -eq 0 ]; then
    echo "FAIL: a failed board save exited 0"
    exit 1
  fi
  case "$save_err" in
    *".kanban-board"*) ;;
    *) echo "FAIL: save diagnostic does not name the board: $save_err"; exit 1 ;;
  esac
  # a board failure is exit 2, not the usage code 1
  assert_eq "2" "$save_exit"
  # and the board itself is untouched
  assert_board_contains '"title":.*"written by rkb"'
  assert_board_not_contains '"doomed"'
fi

# --- a valid board still works after all of that ------------------------------

rm -f .kanban-board
run build init
assert_exit_code 0 $exit_code
run plan lint
assert_exit_code 0 $exit_code

run build add --title "first"
assert_exit_code 0 $exit_code
run build add --title "second" --blocked-by 1
assert_exit_code 0 $exit_code
run build move 1 ready
assert_exit_code 0 $exit_code
run plan stats
assert_exit_code 0 $exit_code
assert_stdout_contains '"total":[[:space:]]*2'

# an absent board is a board error here, as it is for every other mutating command
rm -f .kanban-board
run build archive-done
assert_exit_code 2 $exit_code
assert_stderr_contains "no board found"

# atomic save leaves no temp file behind. The temp name is now unique per process, so
# check for any of them rather than the old fixed path.
if ls .kanban-board.* >/dev/null 2>&1; then
  echo "FAIL: a temp file was left behind after a successful save:"
  ls -a | grep 'kanban-board\.'
  exit 1
fi

# ...and none is left behind when a write is refused, either
write_board "$(valid_ticket 1 ok backlog)"
if ls .kanban-board.* >/dev/null 2>&1; then
  echo "FAIL: a temp file was left behind"
  exit 1
fi
run build overwrite 1 blocked_by "1,bad"
assert_exit_code 1 $exit_code
if ls .kanban-board.* >/dev/null 2>&1; then
  echo "FAIL: a temp file survived a refused write"
  exit 1
fi

# the board must stay readable after every one of those
run plan lint
assert_exit_code 0 $exit_code

# --- the single-writer contract (ticket 007, QA finding) ----------------------
# Saves are atomic, so the board can never be half-written. Losing a ticket to a
# concurrent writer is documented as out of contract rather than tested, since a
# parallel test is timing-dependent and would be flaky.

# permissions must not silently tighten: mkstemp creates 0600, so a missing fchmod
# would make the board owner-only where it used to be 0644. The board has to be written
# by rkb for this to mean anything - a shell redirect would give 644 regardless.
write_board "$(valid_ticket 1 ok backlog)"
run build overwrite 1 title "forces rkb to save"
assert_exit_code 0 $exit_code
mode=$(stat -c '%a' .kanban-board 2>/dev/null || echo "unknown")
if [ "$mode" = "600" ]; then
  echo "FAIL: board is owner-only (0600); mkstemp's mode was not restored"
  exit 1
fi

teardown