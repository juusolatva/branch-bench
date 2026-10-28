# AGENTS.md

This file provides guidance and technical context for AI agents working on code in this repository.

## Project Overview

`branch_bench` is a single-file C microbenchmark ([`branch_bench.c`](branch_bench.c)) designed to measure the real CPU performance penalties associated with branch mispredictions across modern processor pipelines.

It runs four self-contained tests comparing predictable versus unpredictable branching patterns:

1. **Test 1 — Threshold Sum**: Sorted vs. shuffled array filtering elements above a threshold (`128`). Measures direct branch prediction behavior.
2. **Test 2 — Stride Conditional**: Periodic 25% take-rate ("every 4th") vs. random 25% take-rate. Evaluates pattern history table learning.
3. **Test 3 — Indirect Dispatch**: Sequential vs. random function pointer dispatch across 32 distinct leaf functions. Evaluates the history-based indirect branch predictor (a plain Branch Target Buffer alone cannot learn the 32-target cycle).
4. **Test 4 — Branch vs. Branchless**: Computes identical threshold sums using conditional jumps vs. arithmetic bitmasks (`(u64)0 - (u64)(arr[i] > THRESHOLD)`), across both sorted and random datasets to isolate the pure misprediction cycle penalty.

On Linux systems supporting `perf_event_open(2)`, the benchmark reads hardware performance counters (`PERF_COUNT_HW_BRANCH_INSTRUCTIONS`, `PERF_COUNT_HW_BRANCH_MISSES` and `PERF_COUNT_HW_CPU_CYCLES`) as one perf group, so all counts cover the same window. Any counter that fails to open is left out of the group; the console omits it and the Markdown report shows "–" for it. From the cycle and miss counts, each test reports a **Cost per miss** (extra cycles ÷ extra misses between its predictable and unpredictable variant; Test 4 uses its two branch variants). It is skipped when either counter is missing, or when the unpredictable variant adds fewer than 1000 misses or no cycles. Counters need `/proc/sys/kernel/perf_event_paranoid` ≤ 2. When hardware counters are unavailable (e.g. non-Linux, locked down VMs/containers), it gracefully falls back to monotonic wall-clock timing (`now_ms()`). The wall-clock fallback uses POSIX `clock_gettime(CLOCK_MONOTONIC)`, so the benchmark needs a POSIX system; it does not build with MSVC.

---

## Build, Run, and Verification

### Canonical Run Command
```sh
./run.sh            # options are passed on, e.g. ./run.sh -n 5 -o report.md
```

The script compiles the code and immediately executes the benchmark with its arguments. It stops if compilation fails (`set -e`), so a stale binary never runs. It builds with:
```sh
gcc -std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -o branch_bench branch_bench.c
```
On x86-64 it adds `-Wa,-mbranches-within-32B-boundaries,-malign-branch=jcc+fused+jmp+call+ret+indirect` (see the JCC erratum under Testing and Validation). It also passes its flags and `git describe --always --dirty` as `-DBUILD_FLAGS="\"...\""` and `-DBUILD_COMMIT="\"...\""`; the banner and report header print them when set, and always print the compiler (`__VERSION__`).

On Windows, build and run inside WSL2. [`.gitattributes`](.gitattributes) keeps the `.sh` scripts LF in every checkout, so they run there.

### Critical Compiler Flags
Both optimization flags are strictly load-bearing:
- `-fno-tree-vectorize`: Prevents the compiler from vectorizing loops with SIMD instructions, which would eliminate branches altogether.
- `-fno-if-conversion`: Prevents the compiler from converting `if`/`else` control flow into conditional move (`cmov`) instructions. Real branch instructions are required for Tests 1–3 and Test 4's branch variant to trigger branch prediction logic.

