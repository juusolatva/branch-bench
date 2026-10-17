# AGENTS.md

This file provides guidance and technical context for AI agents working on code in this repository.

## Project Overview

`branch_bench` is a single-file C microbenchmark ([`branch_bench.c`](branch_bench.c)) designed to measure the real CPU performance penalties associated with branch mispredictions across modern processor pipelines.

It runs four self-contained tests comparing predictable versus unpredictable branching patterns:

1. **Test 1 — Threshold Sum**: Sorted vs. shuffled array filtering elements above a threshold (`128`). Measures direct branch prediction behavior.
2. **Test 2 — Stride Conditional**: Periodic 25% take-rate ("every 4th") vs. random 25% take-rate. Evaluates pattern history table learning.
3. **Test 3 — Indirect Dispatch**: Sequential vs. random function pointer dispatch across 32 distinct leaf functions. Evaluates the history-based indirect branch predictor (a plain Branch Target Buffer alone cannot learn the 32-target cycle).
4. **Test 4 — Branch vs. Branchless**: Computes identical threshold sums using conditional jumps vs. arithmetic bitmasks (`(u64)0 - (u64)(arr[i] > THRESHOLD)`), across both sorted and random datasets to isolate the pure misprediction cycle penalty.

On Linux systems supporting `perf_event_open(2)`, the benchmark reads hardware performance counters (`PERF_COUNT_HW_BRANCH_INSTRUCTIONS`, `PERF_COUNT_HW_BRANCH_MISSES` and `PERF_COUNT_HW_CPU_CYCLES`) as one perf group, so all counts cover the same window. Any counter that fails to open is left out of the group, and the output shows "–" for it. From the cycle and miss counts, each test reports a **Cost per miss** (extra cycles ÷ extra misses between its predictable and unpredictable variant). When hardware counters are unavailable (e.g. non-Linux, locked down VMs/containers), it gracefully falls back to monotonic wall-clock timing (`now_ms()`). The wall-clock fallback uses POSIX `clock_gettime(CLOCK_MONOTONIC)`, so the benchmark needs a POSIX system; it does not build with MSVC.

---

## Build, Run, and Verification

### Canonical Run Command
```sh
./run.sh
```

The script compiles the code and immediately executes the benchmark:
```sh
gcc -std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -o branch_bench branch_bench.c
./branch_bench
```

### Critical Compiler Flags
Both optimization flags are strictly load-bearing:
- `-fno-tree-vectorize`: Prevents the compiler from vectorizing loops with SIMD instructions, which would eliminate branches altogether.
- `-fno-if-conversion`: Prevents the compiler from converting `if`/`else` control flow into conditional move (`cmov`) instructions. Real branch instructions are required for Tests 1–3 to trigger branch prediction logic.

`-std=c99` enforces the C99 requirement at build time. POSIX and Linux APIs (`clock_gettime`, `syscall`, `localtime_r`) are exposed by the `#define _DEFAULT_SOURCE` at the top of `branch_bench.c`, which must stay before the first `#include`. Do not replace it with `_POSIX_C_SOURCE`: that hides `syscall()` and breaks the build.

### Compiler Support
GCC is the reference compiler. Clang rejects `-fno-if-conversion` and has no equivalent flag; it builds with `clang -std=c99 -O2 -fno-vectorize -fno-slp-vectorize`, but may still emit `cmov` for Tests 1–3, so verify the disassembly (`objdump -d`) before trusting Clang results.

