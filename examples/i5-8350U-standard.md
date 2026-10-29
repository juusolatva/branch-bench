## Intel(R) Core(TM) i5-8350U CPU @ 3.60 GHz

_2026-10-29_

_Benchmark revision 4._

_Profile: standard._

_Kernels: asm-x86_64 for the branch kernels of Tests 1, 2 and 4–6; the other kernels are C._

| Array size | Repetitions | Perf counters | Compiler |
|---:|---:|---|---|
| 4194304 elements | 64/trial | available (hardware counts) | GCC 16.2.1 20260819 (Red Hat 16.2.1-2) |

_Build flags: `-std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -Wa,-mbranches-within-32B-boundaries,-malign-branch=jcc+fused+jmp+call+ret+indirect`_

_Commit: `v1.1.0-25-g4311d19`_

_Median of 6 trials per variant; each trial follows one untimed training run, and paired variants alternate AB/BA. Range is the fastest–slowest trial._

_All tests were run (--hard), including the hard tests 5 and 6._

_Pinned to CPU 2._

### Test 1 — Threshold Sum

Sorted vs shuffled copies of one random byte array, same branch kernel.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted | 196.5 | 192.8–199.3 | 695647666 | 536871231 | 747 | 0.0% | 0.000 |
| Shuffled | 1009.6 | 1008.2–1011.2 | 3583748869 | 536872095 | 134171600 | 25.0% | 0.500 |

**Slowdown:** 5.14×

**Effective extra cycles per additional miss:** 21.5 cycles

_Elements > 128: 2081995 of 4194304 per pass (49.6%)._

### Test 2 — Bias and Ordering

Same number of ones per bias in periodic, clustered and shuffled order, plus constant controls; one branch site.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1/16 periodic 1000000000000000 | 236.5 | 235.4–241.6 | 825860441 | 536871294 | 57307 | 0.0% | 0.000 |
| 1/16 clustered | 233.4 | 228.2–235.3 | 813470640 | 536871283 | 3935 | 0.0% | 0.000 |
| 1/16 shuffled | 343.6 | 341.1–352.7 | 1214049507 | 536871459 | 17928128 | 3.3% | 0.067 |
| 1/4 periodic 1000 | 235.5 | 231.9–241.7 | 828452854 | 536871285 | 78035 | 0.0% | 0.000 |
| 1/4 clustered | 214.5 | 213.0–234.3 | 750816212 | 536871265 | 18842 | 0.0% | 0.000 |
| 1/4 shuffled | 707.9 | 701.1–712.0 | 2508009650 | 536871782 | 76496716 | 14.2% | 0.285 |
| 1/2 periodic 10 | 227.6 | 225.0–234.5 | 792944323 | 536871276 | 924 | 0.0% | 0.000 |
| 1/2 clustered | 198.6 | 197.3–201.1 | 687720589 | 536871264 | 6240 | 0.0% | 0.000 |
| 1/2 shuffled | 1053.4 | 1050.6–1058.4 | 3739458640 | 536872132 | 134211729 | 25.0% | 0.500 |
| 3/4 periodic 1110 | 234.8 | 231.9–238.0 | 819614237 | 536871288 | 1053 | 0.0% | 0.000 |
| 3/4 clustered | 181.6 | 180.3–191.7 | 617626804 | 536871234 | 50559 | 0.0% | 0.000 |
| 3/4 shuffled | 665.6 | 659.3–671.4 | 2349674716 | 536871810 | 76787367 | 14.3% | 0.286 |
| 15/16 periodic 1111111111111110 | 184.5 | 182.6–186.3 | 622338867 | 536871232 | 6853 | 0.0% | 0.000 |
| 15/16 clustered | 169.0 | 168.7–180.8 | 564067110 | 536871218 | 24575 | 0.0% | 0.000 |
| 15/16 shuffled | 296.0 | 289.7–302.6 | 1018832231 | 536871359 | 18948993 | 3.5% | 0.071 |
| Constant 0 | 233.8 | 232.2–238.7 | 822044962 | 536871285 | 138 | 0.0% | 0.000 |
| Constant 1 | 165.9 | 164.5–180.5 | 551654390 | 536871205 | 100 | 0.0% | 0.000 |

**1/16 shuffled vs periodic:** 1.45×

**1/16 shuffled vs clustered:** 1.48×

**1/4 shuffled vs periodic:** 2.98×

**1/4 shuffled vs clustered:** 3.22×

