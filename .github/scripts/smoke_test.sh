#!/bin/sh
# Runs a branch_bench binary through every command-line path with small
# sizes: the default tests, the opt-in tests 5 and 6 and their size rules,
# --hard, each option in short and long form, the report file and invalid
# input.  Checks exit codes only, never timings.  Used for the
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
# Test 3 skips when --size is not a multiple of 64 (equal calls to every
# target).
expect 0 -t 3 --size 1000 --reps 1 -o "$dir/skip3.md"
grep -q 'Skipped:' "$dir/skip3.md" || { echo "FAIL skip3.md lacks the skip reason"; fail=1; }
expect 0 -t 3 --size 64 --reps 1
# --cpu pins the run; a CPU that cannot be used is reported, not fatal.
# The CPUs come from this process's own affinity mask, so the cases hold in
# a cpuset-limited container and on hosts with up to 1024 CPUs.
allowed=$(python3 -c 'import os; print(min(os.sched_getaffinity(0)))')
expect 0 -t 3 --cpu "$allowed" --size 4K --reps 1 -o "$dir/cpu_ok.md"
grep -q "^_Pinned to CPU $allowed._\$" "$dir/cpu_ok.md" || { echo "FAIL cpu_ok.md lacks the pinned line"; fail=1; }
denied=$(python3 -c 'import os; s = os.sched_getaffinity(0); print(next((c for c in range(1024) if c not in s), ""))')
if [ -n "$denied" ]; then
    expect 0 -t 3 --cpu "$denied" --size 4K --reps 1 -o "$dir/cpu_bad.md"
    grep -q "^_CPU affinity request for CPU $denied failed: " "$dir/cpu_bad.md" ||
        { echo "FAIL cpu_bad.md lacks the failed-pin line"; fail=1; }
    grep -q 'running unpinned' "$dir/stderr" || { echo "FAIL failed pin lacks its warning"; fail=1; }
else
    echo "skip failed-pin case: all of CPUs 0-1023 are allowed"
fi
# --seed takes a decimal or 0x hex base seed for every test's input.
expect 0 -t 3 --seed 7 --size 4K --reps 1
expect 0 -t 3 --seed 0x1f --size 4K --reps 1
expect 0 -t 3 --seed 010 --size 4K --reps 1
expect 0 -t 1 --size 4K --reps 1 -o "$dir/rev.md"
grep -q '^_Benchmark revision 3._$' "$dir/rev.md" || { echo "FAIL rev.md lacks the revision line"; fail=1; }
# Test 5 is opt-in; it skips (exit 0) when --size is not a multiple of 2048,
# while other selected tests still run.
# Test 4 first: fake_perf.so fails the first and third counter reads, and its
# four measurements absorb them, so every Test 5 row gets counts.
# shellcheck disable=SC2086
expect 0 -t 4 -t 5 $small -o "$dir/t5.md"
grep -q '^| Period 2048 (k=11) |' "$dir/t5.md" || { echo "FAIL t5.md lacks Test 5's table"; fail=1; }
expect 0 -t 5 --size 1000 --reps 1 -o "$dir/skip5.md"
grep -q 'Skipped:' "$dir/skip5.md" || { echo "FAIL skip5.md lacks the skip reason"; fail=1; }
# Below 64K elements Test 5 measures, but replaces its summary with a note.
expect 0 -t 5 --size 2048 --reps 1 -o "$dir/short5.md"
grep -q '^_n/a: --size below' "$dir/short5.md" || { echo "FAIL short5.md lacks the n/a note"; fail=1; }
expect 0 -t 1 -t 5 --size 1000 --reps 1 -o "$dir/mixed.md"
grep -q '^| Sorted |' "$dir/mixed.md" || { echo "FAIL mixed.md lacks Test 1's table"; fail=1; }
grep -q 'Skipped:' "$dir/mixed.md" || { echo "FAIL mixed.md lacks Test 5's skip reason"; fail=1; }
# Test 2 skips when --size is not a multiple of 16 (whole periods of every
# bias pattern); other selected tests still run.
expect 0 -t 2 --size 1000 --reps 1 -o "$dir/skip2.md"
grep -q 'Skipped:' "$dir/skip2.md" || { echo "FAIL skip2.md lacks the skip reason"; fail=1; }
expect 0 -t 2 --size 16 --reps 1
# Test 6 is opt-in; it skips when --size < 64 or --size × --reps < 65664.
# At 64K × 6 only d = 4 gets the 65536 blocks per pass that a "Correlation
# used" figure needs, so with the miss counter (CI's fake_perf.so step
# checks this) the report has both a figure and n/a entries.
expect 0 -t 4 -t 6 --size 64K --reps 6 -o "$dir/t6.md"
grep -q '^| d= 4 correlated |' "$dir/t6.md" || { echo "FAIL t6.md lacks Test 6's table"; fail=1; }
expect 0 -t 6 --size 1000 --reps 1 -o "$dir/skip6.md"
grep -q 'Skipped:' "$dir/skip6.md" || { echo "FAIL skip6.md lacks the skip reason"; fail=1; }
expect 0 -t 6 --size 32 --reps 100000 -o "$dir/skip6b.md"
grep -q 'Skipped:' "$dir/skip6b.md" || { echo "FAIL skip6b.md lacks the skip reason"; fail=1; }
# --hard runs all six tests, also next to -t, and says so.
# shellcheck disable=SC2086
expect 0 --hard $small -o "$dir/hard.md"
grep -q '(all, with --hard)' "$dir/stdout" || { echo "FAIL --hard banner lacks its tests line"; fail=1; }
expect 0 --hard -t 2 --size 4K --reps 1 -o "$dir/hard2.md"
# The largest legal run fits the trial log: 71 (variant, pair) streams
# (Test 2's shuffled rows and Test 5's random row are in two pairs) × 99
# trials = 7029 records.  Large
# enough that Test 6, the biggest user, runs.
expect 0 --hard -n 99 --size 64K --reps 2 --json "$dir/max.jsonl"
for f in hard.md hard2.md; do
    for t in 1 2 3 4 5 6; do
        grep -q "^### Test $t " "$dir/$f" || { echo "FAIL $f lacks Test $t"; fail=1; }
    done
    grep -q 'All tests were run (--hard)' "$dir/$f" || { echo "FAIL $f lacks the --hard note"; fail=1; }
    if grep -q 'Only test(s)' "$dir/$f"; then echo "FAIL $f says only some tests ran"; fail=1; fi
