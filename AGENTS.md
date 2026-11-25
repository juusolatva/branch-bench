# AGENTS.md

Instructions for AI agents working in this repository. [`CLAUDE.md`](CLAUDE.md)
imports this file. Usage is described in [`README.md`](README.md), and checks
and CI in [`TESTING.md`](TESTING.md).

## Project

[`branch_bench.c`](branch_bench.c) is a single-file C99 microbenchmark that
measures the cost of branch mispredictions. Each test compares a predictable
variant with an unpredictable one:

1. **Threshold Sum** (`sum_threshold`): sums bytes above 128 in a sorted and
   a shuffled array.
2. **Stride Conditional** (`count_decisions`): a one at every 4th position
   vs. a random 25% of positions.
3. **Indirect Dispatch** (`dispatch_sequential`, `dispatch_random`): calls 32
   leaf functions through pointers, in `i % 32` order or at random.
4. **Branch vs Branchless** (`sum_branch`, `sum_branchless`): Test 1's sum on
   sorted and random data. It checks that both variants give the same sum, and
   exits with status 1 if they do not.

On Linux, `perf_event_open(2)` reads branches, branch misses and cycles as a
single group. A counter that fails to open is left out, and without counters
only wall-clock times (`clock_gettime(CLOCK_MONOTONIC)`) are shown. The
benchmark needs POSIX and does not build with MSVC.

## Building

`./run.sh [options]` builds the benchmark and runs it with the given options.
It stops if the build fails. It builds with:

```sh
gcc -std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -o branch_bench branch_bench.c
```

On x86-64 it also adds
`-Wa,-mbranches-within-32B-boundaries,-malign-branch=jcc+fused+jmp+call+ret+indirect`.

### Compiler flags

- `-fno-tree-vectorize` and `-fno-if-conversion` are load-bearing. Without
  them GCC vectorizes the loops or turns the branches into `cmov`, and the
  mispredictions the benchmark measures disappear.