**1/2 shuffled vs periodic:** 4.61×

**1/2 shuffled vs clustered:** 5.30×

**3/4 shuffled vs periodic:** 2.83×

**3/4 shuffled vs clustered:** 3.58×

**15/16 shuffled vs periodic:** 1.61×

**15/16 shuffled vs clustered:** 1.72×

**Effective extra cycles per additional miss:** 21.9 cycles

### Test 3 — Indirect Dispatch

One indirect call site over 1–64 leaf targets; cyclic vs shuffled index sequences with equal target counts.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/call |
|---|---:|---:|---:|---:|---:|---:|---:|
| T= 1 cyclic | 6.6 | 6.5–8.9 | 21522203 | 12582960 | 993 | 0.0% | 0.000 |
| T= 1 shuffled | 6.8 | 6.5–9.8 | 21810676 | 12582960 | 9938 | 0.1% | 0.002 |
| T= 2 cyclic | 7.2 | 7.0–7.5 | 23659211 | 12582962 | 788 | 0.0% | 0.000 |
| T= 2 shuffled | 21.7 | 21.5–22.0 | 76331003 | 12582983 | 2098396 | 16.7% | 0.500 |
| T= 4 cyclic | 7.1 | 7.0–9.5 | 23746465 | 12582961 | 1909 | 0.0% | 0.000 |
| T= 4 shuffled | 29.0 | 28.5–31.3 | 101488150 | 12582987 | 3148721 | 25.0% | 0.751 |
| T= 8 cyclic | 7.1 | 7.0–7.2 | 23935010 | 12582961 | 102 | 0.0% | 0.000 |
| T= 8 shuffled | 32.1 | 31.7–32.9 | 113988908 | 12582987 | 3672806 | 29.2% | 0.876 |
| T=16 cyclic | 7.2 | 7.0–7.9 | 23966197 | 12582960 | 134 | 0.0% | 0.000 |
| T=16 shuffled | 34.2 | 33.8–35.3 | 119638706 | 12582995 | 3932393 | 31.3% | 0.938 |
| T=32 cyclic | 7.2 | 7.2–7.5 | 24387803 | 12582960 | 112 | 0.0% | 0.000 |
| T=32 shuffled | 35.5 | 34.3–36.1 | 123289747 | 12582996 | 4063175 | 32.3% | 0.969 |
| T=64 cyclic | 7.4 | 7.2–7.9 | 24449248 | 12582960 | 17580 | 0.1% | 0.004 |
| T=64 shuffled | 36.1 | 34.6–37.9 | 126072680 | 12582997 | 4129752 | 32.8% | 0.985 |

**T=1 shuffled vs cyclic:** 1.01×

**T=2 shuffled vs cyclic:** 3.01×

**T=4 shuffled vs cyclic:** 3.90×

**T=8 shuffled vs cyclic:** 4.56×

**T=16 shuffled vs cyclic:** 4.74×

**T=32 shuffled vs cyclic:** 4.85×

**T=64 shuffled vs cyclic:** 4.79×

**Effective extra cycles per additional miss:** 24.6 cycles

### Test 4 — Branch vs Branchless

Same sum via Test 1's branch kernel and an arithmetic mask, on sorted and shuffled copies of one byte array. A branchless vs branch ratio below 1 means branchless is faster.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/elem |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sorted + branch | 199.2 | 194.1–201.3 | 695863050 | 536871256 | 846 | 0.0% | 0.000 |
| Sorted + branchless | 166.4 | 165.5–172.2 | 549413359 | 268435763 | 111 | 0.0% | 0.000 |
| Shuffled + branch | 1010.8 | 1008.5–1176.8 | 3584414146 | 536872098 | 134192985 | 25.0% | 0.500 |
| Shuffled + branchless | 165.8 | 165.7–169.6 | 553544928 | 268435745 | 135 | 0.0% | 0.000 |

**Branch, shuffled vs sorted:** 5.10×

**Sorted, branchless vs branch:** 0.85×

**Shuffled, branchless vs branch:** 0.16×

**Effective extra cycles per additional miss:** 21.6 cycles

_Elements > 128: 2080598 of 4194304 per pass (49.6%)._

### Test 5 — Pattern Learnability Matrix

