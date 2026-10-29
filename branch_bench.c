/*
 * branch_bench.c — CPU Branch Prediction Efficiency Benchmark
 *
 * Four tests that contrast highly predictable vs unpredictable branching
 * to measure the real CPU cost of branch misprediction:
 *
 *  Test 1 — Threshold Sum       sorted vs shuffled array              (classic direct-branch demo)
 *  Test 2 — Stride Conditional  periodic 25% vs random 25% take-rate  (repeating pattern stress)
 *  Test 3 — Indirect Dispatch   sequential vs random function pointer  (indirect branch predictor)
 *  Test 4 — Branch vs Branchless sorted/random × branch/arithmetic    (misprediction cost isolation)
 *
 * On Linux, hardware counters are read via perf_event_open(2) when the kernel
 * allows it (in a VM only if the hypervisor exposes a virtual PMU, which
 * many cloud VPSes don't).  Falls back to wall-clock timing
 * automatically, which is meaningful everywhere including containers and VMs.
 *
 * Build (or use run.sh, which adds the x86-64 flags and build provenance):
 *   gcc -std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -o branch_bench branch_bench.c
 * On x86-64 add, to keep kernel jumps off 32-byte boundaries (JCC erratum):
 *   -Wa,-mbranches-within-32B-boundaries,-malign-branch=jcc+fused+jmp+call+ret+indirect
 *
 * Note: -fno-tree-vectorize prevents SIMD rewriting that would bypass branches.
 *       -fno-if-conversion keeps if-else as actual branch instructions (not CMOV),
 *       which is required for tests 1-3 and Test 4's branch variant to show
 *       misprediction cost.
 *       Test 4's branchless variant uses arithmetic masks — unaffected by these flags.
 *       clang rejects -fno-if-conversion and has no equivalent; it builds with
 *       -fno-vectorize -fno-slp-vectorize instead, but (as of clang 18-21)
 *       makes Tests 1, 2 and 4's branch variant branchless (CMOV, SBB), so
 *       clang timings don't show the misprediction cost.
 *
 * Run:
 *   ./branch_bench
 */

/* Expose POSIX (clock_gettime, localtime_r, fdopen) and syscall() under -std=c99. */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>

/* ── Linux perf_event support (optional) ────────────────────────────────── */
#ifdef __linux__
#  include <sys/syscall.h>
#  ifdef __NR_perf_event_open
#    include <linux/perf_event.h>
#    include <sys/ioctl.h>
#    define HAVE_PERF 1
#  endif
#endif

/* ── Tuneable parameters ─────────────────────────────────────────────────── */
#define DEFAULT_SIZE  (1u << 22)   /* 4 194 304 elements (tests 1/2/4) or calls (test 3) */
#define DEFAULT_REPS  64           /* outer repetitions (tests 1/2/4)        */
#define THRESHOLD     128          /* byte comparison threshold               */
#define NUM_FUNCS     32           /* distinct indirect call targets (test 3) */
#define NUM_TESTS     4
#define MAX_TRIALS    99           /* upper limit for -n/--trials             */
#define MAX_SIZE      (1UL << 30)  /* upper limit for --size                  */
#define MAX_REPS      100000UL     /* upper limit for --reps                  */

/* ── Build provenance (printed in the banner and report) ────────────────── */
/* Clang's __VERSION__ already names the compiler; GCC's is only the number. */
#if defined(__clang__)
#  define COMPILER_VERSION __VERSION__
#elif defined(__GNUC__)
#  define COMPILER_VERSION "GCC " __VERSION__
#else
#  define COMPILER_VERSION "unknown compiler"
#endif
/* run.sh passes its compiler flags and git commit; other builds leave them out. */
#ifndef BUILD_FLAGS
#  define BUILD_FLAGS ""
#endif
#ifndef BUILD_COMMIT
#  define BUILD_COMMIT ""
#endif

/* Run-time settings, changed only by command-line options. */
static size_t   size  = DEFAULT_SIZE;  /* --size: elements per array (tests 1/2/4),
                                          calls per trial (test 3) */
static unsigned reps  = DEFAULT_REPS;  /* --reps: passes over the array per trial */
static int      trials = 1;            /* -n/--trials: 1 = single measurement, no warm-up */
static unsigned tests_selected = 0;    /* -t/--test bitmask (bit n-1 = test n); 0 = all */

/* ── Types ───────────────────────────────────────────────────────────────── */
typedef uint64_t        u64;
typedef unsigned char   u8;

/* Perf counters, one row each in opening order: X(slot, generic event, ARM raw
 * code, Markdown header, console label, console width).  The CTR_* enum and
 * counter_descs[] are both generated from this list, so a slot can't exist
 * without its descriptor.  ARM codes are speculative counts, so they include
 * wrong-path branches.  Not 0x21 for misses: that is BR_RETIRED, which counts
 * every retired branch. */
#define FOR_EACH_COUNTER(X) \
    X(BRANCHES, BRANCH_INSTRUCTIONS, 0x12, "Branches", "branches", 10) \
    X(MISSES,   BRANCH_MISSES,       0x10, "Misses",   "misses",    9) \
    X(CYCLES,   CPU_CYCLES,          0x11, "Cycles",   "cycles",   11)

#define CTR_ENUM(slot, ev, raw, md, label, width) CTR_##slot,
enum { FOR_EACH_COUNTER(CTR_ENUM) CTR_COUNT };

typedef struct {
    double time_ms;
    double time_min_ms;    /* fastest/slowest trial; equal to time_ms */
    double time_max_ms;    /* when trials == 1                        */
    u64    counts[CTR_COUNT]; /* 0 for any counter not opened         */
    int    counts_valid;   /* successful counter measurement for this trial */
    u64    result;         /* anti-dead-code-elimination sink         */
} Result;

/* Forces the optimizer to treat v as observed, so the computation that
 * produced it can't be proven dead and elided. Use whenever a test's
 * results aren't already consumed by something with observable side
 * effects (e.g. a printed sanity check).  A store to a volatile object
 * can't be elided, so writing v there is enough; it is never read back. */
static volatile u64 anti_dce_value;

