# branch_bench — core

Single-file C99 microbenchmark measuring branch-misprediction cost. `AGENTS.md` (pulled in by `CLAUDE.md`) is the authoritative guide: tests, CLI options, conventions 1–7, TODO, long-term goals. Don't duplicate it; consult it first.

## Source map
- `branch_bench.c` — everything. Order: `#define _DEFAULT_SOURCE` + includes → perf includes (`HAVE_PERF`) → tunables (`DEFAULT_SIZE`, `DEFAULT_REPS`, `THRESHOLD`, `NUM_FUNCS`, `NUM_TESTS`, `MAX_*`) and run-time settings (`size`, `reps`, `trials`, `tests_selected`) → `Result` → timing → perf group → `median_trial`, clock accounting, `MEASURE_ONCE`/`MEASURE` → RNG → CPU model/heading → output helpers (`Work`, `report_table`, `print_row`, `print_miss_cost`) → tests 1–4 (kernels + `run_testN`) → CLI parsing → banner / report header / clock summary → `main`.
- `run.sh` — canonical build+run (`-std=c99` + required flags).
- `README.md` — user docs, incl. the interpretation guide (its only copy; the program just points to it).
- `RESULTS.md` — per-CPU reports from `-o`, pasted by hand under `# NEW RESULTS` (bare metal or WSL2), `# OLD RESULTS` (old format) or `# VPS RESULTS`.
- `.github/workflows/ci.yml` — gcc (x86-64+arm64) / clang / sanitizer / SonarQube coverage jobs. Scripts in `.github/scripts/`: `check_kernels.sh` (fails if a Test 1–3 kernel has cmov/csel, flag arithmetic (setcc/sbb/adc, adc/sbc/ngc) or SIMD, or Test 3 loses its indirect call; the loop back-edge alone satisfies its branch check, so a pass is necessary, not sufficient; the clang job runs it and requires FAIL for Tests 1/4 and 2), `smoke_test.sh` (every option + invalid input, exit codes only), `fake_perf.c` (LD_PRELOAD shim faking perf counters, `/proc/cpuinfo`, cpufreq for coverage). `.github/dependabot.yml` — monthly grouped PRs for SHA-pinned actions and `.github/requirements.txt` (gcovr pin, used as a pip constraint by `pipx install` in ci.yml). `sonar-project.properties` — scan settings (only `branch_bench.c` is analyzed).

## Invariants / gotchas
- Run-time settings are globals set only by CLI options; each `run_testN` copies `size` into a local `len` once (otherwise static analyzers report out-of-bounds reads).
- Default output (no options) must keep its format; opt-in features (`-n`, `-t`, ...) only add output when used.
- Kernel inner loops must stay instruction-identical across changes so new results stay comparable with RESULTS.md; any edit can still move them (see `mem:task_completion`).
- Perf counters (branches, misses, cycles) are one group; each can be missing independently (`perf_has(CTR_*)`). ARM raw fallback codes 0x12/0x10/0x11 are unverified on hardware (AGENTS.md TODO); 0x21 is BR_RETIRED, never use it for misses.
- Report heading (`model @ max GHz`) is generated; RESULTS.md suffixes like `(WSL2)` or board/core names are hand-edited.
- RESULTS.md's `# OLD RESULTS` sections (i5-3380M, Ryzen 9600X) are historical: old Test 3 labels and the pre-Cycles table layout. Keep them (i5-3380M: hardware gone, needed for the Ivy Bridge vs Haswell Test 3 comparison); don't regenerate or normalize them.
- Console and Markdown report are written in parallel in each `run_testN`; change both together.
- CI runners have no PMU or cpufreq: those paths are covered only via `fake_perf.c`. New perf events, `/proc`/sysfs reads or cpuinfo keys need matching fakes there, or new-code coverage drops below the 80% gate.
- On a Windows checkout Serena's clangd parses `branch_bench.c` without `__linux__` (no `HAVE_PERF`): `perf_add`, `perf_fd`, `cpuinfo_model_value` etc. are invisible, and `find_symbol('perf_init')`/`perf_stop` return the empty `#else` stubs, so symbol edits would hit the stubs. Read/edit perf and cpuinfo code with plain file tools (`replace_content`, Read/Edit), or run Serena from Linux/WSL.
- Test 3 leaves exist only via macro expansion: symbol tools show `FOR_EACH_LEAF/leaf_N` located on the `FOR_EACH_LEAF(DEF_LEAF)` line. Edit the `DEF_LEAF`/`FOR_EACH_LEAF` text, never `leaf_N` symbols.

Further memories:
- Toolchain, required flags, feature macros, compiler/platform support: `mem:tech_stack`
- Build/run/report/analysis commands: `mem:suggested_commands`
- Kernel and measurement patterns (MEASURE, counters, Work/ReportRow output, anti-DCE, X-macro leaves): `mem:conventions`
- What to verify before declaring a change done (builds, analyzer, disassembly, output format): `mem:task_completion`
