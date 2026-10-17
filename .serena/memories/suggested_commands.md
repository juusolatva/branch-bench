# Suggested commands (run from repo root)

- Build and run: `./run.sh`
- Build with warnings: `gcc -std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -Wall -Wextra -pedantic -o branch_bench branch_bench.c`
- Clang build check: `clang -std=c99 -O2 -fno-vectorize -fno-slp-vectorize -Wall -Wextra -pedantic -o /tmp/bb branch_bench.c` (clang rejects `-fno-if-conversion`; its timings aren't trusted)
- Static analysis (mirrors SonarQube Cloud's path-sensitive C checks): `clang --analyze -std=c99 branch_bench.c` — must print no warnings (writes a `.plist` in the cwd; run from a scratch dir)
- Markdown report: `./branch_bench -o report.md`; steadier numbers: `-n 5`; quick/small runs: `-t <N>`, `--size 64K --reps 2`. Help: `./branch_bench --help`. To add a machine to `RESULTS.md`, append the report's `## <CPU>` section by hand (defaults only).
- Kernel disassembly: `objdump -d --no-show-raw-insn branch_bench | awk '/<sum_branch>:/,/^$/'`. The body shared by `sum_threshold` and `sum_branch` is listed only under `sum_branch`; other kernels may carry suffixes like `.constprop.0`.
- Perf counter permission: `cat /proc/sys/kernel/perf_event_paranoid` (≤ 2 needed).
- CI checks locally: `.github/scripts/check_kernels.sh ./branch_bench`; `.github/scripts/smoke_test.sh <binary> [work dir]` (exit 0 = all ok).
- Coverage with faked counters/sysfs: build `gcc -std=c99 -O0 --coverage -fno-tree-vectorize -fno-if-conversion -o branch_bench_cov branch_bench.c` and `gcc -shared -fPIC -o fake_perf.so .github/scripts/fake_perf.c -ldl` (no `-pedantic`: dlsym casts), run `LD_PRELOAD=$PWD/fake_perf.so .github/scripts/smoke_test.sh ./branch_bench_cov`, then `gcov -b branch_bench_cov-branch_bench.gcda`. `FAKE_PERF_EVENTS=misses` (or `branches,cycles`, ...) opens only those counters. Build in a scratch dir: `.gcda`/`.gcno` land next to the binary.
- SonarQube Cloud (public project, no auth): issues `curl -s "https://sonarcloud.io/api/issues/search?componentKeys=juusolatva_branch_bench&resolved=false&ps=100"`; quality gate `.../api/qualitygates/project_status?projectKey=juusolatva_branch_bench`; per-line coverage `.../api/sources/lines?key=juusolatva_branch_bench:branch_bench.c&from=1&to=5000` (`lineHits`, `conditions`, `coveredConditions`, `isNew`).
- Full default run takes ~10 s (4M elements × 64 reps per variant); `-n N` takes about N+1 times as long.