static void anti_dce_sink(u64 v)
{
    anti_dce_value = v;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Timing
 * ───────────────────────────────────────────────────────────────────────── */
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Perf counters  (no-ops when unavailable)
 *
 * Call convention: perf_start() is called just *before* the now_ms() timer
 * starts, and perf_stop() just *after* it stops, so the ioctl/read syscalls
 * themselves are never counted in time_ms. This means the perf window is
 * very slightly wider than the timed window — counts[CTR_BRANCHES] and
 * counts[CTR_MISSES] can include a handful of instructions time_ms doesn't.
 * Negligible in magnitude (a few syscalls' worth out of millions of
 * branches), and intentional: the alternative is syscall overhead leaking
 * into time_ms.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct {
    uint64_t    generic;        /* PERF_TYPE_HARDWARE config, if HAVE_PERF */
    uint64_t    arm_raw;        /* ARM PMU raw event fallback code         */
    const char *md_header;      /* Markdown table column header            */
    const char *console_label;  /* Console label before the count          */
    int         console_width;  /* Field width for the console count       */
} CounterDesc;

/* Generic hardware event code; never opened without HAVE_PERF. */
#ifdef HAVE_PERF
#  define PERF_HW(ev) PERF_COUNT_HW_##ev
#else
#  define PERF_HW(ev) 0
#endif

/* Indexed by CTR_* (opening order). */
#define CTR_DESC(slot, ev, raw, md, label, width) \
    { PERF_HW(ev), raw, md, label, width },
static const CounterDesc counter_descs[CTR_COUNT] = { FOR_EACH_COUNTER(CTR_DESC) };

/* Fails to compile when array a doesn't have exactly n entries (C99 has no
 * _Static_assert). */
#define CHECK_LEN(name, a, n) \
    typedef char name[sizeof (a) / sizeof (a)[0] == (n) ? 1 : -1]

/* Column order in console and Markdown output (distinct from opening order).
 * Each CTR_* must appear exactly once; CHECK_LEN fails the build when the
 * entry count differs from CTR_COUNT. */
static const int counter_display_order[] = {
    CTR_CYCLES, CTR_BRANCHES, CTR_MISSES
};
CHECK_LEN(counter_display_order_len_check, counter_display_order, CTR_COUNT);

/* Bit c set when counter c opened only as its ARM raw event because the
 * generic one failed; stays 0 on other architectures and without HAVE_PERF. */
static unsigned perf_raw_mask = 0;

#ifdef HAVE_PERF
/* All counters are opened as one perf group so the kernel schedules them
 * together and they cover exactly the same window.  The first counter that
 * opens becomes the group leader; one that fails to open is left out.
 * perf_open_mask, not perf_fd[], says which counters are open, so the
 * zero-initialized state means "none open" before perf_init() runs. */
static int      perf_fd[CTR_COUNT];
static int      perf_slot[CTR_COUNT];   /* position in the group read buffer */
static unsigned perf_open_mask = 0;     /* bit c set once counter c opened   */
static int      perf_leader    = -1;
static int      perf_members   = 0;
static u64      perf_enabled_start;
static u64      perf_running_start;
static int      perf_started;

static int perf_has(int ctr) { return (perf_open_mask >> ctr) & 1u; }

static int perf_open(uint32_t type, uint64_t config, int group_fd)
{
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type           = type;
    pe.size           = sizeof(pe);
    pe.config         = config;
    pe.disabled       = (group_fd == -1);   /* members follow the leader */
    pe.exclude_kernel = 1;
    pe.exclude_hv     = 1;
    pe.read_format    = PERF_FORMAT_GROUP |
                        PERF_FORMAT_TOTAL_TIME_ENABLED |
                        PERF_FORMAT_TOTAL_TIME_RUNNING;
    return (int)syscall(__NR_perf_event_open, &pe, 0, -1, group_fd, 0);
}

/* Adds one counter to the group: the generic hardware event first, then
 * on ARM the architectural PMU event code (ARMv7 and ARMv8 alike). */
static void perf_add(int ctr)
{
    const CounterDesc *d = &counter_descs[ctr];
    int fd = perf_open(PERF_TYPE_HARDWARE, d->generic, perf_leader);
    unsigned raw = 0;
#if defined(__aarch64__) || defined(__arm__)
    if (fd < 0) {
        fd = perf_open(PERF_TYPE_RAW, d->arm_raw, perf_leader);
        raw = 1;
    }
#endif
    if (fd < 0) return;
    if (perf_leader < 0) perf_leader = fd;
    perf_fd[ctr]    = fd;
    perf_slot[ctr]  = perf_members++;
    perf_open_mask |= 1u << ctr;
    perf_raw_mask  |= raw << ctr;
}

static void perf_init(void)
{
    for (int c = 0; c < CTR_COUNT; c++)
        perf_add(c);
}

static void perf_close(void)
{
    for (int c = 0; c < CTR_COUNT; c++)
        if (perf_has(c)) close(perf_fd[c]);
}

/* Scheduling times are cumulative: PERF_EVENT_IOC_RESET resets counts only. */
static int perf_read_group(u64 *buf)
{
    ssize_t want = (ssize_t)((3 + (size_t)perf_members) * sizeof(u64));
    return read(perf_leader, buf, (size_t)want) == want &&
           buf[0] == (u64)perf_members;
}

static void perf_start(void)
{
    perf_started = 0;
    if (perf_leader < 0) return;
    if (ioctl(perf_leader, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) < 0)
        return;
    u64 buf[3 + CTR_COUNT];
    if (!perf_read_group(buf)) return;
    perf_enabled_start = buf[1];
    perf_running_start = buf[2];
    perf_started = ioctl(perf_leader, PERF_EVENT_IOC_ENABLE,
                         PERF_IOC_FLAG_GROUP) == 0;
}

static void perf_stop(Result *r)
{
    r->counts_valid = 0;
    if (perf_leader < 0) return;
    int stopped = ioctl(perf_leader, PERF_EVENT_IOC_DISABLE,
                        PERF_IOC_FLAG_GROUP) == 0;
    int started = perf_started;
    perf_started = 0;
    if (!started || !stopped) return;

    /* nr, time_enabled, time_running, then one value per member */
    u64 buf[3 + CTR_COUNT];
    if (!perf_read_group(buf) || buf[1] < perf_enabled_start ||
        buf[2] < perf_running_start)
        return;
    u64 enabled = buf[1] - perf_enabled_start;
    u64 running = buf[2] - perf_running_start;
    if (!running || running > enabled) return;

    double scale = 1.0;
    if (running < enabled) {       /* counters were time-shared this trial */
        static int warned = 0;
        scale = (double)enabled / (double)running;
        if (!warned) {
            fprintf(stderr, "Note: perf counters were shared with other users; "
                            "counts are scaled estimates.\n");
            warned = 1;
        }
    }
    for (int c = 0; c < CTR_COUNT; c++)
        if (perf_has(c))
            r->counts[c] = (u64)((double)buf[3 + perf_slot[c]] * scale);
    r->counts_valid = 1;
}

static int perf_available(void)  { return perf_leader >= 0; }

#else
static void perf_init(void)       {}
static void perf_close(void)      {}
static void perf_start(void)      {}
static void perf_stop(Result *r)  { (void)r; }
static int  perf_available(void)  { return 0; }
static int  perf_has(int ctr)     { (void)ctr; return 0; }
#endif

/* Opening a counter doesn't guarantee that a particular trial could read it. */
static int result_has(const Result *r, int ctr)
{
    return r->counts_valid && perf_has(ctr);
}

/* Sorts trials by time and returns the median one (the lower of the two
 * middle trials when n is even), so every field of the returned row comes
 * from the same real run.  Also records the fastest and slowest time. */
static Result median_trial(Result *t, int n)
{
    for (int i = 1; i < n; i++) {
        Result x = t[i];
        int j = i - 1;
        while (j >= 0 && t[j].time_ms > x.time_ms) {
            t[j + 1] = t[j];
            j--;
        }
        t[j + 1] = x;
    }
    Result m = t[(n - 1) / 2];
    m.time_min_ms = t[0].time_ms;
    m.time_max_ms = t[n - 1].time_ms;
    return m;
}

/* Cycles and time summed over every measured trial (warm-ups excluded), for
 * the average clock the results were actually produced at.  Cycles are
 * user-mode only, so time spent in the kernel or preempted slightly lowers
 * the figure; on an otherwise idle system that is well under 1%. */
static double clock_cycles_sum = 0;
static double clock_ms_sum     = 0;

static void account_clock(const Result *r)
{
    if (!result_has(r, CTR_CYCLES)) return;
    clock_cycles_sum += (double)r->counts[CTR_CYCLES];
    clock_ms_sum     += r->time_ms;
}

/* Average clock in GHz over all measured trials, or 0 if unknown. */
static double measured_ghz(void)
{
    return clock_ms_sum > 0 ? clock_cycles_sum / (clock_ms_sum * 1e6) : 0;
}

/* Runs one measured kernel call: counters wrap the timer, never the other
 * way round (see the call convention above).  A macro rather than a function
 * so the kernel is still called directly, keeping its codegen unchanged. */
#define MEASURE_ONCE(res, call)             \
    do {                                    \
        perf_start();                       \
        double t0_ = now_ms();              \
        (res).result = (call);              \
        (res).time_ms = now_ms() - t0_;     \
        perf_stop(&(res));                  \
        account_clock(&(res));              \
    } while (0)

/* `trials` limited to 1..MAX_TRIALS, so MEASURE always has at least one
 * trial to report and never overruns its trial array. */
static int trial_count(void)
{
    if (trials < 1) return 1;
    if (trials > MAX_TRIALS) return MAX_TRIALS;
    return trials;
}

/* Measures `call` `trials` times and keeps the median trial.  With more than
 * one trial, an untimed warm-up run comes first so the first trial doesn't
 * pay for cold caches, an untrained predictor or a CPU still ramping up. */
#define MEASURE(res, call)                                  \
    do {                                                    \
        Result trial_[MAX_TRIALS];                          \
        const int n_trials_ = trial_count();                \
        if (n_trials_ > 1)                                  \
            anti_dce_sink(call);                            \
        for (int k_ = 0; k_ < n_trials_; k_++) {            \
            memset(&trial_[k_], 0, sizeof(trial_[k_]));     \
            MEASURE_ONCE(trial_[k_], call);                 \
        }                                                   \
        (res) = median_trial(trial_, n_trials_);            \
    } while (0)

/* ─────────────────────────────────────────────────────────────────────────
 * Fast RNG — xorshift64, avoids stdlib rand() overhead and bias
 * ───────────────────────────────────────────────────────────────────────── */
static u64 rng_state = 0xdeadbeefcafe1234ULL;

static inline u64 rng64(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Formatted report output (-o/--output)
 *
 * The console output above is meant to be read as it scrolls by. This
 * writes the same numbers to a Markdown file as proper tables, so results
 * can be dropped straight into RESULTS.md instead of pasted as a raw
 * terminal transcript.
 * ───────────────────────────────────────────────────────────────────────── */
#ifdef __linux__
/* If `line` is a /proc/cpuinfo CPU-model entry with a non-empty value, copy
 * the value (minus its trailing newline) into buf and return 1; otherwise
 * leave buf untouched and return 0.
 * x86: "model name\t: ..."   ARM: "Model\t: ..." (falls back to
 * "Hardware" on some older ARM kernels if "Model" is absent) */
static int cpuinfo_model_value(const char *line, char *buf, size_t bufsz)
{
    if (strncmp(line, "model name", 10) != 0 &&
        strncmp(line, "Model", 5) != 0 &&
        strncmp(line, "Hardware", 8) != 0)
        return 0;
    const char *value = strchr(line, ':');
    if (!value) return 0;
    value++;
    while (*value == ' ' || *value == '\t') value++;
    size_t len = strlen(value);
    while (len && (value[len-1] == '\n' || value[len-1] == '\r'))
        len--;
    if (!len) return 0;
    snprintf(buf, bufsz, "%.*s", (int)len, value);
    return 1;
}
#endif

static void get_cpu_model(char *buf, size_t bufsz)
{
    snprintf(buf, bufsz, "Unknown CPU");
#ifdef __linux__
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (!cpuinfo_model_value(line, buf, bufsz)) continue;
        if (strncmp(line, "model name", 10) == 0)
            break;  /* prefer x86 "model name" over any later match */
    }
    fclose(f);
#endif
}

/* Highest maximum clock of any CPU in GHz, from cpufreq sysfs (turbo/boost
 * included), or 0 if unknown (e.g. WSL2 and many VMs).  The highest rather
 * than cpu0's, since on hybrid CPUs cpu0 may be a slower core. */
static double cpu_max_ghz(void)
{
    long khz_max = 0;
#ifdef __linux__
    long ncpu = sysconf(_SC_NPROCESSORS_CONF);
    for (long c = 0; c < ncpu; c++) {
        char path[96];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%ld/cpufreq/cpuinfo_max_freq", c);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        long khz = 0;
        if (fscanf(f, "%ld", &khz) == 1 && khz > khz_max)
            khz_max = khz;
        fclose(f);
    }
#endif
    return (double)khz_max / 1e6;
}

/* Report heading: the CPU model, with any "@ <base clock>" it carries
 * replaced by the maximum clock when that is known. */
static void cpu_heading(char *buf, size_t bufsz)
{
    get_cpu_model(buf, bufsz);
    double ghz = cpu_max_ghz();
    if (ghz <= 0) return;
    char *at = strstr(buf, " @ ");
    if (at) *at = '\0';
    size_t len = strlen(buf);
    snprintf(buf + len, bufsz - len, " @ %.2f GHz", ghz);
}

/* Work done per trial, for the misses-per-item column: elements processed
 * (tests 1/2/4, all passes) or indirect calls made (test 3). */
typedef struct {
    u64         count;
    const char *unit;      /* "elem" or "call" */
} Work;

typedef struct {
    const char *label;
    Result      r;
    const Work *work;      /* this row's work, or NULL for the test's Work */
} ReportRow;

/* The Work that applies to one row: its own, else the test's. */
static const Work *row_work(const ReportRow *row, const Work *w)
{
    return row->work ? row->work : w;
}

static double misses_per_item(const Result *r, const Work *w)
{
    return w->count ? (double)r->counts[CTR_MISSES] / (double)w->count : 0;
}

/* Writes `s` to `f` with runs of two or more spaces collapsed to one space,
 * so console padding inside a row label does not appear in Markdown tables. */
static void fputs_collapsed(FILE *f, const char *s)
{
    for (; *s; s++) {
        if (s[0] == ' ' && s[1] == ' ') continue;
        fputc(*s, f);
    }
}

/* Emits one Markdown table (variant × time/counters/miss rates) for a
 * test, given its already-computed Result rows. */
static void report_table(FILE *f, const char *title, const char *desc,
                         const ReportRow *rows, int n, const Work *w)
{
    if (!f) return;
    fprintf(f, "### %s\n\n", title);
    if (desc) fprintf(f, "%s\n\n", desc);
    fprintf(f, "| Variant | Time (ms) |");
    if (trials > 1) fprintf(f, " Range (ms) |");
    for (int k = 0; k < CTR_COUNT; k++)
        fprintf(f, " %s |", counter_descs[counter_display_order[k]].md_header);
    fprintf(f, " Miss %% | Misses/%s |\n", w->unit);
    fprintf(f, "|---|---:|");
    if (trials > 1) fprintf(f, "---:|");
    for (int k = 0; k < CTR_COUNT; k++)
        fprintf(f, "---:|");
    fprintf(f, "---:|---:|\n");
    for (int i = 0; i < n; i++) {
        const Result *r = &rows[i].r;
        const u64 branches = r->counts[CTR_BRANCHES];
        const u64 misses   = r->counts[CTR_MISSES];
        fprintf(f, "| ");
        fputs_collapsed(f, rows[i].label);
        fprintf(f, " | %.1f |", r->time_ms);
        if (trials > 1)
            fprintf(f, " %.1f–%.1f |", r->time_min_ms, r->time_max_ms);
        for (int k = 0; k < CTR_COUNT; k++) {
            int c = counter_display_order[k];
            if (result_has(r, c))
                fprintf(f, " %" PRIu64 " |", r->counts[c]);
            else
                fprintf(f, " – |");
        }
        if (result_has(r, CTR_BRANCHES) && result_has(r, CTR_MISSES) && branches)
            fprintf(f, " %.1f%% |", 100.0 * (double)misses / (double)branches);
        else
            fprintf(f, " – |");
        if (result_has(r, CTR_MISSES))
            fprintf(f, " %.3f |\n", misses_per_item(r, row_work(&rows[i], w)));
        else
            fprintf(f, " – |\n");
    }
    fprintf(f, "\n");
}

/* Console counterpart of report_table(): one aligned result row. */
static void print_row(const char *label, const Result *r, const Work *w)
{
    const u64 branches = r->counts[CTR_BRANCHES];
    const u64 misses   = r->counts[CTR_MISSES];
    printf("  %-38s  %7.1f ms", label, r->time_ms);
    if (trials > 1)
        printf(" (%7.1f–%7.1f)", r->time_min_ms, r->time_max_ms);
    for (int k = 0; k < CTR_COUNT; k++) {
        int c = counter_display_order[k];
        if (!result_has(r, c)) continue;
        const CounterDesc *d = &counter_descs[c];
        printf("  %s %*" PRIu64, d->console_label, d->console_width, r->counts[c]);
        if (c == CTR_MISSES) {
            printf("  (");
            if (result_has(r, CTR_BRANCHES) && branches)
                printf("%.1f%%, ", 100.0 * (double)misses / (double)branches);
            printf("%.3f/%s)", misses_per_item(r, w), w->unit);
        }
    }
    if (perf_available() && !r->counts_valid)
        printf("  (hardware counts unavailable)");
    printf("\n");
}

/* Extra cycles per extra miss between a predictable and an unpredictable
 * run of the same work: the effective cost of one misprediction.  Skipped
 * when the counters are missing or the miss difference is too small to
 * give a meaningful ratio. */
static void print_miss_cost(FILE *report, const Result *predictable,
                            const Result *unpredictable)
{
    if (!result_has(predictable, CTR_CYCLES) || !result_has(predictable, CTR_MISSES) ||
        !result_has(unpredictable, CTR_CYCLES) || !result_has(unpredictable, CTR_MISSES))
        return;
    const u64 pred_misses   = predictable->counts[CTR_MISSES];
    const u64 unpred_misses = unpredictable->counts[CTR_MISSES];
    const u64 pred_cycles   = predictable->counts[CTR_CYCLES];
    const u64 unpred_cycles = unpredictable->counts[CTR_CYCLES];
    if (unpred_misses < pred_misses + 1000 || unpred_cycles <= pred_cycles)
        return;
    double cost = (double)(unpred_cycles - pred_cycles) /
                  (double)(unpred_misses - pred_misses);
    printf("  → Cost per miss: %.1f cycles\n", cost);
    if (report) fprintf(report, "**Cost per miss:** %.1f cycles\n\n", cost);
}

/* Emits all console rows and the Markdown table for a test. */
static void emit_rows(FILE *report, const char *title, const char *desc,
                      const ReportRow *rows, int n, const Work *w)
{
    for (int i = 0; i < n; i++)
        print_row(rows[i].label, &rows[i].r, row_work(&rows[i], w));
    report_table(report, title, desc, rows, n, w);
}

/* Emits a ratio summary line to both console and the Markdown report. */
static void emit_ratio(FILE *report, const char *console_prefix,
                       const char *md_label, double num_ms, double denom_ms,
                       const char *suffix)
{
    double ratio = num_ms / denom_ms;
    printf("%s%.2f×%s\n", console_prefix, ratio, suffix);
    if (report)
        fprintf(report, "**%s:** %.2f×%s\n\n", md_label, ratio, suffix);
}

/* Standard 2-row emission (predictable vs unpredictable): console rows,
 * Markdown table, slowdown ratio, and cost per miss. */
static void emit_pair_summary(FILE *report, const char *title, const char *desc,
                              const ReportRow rows[2], const Work *w)
{
    emit_rows(report, title, desc, rows, 2, w);
    emit_ratio(report, "  → Slowdown:  ", "Slowdown",
               rows[1].r.time_ms, rows[0].r.time_ms, "");
    print_miss_cost(report, &rows[0].r, &rows[1].r);
    printf("\n");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Array utilities
 * ───────────────────────────────────────────────────────────────────────── */
static int cmp_u8_asc(const void *a, const void *b)
{
    return (int)*(const u8*)a - (int)*(const u8*)b;
}

static void fill_random_bytes(u8 *arr, size_t n)
{
    for (size_t i = 0; i < n; i++)
        arr[i] = (u8)(rng64() & 0xff);
}

static void shuffle_u8(u8 *arr, size_t n)
{
    for (size_t i = n - 1; i > 0; --i) {
        size_t j = rng64() % (i + 1);
        u8 tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *
 *  TEST 1 — Threshold Sum: Sorted vs Shuffled Array
 *
 *  Accumulates all elements > THRESHOLD.  Both arrays contain identical data,
 *  just in different orders.
 *
 *  Sorted:   the branch outcome is "not-taken" for the first ~50% of the
 *            array, then "always-taken" for the rest.  The predictor finds
 *            the crossover within a few iterations and then has near-zero
 *            mispredictions.
 *
 *  Shuffled: each element's outcome is independent and ~50/50.  The predictor
 *            can do no better than chance, yielding ~50% misprediction rate.
 *
 * ═══════════════════════════════════════════════════════════════════════════ */
__attribute__((noinline))
static u64 sum_threshold(const u8 *arr, size_t n, unsigned n_reps)
{
    u64 acc = 0;
    for (unsigned rep = 0; rep < n_reps; rep++)
        for (size_t i = 0; i < n; i++)
            if (arr[i] > THRESHOLD)   /* <── THE branch being predicted */
                acc += arr[i];
    return acc;
}

static void run_test1(FILE *report)
{
    /* Read once, so the allocation and every loop provably use the same length. */
    const size_t len = size;
    u8 *sorted   = malloc(len);
    u8 *shuffled = malloc(len);
    if (!sorted || !shuffled) { perror("malloc"); exit(1); }

    /* Fill with uniform random bytes, then sort one copy */
    fill_random_bytes(sorted, len);
    memcpy(shuffled, sorted, len);
    qsort(sorted, len, 1, cmp_u8_asc);
    shuffle_u8(shuffled, len);

    printf("TEST 1 — Threshold Sum  (sorted vs shuffled array, %zu×%u reps)\n",
           len, reps);
    printf("  Counts/sums elements > %d.  Identical data, different order.\n\n",
           THRESHOLD);

    Result rs = {0};
    Result rr = {0};
    MEASURE(rs, sum_threshold(sorted, len, reps));
    MEASURE(rr, sum_threshold(shuffled, len, reps));

    anti_dce_sink(rs.result + rr.result);

    free(sorted); free(shuffled);

    const Work work = { (u64)len * reps, "elem" };
    const ReportRow rows[] = {
        { "Sorted   (predictable, ~0% misses)",    rs, NULL },
        { "Shuffled (unpredictable, ~50% misses)", rr, NULL },
    };
    emit_pair_summary(report, "Test 1 — Threshold Sum",
                      "Sorted vs shuffled array, same direct branch. Identical data, different order.",
                      rows, &work);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *
 *  TEST 2 — Stride Conditional: Periodic vs Random Same-Rate Pattern
 *
 *  Counts how many array elements pass a condition.  Both variants have the
 *  same ~25% branch-taken rate, but one is perfectly periodic and the other
 *  is random.
 *
 *  Periodic ("every 4th"):  the predictor learns the T-N-N-N-T-N-N-N cycle
 *                           within the first few passes → nearly zero misses.
 *
 *  Random 25%:              a lookup table of pre-randomised decisions
 *                           (generated offline so RNG overhead is excluded).
 *                           Same take-rate but no exploitable pattern.
 *
 * ═══════════════════════════════════════════════════════════════════════════ */

__attribute__((noinline))
static u64 count_decisions(const u8 *decisions, size_t n, unsigned n_reps)
{
    u64 acc = 0;
    for (unsigned rep = 0; rep < n_reps; rep++)
        for (size_t i = 0; i < n; i++)
            if (decisions[i])   /* branch outcome determined by table */
                acc++;
    return acc;
}

static void run_test2(FILE *report)
{
    const size_t len = size;
    /* Pre-generated branch decision arrays (avoids RNG in hot loop) */
    u8 *decisions_periodic = malloc(len);  /* 1 at every 4th position        */
    u8 *decisions_random   = malloc(len);  /* 1 with 25% probability, random */
    if (!decisions_periodic || !decisions_random) { perror("malloc"); exit(1); }

    for (size_t i = 0; i < len; i++) {
        decisions_periodic[i] = ((i & 3) == 0) ? 1 : 0;
        decisions_random[i] = (((rng64() >> 32) % 4) == 0) ? 1 : 0;
    }

    printf("TEST 2 — Stride Conditional  (periodic vs random 25%%, %zu×%u reps)\n",
           len, reps);
    printf("  Both arrays have ~25%% ones; only the pattern differs.\n\n");

    Result rp = {0};
    Result rr = {0};
    MEASURE(rp, count_decisions(decisions_periodic, len, reps));
    MEASURE(rr, count_decisions(decisions_random, len, reps));

    anti_dce_sink(rp.result + rr.result);

    free(decisions_periodic);
    free(decisions_random);

    const Work work = { (u64)len * reps, "elem" };
    const ReportRow rows[] = {
        { "Periodic (every 4th — learnable)",   rp, NULL },
        { "Random   (same rate — unlearnable)", rr, NULL },
    };
    emit_pair_summary(report, "Test 2 — Stride Conditional",
                      "Both arrays have ~25% ones; only the pattern differs.",
                      rows, &work);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *
 *  TEST 3 — Indirect Branch Dispatch: Sequential vs Random  (indirect predictor)
 *
 *  Calls one of NUM_FUNCS trivial leaf functions via a function-pointer table.
 *  The CPU's indirect-branch predictor tries to guess the *target address* of
 *  each indirect call, not just taken/not-taken.  A plain Branch Target
 *  Buffer (BTB) only remembers the last target of each branch, which is
 *  wrong on every call here, so any learning comes from a predictor that
 *  also uses branch history.
 *
 *  Sequential (i % NUM_FUNCS):  a repeating cycle of 32 targets.  History-
 *                                based indirect predictors (e.g. Haswell and
 *                                newer) learn it → almost no misses; older
 *                                cores (e.g. Ivy Bridge, Cortex-A72) miss
 *                                ~28 of every 32 calls.
 *
 *  Random:                      the next target is drawn from a pre-shuffled
 *                               array of random indices → nothing to learn
 *                               → right only by chance, ~1 call in 32.
 *
 *  This mirrors stress-ng's --branch and --icache stressors.
 *
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Generate NUM_FUNCS distinct leaf functions via X-macro ─────────────────── */
typedef u64 (*leaf_fn_t)(u64);

#define FOR_EACH_LEAF(F) \
    F( 0) F( 1) F( 2) F( 3) F( 4) F( 5) F( 6) F( 7) \
    F( 8) F( 9) F(10) F(11) F(12) F(13) F(14) F(15) \
    F(16) F(17) F(18) F(19) F(20) F(21) F(22) F(23) \
    F(24) F(25) F(26) F(27) F(28) F(29) F(30) F(31)

/* Each function does one unique arithmetic op so it can't be merged by the
 * linker's identical-code folding (ICF). The constant is a unique Weyl-
 * sequence step derived from the golden ratio. */
#define DEF_LEAF(n) \
    __attribute__((noinline)) \
    static u64 leaf_##n(u64 x) { return x ^ ((u64)(n) * 0x9e3779b97f4a7c15ULL); }

FOR_EACH_LEAF(DEF_LEAF)

#define PTR_LEAF(n) leaf_##n,
static leaf_fn_t leaf_table[NUM_FUNCS] = { FOR_EACH_LEAF(PTR_LEAF) };

__attribute__((noinline))
static u64 dispatch_sequential(size_t n)
{
    u64 acc = 0;
    for (size_t i = 0; i < n; i++)
        acc = leaf_table[i % NUM_FUNCS](acc);
    return acc;
}

__attribute__((noinline))
static u64 dispatch_random(const u8 *idx, size_t n)
{
    u64 acc = 0;
    for (size_t i = 0; i < n; i++)
        acc = leaf_table[idx[i]](acc);
    return acc;
}

static void run_test3(FILE *report)
{
    const size_t len = size;
    /* Pre-built random index sequence (uint8_t → 1 byte/call → cache-friendly) */
    u8 *dispatch_indices = malloc(len);
    if (!dispatch_indices) { perror("malloc"); exit(1); }
    for (size_t i = 0; i < len; i++)
        dispatch_indices[i] = (u8)((rng64() >> 32) % NUM_FUNCS);

    if (len % (1u << 20) == 0)
        printf("TEST 3 — Indirect Dispatch  (%u targets, %zuM calls)\n",
               NUM_FUNCS, len >> 20);
    else
        printf("TEST 3 — Indirect Dispatch  (%u targets, %zu calls)\n",
               NUM_FUNCS, len);
    printf("  Calls leaf functions via pointer. The indirect predictor must guess the target address.\n\n");

    Result rs = {0};
    Result rr = {0};
    MEASURE(rs, dispatch_sequential(len));
    MEASURE(rr, dispatch_random(dispatch_indices, len));

    anti_dce_sink(rs.result + rr.result);

    free(dispatch_indices);

    const Work work = { len, "call" };
    const ReportRow rows[] = {
        { "Sequential i%32 (learnable cycle)", rs, NULL },
        { "Random index (unlearnable)",        rr, NULL },
    };
    emit_pair_summary(report, "Test 3 — Indirect Dispatch",
                      "32 targets, function-pointer call. The indirect predictor must guess the target address.",
                      rows, &work);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *
 *  TEST 4 — Branch vs Branchless Computation  (misprediction cost isolation)
 *
 *  Both variants compute the same result: sum of elements > THRESHOLD.
 *  Tested on sorted data (predictable) and random data (unpredictable).
 *
 *  Branch version:
 *      if (arr[i] > T) acc += arr[i];
 *    The compiler emits a conditional jump.  On random data this mispredicts
 *    ~50% of the time.
 *
 *  Branchless version (arithmetic mask):
 *      mask = 0 - (u64)(arr[i] > T);   → 0 or 0xFFFF...FFFF
 *      acc += arr[i] & mask;
 *    No conditional jump at all — the comparison result is a 0/1 integer
 *    that is sign-extended into a bitmask.  Performance is the same regardless
 *    of data distribution.
 *
 *  Expected pattern:
 *      Sorted   + branch    ≈ Sorted   + branchless  (few misses anyway)
 *      Random   + branch    >> Random  + branchless  (misprediction penalty)
 *
 *  This directly quantifies the penalty per misprediction and shows why
 *  modern compilers emit CMOV/branchless code for data-independent conditions.
 *
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Set when Test 4's branch and branchless sums differ; main() then exits 1. */
static int sanity_failed = 0;

/* Branch version — kept as a branch by -fno-if-conversion build flag */
__attribute__((noinline))
static u64 sum_branch(const u8 *arr, size_t n, unsigned n_reps)
{
    u64 acc = 0;
    for (unsigned rep = 0; rep < n_reps; rep++)
        for (size_t i = 0; i < n; i++)
            if (arr[i] > THRESHOLD)
                acc += arr[i];
    return acc;
}

/* Branchless version — arithmetic mask, no conditional jump possible */
__attribute__((noinline))
static u64 sum_branchless(const u8 *arr, size_t n, unsigned n_reps)
{
    u64 acc = 0;
    for (unsigned rep = 0; rep < n_reps; rep++)
        for (size_t i = 0; i < n; i++) {
            u64 mask = (u64)0 - (u64)(arr[i] > THRESHOLD);  /* 0 or ~0ULL */
            acc += arr[i] & mask;
        }
    return acc;
}

static void run_test4(FILE *report)
{
    const size_t len = size;
    u8 *sorted   = malloc(len);
    u8 *random_d = malloc(len);
    if (!sorted || !random_d) { perror("malloc"); exit(1); }

    fill_random_bytes(sorted, len);
    qsort(sorted, len, 1, cmp_u8_asc);
    fill_random_bytes(random_d, len);

    printf("TEST 4 — Branch vs Branchless  (sorted and random data, %zu×%u reps)\n",
           len, reps);
    printf("  Branchless uses arithmetic mask: 0 - (u64)(v > T) to avoid jumps.\n\n");

    Result r_sb = {0};
    Result r_sl = {0};
    Result r_rb = {0};
    Result r_rl = {0};
    MEASURE(r_sb, sum_branch(sorted, len, reps));
    MEASURE(r_sl, sum_branchless(sorted, len, reps));
    MEASURE(r_rb, sum_branch(random_d, len, reps));
    MEASURE(r_rl, sum_branchless(random_d, len, reps));

    free(sorted); free(random_d);

    /* verify both produce same result on the same data (sanity check) —
     * this comparison already reads all four .result fields, so unlike
     * tests 1-3 there's no separate anti-DCE sink needed here. */
    if (r_sb.result != r_sl.result || r_rb.result != r_rl.result) {
        fprintf(stderr, "  ERROR: branch/branchless results differ!\n");
        sanity_failed = 1;
    }

    const Work work = { (u64)len * reps, "elem" };
    const ReportRow rows[] = {
        { "Sorted  + branch",     r_sb, NULL },
        { "Sorted  + branchless", r_sl, NULL },
        { "Random  + branch",     r_rb, NULL },
        { "Random  + branchless", r_rl, NULL },
    };
    emit_rows(report, "Test 4 — Branch vs Branchless",
              "Same sum, computed via conditional jump vs. arithmetic mask, on sorted and random data.",
              rows, 4, &work);
    emit_ratio(report, "\n  Branch penalty on random data:  ",
               "Branch penalty on random data", r_rb.time_ms, r_sb.time_ms,
               " vs sorted-branch");
    emit_ratio(report, "  Branchless is consistent:       ",
               "Branchless is consistent", r_rl.time_ms, r_sl.time_ms,
               " random vs sorted");
    print_miss_cost(report, &r_sb, &r_rb);
    printf("\n");
}

/* ─────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────── */
static int is_opt(const char *arg, const char *short_name, const char *long_name)
{
    return (short_name && strcmp(arg, short_name) == 0) || strcmp(arg, long_name) == 0;
}

/* Returns the value following option `arg`, or NULL (after printing an
 * error) if the command line ends first. */
static const char *option_value(int argc, char **argv, int *i,
                                const char *arg, const char *what)
{
    if (*i >= argc) {
        fprintf(stderr, "Missing %s after %s (try --help)\n", what, arg);
        return NULL;
    }
    return argv[(*i)++];
}

/* Parses a whole number in [lo, hi] with an optional K or M suffix
 * (×1024, ×1024²).  Prints an error and returns 0 if invalid. */
static int parse_count(const char *s, const char *what,
                       unsigned long lo, unsigned long hi, unsigned long *out)
{
    const char *rest = s;               /* first character after the number */
    unsigned long v = 0;
    unsigned long mult = 1;
    const int has_digits = (*s >= '0' && *s <= '9');  /* strtoul would accept "-1" */
    if (has_digits) {
        char *end;
        v = strtoul(s, &end, 10);
        rest = end;
    }
    if (*rest == 'K' || *rest == 'k') {
        mult = 1UL << 10;
        rest++;
    } else if (*rest == 'M' || *rest == 'm') {
        mult = 1UL << 20;
        rest++;
    }
    if (!has_digits || *rest != '\0' || v > hi / mult || v * mult < lo) {
        fprintf(stderr, "Invalid %s: %s (must be %lu–%lu)\n", what, s, lo, hi);
        return 0;
    }
    *out = v * mult;
    return 1;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n"
           "  -o, --output <file>  write a formatted Markdown report there\n"
           "                       (in addition to the normal console output)\n"
           "  -n, --trials <N>     measure each variant N times (1–%d) after one\n"
           "                       warm-up run and report the median trial\n"
           "                       (default: 1, a single measurement, no warm-up)\n"
           "  -t, --test <N>       run only test N (1–%d); repeat to run several\n"
           "                       (default: all tests)\n"
           "      --size <N>       elements per array (tests 1/2/4) and calls\n"
           "                       per trial (test 3); K/M suffixes allowed\n"
           "                       (default: %u = 4M, max: %lu = 1024M)\n"
           "      --reps <N>       passes over the array per trial, tests 1/2/4\n"
           "                       (default: %u, max: %lu)\n"
           "  -h, --help           show this help\n",
           prog, MAX_TRIALS, NUM_TESTS, DEFAULT_SIZE, MAX_SIZE, DEFAULT_REPS, MAX_REPS);
}

/* option_value() followed by parse_count(), for options taking a number. */
static int option_number(int argc, char **argv, int *i, const char *what,
                         unsigned long lo, unsigned long hi, unsigned long *out)
{
    const char *val = option_value(argc, argv, i, argv[*i - 1], what);
    return val && parse_count(val, what, lo, hi, out);
}

/* Applies the option at argv[*i], advancing *i past it and its value.
 * Returns -1 to go on, or the exit code to stop with (0 after --help,
 * 1 on an invalid option). */
static int apply_option(int argc, char **argv, int *i, const char **out_path)
{
    const char *arg = argv[(*i)++];
    unsigned long v;
    if (is_opt(arg, "-h", "--help")) {
        print_usage(argv[0]);
        return 0;
    }
    if (is_opt(arg, "-o", "--output")) {
        *out_path = option_value(argc, argv, i, arg, "file name");
        return *out_path ? -1 : 1;
    }
    if (is_opt(arg, "-n", "--trials")) {
        if (!option_number(argc, argv, i, "trial count", 1, MAX_TRIALS, &v)) return 1;
        trials = (int)v;
        return -1;
    }
    if (is_opt(arg, "-t", "--test")) {
        if (!option_number(argc, argv, i, "test number", 1, NUM_TESTS, &v)) return 1;
        tests_selected |= 1u << (v - 1);
        return -1;
    }
    if (is_opt(arg, NULL, "--size")) {
        if (!option_number(argc, argv, i, "size", 1, MAX_SIZE, &v)) return 1;
        size = (size_t)v;
        return -1;
    }
    if (is_opt(arg, NULL, "--reps")) {
        if (!option_number(argc, argv, i, "repetition count", 1, MAX_REPS, &v)) return 1;
        reps = (unsigned)v;
        return -1;
    }
    fprintf(stderr, "Unknown argument: %s (try --help)\n", arg);
    return 1;
}

/* Applies all command-line options.  Returns -1 to go on and run, or the
 * exit code to stop with. */
static int parse_args(int argc, char **argv, const char **out_path)
{
    int i = 1;
    while (i < argc) {
        int rc = apply_option(argc, argv, &i, out_path);
        if (rc >= 0) return rc;
    }
    return -1;
}

static int test_selected(int n)
{
    return tests_selected == 0 || ((tests_selected >> (n - 1)) & 1u);
}

/* Comma-separated list of the selected tests, e.g. "1, 3". */
static void selected_tests_list(char *buf, size_t bufsz)
{
    size_t len = 0;
    buf[0] = '\0';
    for (int n = 1; n <= NUM_TESTS; n++) {
        if (!test_selected(n) || len >= bufsz) continue;
        int written = snprintf(buf + len, bufsz - len, "%s%d", len ? ", " : "", n);
        if (written < 0) break;
        len += (size_t)written;
    }
}

/* Counters that opened as raw events, e.g. "branches (0x12), cycles (0x11)". */
static void raw_events_list(char *buf, size_t bufsz)
{
    size_t len = 0;
    buf[0] = '\0';
    for (int c = 0; c < CTR_COUNT; c++) {
        if (!((perf_raw_mask >> c) & 1u) || len >= bufsz) continue;
        int written = snprintf(buf + len, bufsz - len, "%s%s (0x%" PRIx64 ")",
                               len ? ", " : "", counter_descs[c].console_label,
                               counter_descs[c].arm_raw);
        if (written < 0) break;
        len += (size_t)written;
    }
}

static void print_banner(const char *tests_list)
{
    printf("══════════════════════════════════════════════════════════════\n");
    printf("  CPU Branch Prediction Benchmark\n");
    printf("  Array size:      %10zu elements\n", size);
    printf("  Repetitions:     %10u per trial\n", reps);
    if (trials > 1)
        printf("  Trials:          %10d per variant (median shown, after 1 warm-up run)\n",
               trials);
    if (tests_selected)
        printf("  Tests:           %10s (selected with -t)\n", tests_list);
    printf("  Perf counters:   %s\n",
           perf_available() ? "AVAILABLE (hardware branch-miss counts shown)"
                            : "unavailable — showing wall-clock timing only");
    if (perf_raw_mask) {
        char raw[96];
        raw_events_list(raw, sizeof raw);
        printf("  Raw PMU events:  %s (generic events failed)\n", raw);
    }
    printf("  Compiler:        %s\n", COMPILER_VERSION);
    if (BUILD_FLAGS[0])
        printf("  Build flags:     %s\n", BUILD_FLAGS);
    if (BUILD_COMMIT[0])
        printf("  Commit:          %s\n", BUILD_COMMIT);
    printf("══════════════════════════════════════════════════════════════\n\n");
}

static void write_report_header(FILE *report, const char *tests_list)
{
    char cpu[192];
    cpu_heading(cpu, sizeof(cpu));
    time_t now = time(NULL);
    struct tm tm_now;
    char date[32] = "unknown date";
    if (localtime_r(&now, &tm_now))
        strftime(date, sizeof(date), "%Y-%m-%d", &tm_now);

    fprintf(report, "## %s\n\n", cpu);
    fprintf(report, "_%s_\n\n", date);
    fprintf(report, "| Array size | Repetitions | Perf counters | Compiler |\n");
    fprintf(report, "|---:|---:|---|---|\n");
    fprintf(report, "| %zu elements | %u/trial | %s | %s |\n\n",
            size, reps,
            perf_available() ? "available (hardware counts)" : "unavailable (wall-clock only)",
            COMPILER_VERSION);
    if (perf_raw_mask) {
        char raw[96];
        raw_events_list(raw, sizeof raw);
        fprintf(report, "_Raw ARM PMU events (generic events failed): %s_\n\n", raw);
    }
    if (BUILD_FLAGS[0])
        fprintf(report, "_Build flags: `%s`_\n\n", BUILD_FLAGS);
    if (BUILD_COMMIT[0])
        fprintf(report, "_Commit: `%s`_\n\n", BUILD_COMMIT);
    if (trials > 1)
        fprintf(report, "_Median of %d trials per variant, after one warm-up run. "
                        "Range is the fastest–slowest trial._\n\n", trials);
    if (tests_selected)
        fprintf(report, "_Only test(s) %s were run._\n\n", tests_list);
}

/* Average clock over all measured trials, when the cycle counter worked. */
static void print_clock_summary(FILE *report)
{
    double avg_ghz = measured_ghz();
    double max_ghz = cpu_max_ghz();
    if (avg_ghz <= 0) return;
    printf("Average clock during measurements: %.2f GHz", avg_ghz);
    if (max_ghz > 0) printf(" (max %.2f GHz)", max_ghz);
    printf("\n\n");
    if (report)
        fprintf(report, "**Average clock during measurements:** %.2f GHz\n\n", avg_ghz);
}

int main(int argc, char **argv)
{
    const char *out_path = NULL;
    int rc = parse_args(argc, argv, &out_path);
    if (rc >= 0) return rc;

    FILE *report = NULL;
    if (out_path) {
        /* open() rather than fopen() (0666), so a new report is owner-only. */
        int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) { perror(out_path); return 1; }
        report = fdopen(fd, "w");
        if (!report) { perror("fdopen"); close(fd); return 1; }
    }

    perf_init();

    char tests_list[32];
    selected_tests_list(tests_list, sizeof(tests_list));
    print_banner(tests_list);
    if (report)
        write_report_header(report, tests_list);

    if (test_selected(1)) run_test1(report);
    if (test_selected(2)) run_test2(report);
    if (test_selected(3)) run_test3(report);
    if (test_selected(4)) run_test4(report);

    print_clock_summary(report);

    printf("See README.md for how to interpret these results.\n");

    int report_failed = 0;
    if (report) {
        /* A previous buffered write may have failed even if close succeeds. */
        report_failed = ferror(report) != 0;
        if (fclose(report) != 0) report_failed = 1;
        if (report_failed)
            fprintf(stderr, "Error writing report to %s\n", out_path);
        else
            printf("\nFormatted report written to %s\n", out_path);
    }

    perf_close();

    return sanity_failed || report_failed ? 1 : 0;
}
