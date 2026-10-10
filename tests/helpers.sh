RKB="$(cd "$(dirname "$BASH_SOURCE")/.." && pwd)/rkb"

setup() {
  TEST_DIR=$(mktemp -d /tmp/rkb-test-XXXXXX)
  cd "$TEST_DIR"
}

teardown() {
  rm -rf "$TEST_DIR"
}

assert_exit_code() {
  local expected=$1
  local actual=$2
  if [ "$actual" -ne "$expected" ]; then
    echo "FAIL: expected exit code $expected, got $actual"
    exit 1
  fi
}

# Exact match, including trailing newlines. assert_stdout_contains passes for a
# command that printed the right thing *plus* something else, which is how
# `build add` printing "ok\nok" went unnoticed.
assert_eq() {
  local expected="$1"
  local actual="$2"
  if [ "$actual" != "$expected" ]; then
    echo "FAIL: expected exactly [$expected], got [$actual]"
    exit 1
  fi
}

assert_stdout_contains() {
  local pattern="$1"
  if ! echo "$stdout" | grep -Eq -- "$pattern"; then
    echo "FAIL: stdout did not contain '$pattern'"
    echo "stdout: $stdout"
    exit 1
  fi
}

assert_stdout_not_contains() {
  local pattern="$1"
  if echo "$stdout" | grep -Eq -- "$pattern"; then
    echo "FAIL: stdout contains '$pattern'"
    echo "stdout: $stdout"
    exit 1
  fi
}

assert_stderr_contains() {
  local pattern="$1"
  if ! echo "$stderr" | grep -Eq -- "$pattern"; then
    echo "FAIL: stderr did not contain '$pattern'"
    echo "stderr: $stderr"
    exit 1
  fi
}

assert_stderr_not_contains() {
  local pattern="$1"
  if echo "$stderr" | grep -Eq -- "$pattern"; then
    echo "FAIL: stderr contains '$pattern'"
    echo "stderr: $stderr"
    exit 1
  fi
}

# Checks that the command's stderr ends with a newline. Command substitution strips
# trailing newlines, so $stderr can never answer this - it re-runs the command and
# looks at the raw file. Pass the same arguments `run` would have taken.
assert_stderr_ends_in_newline() {
  local errfile
  errfile=$(mktemp)
  "$RKB" "$@" 2>"$errfile"
  if [ -s "$errfile" ] && [ -n "$(tail -c 1 "$errfile")" ]; then
    rm -f "$errfile"
    echo "FAIL: stderr does not end with a newline"
    exit 1
  fi
  rm -f "$errfile"
}

assert_board_exists() {
  if [ ! -f ".kanban-board" ]; then
    echo "FAIL: .kanban-board not found"
    exit 1
  fi
}

assert_board_contains() {
  local pattern="$1"
  if ! grep -Eq -- "$pattern" .kanban-board; then
    echo "FAIL: .kanban-board does not contain '$pattern'"
    echo "contents: $(cat .kanban-board)"
    exit 1
  fi
}

assert_board_not_contains() {
  local pattern="$1"
  if grep -Eq -- "$pattern" .kanban-board; then
    echo "FAIL: .kanban-board contains '$pattern'"
    echo "contents: $(cat .kanban-board)"
    exit 1
  fi
}

run() {
  local outfile=$(mktemp)
  local errfile=$(mktemp)
  "$RKB" "$@" >"$outfile" 2>"$errfile"; exit_code=$?
  stdout=$(cat "$outfile")
  stderr=$(cat "$errfile")
  rm -f "$outfile" "$errfile"
}