`-std=c99` enforces the C99 requirement at build time. POSIX and Linux APIs (`clock_gettime`, `localtime_r`, `fdopen`, `syscall`) are exposed by the `#define _DEFAULT_SOURCE` at the top of `branch_bench.c`, which must stay before the first `#include`. Do not replace it with `_POSIX_C_SOURCE`: that hides `syscall()` and breaks the build.

### Compiler Support
GCC is the reference compiler. Clang rejects `-fno-if-conversion` and has no equivalent flag; it builds with `clang -std=c99 -O2 -fno-vectorize -fno-slp-vectorize`, but (as of Clang 18–21) makes Tests 1, 2 and Test 4's branch variant branchless: `cmov` in `sum_threshold`/`sum_branch` and `sbb` arithmetic in `count_decisions`. Clang timings for those tests do not show the misprediction cost. When checking any build's disassembly (`objdump -d`), look for a conditional jump on the data inside the inner loop, not just for the absence of `cmov`.

### CLI Options
- `./branch_bench`: Runs all four benchmarks and outputs results to stdout.
- `./branch_bench -o <file.md>` / `./branch_bench --output <file.md>`: Generates a structured Markdown report matching the format used in [`RESULTS.md`](RESULTS.md) alongside the stdout stream. Its heading is the CPU model with the highest `cpuinfo_max_freq` from cpufreq sysfs (`@ 3.60 GHz`, replacing any base clock in the model name; left as-is where sysfs has no clock information, e.g. WSL2). When the cycle counter works, the console and report end with the **average clock during measurements** (total cycles ÷ total time over all measured trials, warm-ups excluded). The header table records the compiler, followed by the build flags and commit when `run.sh` built the binary. When a counter opened only as its ARM raw event (see TODO), the banner's "Raw PMU events" line and the report's "Raw ARM PMU events" line name it; no such line means every open counter is the generic event. An existing file is overwritten (a new one is created owner-only, mode 0600), so never pass `RESULTS.md`: write a separate file and paste its `## <CPU>` section into RESULTS.md by hand, under `# NEW RESULTS` (bare metal or WSL2) or `# VPS RESULTS` (cloud VMs), not at the end of the file. Current entries use the defaults with `-n 6`.
- `./branch_bench -n <N>` / `./branch_bench --trials <N>`: Measures each variant N times (1–99) and reports the trial with the median time. N > 1 adds one untimed warm-up run and the fastest–slowest range (console, and a Range column in the report). The default (`-n 1`) is a single measurement with no warm-up, and its output format is unchanged.
- `./branch_bench -t <N>` / `./branch_bench --test <N>`: Runs only test N (1–4); repeat the option to run several. The banner and report note which tests ran.
- `./branch_bench --size <N>`: Elements per array for Tests 1/2/4 and calls per trial for Test 3 (default 4M = 4194304, max 1024M; `K`/`M` suffixes, case-insensitive, are powers of 1024).
- `./branch_bench --reps <N>`: Passes over the array per trial for Tests 1/2/4 (default 64, max 100000).
- Non-default sizes are visible in the banner, test headings and report header table. Results are only comparable with RESULTS.md at the defaults (`--size 4M --reps 64`).
- `./branch_bench -h` / `./branch_bench --help`: Displays usage information.

### Testing and Validation
There is no dedicated test framework or linter in this repository. Validate changes by compiling with `gcc` using the required flags (plus `-Wall -Wextra -pedantic` to catch issues) and executing the binary. `clang --analyze -std=c99 -Xclang -analyzer-werror <repo>/branch_bench.c` should report no warnings; run it from a scratch directory, because it writes a `.plist` into the current one. It finds the same kind of path-based bugs (uninitialized values, out-of-bounds reads) that SonarQube Cloud reports. SonarQube's C rules that this code has hit are listed in Code Architecture section 8. `.github/scripts/perf_test.c` tests the perf scaling and error paths without a PMU: `gcc -std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -o /tmp/perf_test .github/scripts/perf_test.c && /tmp/perf_test`. It `#include`s `branch_bench.c` with `main`, `read` and `ioctl` renamed, so every `read()`/`ioctl()` in the file reaches its stubs, which serve only counter reads; a new `read()` or `ioctl()` for anything else (e.g. sysfs) needs the stubs to pass it through. Test 4 includes a built-in runtime sanity check verifying that branch and branchless implementations compute identical sums; on a mismatch it prints an error and the program exits with status 1.