done

# The report must contain every test that ran.
for t in 1 2 3 4; do
    grep -q "^### Test $t " "$dir/report.md" || { echo "FAIL report.md lacks Test $t"; fail=1; }
done
# ... and only those: the default run leaves out the opt-in tests.
if grep -q '^### Test [56] ' "$dir/report.md"; then
    echo "FAIL default report.md contains an opt-in test"; fail=1
fi
# ... and describe the training and pairing protocol when trials > 1.
grep -q '^_Median of 3 trials per variant; each trial follows one untimed training run' "$dir/report.md" ||
    { echo "FAIL report.md lacks the trial protocol line"; fail=1; }
# ... and say nothing about affinity unless --cpu was given.
if grep -q 'CPU affinity\|Pinned to' "$dir/report.md"; then echo "FAIL default report mentions affinity"; fail=1; fi
# ... and record the compiler that built it.
grep -q '^| Array size | Repetitions | Perf counters | Compiler |$' "$dir/report.md" ||
    { echo "FAIL report.md lacks a Compiler column"; fail=1; }

# --json: every trial and summary as JSON Lines; check_export.py rebuilds
# the summaries from the trials and compares them with the report.
check_export=$(dirname "$0")/check_export.py
export_ok() {
    if ! python3 "$check_export" "$@" >"$dir/check.out" 2>&1; then
        echo "FAIL check_export.py $*"; cat "$dir/check.out"; fail=1
    else
        echo "ok   check_export.py $*"
    fi
}
# shellcheck disable=SC2086
expect 0 $small -n 3 -o "$dir/report3.md" --json "$dir/run.jsonl"
export_ok "$dir/run.jsonl" --report "$dir/report3.md"
# shellcheck disable=SC2086
expect 0 --hard $small -o "$dir/hard3.md" --json "$dir/hard.jsonl"
export_ok "$dir/hard.jsonl" --report "$dir/hard3.md"
# A skipped Test 2 leaves a valid export of the tests that ran.
expect 0 -t 1 -t 2 --size 1000 --reps 1 -o "$dir/skip2b.md" --json "$dir/skip2b.jsonl"
export_ok "$dir/skip2b.jsonl" --report "$dir/skip2b.md"
# One element per array: the kernels' smallest input, against the reference.
expect 0 -t 1 -t 4 --size 1 --reps 1 -o "$dir/one.md" --json "$dir/one.jsonl"
export_ok "$dir/one.jsonl" --report "$dir/one.md"
# Test 6's "Correlation used" line is rebuilt from the export too.  At
# 64K × 6, d = 4 has a figure and the other distances n/a (with counters).
expect 0 -t 4 -t 6 --size 64K --reps 6 -o "$dir/t6j.md" --json "$dir/t6.jsonl"
export_ok "$dir/t6.jsonl" --report "$dir/t6j.md"
# The checker must notice a changed figure (none without the miss counter).
sed 's/^\(\*\*Correlation used:\*\* d=4 \)[0-9]*%/\1101%/' "$dir/t6j.md" > "$dir/t6bad.md"
if cmp -s "$dir/t6j.md" "$dir/t6bad.md"; then
    echo "skip corrupted-correlation case: no miss counter"