N static data branches per block, each reading its own repeating L-bit random word (block-major); constant, alternating, de Bruijn and balanced-reference controls at N = 1.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/branch |
|---|---:|---:|---:|---:|---:|---:|---:|
| N= 1 L= 8 E= 64 | 2.8 | 2.8–2.9 | 9971250 | 8405039 | 32755 | 0.4% | 0.008 |
| N= 1 L= 16 E= 64 | 3.6 | 3.6–3.8 | 12873678 | 8405041 | 16441 | 0.2% | 0.004 |
| N= 1 L= 32 E= 64 | 3.7 | 3.6–3.8 | 12961391 | 8405041 | 32117 | 0.4% | 0.008 |
| N= 1 L= 64 E= 64 | 3.3 | 3.3–3.4 | 11707002 | 8405038 | 32761 | 0.4% | 0.008 |
| N= 1 L= 128 E= 64 | 4.0 | 3.7–4.1 | 13683376 | 8405039 | 24108 | 0.3% | 0.006 |
| N= 1 L= 256 E= 64 | 3.6 | 3.4–3.8 | 12012976 | 8405040 | 3420 | 0.0% | 0.001 |
| N= 4 L= 8 E= 64 | 1.5 | 1.4–1.5 | 4953531 | 5247021 | 4135 | 0.1% | 0.001 |
| N= 4 L= 16 E= 64 | 2.0 | 2.0–2.1 | 6973042 | 5247023 | 8124 | 0.2% | 0.002 |
| N= 4 L= 32 E= 64 | 1.8 | 1.8–1.8 | 6343743 | 5247021 | 4127 | 0.1% | 0.001 |
| N= 4 L= 64 E= 64 | 1.8 | 1.8–1.8 | 6248878 | 5247020 | 6373 | 0.1% | 0.002 |
| N= 4 L= 128 E= 64 | 1.8 | 1.8–1.8 | 6386287 | 5247021 | 8239 | 0.2% | 0.002 |
| N= 4 L= 256 E= 64 | 1.8 | 1.8–1.8 | 6335130 | 5247021 | 4164 | 0.1% | 0.001 |
| N= 16 L= 8 E= 64 | 1.5 | 1.5–1.6 | 5333230 | 4457516 | 1043 | 0.0% | 0.000 |
| N= 16 L= 16 E= 64 | 1.5 | 1.5–1.6 | 5184365 | 4457517 | 1068 | 0.0% | 0.000 |
| N= 16 L= 32 E= 64 | 1.6 | 1.6–1.8 | 5482220 | 4457518 | 1549 | 0.0% | 0.000 |
| N= 16 L= 64 E= 64 | 1.7 | 1.6–1.7 | 5641253 | 4457517 | 1965 | 0.0% | 0.000 |
| N= 16 L= 128 E= 64 | 1.6 | 1.6–1.7 | 5527649 | 4457516 | 6117 | 0.1% | 0.001 |
| N= 16 L= 256 E= 64 | 1.6 | 1.6–1.6 | 5503751 | 4457517 | 4048 | 0.1% | 0.001 |
| N= 64 L= 8 E= 64 | 1.6 | 1.5–1.6 | 5330918 | 4260141 | 268 | 0.0% | 0.000 |
| N= 64 L= 16 E= 64 | 1.5 | 1.5–1.6 | 5243162 | 4260141 | 293 | 0.0% | 0.000 |
| N= 64 L= 32 E= 64 | 1.5 | 1.5–1.5 | 5072083 | 4260140 | 544 | 0.0% | 0.000 |
| N= 64 L= 64 E= 64 | 1.5 | 1.5–1.6 | 5325716 | 4260140 | 5160 | 0.1% | 0.001 |
| N= 64 L= 128 E= 64 | 1.6 | 1.6–2.8 | 5518993 | 4260141 | 13096 | 0.3% | 0.003 |
| N= 64 L= 256 E= 64 | 9.0 | 8.9–9.5 | 32294343 | 4260152 | 1239289 | 29.1% | 0.295 |
| N=256 L= 16 E= 64 | 1.5 | 1.4–1.5 | 5144381 | 4219127 | 1907 | 0.0% | 0.000 |
| N=256 L= 32 E= 64 | 1.5 | 1.5–1.6 | 5077962 | 4219128 | 1873 | 0.0% | 0.000 |
| N=256 L= 48 E= 64 | 7.4 | 7.2–7.9 | 26375420 | 4219133 | 972645 | 23.1% | 0.231 |
| N=512 L= 16 E= 64 | 1.5 | 1.5–1.5 | 5153363 | 4235458 | 3309 | 0.1% | 0.001 |
| N=512 L= 32 E= 64 | 10.4 | 10.2–10.8 | 37193514 | 4235468 | 1255041 | 29.6% | 0.297 |
| N=512 L= 48 E= 64 | 12.5 | 12.3–13.3 | 44169457 | 4235473 | 1558826 | 36.8% | 0.369 |
| Control constant-0 | 4.4 | 4.3–4.8 | 15510391 | 8388719 | 73 | 0.0% | 0.000 |
| Control alternating | 3.5 | 3.5–4.0 | 11740027 | 8388720 | 1697 | 0.0% | 0.000 |
| Control de Bruijn 32 | 3.3 | 3.3–3.4 | 11703421 | 8388718 | 117 | 0.0% | 0.000 |
| Control de Bruijn 64 | 3.2 | 3.2–3.2 | 11407931 | 8388719 | 93 | 0.0% | 0.000 |
| Control de Bruijn 128 | 3.2 | 3.2–3.3 | 11574817 | 8388719 | 226 | 0.0% | 0.000 |
| Control de Bruijn 256 | 3.3 | 3.3–3.3 | 11423779 | 8388721 | 364 | 0.0% | 0.000 |
| Control de Bruijn 512 | 3.9 | 3.9–4.3 | 13704931 | 8388719 | 97717 | 1.2% | 0.023 |
| Control de Bruijn 1024 | 3.8 | 3.7–3.9 | 13309031 | 8388719 | 83101 | 1.0% | 0.020 |
| Control de Bruijn 2048 | 4.7 | 4.7–5.3 | 16914609 | 8388720 | 248856 | 3.0% | 0.059 |
| Control reference 65536 | 16.1 | 16.1–16.4 | 57771258 | 8388732 | 2094208 | 25.0% | 0.499 |