Changes anywhere in the file can move the kernels to new addresses, which can change timings even when their code is untouched. After a change, compare the kernels' disassembly with the previous build, ignoring addresses and padding `nop`s:
```sh
objdump -d --no-show-raw-insn branch_bench | awk '/<(sum_threshold|sum_branch|sum_branchless|count_decisions|dispatch_sequential|dispatch_random)(\.[^>]*)?>:/,/^$/'
```
GCC may add clone suffixes such as `.constprop.0`. If kernel code changed, also compare cycles per element and cost per miss with the previous build (`-n 5`). No jump in a kernel may cross or end on a 32-byte boundary: Intel's JCC-erratum microcode slows such jumps on Skylake-derived CPUs, including the i5-8350U in RESULTS.md, so an unrelated edit could add or remove the penalty for some kernels. `run.sh`'s x86-64 assembler flags pad every jump, `call` and `ret` off those boundaries (`-mbranches-within-32B-boundaries` alone leaves `call`/`ret` out). `.github/scripts/check_jcc.sh ./branch_bench` lists every such jump in the kernels (a macro-fused `cmp`/`test` + `jcc` pair counts as one jump) and exits 2 if a kernel is missing, e.g. after a rename; a build with the flags must list none, and CI's x86-64 gcc job fails on any. Builds without the flags (CI's other jobs, Clang) are not checked. Padding adds only `nop`s and redundant `cs` prefixes, so the kernels' instructions stay the same.

