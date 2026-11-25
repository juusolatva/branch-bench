# branch bench

[![GitHub Release](https://img.shields.io/github/v/release/juusolatva/branch-bench)](https://github.com/juusolatva/branch-bench/releases/latest)
[![CI](https://github.com/juusolatva/branch-bench/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/juusolatva/branch-bench/actions/workflows/ci.yml)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=juusolatva_branch_bench&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=juusolatva_branch_bench)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=juusolatva_branch_bench&metric=coverage)](https://sonarcloud.io/summary/new_code?id=juusolatva_branch_bench)
[![License: MIT](https://img.shields.io/github/license/juusolatva/branch-bench)](LICENSE)

A simple benchmark for testing out gains from branch prediction or conversely losses from mispredictions.

## The tests

1. **Threshold Sum** sums the bytes above 128 in a sorted and a shuffled copy of the same random array.
2. **Stride Conditional** counts the ones in a 0/1 array. It compares a one at every 4th position with a 25% chance of a one at each position.
3. **Indirect Dispatch** calls 32 functions through a pointer table, in sequential `i % 32` order or at random.
4. **Branch vs Branchless** computes Test 1's sum with a conditional jump and with an arithmetic mask, on sorted and random data. It checks that both give the same result.

## Building and running

Use one of these:

- `./run.sh`, which builds the benchmark and runs it with any options you give it
  (e.g. `./run.sh -n 5 -o report.md`). After that, `./branch_bench` runs it directly.
- a [prebuilt binary](#prebuilt-binaries);
- exactly this command, adding
  `-Wa,-mbranches-within-32B-boundaries,-malign-branch=jcc+fused+jmp+call+ret+indirect`
  on x86-64 as `./run.sh` does:

  ```sh
  gcc -std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -o branch_bench branch_bench.c
  ```

Without `-fno-tree-vectorize` and `-fno-if-conversion`, the compiler optimizes
the mispredictions away. Other optimization levels give results that can't be
compared with [RESULTS.md](RESULTS.md). The x86-64 assembler flags keep the
kernels' jumps off 32-byte boundaries, which Intel's JCC-erratum fix slows on
Skylake-derived CPUs.

Build with GCC. Clang has no equivalent of `-fno-if-conversion` and makes
Tests 1 and 2 and Test 4's branch variant branchless, so its timings show no
misprediction cost.

The benchmark runs on Linux; on Windows, use WSL2. Other POSIX systems build
with GCC too, but they show times only. On macOS, `gcc` is Clang, so install GCC
(e.g. from Homebrew) and build with the command above using its versioned
`gcc-<N>`, without the `-Wa,...` flags. Those flags need the GNU assembler
(binutils 2.34 or later), which macOS does not use.

### Prebuilt binaries

Each [release](https://github.com/juusolatva/branch-bench/releases) has static
Linux binaries for x86-64 and arm64 (aarch64). They are built with the newest
GCC release and the flags above. Because they are static, they run on any Linux
of that architecture, including musl systems such as OpenWrt. Download the
binary for your machine (`uname -m`) and its `.sha256` file, set `tag` to that
release, and run:

```sh
tag=vX.Y.Z
bin=branch_bench-$tag-linux-$(uname -m)
sha256sum -c "$bin.sha256" && chmod +x "$bin" && ./"$bin" -n 6 -o report.md
```

`gh attestation verify <file> --repo juusolatva/branch-bench` checks that a
binary was built by this repository's release workflow.

## Options

| Option | Effect |
|---|---|
| `-o`, `--output <file>` | also write a Markdown report to the file |
| `-n`, `--trials <N>` | measure each variant N times (1–99) after one untimed warm-up and report the median trial (default 1, no warm-up) |
| `-t`, `--test <N>` | run only test N (1–4); repeat for several |
| `--size <N>` | elements per array, and calls per trial in Test 3 (default 4M, max 1024M) |
| `--reps <N>` | passes over the array per trial in Tests 1, 2 and 4 (default 64, max 100000) |
| `-h`, `--help` | show usage |

```sh
./branch_bench -n 5 -o report.md    # steadier numbers, saved as a report
./branch_bench -t 3                 # only Test 3
./branch_bench --size 1M --reps 8   # quicker run, e.g. on small boards
```

- `--size` and `--reps` accept `K`/`M` suffixes, case-insensitive powers of 1024.
  Results can only be compared with [RESULTS.md](RESULTS.md) at the defaults.
- With `-n` above 1, the run takes about N+1 times as long. The console and the
  report show the trial with the median time (the lower middle one for an even N)
  and the fastest–slowest range. Time and hardware counts always come from the
  same trial.
- The program exits with status 1 on an invalid option, on a failure to write the
  report, and when Test 4's two sums differ.

### Reports

`-o` writes a Markdown report alongside the console output. An existing file
is overwritten, and a new one is readable only by you.

The report heading shows the CPU's maximum (boost) clock where Linux reports
it. Where it does not (WSL2, most VMs), the heading is the model name as is,
which on Intel CPUs may end in the base clock. CPUs rarely hold their maximum
clock under sustained load, so when the cycle counter works, the console and
the report also end with the **average clock during measurements**. That is the
clock the results were actually produced at, and it often explains why timings
differ between runs on the same machine.

## Hardware counters

Branch, miss and cycle counts need Linux hardware performance counters, which
require `/proc/sys/kernel/perf_event_paranoid` to be 2 or lower. Debian and
Ubuntu default to a higher value. `sudo sysctl kernel.perf_event_paranoid=2`
lowers it until the next reboot. Most VMs and containers have no counters, so
they show only times. A VM has counters only if its hypervisor provides a
virtual PMU (WSL2 can).

- A counter that can't be opened is left out of the console output and shows
  as – in the report.
- If the kernel time-shares the counters with other users, a note says the
  counts are scaled estimates.
- If a trial's counter read fails, that trial still reports its time. Its counts
  are marked unavailable and are left out of the cost per miss and the average
  clock.
- On ARM, a generic event that fails is retried as its raw PMU event: `0x12`
  (`BR_PRED`) for branches, `0x10` (`BR_MIS_PRED`) for misses and `0x11`
  (`CPU_CYCLES`) for cycles. The banner's **Raw PMU events** line and the report's
  **Raw ARM PMU events** line name the counters that fell back.

## Interpreting the results

A branch misprediction costs a pipeline flush. The work fetched down the wrong
path is thrown away, and the front end restarts at the correct target. The
minimum penalty is roughly the pipeline depth, typically ~15–20 cycles on modern
out-of-order x86 cores.

- **Slowdown** (Tests 1–3) is the unpredictable variant's time divided by the
  predictable one's. In Test 4, the **Branch penalty on random data** divides the
  branching time on random data by the branching time on sorted data. **Branchless is
  consistent** makes the same comparison for the branchless version; a value
  near 1 means similar times.
- **Cost per miss** is the extra cycles divided by the extra mispredictions
  between the predictable and unpredictable variants (in Test 4, between the two
  branch variants). It needs the cycle and miss counters, and it is left out when
  the unpredictable variant adds fewer than 1000 misses or no cycles. Because it
  is measured in cycles, the clock speed affects it much less than the times,
  though not entirely, since some of a miss's cost can be waiting on memory. In
  RESULTS.md it is ~22 cycles for Test 1 on the Intel machines and ~20 on the
  Raspberry Pi 4. It is only meaningful when the unpredictable variant adds many
  misses. When both variants miss about equally often, as in Test 3 on cores that
  can't learn the sequential cycle, the figure is inflated (~54 cycles on the Pi 4).
- **Miss %** is misses divided by *all* counted branches. That includes the
  loop's own back-edge branch and, in Test 3, the leaf function's return. Test 1's
  shuffled branch is mispredicted ~50% of the time, but with two branches per
  element that shows as ~25%. **Misses per element** (per call in Test 3) is about
  0.500 for Test 1 shuffled. It approximates the data branch's miss probability.
- On ARM the branch counter may also count speculatively executed branches on the
  wrong path. The unpredictable variants then show more branches and a lower
  Miss % (20.7% instead of 25% for Test 1 shuffled on the Pi 4). The raw `BR_PRED`
  event counts this way. Misses per element is unaffected.

**Sanity check for Test 1** (default size): each repetition walks 4,194,304
elements. At a 50% miss rate that is ~2.1M misses. At ~20 cycles each, that makes
~42M cycles, or ~10 ms at 4 GHz. This matches the measured shuffled-minus-sorted
difference of ~9–14 ms per repetition on the non-VPS x86 machines in RESULTS.md.

What the tests show:

- **Tests 1 and 2** use direct conditional branches. Test 2 shows the predictor
  learning a repeating pattern (every 4th element) but not a random one.
- **Test 3** uses indirect calls, so the CPU must predict a target address. A
  plain branch target buffer remembers only each branch's last target, and it
  would miss on every call of the `i % 32` cycle. Learning the cycle takes an
  indirect predictor that uses branch history. Haswell (i7-4770) and the newer x86
  cores with counters in RESULTS.md predict it almost perfectly. The Ivy Bridge i5-3380M and the
  Cortex-A72 still miss ~27–28 of every 32 calls. The EPYC-Rome VPS (no counters)
  shows only a 1.20× slowdown, like the i5-3380M's 1.17×, so it apparently does not
  learn the cycle, possibly because of hypervisor mitigations. With random indices,
  every core is right only by chance, about 1 call in 32. On the Intel machines, a
  missed indirect call costs about the same as a missed conditional branch
  (~23–26 cycles).
- **Test 4** removes the data branch but not the loop branch, which is why its
  branch count halves. Branchless takes about the same time on sorted and random
  data, but it is not automatically faster. On sorted data, the branching version
  wins on the i7-4770 (144 ms vs 195 ms), the i5-3380M and the Ryzen 5 9600X: a
  correctly predicted branch is nearly free, while branchless always does the
  extra work. The i5-8350U and the Raspberry Pi 4 are exceptions. On the i5-8350U,
  each correctly predicted *taken* branch costs about 2 cycles, and the sorted loop
  takes 1.5 taken branches per element. The likely, but unconfirmed, reason is that
  Skylake and Kaby Lake have their loop stream detector disabled by microcode
  (`lsd.uops` reads 0). The Pi 4's reason has not been investigated.

## Adding results

The current entries in [RESULTS.md](RESULTS.md) are default-size runs with
`-n 6` on an otherwise idle machine. Write the report to a separate file (never
`-o RESULTS.md`) and paste its `## <CPU>` section under `# NEW RESULTS`, or under
`# VPS RESULTS` for a virtual server. `# OLD RESULTS` holds runs from an earlier
version. Suffixes such as `(WSL2)` in headings, and clocks where Linux reports
none, are added by hand.

## Development

[TESTING.md](TESTING.md) covers the checks to run before committing, and CI.
[AGENTS.md](AGENTS.md) covers code conventions and releases. To report a
vulnerability, see [SECURITY.md](SECURITY.md).