### CLI Options
- `./branch_bench`: Runs all four benchmarks and outputs results to stdout.
- `./branch_bench -o <file.md>` / `./branch_bench --output <file.md>`: Generates a structured Markdown report matching the format used in [`RESULTS.md`](RESULTS.md) alongside the stdout stream. Its heading is the CPU model with the highest `cpuinfo_max_freq` from cpufreq sysfs (`@ 3.60 GHz`, replacing any base clock in the model name; left as-is where sysfs has no clock information, e.g. WSL2). When the cycle counter works, the console and report end with the **average clock during measurements** (total cycles ÷ total time over all measured trials, warm-ups excluded).
- `./branch_bench -n <N>` / `./branch_bench --trials <N>`: Measures each variant N times (1–99) after one untimed warm-up run and reports the trial with the median time, plus the fastest–slowest range (console, and a Range column in the report). The default is a single measurement with no warm-up, and its output format is unchanged.
- `./branch_bench -t <N>` / `./branch_bench --test <N>`: Runs only test N (1–4); repeat the option to run several. The banner and report note which tests ran.
- `./branch_bench --size <N>`: Elements per array for Tests 1/2/4 and calls per trial for Test 3 (default 4M = 4194304, max 1024M; `K`/`M` suffixes are powers of 1024).
- `./branch_bench --reps <N>`: Passes over the array per trial for Tests 1/2/4 (default 64, max 100000).
- Non-default sizes are visible in the banner, test headings and report header table. Results are only comparable with RESULTS.md at the defaults.
- `./branch_bench -h` / `./branch_bench --help`: Displays usage information.

### Testing and Validation
There is no dedicated test framework or linter in this repository. Validate changes by compiling with `gcc` using the required flags (plus `-Wall -Wextra -pedantic` to catch issues) and executing the binary. `clang --analyze -std=c99 branch_bench.c` should report no warnings; it finds the same kind of path-based bugs (uninitialized values, out-of-bounds reads) that SonarQube Cloud reports. SonarQube also flags implicit integer-to-`double` conversions (rule S5276), so write them as explicit `(double)` casts; they compile to the same code. Test 4 includes a built-in runtime sanity check verifying that branch and branchless implementations compute identical sums; on a mismatch it prints an error and the program exits with status 1.

Changes anywhere in the file can move the kernels to new addresses, which can change timings even when their code is untouched. After a change, compare the kernels' disassembly with the previous build. Also check that no jump in a kernel's hot loop crosses or ends on a 32-byte boundary: Intel's JCC-erratum microcode slows such jumps on Skylake-derived CPUs, including the i5-8350U in RESULTS.md.