elif python3 "$check_export" "$dir/t6.jsonl" --report "$dir/t6bad.md" > "$dir/check.out" 2>&1; then
    echo "FAIL check_export.py accepted a corrupted Correlation used line"; fail=1
elif grep -q '^FAIL Test 6: report lacks' "$dir/check.out"; then
    echo "ok   check_export.py rejects a corrupted Correlation used line"
else
    echo "FAIL check_export.py failed for another reason:"; cat "$dir/check.out"; fail=1
fi
# Selecting more tests does not change Test 3's input; --seed does.
expect 0 -t 3 --size 4K --reps 1 --json "$dir/a.jsonl"
expect 0 -t 1 -t 3 --size 4K --reps 1 --json "$dir/b.jsonl"
export_ok "$dir/a.jsonl" --same-results "$dir/b.jsonl" --test 3
expect 0 -t 3 --seed 7 --size 4K --reps 1 --json "$dir/c.jsonl"
export_ok "$dir/a.jsonl" --different-results "$dir/c.jsonl" --test 3
# The same for every other test: its input must not depend on Test 1 having
# run before it, and must change with --seed.
for t in 1 2 4 5 6; do
    expect 0 -t "$t" --size 64K --reps 2 --json "$dir/t$t-a.jsonl"
    expect 0 -t 1 -t "$t" --size 64K --reps 2 --json "$dir/t$t-b.jsonl"
    expect 0 -t "$t" --seed 7 --size 64K --reps 2 --json "$dir/t$t-c.jsonl"
    export_ok "$dir/t$t-a.jsonl" --same-results "$dir/t$t-b.jsonl" --test "$t"
    export_ok "$dir/t$t-a.jsonl" --different-results "$dir/t$t-c.jsonl" --test "$t"
done
export_ok "$dir/max.jsonl"
# The checker must notice a report whose numbers differ from the export:
# change the first counter cell that holds a number (none without counters).
python3 - "$dir/report3.md" "$dir/bad.md" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
header = None
out = []
done = False
for line in text.splitlines(keepends=True):
    if line.startswith("| Variant |"):
        header = [c.strip() for c in line.strip().strip("|").split("|")]
    elif not done and header and line.startswith("| ") and not line.startswith("|---"):
        cells = line.rstrip("\n").split("|")
        col = header.index("Cycles") + 1
        if re.fullmatch(r" [0-9]+ ", cells[col]):
            cells[col] = " 999 "
            line = "|".join(cells) + "\n"
            done = True
    out.append(line)
open(sys.argv[2], "w", encoding="utf-8").write("".join(out))
PY
if cmp -s "$dir/report3.md" "$dir/bad.md"; then
    echo "skip corrupted-report case: no counter columns without perf counters"
elif python3 "$check_export" "$dir/run.jsonl" --report "$dir/bad.md" > "$dir/check.out" 2>&1; then
    echo "FAIL check_export.py accepted a corrupted counter column"; fail=1
elif grep -q '^FAIL Test [0-9] .*: Cycles ' "$dir/check.out"; then
    echo "ok   check_export.py rejects a corrupted counter column"
else
    echo "FAIL check_export.py failed for another reason:"; cat "$dir/check.out"; fail=1
fi
# -o and --json must be different files, also through a hard link.
expect 1 -t 3 --size 1K --reps 1 -o "$dir/same.out" --json "$dir/same.out"
: > "$dir/x.md"; ln -f "$dir/x.md" "$dir/y.json"
expect 1 -t 3 --size 1K --reps 1 -o "$dir/x.md" --json "$dir/y.json"
# A --json path that cannot be opened must not empty an existing report.
echo "keep me" > "$dir/keep.md"
expect 1 -t 3 --size 1K --reps 1 -o "$dir/keep.md" --json "$dir/no/such/dir/x.jsonl"
grep -q '^keep me$' "$dir/keep.md" || { echo "FAIL failed --json open truncated the report"; fail=1; }
# ... and through two spellings of a file that does not exist yet.
rm -f "$dir/fresh.md"
expect 1 -t 3 --size 1K --reps 1 -o "$dir/fresh.md" --json "$dir/./fresh.md"

# Invalid input: exit code 1 with a message
expect 1 --size 0
expect 1 --size 5X
expect 1 --size -1
expect 1 --size 2048M
expect 1 --reps 0
expect 1 --reps
expect 1 -n 100
expect 1 -t 7
expect 1 --cpu 1024
expect 1 --cpu
expect 1 --cpu x
expect 1 --json
expect 1 --json "$dir/no/such/dir/x.jsonl"
expect 1 --seed
expect 1 --seed -1
expect 1 --seed 18446744073709551616
expect 1 --seed 12abc
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
    expect 1 -t 3 --size 1 --reps 1 --json /dev/full
    if ! grep -q 'Error writing JSON export' "$dir/stderr"; then
        echo "FAIL JSON write failure lacks a diagnostic"; fail=1
    fi
fi

exit "$fail"
