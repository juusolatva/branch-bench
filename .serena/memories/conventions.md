# Conventions

Full rules are in `AGENTS.md` (sections 1–7). Key points:

- Kernels are `static __attribute__((noinline))`, take `(data, len, n_reps)` (Test 3: `len` calls) and return a value that gets consumed: through `anti_dce_sink(u64)` or an observable check (Test 4 compares branch and branchless sums).
- Measure only via `MEASURE(result, kernel(args))`: it runs the `-n` trials (warm-up only when trials > 1) and returns the median trial; `MEASURE_ONCE` wraps one call as perf_start → timer → kernel → timer → perf_stop. Nothing else goes inside: no allocation, RNG, I/O or syscalls. Keep it a macro so kernels are called directly.
- A reported row is one real trial, never a per-field median.
- Guard each counter field with `perf_has(CTR_BRANCHES|CTR_MISSES|CTR_CYCLES)`; any may be missing. New counters: `perf_add()` in `perf_init()` + `Result` field + `CTR_*` slot.
- Output per test: `const Work work = { count, "elem" | "call" }`; console rows via `print_row(label, &r, &work)`; report via `ReportRow rows[]` + `report_table(report, title, desc, rows, n, &work)`; then `→ Slowdown:` / `**Slowdown:**` lines and `print_miss_cost(report, &predictable, &unpredictable)`. Console labels may be padded; report labels not.
- Each `run_testN`: `const size_t len = size;` first, setup before measuring, free everything at the end.
- Randomness only from xorshift64 `rng64()` / `rng_state`, never `rand()`.
- Test 3 leaves: `FOR_EACH_LEAF(DEF_LEAF)` / `PTR_LEAF`, each with distinct arithmetic so ICF can't merge them; keep `NUM_FUNCS` in sync. (GCC does fold `sum_branch` into `sum_threshold`: identical bodies, harmless.)
- CLI: options handled one at a time in `apply_option()`; numeric values via `option_number()`/`parse_count()` (range-checked, K/M suffixes). Keep functions small: SonarQube Cloud flags cognitive complexity > 25.
- Types: `u64`/`u8`; format u64 with `PRIu64`, `size_t` with `%zu`; unsigned literal suffixes uppercase (`1UL`). Integer→`double` conversions always as explicit `(double)` casts (SonarQube S5276), also in mixed arithmetic like `100.0 * (double)a / (double)b`.
- Style: 4-space indent, K&R braces, section-banner comments (`/* ── Title ─── */`), aligned `#define` comments, `static` on every file-scope symbol.
