#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

passed=0
failed=0

for test in test_*.sh; do
  if [ -f "$test" ]; then
    printf "Running %s ... " "$test"
    if bash "$test" 2>&1; then
      echo "PASS"
      passed=$((passed + 1))
    else
      echo "FAIL"
      failed=$((failed + 1))
    fi
  fi
done

echo
echo "Results: $passed passed, $failed failed"
[ "$failed" -eq 0 ] || exit 1
