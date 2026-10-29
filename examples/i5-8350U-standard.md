## Intel(R) Core(TM) i5-8350U CPU @ 3.60 GHz

_2026-10-29_

_Benchmark revision 4._

_Profile: standard._

_Kernels: asm-x86_64 for the branch kernels of Tests 1, 2 and 4–6; the other kernels are C._

| Array size | Repetitions | Perf counters | Compiler |
|---:|---:|---|---|
| 4194304 elements | 64/trial | available (hardware counts) | GCC 16.2.1 20260819 (Red Hat 16.2.1-2) |

_Build flags: `-std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -Wa,-mbranches-within-32B-boundaries,-malign-branch=jcc+fused+jmp+call+ret+indirect`_

_Commit: `v1.1.0-27-gc4f7b15`_

_Median of 6 trials per variant; each trial follows one untimed training run, and paired variants alternate AB/BA. Range is the fastest–slowest trial._

_All tests were run (--hard), including the hard tests 5 and 6._

_Pinned to CPU 2._

### Test 1 — Threshold Sum

Sorted vs shuffled copies of one random byte array, same branch kernel.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted | 196.8 | 194.4–209.2 | 695194019 | 536871227 | 680 | 0.0% | 0.000 |
| Shuffled | 1009.7 | 1006.7–1023.9 | 3584630971 | 536872078 | 134179565 | 25.0% | 0.500 |

**Slowdown:** 5.09×

**Effective extra cycles per additional miss:** 21.6 cycles

_Elements > 128: 2081995 of 4194304 per pass (49.6%)._

### Test 2 — Bias and Ordering

Same number of ones per bias in periodic, clustered and shuffled order, plus constant controls; one branch site.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1/16 periodic 1000000000000000 | 230.8 | 230.3–237.2 | 820039037 | 536871263 | 6382 | 0.0% | 0.000 |
| 1/16 clustered | 229.1 | 227.2–231.0 | 813349321 | 536871271 | 3775 | 0.0% | 0.000 |
| 1/16 shuffled | 343.4 | 341.5–347.0 | 1216029136 | 536871381 | 17926008 | 3.3% | 0.067 |
| 1/4 periodic 1000 | 232.5 | 229.6–242.9 | 821919009 | 536871295 | 79781 | 0.0% | 0.000 |
| 1/4 clustered | 216.9 | 215.1–329.0 | 760844049 | 536871273 | 6689 | 0.0% | 0.000 |
| 1/4 shuffled | 713.3 | 702.5–825.5 | 2532505088 | 536871782 | 76346652 | 14.2% | 0.284 |
| 1/2 periodic 10 | 265.3 | 244.5–295.9 | 824141423 | 536871314 | 2573 | 0.0% | 0.000 |
| 1/2 clustered | 204.9 | 199.8–211.7 | 680502591 | 536871268 | 29749 | 0.0% | 0.000 |
| 1/2 shuffled | 1117.6 | 1054.0–1335.2 | 3786022223 | 536872372 | 134216755 | 25.0% | 0.500 |
| 3/4 periodic 1110 | 235.2 | 234.1–252.5 | 819615912 | 536871307 | 1266 | 0.0% | 0.000 |
| 3/4 clustered | 179.7 | 177.5–183.3 | 619552117 | 536871221 | 21540 | 0.0% | 0.000 |
| 3/4 shuffled | 665.3 | 662.1–675.5 | 2358746734 | 536871779 | 76682676 | 14.3% | 0.286 |
| 15/16 periodic 1111111111111110 | 178.4 | 178.4–182.7 | 618067151 | 536871215 | 2796 | 0.0% | 0.000 |
| 15/16 clustered | 172.8 | 163.8–263.8 | 582658586 | 536871226 | 5993 | 0.0% | 0.000 |
| 15/16 shuffled | 288.3 | 286.1–327.7 | 1017273051 | 536871344 | 18992162 | 3.5% | 0.071 |
| Constant 0 | 353.7 | 275.0–385.3 | 1031887856 | 536871876 | 1613 | 0.0% | 0.000 |
| Constant 1 | 224.9 | 208.4–321.3 | 663376581 | 536871267 | 325 | 0.0% | 0.000 |

**1/16 shuffled vs periodic:** 1.47×

**1/16 shuffled vs clustered:** 1.50×

**1/4 shuffled vs periodic:** 3.07×

**1/4 shuffled vs clustered:** 3.06×

