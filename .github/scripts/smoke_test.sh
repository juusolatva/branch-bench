#!/bin/sh
# Runs a branch_bench binary through every command-line path with small
# sizes: all tests, each option in short and long form, the report file and
# invalid input.  Checks exit codes and report contents, never timings.
# Used for the sanitizer and coverage builds.
#
# Usage: smoke_test.sh <binary> [work dir]
set -u

bin=${1:?usage: smoke_test.sh <binary> [work dir]}
if [ $# -ge 2 ]; then
    dir=$2
    mkdir -p "$dir"
else
    # Only a directory this script created is removed again.
    dir=$(mktemp -d) || exit 1
    trap 'rm -rf "$dir"' EXIT
fi
small="--size 64K --reps 2"
fail=0

# expect <exit code> <args...>
expect() {
    want=$1; shift
    "$bin" "$@" >"$dir/stdout" 2>"$dir/stderr"
    got=$?
    if [ "$got" -eq "$want" ] && [ "$want" -ne 0 ] && [ ! -s "$dir/stderr" ]; then
        echo "FAIL (exit $got) $*: no error message"
        fail=1
    elif [ "$got" -eq "$want" ]; then
        echo "ok   (exit $got) $*"
    else
        echo "FAIL (exit $got, want $want) $*"
        cat "$dir/stderr"
        fail=1
    fi
}

# Normal runs
expect 0 --help
expect 0 -h
# shellcheck disable=SC2086  # $small is meant to split into words
expect 0 $small
# shellcheck disable=SC2086
expect 0 $small -n 3 -o "$dir/report.md"
expect 0 --trials 2 --test 2 --test 4 --output "$dir/report2.md" --size 4k --reps 1
expect 0 -t 1 -t 3 --size 1m --reps 1
expect 0 -t 3 --size 1000 --reps 1
expect 0 -t 1 --size 1 --reps 1k

# The report must contain every test that ran.
for t in 1 2 3 4; do
    grep -q "^### Test $t " "$dir/report.md" || { echo "FAIL report.md lacks Test $t"; fail=1; }
done
# A report of selected tests has only those, and says so.
for t in 1 3; do
    if grep -q "^### Test $t " "$dir/report2.md"; then
        echo "FAIL report2.md has Test $t, which was not selected"; fail=1
    fi
done
for t in 2 4; do
    grep -q "^### Test $t " "$dir/report2.md" || { echo "FAIL report2.md lacks Test $t"; fail=1; }
done
grep -qx '_Only test(s) 2, 4 were run._' "$dir/report2.md" ||
    { echo "FAIL report2.md lacks the selected-tests note"; fail=1; }
# ... and record the compiler that built it.
grep -q '^| Array size | Repetitions | Perf counters | Compiler |$' "$dir/report.md" ||
    { echo "FAIL report.md lacks a Compiler column"; fail=1; }

# Invalid input: exit code 1 with a message
expect 1 --size 0
expect 1 --size 5X
expect 1 --size -1
expect 1 --size 2048M
expect 1 --reps 0
expect 1 --reps
expect 1 --reps 1M
expect 1 -n 100
expect 1 -t 5
expect 1 -o
expect 1 -o "$dir/no/such/dir/report.md"
expect 1 --bogus

# /dev/full accepts open() but fails writes, including buffered close().
if [ -c /dev/full ]; then
    expect 1 -t 3 --size 1 --reps 1 -o /dev/full
    if grep -q 'Formatted report written' "$dir/stdout"; then
        echo "FAIL report write failure announced success"; fail=1
    fi
    if ! grep -q 'Error writing report' "$dir/stderr"; then
        echo "FAIL report write failure lacks a diagnostic"; fail=1
    fi
fi

exit "$fail"
