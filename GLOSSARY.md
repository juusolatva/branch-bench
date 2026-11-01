# Branch benchmark

The language of branch_bench's comparisons and measurements. These terms distinguish the work being compared from the observations used to describe its performance.

## Language

### Comparisons

**Test**:
One of the four benchmark comparisons: Threshold Sum, Stride Conditional, Indirect Dispatch, or Branch vs. Branchless.
_Avoid_: Trial, variant

**Variant**:
One case within a test, defined by its input pattern and method of computation, such as sorted with branching or random with branchless computation.
_Avoid_: Test, trial

**Kernel**:
The computation measured for a variant, excluding input preparation and result reporting; variants may share a kernel while using different input patterns.

**Threshold Sum**:
The test comparing sums of byte values strictly above 128 in sorted and shuffled arrangements of identical data.

**Stride Conditional**:
The test comparing counts of ones in a periodic pattern with a one at every fourth position and a random pattern with a 25% probability of a one at each position.

**Indirect Dispatch**:
The test comparing calls among 32 distinct targets in a repeating sequential cycle and a random target sequence.

**Branch vs. Branchless**:
The test comparing branching and branchless threshold sums on sorted and random data, with equal sums required between the two methods on each dataset.

**Predictable variant**:
The structured-pattern baseline in a comparison: sorted data, periodic decisions, or sequential dispatch; the name describes intended predictability, which a particular CPU may not achieve.
_Avoid_: Zero-miss variant

**Unpredictable variant**:
The shuffled or random-pattern case paired with the structured baseline, intended to make branch outcomes or dispatch targets harder to predict.
_Avoid_: Always-mispredicted variant

**Branchless variant**:
A Test 4 variant whose threshold selection uses arithmetic in place of a data-dependent conditional jump; loop-control branches can remain.
_Avoid_: Branch-free loop

### Measurements

**Run**:
One execution of the benchmark, containing the selected tests and their variants, warm-ups and measured trials.
_Avoid_: Trial when referring to the entire execution

**Trial**:
One measured execution of a variant's kernel over its full workload, with elapsed time and any available hardware counts belonging to that execution.
_Avoid_: Repetition, pass

**Repetition**:
One complete pass over an array within a trial in Tests 1, 2 and 4; Test 3 instead measures a specified number of calls without repetitions.
_Avoid_: Trial

**Warm-up**:
One unmeasured execution of a variant before its trials when more than one trial is requested, excluded from reported measurements.
_Avoid_: First trial

**Median trial**:
The trial selected by ordering a variant's trials by elapsed time and choosing the middle one, or the lower middle one for an even count; its time and hardware counts stay together.
_Avoid_: Average trial, median of each metric

**Work per trial**:
The number of element visits across all repetitions in Tests 1, 2 and 4, or the number of indirect calls in Test 3.
_Avoid_: Array size when referring to total element visits

**Available hardware count**:
A hardware event count supported by the run and successfully measured for a particular trial; a missing count is unknown, and a time-shared count is a scaled estimate.
_Avoid_: Zero when a count is unavailable

### Reported metrics

**Range**:
The fastest and slowest elapsed times among a variant's measured trials, reported when multiple trials are requested.
_Avoid_: Confidence interval

**Miss %**:
The percentage of counted branches that were mispredicted, including loop-control branches in the denominator; the branch count may include speculative branches on ARM.
_Avoid_: Data-branch miss rate

**Misses per element / Misses per call**:
The measured mispredictions divided by work per trial: element visits for Tests 1, 2 and 4, or indirect calls for Test 3.
_Avoid_: Miss %

**Cost per miss**:
An estimate of extra cycles per extra misprediction between the reported unpredictable and predictable trials, using the two branching variants in Test 4.
It is available only when both trials have cycle and miss counts, with at least 1000 extra misses and a positive cycle difference.
_Avoid_: Fixed pipeline penalty

**Average clock during measurements**:
Total measured cycles divided by total elapsed time across all trials with available cycle counts, excluding warm-ups.
_Avoid_: Maximum clock, base clock, median-trial clock
