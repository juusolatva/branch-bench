# Tech stack

- C99 (`-std=c99`, enforced by `run.sh`), one translation unit. `#define _DEFAULT_SOURCE` must stay before the first include: it exposes POSIX/Linux APIs (`clock_gettime`, `localtime_r`, `syscall`, `sysconf`). `_POSIX_C_SOURCE` alone hides `syscall()` and breaks the build.
- Headers: stdio/stdlib/stdint/string/time/inttypes; on Linux also unistd, sys/syscall, linux/perf_event, sys/ioctl.
- Compilers: gcc is the reference (`run.sh`). clang builds only with `-fno-vectorize -fno-slp-vectorize` (no `-fno-if-conversion`), so its results aren't trusted. No Makefile/CMake, no package manager. Needs POSIX; not MSVC.
- Required flags (never drop): `-O2 -fno-tree-vectorize -fno-if-conversion`. Without them the kernels get vectorized or cmov-converted and the mispredictions disappear. Test 4's branchless variant doesn't depend on them.
- Perf: `perf_event_open(2)`, user mode only, one group of branches/misses/cycles read with `PERF_FORMAT_GROUP` (+ time enabled/running, scaled if multiplexed). Unavailable with restrictive `perf_event_paranoid`, in most containers/VMs and WSL2; then wall-clock only (`now_ms()`, monotonic).
- Clock info: max clock from `/sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_max_freq` (highest across CPUs); measured average from cycles ÷ time.
- Code quality: SonarQube Cloud (public project `juusolatva_branch_bench`), scanned from the CI `sonarqube` job (Build Wrapper + gcovr coverage; Automatic Analysis off). Quality gate on new code: coverage ≥ 80%, code-smell severity score ≤ 14 (5 new Major issues scored 15), duplication ≤ 3%. The user marks false positives there.
- CI: GitHub Actions, hosted runners (VMs, no PMU/cpufreq) — correctness and codegen only, never timings.
- Results so far: Intel Ivy Bridge/Haswell/Kaby Lake R, AMD Zen 5 (WSL2), Raspberry Pi 4 (Cortex-A72). Planned: Cudy WR3000S (Cortex-A53, OpenWrt). Dev machine: Fedora Linux x86_64 (i5-8350U).
