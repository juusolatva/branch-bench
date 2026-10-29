/*
 * branch_bench.c — CPU Branch Prediction Efficiency Benchmark
 *
 * Tests that contrast predictable vs unpredictable branching to measure the
 * real CPU cost of branch misprediction.  Tests 1-4 run by default; tests
 * 5-6 probe longer-range behaviour and run only with -t or --hard:
 *
 *  Test 1 — Threshold Sum       sorted vs shuffled array              (classic direct-branch demo)
 *  Test 2 — Bias and Ordering  5 biases × periodic/clustered/shuffled (bias vs sequence structure)
 *  Test 3 — Indirect Dispatch   cyclic vs shuffled, 1–64 targets       (indirect branch predictor)
 *  Test 4 — Branch vs Branchless sorted/shuffled × branch/arithmetic (branchless per order)
 *  Test 5 — Pattern Learnability de Bruijn periods 32..2048 vs random (pattern length, opt-in)
 *  Test 6 — Correlation Distance B copies A after d fillers vs random (correlation reach, opt-in)
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
 *       -fno-if-conversion keeps if-else as actual branch instructions (not CMOV)
 *       in the C kernels with a data branch (Test 6, and the C fallback of
 *       Tests 1, 2, 4 and 5 on architectures without the asm kernels).
 *       Test 4's branchless variant uses arithmetic masks — unaffected by these flags.
 *       clang rejects -fno-if-conversion and has no equivalent; it builds with
 *       -fno-vectorize -fno-slp-vectorize instead.  The asm kernels keep their
 *       branches, but clang (18-22) makes Test 6's C kernel branchless, so its
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
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/utsname.h>

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
#define DEFAULT_SIZE  (1u << 22)   /* 4 194 304 elements (tests 1/2/4/5/6) or calls (test 3) */
#define DEFAULT_REPS  64           /* passes (tests 1/2/4/5); test 6 budget  */
#define THRESHOLD     128          /* byte comparison threshold               */
#define NUM_FUNCS     64           /* leaf targets, largest count (test 3)    */
#define T3_COUNTS     7            /* target counts 1, 2, 4, ... 64 (test 3)  */
#define NUM_TESTS     6
#define DEFAULT_TESTS 0xFu         /* tests run without -t: 1–4 (5 and 6 are opt-in) */
#define HARD_TESTS    0x3Fu        /* tests run with --hard: all six              */
#define MAX_TRIALS    99           /* upper limit for -n/--trials             */
#define MAX_SIZE      (1UL << 30)  /* upper limit for --size                  */
#define MAX_REPS      100000UL     /* upper limit for --reps                  */
#define MAX_CPU       1023         /* highest CPU number for --cpu            */
#define BENCH_REVISION 4           /* bumped whenever measurements change     */
#define DEFAULT_SEED  0xdeadbeefcafe1234ULL  /* --seed default */

/* Test 2: biases (each with its periodic word) and their common period */
#define T2_BIASES      5
#define T2_PERIOD      16

/* Test 5: de Bruijn pattern orders k (periods 2^k) and the random reference */
#define DB_MIN_ORDER   5
#define DB_MAX_ORDER   11
#define DB_MAX_PERIOD  (1u << DB_MAX_ORDER)

/* ── Kernel backend ───────────────────────────────────────────────────────
 * The data-branch kernels (sum_threshold, count_decisions) are one asm
 * block each on x86-64 and AArch64, so no compiler can turn their branch
 * into a conditional move or duplicate it.  Elsewhere, or when built with
 * -DBRANCH_BENCH_C_KERNELS, they are plain C, whose branches depend on the
 * compiler (check them with .github/scripts/check_kernels.sh). */
#if defined(BRANCH_BENCH_C_KERNELS)
#  define KERNEL_ASM 0
#  define KERNEL_BACKEND "c"
#elif defined(__x86_64__)
#  define KERNEL_ASM 1
#  define KERNEL_BACKEND "asm-x86_64"
#elif defined(__aarch64__)
#  define KERNEL_ASM 1
#  define KERNEL_BACKEND "asm-aarch64"
#else
#  define KERNEL_ASM 0
#  define KERNEL_BACKEND "c"
#endif
/* How the asm data branches jump: they skip the update when the source
 * condition is false.  Unknown for C kernels. */
#if KERNEL_ASM
#  define ASM_TAKEN_WHEN "false"
#else
#  define ASM_TAKEN_WHEN NULL
#endif
#define BB_STR_(x) #x
#define BB_STR(x)  BB_STR_(x)

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

typedef uint64_t        u64;
typedef unsigned char   u8;

/* Run-time settings, changed only by command-line options. */
static size_t   size  = DEFAULT_SIZE;  /* --size: elements per array (tests 1/2/4),
                                          calls per trial (test 3) */
static unsigned reps  = DEFAULT_REPS;  /* --reps: passes over the array per trial
                                          (tests 1/2/4) */
static int      trials = 1;            /* -n/--trials: measured trials per variant, each
                                          after TRAIN_RUNS untimed runs */
static unsigned tests_selected = 0;    /* -t/--test bitmask (bit n-1 = test n);
                                          0 = DEFAULT_TESTS (1–4) */
static int      hard_selected = 0;     /* --hard: all tests, including 5 and 6 */
static u64      base_seed = DEFAULT_SEED;  /* --seed: every test's input derives from it */
static int      seed_given = 0;        /* --seed was on the command line */
static int      cpu_requested = -1;    /* --cpu: CPU to pin to, -1 = none */
static const char *json_path = NULL;   /* --json: JSON Lines export file */

/* --profile: which grids Tests 5 and 6 measure.  Tests 1–4 ignore it. */
typedef enum { PROFILE_QUICK, PROFILE_STANDARD, PROFILE_RESEARCH } Profile;
static const char *const profile_names[] = { "quick", "standard", "research" };
static Profile  profile = PROFILE_STANDARD;

/* ── Types ───────────────────────────────────────────────────────────────── */

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

/* Why a trial's counts are, or are not, usable.  PERF_NONE is 0, so a
 * zero-initialized Result never claims valid counts. */
enum {
    PERF_NONE,            /* no counter open                          */
    PERF_OK,              /* counts read for the whole trial          */
    PERF_START_FAILED,    /* reset, baseline read or enable failed    */
    PERF_STOP_FAILED,     /* disable failed                           */
    PERF_READ_FAILED,     /* short read or wrong group size           */
    PERF_TIMES_INVALID,   /* scheduling times went backwards          */
    PERF_NOT_SCHEDULED    /* the group never ran during the trial     */
};

static const char *perf_status_name(int s)
{
    static const char *const names[] = {
        "none", "ok", "start_failed", "stop_failed", "read_failed",
        "times_invalid", "not_scheduled"
    };
    return s >= 0 && s <= PERF_NOT_SCHEDULED ? names[s] : "unknown";
}

