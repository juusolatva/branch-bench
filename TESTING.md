# Testing

How to check a change to branch_bench. There is no test framework: the
checks are the shell scripts and C files in [`.github/scripts/`](.github/scripts/).
Each script's header comment explains what it checks and what arguments it takes.
CI runs them on pushes to `main` and on pull requests (see [CI](#ci)). Hosted runners have no
hardware performance counters, though, so timings and real counter
output can only be checked on your own machine.

## Quick check

Run these from the repository root before committing:

```sh
flags="-std=c99 -O2 -fno-tree-vectorize -fno-if-conversion"
gcc $flags -Wall -Wextra -pedantic -Werror -c -o /dev/null branch_bench.c              # warnings, as in CI
gcc $flags -Wall -Wextra -pedantic -Werror -U__linux__ -c -o /dev/null branch_bench.c  # non-perf path
./run.sh --size 64K --reps 2                   # build as run.sh does, run a small workload
.github/scripts/smoke_test.sh ./branch_bench   # every option, report contents, invalid input
.github/scripts/check_kernels.sh ./branch_bench
.github/scripts/check_jcc.sh ./branch_bench    # x86-64 only; must print nothing
(
    dir=$(mktemp -d) && trap 'rm -rf "$dir"' EXIT && repo=$PWD &&
    gcc $flags -Wall -Wextra -pedantic -Werror -o "$dir/perf_test" .github/scripts/perf_test.c &&
    "$dir/perf_test" &&
    cd "$dir" && clang --analyze -std=c99 -Xclang -analyzer-werror "$repo/branch_bench.c"
)
```

- **`smoke_test.sh <binary> [work dir]`** runs each option in its short and long
  forms, valid and invalid. It checks exit codes and report contents, and tests a
  failed report write through `/dev/full` where that file exists. It never checks timings.
- **`check_kernels.sh`** fails if the kernels lost what they measure. Tests 1–2
  need a conditional branch on the data inside the inner loop, with no `cmov`/`csel`,
  no flag arithmetic and no SIMD. Test 3 needs an indirect call to 32 distinct leaf
  functions, and Test 4's branchless variant must have no data branch. It supports
  x86-64 and aarch64; for a cross build, set `ARCH` and `OBJDUMP`. A pass is
  necessary but not sufficient, so still read the disassembly (below).
- **`check_jcc.sh`** lists kernel jumps that cross or end on a 32-byte boundary
  (see [JCC erratum](#jcc-erratum)). A build made by `run.sh` must list none.
- **`perf_test.c`** tests counter scaling, error recovery and raw-event reporting
  without a PMU. It builds in a new private directory, never at a fixed path such as
  `/tmp/perf_test`, because another user on the machine could own that path.
- **`clang --analyze`** finds the same kind of path-based bugs that SonarQube
  reports (uninitialized values, out-of-bounds reads). Run it from a scratch
  directory, because it writes a `.plist` file into the current one.

## After changing code

### Kernel codegen

Any change in the file can move the kernels to new addresses, and that can change
timings even when the kernels' code is untouched. Compare the kernels'
disassembly with the previous build's, ignoring addresses and padding `nop`s:

```sh
objdump -d --no-show-raw-insn branch_bench | awk '/<(sum_threshold|sum_branch|sum_branchless|count_decisions|dispatch_sequential|dispatch_random)(\.[^>]*)?>:/,/^$/'
```

- Look for a conditional jump on the data inside the inner loop, not just for
  the absence of `cmov`.
- GCC may add clone suffixes such as `.constprop.0`. It folds `sum_branch` into
  `sum_threshold` because their bodies are identical, so only one of the two is listed.
- If kernel code changed, compare times, cycles and cost per miss with the
  previous build on real hardware (`./branch_bench -n 5`).

### JCC erratum

On Skylake-derived Intel CPUs, including the i5-8350U in RESULTS.md, microcode
slows any jump that crosses or ends on a 32-byte boundary. An unrelated edit
could therefore add or remove that penalty for some kernels. `run.sh`'s x86-64
assembler flags pad every jump, `call` and `ret` off those boundaries;
`-mbranches-within-32B-boundaries` on its own leaves `call` and `ret` out. The
padding adds only `nop`s and redundant `cs` prefixes, so the kernels'
instructions stay the same. Builds without the flags (Clang, CI's other jobs)
are not checked.

### Default output

A run with no options must keep its console and report format. Compare it with
the previous build's output, with the numbers masked (in bash, for the `<(...)`):

```bash
mask() { sed -E 's/[0-9][0-9.]*/N/g; s/ +/ /g' "$@"; }
old/branch_bench -o report.md > old.txt && mv report.md old.md
./branch_bench -o report.md > new.txt && mv report.md new.md
diff <(mask old.txt) <(mask new.txt); diff <(mask old.md) <(mask new.md)
```

Here `old/branch_bench` is the previous build. Expected differences are the
build lines (compiler, flags, commit) and any "Cost per miss" line. That line
appears only when the counts allow it, so it can come and go between runs.

## Sanitizers

This is CI's ASan+UBSan build. Sanitizer builds are for correctness only; never
use them for results.

```sh
gcc -std=c99 -O1 -g -fno-omit-frame-pointer -fno-sanitize-recover=all \
    -fno-tree-vectorize -fno-if-conversion -fsanitize=address,undefined \
    -o branch_bench_san branch_bench.c
ASAN_OPTIONS=exitcode=86:detect_leaks=1 UBSAN_OPTIONS=exitcode=86:halt_on_error=1:print_stacktrace=1 \
    .github/scripts/smoke_test.sh ./branch_bench_san
```

Exit code 86 keeps a sanitizer report from passing as an expected "invalid
option" exit (status 1). CI also builds `perf_test.c` this way. For CI's Clang
MSan build, which uses Clang's flags (Clang rejects `-fno-if-conversion`):

```sh
clang -std=c99 -O1 -g -fno-omit-frame-pointer -fno-sanitize-recover=all \
    -fno-vectorize -fno-slp-vectorize -fsanitize=memory -fsanitize-memory-track-origins \
    -o branch_bench_san branch_bench.c
MSAN_OPTIONS=exitcode=86 .github/scripts/smoke_test.sh ./branch_bench_san
```

## Coverage

The SonarQube quality gate needs 80% line coverage. Most of the perf and sysfs
code only runs with real counters, so the coverage runs go through
[`fake_perf.c`](.github/scripts/fake_perf.c). It is an `LD_PRELOAD` shim that fakes
the perf counters, `/proc/cpuinfo`, cpufreq sysfs and the CPU count.
`FAKE_PERF_EVENTS` (a comma-separated list of `branches`, `misses` and `cycles`; all
three by default) selects which counters open.

To reproduce CI's coverage, work in a scratch copy of the repository, because the
`.gcda`/`.gcno` files land next to the binaries:

```sh
tmp=$(mktemp -d)
gcc -std=c99 -O0 -g --coverage -fno-tree-vectorize -fno-if-conversion \
    -DBUILD_FLAGS='"--coverage"' -DBUILD_COMMIT='"local-coverage"' -o branch_bench_cov branch_bench.c
gcc -shared -fPIC -o "$tmp/fake_perf.so" .github/scripts/fake_perf.c -ldl
.github/scripts/smoke_test.sh ./branch_bench_cov                       # real counters if any (none in CI)
export FAKE_SO="$tmp/fake_perf.so" BENCH="$PWD/branch_bench_cov"
printf '#!/bin/sh\nLD_PRELOAD="$FAKE_SO" exec "$BENCH" "$@"\n' > "$tmp/bench_fake"
chmod +x "$tmp/bench_fake"                                            # shim for the benchmark only
.github/scripts/smoke_test.sh "$tmp/bench_fake"                       # all counters faked
for ev in misses branches,cycles; do                                  # some counters
    FAKE_PERF_EVENTS=$ev "$tmp/bench_fake" -t 1 --size 64K --reps 2 -o "$tmp/part.md"
done
gcc -std=c99 -O0 -g --coverage -fno-tree-vectorize -fno-if-conversion \
    -o perf_test_cov .github/scripts/perf_test.c && ./perf_test_cov
python3 -m venv "$tmp/gcovr" && "$tmp/gcovr/bin/pip" install --require-hashes -r .github/requirements.txt &&
"$tmp/gcovr/bin/gcovr" --root . --filter 'branch_bench\.c$' --merge-lines \
    --sonarqube coverage.xml --print-summary --fail-under-line 80
rm -rf "$tmp"
```

`gcov -b branch_bench_cov-branch_bench.gcda` gives a quick summary without gcovr.
It reads only the benchmark's own coverage data, so it leaves out what only
`perf_test_cov` reaches (such as `perf_stop()`'s error paths) and reports lower
coverage than gcovr's merged figure. `--merge-lines` is needed
because `perf_test.c` renames `main()`, and gcovr would otherwise count those lines
once for each function name. The gcovr steps also check an update to the pinned gcovr.

## Keeping the tests in sync

- **New option:** add its valid and invalid forms to `smoke_test.sh`.
- **New test:** add its number to `smoke_test.sh`'s `for t in 1 2 3 4` report
  loop, and change the out-of-range case `expect 1 -t 5` to the next number. Check
  that the selected-tests case (`report2.md`, with its two loops over unselected
  and selected tests) still covers the new test.
- **New kernel:** add it to `check_jcc.sh` in both places: the function-name
  regex and the `want` list of kernels that must exist. Add it to
  `check_kernels.sh` with the right check: a data branch, an indirect call, or
  branchless.
- **New perf event, `/proc` file or sysfs file:** extend `fake_perf.c`, so the coverage
  runs still reach the new code. A new counter also needs an entry in `fake_events[]`.
- **New system header in `branch_bench.c`:** add it to `perf_test.c`'s include list
  as well. `perf_test.c` includes `branch_bench.c` with `main`, `read` and
  `ioctl` renamed, and it includes the system headers first so that glibc's
  fortified `read()` wrapper never sees the renames.
- **New `read()` or `ioctl()` call** for anything other than counters (e.g. sysfs):
  make `perf_test.c`'s stubs pass the call through, because they serve only counter reads.

## On real hardware

- **Counters** need `/proc/sys/kernel/perf_event_paranoid` ≤ 2.
- **Which events opened:** the banner's "Raw PMU events" line names every counter
  that fell back to its ARM raw event. If the line is missing, every open counter
  used its generic event. `strace -e perf_event_open ./branch_bench -t 1 --size 64K --reps 1`
  shows the same thing: `PERF_TYPE_RAW` with config `0x12` means the branches
  counter used the fallback.
- **Other architectures:** cross-compile (e.g. with `mips-linux-gnu-gcc`) and run
  under `qemu-user` to check correctness and output format. Timings under
  emulation mean nothing, so results need real hardware. Read the kernels'
  disassembly on each new architecture as well, because conditional moves such as
  MIPS `movn`/`movz` or RISC-V `czero` can replace the branches.

## CI

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs on pushes to `main`,
on pull requests and on manual dispatch. Each step's purpose is in its comments.
CI checks correctness and codegen only, never timings.

| Job | What it checks |
|---|---|
| gcc (x86-64, arm64) | `-Werror` build (with JCC flags on x86-64), non-perf build, small run, `perf_test.c`, `check_kernels.sh` (it must also reject `branchless_fixture.c`), `check_jcc.sh` on x86-64 |
| gcc-latest (x86-64) | with the newest GCC: `-Werror` build with JCC flags, small run, `check_kernels.sh`, `check_jcc.sh`; a fortified (`-D_FORTIFY_SOURCE=2`) Clang build that must still reach `perf_test.c`'s stubs and the shim |
| clang | `-Werror` build, small run, `check_kernels.sh` as an expected failure (Clang removes the branches in Tests 1–2), `clang --analyze` |
| sanitizers | GCC ASan+UBSan (x86-64, arm64) and Clang MSan, running `smoke_test.sh` and `perf_test.c` |
| sonarqube | coverage build with and without `fake_perf.c`, gcovr (fails below 80%), SonarQube Cloud scan |

- **Compilers:** CI uses Ubuntu 24.04's GCC 13 and Clang 18. gcc-latest uses
  the newest GCC image, pinned by tag and digest in
  [`.github/gcc-latest/Dockerfile`](.github/gcc-latest/Dockerfile). The release workflow
  uses the same image. That Dockerfile is never built: it exists so Dependabot can
  update the image. A pass in CI says nothing about other local compiler versions.
- **Pinned inputs:** actions are pinned to commit SHAs. gcovr and its dependencies
  are pinned by version and hash in `.github/requirements.txt`. That file is generated
  from `requirements.in` by `pip-compile --generate-hashes`, run in `.github/` under
  Python 3.12; the exact command is in both files.
  Once a month, [Dependabot](.github/dependabot.yml) checks all of these and the
  gcc-latest image. It opens a pull request for any release that is at least
  7 days old.
- **Code scanning:** GitHub's CodeQL default setup also scans the code, outside this workflow.

## SonarQube Cloud

The project `juusolatva_branch_bench` is public. The quality gate requires at
least 80% coverage on new code and at most 3% duplicated lines, and it sets
severity limits for new bugs, code smells, vulnerabilities and dependency issues.
The scan waits for the gate, so a failed gate fails the sonarqube job.
No `sonar.projectVersion` is set, so "new code" means everything since the first analysis.

- **Setup:** `SONAR_TOKEN` must be both an Actions secret and a Dependabot secret,
  because Dependabot's pull requests see only Dependabot secrets. Without the token,
  as on pull requests from forks, the scan is skipped with a notice. Automatic
  Analysis must be disabled in SonarQube Cloud.
- **API** (no token needed; add `&pullRequest=<N>` for a pull request):
  - open issues: `https://sonarcloud.io/api/issues/search?componentKeys=juusolatva_branch_bench&resolved=false&ps=100`
  - quality gate: `https://sonarcloud.io/api/qualitygates/project_status?projectKey=juusolatva_branch_bench`
  - per-line coverage: `https://sonarcloud.io/api/sources/lines?key=juusolatva_branch_bench:branch_bench.c&from=1&to=5000` (`lineHits`, `isNew`)
- **Known false positives:** `csecurity:S5145` (logging injection) on the
  `printf`/`fprintf` calls in `report_table()` and `print_row()`. Those calls print
  only numbers and string literals. Code workarounds have not helped, so mark the
  issues False Positive, on the pull request and on `main`. An issue can come back
  as new when its line is edited. If they keep reappearing, ignore the rule for
  `branch_bench.c` through `sonar.issue.ignore.multicriteria` in
  `sonar-project.properties`, with a comment saying why. The taint analysis runs
  only in SonarQube Cloud, so each attempt costs a CI run.
