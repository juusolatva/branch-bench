# Task completion checklist

No test framework, linter or formatter exists. Verify with:

1. gcc build with `-std=c99 -Wall -Wextra -pedantic` plus the required flags: no warnings. Also the clang build (see `mem:suggested_commands`).
2. `clang --analyze -std=c99 -Xclang -analyzer-werror <repo>/branch_bench.c`, run from a scratch dir (CI form; see `mem:suggested_commands`): no warnings.
3. `./branch_bench` exits 0, all four tests run, and Test 4 prints no "results differ" warning. Shuffled/random variants should be clearly slower than sorted/periodic in Tests 1, 2 and 4-branch.
4. Output: the default run's console and `-o` report keep their format (compare with the previous build, numbers masked). New features must only change output when their option is used.
5. Kernels: compare the disassembly with the previous build (instructions, ignoring addresses and padding nops); the data-dependent conditional branch must remain inside the inner loop of `sum_threshold`/`count_decisions` (no cmov, setcc/sbb/adc or SIMD; the loop back-edge doesn't count) and no loop may be eliminated. Check that no hot-loop jump crosses or ends on a 32-byte boundary (JCC erratum on Skylake-derived Intel). If kernel code changed, compare cycles per element and cost per miss against the previous build (`-n 5`).
6. Option changes: test invalid and missing values (exit code 1, clear message) and `--help`.
7. Non-perf path: code must still compile without `HAVE_PERF` (e.g. `-U__linux__` on a scratch copy) and handle missing individual counters.
8. CI parity: `smoke_test.sh` passes (normal build, and with the `fake_perf.c` shim when perf/cpuinfo/cpufreq code changed); `check_kernels.sh` passes (necessary, not sufficient: still do step 5); new options are added to `smoke_test.sh`; a new branch-keeping kernel is added to `check_kernels.sh`; a new perf counter follows the checklist in `mem:conventions`. See `mem:suggested_commands`.
9. Keep `AGENTS.md`, `README.md` and these memories in sync with behaviour changes.

Commits are allowed; prefer opening a pull request on a branch over pushing directly to `main`.
