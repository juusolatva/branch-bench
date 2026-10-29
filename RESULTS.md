# NEW RESULTS

_All entries in this file are benchmark revision 1: one warm-up run per variant (with `-n` > 1) instead of a training run before every trial, unpaired variants, and a cost per miss taken from the median trials. Revision 3 changed the kernels and Tests 1–4 as well, so new runs are not comparable with them._

## Intel(R) Core(TM) i7-4770 CPU @ 3.90 GHz

_2026-10-08_

| Array size | Repetitions | Perf counters |
|---:|---:|---|
| 4194304 elements | 64/trial | available (hardware counts) |

_Median of 6 trials per variant, after one warm-up run. Range is the fastest–slowest trial._

### Test 1 — Threshold Sum

Sorted vs shuffled array, same direct branch. Identical data, different order.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted (predictable, ~0% misses) | 133.3 | 131.9–162.5 | 480429607 | 536871149 | 736 | 0.0% | 0.000 |
| Shuffled (unpredictable, ~50% misses) | 949.9 | 925.2–1018.9 | 3443318623 | 536872670 | 134188257 | 25.0% | 0.500 |

**Slowdown:** 7.12×

**Cost per miss:** 22.1 cycles

### Test 2 — Stride Conditional

Both arrays have ~25% ones; only the pattern differs.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Periodic (every 4th — learnable) | 147.4 | 137.4–172.2 | 534796470 | 536871170 | 1447 | 0.0% | 0.000 |
| Random (same rate — unlearnable) | 732.6 | 691.6–755.7 | 2571232220 | 536871757 | 74569121 | 13.9% | 0.278 |

**Slowdown:** 4.97×

**Cost per miss:** 27.3 cycles

### Test 3 — Indirect Dispatch

32 targets, function-pointer call. The indirect predictor must guess the target address.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/call |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sequential i%32 (learnable cycle) | 6.2 | 5.8–6.4 | 21428676 | 12582959 | 1749 | 0.0% | 0.000 |
| Random index (unlearnable) | 32.9 | 32.4–34.5 | 115886305 | 12582984 | 4061652 | 32.3% | 0.968 |

**Slowdown:** 5.33×

**Cost per miss:** 23.3 cycles

### Test 4 — Branch vs Branchless

Same sum, computed via conditional jump vs. arithmetic mask, on sorted and random data.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted + branch | 144.4 | 142.9–173.1 | 487986440 | 536871499 | 921 | 0.0% | 0.000 |
| Sorted + branchless | 195.3 | 194.6–226.4 | 683571215 | 268435760 | 106 | 0.0% | 0.000 |
| Random + branch | 957.4 | 943.6–1063.9 | 3465778987 | 536872757 | 134175540 | 25.0% | 0.500 |
| Random + branchless | 209.9 | 189.5–224.1 | 751258568 | 268435822 | 107 | 0.0% | 0.000 |

**Branch penalty on random data:** 6.63× vs sorted-branch

**Branchless is consistent:** 1.07× random vs sorted

**Cost per miss:** 22.2 cycles

**Average clock during measurements:** 3.55 GHz

## Intel(R) Core(TM) i5-8350U CPU @ 3.60 GHz

_2026-10-08_

_Built before `run.sh` aligned the kernels' jumps: this Skylake-derived CPU has the JCC erratum, so some kernels may include its penalty. Needs a rerun._

| Array size | Repetitions | Perf counters |
|---:|---:|---|
| 4194304 elements | 64/trial | available (hardware counts) |

_Median of 6 trials per variant, after one warm-up run. Range is the fastest–slowest trial._

### Test 1 — Threshold Sum

Sorted vs shuffled array, same direct branch. Identical data, different order.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted (predictable, ~0% misses) | 225.6 | 217.3–242.0 | 723414702 | 536871247 | 955 | 0.0% | 0.000 |
| Shuffled (unpredictable, ~50% misses) | 1093.3 | 1091.8–1139.0 | 3653858486 | 536872259 | 134118934 | 25.0% | 0.500 |

**Slowdown:** 4.85×

**Cost per miss:** 21.8 cycles

### Test 2 — Stride Conditional

Both arrays have ~25% ones; only the pattern differs.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Periodic (every 4th — learnable) | 270.4 | 264.2–285.7 | 864874741 | 536871324 | 62766 | 0.0% | 0.000 |
| Random (same rate — unlearnable) | 791.6 | 773.4–826.1 | 2593327506 | 536871924 | 76199879 | 14.2% | 0.284 |