**1/2 shuffled vs periodic:** 4.50×

**1/2 shuffled vs clustered:** 5.44×

**3/4 shuffled vs periodic:** 2.80×

**3/4 shuffled vs clustered:** 3.69×

**15/16 shuffled vs periodic:** 1.62×

**15/16 shuffled vs clustered:** 1.53×

**Effective extra cycles per additional miss:** 22.2 cycles

### Test 3 — Indirect Dispatch

One indirect call site over 1–64 leaf targets; cyclic vs shuffled index sequences with equal target counts.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/call |
|---|---:|---:|---:|---:|---:|---:|---:|
| T= 1 cyclic | 10.5 | 10.2–11.9 | 29987032 | 12582967 | 103600 | 0.8% | 0.025 |
| T= 1 shuffled | 10.7 | 10.3–10.8 | 30523027 | 12582969 | 66134 | 0.5% | 0.016 |
| T= 2 cyclic | 11.0 | 10.4–13.9 | 30256207 | 12582964 | 41920 | 0.3% | 0.010 |
| T= 2 shuffled | 30.7 | 29.6–32.1 | 91263628 | 12582987 | 2262855 | 18.0% | 0.540 |
| T= 4 cyclic | 7.8 | 7.5–8.8 | 24476434 | 12582960 | 15555 | 0.1% | 0.004 |
| T= 4 shuffled | 31.3 | 30.3–36.9 | 104725687 | 12582988 | 3183705 | 25.3% | 0.759 |
| T= 8 cyclic | 12.1 | 8.0–18.2 | 33202419 | 12582965 | 40426 | 0.3% | 0.010 |
| T= 8 shuffled | 44.0 | 41.8–51.1 | 129785138 | 12583002 | 3710425 | 29.5% | 0.885 |
| T=16 cyclic | 8.1 | 7.6–14.8 | 25716109 | 12582961 | 51551 | 0.4% | 0.012 |
| T=16 shuffled | 38.0 | 37.5–45.2 | 123329764 | 12582994 | 3944050 | 31.3% | 0.940 |
| T=32 cyclic | 8.3 | 8.0–9.4 | 26798626 | 12582961 | 78601 | 0.6% | 0.019 |
| T=32 shuffled | 38.8 | 36.8–39.1 | 129156521 | 12583000 | 4073805 | 32.4% | 0.971 |
| T=64 cyclic | 8.0 | 7.5–9.5 | 25550582 | 12582961 | 24978 | 0.2% | 0.006 |
| T=64 shuffled | 37.3 | 36.7–41.7 | 126780183 | 12582994 | 4130441 | 32.8% | 0.985 |

**T=1 shuffled vs cyclic:** 0.99×

**T=2 shuffled vs cyclic:** 2.63×

**T=4 shuffled vs cyclic:** 4.11×

**T=8 shuffled vs cyclic:** 3.59×

**T=16 shuffled vs cyclic:** 4.30×

**T=32 shuffled vs cyclic:** 4.54×

**T=64 shuffled vs cyclic:** 4.59×

**Effective extra cycles per additional miss:** 25.2 cycles

### Test 4 — Branch vs Branchless

Same sum via Test 1's branch kernel and an arithmetic mask, on sorted and shuffled copies of one byte array. A branchless vs branch ratio below 1 means branchless is faster.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted + branch | 211.3 | 201.2–258.5 | 708924588 | 536871283 | 990 | 0.0% | 0.000 |
| Sorted + branchless | 174.7 | 171.5–201.0 | 561918159 | 268435768 | 201 | 0.0% | 0.000 |
| Shuffled + branch | 1039.9 | 1011.0–1239.2 | 3620872052 | 536872109 | 134184183 | 25.0% | 0.500 |
| Shuffled + branchless | 168.8 | 166.9–175.8 | 559404674 | 268435749 | 207 | 0.0% | 0.000 |

**Branch, shuffled vs sorted:** 4.93×

**Sorted, branchless vs branch:** 0.83×

**Shuffled, branchless vs branch:** 0.16×

**Effective extra cycles per additional miss:** 21.8 cycles

_Elements > 128: 2080598 of 4194304 per pass (49.6%)._

### Test 5 — Pattern Learnability Matrix