**Misses per data branch (blocks 256, E=64):**

| N | L=8 | L=16 | L=32 | L=64 | L=128 | L=256 |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.008 | 0.004 | 0.008 | 0.008 | 0.006 | 0.001 |
| 4 | 0.001 | 0.002 | 0.001 | 0.002 | 0.002 | 0.001 |
| 16 | 0.000 | 0.000 | 0.000 | 0.000 | 0.001 | 0.001 |
| 64 | 0.000 | 0.000 | 0.000 | 0.001 | 0.003 | 0.295 |

**Cycles per data branch (blocks 256, E=64):**

| N | L=8 | L=16 | L=32 | L=64 | L=128 | L=256 |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 2.38 | 3.07 | 3.09 | 2.79 | 3.26 | 2.86 |
| 4 | 1.18 | 1.66 | 1.51 | 1.49 | 1.52 | 1.51 |
| 16 | 1.27 | 1.24 | 1.31 | 1.34 | 1.32 | 1.31 |
| 64 | 1.27 | 1.25 | 1.21 | 1.27 | 1.32 | 7.70 |

**Misses per data branch (blocks 96, E=64):**

| N | L=16 | L=32 | L=48 |
|---:|---:|---:|---:|
| 256 | 0.000 | 0.000 | 0.231 |
| 512 | 0.001 | 0.297 | 0.369 |

**Cycles per data branch (blocks 96, E=64):**

| N | L=16 | L=32 | L=48 |
|---:|---:|---:|---:|
| 256 | 1.22 | 1.21 | 6.28 |
| 512 | 1.22 | 8.80 | 10.45 |

**Reference vs de Bruijn 32:** 4.88×

**Effective extra cycles per additional miss:** 22.0 cycles

_Training: 64 complete periods per site before every trial; cumulative exposure per kernel is in the export (exposure_before)._

_Footprint: N × 256, N × 96 data bytes per cell in each group (65536 for the controls); cache residency is not measured._

_Misses include the block and pass loop branches (1 per N data branches, plus pass exits)._

_These rates describe this kernel's learnability on this machine; no knee here is a table capacity or history length._

### Test 6 — Loop Exits

One counted inner loop, same branch address for every trip count C; work is reported per completed loop and per inner iteration.