**Slowdown:** 2.93×

**Cost per miss:** 22.7 cycles

### Test 3 — Indirect Dispatch

32 targets, function-pointer call. The indirect predictor must guess the target address.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/call |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sequential i%32 (learnable cycle) | 9.7 | 9.5–10.9 | 30337962 | 12582964 | 266111 | 2.1% | 0.063 |
| Random index (unlearnable) | 39.7 | 37.8–43.8 | 129160096 | 12582998 | 4080034 | 32.4% | 0.973 |

**Slowdown:** 4.10×

**Cost per miss:** 25.9 cycles

### Test 4 — Branch vs Branchless

Same sum, computed via conditional jump vs. arithmetic mask, on sorted and random data.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted + branch | 224.4 | 220.8–239.2 | 716134534 | 536871280 | 1083 | 0.0% | 0.000 |
| Sorted + branchless | 189.5 | 188.0–200.3 | 582170332 | 268435762 | 522 | 0.0% | 0.000 |
| Random + branch | 1094.2 | 1077.0–1116.7 | 3638314034 | 536872282 | 134125347 | 25.0% | 0.500 |
| Random + branchless | 191.1 | 189.7–220.0 | 591265728 | 268435790 | 545 | 0.0% | 0.000 |

**Branch penalty on random data:** 4.88× vs sorted-branch

**Branchless is consistent:** 1.01× random vs sorted

**Cost per miss:** 21.8 cycles

**Average clock during measurements:** 3.25 GHz

## Raspberry Pi 4 Model B Rev 1.5 @ 1.60 GHz

_2026-10-08_

| Array size | Repetitions | Perf counters |
|---:|---:|---|
| 4194304 elements | 64/trial | available (hardware counts) |

_Median of 6 trials per variant, after one warm-up run. Range is the fastest–slowest trial._

### Test 1 — Threshold Sum

Sorted vs shuffled array, same direct branch. Identical data, different order.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted (predictable, ~0% misses) | 536.0 | 535.8–539.8 | 852894170 | 536872412 | 436 | 0.0% | 0.000 |
| Shuffled (unpredictable, ~50% misses) | 2204.9 | 2204.1–2206.7 | 3515703285 | 646376023 | 134085205 | 20.7% | 0.500 |

**Slowdown:** 4.11×

**Cost per miss:** 19.9 cycles

### Test 2 — Stride Conditional

Both arrays have ~25% ones; only the pattern differs.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Periodic (every 4th — learnable) | 800.8 | 800.7–801.2 | 1275207139 | 536872210 | 301 | 0.0% | 0.000 |
| Random (same rate — unlearnable) | 1645.7 | 1640.8–1657.6 | 2622568069 | 598472992 | 84191614 | 14.1% | 0.314 |

**Slowdown:** 2.06×

**Cost per miss:** 16.0 cycles

### Test 3 — Indirect Dispatch

32 targets, function-pointer call. The indirect predictor must guess the target address.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/call |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sequential i%32 (learnable cycle) | 57.3 | 57.3–57.4 | 91501693 | 12583109 | 3538973 | 28.1% | 0.844 |
| Random index (unlearnable) | 75.1 | 75.0–75.2 | 119786977 | 16692431 | 4063962 | 24.3% | 0.969 |

**Slowdown:** 1.31×

**Cost per miss:** 53.9 cycles

### Test 4 — Branch vs Branchless

Same sum, computed via conditional jump vs. arithmetic mask, on sorted and random data.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted + branch | 535.9 | 535.8–536.0 | 852934017 | 536872375 | 413 | 0.0% | 0.000 |
| Sorted + branchless | 423.9 | 423.7–424.1 | 674690055 | 268435966 | 73 | 0.0% | 0.000 |
| Random + branch | 2205.6 | 2205.5–2206.1 | 3516908781 | 646604146 | 134229474 | 20.8% | 0.500 |
| Random + branchless | 423.9 | 423.7–424.1 | 674553531 | 268435956 | 76 | 0.0% | 0.000 |

**Branch penalty on random data:** 4.12× vs sorted-branch

**Branchless is consistent:** 1.00× random vs sorted

**Cost per miss:** 19.8 cycles

**Average clock during measurements:** 1.59 GHz


