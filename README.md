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
5. **Pattern Learnability** (opt-in) runs Test 2's kernel on repeating de Bruijn patterns with periods 32 to 2048, and on random 50% ones.
6. **Correlation Distance** (opt-in) makes a branch repeat the random outcome of a branch d = 4 to 1024 constant-outcome branches earlier, against a reference where it is random.

A run without `-t` runs Tests 1–4. It takes longer than before revision 3 (Test 2 measures 17 rows instead of 2): about 30 s instead of 10 s at the defaults on an i5-8350U laptop. Tests 5 and 6 run only when selected with `-t` or `--hard` (all six tests). Results are comparable only within one benchmark revision; RESULTS.md holds revision 1 (see [Seeds and revisions](#seeds-and-revisions)).

## Notes

You **MUST** run this with ./run.sh, use a [prebuilt binary](#prebuilt-binaries), or build it with exactly `gcc -std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -o branch_bench branch_bench.c`, adding `-Wa,-mbranches-within-32B-boundaries,-malign-branch=jcc+fused+jmp+call+ret+indirect` on x86-64 as `./run.sh` does. Without *-fno-tree-vectorize* and *-fno-if-conversion* the compiler optimizes the mispredictions away, and other optimization levels give results that cannot be compared with [RESULTS.md](RESULTS.md). The x86-64 assembler flags keep every jump in the kernels off 32-byte boundaries, which Intel's JCC-erratum fix slows on Skylake-derived CPUs. `./run.sh` builds and then runs the benchmark with any options you give it (e.g. `./run.sh -n 5 -o report.md`); after that, `./branch_bench` runs it directly. Build with GCC. On x86-64 and AArch64 the kernels of Tests 1, 2, 4 and 5 are written in assembly, so they keep their branch with any compiler, Clang included; Test 6's kernel is C, and Clang (which has no equivalent of *-fno-if-conversion*) makes it branchless, so its Clang timings show no misprediction cost. Other architectures use C versions of all kernels, whose branches depend on the compiler; the banner and report say which kernels ran (`Kernels:`). See [AGENTS.md](AGENTS.md).

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
reference computed without the kernel (Tests 1–4 check every row).

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
Lines (one JSON object per line, schema 1), next to the console output
and any `-o` report. The two must be different files; naming the same
file (also via a hard link or `./`) is an error, and neither file is
emptied until both have opened:

```sh
./branch_bench -n 5 -o report.md --json run.jsonl
```

| `type` | One record per | Main fields |
|---|---|---|
| `meta` (first line) | run | `schema` 1, `revision`, CPU, microcode, arch, kernel, compiler, build flags, commit, `alignment_policy`, `seed` and `test_seeds`, size/reps/trials, `training_runs`, `schedule` (`AB/BA`), `affinity`, `hypervisor`, `smt_siblings`, `governor`, `max_ghz`, `effective_user_cycle_ghz`, and per counter its event type, config and `semantics` (`retired`, `speculative`, `cycles`, `unknown`) |
| `kernel` | kernel | declared `data_branches`, `indirect_calls`, `inner_loop_branches`, unit, tests |
| `variant` | report row | `test`, `variant` (row index), `label`, `kernel`, `work` and `unit` per trial; its input, recorded before measuring: `pass_items` (items per pass), `cond_true` (items per pass whose source condition holds) and `input_hash` (FNV-1a of the input buffer), each `null` where the test does not record it |
| `trial` | measured trial | `test`, `variant`, `pair`, run-wide `seq`, `trial`, `train_runs`, `time_ms`, `result` (the kernel's checksum), `perf_status`, `enabled_ns`/`running_ns`, `counts` (scaled) and `raw_counts` |
| `pair` | compared pair | `a`/`b` variants, `pairs`, `valid`, summed times, `slowdown`, signed `delta` per counter, `effective_cost` and `cost_status` (`resolved`, `inconclusive`, `unavailable`), and `cost_shown` (the cost line was printed) |

Counters print as exact integers; a value that does not exist (a counter
that is not open, a trial whose read failed, a ratio without data) is
`null`, never 0. `.github/scripts/check_export.py run.jsonl --report
report.md` rebuilds every pair summary from the trials and checks the
report's medians, ranges, counter columns, ratios and cost lines against
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
training run of the same variant, so the predictor has seen that
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
./branch_bench -t 5                 # opt-in Test 5 (pattern learnability)
./branch_bench -t 6                 # opt-in Test 6 (correlation distance)
./branch_bench --hard               # all six tests
```

Test 2 needs a `--size` that is a multiple of 16 (whole periods of
every bias pattern), Test 3 a multiple of 64 (equal calls to every
target), and Test 5, which tiles whole periods of its longest
pattern, a multiple of 2048. At any other size such a test prints a skip
note and the other selected tests still run. Test 6 needs `--size` ≥ 64
and `--size` × `--reps` ≥ 65664.

`--size` sets the elements per array (and the calls per trial for
Test 3); `K`/`M` suffixes are allowed. `--reps` sets how many passes each
trial makes over the array (for Test 6, it scales the branch budget). Results are only comparable with
[RESULTS.md](RESULTS.md) at the defaults (`--size 4M --reps 64`).
`--cpu <N>` pins the whole run to CPU N before the counters open, which
keeps the scheduler from moving it between cores (e.g. P- and E-cores).
If the CPU cannot be used, a warning says so, the banner and report say
the pin failed, and the run continues unpinned.

Run `./branch_bench --help` for all options.

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
are revision 1, and this version is revision 3.

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
Test 3 on cores that cannot learn the sequential cycle, the small
difference in work between the variants dominates and the figure is
inflated (~54 cycles for Test 3 on the Raspberry Pi 4).

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
  would miss on every call of the sequential `i % 32` cycle; learning that
  cycle takes an indirect predictor that uses branch history. Haswell
  (i7-4770) and the newer x86 cores with counters in RESULTS.md predict
  it almost perfectly, while the Ivy Bridge i5-3380M and the Cortex-A72
  still miss ~27–28 of every 32 calls (these figures are from revision 1,
  which compared a 32-target cycle with random indices; revision 3 sweeps
  1–64 targets through one call site). The EPYC-Rome VPS (no counters;
  its QEMU model name hides the host core) shows only a 1.20× slowdown,
  like the i5-3380M's 1.17×, so it apparently does not learn the cycle,
  possibly because of hypervisor mitigations. With random indices every
  core is right only by chance, about 1 call in 32. On the Intel machines a missed indirect call costs
  about the same as a missed conditional branch (~23–26 cycles).
- **Test 4** removes the data-dependent branch but not the loop branch,
  which is why its branch count halves. Its time barely changes between
  sorted and random data, but it is not automatically faster: on sorted
  data the branching version wins on the i7-4770 (144 ms vs 195 ms), the
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
- **Test 5** feeds Test 2's branch binary de Bruijn sequences of order
  k = 5 to 11. Each period of 2^k outcomes contains every k-bit word
  exactly once, so the last k outcomes always identify the position, and
  a long period does not need an equally long history. Read the misses
  per element against the period: they show how well this kernel's branch
  learns each pattern, compared with the random row's ~0.5. They do not
  measure a predictor's capacity, history length or table structure. In
  the GCC x86-64 build the loop has two conditional branches per element
  (the data branch and the back-edge), so a global-history predictor would
  see about 2k outcomes per k-bit context, and results also depend on
  table sizes, hashing and training. Below `--size 64K` the summary lines
  show n/a: the random row repeats so often that a predictor may learn it.
- **Test 6** asks how far back a correlated outcome can be and still be
  used. Each block runs branch A on a random outcome, d filler branches
  with a constant outcome, then branch B. In the correlated row B equals A,
  so B is predictable only if the predictor's history still reaches A; in
  the reference row B is random. "Correlation used" is the share of the
  0.5 misses per block that a random B costs which the correlated row
  avoids. The filler
  loop's exit can also mispredict, but equally in both rows, so it cancels.
  d counts filler iterations, not history bits: each adds two conditional
  branches in the GCC build (the filler and the loop back-edge). The
  percentage need not fall steadily with d, and "Largest d with ≥ 90%" is
  a description of this run, not a measured history length. A distance
  with fewer than 65536 blocks per pass shows n/a: its random sequences
  repeat so often that a predictor may learn them, which could make B
  predictable without A. At the defaults every distance has enough. Rows do
  different amounts of work, so compare times only within a pair.