### Continuous Integration
[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs on pushes to `main`, on pull requests and on manual dispatch. Hosted runners are VMs, usually without hardware perf counters, so CI checks correctness and codegen only, not timings. They use Ubuntu 24.04's GCC 13 and Clang 18; the gcc-latest job adds the newest GCC release, but a codegen pass in CI still says nothing about another local compiler version. GitHub's CodeQL default setup also scans the code, outside this workflow. Actions are pinned to commit SHAs, and gcovr and all its dependencies to versions and hashes in [`.github/requirements.txt`](.github/requirements.txt), which `pip-compile --generate-hashes` generates from [`.github/requirements.in`](.github/requirements.in) (run it in `.github/` under Python 3.12, CI's version; the exact command is in both files). [`.github/dependabot.yml`](.github/dependabot.yml) opens a monthly pull request when any of them, or the gcc-latest image, has a release at least 7 days old (`cooldown`).
- **gcc** (x86-64 and arm64): builds with the required flags plus `-Wall -Wextra -pedantic -Werror` (and `run.sh`'s JCC alignment flags on x86-64, where it then fails if `check_jcc.sh` lists any hit), compiles the non-perf path (`-U__linux__`), runs `--size 64K --reps 2`, and runs [`.github/scripts/check_kernels.sh`](.github/scripts/check_kernels.sh). It fails if a Test 1–3 kernel (Test 1's is also Test 4's branch variant) contains a conditional move/select (`cmov`; `csel`/`cset` family on arm64), flag arithmetic (`setcc`/`sbb`/`adc`; `adc`/`sbc`/`ngc` on arm64) or SIMD registers, or lacks its data branch (Tests 1–2: a forward conditional branch that starts and lands inside a loop, i.e. between a backward branch and its target, so the loop back-edge and early exits do not count) or indirect call (Test 3). A pass is still necessary, not sufficient: the branch inside the loop need not be the one on the data. The job also builds [`.github/scripts/branchless_fixture.c`](.github/scripts/branchless_fixture.c), a kernel made branchless without any blacklisted instruction, and requires `check_kernels.sh` to reject it with the data-branch message, which proves that check still fails. Still read the inner loop's disassembly after codegen-relevant changes. x86-64 and aarch64 only. Run it locally with `.github/scripts/check_kernels.sh ./branch_bench`. The job also runs [`.github/scripts/perf_test.c`](.github/scripts/perf_test.c), which includes `branch_bench.c` with `read()`/`ioctl()` replaced by stubs and checks the perf scaling and error paths without a PMU.
- **gcc-latest**: runs the gcc job's build (with the JCC alignment flags), small run, `check_kernels.sh` and JCC check in the newest GCC image, pinned by tag and digest in [`.github/gcc-latest/Dockerfile`](.github/gcc-latest/Dockerfile) (the release workflow uses the same image). The Dockerfile is never built; it exists because Dependabot cannot update an image named in a workflow file. x86-64 only.
- **clang**: builds with the Clang flags and `-Werror`, runs the small size, runs `check_kernels.sh` as an expected failure (the step fails unless it reports FAIL for both Test 1/4 and Test 2, which proves the check still detects lost branches), and runs `clang --analyze` with `-analyzer-werror`.
- **sanitizers**: GCC ASan+UBSan (x86-64 and arm64) and Clang MSan, each running [`.github/scripts/smoke_test.sh`](.github/scripts/smoke_test.sh), which exercises every option, the report file and invalid input, and `perf_test.c`. Sanitizer builds are for correctness only; never use them for results.
- **sonarqube**: sets `kernel.perf_event_paranoid=1`, so real counters are used if the VM ever has a PMU. A `--coverage` build under SonarQube's Build Wrapper (with `-DBUILD_FLAGS`/`-DBUILD_COMMIT`, so the banner and report lines that print them count as covered) runs `smoke_test.sh`, once as is and once with the [`.github/scripts/fake_perf.c`](.github/scripts/fake_perf.c) `LD_PRELOAD` shim, which fakes the perf counters (`FAKE_PERF_EVENTS`, a comma list of `branches`, `misses` and `cycles`, selects which ones open; default all three), `/proc/cpuinfo` and cpufreq sysfs so the code paths that need them are covered too; the step greps the report for the cost-per-miss and average-clock lines, which fail it if the shim was not loaded. Two more shim runs open only some counters (`misses`, then `branches,cycles`), and a `--coverage` build of `perf_test.c` covers `perf_stop()`'s error paths. `gcovr` converts the coverage (with `--merge-lines`, because `perf_test.c` renames `main()` and gcovr would otherwise count those lines once per function name) and fails the job below 80% line coverage, which also catches an empty report (gcovr itself exits 0 when its filter matches nothing). The scan uploads both (settings in [`sonar-project.properties`](sonar-project.properties)). Needs `SONAR_TOKEN` as both an Actions secret and a Dependabot secret, because Dependabot's pull requests see only Dependabot secrets (the token is passed only to the scan step; without it, e.g. on pull requests from forks, the scan is skipped with a notice), and Automatic Analysis disabled in SonarQube Cloud.

[`.github/workflows/release.yml`](.github/workflows/release.yml) runs when a `v*` tag is pushed. On x86-64 and arm64 it builds a static binary (`-static` plus run.sh's flags, `BUILD_COMMIT` = the tag) in the gcc-latest image, then checks it on the runner: no `INTERP` header, the banner's Commit line, `smoke_test.sh`, `check_kernels.sh` and, on x86-64, `check_jcc.sh`. A second job publishes the binaries (`branch_bench-<tag>-linux-<arch>`), `SHA256SUMS` and build provenance attestations as a GitHub release with generated notes. Pull requests that change the workflow or the Dockerfile run only the build and checks, because the gcc-latest CI job covers neither arm64 nor static linking. Don't strip the binaries: both check scripts need the kernel symbols. Release with `git tag -a vX.Y.Z -m vX.Y.Z && git push origin vX.Y.Z` on a commit on `main` whose CI passed; the build fails if `git describe --tags` at that commit gives anything but the tag (e.g. it can pick another tag on the same commit).

The SonarQube Cloud quality gate requires at least 80% coverage on new code, plus at most 3% duplicated lines and severity limits for new bugs, code smells, vulnerabilities and SCA (dependency) issues. The scan waits for the gate (`sonar.qualitygate.wait=true` in `sonar-project.properties`), so a failed gate fails the sonarqube job. No `sonar.projectVersion` is set, so the new-code period never advances and "new code" covers everything since the first analysis. Paths reachable only with perf counters or sysfs are covered through the shim.

SonarQube's C taint analysis reports `csecurity:S5145` (logging injection, CWE-117) on the `printf`/`fprintf` calls in `report_table()` and `print_row()` that print `Result` fields. These are false positives: those calls print only numbers (`%.1f`, `PRIu64`) and string literals, which cannot inject text. The reported taint source is unreliable (it has pointed at the volatile store in `anti_dce_sink()` and at `Result r_sb = {0};`), and code workarounds did not help (`noinline` on the test functions, copying fields into locals, a write-only sink), so do not change code to silence them. Mark them False Positive in SonarQube Cloud, on the pull request and on `main`; an issue can come back as new when its line is edited. The taint analysis runs only in SonarQube Cloud (the local `sonar` CLI's deep analysis returns 403 Forbidden for this project), so each attempt costs a CI run. If the issues keep reappearing, ignore the rule for `branch_bench.c` via `sonar.issue.ignore.multicriteria` in `sonar-project.properties`, with a comment explaining why.

The SonarQube Cloud project `juusolatva_branch_bench` is public, so its API needs no token: open issues `curl -s "https://sonarcloud.io/api/issues/search?componentKeys=juusolatva_branch_bench&resolved=false&ps=100"`, quality gate `https://sonarcloud.io/api/qualitygates/project_status?projectKey=juusolatva_branch_bench`, per-line coverage `https://sonarcloud.io/api/sources/lines?key=juusolatva_branch_bench:branch_bench.c&from=1&to=5000` (`lineHits`, `isNew`). Add `&pullRequest=<N>` for a pull request.

To reproduce CI's coverage locally (from a scratch copy of the repo, because `.gcda`/`.gcno` files land next to the binaries):
```sh
gcc -std=c99 -O0 -g --coverage -fno-tree-vectorize -fno-if-conversion -o branch_bench_cov branch_bench.c
gcc -shared -fPIC -o fake_perf.so .github/scripts/fake_perf.c -ldl
.github/scripts/smoke_test.sh ./branch_bench_cov                                 # no counters in a VM (CI)
LD_PRELOAD=$PWD/fake_perf.so .github/scripts/smoke_test.sh ./branch_bench_cov   # all counters faked
for ev in misses branches,cycles; do                                            # some counters
    FAKE_PERF_EVENTS=$ev LD_PRELOAD=$PWD/fake_perf.so ./branch_bench_cov -t 1 --size 64K --reps 2 -o /tmp/part.md
done
gcc -std=c99 -O0 -g --coverage -fno-tree-vectorize -fno-if-conversion -o perf_test_cov .github/scripts/perf_test.c && ./perf_test_cov
gcov -b branch_bench_cov-branch_bench.gcda   # summary; per-line counts in branch_bench.c.gcov
python3 -m venv /tmp/gcovr && /tmp/gcovr/bin/pip install --require-hashes -r .github/requirements.txt
/tmp/gcovr/bin/gcovr --root . --filter 'branch_bench\.c$' --merge-lines --sonarqube coverage.xml --print-summary --fail-under-line 80
```
The last two lines install CI's pinned gcovr and write the `coverage.xml` that CI uploads; use them to check a gcovr update. gcovr's line count can differ from `gcov -b`'s by a line or two.

When adding an option, add its valid and invalid forms to `smoke_test.sh`. When adding a kernel that must keep a branch, add it to `check_kernels.sh`; add every new kernel to `check_jcc.sh`'s function list. When code starts using a new perf event, `/proc` or sysfs file (e.g. new `/proc/cpuinfo` keys), extend `fake_perf.c` so the coverage job still reaches it.

---

## Code Architecture and Implementation Conventions

### 1. Function Inlining Prevention
All benchmark kernels (such as `sum_threshold`, `count_decisions`, `dispatch_sequential`, `dispatch_random`, `sum_branch`, and `sum_branchless`) must be annotated with `__attribute__((noinline))`. This keeps the compiler from inlining hot loops into their callers. It does not stop other interprocedural optimizations, such as identical code folding (see section 5) or `.constprop` clones, so check the disassembly.

### 2. Dead Code Elimination (DCE) Prevention
Because benchmark loops compute values without external side effects during timing, the compiler could eliminate the loop entirely.
- Always retain or consume the computed output.
- Use `anti_dce_sink(u64 v)` (in [`branch_bench.c`](branch_bench.c)) on computed results. It stores the value into a file-scope `volatile u64` that is never read back; the compiler cannot elide a volatile store, so the computation stays.
- If a test performs an observable validation (such as Test 4's result comparison check), an additional `anti_dce_sink` call may be omitted.

### 3. Separation of Setup vs. Timed Execution
- Never allocate memory, perform I/O, or invoke the random number generator inside the timed measurement loop.
- Array generation, shuffling, and table initialization (e.g. `decisions_random`, `dispatch_indices`) must occur *before* starting the timer.
- Free all allocated resources once the benchmark passes complete.
- Read the `--size` global once at the start of each test (`const size_t len = size;`) and use `len` for the allocation, setup loops and kernel calls. Reading the global repeatedly makes static analyzers (SonarQube, `clang --analyze`) report out-of-bounds accesses, because they assume it may change between the `malloc` and the kernel call.

### 4. Deterministic and Low-Overhead PRNG
Use the local xorshift64 PRNG (`rng64()` / `rng_state`) instead of `stdlib rand()`. This avoids glibc locking overhead and distribution skew.

### 5. Indirect Dispatch and Identical Code Folding (ICF)
Test 3 generates 32 leaf functions via X-macros (`FOR_EACH_LEAF` / `DEF_LEAF`). Each leaf XORs its argument with its own constant, the leaf number times the Weyl sequence step `0x9e3779b97f4a7c15ULL`. This prevents Identical Code Folding (GCC's `-fipa-icf`, on at `-O2`, or the linker's) from merging the function pointers into a single destination address.

GCC *does* fold `sum_branch` into `sum_threshold`, because their bodies are identical, so the disassembly shows a single function under one of the two names. That is harmless: Test 1 and Test 4's branch variants are meant to run the same code.

### 6. Perf Timing Window Convention
- Measure every kernel call with `MEASURE(result, kernel(args))`. It runs the `-n/--trials` loop (with a warm-up only when trials > 1) and keeps the median trial via `median_trial()`. Each trial goes through `MEASURE_ONCE`, which calls `perf_start()` immediately *before* the `now_ms()` timer starts and `perf_stop()` immediately *after* it stops.
- The reported row is one real trial (the median by time, the lower middle one for even counts), never a per-field median, so time, cycles, branches and misses always belong together.
- Do **not** nest `perf_start()` or `perf_stop()` inside the `now_ms()` timing window; doing so introduces system call overhead into the wall-clock measurements.
- `MEASURE` is a macro, not a function, on purpose: the kernel must be called directly (not through a function pointer) so its codegen stays unchanged.
- Guard access to each counter slot in `r.counts[]` with `perf_has(CTR_BRANCHES)`, `perf_has(CTR_MISSES)` or `perf_has(CTR_CYCLES)`; any of them can be missing independently (`print_row()`, `report_table()` and `print_miss_cost()` already do).
- New counters go into the same group. Each needs a row in the `FOR_EACH_COUNTER` list (slot name, generic event as the `PERF_COUNT_HW_` suffix, ARM raw fallback, Markdown header, console label and width), which generates both its `CTR_*` slot and its `counter_descs[]` entry; its position in `counter_display_order[]` (`CHECK_LEN` fails the build when that list's length differs from `CTR_COUNT`); and an entry in `fake_perf.c`'s `fake_events[]` table (and `FAKE_PERF_EVENTS`). `perf_test.c` sizes its fake reads by `CTR_COUNT` and opens every counter, so it covers the new one unchanged; run it.
- Which counters are open is tracked in `perf_open_mask`, not by `perf_fd[]` values, so the zero-initialized state means "none open"; test it only through `perf_has()`.

### 7. Formatted Report Emission
Each test function receives `FILE *report` (which will be `NULL` if `-o` was not specified). Any new test should:
- describe its work per trial once, as `const Work work = { count, "elem" }` (or `"call"`), which drives the misses-per-item column;
- construct a `ReportRow` array (`{ label, result }`);
- for standard 2-row (predictable vs. unpredictable) tests, call `emit_pair_summary(report, title, desc, rows, &work)`, which emits the console rows, the Markdown table, the slowdown ratio and the cost-per-miss line;
- for multi-row tests (like Test 4), call `emit_rows(report, title, desc, rows, count, &work)`, `emit_ratio(report, console_prefix, md_label, num_ms, denom_ms, suffix)` for each comparison ratio, and `print_miss_cost(report, &predictable, &unpredictable)`, then `printf("\n")` to end the test's console block (`emit_pair_summary` does this itself).

Call these after all `MEASURE` calls. The console heading stays in `run_testN` before them, so progress shows while the kernels run.

This keeps console and Markdown output consistent from a single `ReportRow` definition. Labels in `ReportRow` may carry extra internal spaces for console alignment; `report_table()` automatically collapses multi-space runs when writing Markdown.

To wire up a new test, bump `NUM_TESTS`, add `if (test_selected(N)) run_testN(report);` in `main()`, add N to `smoke_test.sh`'s `for t in 1 2 3 4` report loop and change its `expect 1 -t 5` invalid case to the new out-of-range number, add its kernel to `check_kernels.sh` if it must keep a branch, and add it to `check_jcc.sh`'s function list.

### 8. Output Stability, CLI and SonarQube Rules
- The default run (no options) keeps its console and report format; an opt-in option (`-n`, `-t`, ...) may only add output when it is used. Compare the default output with the previous build, numbers masked.
- Options are parsed one at a time in `apply_option()`; numeric values go through `option_number()`/`parse_count()` (range check, `K`/`M` suffixes). Keep functions small: SonarQube flags cognitive complexity above 25.
- Format `u64` with `PRIu64` and `size_t` with `%zu`.
- Integer literal suffixes `L`/`UL`/`ULL` are uppercase (SonarQube S818 flags a lowercase `l`); lowercase `u` is accepted.
- Write every integer-to-`double` conversion as an explicit `(double)` cast, also in mixed arithmetic such as `100.0 * (double)a / (double)b` (SonarQube S5276 flags implicit ones; the code is the same).
- Every file-scope symbol is `static`.

---

## Git and Contribution Guidelines
- Committing changes is allowed; prefer opening pull requests on a branch over pushing directly to `main`.
- Keep the codebase self-contained within [`branch_bench.c`](branch_bench.c).
- Keep this file and README.md in sync with behaviour changes. The Serena memories in `.serena/memories/` only point here and hold Serena-specific tool notes; don't copy project rules into them.
- RESULTS.md's `# OLD RESULTS` sections are historical (old Test 3 labels and table layout). The i5-3380M hardware is gone, and its section is the only Ivy Bridge data for the Test 3 comparison with Haswell. Don't regenerate or reformat them. Suffixes such as `(WSL2)` in headings are added by hand.
- Maintain C99 compatibility (C99 language plus POSIX/Linux APIs, built with GCC) without unnecessary external dependencies. GNU extensions already in use, such as `__attribute__((noinline))`, are accepted.

---

## TODO

### Measurement quality
- **Verify the ARM raw-event fallback on hardware:** `perf_add()` falls back to raw events `0x12` (`BR_PRED`), `0x10` (`BR_MIS_PRED`) and `0x11` (`CPU_CYCLES`) when the generic events fail. Misses previously used `0x21` (`BR_RETIRED`, which counts all retired branches); the fix has been built and run on a Raspberry Pi 4 and is compiled on CI's arm64 runner (which has no PMU, so the fallback is tried there but never succeeds). Still open: confirm which path ran on the Pi, and test on a Cudy WR3000S (Cortex-A53, OpenWrt). On OpenWrt, check that the kernel has `CONFIG_PERF_EVENTS` enabled, and expect "Unknown CPU" because arm64 `/proc/cpuinfo` has no model line there.
  - **Raspberry Pi 4 (Cortex-A72):** run done, path not yet confirmed; RESULTS.md has a run with all three counters. Misses look correct (0.500 per element for Test 1 shuffled), but the unpredictable variants count more branches than the predictable ones (646M vs 537M in Test 1), which suggests the speculative `BR_PRED` raw event was used, i.e. the generic branch event failed and the fallback ran. Confirm which path ran by rerunning `./run.sh -t 1 --size 64K --reps 1`: the banner lists any counter that fell back to its raw event (`perf_raw_mask`). `strace -e perf_event_open` shows the same (`PERF_TYPE_RAW` with config `0x12` means the fallback).
  - **Cudy WR3000S (Cortex-A53):** not run yet.

---

## Long-Term Goals

### Portability to more architectures (MIPS, RISC-V, PowerPC, ...)
The benchmark is developed and tested on x86-64 and ARM (Cortex-A72) Linux. Supporting other architectures is a long-term goal; known gaps:
- **CPU model detection:** `get_cpu_model()` only recognizes the x86 `model name` and ARM `Model`/`Hardware` keys in `/proc/cpuinfo`. MIPS uses `cpu model` (plus `system type`), PowerPC uses `cpu`, and RISC-V often has only `isa`/`uarch`, so these currently report "Unknown CPU".
- **Perf counters:** generic `PERF_COUNT_HW_*` events (branches, misses, cycles) are tried first everywhere, but raw-event fallbacks exist only for ARM. Many embedded MIPS cores have no PMU or no kernel PMU driver, so these fall back to wall-clock timing only.
- **Branch codegen:** `-fno-if-conversion` must still leave real branches in Tests 1–3 and Test 4's branch variant. Check the disassembly on each new architecture: MIPS has conditional moves (`movn`/`movz`, `seleqz`/`selnez` on R6) and RISC-V has `czero` (Zicond).
- **32-bit targets:** the `u64` accumulators and Test 3 leaf arithmetic become multi-instruction sequences on 32-bit cores (e.g. MIPS32), which changes the per-iteration work and makes timings incomparable with 64-bit results.
- **Memory footprint:** by default the tests allocate two 4 MiB buffers at a time; on small embedded boards use `--size` (and `--reps` to keep run times sensible).
- **Interpretation:** README's interpretation section is written for deep out-of-order cores. In-order cores (common on MIPS) have much shorter misprediction penalties.
- **Validation:** cross-compile (e.g. `mips-linux-gnu-gcc`) and run under `qemu-user` to check correctness and output format. Timings under emulation are meaningless; real hardware is required for results.
