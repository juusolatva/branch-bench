# branch bench

[![CI](https://github.com/juusolatva/branch_bench/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/juusolatva/branch_bench/actions/workflows/ci.yml)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=juusolatva_branch_bench&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=juusolatva_branch_bench)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=juusolatva_branch_bench&metric=coverage)](https://sonarcloud.io/summary/new_code?id=juusolatva_branch_bench)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A simple benchmark for testing out gains from branch prediction or conversely losses from mispredictions.

## The tests

1. **Threshold Sum** sums the bytes above 128 in a sorted and a shuffled copy of the same random array, and prints how many bytes per pass are above 128 (about half).
2. **Bias and Ordering** counts the ones in 0/1 arrays with 1/16, 1/4, 1/2, 3/4 or 15/16 ones. Each bias has exactly the same number of ones in three orders, a repeating word (e.g. `1000`), all ones first, and shuffled, plus all-0 and all-1 controls: 17 rows.
3. **Indirect Dispatch** calls functions through a pointer table from one call site, for 1, 2, 4, … 64 targets, each in a repeating cycle (`i % T`) and in the same calls shuffled.
4. **Branch vs Branchless** computes Test 1's sum with Test 1's conditional-jump kernel and with an arithmetic mask, on a sorted and a shuffled copy of one random array, and compares branchless with branch for each order directly.
5. **Pattern Learnability Matrix** (opt-in) runs N = 1, 4, 16, 64, 256 or 512 conditional branches per block, each repeating its own random L-bit word, after a fixed number of training periods, and shows misses and cycles per data branch as N × L tables. Constant, alternating, de Bruijn and balanced-random patterns are controls.
6. **Loop Exits** (opt-in) runs one counted inner loop for trip counts C = 1 to 512, always on the same branch, and shows cycles per inner iteration and misses per completed loop.

A run without `-t` runs Tests 1–4. It takes longer than before revision 3 (Test 2 measures 17 rows instead of 2): about 30 s instead of 10 s at the defaults on an i5-8350U laptop. Tests 5 and 6 run only when selected with `-t` or `--hard` (all six tests); their grids come from `--profile` (see [Profiles](#profiles)). Results are comparable only within one benchmark revision; RESULTS.md holds revision 1 (see [Seeds and revisions](#seeds-and-revisions)).

## Notes

You **MUST** run this with ./run.sh, use a [prebuilt binary](#prebuilt-binaries), or build it with exactly `gcc -std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -o branch_bench branch_bench.c`, adding `-Wa,-mbranches-within-32B-boundaries,-malign-branch=jcc+fused+jmp+call+ret+indirect` on x86-64 as `./run.sh` does. Without *-fno-tree-vectorize* and *-fno-if-conversion* the compiler optimizes the mispredictions away, and other optimization levels give results that cannot be compared with [RESULTS.md](RESULTS.md). The x86-64 assembler flags keep every jump in the kernels off 32-byte boundaries, which Intel's JCC-erratum fix slows on Skylake-derived CPUs. `./run.sh` builds and then runs the benchmark with any options you give it (e.g. `./run.sh -n 5 -o report.md`); after that, `./branch_bench` runs it directly. Build with GCC. On x86-64 and AArch64 the branch kernels of Tests 1, 2 and 4–6 are written in assembly, so they keep their branches with any compiler, Clang included. Other architectures use C versions of all kernels, whose branches depend on the compiler (Clang has no equivalent of *-fno-if-conversion*), and Test 5 then measures only its N = 1 cells; the banner and report say which kernels ran (`Kernels:`). See [AGENTS.md](AGENTS.md).

The benchmark builds on Linux with GCC; on Windows, use WSL2. Other POSIX systems build with GCC too but show times only. On macOS `gcc` is Clang, so install GCC (e.g. from Homebrew) and build with the command above using its versioned `gcc-<N>` (`./run.sh` calls plain `gcc`), without the `-Wa,...` flags: they need the GNU assembler (binutils 2.34 or later), which macOS does not use.

Branch, miss and cycle counts need Linux hardware performance counters, which require `/proc/sys/kernel/perf_event_paranoid` to be 2 or lower. Check it with `cat /proc/sys/kernel/perf_event_paranoid`; distributions such as Debian and Ubuntu default to a higher value, and `sudo sysctl kernel.perf_event_paranoid=2` lowers it until the next reboot. Without counters, and in most VMs and containers, only times are shown: a VM has counters only if its hypervisor provides a virtual PMU (WSL2 can). A counter that cannot be opened is left out of the console output and shows as – in the report. If the kernel time-shares the counters with other users, a note says the counts are scaled estimates.

If a trial's counter read fails or its group never gets scheduled, that
trial still reports its wall-clock time. Its hardware counts show as –
in the report and are excluded from the effective cost per miss and the cycle rate.

## Prebuilt binaries

Each [release](https://github.com/juusolatva/branch-bench/releases) has static Linux binaries for x86-64 and arm64 (aarch64), built with the newest GCC release and the flags above. Being static, they run on any Linux of that architecture, including musl systems such as OpenWrt. The banner shows the compiler, flags and release tag. Download the one for your machine (`uname -m`) and its `.sha256` file, then:

```sh
bin=$(ls branch_bench-*-linux-"$(uname -m)")
sha256sum -c "$bin.sha256"
chmod +x "$bin"
./"$bin" -n 6 -o report.md
```

`gh attestation verify <file> --repo juusolatva/branch-bench` checks that a binary was built by this repository's release workflow.

## Saving results

Console output is meant to be read as it scrolls by. To get a clean,
table-formatted Markdown report instead use `-o`/`--output`:

```sh
./branch_bench -o report.md
```

This writes the report file in addition to the normal
console output. An existing file is overwritten, and a new one is
readable only by you. The program exits with status 1 on errors,
including an invalid option or a kernel result that differs from the
reference computed without the kernel (Tests 1–4 check every row); such
a run also ends its console output and report with an ERROR line, so its
numbers are not mistaken for valid ones.

The report heading shows the CPU's maximum (turbo/boost) clock where
Linux reports it. Where it does not (WSL2, most VMs), the heading is the
model name as is, which on Intel CPUs may end in the base clock
(e.g. `@ 2.90GHz`). The CPU rarely holds its maximum clock under sustained load,
so the report also ends with the **effective user-cycle rate during
measurements**: user-mode cycles divided by wall time over all measured
trials. It is close to the clock the results were actually produced at,
and often explains why timings differ between runs on the same machine.
Time spent in the kernel or preempted lowers it, so it is not the core's
instantaneous clock.

## Machine-readable export

`--json <file>` writes every measured trial and every summary as JSON
Lines (one JSON object per line, schema 2), next to the console output
and any `-o` report. The two must be different files; naming the same
file (also via a hard link or `./`) is an error, and neither file is
emptied until both have opened:

```sh
./branch_bench -n 5 -o report.md --json run.jsonl
```

| `type` | One record per | Main fields |
|---|---|---|
| `meta` (first line) | run | `schema` 2, `revision`, `profile`, `results_ok` (false when a kernel result differed from its reference), CPU, microcode, arch, kernel, compiler, `kernel_backend` (`asm-x86_64`, `asm-aarch64` or `c`, for the data-branch kernels; each `kernel` record has its own `backend`), build flags, commit, `alignment_policy`, `seed` and `test_seeds`, size/reps/trials, `training_runs` (the untimed runs before each trial of Tests 1–4 and 6; Test 5 trains by exposure instead), `schedule` (`AB/BA`), `affinity`, `hypervisor`, `smt_siblings`, `governor`, `max_ghz`, `effective_user_cycle_ghz`, and per counter its event type, config and `semantics` (`retired`, `speculative`, `cycles`, `unknown`) |
| `test` | selected test | `test` number, `id` (`threshold-sum`, `bias-ordering`, `indirect-dispatch`, `branch-vs-branchless`, `pattern-matrix`, `loop-exits`), `revision` and `title`; the id says which test a number means, since Test 6 meant Correlation Distance up to revision 3 |
| `kernel` | kernel | `id`, `revision`, `backend` (`asm-…` or `c`), `entry` (always `null`), unit, declared `data_branches`, `indirect_calls`, `inner_loop_branches`, `taken_when` (`"false"`: the asm data branch jumps, skipping the update, when its condition is false; `null` for C kernels), tests |
| `variant` | report row | `test`, `variant` (row index), `label`, `kernel`, `work` and `unit` per trial; its input, recorded before measuring: `pass_items` (items per pass), `cond_true` (items per pass whose source condition holds) and `input_hash` (FNV-1a of the input buffer), each `null` where the test does not record it; `params`, the test's own parameters (Test 5: `N`, `L`, `blocks`, `passes`, `exposure`, `family`, `group`, `data_bytes`, `skipped`, `reason`, `ones`, `distinct`, `short_period`; Test 6: `C`, `loops`, `iters`, `body`), else `null` |
| `trial` | measured trial | `test`, `variant`, `pair`, run-wide `seq`, `trial`, `train_runs` (`null` in Test 5, which trains by exposure), `exposure_before` (Test 5: the kernel's executions per site before this trial's timed call, training included; else `null`), `time_ms`, `result` (the kernel's checksum), `perf_status`, `enabled_ns`/`running_ns`, `counts` (scaled) and `raw_counts` |
| `pair` | compared pair | `a`/`b` variants, `pairs`, `valid`, summed times, `slowdown`, signed `delta` per counter, `effective_cost` and `cost_status` (`resolved`, `inconclusive`, `unavailable`), and `cost_shown` (the cost line was printed) |

Counters print as exact integers; a value that does not exist (a counter
that is not open, a trial whose read failed, a ratio without data) is
`null`, never 0. `.github/scripts/check_export.py run.jsonl --report
report.md` rebuilds every pair summary from the trials and checks the
report's medians, ranges, counter columns, ratios, cost lines, Test 5's
matrices and exposure records and Test 6's loop-exit curve against
them. Seeds and counts are 64-bit integers, which exceed the 2⁵³ that
JavaScript numbers (and `jq`) hold exactly; read them with a parser that
keeps integers, such as Python's `json`.

## Repeated measurements

By default each variant is measured once. For steadier numbers use
`-n`/`--trials`:

```sh
./branch_bench -n 5 -o report.md
```

Each variant is then measured 5 times. The trial with the median time is
reported, together with the fastest–slowest range, so you can see how
noisy the run was.

Every measured trial, also with the default `-n 1`, follows one untimed
training run of the same variant (Test 5 instead trains a fixed number of
complete periods of every branch; see [Profiles](#profiles)), so the predictor has seen that
variant's pattern just before it is timed. This does not reset predictor
state left by other variants; it only makes the exposure the same for
every trial. A run takes about twice as long per trial as the measured
time alone. The two variants of a comparison (e.g. sorted and shuffled)
are measured in alternating order, AB, BA, AB, ..., so slow drift such as
clock changes affects both alike, and each **Slowdown** is the summed
time of one variant's trials over the other's.

## Choosing tests and sizes

```sh
./branch_bench -t 3                 # run only Test 3 (repeat -t for more)
./branch_bench --size 1M --reps 8   # smaller, quicker run (e.g. small boards)
./branch_bench -t 5                 # opt-in Test 5 (pattern learnability matrix)
./branch_bench -t 6                 # opt-in Test 6 (loop exits)
./branch_bench --hard               # all six tests
./branch_bench -t 5 --profile quick # a small Test 5 grid
```

Test 2 needs a `--size` that is a multiple of 16 (whole periods of
every bias pattern) and Test 3 a multiple of 64 (equal calls to every
target). At any other size such a test prints a skip note and the other
selected tests still run.

`--size` sets the elements per array of Tests 1, 2 and 4 (and the calls
per trial for Test 3); `K`/`M` suffixes are allowed. `--reps` sets how
many passes each trial makes over the array. Neither changes Tests 5
and 6, whose sizes come from `--profile`. Results are only comparable with
[RESULTS.md](RESULTS.md) at the defaults (`--size 4M --reps 64`).
`--cpu <N>` pins the whole run to CPU N before the counters open, which
keeps the scheduler from moving it between cores (e.g. P- and E-cores).
If the CPU cannot be used, a warning says so, the banner and report say
the pin failed, and the run continues unpinned.

Run `./branch_bench --help` for all options.

## Profiles

`--profile quick|standard|research` (default `standard`) picks what
Tests 5 and 6 measure; Tests 1–4 ignore it. Every expanded value is in
the export (`params`), and the banner and report name the profile.

| Profile | Test 5 cells (N × L, blocks per pass) | Test 5 training | Test 6 counts C |
|---|---|---|---|
| `quick` | {1, 4, 16, 64} × {8, 32} over 256 blocks; {256, 512} × {16} over 96 | 8 periods | 1, 2, 4, 16, 64, 256 (2¹⁸ inner iterations per row) |
| `standard` | {1, 4, 16, 64} × {8, 16, 32, 64, 128, 256} over 256 blocks; {256, 512} × {16, 32, 48} over 96 | 64 periods | 1–4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 160, 192, 256, 384, 512 (2²⁴ per row) |
| `research` | {1, 4, 16, 64} × {8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256} over 768 blocks; {256, 512} × {16, 32, 48} over 96; {1} × {512 … 65536} over 65536 | 8, 64 and 512 periods, each a separate table | every C from 1 to 160, then 192, 256, 384, 512, plus a second loop body at the standard counts (2²⁴ per row) |

The standard values are provisional until the hardware pilots of the
first six-test release fix them. Test 5's controls (constant 0,
alternating, de Bruijn periods 32–2048 (only 32 and 2048 in `quick`), and a balanced random reference
of 65536 outcomes) run at N = 1 over 65536 blocks in every profile.

In Test 5, the blocks per pass are a multiple of every L in a group, so
a given N has the same data footprint (N × blocks bytes) at every period,
and every branch trains for exactly the profile's number of complete
periods before each trial. Each measured row runs at least 64 complete
periods per branch and 2²² data-branch executions (2¹⁶ in `quick`).
A cell whose training and measurement would exceed the profile's
execution budget is skipped with a note, never shrunk; builds without the
assembly kernels skip every N > 1 cell.

## Seeds and revisions

Every test builds its input from its own seed, derived from a base seed,
the test number and the input stream. Selecting or reordering tests
therefore never changes another test's input, and the same command gives
the same data on every run. `--seed <N>` (decimal, or hex after `0x`; `010` is ten) changes the
base seed; the banner and report show it when it is given.

The banner and report also show the **benchmark revision**. It goes up
whenever a change alters what is measured (inputs, warm-up, pairing,
summaries), even when the test numbers stay the same. Results from
different revisions are not comparable; the entries in RESULTS.md so far
are revision 1, and this version is revision 4.

### Revision 4 migration

Revision 4 replaces Tests 5 and 6 under the same numbers:

- **Test 5** was a de Bruijn period sweep over `--size` elements on
  Test 2's kernel. It is now the pattern learnability matrix
  (`pattern-matrix`, kernels `pattern_sites_N`), sized by `--profile`.
- **Test 6** was Correlation Distance (`correlate_distance`). It is now
  Loop Exits (`loop-exits`, kernels `loop_exit` and `loop_exit_body2`).

Results of the old tests keep their old identity: an export from
revision 3 or earlier (schema 1, or no export) with a Test 5 or 6 means the old
tests, and nothing of theirs is comparable with revision 4. Correlation
experiments are planned to return only as an explicitly experimental
Test 10. `--size` and `--reps` no longer affect Tests 5 and 6.

## Adding results

The current entries in [RESULTS.md](RESULTS.md) are default-size runs
with `-n 6` on an otherwise idle machine. Write the report to a separate
file (never `-o RESULTS.md`) and paste its `## <CPU>` section under
`# NEW RESULTS`, or under `# VPS RESULTS` for a virtual server. The report
records the compiler (and, when built by `./run.sh`, the flags and commit),
since codegen can change timings. `# OLD RESULTS` holds
runs from an earlier version (single trial, no cycle counter, older
Test 3 labels). Suffixes such as `(WSL2)` in headings, and clocks where
Linux reports none, were added by hand.

## Interpreting the results

**Miss %** is misses ÷ branches, shown only when both counters count the same population. On x86 both are retired branches. On ARM, Linux counts generic branches as retired but generic misses speculatively, so the column shows "–" there unless both counters fell back to their raw (speculative) ARM events; misses per element are shown either way.

A branch misprediction costs a pipeline flush: the work fetched down the
wrong path is thrown away and the front end restarts at the correct
target. The minimum penalty is roughly the pipeline depth, typically
~15–20 cycles on modern out-of-order x86 cores.

When the cycle and miss counters are available, each test prints the
**Effective extra cycles per additional miss**: the extra cycles divided
by the extra mispredictions between its predictable and unpredictable
variant (for Test 4, its branch kernel on shuffled vs sorted data), summed over the paired
trials whose counters both read correctly. Differences keep their sign,
so a pair where the unpredictable variant happened to be faster pulls
the figure down instead of being dropped. When the unpredictable variant
adds fewer than 1000 misses per valid pair, the figure would be noise, so
the line says "inconclusive" and shows the raw differences instead; with
some pairs invalid it says how many were used. The figure depends on the
workload around the branch, so it is not the pipeline's recovery latency.
Being measured in cycles, it is
far less affected by the clock speed the CPU happened to run at than the
times are, though not entirely: part of a miss's cost can be waiting on
memory, which takes a fixed time rather than a fixed number of cycles.
In [RESULTS.md](RESULTS.md) (revision 1, where it was called cost per
miss and came from the median trials) it is ~22 cycles for Test 1 on the Intel
machines and ~20 on the Raspberry Pi 4's Cortex-A72. For the older
results, which predate the cycle counter, estimates from the times and
listed clock speeds put it at ~20–25 cycles for Test 1.

The effective cost is only meaningful when the unpredictable variant adds
many misses. When the two variants miss almost equally often, as in
revision 1's Test 3 on cores that could not learn its 32-target cycle,
the small difference in work between the variants dominates and the
figure is inflated (~54 cycles for that Test 3 on the Raspberry Pi 4).
In revision 3 both rows of a Test 3 pair run the same code, so only the
order differs; the figure comes from its 64-target pair.

**Miss %** is misses divided by *all* branches in the timed loop,
including the loop's own back-edge branch, which is almost always
predicted correctly. Test 1's shuffled branch is mispredicted ~50% of the
time, but with two branches per element that shows up as ~25%. The
**misses per element** figure (per call for Test 3) shows the
mispredicted branch's own rate directly: 0.500 for Test 1 shuffled.

On ARM the branch counter may count speculatively executed branches,
including ones on the wrong path after a misprediction, so the
unpredictable variants show more branches than the predictable ones and
a lower Miss % (20.7% instead of 25% for Test 1 shuffled on the
Raspberry Pi 4). Misses per element is unaffected.

**Sanity check for Test 1** (default size): each repetition walks 4,194,304 elements. At
a 50% miss rate that is ~2.1M misses per repetition; at ~20 cycles each
that is ~42M cycles, or ~10 ms per repetition at 4 GHz. That matches
the measured shuffled-minus-sorted difference of ~9–14 ms per repetition
on the non-VPS x86 machines in RESULTS.md (the low end, ~9.5 ms, is the
Ryzen 5 9600X under WSL2).

- **Tests 1 and 2** use direct conditional branches (a jump inside the
  loop). Test 2 separates how often the branch goes one way (the bias)
  from the order: within a bias every row has the same number of ones,
  so a shuffled row's extra misses come from its order alone. The
  RESULTS.md entries (revision 1) still show the old Test 2, every 4th
  element vs. a random 25%.
- **Test 3** uses indirect calls through a function pointer, so the CPU
  must predict a target address, not just taken/not-taken. A plain branch
  target buffer (BTB) remembers only the last target of each branch and
  would miss on every call of a cyclic sequence of two or more targets;
  learning the cycle takes an indirect predictor that uses branch
  history. A shuffled sequence of T targets can be right only by chance,
  about 1 call in T; T = 1 is the single-target control. In RESULTS.md
  (revision 1, which compared a sequential 32-target cycle with random
  indices from two call sites), Haswell (i7-4770) and the newer x86 cores
  with counters predict the cycle almost perfectly, while the Ivy Bridge
  i5-3380M and the Cortex-A72 still miss ~27–28 of every 32 calls. The
  EPYC-Rome VPS (no counters; its QEMU model name hides the host core)
  shows only a 1.20× slowdown, like the i5-3380M's 1.17×, so it
  apparently does not learn the cycle, possibly because of hypervisor
  mitigations. On the Intel machines a missed indirect call costs about
  the same as a missed conditional branch (~23–26 cycles).
- **Test 4** removes the data-dependent branch but not the loop branch,
  which is why its branch count halves. Revision 3 reports branchless vs
  branch for the sorted and the shuffled copy directly (below 1× means
  branchless is faster). Its time barely changes between sorted and
  shuffled data, but it is not automatically faster: in RESULTS.md
  (revision 1, sorted vs separately generated random data) the branching
  version wins on sorted data on the i7-4770 (144 ms vs 195 ms), the
  i5-3380M and the Ryzen 5 9600X, because a correctly predicted branch is
  nearly free while the branchless version always does the extra work.
  The i5-8350U and the Raspberry Pi 4 are exceptions, where branchless is
  faster even on sorted data. On the i5-8350U, front-end counters show
  that each correctly predicted *taken* branch costs about 2 cycles, at
  any clock speed, and the sorted branch loop takes 1.5 taken branches
  per element. The likely, but unconfirmed, reason is that Skylake and
  Kaby Lake have their loop stream detector disabled by microcode
  (`lsd.uops` reads 0 there), so short loops cannot hide the cost of their
  jumps as Haswell's can. The Skylake Xeon VPS shows branching winning
  on sorted data (319 ms vs 390 ms), but its QEMU model name hides the
  host's real core, so it neither confirms nor refutes this. The
  Raspberry Pi 4's reason has not been investigated.
- **Test 5** asks how the misses depend on the number of branches N and
  the pattern length L. Each of the N branches of a block reads its own
  byte of the block, and each repeats its own random L-bit word, so all
  N patterns must be learned at once. Read the matrices against the
  controls: the constant and alternating rows are the floor, and the
  balanced reference (65536 random outcomes, about 0.5 misses per branch)
  the ceiling. Where misses rise with N or L is a description of this
  kernel after this much training on this machine; it is not a
  predictor's table capacity, entry count or history length. Several
  things the matrix does not separate: the block loop's own branch (one
  per N data branches) and the pass loop's exit are in the counts; a
  larger N also means more code and a larger data footprint (N × blocks
  bytes), and cache effects are not measured; and training is fixed, so a
  row that is still learning looks worse than a steady one. The research
  profile repeats every cell after 8, 64 and 512 training periods to show
  that. Random words are not filtered: with N = 512 and L = 8 many
  branches share a word (`distinct` in the export), and a word can have a
  shorter period than L (`short_period`). The de Bruijn controls contain
  every k-bit word once per period of 2^k, so a short history identifies
  the position; how well they are learned is again a property of this
  kernel and machine.
- **Test 6** runs one counted loop: the inner branch is taken C − 1 times
  and not taken once per completed loop, and the trip count is a
  run-time value, so every count uses the same branch address and body.
  Misses per loop are near 0 where the exit is predicted and approach 1
  where it is not; they include the outer loop branch and the call. A
  count at which misses rise describes this kernel on this machine; it is
  not a loop predictor's limit, and published loop-predictor sizes are
  reference observations, not expected values. Look at the neighbouring
  counts (the research profile measures every count up to 160) before
  describing a transition. Rows do different work (about 2²⁴ inner
  iterations, in C × loops), so compare the per-iteration and per-loop
  figures, not the times. The research profile's `body 2` rows repeat
  standard counts with a second loop body as a control.
