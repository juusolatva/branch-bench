#!/bin/sh
# Runs a branch_bench binary through every command-line path with small
# sizes: all tests, each option in short and long form, the report file and
# invalid input.  Checks exit codes only, never timings.  Used for the
# sanitizer and coverage builds.
#
# Usage: smoke_test.sh <binary> [work dir]
set -u

bin=${1:?usage: smoke_test.sh <binary> [work dir]}
dir=${2:-$(mktemp -d)}
mkdir -p "$dir"
small="--size 64K --reps 2"
fail=0

# expect <exit code> <args...>
expect() {
    want=$1; shift
    "$bin" "$@" >"$dir/stdout" 2>"$dir/stderr"
    got=$?
    if [ "$got" -eq "$want" ]; then
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
# Test 5 is opt-in; it skips (exit 0) when --size is not a multiple of 2048,
# while other selected tests still run.
# Test 4 first: fake_perf.so fails the first and third counter reads, and its
# four measurements absorb them, so every Test 5 row gets counts.
# shellcheck disable=SC2086
expect 0 -t 4 -t 5 $small -o "$dir/t5.md"
grep -q '^### Test 5 ' "$dir/t5.md" || { echo "FAIL t5.md lacks Test 5"; fail=1; }
expect 0 -t 5 --size 1000 --reps 1 -o "$dir/skip5.md"
grep -q 'Skipped:' "$dir/skip5.md" || { echo "FAIL skip5.md lacks the skip reason"; fail=1; }
expect 0 -t 2 -t 5 --size 1000 --reps 1 -o "$dir/mixed.md"
grep -q '^### Test 2 ' "$dir/mixed.md" || { echo "FAIL mixed.md lacks Test 2"; fail=1; }
grep -q 'Skipped:' "$dir/mixed.md" || { echo "FAIL mixed.md lacks Test 5's skip reason"; fail=1; }

# The report must contain every test that ran.
for t in 1 2 3 4; do
    grep -q "^### Test $t " "$dir/report.md" || { echo "FAIL report.md lacks Test $t"; fail=1; }
done
# ... and only those: the default run leaves out the opt-in tests.
if grep -q '^### Test 5 ' "$dir/report.md"; then
    echo "FAIL default report.md contains opt-in Test 5"; fail=1
fi
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
expect 1 -n 100
expect 1 -t 6
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