# OLD RESULTS

## Intel(R) Core(TM) i5-3380M CPU @ 3.60 GHz

_2026-08-21_

| Array size | Repetitions | Perf counters |
|---:|---:|---|
| 4194304 elements | 64/trial | available (hardware counts) |

### Test 1 — Threshold Sum

Sorted vs shuffled array, same direct branch. Identical data, different order.

| Variant | Time (ms) | Branches | Misses | Miss % |
|---|---:|---:|---:|---:|
| Sorted (predictable, ~0% misses) | 155.3 | 536871305 | 932 | 0.0% |
| Shuffled (unpredictable, ~50% misses) | 934.3 | 536871987 | 134132567 | 25.0% |

**Slowdown:** 6.02×

### Test 2 — Stride Conditional

Both arrays have ~25% ones; only the pattern differs.

| Variant | Time (ms) | Branches | Misses | Miss % |
|---|---:|---:|---:|---:|
| Periodic (every 4th — learnable) | 205.8 | 536871271 | 10329 | 0.0% |
| Random (same rate — unlearnable) | 664.7 | 536871714 | 70296278 | 13.1% |

**Slowdown:** 3.23×

### Test 3 — Indirect Dispatch

32 targets, function-pointer call. BTB must predict the target address.

| Variant | Time (ms) | Branches | Misses | Miss % |
|---|---:|---:|---:|---:|
| Sequential i%32 (BTB learns cycle) | 28.2 | 12583001 | 3669784 | 29.2% |
| Random index (BTB always wrong) | 33.0 | 12583004 | 4063349 | 32.3% |

**Slowdown:** 1.17×

### Test 4 — Branch vs Branchless

Same sum, computed via conditional jump vs. arithmetic mask, on sorted and random data.

| Variant | Time (ms) | Branches | Misses | Miss % |
|---|---:|---:|---:|---:|
| Sorted + branch | 154.9 | 536871193 | 927 | 0.0% |
| Sorted + branchless | 207.1 | 268435787 | 84 | 0.0% |
| Random + branch | 934.5 | 536871980 | 134193939 | 25.0% |
| Random + branchless | 206.1 | 268435798 | 80 | 0.0% |

**Branch penalty on random data:** 6.03× vs sorted-branch

**Branchless is consistent:** 0.99× random vs sorted

## AMD Ryzen 5 9600X 6-Core Processor @ 5.50 GHz (WSL2)

_2026-08-21_

| Array size | Repetitions | Perf counters |
|---:|---:|---|
| 4194304 elements | 64/trial | available (hardware counts) |

### Test 1 — Threshold Sum

Sorted vs shuffled array, same direct branch. Identical data, different order.

| Variant | Time (ms) | Branches | Misses | Miss % |
|---|---:|---:|---:|---:|
| Sorted (predictable, ~0% misses) | 50.8 | 536871085 | 1543 | 0.0% |
| Shuffled (unpredictable, ~50% misses) | 656.3 | 536871714 | 134161142 | 25.0% |

**Slowdown:** 12.91×

### Test 2 — Stride Conditional

Both arrays have ~25% ones; only the pattern differs.

| Variant | Time (ms) | Branches | Misses | Miss % |
|---|---:|---:|---:|---:|
| Periodic (every 4th — learnable) | 53.7 | 536871088 | 6230 | 0.0% |
| Random (same rate — unlearnable) | 390.9 | 536871441 | 71468641 | 13.3% |

**Slowdown:** 7.28×

### Test 3 — Indirect Dispatch

32 targets, function-pointer call. BTB must predict the target address.

| Variant | Time (ms) | Branches | Misses | Miss % |
|---|---:|---:|---:|---:|
| Sequential i%32 (BTB learns cycle) | 4.6 | 12582975 | 254 | 0.0% |
| Random index (BTB always wrong) | 28.9 | 12582998 | 4063451 | 32.3% |

**Slowdown:** 6.26×

### Test 4 — Branch vs Branchless

Same sum, computed via conditional jump vs. arithmetic mask, on sorted and random data.

| Variant | Time (ms) | Branches | Misses | Miss % |
|---|---:|---:|---:|---:|
| Sorted + branch | 50.3 | 536871086 | 1240 | 0.0% |
| Sorted + branchless | 66.6 | 268435647 | 625 | 0.0% |
| Random + branch | 655.9 | 536871730 | 134191545 | 25.0% |
| Random + branchless | 66.5 | 268435643 | 839 | 0.0% |