- The x86-64 assembler flags keep every jump, `call` and `ret` off 32-byte
  boundaries (the JCC erratum; see [TESTING.md](TESTING.md#jcc-erratum)).
- `#define _DEFAULT_SOURCE` must stay before the first `#include`. Do not
  replace it with `_POSIX_C_SOURCE`: that hides `syscall()` and breaks the build.
- `run.sh` passes its flags and `git describe --always --dirty` to the compiler
  as `-DBUILD_FLAGS` and `-DBUILD_COMMIT`, which become C string literals. The
  banner and report print them, together with the compiler (`__VERSION__`).
  Never pass unchecked text in `-DBUILD_*`. A tag name can contain `"`, so when
  the output has any character outside `[A-Za-z0-9._+-]`, `run.sh` uses the bare
  commit hash instead.

### Compiler support

GCC is the reference compiler. Clang rejects `-fno-if-conversion` and has no
equivalent. It builds with `clang -std=c99 -O2 -fno-vectorize -fno-slp-vectorize`,
but Clang 18–21 make Tests 1 and 2 and Test 4's branch variant branchless (`cmov`,
`sbb`), so Clang timings show no misprediction cost. On Windows, build in WSL2;
[`.gitattributes`](.gitattributes) keeps the `.sh` scripts LF. macOS needs an
installed GNU GCC (`gcc-<N>`) and no `-Wa,...` flags.

## Code conventions

### 1. No inlining of kernels

Mark every benchmark kernel `__attribute__((noinline))`. That attribute does
not stop identical code folding or `.constprop` clones, so check the
disassembly ([TESTING.md](TESTING.md#kernel-codegen)).

### 2. Dead code elimination

Pass computed results to `anti_dce_sink(u64)`, which stores them in a `volatile`
global. A test with an observable check, such as Test 4's sum comparison, may skip it.

### 3. Setup outside the timed loop

- No allocation, I/O or random number generation inside a measurement. Build
  the arrays and tables (such as `decisions_random` and `dispatch_indices`) first.
- Free everything you allocate once the passes are done.
- Read the `--size` global once at the start of each test
  (`const size_t len = size;`), and use `len` for the allocation, the setup
  loops and the kernel calls. If the global is read again, static analyzers
  report out-of-bounds accesses.

### 4. PRNG

Use the xorshift64 `rng64()` / `rng_state`, never `rand()`.

### 5. Test 3 leaves and identical code folding

The 32 leaf functions come from X-macros (`FOR_EACH_LEAF` / `DEF_LEAF`). Each
leaf XORs its argument with its own constant (the leaf number ×
`0x9e3779b97f4a7c15ULL`), so GCC's `-fipa-icf` and the linker cannot merge
them. GCC does fold `sum_branch` into `sum_threshold`. That is intended:
Test 1 and Test 4's branch variant run the same code.

### 6. Measurement and perf counters

- Measure every kernel call with `MEASURE(result, kernel(args))`. It runs the
  `-n` trials (with a warm-up only when trials > 1) and keeps the median trial by
  time (the lower middle one for an even count). The reported row is one real trial,
  never a median of each field taken separately.
- `MEASURE` is a macro so that the kernel is called directly, not through a
  function pointer, which keeps its codegen unchanged.
- `MEASURE_ONCE` calls `perf_start()` just before the `now_ms()` timer starts
  and `perf_stop()` just after it stops. Never move perf calls inside the timing window.
- Before using a counter value, check `result_has(&r, CTR_MISSES)` (or
  `CTR_BRANCHES`, `CTR_CYCLES`). It checks both that the counter opened and that this
  trial's read succeeded (`counts_valid`). `perf_has()` checks only whether the
  counter opened. Track open counters in `perf_open_mask`, never by `perf_fd[]` values.
- When counters are time-shared, scale them to estimates. Copy unscaled counts
  exactly, never through a `double`. A failed read, a failed control call or a
  group that never ran leaves `counts_valid` false. That trial keeps its time
  but is left out of cost per miss and average clock.
- To add a counter, give it a row in `FOR_EACH_COUNTER`. The row lists the slot,
  the `PERF_COUNT_HW_` suffix, the ARM raw fallback, the Markdown header, the
  console label and the width. Also add the counter to `counter_display_order[]`
  (`CHECK_LEN` fails the build if the length is wrong) and to `fake_perf.c`'s
  `fake_events[]`, then run `perf_test.c`.

### 7. Report output

Each test function gets `FILE *report` (`NULL` without `-o`). After all of its
`MEASURE` calls:

- describe the work per trial as `const Work work = { count, "elem" }` (or `"call"`);
- build a `ReportRow` array (`{ label, result }`);
- for a two-row test, call `emit_pair_summary(report, title, desc, rows, &work)`,
  which prints the rows, the table, the slowdown and the cost per miss;
- for more rows (like Test 4), call `emit_rows(...)`, then `emit_ratio(...)` for
  each ratio and `print_miss_cost(report, &predictable, &unpredictable)`, then
  `printf("\n")`.

Keep the console heading in `run_testN` before the measurements, so progress
shows while the kernels run. Labels may contain extra spaces for console
alignment; `report_table()` collapses them in the Markdown output.

To add a test, bump `NUM_TESTS`, add `if (test_selected(N)) run_testN(report);` to
`main()`, and update the test scripts as
[TESTING.md](TESTING.md#keeping-the-tests-in-sync) describes.

### 8. Output, CLI and SonarQube rules

- A run with no options keeps its console and report format. An option such as
  `-n` or `-t` may add output only when it is used.
- `apply_option()` parses one option at a time. Numeric values go through
  `option_number()`/`parse_count()`, which handle the range check and the
  `K`/`M` suffixes.
- Keep functions small: SonarQube flags cognitive complexity above 25.
- Format `u64` with `PRIu64` and `size_t` with `%zu`.
- Write the integer literal suffixes `L`/`UL`/`ULL` in uppercase (S818).
- Write every integer-to-`double` conversion as an explicit `(double)` cast,
  including in mixed arithmetic (S5276).
- File-scope helpers and globals are `static`; only `main()` has external linkage.
- Don't change code to silence the known `csecurity:S5145` false positives (see
  [TESTING.md](TESTING.md#sonarqube-cloud)).

## Before committing

Run the [quick check](TESTING.md#quick-check). If the change could affect the
kernels' code or addresses, compare their disassembly, and compare the default
output format with the previous build's ([TESTING.md](TESTING.md#after-changing-code)).
Keep README.md, TESTING.md and this file in step with behaviour changes.

## Results

RESULTS.md is edited by hand. Never run the benchmark with `-o RESULTS.md`: an
existing file is overwritten. README's "Adding results" section describes
the process. Don't regenerate or reformat the `# OLD RESULTS` sections. The
i5-3380M hardware is gone, and its section is the only Ivy Bridge data for the
Test 3 comparison with Haswell.

## Releases

Tag a commit on `main` whose CI passed:
`git tag -s vX.Y.Z -m vX.Y.Z && git push origin vX.Y.Z`.
[`release.yml`](.github/workflows/release.yml) then builds static x86-64 and
arm64 binaries in the gcc-latest image and runs the test scripts on them. It
publishes the binaries with `.sha256` files and build provenance attestations.

- The tag must be signed with a key on the tagger's GitHub account, because the
  workflow requires `verification.reason` to be `valid`.
- `git describe --tags` at the commit must give exactly the tag, and the tag
  name may contain only `[A-Za-z0-9._+-]`.
- A repository ruleset that blocks updating and deleting `v*` tags keeps a tag
  from being moved between verification and publication.
- Don't strip the binaries: `check_kernels.sh` and `check_jcc.sh` need the
  kernel symbols.

## Git

- Committing is allowed. Prefer a pull request from a branch to pushing to `main`.
- Keep the benchmark self-contained in `branch_bench.c`: C99 plus POSIX/Linux
  APIs, built with GCC, with no external dependencies. GNU extensions already in
  use, such as `__attribute__((noinline))`, are fine.

## TODO

**Verify the ARM raw-event fallback on hardware.** When a generic event fails,
`perf_add()` retries it as a raw event: `0x12` (`BR_PRED`), `0x10`
(`BR_MIS_PRED`) or `0x11` (`CPU_CYCLES`).

- **Raspberry Pi 4 (Cortex-A72):** a run with all three counters is in
  RESULTS.md. In Test 1, the unpredictable variant counts more branches than the
  predictable one (646M vs 537M). That suggests the speculative `BR_PRED` fallback
  was used. Confirm it with `./run.sh -t 1 --size 64K --reps 1` and look at the
  banner's "Raw PMU events" line.
- **Cudy WR3000S (Cortex-A53, OpenWrt):** not run yet. Check that the kernel has
  `CONFIG_PERF_EVENTS`. Expect "Unknown CPU", because arm64 `/proc/cpuinfo` has
  no model line there.

## Long-term: other architectures

The benchmark is tested on x86-64 and ARM Linux. These are the known gaps for
MIPS, RISC-V, PowerPC and others:

- **CPU model:** `get_cpu_model()` reads only the `Model`, `model name` and
  `Hardware` keys of `/proc/cpuinfo`. MIPS uses `cpu model`, PowerPC uses `cpu`,
  and RISC-V often has only `isa`/`uarch`.
- **Counters:** raw-event fallbacks exist only for ARM. Many MIPS cores have
  no PMU driver, so they get wall-clock timing only.
- **Codegen:** check that the branches survive. MIPS has `movn`/`movz` (and
  `seleqz`/`selnez` on R6), and RISC-V has `czero` (Zicond).
- **32-bit cores:** `u64` arithmetic takes several instructions there, so
  timings can't be compared with 64-bit results.
- **Interpretation:** README assumes deep out-of-order cores. In-order cores
  have much shorter misprediction penalties.