typedef struct {
    double time_ms;
    double time_min_ms;    /* fastest/slowest trial; equal to time_ms */
    double time_max_ms;    /* when trials == 1                        */
    u64    counts[CTR_COUNT];     /* scaled estimate; 0 for any counter not read */
    u64    raw_counts[CTR_COUNT]; /* as read, before scaling for time-sharing   */
    u64    enabled_ns;     /* this trial's group scheduling times     */
    u64    running_ns;
    unsigned open_mask;    /* counters open for this trial (perf_open_mask) */
    unsigned raw_mask;     /* ... and those opened as ARM raw events   */
    int    perf_status;    /* PERF_*: whether counts[] can be used    */
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

/* What population a counter counts.  A miss rate is only meaningful when
 * branches and misses count the same one. */
enum { SEM_UNKNOWN, SEM_RETIRED, SEM_SPECULATIVE, SEM_CYCLES };
enum { ARCH_X86, ARCH_ARM, ARCH_OTHER };

static const char *semantics_name(int sem)
{
    static const char *const names[] = { "unknown", "retired", "speculative", "cycles" };
    return names[sem];
}

static int host_arch(void)
{
#if defined(__x86_64__) || defined(__i386__)
    return ARCH_X86;
#elif defined(__aarch64__) || defined(__arm__)
    return ARCH_ARM;
#else
    return ARCH_OTHER;
#endif
}

/* x86: the generic branch and miss events are retired-branch events on
 * Intel and AMD.  ARM: Linux v6.12 drivers/perf/arm_pmuv3.c maps generic
 * branches to BR_RETIRED (0x21) or PC_WRITE_RETIRED (0x0C), both retired,
 * but generic misses to BR_MIS_PRED (0x10), which is speculative; the raw
 * fallbacks 0x12 and 0x10 are both speculative.  Other architectures are
 * unknown. */
static int counter_semantics(int ctr, int is_raw, int arch)
{
    if (ctr == CTR_CYCLES) return SEM_CYCLES;
    if (arch == ARCH_ARM)
        return is_raw || ctr == CTR_MISSES ? SEM_SPECULATIVE : SEM_RETIRED;
    if (arch == ARCH_X86 && !is_raw) return SEM_RETIRED;
    return SEM_UNKNOWN;
}

static int result_sem(const Result *r, int ctr)
{
    return counter_semantics(ctr, (int)((r->raw_mask >> ctr) & 1u), host_arch());
}

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

/* Stores the trial's counts and sets r->perf_status to exactly one PERF_*
 * reason on every path. */
static void perf_stop(Result *r)
{
    r->perf_status = PERF_NONE;
    r->open_mask   = perf_open_mask;
    r->raw_mask    = perf_raw_mask;
    if (perf_leader < 0) return;
    int stopped = ioctl(perf_leader, PERF_EVENT_IOC_DISABLE,
                        PERF_IOC_FLAG_GROUP) == 0;
    int started = perf_started;
    perf_started = 0;
    r->perf_status = PERF_START_FAILED;
    if (!started) return;
    r->perf_status = PERF_STOP_FAILED;
    if (!stopped) return;

    /* nr, time_enabled, time_running, then one value per member */
    u64 buf[3 + CTR_COUNT];
    r->perf_status = PERF_READ_FAILED;
    if (!perf_read_group(buf)) return;
    r->perf_status = PERF_TIMES_INVALID;
    if (buf[1] < perf_enabled_start || buf[2] < perf_running_start) return;
    u64 enabled = buf[1] - perf_enabled_start;
    u64 running = buf[2] - perf_running_start;
    if (running > enabled) return;
    r->perf_status = PERF_NOT_SCHEDULED;
    if (!running) return;

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
    for (int c = 0; c < CTR_COUNT; c++) {
        if (!perf_has(c)) continue;
        r->raw_counts[c] = buf[3 + perf_slot[c]];
        r->counts[c] = (u64)((double)r->raw_counts[c] * scale);
    }
    r->enabled_ns  = enabled;
    r->running_ns  = running;
    r->perf_status = PERF_OK;
}

static int perf_available(void)  { return perf_leader >= 0; }
static unsigned perf_mask(void)  { return perf_open_mask; }

#else
static void perf_init(void)       {}
static void perf_close(void)      {}
static void perf_start(void)      {}
static void perf_stop(Result *r)  { r->perf_status = PERF_NONE; r->raw_mask = perf_raw_mask; }
static int  perf_available(void)  { return 0; }
static unsigned perf_mask(void)   { return 0; }
#endif

/* Opening a counter doesn't guarantee that a particular trial could read it. */
static int result_has(const Result *r, int ctr)
{
    return r->perf_status == PERF_OK && ((r->open_mask >> ctr) & 1u);
}

/* Miss % only for a trial whose branch and miss counters count the same,
 * known population (e.g. not retired branches vs speculative misses). */
static int miss_rate_valid(const Result *r)
{
    if (!result_has(r, CTR_BRANCHES) || !result_has(r, CTR_MISSES) ||
        !r->counts[CTR_BRANCHES])
        return 0;
    const int sem = result_sem(r, CTR_BRANCHES);
    return sem != SEM_UNKNOWN && sem == result_sem(r, CTR_MISSES);
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

/* User-mode cycles and wall time summed over every measured trial (training
 * runs excluded).  Their ratio is an effective rate, not the core clock:
 * time spent in the kernel or preempted lowers it. */
static double clock_cycles_sum = 0;
static double clock_ms_sum     = 0;

static void account_clock(const Result *r)
{
    if (!result_has(r, CTR_CYCLES)) return;
    clock_cycles_sum += (double)r->counts[CTR_CYCLES];
    clock_ms_sum     += r->time_ms;
}

/* Effective user-cycle rate in GHz over all measured trials, or 0 if unknown. */
static double measured_ghz(void)
{
    return clock_ms_sum > 0 ? clock_cycles_sum / (clock_ms_sum * 1e6) : 0;
}


/* `trials` limited to 1..MAX_TRIALS, so MEASURE always has at least one
 * trial to report and never overruns its trial array. */
static int trial_count(void)
{
    if (trials < 1) return 1;
    if (trials > MAX_TRIALS) return MAX_TRIALS;
    return trials;
}

/* Work done per trial, for the misses-per-item column: elements processed
 * (tests 1/2/4/5, all passes), indirect calls made (test 3) or blocks run
 * (test 6, all passes). */
typedef struct {
    u64         count;
    const char *unit;      /* "elem", "call" or "block" */
} Work;

/* ─────────────────────────────────────────────────────────────────────────
 * Measurement log
 *
 * Every measured trial is kept, with its test, variant (the row index in
 * that test's rows[]), pair and run-wide order, so every reported summary
 * can be rebuilt from the trials.  The trial log grows only between tests
 * (reserve_trials()), so nothing is allocated while measuring.
 * ───────────────────────────────────────────────────────────────────────── */
#define MAX_PAIRS         64
#define TRAIN_RUNS        1         /* untimed runs before every measured trial */
#define COST_MIN_MISSES_PER_PAIR 1000.0

typedef struct {
    int         test;
    int         variant;   /* row index in the test's rows[] */
    int         pair;      /* pair_info[] index, or -1 when unpaired */
    int         seq;       /* run-wide measurement order */
    int         trial;     /* 0-based trial of this variant in this pair */
    const char *call;      /* the kernel call, as written */
    Result      r;
    int         has_exposure;
    u64         exposure_before;  /* Test 5: per-site executions of this kernel
                                     before the timed call (training included) */
} TrialRecord;

static TrialRecord *trial_log = NULL;
static int         trial_log_len = 0;
static int         trial_log_cap = 0;
/* Exposure for the next logged trial (Test 5), set by its preparation. */
static int         next_has_exposure = 0;
static u64         next_exposure = 0;

/* Two variants of one test measured in alternating order. */
typedef struct {
    int         test;
    int         a;
    int         b;
    const char *ratio_label;   /* Markdown label of its printed ratio, if any */
    int         cost_shown;    /* its effective-cost line (value or inconclusive) was printed */
} PairInfo;

static PairInfo pair_info[MAX_PAIRS];
static int      pair_count = 0;
static int      current_test = 0;

static void begin_test(int n)
{
    current_test = n;
}

static void log_full(const char *what)
{
    fprintf(stderr, "Internal error: %s full\n", what);
    exit(1);
}

/* Makes room for `streams` more variant streams of trial_count() trials
 * each (a variant in two pairs is two streams).  Called between tests,
 * never while measuring. */
static void reserve_trials(int streams)
{
    const long long need = (long long)trial_log_len + (long long)streams * trial_count();
    if (need <= trial_log_cap) return;
    if (need > INT_MAX / 2) log_full("trial log");
    TrialRecord *grown = realloc(trial_log, (size_t)need * sizeof *grown);
    if (!grown) { perror("realloc"); exit(1); }
    trial_log = grown;
    trial_log_cap = (int)need;
}

static void log_trial(int variant, int pair, int trial, const char *call, const Result *r)
{
    if (trial_log_len >= trial_log_cap) log_full("trial log");
    TrialRecord *t = &trial_log[trial_log_len];
    memset(t, 0, sizeof *t);
    t->test    = current_test;
    t->variant = variant;
    t->pair    = pair;
    t->seq     = trial_log_len;
    t->trial   = trial;
    t->call    = call;
    t->r       = *r;
    t->has_exposure    = next_has_exposure;
    t->exposure_before = next_exposure;
    next_has_exposure = 0;
    trial_log_len++;
}

static int new_pair(int a, int b)
{
    if (pair_count >= MAX_PAIRS) log_full("pair table");
    PairInfo *p = &pair_info[pair_count];
    p->test = current_test;
    p->a = a;
    p->b = b;
    p->ratio_label = NULL;
    p->cost_shown = 0;
    return pair_count++;
}

/* What each variant's rows describe, for the export: its label and work,
 * registered by emit_rows(), its input, registered by log_input() at setup,
 * and its test-specific parameters (log_param()).  One entry per (test,
 * variant); the table grows outside measurements. */
#define MAX_PARAMS 16

typedef struct {
    int         test;
    int         variant;
    const char *label;       /* NULL until emit_rows() registers the row */
    Work        work;
    int         has_input;
    u64         pass_items;  /* items per pass (elements, blocks or calls) */
    int         has_cond;
    u64         cond_true;   /* items per pass whose source condition holds */
    u64         input_hash;  /* fnv1a64() of the input buffer */
    int         n_params;
    const char *param_key[MAX_PARAMS];
    u64         param_val[MAX_PARAMS];
} VariantInfo;

static VariantInfo *variant_info = NULL;
static int         variant_count = 0;
static int         variant_cap = 0;

/* FNV-1a, 64-bit: identifies an input buffer in the export. */
static u64 fnv1a64(const u8 *p, size_t n)
{
    u64 h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* The entry for (current_test, variant), created zeroed on first use. */
static VariantInfo *variant_entry(int variant)
{
    for (int i = 0; i < variant_count; i++)
        if (variant_info[i].test == current_test && variant_info[i].variant == variant)
            return &variant_info[i];
    if (variant_count == variant_cap) {
        if (variant_cap > INT_MAX / 4) log_full("variant table");
        const int cap = variant_cap ? 2 * variant_cap : 64;
        VariantInfo *grown = realloc(variant_info, (size_t)cap * sizeof *grown);
        if (!grown) { perror("realloc"); exit(1); }
        variant_info = grown;
        variant_cap = cap;
    }
    VariantInfo *v = &variant_info[variant_count++];
    memset(v, 0, sizeof *v);
    v->test = current_test;
    v->variant = variant;
    return v;
}

static void log_variant(int variant, const char *label, const Work *w)
{
    VariantInfo *v = variant_entry(variant);
    if (v->label) {
        if (strcmp(v->label, label) || v->work.count != w->count) {
            fprintf(stderr, "Internal error: test %d variant %d registered as two rows\n",
                    current_test, variant);
            exit(1);
        }
        return;
    }
    v->label = label;
    v->work = *w;
}

/* Records a variant's input before it is measured: pass_items items per
 * pass from buf, of which cond_true (when has_cond) meet the kernel's
 * source condition.  A variant measured in two pairs logs it once, or
 * twice with the same values. */
static void log_input(int variant, const u8 *buf, size_t pass_items, int has_cond, u64 cond_true)
{
    VariantInfo *v = variant_entry(variant);
    const u64 hash = fnv1a64(buf, pass_items);
    if (!has_cond) cond_true = 0;
    if (cond_true > pass_items ||
        (v->has_input && (v->pass_items != pass_items || v->has_cond != has_cond ||
                          v->cond_true != cond_true || v->input_hash != hash))) {
        fprintf(stderr, "Internal error: test %d variant %d input logged twice or inconsistent\n",
                current_test, variant);
        exit(1);
    }
    v->has_input  = 1;
    v->pass_items = pass_items;
    v->has_cond   = has_cond;
    v->cond_true  = cond_true;
    v->input_hash = hash;
}

/* Records one named parameter of a variant (exported in its "params"
 * object, in logging order).  The same key may be logged again only with
 * the same value. */
static void log_param(int variant, const char *key, u64 value)
{
    VariantInfo *v = variant_entry(variant);
    for (int i = 0; i < v->n_params; i++) {
        if (strcmp(v->param_key[i], key) != 0) continue;
        if (v->param_val[i] == value) return;
        fprintf(stderr, "Internal error: test %d variant %d parameter %s logged as "
                        "%" PRIu64 " and %" PRIu64 "\n", current_test, variant, key,
                v->param_val[i], value);
        exit(1);
    }
    if (v->n_params == MAX_PARAMS) log_full("parameter list");
    v->param_key[v->n_params] = key;
    v->param_val[v->n_params] = value;
    v->n_params++;
}

/* The reported row for a variant: its median trial by time over all its
 * logged trials (in any pair), with the fastest and slowest time. */
static Result representative(int test, int variant)
{
    Result t[2 * MAX_TRIALS];
    int n = 0;
    for (int i = 0; i < trial_log_len; i++) {
        if (trial_log[i].test != test || trial_log[i].variant != variant) continue;
        if (n == 2 * MAX_TRIALS) log_full("representative buffer");
        t[n++] = trial_log[i].r;
    }
    if (!n) {
        const Result none = {0};
        return none;
    }
    return median_trial(t, n);
}

/* Pairs run AB, BA, AB, ... so drift affects both variants alike. */
static int pair_a_first(int k)
{
    return (k & 1) == 0;
}

/* One measured trial: TRAIN_RUNS untimed calls, then one timed call, which
 * is logged.  Counters wrap the timer, never the other way round (see the
 * call convention above).  Macros rather than functions so the kernel is
 * still called directly, keeping its codegen unchanged. */
#define MEASURE_ONCE(res, call)             \
    do {                                    \
        perf_start();                       \
        double t0_ = now_ms();              \
        (res).result = (call);              \
        (res).time_ms = now_ms() - t0_;     \
        perf_stop(&(res));                  \
        account_clock(&(res));              \
    } while (0)

/* One logged trial after the untimed statement `prepare` (training). */
#define MEASURE_PREPARED(variant, pair, k, prepare, call)       \
    do {                                                        \
        Result t_;                                              \
        memset(&t_, 0, sizeof t_);                              \
        prepare;                                                \
        MEASURE_ONCE(t_, call);                                 \
        log_trial((variant), (pair), (k), #call, &t_);          \
    } while (0)

#define MEASURE_TRAINED(variant, pair, k, call)                 \
    MEASURE_PREPARED(variant, pair, k,                          \
                     for (int r_ = 0; r_ < TRAIN_RUNS; r_++)    \
                         anti_dce_sink(call),                   \
                     call)

/* Measures `call` `trials` times and reports its median trial. */
#define MEASURE(res, variant, call)                             \
    do {                                                        \
        const int n_trials_ = trial_count();                    \
        for (int k_ = 0; k_ < n_trials_; k_++)                  \
            MEASURE_TRAINED((variant), -1, k_, call);           \
        (res) = representative(current_test, (variant));        \
    } while (0)

/* Measures two variants `trials` times each in AB/BA order as one pair,
 * reports each one's median trial (over every pair the variant is in) and
 * stores the pair id in pair_out. */
#define MEASURE_PAIR(ra, va, call_a, rb, vb, call_b, pair_out)  \
    do {                                                        \
        const int n_trials_ = trial_count();                    \
        const int p_ = new_pair((va), (vb));                    \
        for (int k_ = 0; k_ < n_trials_; k_++) {                \
            if (pair_a_first(k_)) {                             \
                MEASURE_TRAINED((va), p_, k_, call_a);          \
                MEASURE_TRAINED((vb), p_, k_, call_b);          \
            } else {                                            \
                MEASURE_TRAINED((vb), p_, k_, call_b);          \
                MEASURE_TRAINED((va), p_, k_, call_a);          \
            }                                                   \
        }                                                       \
        (ra) = representative(current_test, (va));              \
        (rb) = representative(current_test, (vb));              \
        (pair_out) = p_;                                        \
    } while (0)

/* Sums over one variant pair's trials, matched by trial index k.  Times
 * cover every trial pair; counter deltas (b − a, signed) cover only valid
 * trial pairs, where both trials read their counters, and only counters
 * open on both. */
typedef struct {
    int      pairs;
    int      valid;
    double   ms_a;
    double   ms_b;
    double   delta[CTR_COUNT];
    unsigned open_mask;
} PairStats;

static const TrialRecord *find_trial(int pair, int variant, int k)
{
    for (int i = 0; i < trial_log_len; i++)
        if (trial_log[i].pair == pair && trial_log[i].variant == variant &&
            trial_log[i].trial == k)
            return &trial_log[i];
    return NULL;
}

static PairStats pair_stats(int pair)
{
    PairStats s;
    memset(&s, 0, sizeof s);
    s.open_mask = ~0u;
    const PairInfo *p = &pair_info[pair];
    for (int k = 0; ; k++) {
        const TrialRecord *a = find_trial(pair, p->a, k);
        const TrialRecord *b = find_trial(pair, p->b, k);
        if (!a || !b) break;
        s.pairs++;
        s.ms_a += a->r.time_ms;
        s.ms_b += b->r.time_ms;
        if (a->r.perf_status != PERF_OK || b->r.perf_status != PERF_OK) continue;
        s.valid++;
        s.open_mask &= a->r.open_mask & b->r.open_mask;
        for (int c = 0; c < CTR_COUNT; c++)
            s.delta[c] += (double)b->r.counts[c] - (double)a->r.counts[c];
    }
    if (!s.valid) s.open_mask = 0;
    return s;
}

/* Effective extra cycles per additional miss over a pair's valid trials:
 * Σ Δcycles ÷ Σ Δmisses, signed.  Returns 1 with *cost set, 0 when the miss
 * difference is too small to resolve, -1 when there is no valid trial pair
 * or cycles or misses were not open on both trials. */
static int pair_cost(const PairStats *s, double *cost)
{
    const unsigned need = (1u << CTR_CYCLES) | (1u << CTR_MISSES);
    if (!s->valid || (s->open_mask & need) != need) return -1;
    if (s->delta[CTR_MISSES] < COST_MIN_MISSES_PER_PAIR * (double)s->valid) return 0;
    *cost = s->delta[CTR_CYCLES] / s->delta[CTR_MISSES];
    return 1;
}

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

/* Uniform in [0, bound) for bound >= 1: rejects the low values that would
 * make some remainders more likely than others. */
static u64 rng_below(u64 bound)
{
    const u64 reject = (0 - bound) % bound;
    u64 r;
    do r = rng64(); while (r < reject);
    return r % bound;
}

/* The splitmix64 finalizer: a fixed, well-mixed bijection on 64 bits. */
static u64 splitmix64(u64 x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

/* Seed for one input stream of one test, derived only from --seed, the test
 * number, a configuration number and a stream index, so selecting or
 * reordering tests never changes another test's input.  Never 0, where
 * xorshift64 would stick. */
static u64 test_seed(int test, unsigned config, unsigned index)
{
    u64 s = splitmix64(base_seed ^ splitmix64((u64)test));
    s = splitmix64(s ^ config);
    s = splitmix64(s ^ index);
    return s ? s : DEFAULT_SEED;
}

/* Restarts the generator for test n's setup. */
static void seed_test(int test)
{
    rng_state = test_seed(test, 0, 0);
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

/* CPU affinity (--cpu).  Set once, before the counters open, so they and
 * every measurement run on the requested CPU.  A failure is recorded and
 * reported, never hidden: the run goes on unpinned. */
enum { AFF_NOT_REQUESTED, AFF_PINNED, AFF_FAILED, AFF_UNSUPPORTED };
static int affinity_status = AFF_NOT_REQUESTED;
static int affinity_errno  = 0;

static const char *affinity_error(void)
{
    return strerror(affinity_errno);
}

#if defined(__linux__) && defined(__NR_sched_setaffinity) && defined(__NR_sched_getaffinity)
/* Pins to cpu_requested through the raw syscalls (glibc's wrappers need
 * _GNU_SOURCE) and reads the mask back: exactly that CPU must be set. */
static void pin_to_cpu(void)
{
    unsigned long want[(MAX_CPU + 1) / (8 * sizeof(unsigned long))];
    unsigned long got[sizeof want / sizeof want[0]];
    const size_t bits = 8 * sizeof want[0];
    const size_t cpu = (size_t)cpu_requested;
    memset(want, 0, sizeof want);
    memset(got, 0, sizeof got);
    want[cpu / bits] = 1UL << (cpu % bits);
    affinity_status = AFF_FAILED;
    if (syscall(__NR_sched_setaffinity, 0, sizeof want, want) != 0 ||
        syscall(__NR_sched_getaffinity, 0, sizeof got, got) < 0) {
        affinity_errno = errno;
        return;
    }
    if (memcmp(want, got, sizeof want) != 0) {
        affinity_errno = EINVAL;
        return;
    }
    affinity_status = AFF_PINNED;
}
#else
static void pin_to_cpu(void)
{
    affinity_status = AFF_UNSUPPORTED;
}
#endif

static void apply_affinity(void)
{
    if (cpu_requested < 0) return;
    pin_to_cpu();
    if (affinity_status == AFF_FAILED)
        fprintf(stderr, "Warning: could not pin to CPU %d (%s); running unpinned.\n",
                cpu_requested, affinity_error());
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
        if (miss_rate_valid(r))
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
            if (miss_rate_valid(r))
                printf("%.1f%%, ", 100.0 * (double)misses / (double)branches);
            printf("%.3f/%s)", misses_per_item(r, w), w->unit);
        }
    }
    if (perf_available() && r->perf_status != PERF_OK)
        printf("  (hardware counts unavailable)");
    printf("\n");
}

/* Prints a pair's effective extra cycles per additional miss, or the raw
 * differences when they are too small to resolve; nothing without cycle
 * and miss counts. */
static void print_pair_cost(FILE *report, int pair)
{
    const PairStats s = pair_stats(pair);
    double cost = 0;
    const int status = pair_cost(&s, &cost);
    if (status < 0) return;
    char text[160];
    if (status == 0) {
        snprintf(text, sizeof text,
                 "inconclusive (Δcycles %+.0f, Δmisses %+.0f over %d valid pair(s))",
                 s.delta[CTR_CYCLES], s.delta[CTR_MISSES], s.valid);
    } else {
        int n = snprintf(text, sizeof text, "%.1f cycles", cost);
        if (s.valid < s.pairs && n > 0)
            snprintf(text + n, sizeof text - (size_t)n, " (%d of %d pairs)", s.valid, s.pairs);
    }
    pair_info[pair].cost_shown = 1;
    printf("  → Effective extra cycles per additional miss: %s\n", text);
    if (report)
        fprintf(report, "**Effective extra cycles per additional miss:** %s\n\n", text);
}

/* Emits all console rows and the Markdown table for a test. */
static void emit_rows(FILE *report, const char *title, const char *desc,
                      const ReportRow *rows, int n, const Work *w)
{
    for (int i = 0; i < n; i++) {
        print_row(rows[i].label, &rows[i].r, row_work(&rows[i], w));
        log_variant(i, rows[i].label, row_work(&rows[i], w));
    }
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

/* emit_ratio() for a pair: its summed b time over its summed a time. */
static void emit_pair_ratio(FILE *report, const char *console_prefix,
                            const char *md_label, int pair, const char *suffix)
{
    const PairStats s = pair_stats(pair);
    pair_info[pair].ratio_label = md_label;
    emit_ratio(report, console_prefix, md_label, s.ms_b, s.ms_a, suffix);
}

/* For a test that cannot run at this --size/--reps: says why, on the
 * console and in the report, in place of its results. */
static void emit_skip(FILE *report, const char *title, const char *reason)
{
    printf("  Skipped: %s\n\n", reason);
    if (report) fprintf(report, "### %s\n\n_Skipped: %s_\n\n", title, reason);
}

/* Appends ", item" (or the first item) to the list in buf.  Once an item is
 * truncated, *len reaches bufsz and later items are dropped, so the size
 * passed to snprintf never wraps around. */
static void list_append(char *buf, size_t bufsz, size_t *len, const char *item)
{
    if (*len >= bufsz) return;
    int written = snprintf(buf + *len, bufsz - *len, "%s%s", *len ? ", " : "", item);
    if (written < 0) return;
    *len += (size_t)written;
}

/* Standard 2-row emission (predictable vs unpredictable): console rows,
 * Markdown table, slowdown ratio, and effective cost per additional miss. */
static void emit_pair_summary(FILE *report, const char *title, const char *desc,
                              const ReportRow rows[2], const Work *w, int pair)
{
    emit_rows(report, title, desc, rows, 2, w);
    emit_pair_ratio(report, "  → Slowdown:  ", "Slowdown", pair, "");
    print_pair_cost(report, pair);
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
        size_t j = (size_t)rng_below((u64)i + 1);
        u8 tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

/* Set when a row's result differs from its reference, or Test 5's patterns
 * fail their check; main() then exits 1. */
static int sanity_failed = 0;

/* Compares a measured kernel result with its independently computed
 * reference. */
static void check_result(const char *what, u64 got, u64 want)
{
    if (got == want) return;
    fprintf(stderr, "  ERROR: %s result %" PRIu64 ", reference %" PRIu64 "\n", what, got, want);
    sanity_failed = 1;
}

/* Tests 1 and 4: one random byte array, as a sorted and a shuffled copy, so
 * both orders hold the same multiset.  Uses the current rng_state, so each
 * test keeps its own seed. */
static void prepare_threshold(u8 *sorted, u8 *shuffled, size_t n)
{
    fill_random_bytes(sorted, n);
    memcpy(shuffled, sorted, n);
    qsort(sorted, n, 1, cmp_u8_asc);
    shuffle_u8(shuffled, n);
}

/* The threshold sum over n_reps passes, from a histogram rather than the
 * kernel's compare-per-element loop; *above gets the elements > THRESHOLD
 * per pass. */
static u64 threshold_reference(const u8 *arr, size_t n, unsigned n_reps, u64 *above)
{
    u64 count[256] = {0};
    for (size_t i = 0; i < n; i++) count[arr[i]]++;
    u64 sum = 0;
    *above = 0;
    for (unsigned v = THRESHOLD + 1; v < 256; v++) {
        sum += (u64)v * count[v];
        *above += count[v];
    }
    return sum * n_reps;
}

/* "Elements > 128: <above> of <n> per pass (<pct>%)" */
static void threshold_note(char *buf, size_t bufsz, u64 above, size_t n)
{
    snprintf(buf, bufsz, "Elements > %d: %" PRIu64 " of %zu per pass (%.1f%%)",
             THRESHOLD, above, n, 100.0 * (double)above / (double)n);
}

/* The report's copy of the note the console prints in the test heading. */
static void emit_threshold_note(FILE *report, const char *note)
{
    if (report) fprintf(report, "_%s._\n\n", note);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *
 *  TEST 1 — Threshold Sum: Sorted vs Shuffled Array
 *
 *  Accumulates all elements > THRESHOLD.  One random byte array is copied:
 *  one copy is sorted, the other shuffled, so both hold the same multiset
 *  (about 127/256 of the bytes are above the threshold; the run prints the
 *  actual count).
 *
 *  Sorted:   the condition (arr[i] > THRESHOLD) is false up to the
 *            crossover and true after it (the asm jump, which skips the
 *            add, is taken before it and not after), so the predictor is
 *            wrong only around the crossover.
 *
 *  Shuffled: each element's outcome is effectively random, so a predictor
 *            cannot learn it.
 *
 * ═══════════════════════════════════════════════════════════════════════════ */
/* Where KERNEL_ASM is set, the whole function body is one asm block: the
 * n == 0 / n_reps == 0 exits, the pass loop at 1: and the element loop at
 * 2: (a C guard would let Clang add setcc code around it).  The data
 * branch (jbe / b.ls) skips the add unless arr[i] > THRESHOLD. */
__attribute__((noinline))
static u64 sum_threshold(const u8 *arr, size_t n, unsigned n_reps)
{
    u64 acc = 0;
#if KERNEL_ASM
    const u8 *p;
    u64 v;
    unsigned r = n_reps;
#  if defined(__x86_64__)
    __asm__ __volatile__(
        "test %[n], %[n]\n\t"
        "jz 4f\n\t"
        "test %k[r], %k[r]\n\t"
        "jz 4f\n"
        "1:\n\t"
        "mov %[arr], %[p]\n"
        "2:\n\t"
        "movzbl (%[p]), %k[v]\n\t"
        "cmp $" BB_STR(THRESHOLD) ", %k[v]\n\t"
        "jbe 3f\n\t"                 /* <── THE branch; skips: ASM_TAKEN_WHEN */
        "add %[v], %[acc]\n"
        "3:\n\t"
        "inc %[p]\n\t"
        "cmp %[end], %[p]\n\t"
        "jne 2b\n\t"
        "dec %k[r]\n\t"
        "jnz 1b\n"
        "4:"
        : [acc] "+r"(acc), [p] "=&r"(p), [v] "=&r"(v), [r] "+r"(r)
        : [arr] "r"(arr), [end] "r"(arr + n), [n] "r"(n)
        : "cc", "memory");
#  else
    __asm__ __volatile__(
        "cbz %[n], 4f\n\t"
        "cbz %w[r], 4f\n"
        "1:\n\t"
        "mov %[p], %[arr]\n"
        "2:\n\t"
        "ldrb %w[v], [%[p]], #1\n\t"
        "cmp %w[v], #" BB_STR(THRESHOLD) "\n\t"
        "b.ls 3f\n\t"                /* <── THE branch being predicted */
        "add %[acc], %[acc], %[v]\n"
        "3:\n\t"
        "cmp %[p], %[end]\n\t"
        "b.ne 2b\n\t"
        "subs %w[r], %w[r], #1\n\t"
        "b.ne 1b\n"
        "4:"
        : [acc] "+r"(acc), [p] "=&r"(p), [v] "=&r"(v), [r] "+r"(r)
        : [arr] "r"(arr), [end] "r"(arr + n), [n] "r"(n)
        : "cc", "memory");
#  endif
#else
    for (unsigned rep = 0; rep < n_reps; rep++)
        for (size_t i = 0; i < n; i++)
            if (arr[i] > THRESHOLD)   /* <── THE branch being predicted */
                acc += arr[i];
#endif
    return acc;
}

static void run_test1(FILE *report)
{
    seed_test(1);
    /* Read once, so the allocation and every loop provably use the same length. */
    const size_t len = size;
    u8 *sorted   = malloc(len);
    u8 *shuffled = malloc(len);
    if (!sorted || !shuffled) { perror("malloc"); exit(1); }
    prepare_threshold(sorted, shuffled, len);
    u64 above = 0;
    const u64 want = threshold_reference(sorted, len, reps, &above);
    char note[96];
    threshold_note(note, sizeof note, above, len);

    printf("TEST 1 — Threshold Sum  (sorted vs shuffled array, %zu×%u reps)\n",
           len, reps);
    printf("  Counts/sums elements > %d.  Identical data, different order.\n", THRESHOLD);
    printf("  %s\n\n", note);

    log_input(0, sorted, len, 1, above);
    log_input(1, shuffled, len, 1, above);
    Result rs = {0};
    Result rr = {0};
    int pair;
    MEASURE_PAIR(rs, 0, sum_threshold(sorted, len, reps),
                 rr, 1, sum_threshold(shuffled, len, reps), pair);

    free(sorted); free(shuffled);
    check_result("Test 1 sorted", rs.result, want);
    check_result("Test 1 shuffled", rr.result, want);

    const Work work = { (u64)len * reps, "elem" };
    const ReportRow rows[] = {
        { "Sorted",   rs, NULL },
        { "Shuffled", rr, NULL },
    };
    emit_pair_summary(report, "Test 1 — Threshold Sum",
                      "Sorted vs shuffled copies of one random byte array, same branch kernel.",
                      rows, &work, pair);
    emit_threshold_note(report, note);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *
 *  TEST 2 — Bias and Ordering: periodic vs clustered vs shuffled
 *
 *  Counts the ones in a 0/1 array.  For each bias (1/16, 1/4, 1/2, 3/4,
 *  15/16 ones) the three arrays hold exactly the same number of ones, in
 *  three orders:
 *
 *  Periodic:   a short word repeated (e.g. 1000 for 1/4).
 *  Clustered:  all the ones first, then all the zeros.
 *  Shuffled:   the periodic array, randomly permuted.
 *
 *  Constant-0 and constant-1 arrays are controls.  Every row runs the same
 *  branch site, so within a bias only the order differs.  The results show
 *  how this kernel's branch responds to bias and order; they say nothing
 *  general about a predictor's type.
 *
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Same loop frame as sum_threshold; the data branch (je / cbz) skips the
 * increment when decisions[i] is 0. */
__attribute__((noinline))
static u64 count_decisions(const u8 *decisions, size_t n, unsigned n_reps)
{
    u64 acc = 0;
#if KERNEL_ASM
    const u8 *p;
    unsigned r = n_reps;
#  if defined(__x86_64__)
    __asm__ __volatile__(
        "test %[n], %[n]\n\t"
        "jz 4f\n\t"
        "test %k[r], %k[r]\n\t"
        "jz 4f\n"
        "1:\n\t"
        "mov %[arr], %[p]\n"
        "2:\n\t"
        "cmpb $0, (%[p])\n\t"
        "je 3f\n\t"                  /* branch outcome determined by table */
        "inc %[acc]\n"
        "3:\n\t"
        "inc %[p]\n\t"
        "cmp %[end], %[p]\n\t"
        "jne 2b\n\t"
        "dec %k[r]\n\t"
        "jnz 1b\n"
        "4:"
        : [acc] "+r"(acc), [p] "=&r"(p), [r] "+r"(r)
        : [arr] "r"(decisions), [end] "r"(decisions + n), [n] "r"(n)
        : "cc", "memory");
#  else
    u64 v;
    __asm__ __volatile__(
        "cbz %[n], 4f\n\t"
        "cbz %w[r], 4f\n"
        "1:\n\t"
        "mov %[p], %[arr]\n"
        "2:\n\t"
        "ldrb %w[v], [%[p]], #1\n\t"
        "cbz %w[v], 3f\n\t"          /* branch outcome determined by table */
        "add %[acc], %[acc], #1\n"
        "3:\n\t"
        "cmp %[p], %[end]\n\t"
        "b.ne 2b\n\t"
        "subs %w[r], %w[r], #1\n\t"
        "b.ne 1b\n"
        "4:"
        : [acc] "+r"(acc), [p] "=&r"(p), [v] "=&r"(v), [r] "+r"(r)
        : [arr] "r"(decisions), [end] "r"(decisions + n), [n] "r"(n)
        : "cc", "memory");
#  endif
#else
    for (unsigned rep = 0; rep < n_reps; rep++)
        for (size_t i = 0; i < n; i++)
            if (decisions[i])   /* branch outcome determined by table */
                acc++;
#endif
    return acc;
}

/* Test 5's kernels: pattern_sites_N runs N static data branches per block,
 * site s reading data[block * N + s] (block-major), over `blocks` blocks
 * and `passes` passes, and returns the number of nonzero bytes seen.  One
 * asm block holds the whole body, as in count_decisions; the N sites come
 * from one .rept body whose offset .Lbb_site counts 0 ... N - 1, so every
 * site is the same instruction sequence reading its own stream.  Without
 * the asm kernels only N = 1 exists, as a C loop. */
#if KERNEL_ASM
#  if defined(__x86_64__)
#    define PATTERN_ASM(N)                                                \
    __asm__ __volatile__(                                                 \
        "test %[b], %[b]\n\t"                                             \
        "jz 4f\n\t"                                                       \
        "test %k[r], %k[r]\n\t"                                           \
        "jz 4f\n"                                                         \
        "1:\n\t"                                                          \
        "mov %[arr], %[p]\n"                                              \
        "2:\n\t"                                                          \
        ".set .Lbb_site, 0\n\t"                                           \
        ".rept " #N "\n\t"                                                \
        "cmpb $0, .Lbb_site(%[p])\n\t"                                    \
        "je 3f\n\t"                  /* one data branch per site */       \
        "inc %[acc]\n"                                                    \
        "3:\n\t"                                                          \
        ".set .Lbb_site, .Lbb_site + 1\n\t"                               \
        ".endr\n\t"                                                       \
        "add $" #N ", %[p]\n\t"                                           \
        "cmp %[end], %[p]\n\t"                                            \
        "jne 2b\n\t"                                                      \
        "dec %k[r]\n\t"                                                   \
        "jnz 1b\n"                                                        \
        "4:"                                                              \
        : [acc] "+r"(acc), [p] "=&r"(p), [r] "+r"(r)                      \
        : [arr] "r"(data), [end] "r"(data + (size_t)(N) * blocks),        \
          [b] "r"(blocks)                                                 \
        : "cc", "memory")
#  else
#    define PATTERN_ASM(N)                                                \
    u64 v;                                                                \
    __asm__ __volatile__(                                                 \
        "cbz %[b], 4f\n\t"                                                \
        "cbz %w[r], 4f\n"                                                 \
        "1:\n\t"                                                          \
        "mov %[p], %[arr]\n"                                              \
        "2:\n\t"                                                          \
        ".set .Lbb_site, 0\n\t"                                           \
        ".rept " #N "\n\t"                                                \
        "ldrb %w[v], [%[p], #.Lbb_site]\n\t"                              \
        "cbz %w[v], 3f\n\t"          /* one data branch per site */       \
        "add %[acc], %[acc], #1\n"                                        \
        "3:\n\t"                                                          \
        ".set .Lbb_site, .Lbb_site + 1\n\t"                               \
        ".endr\n\t"                                                       \
        "add %[p], %[p], #" #N "\n\t"                                     \
        "cmp %[p], %[end]\n\t"                                            \
        "b.ne 2b\n\t"                                                     \
        "subs %w[r], %w[r], #1\n\t"                                       \
        "b.ne 1b\n"                                                       \
        "4:"                                                              \
        : [acc] "+r"(acc), [p] "=&r"(p), [v] "=&r"(v), [r] "+r"(r)        \
        : [arr] "r"(data), [end] "r"(data + (size_t)(N) * blocks),        \
          [b] "r"(blocks)                                                 \
        : "cc", "memory")
#  endif
#  define DEF_PATTERN_KERNEL(N)                                           \
    __attribute__((noinline))                                             \
    static u64 pattern_sites_##N(const u8 *data, size_t blocks, unsigned passes) \
    {                                                                     \
        u64 acc = 0;                                                      \
        const u8 *p;                                                      \
        unsigned r = passes;                                              \
        PATTERN_ASM(N);                                                   \
        return acc;                                                       \
    }
DEF_PATTERN_KERNEL(1)
DEF_PATTERN_KERNEL(4)
DEF_PATTERN_KERNEL(16)
DEF_PATTERN_KERNEL(64)
DEF_PATTERN_KERNEL(256)
DEF_PATTERN_KERNEL(512)
#else
__attribute__((noinline))
static u64 pattern_sites_1(const u8 *data, size_t blocks, unsigned passes)
{
    u64 acc = 0;
    /* Emits nothing; without it this body equals count_decisions' C loop
     * and identical code folding merges the two (section 5 of AGENTS.md). */
    __asm__ __volatile__("");
    for (unsigned rep = 0; rep < passes; rep++)
        for (size_t i = 0; i < blocks; i++)
            if (data[i])        /* the one site */
                acc++;
    return acc;
}
#endif

/* 1 if this build has pattern_sites_n. */
static int pattern_kernel_available(unsigned n)
{
    return KERNEL_ASM || n == 1;
}

#define T2_TITLE "Test 2 — Bias and Ordering"
#define T2_DESC  "Same number of ones per bias in periodic, clustered and shuffled order, plus constant controls; one branch site."
#define T2_ROWS  (3 * T2_BIASES + 2)

/* Each bias's periodic word; every length divides T2_PERIOD. */
static const char *const t2_words[] = {
    "1000000000000000", "1000", "10", "1110", "1111111111111110",
};
CHECK_LEN(t2_words_len_check, t2_words, T2_BIASES);

/* Row labels: bias b's periodic, clustered and shuffled rows are 3b, 3b+1
 * and 3b+2, then the two controls.  A bias row's first word is its bias
 * (check_export.py reads it). */
static const char *const t2_labels[] = {
    "1/16  periodic 1000000000000000", "1/16  clustered", "1/16  shuffled",
    "1/4   periodic 1000",             "1/4   clustered", "1/4   shuffled",
    "1/2   periodic 10",               "1/2   clustered", "1/2   shuffled",
    "3/4   periodic 1110",             "3/4   clustered", "3/4   shuffled",
    "15/16 periodic 1111111111111110", "15/16 clustered", "15/16 shuffled",
    "Constant 0", "Constant 1",
};
CHECK_LEN(t2_labels_len_check, t2_labels, T2_ROWS);

static const char *const t2_ratio_periodic[] = {
    "1/16 shuffled vs periodic", "1/4 shuffled vs periodic", "1/2 shuffled vs periodic",
    "3/4 shuffled vs periodic", "15/16 shuffled vs periodic",
};
static const char *const t2_ratio_clustered[] = {
    "1/16 shuffled vs clustered", "1/4 shuffled vs clustered", "1/2 shuffled vs clustered",
    "3/4 shuffled vs clustered", "15/16 shuffled vs clustered",
};
static const char *const t2_prefix_periodic[] = {
    "  → 1/16  shuffled vs periodic:  ", "  → 1/4   shuffled vs periodic:  ",
    "  → 1/2   shuffled vs periodic:  ", "  → 3/4   shuffled vs periodic:  ",
    "  → 15/16 shuffled vs periodic:  ",
};
static const char *const t2_prefix_clustered[] = {
    "  → 1/16  shuffled vs clustered: ", "  → 1/4   shuffled vs clustered: ",
    "  → 1/2   shuffled vs clustered: ", "  → 3/4   shuffled vs clustered: ",
    "  → 15/16 shuffled vs clustered: ",
};
CHECK_LEN(t2_ratio_periodic_len_check, t2_ratio_periodic, T2_BIASES);
CHECK_LEN(t2_ratio_clustered_len_check, t2_ratio_clustered, T2_BIASES);
CHECK_LEN(t2_prefix_periodic_len_check, t2_prefix_periodic, T2_BIASES);
CHECK_LEN(t2_prefix_clustered_len_check, t2_prefix_clustered, T2_BIASES);

/* Rows with their labels, no result yet and the test's Work. */
static void init_rows(ReportRow *rows, const char *const *labels, int n)
{
    const Result none = {0};
    for (int i = 0; i < n; i++) {
        rows[i].label = labels[i];
        rows[i].r     = none;
        rows[i].work  = NULL;
    }
}

static void fill_periodic(u8 *buf, size_t n, const char *word)
{
    const size_t wl = strlen(word);
    for (size_t i = 0; i < n; i++)
        buf[i] = word[i % wl] == '1';
}

/* `ones` ones, then zeros. */
static void fill_clustered(u8 *buf, size_t n, size_t ones)
{
    memset(buf, 1, ones);
    memset(buf + ones, 0, n - ones);
}

static size_t count_ones(const u8 *buf, size_t n)
{
    size_t ones = 0;
    for (size_t i = 0; i < n; i++) ones += buf[i] != 0;
    return ones;
}

/* The smallest p (≤ T2_PERIOD, dividing n) with buf[i] == buf[i % p]
 * everywhere, else n. */
static size_t fundamental_period(const u8 *buf, size_t n)
{
    for (size_t p = 1; p <= T2_PERIOD && p < n; p++) {
        if (n % p) continue;
        size_t i = p;
        while (i < n && buf[i] == buf[i % p]) i++;
        if (i == n) return p;
    }
    return n;
}

static int test2_size_ok(size_t len)
{
    return len >= T2_PERIOD && len % T2_PERIOD == 0;
}

/* Builds bias b's three orders in bufs[] (periodic, clustered, shuffled),
 * checks that they hold its exact number of ones (and the periodic one its
 * word's period), and measures periodic and clustered each paired with
 * shuffled. */
static void measure_bias(int b, u8 *const bufs[3], size_t len,
                         ReportRow rows[T2_ROWS], int *pair_p, int *pair_c)
{
    u8 *periodic  = bufs[0];
    u8 *clustered = bufs[1];
    u8 *shuffled  = bufs[2];
    const char *word = t2_words[b];
    const size_t wl = strlen(word);
    if (!wl || T2_PERIOD % wl) {
        fprintf(stderr, "Internal error: Test 2 word %s does not divide %d\n", word, T2_PERIOD);
        exit(1);
    }
    size_t word_ones = 0;
    for (size_t i = 0; i < wl; i++) word_ones += word[i] == '1';
    const size_t want_ones = len / wl * word_ones;
    fill_periodic(periodic, len, word);
    fill_clustered(clustered, len, want_ones);
    memcpy(shuffled, periodic, len);
    rng_state = test_seed(2, (unsigned)b + 1, 0);
    shuffle_u8(shuffled, len);
    if (fundamental_period(periodic, len) != wl || count_ones(periodic, len) != want_ones ||
        count_ones(clustered, len) != want_ones || count_ones(shuffled, len) != want_ones) {
        fprintf(stderr, "Internal error: Test 2 input check failed for word %s\n", word);
        exit(1);
    }
    const int v = 3 * b;
    log_input(v, periodic, len, 1, want_ones);
    log_input(v + 1, clustered, len, 1, want_ones);
    log_input(v + 2, shuffled, len, 1, want_ones);
    MEASURE_PAIR(rows[v].r, v, count_decisions(periodic, len, reps),
                 rows[v + 2].r, v + 2, count_decisions(shuffled, len, reps), *pair_p);
    MEASURE_PAIR(rows[v + 1].r, v + 1, count_decisions(clustered, len, reps),
                 rows[v + 2].r, v + 2, count_decisions(shuffled, len, reps), *pair_c);
    const u64 want = (u64)want_ones * reps;
    for (int k = 0; k < 3; k++)
        check_result(rows[v + k].label, rows[v + k].r.result, want);
}

/* A constant-0 or constant-1 control row, measured alone. */
static void measure_constant(u8 *buf, size_t len, int one, ReportRow rows[T2_ROWS])
{
    const int v = 3 * T2_BIASES + one;
    const u64 ones = one ? (u64)len : 0;
    memset(buf, one, len);
    log_input(v, buf, len, 1, ones);
    MEASURE(rows[v].r, v, count_decisions(buf, len, reps));
    check_result(rows[v].label, rows[v].r.result, ones * reps);
}

static void run_test2(FILE *report)
{
    seed_test(2);
    const size_t len = size;
    printf("TEST 2 — Bias and Ordering  (%d biases × periodic/clustered/shuffled, %zu×%u reps)\n",
           T2_BIASES, len, reps);
    printf("  %s\n\n", T2_DESC);
    if (!test2_size_ok(len)) {
        emit_skip(report, T2_TITLE,
                  "Test 2 needs --size to be a multiple of 16 (whole periods of every bias pattern).");
        return;
    }
    u8 *bufs[3] = { malloc(len), malloc(len), malloc(len) };
    if (!bufs[0] || !bufs[1] || !bufs[2]) { perror("malloc"); exit(1); }

    ReportRow rows[T2_ROWS];
    init_rows(rows, t2_labels, T2_ROWS);
    int pair_p[T2_BIASES];
    int pair_c[T2_BIASES];
    for (int b = 0; b < T2_BIASES; b++)
        measure_bias(b, bufs, len, rows, &pair_p[b], &pair_c[b]);
    measure_constant(bufs[0], len, 0, rows);
    measure_constant(bufs[0], len, 1, rows);
    for (int i = 0; i < 3; i++) free(bufs[i]);

    const Work work = { (u64)len * reps, "elem" };
    emit_rows(report, T2_TITLE, T2_DESC, rows, T2_ROWS, &work);
    printf("\n");
    for (int b = 0; b < T2_BIASES; b++) {
        emit_pair_ratio(report, t2_prefix_periodic[b], t2_ratio_periodic[b], pair_p[b], "");
        emit_pair_ratio(report, t2_prefix_clustered[b], t2_ratio_clustered[b], pair_c[b], "");
    }
    print_pair_cost(report, pair_p[2]);   /* 1/2: shuffled vs periodic */
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *
 *  TEST 3 — Indirect Branch Dispatch: cyclic vs shuffled, 1–64 targets
 *
 *  dispatch_indexed() loads a precomputed target index per call and calls
 *  that leaf function through one indirect call site.  The CPU's indirect
 *  predictor must guess the target address.  For each target count T (1, 2,
 *  4, ... 64) two index sequences with exactly the same number of calls to
 *  every target are compared:
 *
 *  Cyclic:    0, 1, ..., T-1, 0, 1, ...  For T >= 2 a plain Branch Target
 *             Buffer only remembers the last target, so learning the cycle
 *             takes a predictor that uses branch history.  T = 1 is the
 *             single-target control.
 *
 *  Shuffled:  the cyclic sequence, randomly permuted: nothing to learn
 *             beyond the target frequencies.
 *
 *  Both rows of a pair run the same code on the same table; only the order
 *  differs.  The leaves have identical instruction shapes (check_kernels.sh
 *  checks this), so every call does the same work.
 *
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Generate NUM_FUNCS distinct leaf functions via X-macro ─────────────────── */
typedef u64 (*leaf_fn_t)(u64);

#define FOR_EACH_LEAF(F) \
    F( 0) F( 1) F( 2) F( 3) F( 4) F( 5) F( 6) F( 7) \
    F( 8) F( 9) F(10) F(11) F(12) F(13) F(14) F(15) \
    F(16) F(17) F(18) F(19) F(20) F(21) F(22) F(23) \
    F(24) F(25) F(26) F(27) F(28) F(29) F(30) F(31) \
    F(32) F(33) F(34) F(35) F(36) F(37) F(38) F(39) \
    F(40) F(41) F(42) F(43) F(44) F(45) F(46) F(47) \
    F(48) F(49) F(50) F(51) F(52) F(53) F(54) F(55) \
    F(56) F(57) F(58) F(59) F(60) F(61) F(62) F(63)

/* Leaf n's constant: a Weyl-sequence step, distinct and nonzero, with every
 * 16-bit chunk in 1..0x7fff, so each constant needs the same instructions
 * (x86-64 movabs; AArch64 mov + three movk) and every leaf has one shape.
 * Distinct constants also keep identical code folding (ICF) from merging
 * the leaves. */
#define LEAF_CONST(n) \
    ((((u64)(n) + 1) * 0x9e3779b97f4a7c15ULL & 0x7ffe7ffe7ffe7ffeULL) | 0x0001000100010001ULL)

/* Rotate, then xor: order-dependent, so a call to the wrong target or in
 * the wrong order changes the result. */
#define DEF_LEAF(n) \
    __attribute__((noinline)) \
    static u64 leaf_##n(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(n); }

FOR_EACH_LEAF(DEF_LEAF)

#define PTR_LEAF(n) leaf_##n,
static leaf_fn_t leaf_table[NUM_FUNCS] = { FOR_EACH_LEAF(PTR_LEAF) };

__attribute__((noinline))
static u64 dispatch_indexed(const u8 *idx, size_t n)
{
    u64 acc = 0;
    for (size_t i = 0; i < n; i++)
        acc = leaf_table[idx[i]](acc);
    return acc;
}

/* dispatch_indexed()'s result without the calls. */
static u64 dispatch_reference(const u8 *idx, size_t n)
{
    u64 acc = 0;
    for (size_t i = 0; i < n; i++)
        acc = ((acc << 1) | (acc >> 63)) ^ LEAF_CONST(idx[i]);
    return acc;
}

static void fill_cyclic(u8 *idx, size_t n, unsigned targets)
{
    for (size_t i = 0; i < n; i++)
        idx[i] = (u8)(i % targets);
}

/* 1 iff every index is below targets and each target gets n / targets
 * calls. */
static int equal_target_counts(const u8 *idx, size_t n, unsigned targets)
{
    size_t count[NUM_FUNCS] = {0};
    for (size_t i = 0; i < n; i++) {
        if (idx[i] >= targets) return 0;
        count[idx[i]]++;
    }
    for (unsigned t = 0; t < targets; t++)
        if (count[t] != n / targets) return 0;
    return 1;
}

static int test3_size_ok(size_t len)
{
    return len >= NUM_FUNCS && len % NUM_FUNCS == 0;
}

#define T3_TITLE "Test 3 — Indirect Dispatch"
#define T3_DESC  "One indirect call site over 1–64 leaf targets; cyclic vs shuffled index sequences with equal target counts."

static const char *const t3_labels[] = {
    "T= 1 cyclic", "T= 1 shuffled", "T= 2 cyclic", "T= 2 shuffled",
    "T= 4 cyclic", "T= 4 shuffled", "T= 8 cyclic", "T= 8 shuffled",
    "T=16 cyclic", "T=16 shuffled", "T=32 cyclic", "T=32 shuffled",
    "T=64 cyclic", "T=64 shuffled",
};
CHECK_LEN(t3_labels_len_check, t3_labels, 2 * T3_COUNTS);
static const char *const t3_ratio[] = {
    "T=1 shuffled vs cyclic", "T=2 shuffled vs cyclic", "T=4 shuffled vs cyclic",
    "T=8 shuffled vs cyclic", "T=16 shuffled vs cyclic", "T=32 shuffled vs cyclic",
    "T=64 shuffled vs cyclic",
};
static const char *const t3_prefix[] = {
    "  → T= 1 shuffled vs cyclic: ", "  → T= 2 shuffled vs cyclic: ",
    "  → T= 4 shuffled vs cyclic: ", "  → T= 8 shuffled vs cyclic: ",
    "  → T=16 shuffled vs cyclic: ", "  → T=32 shuffled vs cyclic: ",
    "  → T=64 shuffled vs cyclic: ",
};
CHECK_LEN(t3_ratio_len_check, t3_ratio, T3_COUNTS);
CHECK_LEN(t3_prefix_len_check, t3_prefix, T3_COUNTS);
/* The largest target count is NUM_FUNCS: T = 1 << (T3_COUNTS - 1). */
typedef char t3_counts_check[(1u << (T3_COUNTS - 1)) == NUM_FUNCS ? 1 : -1];

/* Builds target count i's two sequences, checks their target counts and
 * measures them as one pair. */
static void measure_targets(int i, u8 *cyclic, u8 *shuffled, size_t len,
                            ReportRow rows[2 * T3_COUNTS], int *pair)
{
    const unsigned targets = 1u << i;
    fill_cyclic(cyclic, len, targets);
    memcpy(shuffled, cyclic, len);
    rng_state = test_seed(3, targets, 0);
    shuffle_u8(shuffled, len);
    if (!equal_target_counts(cyclic, len, targets) || !equal_target_counts(shuffled, len, targets)) {
        fprintf(stderr, "Internal error: Test 3 input check failed for %u targets\n", targets);
        exit(1);
    }
    log_input(2 * i, cyclic, len, 0, 0);
    log_input(2 * i + 1, shuffled, len, 0, 0);
    MEASURE_PAIR(rows[2 * i].r, 2 * i, dispatch_indexed(cyclic, len),
                 rows[2 * i + 1].r, 2 * i + 1, dispatch_indexed(shuffled, len), *pair);
    check_result(rows[2 * i].label, rows[2 * i].r.result, dispatch_reference(cyclic, len));
    check_result(rows[2 * i + 1].label, rows[2 * i + 1].r.result,
                 dispatch_reference(shuffled, len));
}

static void run_test3(FILE *report)
{
    seed_test(3);
    const size_t len = size;
    if (len % (1u << 20) == 0)
        printf("TEST 3 — Indirect Dispatch  (1–%u targets, cyclic vs shuffled, %zuM calls)\n",
               NUM_FUNCS, len >> 20);
    else
        printf("TEST 3 — Indirect Dispatch  (1–%u targets, cyclic vs shuffled, %zu calls)\n",
               NUM_FUNCS, len);
    printf("  Calls leaf functions via pointer. The indirect predictor must guess the target address.\n\n");
    if (!test3_size_ok(len)) {
        emit_skip(report, T3_TITLE,
                  "Test 3 needs --size to be a multiple of 64 (equal calls to every target).");
        return;
    }
    u8 *cyclic   = malloc(len);
    u8 *shuffled = malloc(len);
    if (!cyclic || !shuffled) { perror("malloc"); exit(1); }

    ReportRow rows[2 * T3_COUNTS];
    init_rows(rows, t3_labels, 2 * T3_COUNTS);
    int pairs[T3_COUNTS];
    for (int i = 0; i < T3_COUNTS; i++)
        measure_targets(i, cyclic, shuffled, len, rows, &pairs[i]);
    free(cyclic);
    free(shuffled);

    const Work work = { len, "call" };
    emit_rows(report, T3_TITLE, T3_DESC, rows, 2 * T3_COUNTS, &work);
    printf("\n");
    for (int i = 0; i < T3_COUNTS; i++)
        emit_pair_ratio(report, t3_prefix[i], t3_ratio[i], pairs[i], "");
    print_pair_cost(report, pairs[T3_COUNTS - 1]);   /* 64 targets */
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *
 *  TEST 4 — Branch vs Branchless Computation
 *
 *  Both kernels compute the same result: the sum of elements > THRESHOLD,
 *  on a sorted and a shuffled copy of one random byte array (as in Test 1,
 *  with Test 4's own seed).
 *
 *  Branch version:     sum_threshold(), Test 1's kernel: one conditional
 *                      jump per element.
 *
 *  Branchless version (arithmetic mask):
 *      mask = 0 - (u64)(arr[i] > T);   → 0 or 0xFFFF...FFFF
 *      acc += arr[i] & mask;
 *    No data-dependent jump; the compiler may use setcc or a conditional
 *    select for the comparison.
 *
 *  Three paired comparisons: the branch kernel on shuffled vs sorted (its
 *  effective cost per miss: same instructions, different predictability),
 *  and branchless vs branch for each order.  Branch and branchless run
 *  different instructions, so their time difference is not a pure
 *  misprediction penalty.
 *
 * ═══════════════════════════════════════════════════════════════════════════ */

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
    seed_test(4);
    const size_t len = size;
    u8 *sorted   = malloc(len);
    u8 *shuffled = malloc(len);
    if (!sorted || !shuffled) { perror("malloc"); exit(1); }
    prepare_threshold(sorted, shuffled, len);
    u64 above = 0;
    const u64 want = threshold_reference(sorted, len, reps, &above);
    char note[96];
    threshold_note(note, sizeof note, above, len);

    printf("TEST 4 — Branch vs Branchless  (sorted and shuffled copies, %zu×%u reps)\n",
           len, reps);
    printf("  Branchless uses arithmetic mask: 0 - (u64)(v > T) to avoid jumps.\n");
    printf("  %s\n\n", note);

    const Result none = {0};
    ReportRow rows[] = {
        { "Sorted   + branch",     none, NULL },
        { "Sorted   + branchless", none, NULL },
        { "Shuffled + branch",     none, NULL },
        { "Shuffled + branchless", none, NULL },
    };
    for (int v = 0; v < 4; v++)
        log_input(v, v < 2 ? sorted : shuffled, len, 1, above);
    int pb;
    int ps;
    int pr;
    MEASURE_PAIR(rows[0].r, 0, sum_threshold(sorted, len, reps),
                 rows[2].r, 2, sum_threshold(shuffled, len, reps), pb);
    MEASURE_PAIR(rows[0].r, 0, sum_threshold(sorted, len, reps),
                 rows[1].r, 1, sum_branchless(sorted, len, reps), ps);
    MEASURE_PAIR(rows[2].r, 2, sum_threshold(shuffled, len, reps),
                 rows[3].r, 3, sum_branchless(shuffled, len, reps), pr);

    free(sorted); free(shuffled);
    /* Every row is checked against the reference, so no anti-DCE sink is
     * needed. */
    for (int v = 0; v < 4; v++)
        check_result(rows[v].label, rows[v].r.result, want);

    const Work work = { (u64)len * reps, "elem" };
    emit_rows(report, "Test 4 — Branch vs Branchless",
              "Same sum via Test 1's branch kernel and an arithmetic mask, on sorted and "
              "shuffled copies of one byte array. A branchless vs branch ratio below 1 "
              "means branchless is faster.",
              rows, 4, &work);
    emit_pair_ratio(report, "\n  Branch, shuffled vs sorted:     ",
                    "Branch, shuffled vs sorted", pb, "");
    emit_pair_ratio(report, "  Sorted, branchless vs branch:   ",
                    "Sorted, branchless vs branch", ps, "");
    emit_pair_ratio(report, "  Shuffled, branchless vs branch: ",
                    "Shuffled, branchless vs branch", pr, "");
    print_pair_cost(report, pb);
    emit_threshold_note(report, note);
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *
 *  TEST 5 — Pattern Learnability: de Bruijn patterns vs random  (opt-in)
 *
 *  Runs Test 2's count_decisions() kernel on repeating binary de Bruijn
 *  sequences of orders k = 5..11 (periods 32..2048, half ones), tiled over
 *  the array, and on random 50% ones.  Every k-bit window of a period is
 *  distinct, so k previous outcomes identify the position.  The results show
 *  how well this kernel's data branch learns each period; they do not
 *  measure a predictor's capacity or history length.
 *
 * ═══════════════════════════════════════════════════════════════════════════ */


/* Marks word w in a bitset; returns 1 if it was not marked before. */
static int mark_new(u8 *set, unsigned w)
{
    const u8 bit = (u8)(1u << (w & 7));
    if (set[w >> 3] & bit) return 0;
    set[w >> 3] |= bit;
    return 1;
}

/* Writes the first 2^k bits of the prefer-one de Bruijn sequence of order k
 * (Martin, 1934): k zeros, then each next bit 1 if that makes a k-bit word
 * not seen yet, else 0.  Run on, it would emit k - 1 more (wrap-around)
 * bits; the first 2^k are a cyclic de Bruijn sequence.  Returns 0 for an
 * unsupported k, or if neither bit gives a new word. */
static int debruijn_prefer_one(unsigned k, u8 out[DB_MAX_PERIOD])
{
    if (k < DB_MIN_ORDER || k > DB_MAX_ORDER) return 0;
    const unsigned period = 1u << k;
    const unsigned mask = period - 1;
    u8 seen[DB_MAX_PERIOD / 8] = {0};
    unsigned state = 0;
    for (unsigned i = 0; i < k; i++) out[i] = 0;
    mark_new(seen, 0);
    for (unsigned i = k; i < period; i++) {
        const unsigned one = ((state << 1) | 1u) & mask;
        if (mark_new(seen, one))            state = one;
        else if (mark_new(seen, one & ~1u)) state = one & ~1u;
        else return 0;
        out[i] = (u8)(state & 1u);
    }
    return 1;
}

/* 1 if p[0..2^k-1] (bytes 0/1) is a cyclic de Bruijn sequence of order k:
 * every k-bit word appears exactly once as a cyclic window, and half the
 * bits are ones.  k must be in DB_MIN_ORDER..DB_MAX_ORDER. */
static int debruijn_valid(const u8 *p, unsigned k)
{
    const unsigned period = 1u << k;
    const unsigned mask = period - 1;
    u8 seen[DB_MAX_PERIOD / 8] = {0};
    unsigned state = 0;
    unsigned ones = 0;
    for (unsigned i = 0; i + 1 < k; i++) state = (state << 1) | p[i];
    for (unsigned i = 0; i < period; i++) {
        state = ((state << 1) | p[(i + k - 1) & mask]) & mask;
        ones += p[i];
        if (!mark_new(seen, state)) return 0;
    }
    return ones == period / 2;
}

/* Test 5 matrix inputs.  A cell is N sites, each repeating its own L-bit
 * stream, tiled over `blocks` blocks (block-major: data[b * N + s] is
 * stream s at b mod L).  The main family is one independent random word
 * per site; constant-0, alternating, de Bruijn and a balanced random
 * reference are controls.  Nothing is rejected or redrawn: duplicates and
 * shorter fundamental periods are counted and exported instead. */
enum { T5_RANDOM_WORDS, T5_CONSTANT0, T5_ALTERNATING, T5_DEBRUIJN, T5_BALANCED_REF };
static const char *const t5_family_ids[] = {
    "random-words", "constant-0", "alternating", "de-bruijn", "balanced-ref",
};
#define T5_REF_CONFIG 0x7fffffffu   /* test_seed() config of the balanced reference */

typedef struct {
    unsigned n;        /* static sites per block */
    unsigned l;        /* stream length (period) */
    size_t   blocks;   /* blocks per pass, a multiple of l */
    int      family;
} T5Cell;

typedef struct {
    u64      ones;           /* ones per pass, all sites */
    unsigned distinct;       /* distinct streams among the n sites */
    unsigned short_period;   /* sites whose fundamental period is below l */
} T5Stats;

static unsigned log2_exact(unsigned v)
{
    unsigned k = 0;
    while ((1u << k) < v) k++;
    return k;
}

/* Site s's random word: l bits from test_seed(5, (n << 20) | l, s), 64 per
 * draw, so other sites, cells and tests keep their own streams. */
static void t5_random_word(const T5Cell *c, unsigned s, u8 *out)
{
    const u64 saved = rng_state;
    rng_state = test_seed(5, (c->n << 20) | c->l, s);
    u64 w = 0;
    for (unsigned j = 0; j < c->l; j++) {
        if (j % 64 == 0) w = rng64();
        out[j] = (u8)((w >> (63 - j % 64)) & 1u);
    }
    rng_state = saved;
}

/* Site s of the balanced reference: exactly l / 2 ones, shuffled. */
static void t5_balanced(const T5Cell *c, unsigned s, u8 *out)
{
    const u64 saved = rng_state;
    rng_state = test_seed(5, T5_REF_CONFIG, s);
    for (unsigned j = 0; j < c->l; j++) out[j] = (u8)(j < c->l / 2);
    shuffle_u8(out, c->l);
    rng_state = saved;
}

/* Writes each site's stream into streams[s * l ...] and tiles them into
 * data (blocks * n bytes).  blocks must be a multiple of l. */
static void t5_fill(const T5Cell *c, u8 *data, u8 *streams)
{
    if (c->l == 0 || c->blocks % c->l != 0) {
        fprintf(stderr, "Internal error: Test 5 cell N=%u L=%u has %zu blocks, not whole periods\n",
                c->n, c->l, c->blocks);
        exit(1);
    }
    for (unsigned s = 0; s < c->n; s++) {
        u8 *st = streams + (size_t)s * c->l;
        switch (c->family) {
        case T5_RANDOM_WORDS: t5_random_word(c, s, st); break;
        case T5_BALANCED_REF: t5_balanced(c, s, st); break;
        case T5_DEBRUIJN:
            if (!debruijn_prefer_one(log2_exact(c->l), st)) memset(st, 0, c->l);
            break;   /* an invalid sequence fails t5_validate() */
        case T5_ALTERNATING:
            for (unsigned j = 0; j < c->l; j++) st[j] = (u8)(j & 1u);
            break;
        default:
            memset(st, 0, c->l);
            break;
        }
    }
    for (size_t b = 0; b < c->blocks; b++)
        for (unsigned s = 0; s < c->n; s++)
            data[b * c->n + s] = streams[(size_t)s * c->l + b % c->l];
}

/* 1 if stream st (l bytes) is what its family promises. */
static int t5_family_ok(const T5Cell *c, const u8 *st)
{
    const size_t ones = count_ones(st, c->l);
    switch (c->family) {
    case T5_DEBRUIJN:     return debruijn_valid(st, log2_exact(c->l));
    case T5_BALANCED_REF: return ones == c->l / 2;
    case T5_CONSTANT0:    return ones == 0;
    case T5_ALTERNATING:
        for (unsigned j = 0; j < c->l; j++)
            if (st[j] != (u8)(j & 1u)) return 0;
        return 1;
    default:              return 1;
    }
}

/* Rebuilds every site's stream from data alone (the first l blocks), checks
 * that each byte is 0 or 1, that each site repeats with period l over all
 * blocks and that each stream fits its family, and counts the statistics.
 * Returns 0 on any mismatch. */
static int t5_validate(const T5Cell *c, const u8 *data, T5Stats *st)
{
    const unsigned n = c->n;
    const unsigned l = c->l;
    u8 *streams = calloc((size_t)n * l, 1);
    if (!streams) { perror("calloc"); exit(1); }
    int ok = l > 0 && c->blocks >= l && c->blocks % l == 0;   /* at least one period */
    for (size_t b = 0; ok && b < c->blocks; b++)
        for (unsigned s = 0; ok && s < n; s++) {
            const u8 v = data[b * n + s];
            if (b < l) streams[(size_t)s * l + b] = v;
            ok = v <= 1 && v == data[(b % l) * n + s];
        }
    memset(st, 0, sizeof *st);
    for (unsigned s = 0; ok && s < n; s++) {
        const u8 *ss = streams + (size_t)s * l;
        ok = t5_family_ok(c, ss);
        st->ones += count_ones(ss, l) * (c->blocks / l);
        st->short_period += fundamental_period(ss, l) < l;
        int seen = 0;
        for (unsigned t = 0; t < s && !seen; t++)
            seen = memcmp(ss, streams + (size_t)t * l, l) == 0;
        st->distinct += !seen;
    }
    free(streams);
    return ok;
}

/* ── Test 5 grids ─────────────────────────────────────────────────────────
 * Per profile: groups of N × L cells sharing one block count (a multiple
 * of every L in the group, so each N has the same data footprint across
 * its periods), the training exposures, the minimum measured executions
 * and the point budget (training plus measured executions per trial).
 * The standard exposure was frozen from the i5-8350U and Raspberry Pi 4
 * pilots (README, Profile choices): some Pi cells still learn after 64. */
#define T5_TITLE "Test 5 — Pattern Learnability Matrix"
#define T5_DESC  "N static data branches per block, each reading its own repeating L-bit random word (block-major); constant, alternating, de Bruijn and balanced-reference controls at N = 1."
#define T5_MAX_ROWS            200
#define T5_MIN_MEASURE_PERIODS 64     /* complete periods per site, measured */
#define T5_CONTROL_BLOCKS      65536
#define T5_REF_LEN             65536

static const unsigned t5_n_small[] = { 1, 4, 16, 64 };
static const unsigned t5_n_large[] = { 256, 512 };
static const unsigned t5_n_one[]   = { 1 };
static const unsigned t5_l_quick_a[] = { 8, 32 };
static const unsigned t5_l_quick_b[] = { 16 };
static const unsigned t5_l_std_a[]   = { 8, 16, 32, 64, 128, 256 };
static const unsigned t5_l_std_b[]   = { 16, 32, 48 };
static const unsigned t5_l_res_a[]   = { 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256 };
static const unsigned t5_l_res_long[] = { 512, 1024, 4096, 16384, 65536 };
static const unsigned t5_db_all[]   = { 32, 64, 128, 256, 512, 1024, 2048 };
static const unsigned t5_db_quick[] = { 32, 2048 };
static const unsigned t5_exp_quick[] = { 8 };
static const unsigned t5_exp_std[]   = { 512 };
static const unsigned t5_exp_res[]   = { 8, 64, 512 };

#define T5_LEN(a) ((int)(sizeof a / sizeof a[0]))

typedef struct {
    const unsigned *n;
    int             nn;
    const unsigned *l;
    int             nl;
    size_t          blocks;
} T5Group;

#define T5_GROUP(ns, ls, b) { ns, T5_LEN(ns), ls, T5_LEN(ls), b }
static const T5Group t5_groups_quick[] = {
    T5_GROUP(t5_n_small, t5_l_quick_a, 256), T5_GROUP(t5_n_large, t5_l_quick_b, 96),
};
static const T5Group t5_groups_std[] = {
    T5_GROUP(t5_n_small, t5_l_std_a, 256), T5_GROUP(t5_n_large, t5_l_std_b, 96),
};
static const T5Group t5_groups_res[] = {
    T5_GROUP(t5_n_small, t5_l_res_a, 768), T5_GROUP(t5_n_large, t5_l_std_b, 96),
    T5_GROUP(t5_n_one, t5_l_res_long, 65536),
};

typedef struct {
    const T5Group  *groups;
    int             ngroups;
    const unsigned *exposures;
    int             nexp;
    const unsigned *db;           /* de Bruijn control periods */
    int             ndb;
    u64             min_execs;    /* measured data-branch executions per row */
    u64             budget;       /* training + measured executions per trial */
} T5Profile;

static const T5Profile t5_profiles[] = {
    { t5_groups_quick, T5_LEN(t5_groups_quick), t5_exp_quick, T5_LEN(t5_exp_quick),
      t5_db_quick, T5_LEN(t5_db_quick), 1ULL << 16, 1ULL << 24 },
    { t5_groups_std, T5_LEN(t5_groups_std), t5_exp_std, T5_LEN(t5_exp_std),
      t5_db_all, T5_LEN(t5_db_all), 1ULL << 22, 1ULL << 27 },
    { t5_groups_res, T5_LEN(t5_groups_res), t5_exp_res, T5_LEN(t5_exp_res),
      t5_db_all, T5_LEN(t5_db_all), 1ULL << 22, 1ULL << 28 },
};
CHECK_LEN(t5_profiles_len_check, t5_profiles, PROFILE_RESEARCH + 1);

/* Why a cell was not measured (exported as the "reason" parameter). */
enum { T5_MEASURED = 0, T5_SKIP_BUDGET = 1, T5_SKIP_BACKEND = 2 };

/* One Test 5 row: its cell, training exposure (periods per site), group
 * (index into the profile's groups; ngroups for the controls), measured
 * passes, skip reason and variant id. */
typedef struct {
    T5Cell   cell;
    unsigned exposure;
    int      group;
    unsigned passes;
    int      skip;
    int      variant;
} T5Row;

/* Measured passes: at least T5_MIN_MEASURE_PERIODS complete periods per
 * site and min_execs data-branch executions. */
static unsigned t5_passes(const T5Cell *c, u64 min_execs)
{
    const u64 per_pass = (u64)c->n * c->blocks;
    const u64 by_periods = ((u64)T5_MIN_MEASURE_PERIODS * c->l + c->blocks - 1) / c->blocks;
    const u64 by_execs = (min_execs + per_pass - 1) / per_pass;
    return (unsigned)(by_periods > by_execs ? by_periods : by_execs);
}

/* T5_MEASURED, or why the row is skipped: no kernel for its N in this
 * build, or training plus measurement over the profile's point budget. */
static int t5_skip_reason(const T5Row *r, const T5Profile *p)
{
    const T5Cell *c = &r->cell;
    if (!pattern_kernel_available(c->n)) return T5_SKIP_BACKEND;
    const u64 per_site = (u64)r->exposure * c->l + (u64)c->blocks * r->passes;
    return (u64)c->n * per_site > p->budget ? T5_SKIP_BUDGET : T5_MEASURED;
}

static int t5_add(T5Row *rows, int k, const T5Profile *p, const T5Cell *c,
                  unsigned exposure, int group)
{
    if (k >= T5_MAX_ROWS) log_full("Test 5 row table");
    T5Row *r = &rows[k];
    r->cell = *c;
    r->exposure = exposure;
    r->group = group;
    r->passes = t5_passes(c, p->min_execs);
    r->skip = t5_skip_reason(r, p);
    r->variant = -1;
    return k + 1;
}

/* The profile's rows: every group's N × L cells per exposure, then the
 * controls at the last exposure (constant-0, alternating, de Bruijn, the
 * balanced reference last).  Returns how many. */
static int t5_rows(const T5Profile *p, T5Row rows[T5_MAX_ROWS])
{
    int k = 0;
    for (int e = 0; e < p->nexp; e++)
        for (int g = 0; g < p->ngroups; g++) {
            const T5Group *gr = &p->groups[g];
            for (int i = 0; i < gr->nn; i++)
                for (int j = 0; j < gr->nl; j++) {
                    const T5Cell c = { gr->n[i], gr->l[j], gr->blocks, T5_RANDOM_WORDS };
                    k = t5_add(rows, k, p, &c, p->exposures[e], g);
                }
        }
    const unsigned last = p->exposures[p->nexp - 1];
    const T5Cell zero = { 1, 2, T5_CONTROL_BLOCKS, T5_CONSTANT0 };
    const T5Cell alt = { 1, 2, T5_CONTROL_BLOCKS, T5_ALTERNATING };
    k = t5_add(rows, k, p, &zero, last, p->ngroups);
    k = t5_add(rows, k, p, &alt, last, p->ngroups);
    for (int i = 0; i < p->ndb; i++) {
        const T5Cell db = { 1, p->db[i], T5_CONTROL_BLOCKS, T5_DEBRUIJN };
        k = t5_add(rows, k, p, &db, last, p->ngroups);
    }
    const T5Cell ref = { 1, T5_REF_LEN, T5_CONTROL_BLOCKS, T5_BALANCED_REF };
    const int first = k - 2 - p->ndb;
    k = t5_add(rows, k, p, &ref, last, p->ngroups);
    /* One pass count for all controls (the longest's), so the de Bruijn
     * 32 and reference pair, and any two controls, do equal work. */
    unsigned passes = 0;
    for (int i = first; i < k; i++)
        if (rows[i].passes > passes) passes = rows[i].passes;
    for (int i = first; i < k; i++) {
        rows[i].passes = passes;
        rows[i].skip = t5_skip_reason(&rows[i], p);
    }
    return k;
}

/* Per-site executions of each pattern kernel so far (training and timed),
 * for the exposure records; index by t5_kernel_index(). */
static u64 t5_site_execs[6];

static int t5_kernel_index(unsigned n)
{
    switch (n) {
    case 1:   return 0;
    case 4:   return 1;
    case 16:  return 2;
    case 64:  return 3;
    case 256: return 4;
    default:  return 5;
    }
}

/* Calls pattern_sites_N for N = n directly (no function pointers). */
#define T5_CALL(N) case N: return pattern_sites_##N(data, blocks, passes);
static u64 t5_run(unsigned n, const u8 *data, size_t blocks, unsigned passes)
{
    switch (n) {
    T5_CALL(1)
#if KERNEL_ASM
    T5_CALL(4) T5_CALL(16) T5_CALL(64) T5_CALL(256) T5_CALL(512)
#endif
    default:
        fprintf(stderr, "Internal error: no pattern kernel for N=%u\n", n);
        exit(1);
    }
}
#undef T5_CALL

/* Runs exactly `periods` periods of every site, untimed: whole passes
 * over the cell, then a prefix of whole periods.  Returns the kernels'
 * results. */
static u64 t5_train(unsigned n, const u8 *data, size_t blocks, unsigned l, unsigned periods)
{
    const u64 total = (u64)periods * l;   /* executions per site */
    const u64 q = total / blocks;
    const size_t rest = (size_t)(total % blocks);   /* a multiple of l */
    u64 acc = 0;
    if (q) acc += t5_run(n, data, blocks, (unsigned)q);
    if (rest) acc += t5_run(n, data, rest, 1);
    return acc;
}

/* A row's untimed preparation: training, then the exposure record of the
 * trial that follows (per-site executions of this kernel before it). */
static void t5_prepare(const T5Row *r, const u8 *data)
{
    const T5Cell *c = &r->cell;
    u64 *execs = &t5_site_execs[t5_kernel_index(c->n)];
    anti_dce_sink(t5_train(c->n, data, c->blocks, c->l, r->exposure));
    *execs += (u64)r->exposure * c->l;
    next_exposure = *execs;
    next_has_exposure = 1;
    *execs += (u64)c->blocks * r->passes;
}

/* A row's cell input, checked; NULL (after an error) if the validator
 * rejects it.  Records the input and parameters of variant r->variant. */
static u8 *t5_input(const T5Row *r, T5Stats *st)
{
    const T5Cell *c = &r->cell;
    u8 *data = malloc(c->blocks * c->n);
    u8 *streams = malloc((size_t)c->n * c->l);
    if (!data || !streams) { perror("malloc"); exit(1); }
    t5_fill(c, data, streams);
    free(streams);
    if (!t5_validate(c, data, st)) {
        fprintf(stderr, "  ERROR: Test 5 input N=%u L=%u (%s) failed validation\n",
                c->n, c->l, t5_family_ids[c->family]);
        free(data);
        return NULL;
    }
    log_input(r->variant, data, c->blocks * c->n, 1, st->ones);
    return data;
}

/* The parameters every Test 5 variant records; measured rows add their
 * stream statistics. */
static void t5_params(const T5Row *r, const T5Stats *st)
{
    const T5Cell *c = &r->cell;
    const int v = r->variant;
    log_param(v, "N", c->n);
    log_param(v, "L", c->l);
    log_param(v, "blocks", c->blocks);
    log_param(v, "passes", r->passes);
    log_param(v, "exposure", r->exposure);
    log_param(v, "family", (u64)c->family);
    log_param(v, "group", (u64)r->group);
    log_param(v, "data_bytes", (u64)c->n * c->blocks);
    log_param(v, "skipped", r->skip != T5_MEASURED);
    log_param(v, "reason", (u64)r->skip);
    if (!st) return;
    log_param(v, "ones", st->ones);
    log_param(v, "distinct", st->distinct);
    log_param(v, "short_period", st->short_period);
}

/* Measures one row on its own. */
#define T5_LONE(N)                                                          \
    case N:                                                                 \
        for (int k_ = 0; k_ < n_trials; k_++)                               \
            MEASURE_PREPARED(v, -1, k_, t5_prepare(r, data),                \
                             pattern_sites_##N(data, blocks, passes));      \
        break;

static int measure_t5_row(const T5Row *r, ReportRow *row)
{
    T5Stats st;
    u8 *data = t5_input(r, &st);
    if (!data) return 0;
    t5_params(r, &st);
    const int v = r->variant;
    const size_t blocks = r->cell.blocks;
    const unsigned passes = r->passes;
    const int n_trials = trial_count();
    switch (r->cell.n) {
    T5_LONE(1)
#if KERNEL_ASM
    T5_LONE(4) T5_LONE(16) T5_LONE(64) T5_LONE(256) T5_LONE(512)
#endif
    default:
        break;
    }
    row->r = representative(current_test, v);
    check_result("Test 5 pattern", row->r.result, st.ones * passes);
    free(data);
    return 1;
}
#undef T5_LONE

/* Measures two N = 1 rows of equal work as one pair, AB/BA. */
static int measure_t5_pair(const T5Row *ra, const T5Row *rb, ReportRow *a, ReportRow *b,
                           int *pair)
{
    T5Stats sa;
    T5Stats sb;
    u8 *da = t5_input(ra, &sa);
    u8 *db = da ? t5_input(rb, &sb) : NULL;
    if (!da || !db) { free(da); return 0; }
    t5_params(ra, &sa);
    t5_params(rb, &sb);
    const size_t blocks = ra->cell.blocks;
    const unsigned passes = ra->passes;
    const int p = new_pair(ra->variant, rb->variant);
    for (int k = 0; k < trial_count(); k++) {
        if (pair_a_first(k)) {
            MEASURE_PREPARED(ra->variant, p, k, t5_prepare(ra, da), pattern_sites_1(da, blocks, passes));
            MEASURE_PREPARED(rb->variant, p, k, t5_prepare(rb, db), pattern_sites_1(db, blocks, passes));
        } else {
            MEASURE_PREPARED(rb->variant, p, k, t5_prepare(rb, db), pattern_sites_1(db, blocks, passes));
            MEASURE_PREPARED(ra->variant, p, k, t5_prepare(ra, da), pattern_sites_1(da, blocks, passes));
        }
    }
    a->r = representative(current_test, ra->variant);
    b->r = representative(current_test, rb->variant);
    check_result("Test 5 pattern", a->r.result, sa.ones * passes);
    check_result("Test 5 pattern", b->r.result, sb.ones * passes);
    *pair = p;
    free(da);
    free(db);
    return 1;
}

/* Row labels; static because the variant table keeps pointers to them. */
static char t5_labels[T5_MAX_ROWS][40];

static void t5_label(const T5Row *r, char *buf, size_t n)
{
    const T5Cell *c = &r->cell;
    switch (c->family) {
    case T5_RANDOM_WORDS:
        snprintf(buf, n, "N=%3u L=%5u E=%3u", c->n, c->l, r->exposure);
        break;
    case T5_DEBRUIJN:
        snprintf(buf, n, "Control de Bruijn %4u", c->l);
        break;
    case T5_BALANCED_REF:
        snprintf(buf, n, "Control reference %u", c->l);
        break;
    default:
        snprintf(buf, n, "Control %s", t5_family_ids[c->family]);
        break;
    }
}

/* Gives measured rows variant ids 0, 1, ... in row order and skipped rows
 * the ids after them; returns how many rows are measured. */
static int t5_number(T5Row *t, int n)
{
    int next = 0;
    for (int i = 0; i < n; i++)
        if (t[i].skip == T5_MEASURED) t[i].variant = next++;
    const int measured = next;
    for (int i = 0; i < n; i++)
        if (t[i].skip != T5_MEASURED) t[i].variant = next++;
    return measured;
}

/* The index in t[] of the row with variant id v. */
static int t5_by_variant(const T5Row *t, int n, int v)
{
    for (int i = 0; i < n; i++)
        if (t[i].variant == v) return i;
    return -1;
}

/* Measures every unskipped row, the de Bruijn 32 and reference controls
 * as the pair.  rows[] is indexed by variant id.  Returns 0 if an input
 * failed validation (nothing more is measured). */
static int t5_measure_all(const T5Row *t, int n, ReportRow *rows, Work *works, int *pair)
{
    int db32 = -1;
    int ref = -1;
    for (int i = 0; i < n; i++) {
        if (t[i].skip != T5_MEASURED) continue;
        if (t[i].cell.family == T5_DEBRUIJN && t[i].cell.l == 32) db32 = i;
        if (t[i].cell.family == T5_BALANCED_REF) ref = i;
    }
    for (int i = 0; i < n; i++) {
        const T5Row *r = &t[i];
        if (r->skip != T5_MEASURED || i == ref) continue;
        ReportRow *row = &rows[r->variant];
        Work *w = &works[r->variant];
        w->count = (u64)r->cell.n * r->cell.blocks * r->passes;
        w->unit = "branch";
        row->work = w;
        if (i != db32) {
            if (!measure_t5_row(r, row)) return 0;
            continue;
        }
        ReportRow *rr = &rows[t[ref].variant];
        Work *rw = &works[t[ref].variant];
        *rw = *w;
        rr->work = rw;
        if (!measure_t5_pair(r, &t[ref], row, rr, pair)) return 0;
    }
    return 1;
}

/* Registers the skipped rows (after the measured ones) and says which:
 * a count of cells without their kernel in this build, and each cell over
 * the point budget. */
static void t5_emit_skipped(FILE *report, const T5Row *t, int n, int measured)
{
    static const Work none = { 0, "branch" };
    char list[512] = "";
    size_t used = 0;
    int backend = 0;
    for (int v = measured; v < n; v++) {
        const int i = t5_by_variant(t, n, v);
        log_variant(v, t5_labels[i], &none);
        t5_params(&t[i], NULL);
        if (t[i].skip == T5_SKIP_BACKEND) {
            backend++;
            continue;
        }
        char entry[64];
        snprintf(entry, sizeof entry, "N=%u L=%u E=%u", t[i].cell.n, t[i].cell.l, t[i].exposure);
        list_append(list, sizeof list, &used, entry);
    }
    if (backend) {
        printf("  → Skipped: %d cells with N > 1 (they need the assembly kernels)\n", backend);
        if (report)
            fprintf(report, "_Skipped: %d cells with N > 1 (they need the assembly kernels)._\n\n",
                    backend);
    }
    if (used) {
        printf("  → Skipped (over the point budget): %s\n", list);
        if (report) fprintf(report, "_Skipped (over the point budget): %s._\n\n", list);
    }
}

/* One matrix cell: misses (or cycles) per data-branch execution of the row
 * with group g, exposure e, N and L, "skip" or "–"; returns 1 if it holds a
 * number. */
static int t5_cell(char *buf, size_t bn, const T5Row *t, int n, const ReportRow *rows,
                   int g, unsigned e, unsigned nn, unsigned l, int ctr)
{
    for (int i = 0; i < n; i++) {
        const T5Row *r = &t[i];
        if (r->group != g || r->exposure != e || r->cell.n != nn || r->cell.l != l) continue;
        if (r->skip != T5_MEASURED) { snprintf(buf, bn, "skip"); return 0; }
        const Result *res = &rows[r->variant].r;
        const u64 work = rows[r->variant].work->count;
        if (!result_has(res, ctr) || !work) break;
        snprintf(buf, bn, ctr == CTR_MISSES ? "%.3f" : "%.2f",
                 (double)res->counts[ctr] / (double)work);
        return 1;
    }
    snprintf(buf, bn, "–");
    return 0;
}

/* One N × L table of a group at one exposure, if any cell has the counter. */
static void t5_matrix(FILE *report, const T5Row *t, int n, const ReportRow *rows,
                      const T5Group *gr, int g, unsigned e, int ctr)
{
    char cell[32];
    int any = 0;
    for (int i = 0; i < gr->nn; i++)
        for (int j = 0; j < gr->nl; j++)
            any += t5_cell(cell, sizeof cell, t, n, rows, g, e, gr->n[i], gr->l[j], ctr);
    if (!any) return;
    const char *what = ctr == CTR_MISSES ? "Misses" : "Cycles";
    printf("\n  %s per data branch (blocks %zu, E=%u):\n  %5s", what, gr->blocks, e, "N");
    if (report) fprintf(report, "**%s per data branch (blocks %zu, E=%u):**\n\n| N |",
                        what, gr->blocks, e);
    for (int j = 0; j < gr->nl; j++) {
        snprintf(cell, sizeof cell, "L=%u", gr->l[j]);
        printf(" %13s", cell);
        if (report) fprintf(report, " L=%u |", gr->l[j]);
    }
    printf("\n");
    if (report) {
        fputs("\n|---:|", report);
        for (int j = 0; j < gr->nl; j++) fputs("---:|", report);
        fputc('\n', report);
    }
    for (int i = 0; i < gr->nn; i++) {
        printf("  %5u", gr->n[i]);
        if (report) fprintf(report, "| %u |", gr->n[i]);
        for (int j = 0; j < gr->nl; j++) {
            t5_cell(cell, sizeof cell, t, n, rows, g, e, gr->n[i], gr->l[j], ctr);
            printf(" %13s", cell);
            if (report) fprintf(report, " %s |", cell);
        }
        printf("\n");
        if (report) fputc('\n', report);
    }
    if (report) fputc('\n', report);
}

/* The training, footprint, contamination and interpretation notes. */
static void t5_notes(FILE *report, const T5Profile *p)
{
    char exp[64] = "";
    char groups[128] = "";
    size_t eu = 0;
    size_t gu = 0;
    for (int e = 0; e < p->nexp; e++) {
        char x[16];
        snprintf(x, sizeof x, "%u", p->exposures[e]);
        list_append(exp, sizeof exp, &eu, x);
    }
    for (int g = 0; g < p->ngroups; g++) {
        char x[32];
        snprintf(x, sizeof x, "N × %zu", p->groups[g].blocks);
        list_append(groups, sizeof groups, &gu, x);
    }
    char line[4][320];
    snprintf(line[0], sizeof line[0], "Training: %s complete periods per site before every "
             "trial; cumulative exposure per kernel is in the export (exposure_before).", exp);
    snprintf(line[1], sizeof line[1], "Footprint: %s data bytes per cell in each group (%u "
             "for the controls); cache residency is not measured.", groups, T5_CONTROL_BLOCKS);
    snprintf(line[2], sizeof line[2], "Misses include the block and pass loop branches "
             "(1 per N data branches, plus pass exits).");
    snprintf(line[3], sizeof line[3], "These rates describe this kernel's learnability on "
             "this machine; no knee here is a table capacity or history length.");
    for (int i = 0; i < 4; i++) {
        printf("  → %s\n", line[i]);
        if (report) fprintf(report, "_%s_\n\n", line[i]);
    }
}

static void t5_summary(FILE *report, const T5Row *t, int n, const ReportRow *rows,
                       const T5Profile *p, int pair)
{
    for (int e = 0; e < p->nexp; e++)
        for (int g = 0; g < p->ngroups; g++) {
            t5_matrix(report, t, n, rows, &p->groups[g], g, p->exposures[e], CTR_MISSES);
            t5_matrix(report, t, n, rows, &p->groups[g], g, p->exposures[e], CTR_CYCLES);
        }
    printf("\n");
    if (pair >= 0) {
        emit_pair_ratio(report, "  Reference vs de Bruijn 32: ", "Reference vs de Bruijn 32",
                        pair, "");
        print_pair_cost(report, pair);
    }
    t5_notes(report, p);
}

static void run_test5(FILE *report)
{
    seed_test(5);
    const T5Profile *p = &t5_profiles[profile];
    T5Row t[T5_MAX_ROWS];
    const int n = t5_rows(p, t);
    const int measured = t5_number(t, n);
    printf("TEST 5 — Pattern Learnability Matrix  (%s profile: %d rows, %d measured)\n",
           profile_names[profile], n, measured);
    printf("  %s\n\n", T5_DESC);
    for (int i = 0; i < n; i++) t5_label(&t[i], t5_labels[i], sizeof t5_labels[i]);

    ReportRow *rows = calloc((size_t)(measured ? measured : 1), sizeof *rows);
    Work *works = calloc((size_t)(measured ? measured : 1), sizeof *works);
    if (!rows || !works) { perror("calloc"); exit(1); }
    for (int i = 0; i < n; i++)
        if (t[i].skip == T5_MEASURED) rows[t[i].variant].label = t5_labels[i];
    int pair = -1;
    if (!t5_measure_all(t, n, rows, works, &pair)) {
        sanity_failed = 1;
        free(rows);
        free(works);
        return;
    }
    if (measured) emit_rows(report, T5_TITLE, T5_DESC, rows, measured, &works[0]);
    t5_emit_skipped(report, t, n, measured);
    t5_summary(report, t, n, rows, p, pair);
    free(rows);
    free(works);
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *
 *  TEST 6 — Loop Exits: one counted loop over many trip counts  (opt-in)
 *
 *  loop_exit(c, loops) runs an inner loop of c iterations, loops times.
 *  The trip count is a run-time argument, so every count uses the same
 *  inner loop branch (taken c − 1 times, not taken once per loop) and the
 *  same body.  loop_exit_body2 is the same loop with a second body shape,
 *  a separately identified control.  Up to revision 3, Test 6 was
 *  Correlation Distance; those results keep that identity.
 *
 * ═══════════════════════════════════════════════════════════════════════════ */

#define T6_TITLE "Test 6 — Loop Exits"
#define T6_DESC  "One counted inner loop, same branch address for every trip count C; work is reported per completed loop and per inner iteration."
#define T6_MAX_ROWS 200
#define T6_CONSTRUCTION "Construction: one inner loop branch (dec/jnz; subs/b.ne on AArch64) " \
    "serves every count, taken C−1 times and not taken once per loop; misses per loop " \
    "include the outer loop branch and the call. Loop-count limits seen here describe " \
    "this kernel on this machine, not a predictor's capacity."

/* The whole body is one asm block, as in the data-branch kernels: the
 * exits for no loops or a zero count, the outer loop and the inner loop. */
__attribute__((noinline))
static u64 loop_exit(unsigned c, u64 loops)
{
    u64 acc = 0;
#if KERNEL_ASM
    u64 i;
#  if defined(__x86_64__)
    __asm__ __volatile__(
        "test %[l], %[l]\n\t"
        "jz 4f\n\t"
        "test %k[c], %k[c]\n\t"
        "jz 4f\n"
        "1:\n\t"
        "mov %k[c], %k[i]\n"
        "2:\n\t"
        "inc %[acc]\n\t"
        "dec %[i]\n\t"
        "jnz 2b\n\t"                 /* <── THE inner loop branch */
        "dec %[l]\n\t"
        "jnz 1b\n"
        "4:"
        : [acc] "+r"(acc), [i] "=&r"(i), [l] "+r"(loops)
        : [c] "r"(c)
        : "cc");
#  else
    __asm__ __volatile__(
        "cbz %[l], 4f\n\t"
        "cbz %w[c], 4f\n"
        "1:\n\t"
        "mov %w[i], %w[c]\n"
        "2:\n\t"
        "add %[acc], %[acc], #1\n\t"
        "subs %[i], %[i], #1\n\t"
        "b.ne 2b\n\t"                /* <── THE inner loop branch */
        "subs %[l], %[l], #1\n\t"
        "b.ne 1b\n"
        "4:"
        : [acc] "+r"(acc), [i] "=&r"(i), [l] "+r"(loops)
        : [c] "r"(c)
        : "cc");
#  endif
#else
    if (c == 0) return 0;   /* else GCC keeps an empty outer loop for c = 0 */
    for (u64 l = 0; l < loops; l++)
        for (unsigned i = c; i != 0; i--) {
            acc++;
            __asm__ __volatile__("" : "+r"(acc));   /* no closed form */
        }
#endif
    return acc;
}

/* loop_exit with a second body: acc += i, then acc += 1. */
__attribute__((noinline))
static u64 loop_exit_body2(unsigned c, u64 loops)
{
    u64 acc = 0;
#if KERNEL_ASM
    u64 i;
#  if defined(__x86_64__)
    __asm__ __volatile__(
        "test %[l], %[l]\n\t"
        "jz 4f\n\t"
        "test %k[c], %k[c]\n\t"
        "jz 4f\n"
        "1:\n\t"
        "mov %k[c], %k[i]\n"
        "2:\n\t"
        "add %[i], %[acc]\n\t"
        "inc %[acc]\n\t"
        "dec %[i]\n\t"
        "jnz 2b\n\t"                 /* <── THE inner loop branch */
        "dec %[l]\n\t"
        "jnz 1b\n"
        "4:"
        : [acc] "+r"(acc), [i] "=&r"(i), [l] "+r"(loops)
        : [c] "r"(c)
        : "cc");
#  else
    __asm__ __volatile__(
        "cbz %[l], 4f\n\t"
        "cbz %w[c], 4f\n"
        "1:\n\t"
        "mov %w[i], %w[c]\n"
        "2:\n\t"
        "add %[acc], %[acc], %[i]\n\t"
        "add %[acc], %[acc], #1\n\t"
        "subs %[i], %[i], #1\n\t"
        "b.ne 2b\n\t"                /* <── THE inner loop branch */
        "subs %[l], %[l], #1\n\t"
        "b.ne 1b\n"
        "4:"
        : [acc] "+r"(acc), [i] "=&r"(i), [l] "+r"(loops)
        : [c] "r"(c)
        : "cc");
#  endif
#else
    if (c == 0) return 0;   /* else GCC keeps an empty outer loop for c = 0 */
    for (u64 l = 0; l < loops; l++)
        for (unsigned i = c; i != 0; i--) {
            acc += (u64)i + 1;
            __asm__ __volatile__("" : "+r"(acc));   /* no closed form */
        }
#endif
    return acc;
}

/* Trip counts per profile.  The standard list was frozen from the
 * i5-8350U and Raspberry Pi 4 pilots (README, Profile choices): dense
 * where either machine's misses per loop change; research adds every
 * count 1–160. */
static const unsigned t6_counts_quick[] = { 1, 2, 4, 16, 64, 256 };
static const unsigned t6_counts_standard[] = {
    1, 2, 3, 4, 8, 16, 32, 34, 35, 36, 40, 44, 48, 64, 74, 75, 92, 94, 96, 256,
};
static const unsigned t6_counts_research_tail[] = { 192, 256, 384, 512 };
#define T6_RESEARCH_DENSE 160
#define T6_N_STANDARD ((int)(sizeof t6_counts_standard / sizeof t6_counts_standard[0]))

/* One Test 6 row: trip count, body shape (1 or 2) and loops run. */
typedef struct {
    unsigned c;
    int      body;
    u64      loops;
} T6Row;

/* Inner iterations per row (loops = budget / C). */
static u64 t6_budget(void)
{
    return profile == PROFILE_QUICK ? 1ULL << 18 : 1ULL << 24;
}

static int t6_add(T6Row *rows, int n, unsigned c, int body)
{
    rows[n].c = c;
    rows[n].body = body;
    rows[n].loops = t6_budget() / c;
    return n + 1;
}

/* The profile's rows in measurement order; returns how many. */
static int t6_rows(T6Row rows[T6_MAX_ROWS])
{
    int n = 0;
    if (profile == PROFILE_QUICK) {
        for (size_t i = 0; i < sizeof t6_counts_quick / sizeof t6_counts_quick[0]; i++)
            n = t6_add(rows, n, t6_counts_quick[i], 1);
    } else if (profile == PROFILE_STANDARD) {
        for (int i = 0; i < T6_N_STANDARD; i++)
            n = t6_add(rows, n, t6_counts_standard[i], 1);
    } else {
        for (unsigned c = 1; c <= T6_RESEARCH_DENSE; c++)
            n = t6_add(rows, n, c, 1);
        for (size_t i = 0; i < sizeof t6_counts_research_tail / sizeof t6_counts_research_tail[0]; i++)
            n = t6_add(rows, n, t6_counts_research_tail[i], 1);
        for (int i = 0; i < T6_N_STANDARD; i++)
            n = t6_add(rows, n, t6_counts_standard[i], 2);
    }
    return n;
}

/* What a row's kernel returns, computed without the kernel. */
static u64 t6_reference(const T6Row *t)
{
    const u64 c = t->c;
    return t->body == 1 ? t->loops * c : t->loops * (c * (c + 1) / 2 + c);
}

/* Measures one row and records its parameters. */
static void measure_loop(int v, const T6Row *t, ReportRow *row, Work *work)
{
    const unsigned c = t->c;
    const u64 loops = t->loops;
    log_param(v, "C", c);
    log_param(v, "loops", loops);
    log_param(v, "iters", (u64)c * loops);
    log_param(v, "body", (u64)t->body);
    work->count = loops;
    work->unit  = "loop";
    row->work = work;
    if (t->body == 1)
        MEASURE(row->r, v, loop_exit(c, loops));
    else
        MEASURE(row->r, v, loop_exit_body2(c, loops));
    check_result("Test 6 loop", row->r.result, t6_reference(t));
}

/* A curve cell: value / per with three decimals, or "–" without the counter. */
static void curve_cell(char *buf, size_t n, const Result *r, int ctr, u64 per)
{
    if (result_has(r, ctr) && per)
        snprintf(buf, n, "%.3f", (double)r->counts[ctr] / (double)per);
    else
        snprintf(buf, n, "–");
}

/* The loop-exit curve: per row, cycles per inner iteration and misses per
 * completed loop, then how the loop is built. */
static void emit_loop_curve(FILE *report, const ReportRow *rows, const T6Row *t, int n)
{
    printf("\n  Loop-exit curve:\n  %-14s %4s %12s %10s %12s %12s\n",
           "Variant", "C", "Iterations", "Loops", "Cycles/iter", "Misses/loop");
    if (report)
        fprintf(report, "**Loop-exit curve:**\n\n"
                        "| Variant | C | Iterations | Loops | Cycles/iter | Misses/loop |\n"
                        "|---|---:|---:|---:|---:|---:|\n");
    for (int i = 0; i < n; i++) {
        char cyc[32];
        char mis[32];
        const u64 iters = (u64)t[i].c * t[i].loops;
        curve_cell(cyc, sizeof cyc, &rows[i].r, CTR_CYCLES, iters);
        curve_cell(mis, sizeof mis, &rows[i].r, CTR_MISSES, t[i].loops);
        printf("  %-14s %4u %12" PRIu64 " %10" PRIu64 " %12s %12s\n",
               rows[i].label, t[i].c, iters, t[i].loops, cyc, mis);
        if (report) {
            fputs("| ", report);
            fputs_collapsed(report, rows[i].label);
            fprintf(report, " | %u | %" PRIu64 " | %" PRIu64 " | %s | %s |\n",
                    t[i].c, iters, t[i].loops, cyc, mis);
        }
    }
    printf("\n  → %s\n", T6_CONSTRUCTION);
    if (report) fprintf(report, "\n_%s_\n\n", T6_CONSTRUCTION);
}

/* Row labels; static because the variant table keeps pointers to them. */
static char t6_labels[T6_MAX_ROWS][24];

static void run_test6(FILE *report)
{
    seed_test(6);
    T6Row t[T6_MAX_ROWS];
    const int n = t6_rows(t);
    printf("TEST 6 — Loop Exits  (%s profile: %d rows, %" PRIu64 " inner iterations per row)\n",
           profile_names[profile], n, t6_budget());
    printf("  %s\n\n", T6_DESC);

    ReportRow *rows = malloc((size_t)n * sizeof *rows);
    Work *works = malloc((size_t)n * sizeof *works);
    if (!rows || !works) { perror("malloc"); exit(1); }
    const Result none = {0};
    for (int i = 0; i < n; i++) {
        snprintf(t6_labels[i], sizeof t6_labels[i],
                 t[i].body == 1 ? "C=%4u" : "C=%4u body 2", t[i].c);
        rows[i].label = t6_labels[i];
        rows[i].r     = none;
        measure_loop(i, &t[i], &rows[i], &works[i]);
    }
    emit_rows(report, T6_TITLE, T6_DESC, rows, n, &works[0]);
    emit_loop_curve(report, rows, t, n);
    free(rows);
    free(works);
    printf("\n");
}

/* Each test's identity in the export: test numbers are reused across
 * revisions (Test 6 was Correlation Distance up to revision 3), so the id
 * says which test a number means. */
typedef struct {
    const char *id;
    int         revision;
    const char *title;
} TestDesc;

static const TestDesc test_descs[] = {
    { "threshold-sum",        1, "Test 1 — Threshold Sum"         },
    { "bias-ordering",        1, "Test 2 — Bias and Ordering"     },
    { "indirect-dispatch",    1, "Test 3 — Indirect Dispatch"     },
    { "branch-vs-branchless", 1, "Test 4 — Branch vs. Branchless" },
    { "pattern-matrix",       1, "Test 5 — Pattern Learnability Matrix" },
    { "loop-exits",           1, "Test 6 — Loop Exits"            },
};
CHECK_LEN(test_descs_len_check, test_descs, NUM_TESTS);

/* Measured variant streams of test n (a variant in two pairs counts
 * twice), for reserve_trials(). */
static int test_streams(int n)
{
    static const int streams[NUM_TESTS] = {
        2,                    /* Test 1: one pair */
        5 * 4 + 2,            /* Test 2: two pairs per bias, two constants */
        2 * T3_COUNTS,        /* Test 3: one pair per target count */
        6,                    /* Test 4: three pairs */
        0,                    /* Test 5: counted below, per profile */
        0,                    /* Test 6: counted below, per profile */
    };
    if (n == 5) {   /* every measured row is one stream (the pair's two included) */
        T5Row rows[T5_MAX_ROWS];
        const int k = t5_rows(&t5_profiles[profile], rows);
        return t5_number(rows, k);
    }
    if (n == 6) {
        T6Row rows[T6_MAX_ROWS];
        return t6_rows(rows);
    }
    return streams[n - 1];
}

static int test_selected(int n)
{
    return ((tests_selected ? tests_selected : DEFAULT_TESTS) >> (n - 1)) & 1u;
}

/* ─────────────────────────────────────────────────────────────────────────
 * JSON Lines export (--json), schema 1
 *
 * One object per line: a meta record, then the kernel, variant, trial and
 * pair records.  Written after all tests from the measurement log, so every
 * summary in it (and in the report) can be rebuilt from its trials;
 * .github/scripts/check_export.py does exactly that.  u64 values are bare
 * integers, doubles use %.17g, and anything absent is null.
 * ───────────────────────────────────────────────────────────────────────── */
#define JSON_SCHEMA 2

/* What each kernel's code is declared to contain.  check_kernels.sh
 * --counts measures the same fields from the binary; -1 is "not declared".
 * taken_when says when the data branch jumps relative to its source
 * condition ("false": it skips the update), as written in the asm; NULL
 * for C kernels (the compiler decides) and for kernels without a single
 * data branch.  No function pointers here: taking a
 * kernel's address can change how GCC compiles and places the kernels.
 * Entry addresses therefore come from the binary (check_kernels.sh), and
 * the export's "entry" is null. */
typedef struct {
    const char *id;
    int         revision;
    const char *backend;
    const char *unit;
    int         data_branches;
    int         indirect_calls;
    int         inner_loop_branches;
    const char *taken_when;
    const char *tests;
} KernelDesc;

#define KERNEL(fn, rev, backend) #fn, rev, backend
static const KernelDesc kernel_descs[] = {
    { KERNEL(sum_threshold, 2, KERNEL_BACKEND),   "elem",  1, 0, 2, ASM_TAKEN_WHEN, "1,4" },
    { KERNEL(sum_branchless, 1, "c"),             "elem",  0, 0, 1, NULL,           "4"   },
    { KERNEL(count_decisions, 2, KERNEL_BACKEND), "elem",  1, 0, 2, ASM_TAKEN_WHEN, "2"   },
    { KERNEL(pattern_sites_1, 1, KERNEL_BACKEND), "branch", 1, 0, 2, ASM_TAKEN_WHEN, "5"  },
#if KERNEL_ASM
    { KERNEL(pattern_sites_4, 1, KERNEL_BACKEND),   "branch", 4,   0, 5,   ASM_TAKEN_WHEN, "5" },
    { KERNEL(pattern_sites_16, 1, KERNEL_BACKEND),  "branch", 16,  0, 17,  ASM_TAKEN_WHEN, "5" },
    { KERNEL(pattern_sites_64, 1, KERNEL_BACKEND),  "branch", 64,  0, 65,  ASM_TAKEN_WHEN, "5" },
    { KERNEL(pattern_sites_256, 1, KERNEL_BACKEND), "branch", 256, 0, 257, ASM_TAKEN_WHEN, "5" },
    { KERNEL(pattern_sites_512, 1, KERNEL_BACKEND), "branch", 512, 0, 513, ASM_TAKEN_WHEN, "5" },
#endif
    { KERNEL(dispatch_indexed, 1, "c"),           "call",  0, 1, 2, NULL,           "3"   },
    { KERNEL(loop_exit, 1, KERNEL_BACKEND),       "iter",  0, 0, 1, NULL,           "6"   },
    { KERNEL(loop_exit_body2, 1, KERNEL_BACKEND), "iter",  0, 0, 1, NULL,           "6"   },
};
#undef KERNEL

/* Writes s as a JSON string, escaping quotes, backslashes and control
 * bytes; other bytes (UTF-8) pass through. */
static void json_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') fprintf(f, "\\%c", *p);
        else if (*p == '\n') fputs("\\n", f);
        else if (*p == '\t') fputs("\\t", f);
        else if (*p < 0x20) fprintf(f, "\\u%04x", *p);
        else fputc(*p, f);
    }
    fputc('"', f);
}

static void json_string_or_null(FILE *f, const char *s)
{
    if (s && *s) json_string(f, s);
    else fputs("null", f);
}

/* A double, or null where JSON has no number (inf, nan) or none applies. */
static void json_double(FILE *f, int has, double v)
{
    if (has && isfinite(v)) fprintf(f, "%.17g", v);
    else fputs("null", f);
}

static void json_int(FILE *f, int has, long long v)
{
    if (has) fprintf(f, "%lld", v);
    else fputs("null", f);
}

/* 1 if two open files are the same file (equal paths, hard links, "a" vs
 * "./a"), 0 if not, -1 if either cannot be examined. */
static int same_open_file(FILE *a, FILE *b)
{
    struct stat sa;
    struct stat sb;
    if (fstat(fileno(a), &sa) != 0 || fstat(fileno(b), &sb) != 0) return -1;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

/* First line of a text file, without its newline; 0 if unreadable or empty. */
static int sysfs_line(const char *path, char *buf, size_t n)
{
    char line[256];
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    const int ok = fgets(line, sizeof line, f) != NULL;
    fclose(f);
    if (!ok) return 0;
    const int len = (int)strcspn(line, "\r\n");
    snprintf(buf, n, "%.*s", len, line);
    return len > 0;
}

/* Value of the first /proc/cpuinfo line whose key is exactly `key`. */
static int cpuinfo_value(const char *key, char *buf, size_t n)
{
    int found = 0;
#ifdef __linux__
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return 0;
    char line[4096];   /* x86 flags lines pass 1 KB on recent CPUs */
    const size_t klen = strlen(key);
    while (fgets(line, sizeof line, f)) {
        const char *colon = strchr(line, ':');
        if (!colon || strncmp(line, key, klen) != 0 ||
            strspn(line + klen, " \t") != (size_t)(colon - line) - klen)
            continue;
        const char *v = colon + 1 + strspn(colon + 1, " \t");
        const int len = (int)strcspn(v, "\r\n");
        if (len > 0) {
            snprintf(buf, n, "%.*s", len, v);
            found = 1;
            break;
        }
    }
    fclose(f);
#else
    (void)key; (void)buf; (void)n;
#endif
    return found;
}

/* 1 or 0 if /proc/cpuinfo's flags list has `flag`, -1 without a flags line. */
static int cpuinfo_has_flag(const char *flag)
{
    char flags[4096];
    if (!cpuinfo_value("flags", flags, sizeof flags)) return -1;
    const size_t len = strlen(flag);
    const char *p = flags + strspn(flags, " ");
    while (*p) {
        const size_t word = strcspn(p, " ");
        if (word == len && strncmp(p, flag, len) == 0) return 1;
        p += word;
        p += strspn(p, " ");
    }
    return 0;
}

static void write_counters_meta(FILE *f)
{
    fputs(",\"counters\":[", f);
    for (int c = 0; c < CTR_COUNT; c++) {
        const int open = (int)((perf_mask() >> c) & 1u);
        const int raw  = (int)((perf_raw_mask >> c) & 1u);
        const CounterDesc *d = &counter_descs[c];
        fprintf(f, "%s{\"name\":", c ? "," : "");
        json_string(f, d->console_label);
        const char *type = raw ? "raw" : "hardware";
        fprintf(f, ",\"open\":%s,\"type\":", open ? "true" : "false");
        json_string_or_null(f, open ? type : NULL);
        fputs(",\"config\":", f);
        json_int(f, open, (long long)(raw ? d->arm_raw : d->generic));
        fputs(",\"semantics\":", f);
        json_string_or_null(f, open ? semantics_name(counter_semantics(c, raw, host_arch())) : NULL);
        fputs(",\"exclude_kernel\":true,\"exclude_hv\":true}", f);
    }
    fputs("]", f);
}

static void write_tests_meta(FILE *f)
{
    fputs(",\"test_seeds\":{", f);
    int first = 1;
    for (int n = 1; n <= NUM_TESTS; n++) {
        if (!test_selected(n)) continue;
        fprintf(f, "%s\"%d\":%" PRIu64, first ? "" : ",", n, test_seed(n, 0, 0));
        first = 0;
    }
    fputs("},\"tests\":[", f);
    first = 1;
    for (int n = 1; n <= NUM_TESTS; n++) {
        if (!test_selected(n)) continue;
        fprintf(f, "%s%d", first ? "" : ",", n);
        first = 0;
    }
    fputs("]", f);
}

static void write_affinity_meta(FILE *f)
{
    static const char *const status[] = { "not_requested", "pinned", "failed", "unsupported" };
    fputs(",\"affinity\":{\"requested\":", f);
    json_int(f, cpu_requested >= 0, cpu_requested);
    fprintf(f, ",\"status\":\"%s\",\"error\":", status[affinity_status]);
    json_string_or_null(f, affinity_status == AFF_FAILED ? affinity_error() : NULL);
    fputs("}", f);
}

/* ,"key":"<first line of /sys/devices/system/cpu/cpu<cpu>/<leaf>>" or null. */
static void json_cpu_sysfs(FILE *f, const char *key, int cpu, const char *leaf)
{
    char path[128];
    char buf[256];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/%s", cpu, leaf);
    fprintf(f, ",\"%s\":", key);
    json_string_or_null(f, sysfs_line(path, buf, sizeof buf) ? buf : NULL);
}

/* SMT siblings and governor of metadata_cpu (the pinned CPU, else CPU 0);
 * the hypervisor flag from /proc/cpuinfo's first flags line; max_ghz and
 * the effective rate are machine- and run-wide. */
static void write_system_meta(FILE *f)
{
    const int cpu = affinity_status == AFF_PINNED ? cpu_requested : 0;
    fprintf(f, ",\"metadata_cpu\":%d,\"hypervisor\":", cpu);
    const int hv = cpuinfo_has_flag("hypervisor");
    if (hv < 0) fputs("null", f);
    else fputs(hv ? "true" : "false", f);
    json_cpu_sysfs(f, "smt_siblings", cpu, "topology/thread_siblings_list");
    json_cpu_sysfs(f, "governor", cpu, "cpufreq/scaling_governor");
    const double max_ghz = cpu_max_ghz();
    const double eff_ghz = measured_ghz();
    fputs(",\"max_ghz\":", f);
    json_double(f, max_ghz > 0, max_ghz);
    fputs(",\"effective_user_cycle_ghz\":", f);
    json_double(f, eff_ghz > 0, eff_ghz);
}

static void write_meta(FILE *f)
{
    char cpu[192];
    char microcode[64];
    char kernel[160];
    struct utsname u;
    const int have_uname = uname(&u) == 0;
    get_cpu_model(cpu, sizeof cpu);
    if (have_uname) snprintf(kernel, sizeof kernel, "%s %s", u.sysname, u.release);
    fprintf(f, "{\"type\":\"meta\",\"schema\":%d,\"revision\":%d,\"cpu\":", JSON_SCHEMA, BENCH_REVISION);
    json_string(f, cpu);
    fputs(",\"microcode\":", f);
    json_string_or_null(f, cpuinfo_value("microcode", microcode, sizeof microcode) ? microcode : NULL);
    fputs(",\"arch\":", f);
    json_string_or_null(f, have_uname ? u.machine : NULL);
    fputs(",\"kernel\":", f);
    json_string_or_null(f, have_uname ? kernel : NULL);
    fputs(",\"compiler\":", f);
    json_string(f, COMPILER_VERSION);
    fputs(",\"kernel_backend\":", f);
    json_string(f, KERNEL_BACKEND);
    fprintf(f, ",\"results_ok\":%s", sanity_failed ? "false" : "true");
    fputs(",\"build_flags\":", f);
    json_string_or_null(f, BUILD_FLAGS);
    fputs(",\"commit\":", f);
    json_string_or_null(f, BUILD_COMMIT);
    fprintf(f, ",\"alignment_policy\":\"%s\",\"seed\":%" PRIu64,
            strstr(BUILD_FLAGS, "mbranches-within-32B-boundaries") ? "jcc-32B" : "undeclared",
            base_seed);
    write_tests_meta(f);
    fprintf(f, ",\"profile\":\"%s\"", profile_names[profile]);
    fprintf(f, ",\"size\":%zu,\"reps\":%u,\"trials\":%d,\"training_runs\":%d,"
               "\"schedule\":\"AB/BA\"", size, reps, trial_count(), TRAIN_RUNS);
    write_affinity_meta(f);
    write_system_meta(f);
    write_counters_meta(f);
    fputs("}\n", f);
}

static void write_kernels(FILE *f)
{
    for (size_t i = 0; i < sizeof kernel_descs / sizeof kernel_descs[0]; i++) {
        const KernelDesc *k = &kernel_descs[i];
        fputs("{\"type\":\"kernel\",\"id\":", f);
        json_string(f, k->id);
        fprintf(f, ",\"revision\":%d,\"backend\":", k->revision);
        json_string(f, k->backend);
        fputs(",\"entry\":null,\"unit\":", f);
        json_string(f, k->unit);
        fputs(",\"data_branches\":", f);
        json_int(f, k->data_branches >= 0, k->data_branches);
        fputs(",\"indirect_calls\":", f);
        json_int(f, k->indirect_calls >= 0, k->indirect_calls);
        fputs(",\"inner_loop_branches\":", f);
        json_int(f, k->inner_loop_branches >= 0, k->inner_loop_branches);
        fputs(",\"taken_when\":", f);
        json_string_or_null(f, k->taken_when);
        fputs(",\"tests\":", f);
        json_string(f, k->tests);
        fputs("}\n", f);
    }
}

/* Kernel name of a variant: its first trial's call text up to "(". */
static void variant_kernel(int test, int variant, char *buf, size_t n)
{
    buf[0] = '\0';
    for (int i = 0; i < trial_log_len; i++) {
        if (trial_log[i].test != test || trial_log[i].variant != variant) continue;
        snprintf(buf, n, "%.*s", (int)strcspn(trial_log[i].call, "("), trial_log[i].call);
        return;
    }
}

/* Copies s into out (at most cap - 1 bytes) with runs of spaces collapsed,
 * as fputs_collapsed() writes labels to the Markdown report. */
static void collapse_spaces(const char *s, char *out, size_t cap)
{
    size_t n = 0;
    const char *p = s;
    while (*p && n + 1 < cap) {
        if (!(p[0] == ' ' && p[1] == ' ')) out[n++] = *p;
        p++;
    }
    out[n] = '\0';
}

/* A variant's parameters as a JSON object, or null without any. */
static void write_params(FILE *f, const VariantInfo *v)
{
    if (!v->n_params) {
        fputs("null", f);
        return;
    }
    fputc('{', f);
    for (int i = 0; i < v->n_params; i++) {
        if (i) fputc(',', f);
        json_string(f, v->param_key[i]);
        fprintf(f, ":%" PRIu64, v->param_val[i]);
    }
    fputc('}', f);
}

static void write_variants(FILE *f)
{
    for (int i = 0; i < variant_count; i++) {
        const VariantInfo *v = &variant_info[i];
        if (!v->label) {   /* input logged, but no row was emitted */
            fprintf(stderr, "Internal error: test %d variant %d has an input but no row\n",
                    v->test, v->variant);
            exit(1);
        }
        char label[128];
        char kernel[64];
        collapse_spaces(v->label, label, sizeof label);
        variant_kernel(v->test, v->variant, kernel, sizeof kernel);
        fprintf(f, "{\"type\":\"variant\",\"test\":%d,\"variant\":%d,\"label\":", v->test, v->variant);
        json_string(f, label);
        fputs(",\"kernel\":", f);
        json_string_or_null(f, kernel);
        fprintf(f, ",\"work\":%" PRIu64 ",\"unit\":", v->work.count);
        json_string(f, v->work.unit);
        fputs(",\"pass_items\":", f);
        json_int(f, v->has_input, (long long)v->pass_items);
        fputs(",\"cond_true\":", f);
        json_int(f, v->has_cond, (long long)v->cond_true);
        fputs(",\"input_hash\":", f);
        if (v->has_input) fprintf(f, "%" PRIu64, v->input_hash);
        else fputs("null", f);
        fputs(",\"params\":", f);
        write_params(f, v);
        fputs("}\n", f);
    }
}

/* {"branches":…,"misses":…,"cycles":…} from a trial's counts or raw counts. */
static void write_counts(FILE *f, const Result *r, const u64 *v)
{
    fputc('{', f);
    for (int c = 0; c < CTR_COUNT; c++) {
        fprintf(f, "%s\"%s\":", c ? "," : "", counter_descs[c].console_label);
        if (result_has(r, c)) fprintf(f, "%" PRIu64, v[c]);
        else fputs("null", f);
    }
    fputc('}', f);
}

static void write_trials(FILE *f)
{
    for (int i = 0; i < trial_log_len; i++) {
        const TrialRecord *t = &trial_log[i];
        fprintf(f, "{\"type\":\"trial\",\"test\":%d,\"variant\":%d,\"pair\":", t->test, t->variant);
        json_int(f, t->pair >= 0, t->pair);
        fprintf(f, ",\"seq\":%d,\"trial\":%d,\"train_runs\":", t->seq, t->trial);
        json_int(f, !t->has_exposure, TRAIN_RUNS);   /* Test 5 trains by exposure */
        fprintf(f, ",\"time_ms\":%.17g,"
                   "\"result\":%" PRIu64 ",\"perf_status\":\"%s\",\"enabled_ns\":%" PRIu64
                   ",\"running_ns\":%" PRIu64 ",\"counts\":",
                t->r.time_ms, t->r.result,
                perf_status_name(t->r.perf_status), t->r.enabled_ns, t->r.running_ns);
        write_counts(f, &t->r, t->r.counts);
        fputs(",\"raw_counts\":", f);
        write_counts(f, &t->r, t->r.raw_counts);
        fputs(",\"exposure_before\":", f);
        if (t->has_exposure) fprintf(f, "%" PRIu64, t->exposure_before);
        else fputs("null", f);
        fputs("}\n", f);
    }
}

static void write_pairs(FILE *f)
{
    static const char *const cost_status[] = { "unavailable", "inconclusive", "resolved" };
    for (int p = 0; p < pair_count; p++) {
        const PairInfo *pi = &pair_info[p];
        const PairStats s = pair_stats(p);
        double cost = 0;
        const int status = pair_cost(&s, &cost);
        fprintf(f, "{\"type\":\"pair\",\"test\":%d,\"pair\":%d,\"a\":%d,\"b\":%d,"
                   "\"pairs\":%d,\"valid\":%d,\"time_ms_a\":%.17g,\"time_ms_b\":%.17g,"
                   "\"slowdown\":", pi->test, p, pi->a, pi->b, s.pairs, s.valid, s.ms_a, s.ms_b);
        json_double(f, 1, s.ms_b / s.ms_a);
        fputs(",\"delta\":{", f);
        for (int c = 0; c < CTR_COUNT; c++) {
            fprintf(f, "%s\"%s\":", c ? "," : "", counter_descs[c].console_label);
            json_double(f, (int)((s.open_mask >> c) & 1u), s.delta[c]);
        }
        fputs("},\"effective_cost\":", f);
        json_double(f, status > 0, cost);
        fprintf(f, ",\"cost_status\":\"%s\",\"ratio_label\":", cost_status[status + 1]);
        json_string_or_null(f, pi->ratio_label);
        fprintf(f, ",\"cost_shown\":%s}\n", pi->cost_shown ? "true" : "false");
    }
}

/* One record per selected test: its number, id, revision and title. */
static void write_tests(FILE *f)
{
    for (int n = 1; n <= NUM_TESTS; n++) {
        if (!test_selected(n)) continue;
        const TestDesc *d = &test_descs[n - 1];
        fprintf(f, "{\"type\":\"test\",\"test\":%d,\"id\":", n);
        json_string(f, d->id);
        fprintf(f, ",\"revision\":%d,\"title\":", d->revision);
        json_string(f, d->title);
        fputs("}\n", f);
    }
}

static void write_export(FILE *f)
{
    write_meta(f);
    write_tests(f);
    write_kernels(f);
    write_variants(f);
    write_trials(f);
    write_pairs(f);
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

/* Parses a 64-bit seed: decimal, or hex after "0x".  A leading zero is
 * still decimal (strtoull's base 0 would read "010" as octal 8).  Prints an
 * error and returns 0 if invalid. */
static int parse_seed(const char *s, u64 *out)
{
    const int hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    const char *digits = hex ? s + 2 : s;
    const char *allowed = hex ? "0123456789abcdefABCDEF" : "0123456789";
    const size_t len = strlen(digits);
    errno = 0;
    const unsigned long long v = strtoull(digits, NULL, hex ? 16 : 10);
    if (!len || strspn(digits, allowed) != len || errno == ERANGE) {
        fprintf(stderr, "Invalid seed: %s (must be 0–18446744073709551615, decimal or 0x hex)\n", s);
        return 0;
    }
    *out = (u64)v;
    return 1;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n"
           "  -o, --output <file>  write a formatted Markdown report there\n"
           "                       (in addition to the normal console output)\n"
           "  -n, --trials <N>     measure each variant N times (1–%d), each trial\n"
           "                       after one untimed training run, and report the\n"
           "                       median trial (default: 1)\n"
           "  -t, --test <N>       run only test N (1–%d); repeat to run several\n"
           "                       (default: tests 1–4; tests 5–6 run only when selected)\n"
           "      --hard           run all tests, including the hard tests 5 and 6\n"
           "      --size <N>       elements per array (tests 1/2/4) and calls per\n"
           "                       trial (test 3); K/M suffixes allowed\n"
           "                       (default: %u = 4M, max: %lu = 1024M); tests 5\n"
           "                       and 6 take their sizes from --profile\n"
           "      --reps <N>       passes over the array per trial, tests 1/2/4\n"
           "                       (default: %u, max: %lu)\n"
           "      --profile <P>    quick, standard or research: the grids of tests 5\n"
           "                       and 6 (default: standard)\n"
           "      --json <file>    write every trial and summary as JSON Lines (schema 2)\n"
           "      --cpu <N>        pin the benchmark to CPU N (0–1023) before measuring\n"
           "      --seed <N>       base seed for all test inputs (default: 16045690984503054900)\n"
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

static int apply_run_option(int argc, char **argv, int *i, const char *arg);

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
    if (is_opt(arg, NULL, "--hard")) {
        tests_selected |= HARD_TESTS;
        hard_selected = 1;
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
    return apply_run_option(argc, argv, i, arg);
}

/* Sets profile from its name; prints an error and returns 0 if unknown. */
static int parse_profile(const char *s)
{
    for (int p = 0; p < (int)(sizeof profile_names / sizeof profile_names[0]); p++) {
        if (strcmp(s, profile_names[p]) != 0) continue;
        profile = (Profile)p;
        return 1;
    }
    fprintf(stderr, "Invalid profile: %s (must be quick, standard or research)\n", s);
    return 0;
}

/* apply_option()'s options that set up the run around the tests: --json,
 * --cpu, --profile and --seed.  Same return convention. */
static int apply_run_option(int argc, char **argv, int *i, const char *arg)
{
    unsigned long v;
    if (is_opt(arg, NULL, "--json")) {
        json_path = option_value(argc, argv, i, arg, "file name");
        return json_path ? -1 : 1;
    }
    if (is_opt(arg, NULL, "--cpu")) {
        if (!option_number(argc, argv, i, "CPU number", 0, MAX_CPU, &v)) return 1;
        cpu_requested = (int)v;
        return -1;
    }
    if (is_opt(arg, NULL, "--profile")) {
        const char *val = option_value(argc, argv, i, arg, "profile");
        return val && parse_profile(val) ? -1 : 1;
    }
    if (is_opt(arg, NULL, "--seed")) {
        const char *val = option_value(argc, argv, i, arg, "seed");
        if (!val || !parse_seed(val, &base_seed)) return 1;
        seed_given = 1;
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
    printf("  Revision:        %10d\n", BENCH_REVISION);
    printf("  Profile:         %s\n", profile_names[profile]);
    if (seed_given)
        printf("  Seed:            %20" PRIu64 "\n", base_seed);
    if (trials > 1)
        printf("  Trials:          %10d per variant (median shown; each after 1 untimed "
               "training run, AB/BA order)\n", trials);
    if (hard_selected)
        printf("  Tests:           %10s (all, with --hard)\n", tests_list);
    else if (tests_selected)
        printf("  Tests:           %10s (selected with -t)\n", tests_list);
    if (affinity_status == AFF_PINNED)
        printf("  CPU affinity:    pinned to CPU %d\n", cpu_requested);
    else if (affinity_status == AFF_FAILED)
        printf("  CPU affinity:    FAILED to pin to CPU %d (%s)\n", cpu_requested,
               affinity_error());
    else if (affinity_status == AFF_UNSUPPORTED)
        printf("  CPU affinity:    not supported on this system (CPU %d requested)\n",
               cpu_requested);
    printf("  Perf counters:   %s\n",
           perf_available() ? "AVAILABLE (hardware branch-miss counts shown)"
                            : "unavailable — showing wall-clock timing only");
    if (perf_raw_mask) {
        char raw[96];
        raw_events_list(raw, sizeof raw);
        printf("  Raw PMU events:  %s (generic events failed)\n", raw);
    }
    printf("  Compiler:        %s\n", COMPILER_VERSION);
    printf("  Kernels:         %s\n", KERNEL_ASM
           ? KERNEL_BACKEND " (the branch kernels of Tests 1, 2 and 4–6)"
           : "C only (branches depend on the compiler)");
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
    fprintf(report, "_Benchmark revision %d._\n\n", BENCH_REVISION);
    fprintf(report, "_Profile: %s._\n\n", profile_names[profile]);
    fprintf(report, "%s\n\n", KERNEL_ASM
            ? "_Kernels: " KERNEL_BACKEND " for the branch kernels of Tests 1, 2 and 4–6; "
              "the other kernels are C._"
            : "_Kernels: C only; their branches depend on the compiler (check with "
              "check_kernels.sh)._");
    if (seed_given)
        fprintf(report, "_Seed: %" PRIu64 "._\n\n", base_seed);
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
        fprintf(report, "_Median of %d trials per variant; each trial follows one "
                        "untimed training run, and paired variants alternate AB/BA. "
                        "Range is the fastest–slowest trial._\n\n", trials);
    if (hard_selected)
        fprintf(report, "_All tests were run (--hard), including the hard tests 5 and 6._\n\n");
    else if (tests_selected)
        fprintf(report, "_Only test(s) %s were run._\n\n", tests_list);
    if (affinity_status == AFF_PINNED)
        fprintf(report, "_Pinned to CPU %d._\n\n", cpu_requested);
    else if (affinity_status == AFF_FAILED)
        fprintf(report, "_CPU affinity request for CPU %d failed: %s._\n\n",
                cpu_requested, affinity_error());
    else if (affinity_status == AFF_UNSUPPORTED)
        fprintf(report, "_CPU affinity not supported on this system (CPU %d requested)._\n\n",
                cpu_requested);
}

/* User-mode cycles over wall time across all measured trials, when the
 * cycle counter worked.  Kernel time and preemption lower it, so it is an
 * effective rate, not the core's instantaneous clock. */
static void print_clock_summary(FILE *report)
{
    double avg_ghz = measured_ghz();
    double max_ghz = cpu_max_ghz();
    if (avg_ghz <= 0) return;
    printf("Effective user-cycle rate during measurements: %.2f GHz", avg_ghz);
    if (max_ghz > 0) printf(" (max %.2f GHz)", max_ghz);
    printf("\n\n");
    if (report)
        fprintf(report, "**Effective user-cycle rate during measurements:** %.2f GHz\n\n", avg_ghz);
}

/* Opens (creating, owner-only when new) an output file without truncating
 * it: open() rather than fopen() (0666).  open_outputs() truncates once
 * both files are known to be usable and distinct.  NULL after printing why. */
static FILE *open_output(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT, 0600);
    if (fd < 0) { perror(path); return NULL; }
    FILE *f = fdopen(fd, "w");
    if (!f) { perror("fdopen"); close(fd); }
    return f;
}

/* Empties a regular file (a device such as /dev/full is left as is, as
 * O_TRUNC would); 0 on success. */
static int truncate_output(FILE *f, const char *path)
{
    struct stat st;
    if (fstat(fileno(f), &st) == 0 && !S_ISREG(st.st_mode)) return 0;
    if (ftruncate(fileno(f), 0) == 0) return 0;
    perror(path);
    return 1;
}

/* Opens the -o report and the --json export (NULL where not requested).
 * Neither is truncated until both are open and known to be different
 * files, so a bad --json path or an alias never empties an existing
 * report.  0 on success; 1 with both closed after printing why. */
static int open_outputs(const char *out_path, FILE **report, FILE **json)
{
    *report = NULL;
    *json = NULL;
    if (out_path) {
        *report = open_output(out_path);
        if (!*report) return 1;
    }
    int failed = 0;
    if (json_path) {
        *json = open_output(json_path);
        failed = *json == NULL;
    }
    if (!failed && *report && *json && same_open_file(*report, *json) != 0) {
        fprintf(stderr, "Report and JSON export must be different files\n");
        failed = 1;
    }
    if (!failed && *report) failed = truncate_output(*report, out_path);
    if (!failed && *json) failed = truncate_output(*json, json_path);
    if (!failed) return 0;
    if (*report) fclose(*report);
    if (*json) fclose(*json);
    *report = NULL;
    *json = NULL;
    return 1;
}

/* Closes an output file; nonzero if any write failed.  A previous buffered
 * write may have failed even if close succeeds. */
static int close_output(FILE *f)
{
    int failed = ferror(f) != 0;
    if (fclose(f) != 0) failed = 1;
    return failed;
}

/* begin_test() plus room in the trial log for the test's trials. */
static void start_test(int n)
{
    begin_test(n);
    reserve_trials(test_streams(n));
}

static void run_selected_tests(FILE *report)
{
    if (test_selected(1)) { start_test(1); run_test1(report); }
    if (test_selected(2)) { start_test(2); run_test2(report); }
    if (test_selected(3)) { start_test(3); run_test3(report); }
    if (test_selected(4)) { start_test(4); run_test4(report); }
    if (test_selected(5)) { start_test(5); run_test5(report); }
    if (test_selected(6)) { start_test(6); run_test6(report); }
}

/* After a result mismatch (sanity_failed), says so where the numbers are
 * read, not only on stderr, so the report is not mistaken for a valid one. */
static void emit_sanity_note(FILE *report)
{
    if (!sanity_failed) return;
    static const char msg[] =
        "ERROR: a kernel result differed from its reference (see stderr); "
        "do not use these results.";
    printf("\n  %s\n\n", msg);
    if (report) fprintf(report, "**%s**\n\n", msg);
}

/* Closes the report, then writes and closes the export.  Nonzero if either
 * failed. */
static int finish_outputs(FILE *report, const char *out_path, FILE *json)
{
    int failed = 0;
    if (report) {
        if (close_output(report)) {
            fprintf(stderr, "Error writing report to %s\n", out_path);
            failed = 1;
        } else {
            printf("\nFormatted report written to %s\n", out_path);
        }
    }
    if (json) {
        write_export(json);
        if (close_output(json)) {
            fprintf(stderr, "Error writing JSON export to %s\n", json_path);
            failed = 1;
        } else {
            printf("JSON export written to %s\n", json_path);
        }
    }
    return failed;
}

int main(int argc, char **argv)
{
    const char *out_path = NULL;
    int rc = parse_args(argc, argv, &out_path);
    if (rc >= 0) return rc;

    FILE *report;
    FILE *json;
    if (open_outputs(out_path, &report, &json)) return 1;

    apply_affinity();
    perf_init();

    char tests_list[32];
    selected_tests_list(tests_list, sizeof(tests_list));
    print_banner(tests_list);
    if (report)
        write_report_header(report, tests_list);

    run_selected_tests(report);

    print_clock_summary(report);
    emit_sanity_note(report);

    printf("See README.md for how to interpret these results.\n");

    const int output_failed = finish_outputs(report, out_path, json);

    perf_close();

    return sanity_failed || output_failed ? 1 : 0;
}
