# branch_bench Glossary

The language of branch_bench's comparisons and measurements. These terms distinguish the work being compared from the observations used to describe its performance.

## Comparisons

**Test**:
One of the four benchmark comparisons: Threshold Sum, Stride Conditional, Indirect Dispatch, or Branch vs Branchless.
_Avoid_: Trial, variant

**Variant**:
One case within a test, defined by its decision or target pattern and method of computation, such as sorted with branching or random with branchless computation.
_Avoid_: Test, trial

**Kernel**:
The computation measured for a variant, excluding input preparation and result reporting; variants may share a kernel while using different input patterns.
_Avoid_: Benchmark, test

**Threshold Sum**:
The test comparing sums of byte values strictly above 128 in sorted and shuffled arrangements of identical data.

**Stride Conditional**:
The test comparing counts of ones in a periodic pattern with a one at every fourth position and a random pattern with a 25% probability of a one at each position.

**Indirect Dispatch**:
The test comparing calls among 32 distinct targets in a repeating sequential cycle and a random target sequence.

**Branch vs Branchless**:
The test comparing branching and branchless threshold sums on a sorted and a random dataset, with equal sums required between the two methods on each dataset.
Unlike Threshold Sum, the two datasets are generated separately, so they share a value distribution but not their contents.

**Predictable variant**:
The structured-pattern baseline in a comparison: sorted data, periodic decisions, or sequential dispatch; Test 4 has two, one per computation method. The name describes intended predictability, which a particular CPU may not achieve.
_Avoid_: Zero-miss variant

**Unpredictable variant**:
The shuffled or random-pattern case paired with a predictable variant of the same test (in Test 4, the one using the same computation method), intended to make branch outcomes or dispatch targets harder to predict.
_Avoid_: Always-mispredicted variant

**Branchless variant**:
A Test 4 variant whose threshold selection uses arithmetic in place of a data branch; loop-control branches can remain.
_Avoid_: Branch-free loop

**Data branch**:
The branch whose outcome or target the variant's input pattern controls: the threshold comparison in Tests 1 and 4, the decision test in Test 2, or the indirect call in Test 3.
_Avoid_: Branch, when loop-control branches are also meant

**Loop-control branch**:
A branch that runs a kernel's loops, such as the back-edge of the element or repetition loop, rather than testing the input; it is almost always predicted correctly but is still counted.
_Avoid_: Data branch

**Leaf return**:
The return from a Test 3 leaf function after its indirect call; neither a data branch nor a loop-control branch, it is predicted by the return-address stack and still counted, so each Test 3 call executes three counted branches.
_Avoid_: Data branch, loop-control branch

## Measurements

**Run**:
One execution of the benchmark, containing the selected tests and their variants, warm-ups and measured trials.
_Avoid_: Trial

**Trial**:
One measured execution of a variant's kernel over its full workload, with elapsed time and any available hardware counts belonging to that execution.
_Avoid_: Repetition, pass, run

**Repetition**:
One complete pass over an array within a trial in Tests 1, 2 and 4; Test 3 instead measures a specified number of calls without repetitions.
_Avoid_: Trial

**Warm-up**:
One unmeasured execution of a variant before its trials when more than one trial is requested, excluded from reported measurements. Labelled "warm-up run" in the banner, report and help text, the one accepted use of "run" for less than a whole run.
_Avoid_: First trial

**Median trial**:
The trial selected by ordering a variant's trials by elapsed time and choosing the middle one, or the lower middle one for an even count; its time and hardware counts stay together.
_Avoid_: Average trial, median of each metric

**Work per trial**:
The number of element visits across all repetitions in Tests 1, 2 and 4, or the number of indirect calls in Test 3.
_Avoid_: Array size when referring to total element visits

**Timed window**:
The part of a trial measured by the wall-clock timer: the kernel call between the two timer reads, giving the trial's elapsed time.
_Avoid_: Counter window

**Counter window**:
The part of a trial over which hardware counters count, from enabling the counter group to disabling it; it encloses the timed window and so also counts the timer calls and the counter control calls' user-mode code, a negligible addition to a kernel's counts.
_Avoid_: Timed window

**Available hardware count**:
A hardware event count from a counter that opened for the run and was read successfully for a particular trial; a missing count is unknown, and a time-shared count is a scaled estimate.
_Avoid_: Zero when a count is unavailable

## Reported metrics

**Range**:
The fastest and slowest elapsed times among a variant's measured trials, reported when multiple trials are requested.
_Avoid_: Confidence interval

**Miss %**:
The percentage of counted branches that were mispredicted. Both counts cover every branch in the counter window, so loop-control branches and, in Test 3, leaf returns add to the denominator and any of their misses to the numerator.
On ARM, a branch count from the raw `BR_PRED` event (named on the banner's "Raw PMU events" line and the report's "Raw ARM PMU events" line) includes speculatively executed branches, which further lowers the percentage.
_Avoid_: Data-branch miss rate, data-branch miss probability

**Data-branch miss probability**:
The fraction of a variant's data-branch executions that are mispredicted. No counter measures it directly; misses per element or per call approximates it when other branches add few misses.
The `~0% misses` and `~50% misses` in Test 1's variant labels refer to it, which is why the shuffled row shows `~50% misses` beside a Miss % of about 25%.
_Avoid_: Miss %

**Misses per element / Misses per call**:
The measured mispredictions divided by work per trial: element visits for Tests 1, 2 and 4, or indirect calls for Test 3. Labelled `Misses/elem` or `Misses/call` in the report, and `/elem` or `/call` after the miss count on the console.
The count includes misses from all branches in the counter window, including loop-control branches and, in Test 3, leaf returns, so this is an average number of misses per work item rather than the miss probability of one branch.
_Avoid_: Miss %, data-branch miss probability

**Slowdown**:
The reported elapsed time of the unpredictable variant divided by that of the predictable variant in Tests 1–3; a value above 1 means the unpredictable variant took longer.
_Avoid_: Extra time, cycle ratio

**Branch penalty on random data**:
Test 4's reported elapsed time for random data with branching divided by that for sorted data with branching.
_Avoid_: Branchless speedup, cost per miss

**Branchless consistency ratio**:
Test 4's reported elapsed time for random data with branchless computation divided by that for sorted data with branchless computation, labelled `Branchless is consistent` in the results.
A value near 1 means the two measured times were similar.
_Avoid_: Guaranteed equal timings

**Cost per miss**:
An estimate of extra cycles per extra misprediction between the reported unpredictable and predictable trials (for Test 4, using its two branching variants).
It depends on available cycle and miss counts and a sufficiently large increase in misses (at least 1,000 extra misses) with a positive increase in cycles.
_Avoid_: Fixed pipeline penalty

**Average clock during measurements**:
Total measured cycles divided by total elapsed time across all trials with available cycle counts, excluding warm-ups.
Cycles are counted in user mode only, so time spent in the kernel or preempted lowers the figure slightly; cycles come from the counter window and time from the narrower timed window, which raises it negligibly.
_Avoid_: Maximum clock, base clock, median-trial clock