N static data branches per block, each reading its own repeating L-bit random word (block-major); constant, alternating, de Bruijn and balanced-reference controls at N = 1.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/branch |
|---|---:|---:|---:|---:|---:|---:|---:|
| N= 1 L= 8 E=512 | 2.8 | 2.8–2.9 | 9981116 | 8405038 | 33044 | 0.4% | 0.008 |
| N= 1 L= 16 E=512 | 3.7 | 3.6–3.7 | 12892869 | 8405041 | 17030 | 0.2% | 0.004 |
| N= 1 L= 32 E=512 | 3.6 | 3.6–3.7 | 12879201 | 8405040 | 32818 | 0.4% | 0.008 |
| N= 1 L= 64 E=512 | 3.3 | 3.3–3.4 | 11762720 | 8405038 | 32489 | 0.4% | 0.008 |
| N= 1 L= 128 E=512 | 3.5 | 3.4–3.6 | 12091917 | 8405039 | 22960 | 0.3% | 0.005 |
| N= 1 L= 256 E=512 | 3.5 | 3.4–3.9 | 12167792 | 8405038 | 10569 | 0.1% | 0.003 |
| N= 4 L= 8 E=512 | 1.5 | 1.5–1.5 | 4945560 | 5247020 | 4112 | 0.1% | 0.001 |
| N= 4 L= 16 E=512 | 2.1 | 2.1–2.1 | 6982312 | 5247021 | 8147 | 0.2% | 0.002 |
| N= 4 L= 32 E=512 | 1.9 | 1.8–1.9 | 6417913 | 5247023 | 4201 | 0.1% | 0.001 |
| N= 4 L= 64 E=512 | 1.9 | 1.8–1.9 | 6283124 | 5247021 | 8214 | 0.2% | 0.002 |
| N= 4 L= 128 E=512 | 1.8 | 1.8–1.9 | 6391159 | 5247021 | 8075 | 0.2% | 0.002 |
| N= 4 L= 256 E=512 | 1.9 | 1.9–2.0 | 6380191 | 5247021 | 4729 | 0.1% | 0.001 |
| N= 16 L= 8 E=512 | 1.6 | 1.6–1.7 | 5517817 | 4457517 | 1244 | 0.0% | 0.000 |
| N= 16 L= 16 E=512 | 1.5 | 1.5–1.6 | 5186050 | 4457516 | 1043 | 0.0% | 0.000 |
| N= 16 L= 32 E=512 | 1.6 | 1.6–1.7 | 5486008 | 4457517 | 1735 | 0.0% | 0.000 |
| N= 16 L= 64 E=512 | 1.7 | 1.6–1.7 | 5633914 | 4457517 | 1687 | 0.0% | 0.000 |
| N= 16 L= 128 E=512 | 1.6 | 1.6–1.6 | 5418167 | 4457517 | 1291 | 0.0% | 0.000 |
| N= 16 L= 256 E=512 | 1.6 | 1.6–1.6 | 5459029 | 4457517 | 1327 | 0.0% | 0.000 |
| N= 64 L= 8 E=512 | 1.6 | 1.5–1.6 | 5301871 | 4260140 | 276 | 0.0% | 0.000 |
| N= 64 L= 16 E=512 | 1.5 | 1.5–1.6 | 5235456 | 4260141 | 282 | 0.0% | 0.000 |
| N= 64 L= 32 E=512 | 1.5 | 1.5–1.6 | 5213275 | 4260141 | 6681 | 0.2% | 0.002 |
| N= 64 L= 64 E=512 | 1.6 | 1.6–1.7 | 5401899 | 4260141 | 4835 | 0.1% | 0.001 |
| N= 64 L= 128 E=512 | 1.7 | 1.6–2.3 | 5451035 | 4260140 | 7841 | 0.2% | 0.002 |
| N= 64 L= 256 E=512 | 8.9 | 8.9–9.4 | 31974496 | 4260148 | 1226308 | 28.8% | 0.292 |
| N=256 L= 16 E=512 | 1.5 | 1.5–1.7 | 5173200 | 4219127 | 403 | 0.0% | 0.000 |
| N=256 L= 32 E=512 | 1.5 | 1.5–1.6 | 5156931 | 4219128 | 5019 | 0.1% | 0.001 |
| N=256 L= 48 E=512 | 7.3 | 7.2–7.9 | 26222412 | 4219134 | 963069 | 22.8% | 0.229 |
| N=512 L= 16 E=512 | 1.5 | 1.4–1.5 | 5098530 | 4235459 | 1520 | 0.0% | 0.000 |
| N=512 L= 32 E=512 | 10.9 | 10.4–13.2 | 37578415 | 4235468 | 1263441 | 29.8% | 0.299 |
| N=512 L= 48 E=512 | 12.2 | 12.1–13.8 | 43744616 | 4235469 | 1543965 | 36.5% | 0.365 |
| Control constant-0 | 4.3 | 4.3–4.5 | 15383022 | 8388719 | 69 | 0.0% | 0.000 |
| Control alternating | 3.3 | 3.2–3.3 | 11547752 | 8388718 | 188 | 0.0% | 0.000 |
| Control de Bruijn 32 | 3.3 | 3.3–3.5 | 11673073 | 8388718 | 118 | 0.0% | 0.000 |
| Control de Bruijn 64 | 3.4 | 3.3–3.7 | 11933012 | 8388720 | 397 | 0.0% | 0.000 |
| Control de Bruijn 128 | 3.3 | 3.3–3.4 | 11628024 | 8388718 | 270 | 0.0% | 0.000 |
| Control de Bruijn 256 | 3.3 | 3.2–3.4 | 11462304 | 8388718 | 311 | 0.0% | 0.000 |
| Control de Bruijn 512 | 3.8 | 3.6–4.1 | 13615310 | 8388719 | 90638 | 1.1% | 0.022 |
| Control de Bruijn 1024 | 3.8 | 3.7–3.9 | 13093127 | 8388719 | 72554 | 0.9% | 0.017 |
| Control de Bruijn 2048 | 4.9 | 4.8–5.4 | 17505747 | 8388723 | 267434 | 3.2% | 0.064 |
| Control reference 65536 | 16.2 | 16.2–16.3 | 58038077 | 8388731 | 2096571 | 25.0% | 0.500 |

