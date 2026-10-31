# branch bench

[![GitHub Release](https://img.shields.io/github/v/release/juusolatva/branch-bench)](https://github.com/juusolatva/branch-bench/releases/latest)
[![CI](https://github.com/juusolatva/branch-bench/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/juusolatva/branch-bench/actions/workflows/ci.yml)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=juusolatva_branch_bench&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=juusolatva_branch_bench)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=juusolatva_branch_bench&metric=coverage)](https://sonarcloud.io/summary/new_code?id=juusolatva_branch_bench)
[![License: MIT](https://img.shields.io/github/license/juusolatva/branch-bench)](LICENSE)

A simple benchmark for testing out gains from branch prediction or conversely losses from mispredictions.

## The tests

1. **Threshold Sum** sums the bytes above 128 in a sorted and a shuffled copy of the same random array.
2. **Stride Conditional** counts the ones in a 0/1 array that has them at every 4th position, or at random with the same 25% rate.
3. **Indirect Dispatch** calls 32 functions through a pointer table, in sequential `i % 32` order or at random.
4. **Branch vs Branchless** computes Test 1's sum with a conditional jump and with an arithmetic mask, on sorted and random data, and checks that both give the same result.

## Notes

You **MUST** run this with ./run.sh, use a [prebuilt binary](#prebuilt-binaries), or build it with exactly `gcc -std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -o branch_bench branch_bench.c`, adding `-Wa,-mbranches-within-32B-boundaries,-malign-branch=jcc+fused+jmp+call+ret+indirect` on x86-64 as `./run.sh` does. Without *-fno-tree-vectorize* and *-fno-if-conversion* the compiler optimizes the mispredictions away, and other optimization levels give results that cannot be compared with [RESULTS.md](RESULTS.md). The x86-64 assembler flags keep every jump in the kernels off 32-byte boundaries, which Intel's JCC-erratum fix slows on Skylake-derived CPUs. `./run.sh` builds and then runs the benchmark with any options you give it (e.g. `./run.sh -n 5 -o report.md`); after that, `./branch_bench` runs it directly. Build with GCC: Clang has no equivalent of *-fno-if-conversion* and makes Tests 1, 2 and Test 4's branch variant branchless, so its timings show no misprediction cost (see [AGENTS.md](AGENTS.md)).

The benchmark builds on Linux with GCC; on Windows, use WSL2. Other POSIX systems build with GCC too but show times only. On macOS `gcc` is Clang, so install GCC (e.g. from Homebrew) and build with the command above using its versioned `gcc-<N>` (`./run.sh` calls plain `gcc`), without the `-Wa,...` flags: they need the GNU assembler (binutils 2.34 or later), which macOS does not use.

Branch, miss and cycle counts need Linux hardware performance counters, which require `/proc/sys/kernel/perf_event_paranoid` to be 2 or lower. Check it with `cat /proc/sys/kernel/perf_event_paranoid`; distributions such as Debian and Ubuntu default to a higher value, and `sudo sysctl kernel.perf_event_paranoid=2` lowers it until the next reboot. Without counters, and in most VMs and containers, only times are shown: a VM has counters only if its hypervisor provides a virtual PMU (WSL2 can). A counter that cannot be opened is left out of the console output and shows as – in the report. If the kernel time-shares the counters with other users, a note says the counts are scaled estimates.

If a trial's counter read fails or its group never gets scheduled, that
trial still reports its wall-clock time. Its hardware counts show as –
in the report and are excluded from the cost per miss and average clock.

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
including an invalid option or a mismatch between Test 4's branch and
branchless sums.

The report heading shows the CPU's maximum (turbo/boost) clock where
Linux reports it. Where it does not (WSL2, most VMs), the heading is the
model name as is, which on Intel CPUs may end in the base clock
(e.g. `@ 2.90GHz`). The CPU rarely holds its maximum clock under sustained load,
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

A branch misprediction costs a pipeline flush: the work fetched down the
wrong path is thrown away and the front end restarts at the correct
target. The minimum penalty is roughly the pipeline depth, typically
~15–20 cycles on modern out-of-order x86 cores.

When the cycle and miss counters are available, each test prints a
**Cost per miss**: the extra cycles divided by the extra mispredictions
between its predictable and unpredictable variant (for Test 4, its two
branch variants). It is left out when the unpredictable variant adds
fewer than 1000 misses or no cycles. Being measured in cycles, it is
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
on the non-VPS x86 machines in RESULTS.md (the low end, ~9.5 ms, is the
Ryzen 5 9600X under WSL2).

- **Tests 1 and 2** use direct conditional branches (a jump inside the
  loop). Test 2 shows the predictor learning a repeating pattern (every
  4th element taken) but not a random one with the same 25% rate.
- **Test 3** uses indirect calls through a function pointer, so the CPU
  must predict a target address, not just taken/not-taken. A plain branch
  target buffer (BTB) remembers only the last target of each branch and
  would miss on every call of the sequential `i % 32` cycle; learning that
  cycle takes an indirect predictor that uses branch history. Haswell
  (i7-4770) and the newer x86 cores with counters in RESULTS.md predict
  it almost perfectly, while the Ivy Bridge i5-3380M and the Cortex-A72
  still miss ~27–28 of every 32 calls. The EPYC-Rome VPS (no counters;
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