| Variant | Time (ms) | Range (ms) | Cycles | Branches | Misses | Miss % | Misses/loop |
|---|---:|---:|---:|---:|---:|---:|---:|
| C= 1 | 4.8 | 4.7–5.9 | 17100910 | 33554482 | 16 | 0.0% | 0.000 |
| C= 2 | 7.9 | 7.8–8.1 | 28176408 | 25165874 | 6853 | 0.0% | 0.001 |
| C= 3 | 9.4 | 9.4–10.0 | 33718758 | 22369672 | 7937 | 0.0% | 0.001 |
| C= 4 | 11.7 | 11.7–12.1 | 41917717 | 20971575 | 5093 | 0.0% | 0.001 |
| C= 6 | 6.3 | 6.3–6.5 | 22542383 | 19573463 | 2726 | 0.0% | 0.001 |
| C= 8 | 6.3 | 6.2–6.9 | 22390339 | 18874418 | 574 | 0.0% | 0.000 |
| C= 12 | 5.5 | 5.5–5.8 | 19734686 | 18175361 | 2396 | 0.0% | 0.002 |
| C= 16 | 5.4 | 5.3–5.5 | 19250170 | 17825840 | 2314 | 0.0% | 0.002 |
| C= 24 | 5.1 | 5.1–5.7 | 18224201 | 17476299 | 2234 | 0.0% | 0.003 |
| C= 32 | 5.6 | 5.6–5.7 | 20085736 | 17301553 | 2341 | 0.0% | 0.004 |
| C= 48 | 5.3 | 5.2–5.4 | 19046756 | 17126774 | 41 | 0.0% | 0.000 |
| C= 64 | 5.4 | 5.1–5.8 | 18775169 | 17039409 | 44 | 0.0% | 0.000 |
| C= 96 | 6.0 | 5.9–6.3 | 21325518 | 16951963 | 174768 | 1.0% | 1.000 |
| C= 128 | 5.7 | 5.6–7.0 | 20221795 | 16908337 | 131090 | 0.8% | 1.000 |
| C= 160 | 5.4 | 5.4–5.5 | 19508599 | 16882025 | 104865 | 0.6% | 1.000 |
| C= 192 | 5.3 | 5.3–6.0 | 19068160 | 16864581 | 87395 | 0.5% | 1.000 |
| C= 256 | 5.2 | 5.2–5.4 | 18513233 | 16842800 | 65541 | 0.4% | 1.000 |
| C= 384 | 5.0 | 5.0–5.1 | 17916846 | 16820698 | 43694 | 0.3% | 1.000 |
| C= 512 | 4.9 | 4.9–5.7 | 17667092 | 16810031 | 32775 | 0.2% | 1.000 |

**Loop-exit curve:**

| Variant | C | Iterations | Loops | Cycles/iter | Misses/loop |
|---|---:|---:|---:|---:|---:|
| C= 1 | 1 | 16777216 | 16777216 | 1.019 | 0.000 |
| C= 2 | 2 | 16777216 | 8388608 | 1.679 | 0.001 |
| C= 3 | 3 | 16777215 | 5592405 | 2.010 | 0.001 |
| C= 4 | 4 | 16777216 | 4194304 | 2.498 | 0.001 |
| C= 6 | 6 | 16777212 | 2796202 | 1.344 | 0.001 |
| C= 8 | 8 | 16777216 | 2097152 | 1.335 | 0.000 |
| C= 12 | 12 | 16777212 | 1398101 | 1.176 | 0.002 |
| C= 16 | 16 | 16777216 | 1048576 | 1.147 | 0.002 |
| C= 24 | 24 | 16777200 | 699050 | 1.086 | 0.003 |
| C= 32 | 32 | 16777216 | 524288 | 1.197 | 0.004 |
| C= 48 | 48 | 16777200 | 349525 | 1.135 | 0.000 |
| C= 64 | 64 | 16777216 | 262144 | 1.119 | 0.000 |
| C= 96 | 96 | 16777152 | 174762 | 1.271 | 1.000 |
| C= 128 | 128 | 16777216 | 131072 | 1.205 | 1.000 |
| C= 160 | 160 | 16777120 | 104857 | 1.163 | 1.000 |
| C= 192 | 192 | 16777152 | 87381 | 1.137 | 1.000 |
| C= 256 | 256 | 16777216 | 65536 | 1.103 | 1.000 |
| C= 384 | 384 | 16776960 | 43690 | 1.068 | 1.000 |
| C= 512 | 512 | 16777216 | 32768 | 1.053 | 1.000 |

_Construction: one inner loop branch (dec/jnz; subs/b.ne on AArch64) serves every count, taken C−1 times and not taken once per loop; misses per loop include the outer loop branch and the call. Loop-count limits seen here describe this kernel on this machine, not a predictor's capacity._

**Effective user-cycle rate during measurements:** 3.50 GHz