**Misses per data branch (blocks 256, E=512):**

| N | L=8 | L=16 | L=32 | L=64 | L=128 | L=256 |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.008 | 0.004 | 0.008 | 0.008 | 0.005 | 0.003 |
| 4 | 0.001 | 0.002 | 0.001 | 0.002 | 0.002 | 0.001 |
| 16 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| 64 | 0.000 | 0.000 | 0.002 | 0.001 | 0.002 | 0.292 |

**Cycles per data branch (blocks 256, E=512):**

| N | L=8 | L=16 | L=32 | L=64 | L=128 | L=256 |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 2.38 | 3.07 | 3.07 | 2.80 | 2.88 | 2.90 |
| 4 | 1.18 | 1.66 | 1.53 | 1.50 | 1.52 | 1.52 |
| 16 | 1.32 | 1.24 | 1.31 | 1.34 | 1.29 | 1.30 |
| 64 | 1.26 | 1.25 | 1.24 | 1.29 | 1.30 | 7.62 |

**Misses per data branch (blocks 96, E=512):**

| N | L=16 | L=32 | L=48 |
|---:|---:|---:|---:|
| 256 | 0.000 | 0.001 | 0.229 |
| 512 | 0.000 | 0.299 | 0.365 |

**Cycles per data branch (blocks 96, E=512):**

| N | L=16 | L=32 | L=48 |
|---:|---:|---:|---:|
| 256 | 1.23 | 1.23 | 6.24 |
| 512 | 1.21 | 8.89 | 10.35 |

**Reference vs de Bruijn 32:** 4.92×

**Effective extra cycles per additional miss:** 22.1 cycles

_Training: 512 complete periods per site before every trial; cumulative exposure per kernel is in the export (exposure_before)._

_Footprint: N × 256, N × 96 data bytes per cell in each group (65536 for the controls); cache residency is not measured._

_Misses include the block and pass loop branches (1 per N data branches, plus pass exits)._

_These rates describe this kernel's learnability on this machine; no knee here is a table capacity or history length._

### Test 6 — Loop Exits

