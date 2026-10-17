# branch bench

[![CI](https://github.com/juusolatva/branch_bench/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/juusolatva/branch_bench/actions/workflows/ci.yml)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=juusolatva_branch_bench&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=juusolatva_branch_bench)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=juusolatva_branch_bench&metric=coverage)](https://sonarcloud.io/summary/new_code?id=juusolatva_branch_bench)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A simple benchmark for testing out gains from branch prediction or conversely losses from mispredictions.

## Notes

You **MUST** run this with ./run.sh or use the flags *-fno-tree-vectorize* *-fno-if-conversion* when compiling or the compiler will optimize the mispredictions away. Build with GCC: Clang has no equivalent of *-fno-if-conversion* (see [AGENTS.md](AGENTS.md)).

The benchmark needs Linux (or another POSIX system) to build. Branch, miss and cycle counts need Linux hardware performance counters, which require `/proc/sys/kernel/perf_event_paranoid` to be 2 or lower. Without them, and in most VMs and containers, only times are shown.

## Saving results

Console output is meant to be read as it scrolls by. To get a clean,
table-formatted Markdown report instead use `-o`/`--output`:

```sh
./branch_bench -o report.md
```

This writes the report file in addition to the normal
console output.

The report heading shows the CPU's maximum (turbo/boost) clock where
Linux reports it. The CPU rarely holds that clock under sustained load,
so the report also ends with the **average clock during measurements**,
calculated from the cycle counter. That is the clock the results were
actually produced at, and it often explains why timings differ between
runs on the same machine.

## Repeated measurements

By default each variant is measured once. For steadier numbers use
`-n`/`--trials`:

```sh
./branch_bench -n 5 -o report.md
```

Each variant then gets one untimed warm-up run followed by 5 measured
trials. The trial with the median time is reported, together with the
fastest–slowest range, so you can see how noisy the run was. Expect the
run to take about N+1 times as long.

## Choosing tests and sizes

```sh
./branch_bench -t 3                 # run only Test 3 (repeat -t for more)
./branch_bench --size 1M --reps 8   # smaller, quicker run (e.g. small boards)
```

`--size` sets the elements per array (and the calls per trial for
Test 3); `K`/`M` suffixes are allowed. `--reps` sets how many passes each
trial makes over the array. Results are only comparable with
[RESULTS.md](RESULTS.md) at the defaults (`--size 4M --reps 64`).
Run `./branch_bench --help` for all options.

## Interpreting the results

A branch misprediction costs a pipeline flush: the work fetched down the
wrong path is thrown away and the front end restarts at the correct
target. The minimum penalty is roughly the pipeline depth, typically
~15–20 cycles on modern out-of-order x86 cores.

When the CPU cycle counter is available, each test prints a **Cost per
miss**: the extra cycles divided by the extra mispredictions between its
predictable and unpredictable variant. Being measured in cycles, it is
far less affected by the clock speed the CPU happened to run at than the
times are, though not entirely: part of a miss's cost can be waiting on
memory, which takes a fixed time rather than a fixed number of cycles.
In [RESULTS.md](RESULTS.md) it is ~22 cycles for Test 1 on the Intel
machines and ~20 on the Raspberry Pi 4's Cortex-A72. For the older
results, which predate the cycle counter, estimates from the times and
listed clock speeds put it at ~20–25 cycles for Test 1.

The cost per miss is only meaningful when the unpredictable variant adds
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
on the bare-metal x86 machines in RESULTS.md.

- **Tests 1 and 2** use direct conditional branches (a jump inside the
  loop). Test 2 shows the predictor learning a repeating pattern (every
  4th element taken) but not a random one with the same 25% rate.
- **Test 3** uses indirect calls through a function pointer, so the CPU
  must predict a target address, not just taken/not-taken. A plain branch
  target buffer (BTB) remembers only the last target of each branch and
  would miss on every call of the sequential `i % 32` cycle; learning that
  cycle takes an indirect predictor that uses branch history. Haswell
  (i7-4770) and newer cores in RESULTS.md predict it almost perfectly,
  while the Ivy Bridge i5-3380M and the Cortex-A72 still miss ~27–28 of
  every 32 calls. With random indices every core is right only by chance,
  about 1 call in 32. On the Intel machines a missed indirect call costs
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
  jumps as Haswell's can. The Raspberry Pi 4's reason has not been
  investigated.
