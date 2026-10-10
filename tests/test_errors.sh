. ./helpers.sh

setup

# no board → plan list returns empty
run plan list
assert_exit_code 0 $exit_code
assert_stdout_contains '"tickets":'

# no board → plan export fails
run plan export
assert_exit_code 2 $exit_code

# no board → plan show fails
run plan show 1
assert_exit_code 2 $exit_code

# invalid JSON board
echo "not json" > .kanban-board
run plan list
assert_exit_code 2 $exit_code

# invalid command
run rkb build invalid
assert_exit_code 1 $exit_code

# plan with invalid subcommand
run rkb plan invalid
assert_exit_code 1 $exit_code

# --- unknown flags must be rejected, not ignored (ticket 006, defect 6a) ------
# used to be dropped by the getopt loop's missing default case, so the real problem
# was hidden behind "--title is required"

run build add --titel "typo"
assert_exit_code 1 $exit_code
assert_stderr_contains "unknown option '--titel'"
assert_stderr_not_contains "--title is required"

run build add -Z "what"
assert_exit_code 1 $exit_code
assert_stderr_contains "unknown option '-Z'"

# the dangerous variant: a valid --title alongside a typo'd flag used to succeed and
# silently drop the typo'd one
run build add --title "real" --priorityy 2
assert_exit_code 1 $exit_code
assert_stderr_contains "unknown option '--priorityy'"
assert_board_not_contains '"real"'

# --- every subcommand answers --help (ticket 006, defect 6b) ------------------

run build add --help
assert_exit_code 0 $exit_code
assert_stderr_contains "--title"

run build move --help
assert_exit_code 0 $exit_code
assert_stderr_contains "build move"

run build edit --help
assert_exit_code 0 $exit_code
assert_stderr_contains "build edit"

run plan list --help
assert_exit_code 0 $exit_code
assert_stderr_contains "plan list"
# used to ignore the flag and dump the whole board instead
assert_stdout_not_contains '"tickets"'

run plan show --help
assert_exit_code 0 $exit_code
assert_stderr_contains "plan show"

# help wins even when the rest of the command line is wrong
run build add --help
assert_exit_code 0 $exit_code

# --- a flag must never be stored as a field value (ticket 006, defect 6c) -----
# `build edit 1 title --pretty` used to set the title to the literal string

# the earlier invalid-JSON case left a corrupt board behind, which build init now
# (correctly) rejects
rm -f .kanban-board
run build init
assert_exit_code 0 $exit_code
run build add --title "original"
assert_exit_code 0 $exit_code

run build edit 1 title --pretty
assert_exit_code 1 $exit_code
assert_stderr_contains "looks like an option"
assert_board_contains '"title":.*"original"'

run build edit 1 notacolumn --pretty
assert_exit_code 1 $exit_code
assert_board_not_contains '"notacolumn"'

# -- is the documented escape hatch for a value that really starts with a dash
run build overwrite -- 1 title --pretty
assert_exit_code 0 $exit_code
assert_board_contains '"title":.*"--pretty"'

# --- build add's first run on a fresh board (ticket 012, defect 1) ------------
# board_init printed "ok" when it created the board and board_add printed it again,
# so a caller doing `result=$(rkb build add --title x)` got "ok\nok" and failed.

rm -f .kanban-board
run build add --title "first ever"
assert_exit_code 0 $exit_code
assert_eq "ok" "$stdout"

# every add after that also prints exactly one ok
run build add --title "second"
assert_exit_code 0 $exit_code
assert_eq "ok" "$stdout"

run build add --title "third"
assert_exit_code 0 $exit_code
assert_eq "ok" "$stdout"

# --- the gate diagnostic is a sentence, not a mangled line (ticket 012, defect 2)

run build move 1 done
assert_exit_code 1 $exit_code
assert_stderr_contains "override"
assert_stderr_not_contains "overide"
# must end with a newline, or the shell prompt lands on the same line
assert_stderr_ends_in_newline build move 1 done

# --- a missing option value is not an unknown option (ticket 012, defect 3) -----

run build add --title
assert_exit_code 1 $exit_code
assert_stderr_contains "requires a value"
assert_stderr_not_contains "unknown option"

run build add --priority
assert_exit_code 1 $exit_code
assert_stderr_contains "requires a value"

# an unknown flag is still reported as unknown
run build add --titel x
assert_exit_code 1 $exit_code
assert_stderr_contains "unknown option '--titel'"

# --- --help works on the argument-less subcommands (ticket 006) ----------------
# these ran the command anyway: `rkb build archive-done --help` archived a ticket

run build init --help
assert_exit_code 0 $exit_code
assert_stderr_contains "build init"

run build archive-done --help
assert_exit_code 0 $exit_code
assert_stderr_contains "archive-done"
# and it must not have done the archiving it was asked about
assert_board_contains '"status":.*"backlog"'

teardown
