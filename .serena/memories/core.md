# branch_bench — core

Single-file C99 microbenchmark measuring branch-misprediction cost. `AGENTS.md` (pulled in by `CLAUDE.md`) is the authoritative guide: tests, CLI options, conventions 1–7, TODO, long-term goals. Don't duplicate it; consult it first.

## Source map
- `branch_bench.c` — everything. Order: `#define _DEFAULT_SOURCE` + includes → perf includes (`HAVE_PERF`) → tunables (`DEFAULT_SIZE`, `DEFAULT_REPS`, `THRESHOLD`, `NUM_FUNCS`, `NUM_TESTS`, `MAX_*`) and run-time settings (`size`, `reps`, `trials`, `tests_selected`) → `Result` → timing → perf group → `median_trial`, clock accounting, `MEASURE_ONCE`/`MEASURE` → RNG → CPU model/heading → output helpers (`Work`, `report_table`, `print_row`, `print_miss_cost`) → tests 1–4 (kernels + `run_testN`) → CLI parsing → banner / report header / clock summary → `main`.
- `run.sh` — canonical build+run (`-std=c99` + required flags).
- `README.md` — user docs, incl. the interpretation guide (its only copy; the program just points to it).
- `RESULTS.md` — per-CPU reports from `-o`, appended by hand.
- `.github/workflows/ci.yml` — gcc (x86-64+arm64) / clang / sanitizer / SonarQube coverage jobs. Scripts in `.github/scripts/`: `check_kernels.sh` (Test 1–3 kernels keep real branches), `smoke_test.sh` (every option + invalid input, exit codes only), `fake_perf.c` (LD_PRELOAD shim faking perf counters, `/proc/cpuinfo`, cpufreq for coverage). `sonar-project.properties` — scan settings (only `branch_bench.c` is analyzed).

## Invariants / gotchas
- Run-time settings are globals set only by CLI options; each `run_testN` copies `size` into a local `len` once (otherwise static analyzers report out-of-bounds reads).
- Default output (no options) must keep its format; opt-in features (`-n`, `-t`, ...) only add output when used.
- Kernel inner loops must stay instruction-identical across changes so new results stay comparable with RESULTS.md; any edit can still move them (see `mem:task_completion`).
- Perf counters (branches, misses, cycles) are one group; each can be missing independently (`perf_has(CTR_*)`). ARM raw fallback codes 0x12/0x10/0x11 are unverified on hardware (AGENTS.md TODO); 0x21 is BR_RETIRED, never use it for misses.
- Report heading (`model @ max GHz`) is generated; RESULTS.md suffixes like `(WSL2)` or board/core names are hand-edited.
- RESULTS.md's i5-3380M section is historical (hardware gone): it keeps old Test 3 labels and the pre-Cycles table layout. Keep it for the Ivy Bridge vs Haswell Test 3 comparison; don't regenerate or normalize it.
- Console and Markdown report are written in parallel in each `run_testN`; change both together.
- CI runners have no PMU or cpufreq: those paths are covered only via `fake_perf.c`. New perf events, `/proc`/sysfs reads or cpuinfo keys need matching fakes there, or new-code coverage drops below the 80% gate.
- Serena symbol tools (`find_symbol`, `insert_before_symbol`) have timed out on this file; `replace_content` and plain reads work reliably.

Further memories:
- Toolchain, required flags, feature macros, compiler/platform support: `mem:tech_stack`
- Build/run/report/analysis commands: `mem:suggested_commands`
- Kernel and measurement patterns (MEASURE, counters, Work/ReportRow output, anti-DCE, X-macro leaves): `mem:conventions`
- What to verify before declaring a change done (builds, analyzer, disassembly, output format): `mem:task_completion`