### Continuous Integration
[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs on pushes to `main` and on pull requests. Hosted runners are VMs, usually without hardware perf counters, so CI checks correctness and codegen only, not timings.
- **gcc** (x86-64 and arm64): builds with the required flags plus `-Wall -Wextra -pedantic -Werror`, compiles the non-perf path (`-U__linux__`), runs `--size 64K --reps 2`, and runs [`.github/scripts/check_kernels.sh`](.github/scripts/check_kernels.sh), which fails if the Test 1–3 kernels contain a conditional move/select or SIMD registers, or lose their conditional branch or indirect call. Run it locally with `.github/scripts/check_kernels.sh ./branch_bench`.
- **clang**: builds with the Clang flags and `-Werror`, runs the small size, and runs `clang --analyze` with `-analyzer-werror`. The kernel check is not run, because Clang emits `cmov` in `sum_threshold`.
- **sanitizers**: GCC ASan+UBSan (x86-64 and arm64) and Clang MSan, each running [`.github/scripts/smoke_test.sh`](.github/scripts/smoke_test.sh), which exercises every option, the report file and invalid input. Sanitizer builds are for correctness only; never use them for results.
- **sonarqube**: a `--coverage` build under SonarQube's Build Wrapper runs `smoke_test.sh`, once as is and once with the [`.github/scripts/fake_perf.c`](.github/scripts/fake_perf.c) `LD_PRELOAD` shim, which fakes the perf counters (`FAKE_PERF_EVENTS` selects which ones open), `/proc/cpuinfo` and cpufreq sysfs so the code paths that need them are covered too. `gcovr` converts the coverage, and the scan uploads both (settings in [`sonar-project.properties`](sonar-project.properties)). Needs the `SONAR_TOKEN` secret and Automatic Analysis disabled in SonarQube Cloud.

The SonarQube Cloud quality gate requires at least 80% coverage on new code. Paths reachable only with perf counters or sysfs are covered through the shim, which can also be run locally:
```sh
gcc -std=c99 -O0 --coverage -fno-tree-vectorize -fno-if-conversion -o branch_bench_cov branch_bench.c
gcc -shared -fPIC -o fake_perf.so .github/scripts/fake_perf.c -ldl
LD_PRELOAD=$PWD/fake_perf.so .github/scripts/smoke_test.sh ./branch_bench_cov
gcov -b branch_bench_cov-branch_bench.gcda   # summary; per-line counts in branch_bench.c.gcov
```

When adding an option, add its valid and invalid forms to `smoke_test.sh`. When adding a kernel that must keep a branch, add it to `check_kernels.sh`. When code starts using a new perf event, `/proc` or sysfs file (e.g. new `/proc/cpuinfo` keys), extend `fake_perf.c` so the coverage job still reaches it.

---

## Code Architecture and Implementation Conventions

### 1. Function Inlining Prevention
All benchmark kernels (such as `sum_threshold`, `count_decisions`, `dispatch_sequential`, `dispatch_random`, `sum_branch`, and `sum_branchless`) must be annotated with `__attribute__((noinline))`. This guarantees that the compiler will not inline hot loops into caller functions or optimize across call boundaries.

### 2. Dead Code Elimination (DCE) Prevention
Because benchmark loops compute values without external side effects during timing, the compiler could eliminate the loop entirely.
- Always retain or consume the computed output.
- Use `anti_dce_sink(u64 v)` (in [`branch_bench.c`](branch_bench.c)) on computed results, which forces the value into a `volatile u64` sink.
- If a test performs an observable validation (such as Test 4's result comparison check), an additional `anti_dce_sink` call may be omitted.

### 3. Separation of Setup vs. Timed Execution
- Never allocate memory, perform I/O, or invoke the random number generator inside the timed measurement loop.
- Array generation, shuffling, and table initialization (e.g. `decisions_random`, `dispatch_indices`) must occur *before* starting the timer.
- Free all allocated resources once the benchmark passes complete.
- Read the `--size` global once at the start of each test (`const size_t len = size;`) and use `len` for the allocation, setup loops and kernel calls. Reading the global repeatedly makes static analyzers (SonarQube, `clang --analyze`) report out-of-bounds accesses, because they assume it may change between the `malloc` and the kernel call.

### 4. Deterministic and Low-Overhead PRNG
Use the local xorshift64 PRNG (`rng64()` / `rng_state`) instead of `stdlib rand()`. This avoids glibc locking overhead and distribution skew.

### 5. Indirect Dispatch and Identical Code Folding (ICF)
Test 3 generates 32 leaf functions via X-macros (`FOR_EACH_LEAF` / `DEF_LEAF`). Each leaf function performs a distinct arithmetic operation based on a unique Weyl sequence step (`0x9e3779b97f4a7c15ULL`). This prevents Identical Code Folding (GCC's `-fipa-icf`, on at `-O2`, or the linker's) from merging the function pointers into a single destination address.

GCC *does* fold `sum_branch` into `sum_threshold`, because their bodies are identical, so the disassembly shows a single function under one of the two names. That is harmless: Test 1 and Test 4's branch variants are meant to run the same code.

### 6. Perf Timing Window Convention
- Measure every kernel call with `MEASURE(result, kernel(args))`. It runs the `-n/--trials` loop (with a warm-up only when trials > 1) and keeps the median trial via `median_trial()`. Each trial goes through `MEASURE_ONCE`, which calls `perf_start()` immediately *before* the `now_ms()` timer starts and `perf_stop()` immediately *after* it stops.
- The reported row is one real trial (the median by time, the lower middle one for even counts), never a per-field median, so time, cycles, branches and misses always belong together.
- Do **not** nest `perf_start()` or `perf_stop()` inside the `now_ms()` timing window; doing so introduces system call overhead into the wall-clock measurements.
- `MEASURE` is a macro, not a function, on purpose: the kernel must be called directly (not through a function pointer) so its codegen stays unchanged.
- Guard access to each counter field with `perf_has(CTR_BRANCHES)`, `perf_has(CTR_MISSES)` or `perf_has(CTR_CYCLES)`; any of them can be missing independently (`print_row()`, `report_table()` and `print_miss_cost()` already do).
- New counters go into the same group via `perf_add()` in `perf_init()`, plus a matching `Result` field and `CTR_*` slot.

### 7. Formatted Report Emission
Each test function receives `FILE *report` (which will be `NULL` if `-o` was not specified). Any new test should:
- describe its work per trial once, as `const Work work = { count, "elem" }` (or `"call"`), which drives the misses-per-item column;
- print each console row with `print_row(label, &result, &work)`;
- construct a `ReportRow` array and call `report_table(report, title, desc, rows, count, &work)`;
- print its summary lines (e.g. `→ Slowdown:` and `**Slowdown:**`), then `print_miss_cost(report, &predictable, &unpredictable)` for the cost-per-miss line.

This keeps console and Markdown output consistent. Console labels may carry extra padding for alignment; report labels should not.

---

## Git and Contribution Guidelines
- Do not commit changes unless explicitly requested by the user.
- Keep the codebase self-contained within [`branch_bench.c`](branch_bench.c).
- Maintain C99 compatibility (C99 language plus POSIX/Linux APIs, built with GCC) without unnecessary external dependencies. GNU extensions already in use, such as `__attribute__((noinline))`, are accepted.

---

## TODO

### Measurement quality
- **Verify the ARM raw-event fallback on hardware:** `perf_add()` falls back to raw events `0x12` (`BR_PRED`), `0x10` (`BR_MIS_PRED`) and `0x11` (`CPU_CYCLES`) when the generic events fail. Misses previously used `0x21` (`BR_RETIRED`, which counts all retired branches); the fix has only been checked by preprocessing for aarch64 (no ARM toolchain was available), not compiled or run. Test it on a Raspberry Pi 4 and a Cudy WR3000S (Cortex-A53, OpenWrt). On OpenWrt, check that the kernel has `CONFIG_PERF_EVENTS` enabled, and expect "Unknown CPU" because arm64 `/proc/cpuinfo` has no model line there.
  - **Raspberry Pi 4 (Cortex-A72):** done; RESULTS.md has a run with all three counters. Misses look correct (0.500 per element for Test 1 shuffled), but the unpredictable variants count more branches than the predictable ones (646M vs 537M in Test 1), which suggests the speculative `BR_PRED` raw event was used, i.e. the generic branch event failed and the fallback ran. Confirm which path ran with `strace -e perf_event_open ./branch_bench -t 1 --size 64K --reps 1` (`PERF_TYPE_RAW` with config `0x12` means the fallback).
  - **Cudy WR3000S (Cortex-A53):** not run yet.

---

## Long-Term Goals

### Portability to more architectures (MIPS, RISC-V, PowerPC, ...)
The benchmark is developed and tested on x86-64 and ARM (Cortex-A72) Linux. Supporting other architectures is a long-term goal; known gaps:
- **CPU model detection:** `get_cpu_model()` only recognizes the x86 `model name` and ARM `Model`/`Hardware` keys in `/proc/cpuinfo`. MIPS uses `cpu model` (plus `system type`), PowerPC uses `cpu`, and RISC-V often has only `isa`/`uarch`, so these currently report "Unknown CPU".
- **Perf counters:** generic `PERF_COUNT_HW_*` events (branches, misses, cycles) are tried first everywhere, but raw-event fallbacks exist only for ARM. Many embedded MIPS cores have no PMU or no kernel PMU driver, so these fall back to wall-clock timing only.
- **Branch codegen:** `-fno-if-conversion` must still leave real branches in Tests 1–3. Check the disassembly on each new architecture: MIPS has conditional moves (`movn`/`movz`, `seleqz`/`selnez` on R6) and RISC-V has `czero` (Zicond).
- **32-bit targets:** the `u64` accumulators and Test 3 leaf arithmetic become multi-instruction sequences on 32-bit cores (e.g. MIPS32), which changes the per-iteration work and makes timings incomparable with 64-bit results.
- **Memory footprint:** by default the tests allocate two 4 MiB buffers at a time; on small embedded boards use `--size` (and `--reps` to keep run times sensible).
- **Interpretation:** README's interpretation section is written for deep out-of-order cores. In-order cores (common on MIPS) have much shorter misprediction penalties.
- **Validation:** cross-compile (e.g. `mips-linux-gnu-gcc`) and run under `qemu-user` to check correctness and output format. Timings under emulation are meaningless; real hardware is required for results.