**Branch penalty on random data:** 13.03× vs sorted-branch

**Branchless is consistent:** 1.00× random vs sorted


# VPS RESULTS

## AMD EPYC-Rome-v5 Processor

_2026-10-08_

| Array size | Repetitions | Perf counters |
|---:|---:|---|
| 4194304 elements | 64/trial | unavailable (wall-clock only) |

_Median of 6 trials per variant, after one warm-up run. Range is the fastest–slowest trial._

### Test 1 — Threshold Sum

Sorted vs shuffled array, same direct branch. Identical data, different order.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted (predictable, ~0% misses) | 182.4 | 167.3–235.8 | – | – | – | – | – |
| Shuffled (unpredictable, ~50% misses) | 1046.6 | 1035.4–1067.0 | – | – | – | – | – |

**Slowdown:** 5.74×

### Test 2 — Stride Conditional

Both arrays have ~25% ones; only the pattern differs.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Periodic (every 4th — learnable) | 222.6 | 190.5–274.2 | – | – | – | – | – |
| Random (same rate — unlearnable) | 619.7 | 613.8–666.6 | – | – | – | – | – |

**Slowdown:** 2.78×

### Test 3 — Indirect Dispatch

32 targets, function-pointer call. The indirect predictor must guess the target address.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/call |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sequential i%32 (learnable cycle) | 29.9 | 29.9–31.2 | – | – | – | – | – |
| Random index (unlearnable) | 35.8 | 35.2–36.4 | – | – | – | – | – |

**Slowdown:** 1.20×

### Test 4 — Branch vs Branchless

Same sum, computed via conditional jump vs. arithmetic mask, on sorted and random data.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted + branch | 156.8 | 144.8–175.4 | – | – | – | – | – |
| Sorted + branchless | 159.2 | 158.2–164.9 | – | – | – | – | – |
| Random + branch | 1048.3 | 1041.5–1058.7 | – | – | – | – | – |
| Random + branchless | 164.1 | 158.2–169.3 | – | – | – | – | – |

**Branch penalty on random data:** 6.69× vs sorted-branch

**Branchless is consistent:** 1.03× random vs sorted

## Intel Xeon Processor (Skylake)

_2026-10-08_

_Built before `run.sh` aligned the kernels' jumps: Skylake has the JCC erratum, so some kernels may include its penalty. Needs a rerun._

| Array size | Repetitions | Perf counters |
|---:|---:|---|
| 4194304 elements | 64/trial | unavailable (wall-clock only) |

_Median of 6 trials per variant, after one warm-up run. Range is the fastest–slowest trial._

### Test 1 — Threshold Sum

Sorted vs shuffled array, same direct branch. Identical data, different order.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted (predictable, ~0% misses) | 318.2 | 314.8–366.9 | – | – | – | – | – |
| Shuffled (unpredictable, ~50% misses) | 2409.6 | 2399.9–2465.6 | – | – | – | – | – |

**Slowdown:** 7.57×

### Test 2 — Stride Conditional

Both arrays have ~25% ones; only the pattern differs.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Periodic (every 4th — learnable) | 320.3 | 314.8–371.6 | – | – | – | – | – |
| Random (same rate — unlearnable) | 1811.1 | 1758.1–1870.0 | – | – | – | – | – |

**Slowdown:** 5.65×

### Test 3 — Indirect Dispatch

32 targets, function-pointer call. The indirect predictor must guess the target address.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/call |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sequential i%32 (learnable cycle) | 8.9 | 8.6–59.4 | – | – | – | – | – |
| Random index (unlearnable) | 90.5 | 40.6–91.2 | – | – | – | – | – |

**Slowdown:** 10.23×

### Test 4 — Branch vs Branchless

Same sum, computed via conditional jump vs. arithmetic mask, on sorted and random data.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted + branch | 319.4 | 317.1–389.8 | – | – | – | – | – |
| Sorted + branchless | 389.7 | 338.7–391.9 | – | – | – | – | – |
| Random + branch | 2397.8 | 2386.8–2477.4 | – | – | – | – | – |
| Random + branchless | 392.3 | 342.8–478.7 | – | – | – | – | – |

**Branch penalty on random data:** 7.51× vs sorted-branch

**Branchless is consistent:** 1.01× random vs sorted