One counted inner loop, same branch address for every trip count C; work is reported per completed loop and per inner iteration.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/loop |
|---|---:|---:|---:|---:|---:|---:|---:|
| C= 1 | 4.8 | 4.7–4.9 | 17033994 | 33554479 | 6 | 0.0% | 0.000 |
| C= 2 | 8.2 | 7.9–8.4 | 29268956 | 25165877 | 5600 | 0.0% | 0.001 |
| C= 3 | 9.4 | 9.4–10.8 | 33763697 | 22369672 | 8035 | 0.0% | 0.001 |
| C= 4 | 11.7 | 11.7–12.5 | 41821865 | 20971575 | 5040 | 0.0% | 0.001 |
| C= 8 | 6.4 | 6.3–6.8 | 22929624 | 18874417 | 1092 | 0.0% | 0.001 |
| C= 16 | 5.4 | 5.3–5.9 | 19260278 | 17825840 | 2326 | 0.0% | 0.002 |
| C= 32 | 5.6 | 5.5–5.9 | 20046451 | 17301559 | 2317 | 0.0% | 0.004 |
| C= 34 | 5.6 | 5.4–5.7 | 19923281 | 17270695 | 2312 | 0.0% | 0.005 |
| C= 35 | 8.3 | 8.3–8.8 | 29639602 | 17256615 | 640006 | 3.7% | 1.335 |
| C= 36 | 5.6 | 5.4–5.8 | 19960644 | 17243269 | 2310 | 0.0% | 0.005 |
| C= 40 | 9.4 | 9.2–9.9 | 33570882 | 17196683 | 832779 | 4.8% | 1.986 |
| C= 44 | 8.0 | 7.7–8.2 | 28677983 | 17158551 | 516548 | 3.0% | 1.355 |
| C= 48 | 5.3 | 5.3–7.4 | 19013319 | 17126773 | 1987 | 0.0% | 0.006 |
| C= 64 | 6.1 | 5.3–6.4 | 19268638 | 17039411 | 3418 | 0.0% | 0.013 |
| C= 74 | 5.5 | 5.4–6.4 | 19503456 | 17003974 | 1205 | 0.0% | 0.005 |
| C= 75 | 6.4 | 6.4–6.9 | 22784538 | 17000945 | 298435 | 1.8% | 1.334 |
| C= 92 | 6.5 | 6.4–6.8 | 23381359 | 16959623 | 243167 | 1.4% | 1.333 |
| C= 94 | 5.0 | 5.0–5.2 | 17857468 | 16955744 | 19 | 0.0% | 0.000 |
| C= 96 | 6.0 | 5.9–6.0 | 21332043 | 16951963 | 174771 | 1.0% | 1.000 |
| C= 256 | 5.2 | 5.1–5.4 | 18484842 | 16842800 | 65542 | 0.4% | 1.000 |

**Loop-exit curve:**

| Variant | C | Iterations | Loops | Cycles/iter | Misses/loop |
|---|---:|---:|---:|---:|---:|
| C= 1 | 1 | 16777216 | 16777216 | 1.015 | 0.000 |
| C= 2 | 2 | 16777216 | 8388608 | 1.745 | 0.001 |
| C= 3 | 3 | 16777215 | 5592405 | 2.012 | 0.001 |
| C= 4 | 4 | 16777216 | 4194304 | 2.493 | 0.001 |
| C= 8 | 8 | 16777216 | 2097152 | 1.367 | 0.001 |
| C= 16 | 16 | 16777216 | 1048576 | 1.148 | 0.002 |
| C= 32 | 32 | 16777216 | 524288 | 1.195 | 0.004 |
| C= 34 | 34 | 16777198 | 493447 | 1.188 | 0.005 |
| C= 35 | 35 | 16777215 | 479349 | 1.767 | 1.335 |
| C= 36 | 36 | 16777188 | 466033 | 1.190 | 0.005 |
| C= 40 | 40 | 16777200 | 419430 | 2.001 | 1.986 |
| C= 44 | 44 | 16777200 | 381300 | 1.709 | 1.355 |
| C= 48 | 48 | 16777200 | 349525 | 1.133 | 0.006 |
| C= 64 | 64 | 16777216 | 262144 | 1.149 | 0.013 |
| C= 74 | 74 | 16777206 | 226719 | 1.162 | 0.005 |
| C= 75 | 75 | 16777200 | 223696 | 1.358 | 1.334 |
| C= 92 | 92 | 16777212 | 182361 | 1.394 | 1.333 |
| C= 94 | 94 | 16777214 | 178481 | 1.064 | 0.000 |
| C= 96 | 96 | 16777152 | 174762 | 1.271 | 1.000 |
| C= 256 | 256 | 16777216 | 65536 | 1.102 | 1.000 |

_Construction: one inner loop branch (dec/jnz; subs/b.ne on AArch64) serves every count, taken C−1 times and not taken once per loop; misses per loop include the outer loop branch and the call. Loop-count limits seen here describe this kernel on this machine, not a predictor's capacity._

**Effective user-cycle rate during measurements:** 3.39 GHz

