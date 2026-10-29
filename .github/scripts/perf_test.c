/* Deterministic tests of the real perf code; no hardware PMU is needed.
 * Reads return Linux-style cumulative scheduling times.
 *
 * The #defines below rename every read() and ioctl() in branch_bench.c and
 * the system headers it includes, so all of them reach the stubs, which
 * serve only perf counter reads (test_read asserts when it runs out of
 * samples).  If branch_bench.c starts calling read() or ioctl() for anything
 * else, e.g. sysfs, make the stubs pass those calls through. */
#define main benchmark_main
#define read test_read
#define ioctl test_ioctl
#include "../../branch_bench.c"
#undef main
#undef read
#undef ioctl
#include <assert.h>
#include <sys/wait.h>

static u64 samples[8][3 + CTR_COUNT];
static unsigned sample_count;
static unsigned sample_next;
static ssize_t sample_sizes[8];
static unsigned long ioctl_failure;

int test_ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    return request == ioctl_failure ? -1 : 0;
}

ssize_t test_read(int fd, void *buf, size_t count)
{
    (void)fd;
    assert(sample_next < sample_count);
    assert(count >= sizeof samples[0]);
    memcpy(buf, samples[sample_next], sizeof samples[0]);
    return sample_sizes[sample_next++];
}

static void sample(u64 enabled, u64 running, u64 value)
{
    assert(sample_count < sizeof samples / sizeof samples[0]);
    sample_sizes[sample_count] = sizeof samples[0];
    u64 *s = samples[sample_count++];
    s[0] = CTR_COUNT;
    s[1] = enabled;
    s[2] = running;
    for (int c = 0; c < CTR_COUNT; c++) s[3 + c] = value;
}

/* Logs ra and rb as trial 0 of a new pair of variants a and b. */
static int log_pair(int a, int b, const Result *ra, const Result *rb)
{
    const int p = new_pair(a, b);
    log_trial(a, p, 0, "x", ra);
    log_trial(b, p, 0, "x", rb);
    return p;
}

static void reset_log(int test)
{
    trial_log_len = 0;
    pair_count = 0;
    variant_count = 0;
    begin_test(test);
    reserve_trials(256);
}

static void invalid_reporting(Result good)
{
    Result bad = {0};
    bad.time_ms = 1;
    bad.counts[CTR_CYCLES] = 1000; /* Invalid data must never affect clock. */
    good.counts[CTR_CYCLES] = 50000;
    good.counts[CTR_MISSES] = 5000;
    FILE *f = tmpfile();
    assert(f);
    reset_log(1);
    print_pair_cost(f, log_pair(0, 1, &bad, &good));
    assert(ftell(f) == 0);
    const Work work = {100, "elem"};
    const ReportRow rows[] = {{"invalid", bad, NULL}, {"valid", good, NULL}};
    report_table(f, "test", NULL, rows, 2, &work);
    rewind(f);
    char output[2048] = {0};
    assert(fread(output, 1, sizeof(output) - 1, f) > 0);
    assert(strstr(output, "| invalid | 1.0 | – | – | – | – | – |"));
    assert(strstr(output, "| valid |"));
    assert(fclose(f) == 0);
    account_clock(&bad);
    assert(clock_cycles_sum == 0 && clock_ms_sum == 0);
    puts("ok   invalid counters excluded from report, miss cost and clock");
}

static void failed_measurements(void)
{
    static const int expected[9] = {
        PERF_START_FAILED, PERF_READ_FAILED, PERF_READ_FAILED, PERF_NOT_SCHEDULED,
        PERF_READ_FAILED, PERF_START_FAILED, PERF_START_FAILED, PERF_STOP_FAILED,
        PERF_TIMES_INVALID,
    };
    for (int scenario = 0; scenario < 9; scenario++) {
        sample_count = 0;
        sample_next = 0;
        ioctl_failure = 0;
        sample(100, 50, 0);
        sample(200, 150, 100);
        switch (scenario) {
        case 0: sample_sizes[0] -= sizeof(u64); break; /* baseline short read */
        case 1: sample_sizes[1] -= sizeof(u64); break; /* final short read */
        case 2: sample_sizes[1] = -1; break;
        case 3: samples[1][2] = 50; break; /* this trial never ran */
        case 4: samples[1][0] = 0; break;  /* wrong group size */
        case 5: ioctl_failure = PERF_EVENT_IOC_RESET; break;
        case 6: ioctl_failure = PERF_EVENT_IOC_ENABLE; break;
        case 7: ioctl_failure = PERF_EVENT_IOC_DISABLE; break;
        case 8: samples[1][1] = 99; break; /* invalid cumulative times */
        }
        Result r = {0};
        r.perf_status = PERF_OK; /* failure must also invalidate a reused result */
        perf_start();
        perf_stop(&r);
        assert(r.perf_status == expected[scenario]);
    }
    /* Later measurements recover. Successfully read zero counts are valid. */
    ioctl_failure = 0;
    sample_count = 0;
    sample_next = 0;
    sample(200, 150, 0);
    sample(300, 250, 0);
    Result zero = {0};
    zero.time_ms = 1;
    perf_start();
    perf_stop(&zero);
    assert(zero.perf_status == PERF_OK);
    account_clock(&zero);
    assert(clock_cycles_sum == 0 && clock_ms_sum == 1);
    clock_ms_sum = 0;
    puts("ok   failed reads, unscheduled trials, ioctl errors and recovery");
}

/* On ARM a counter can open only as its raw PMU event; the banner and report
 * must say which ones did. */
/* True if the file, read from its start, has a line equal to want. */
static int has_line(FILE *f, const char *want)
{
    char line[256];
    rewind(f);
    while (fgets(line, sizeof line, f))
        if (strcmp(line, want) == 0) return 1;
    return 0;
}

static void raw_event_reporting(void)
{
    perf_raw_mask = (1u << CTR_BRANCHES) | (1u << CTR_CYCLES);
    char list[96];
    raw_events_list(list, sizeof list);
    assert(strcmp(list, "branches (0x12), cycles (0x11)") == 0);

    FILE *report = tmpfile();
    assert(report);
    write_report_header(report, "");
    assert(has_line(report, "_Raw ARM PMU events (generic events failed): "
                            "branches (0x12), cycles (0x11)_\n"));
    fclose(report);

    /* print_banner() writes to stdout: point stdout at a file meanwhile. */
    FILE *banner = tmpfile();
    assert(banner);
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    int redirected = saved >= 0 && dup2(fileno(banner), STDOUT_FILENO) >= 0;
    assert(redirected);
    print_banner("");
    fflush(stdout);
    int restored = dup2(saved, STDOUT_FILENO) >= 0;
    close(saved);
    assert(restored);
    assert(has_line(banner, "  Raw PMU events:  branches (0x12), cycles (0x11) "
                            "(generic events failed)\n"));
    fclose(banner);

    perf_raw_mask = 0;
    puts("ok   raw PMU events named in banner and report");
}

/* A row's own Work overrides the table's for its misses-per-item column. */
static void per_row_work(void)
{
    Result r = {0};
    r.perf_status = PERF_OK;
    r.open_mask = perf_open_mask;
    r.time_ms = 1;
    r.counts[CTR_MISSES] = 500;
    const Work shared = {1000, "elem"}, own = {100, "block"};
    const ReportRow rows[] = {{"shared", r, NULL}, {"own", r, &own}};
    assert(row_work(&rows[0], &shared) == &shared);
    assert(row_work(&rows[1], &shared) == &own);
    FILE *f = tmpfile();
    assert(f);
    report_table(f, "test", NULL, rows, 2, &shared);
    rewind(f);
    char output[2048] = {0};
    assert(fread(output, 1, sizeof(output) - 1, f) > 0);
    assert(fclose(f) == 0);
    assert(strstr(output, " Misses/elem |\n"));
    assert(strstr(output, "| shared |") && strstr(output, " 0.500 |\n"));
    assert(strstr(output, "| own |") && strstr(output, " 5.000 |\n"));
    puts("ok   per-row work in report tables");
}

/* Test 5's pattern generator and checker, size rule and seeded fill. */
static void pattern_learnability(void)
{
    u8 p[DB_MAX_PERIOD];
    for (unsigned k = DB_MIN_ORDER; k <= DB_MAX_ORDER; k++) {
        assert(debruijn_prefer_one(k, p) == 1);
        assert(debruijn_valid(p, k) == 1);
        for (unsigned i = 0; i < k; i++) assert(p[i] == 0);
    }
    assert(debruijn_prefer_one(DB_MIN_ORDER - 1, p) == 0);
    assert(debruijn_prefer_one(DB_MAX_ORDER + 1, p) == 0);
    /* 16 ones in 32, but only two distinct 5-bit windows. */
    u8 alt[32];
    for (unsigned i = 0; i < 32; i++) alt[i] = (u8)(i & 1u);
    assert(debruijn_valid(alt, 5) == 0);

    assert(test5_size_ok(2048) && test5_size_ok(4194304));
    assert(!test5_size_ok(1000) && !test5_size_ok(1024) && !test5_size_ok(3072));

    static u8 x[65536], y[65536];
    const u64 before = rng_state;
    fill_random_bits(x, sizeof x, 0x1234ULL);
    assert(rng_state == before);
    fill_random_bits(y, sizeof y, 0x1234ULL);
    assert(memcmp(x, y, sizeof x) == 0);
    size_t ones = 0;
    for (size_t i = 0; i < sizeof x; i++) ones += x[i];
    assert(ones > sizeof x * 45 / 100 && ones < sizeof x * 55 / 100);
    puts("ok   Test 5 patterns and size rule");
}

static Result with_misses(u64 misses)
{
    Result r = {0};
    r.perf_status = PERF_OK;
    r.open_mask = perf_open_mask;
    r.counts[CTR_MISSES] = misses;
    return r;
}

/* Summary lists stay inside their buffer when an item does not fit. */
static void bounded_list(void)
{
    char buf[16 + 4];
    memset(buf, 'X', sizeof buf);
    /* volatile: GCC would see the deliberate truncation and warn. */
    volatile size_t cap = 16;
    size_t len = 0;
    buf[0] = '\0';
    list_append(buf, cap, &len, "1234567");
    list_append(buf, cap, &len, "abcdefgh");   /* truncated */
    list_append(buf, cap, &len, "more");       /* no room left */
    assert(strcmp(buf, "1234567, abcdef") == 0);
    for (size_t i = 16; i < sizeof buf; i++) assert(buf[i] == 'X');
    puts("ok   bounded summary lists");
}

/* Renders the Test 5 summary into buf. */
static void test5_summary_text(const ReportRow *rows, const Work *w, size_t len,
                               char *buf, size_t bufsz)
{
    FILE *f = tmpfile();
    assert(f);
    reset_log(5);
    const int pairs[2] = {
        log_pair(0, DB_ORDERS, &rows[0].r, &rows[DB_ORDERS].r),
        log_pair(DB_ORDERS - 1, DB_ORDERS, &rows[DB_ORDERS - 1].r, &rows[DB_ORDERS].r),
    };
    print_test5_summary(f, rows, w, len, pairs);
    rewind(f);
    memset(buf, 0, bufsz);
    size_t n = fread(buf, 1, bufsz - 1, f);
    (void)n;
    assert(fclose(f) == 0);
}

/* Test 5's summary: the low-miss list, "none", missing counts, and n/a
 * below 64K elements. */
static void test5_summary(void)
{
    ReportRow rows[DB_ORDERS + 1];
    const Work w = {1000, "elem"};
    char out[2048];
    for (int i = 0; i <= DB_ORDERS; i++) {
        rows[i].label = "x";
        rows[i].r = with_misses(i < 3 ? 40 : 400);
        rows[i].r.time_ms = 1;
        rows[i].work = NULL;
    }
    test5_summary_text(rows, &w, 65536, out, sizeof out);
    assert(strstr(out, "**Random vs period 32:**"));
    assert(strstr(out, "**Periods at ≤ 0.05 misses/elem:** 32, 64, 128\n"));
    for (int i = 0; i < 3; i++) {
        rows[i].r = with_misses(400);
        rows[i].r.time_ms = 1;
    }
    test5_summary_text(rows, &w, 65536, out, sizeof out);
    assert(strstr(out, "**Periods at ≤ 0.05 misses/elem:** none\n"));
    rows[2].r.perf_status = PERF_READ_FAILED;
    test5_summary_text(rows, &w, 65536, out, sizeof out);
    assert(!strstr(out, "Periods at"));
    test5_summary_text(rows, &w, 2048, out, sizeof out);
    assert(strstr(out, "_n/a: --size below 65536"));
    assert(!strstr(out, "Random vs") && !strstr(out, "Periods at"));
    puts("ok   Test 5 summary: list, none, missing counts, n/a");
}

/* Test 5 feeds its kernel the data its labels describe: whole de Bruijn
 * periods and the seeded random row. */
static void measured_data(void)
{
    const int saved_leader = perf_leader;
    const unsigned saved_reps = reps;
    perf_leader = -1;   /* MEASURE without counters */
    reps = 1;

    static u8 buf[4096], rbuf[4096], expect[4096];
    int pairs5[2];
    reset_log(5);
    const Result none = {0};
    ReportRow rows5[DB_ORDERS + 1];
    for (int i = 0; i <= DB_ORDERS; i++) {
        rows5[i].label = "x";
        rows5[i].r = none;
        rows5[i].work = NULL;
    }
    assert(measure_patterns(buf, rbuf, sizeof buf, rows5, pairs5) == 1);
    assert(pair_info[pairs5[0]].a == 0 && pair_info[pairs5[0]].b == DB_ORDERS);
    assert(pair_info[pairs5[1]].a == DB_ORDERS - 1 && pair_info[pairs5[1]].b == DB_ORDERS);
    for (int i = 0; i < DB_ORDERS; i++)
        assert(rows5[i].r.result == sizeof buf / 2);   /* half ones per period */
    fill_random_bits(expect, sizeof expect, test_seed(5, 0, 0));
    assert(rows5[DB_ORDERS].r.result == count_ones(expect, sizeof expect));

    perf_leader = saved_leader;
    reps = saved_reps;
    puts("ok   Test 5 rows measure the data their labels describe");
}

/* Each trial keeps its raw and scaled counts, scheduling times and the
 * counter set it was measured with; Miss % needs matching event semantics. */
static void counter_metadata(void)
{
    sample_count = 0;
    sample_next = 0;
    ioctl_failure = 0;
    sample(0, 0, 0);
    sample(200, 100, 50);
    Result r = {0};
    perf_start();
    perf_stop(&r);
    assert(r.perf_status == PERF_OK);
    for (int c = 0; c < CTR_COUNT; c++) {
        assert(r.raw_counts[c] == 50);
        assert(r.counts[c] == 100);
    }
    assert(r.enabled_ns == 200 && r.running_ns == 100);
    assert(r.open_mask == perf_open_mask && r.raw_mask == perf_raw_mask);

    const int saved_leader = perf_leader;
    perf_leader = -1;
    r.perf_status = PERF_OK;
    perf_stop(&r);
    assert(r.perf_status == PERF_NONE);
    perf_leader = saved_leader;

    assert(counter_semantics(CTR_BRANCHES, 0, ARCH_X86) == SEM_RETIRED);
    assert(counter_semantics(CTR_MISSES, 0, ARCH_X86) == SEM_RETIRED);
    assert(counter_semantics(CTR_BRANCHES, 0, ARCH_ARM) == SEM_RETIRED);
    assert(counter_semantics(CTR_MISSES, 0, ARCH_ARM) == SEM_SPECULATIVE);
    assert(counter_semantics(CTR_BRANCHES, 1, ARCH_ARM) == SEM_SPECULATIVE);
    assert(counter_semantics(CTR_MISSES, 1, ARCH_ARM) == SEM_SPECULATIVE);
    assert(counter_semantics(CTR_MISSES, 0, ARCH_OTHER) == SEM_UNKNOWN);
    assert(counter_semantics(CTR_CYCLES, 0, ARCH_OTHER) == SEM_CYCLES);

    Result m = {0};
    m.perf_status = PERF_OK;
    m.open_mask = perf_open_mask;
    m.counts[CTR_BRANCHES] = 100;
    m.counts[CTR_MISSES] = 10;
    if (host_arch() == ARCH_X86) assert(miss_rate_valid(&m));
    if (host_arch() == ARCH_ARM) {
        assert(!miss_rate_valid(&m));                  /* retired vs speculative */
        m.raw_mask = (1u << CTR_BRANCHES) | (1u << CTR_MISSES);
        assert(miss_rate_valid(&m));                   /* both speculative */
        m.raw_mask = 1u << CTR_MISSES;
        assert(!miss_rate_valid(&m));
    }
    m.counts[CTR_BRANCHES] = 0;
    assert(!miss_rate_valid(&m));
    puts("ok   per-result counter metadata and semantics");
}

/* Records which stub ran, so the measurement order can be checked. */
static char call_order[16];
static int call_n;

__attribute__((noinline)) static u64 stub_a(int x) { call_order[call_n++] = 'A'; return (u64)x + 1; }
__attribute__((noinline)) static u64 stub_b(int x) { call_order[call_n++] = 'B'; return (u64)x + 2; }

/* Each measured trial follows one untimed training run of the same variant,
 * and pairs alternate AB, BA, ... */
static void pair_schedule(void)
{
    const int saved_leader = perf_leader, saved_trials = trials;
    perf_leader = -1;
    trials = 2;
    reset_log(1);
    Result ra = {0}, rb = {0};
    int p;
    MEASURE_PAIR(ra, 0, stub_a(0), rb, 1, stub_b(0), p);
    assert(call_n == 8 && memcmp(call_order, "AABBBBAA", 8) == 0);
    assert(trial_log_len == 4);
    const char order[] = "ABBA";
    for (int i = 0; i < 4; i++) {
        const TrialRecord *t = &trial_log[i];
        assert(t->pair == p && t->test == 1 && t->seq == i);
        assert(t->variant == (order[i] == 'A' ? 0 : 1));
        assert(t->trial == i / 2);
        assert(strcmp(t->call, order[i] == 'A' ? "stub_a(0)" : "stub_b(0)") == 0);
    }
    assert(ra.result == 1 && rb.result == 2);
    assert(pair_info[p].a == 0 && pair_info[p].b == 1 && pair_info[p].test == 1);
    perf_leader = saved_leader;
    trials = saved_trials;
    puts("ok   balanced AB/BA schedule with training runs");
}

static Result counted(u64 cycles, u64 misses, double ms)
{
    Result r = {0};
    r.perf_status = PERF_OK;
    r.open_mask = perf_open_mask;
    r.counts[CTR_CYCLES] = cycles;
    r.counts[CTR_MISSES] = misses;
    r.time_ms = ms;
    return r;
}

/* Logs three trials per variant as one pair: a at 100 cycles, 0 misses,
 * 1 ms; b at cycles_b[k] and misses_b, 2 ms.  Trial `bad` of b (or none,
 * -1) fails its read; `all_bad` fails every trial. */
static int log_cost_pair(const u64 cycles_b[3], u64 misses_b, int bad, int all_bad)
{
    reset_log(1);
    const int p = new_pair(0, 1);
    for (int k = 0; k < 3; k++) {
        Result a = counted(100, 0, 1), b = counted(cycles_b[k], misses_b, 2);
        if (k == bad || all_bad) b.perf_status = PERF_READ_FAILED;
        if (all_bad) a.perf_status = PERF_READ_FAILED;
        log_trial(0, p, k, "x", &a);
        log_trial(1, p, k, "x", &b);
    }
    return p;
}

static void pair_text(int pair, int ratio, char *buf, size_t bufsz)
{
    FILE *f = tmpfile();
    assert(f);
    if (ratio) emit_pair_ratio(f, "", "Slowdown", pair, "");
    print_pair_cost(f, pair);
    rewind(f);
    memset(buf, 0, bufsz);
    size_t n = fread(buf, 1, bufsz - 1, f);
    (void)n;
    assert(fclose(f) == 0);
}

/* Effective cost: summed signed differences over valid pairs, a partial
 * suffix, the inconclusive form, and nothing without counts. */
static void paired_cost(void)
{
    static const u64 cycles_b[3] = {400, 50, 700};
    char out[512];
    int p = log_cost_pair(cycles_b, 1000, -1, 0);
    PairStats s = pair_stats(p);
    assert(s.pairs == 3 && s.valid == 3);
    assert(s.delta[CTR_CYCLES] == 850 && s.delta[CTR_MISSES] == 3000);
    assert(s.ms_a == 3 && s.ms_b == 6);
    pair_text(p, 0, out, sizeof out);
    assert(strcmp(out, "**Effective extra cycles per additional miss:** 0.3 cycles\n\n") == 0);
    assert(pair_info[p].cost_shown);

    p = log_cost_pair(cycles_b, 1000, 2, 0);
    pair_text(p, 0, out, sizeof out);
    assert(strcmp(out, "**Effective extra cycles per additional miss:** 0.1 cycles "
                       "(2 of 3 pairs)\n\n") == 0);

    p = log_cost_pair(cycles_b, 10, -1, 0);
    pair_text(p, 0, out, sizeof out);
    assert(strcmp(out, "**Effective extra cycles per additional miss:** inconclusive "
                       "(Δcycles +850, Δmisses +30 over 3 valid pair(s))\n\n") == 0);
    assert(pair_info[p].cost_shown);   /* the line was printed */

    p = log_cost_pair(cycles_b, 1000, -1, 1);
    pair_text(p, 1, out, sizeof out);
    assert(strcmp(out, "**Slowdown:** 2.00×\n\n") == 0);
    assert(strcmp(pair_info[p].ratio_label, "Slowdown") == 0);

    p = log_cost_pair(cycles_b, 1000, -1, 0);
    for (int i = 0; i < trial_log_len; i++)
        trial_log[i].r.open_mask &= ~(1u << CTR_CYCLES);
    pair_text(p, 0, out, sizeof out);
    assert(out[0] == '\0');
    puts("ok   paired effective cost: signed, partial, inconclusive, unavailable");
}

/* JSON strings escape quotes, backslashes and control bytes and pass UTF-8
 * through; -o and --json must never name one file. */
static void json_escaping(void)
{
    FILE *f = tmpfile();
    assert(f);
    json_string(f, "a\"b\\c\n\x01é");
    rewind(f);
    char out[64] = {0};
    size_t n = fread(out, 1, sizeof out - 1, f);
    (void)n;
    assert(fclose(f) == 0);
    assert(strcmp(out, "\"a\\\"b\\\\c\\n\\u0001é\"") == 0);
    FILE *a = tmpfile(), *b = tmpfile();
    assert(a && b);
    assert(same_open_file(a, a) == 1);
    assert(same_open_file(a, b) == 0);
    assert(fclose(a) == 0 && fclose(b) == 0);
    assert(strcmp(perf_status_name(PERF_NOT_SCHEDULED), "not_scheduled") == 0);
    assert(strcmp(perf_status_name(PERF_NONE), "none") == 0);
    assert(strcmp(semantics_name(SEM_SPECULATIVE), "speculative") == 0);
    puts("ok   JSON string escaping and same-file detection");
}

static void seeds(void)
{
    const u64 saved = base_seed;
    base_seed = 1;
    assert(test_seed(3, 0, 0) == test_seed(3, 0, 0));            /* deterministic */
    for (int a = 1; a <= NUM_TESTS; a++)
        for (int b = a + 1; b <= NUM_TESTS; b++)
            assert(test_seed(a, 0, 0) != test_seed(b, 0, 0));    /* per test */
    assert(test_seed(6, 0, 0) != test_seed(6, 0, 1));            /* per index */
    assert(test_seed(2, 0, 0) != test_seed(2, 1, 0));            /* per config */
    const u64 s1 = test_seed(3, 0, 0);
    base_seed = 2;
    assert(test_seed(3, 0, 0) != s1);                            /* base matters */
    base_seed = 0;
    for (int t = 1; t <= NUM_TESTS; t++) assert(test_seed(t, 0, 0) != 0);
    seed_test(4); rng64(); rng64();
    seed_test(4);
    assert(rng_state == test_seed(4, 0, 0));                     /* prior draws don't leak */
    for (int i = 0; i < 1000; i++) assert(rng_below(3) < 3);
    assert(rng_below(1) == 0);
    u8 a[257]; unsigned before[256] = {0}, after[256] = {0};
    for (int i = 0; i < 257; i++) before[a[i] = (u8)(i * 7)]++;
    shuffle_u8(a, 257);
    for (int i = 0; i < 257; i++) after[a[i]]++;
    assert(memcmp(before, after, sizeof before) == 0);           /* same multiset */
    u64 v;
    assert(parse_seed("0", &v) && v == 0);
    assert(parse_seed("0x10", &v) && v == 16);
    assert(parse_seed("0X1f", &v) && v == 31);
    assert(parse_seed("010", &v) && v == 10);                    /* decimal, not octal */
    assert(parse_seed("08", &v) && v == 8);
    assert(!parse_seed("0x", &v) && !parse_seed("0xg", &v));
    assert(parse_seed("18446744073709551615", &v) && v == UINT64_MAX);
    assert(!parse_seed("18446744073709551616", &v));
    assert(!parse_seed("-1", &v) && !parse_seed("", &v) && !parse_seed("5x", &v));
    base_seed = saved;
    puts("ok   per-test seeds, unbiased draws, seed parsing");
}

/* The data-branch kernels (assembly where available) on edge cases: the
 * threshold boundary, one element, no elements and no passes. */
static void kernels(void)
{
    static const u8 edge[] = { 0, 128, 129, 255, 1, 127 };
    assert(sum_threshold(edge, sizeof edge, 1) == 129 + 255);
    assert(sum_threshold(edge, sizeof edge, 3) == 3 * (129 + 255));
    assert(sum_threshold(edge + 3, 1, 2) == 2 * 255);
    assert(sum_threshold(edge + 1, 1, 5) == 0);    /* 128 is not > 128 */
    assert(sum_threshold(edge, 0, 4) == 0);
    assert(sum_threshold(edge, sizeof edge, 0) == 0);
    static const u8 dec[] = { 0, 1, 1, 0, 1 };
    assert(count_decisions(dec, sizeof dec, 1) == 3);
    assert(count_decisions(dec, sizeof dec, 4) == 12);
    assert(count_decisions(dec + 1, 1, 7) == 7);
    assert(count_decisions(dec, 0, 7) == 0);
    assert(count_decisions(dec, sizeof dec, 0) == 0);
    assert(KERNEL_ASM == (strcmp(KERNEL_BACKEND, "c") != 0));
}

/* Tests 1 and 4: one multiset in two orders, and a reference sum that does
 * not use the kernel's comparison loop. */
static void threshold_inputs(void)
{
    static const u8 edge[] = { 0, 128, 129, 255, 255 };
    u64 above = 0;
    assert(threshold_reference(edge, sizeof edge, 2, &above) == 2 * (129 + 255 + 255));
    assert(above == 3);
    static u8 s[4096];
    static u8 h[4096];
    rng_state = test_seed(1, 0, 0);
    prepare_threshold(s, h, sizeof s);
    for (size_t i = 1; i < sizeof s; i++) assert(s[i - 1] <= s[i]);
    u64 as = 0;
    u64 ah = 0;
    assert(threshold_reference(s, sizeof s, 1, &as) == threshold_reference(h, sizeof h, 1, &ah));
    assert(as == ah);
    assert(fnv1a64(s, sizeof s) != fnv1a64(h, sizeof h));
    assert(sum_threshold(h, sizeof h, 3) == threshold_reference(h, sizeof h, 3, &ah));
    sanity_failed = 0;
    check_result("match", 5, 5);
    assert(!sanity_failed);
    check_result("mismatch", 5, 6);
    assert(sanity_failed);
    sanity_failed = 0;
}

/* Test 2: every bias pattern has exactly its word's ones and period, and
 * the size rule accepts only whole construction periods. */
static void bias_inputs(void)
{
    u8 buf[64];
    for (int b = 0; b < T2_BIASES; b++) {
        const size_t wl = strlen(t2_words[b]);
        assert(T2_PERIOD % wl == 0);
        fill_periodic(buf, sizeof buf, t2_words[b]);
        assert(fundamental_period(buf, sizeof buf) == wl);
        size_t ones = 0;
        for (size_t i = 0; i < wl; i++) ones += t2_words[b][i] == '1';
        assert(count_ones(buf, sizeof buf) == sizeof buf / wl * ones);
    }
    fill_clustered(buf, sizeof buf, 12);
    assert(count_ones(buf, sizeof buf) == 12 && buf[11] == 1 && buf[12] == 0);
    assert(fundamental_period(buf, sizeof buf) == sizeof buf);
    assert(test2_size_ok(16) && test2_size_ok(4096));
    assert(!test2_size_ok(8) && !test2_size_ok(1000));
}

/* Test 3: distinct nonzero leaf constants, equal target counts in every
 * sequence, and a call-free reference that depends on the call order. */
static void dispatch_inputs(void)
{
    for (unsigned a = 0; a < NUM_FUNCS; a++) {
        assert(LEAF_CONST(a) != 0);
        for (unsigned b = a + 1; b < NUM_FUNCS; b++) assert(LEAF_CONST(a) != LEAF_CONST(b));
    }
    u8 idx[256];
    for (unsigned t = 1; t <= NUM_FUNCS; t <<= 1) {
        fill_cyclic(idx, sizeof idx, t);
        assert(equal_target_counts(idx, sizeof idx, t));
        assert(dispatch_indexed(idx, sizeof idx) == dispatch_reference(idx, sizeof idx));
    }
    idx[0] = 1;                                   /* one call moved to another target */
    assert(!equal_target_counts(idx, sizeof idx, NUM_FUNCS));
    fill_cyclic(idx, sizeof idx, 4);
    idx[0] = 4;                                   /* a target outside the count */
    assert(!equal_target_counts(idx, sizeof idx, 4));
    fill_cyclic(idx, sizeof idx, 4);
    const u64 cyc = dispatch_reference(idx, sizeof idx);
    const u8 tmp = idx[0];
    idx[0] = idx[1];
    idx[1] = tmp;
    assert(dispatch_reference(idx, sizeof idx) != cyc);   /* order matters */
    assert(test3_size_ok(64) && test3_size_ok(4096));
    assert(!test3_size_ok(32) && !test3_size_ok(1000));
}

/* Runs fn in a child process and returns its exit status (-1 if it did not
 * exit normally), for the internal-error paths that call exit(1). */
static int exit_status(void (*fn)(void))
{
    fflush(NULL);
    const pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        if (!freopen("/dev/null", "w", stderr)) _exit(2);
        fn();
        _exit(0);
    }
    int st = 0;
    assert(waitpid(pid, &st, 0) == pid);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static const u8 input_a[4] = { 1, 0, 1, 1 };
static const u8 input_b[4] = { 1, 1, 0, 1 };

static void log_input_twice_same(void)
{
    log_input(0, input_a, 4, 1, 3);
    log_input(0, input_a, 4, 1, 3);
}
static void log_input_twice_other_buffer(void)
{
    log_input(0, input_a, 4, 1, 3);
    log_input(0, input_b, 4, 1, 3);
}
static void log_input_no_cond_twice(void)
{
    log_input(0, input_a, 4, 0, 0);
    log_input(0, input_a, 4, 0, 7);   /* cond_true is ignored without has_cond */
}
static void log_input_too_many_true(void)
{
    log_input(0, input_a, 4, 1, 5);
}
static void log_param_twice_same(void)
{
    log_param(0, "N", 4);
    log_param(0, "N", 4);
}
static void log_param_conflict(void)
{
    log_param(0, "N", 4);
    log_param(0, "N", 5);
}
static void log_variant_two_labels(void)
{
    const Work w = { 4, "elem" };
    log_variant(0, "first", &w);
    log_variant(0, "second", &w);
}

/* A variant's input and row can be registered again only unchanged; a row
 * whose result differs from its reference is marked in the report. */
static void variant_guards(void)
{
    reset_log(2);
    assert(exit_status(log_input_twice_same) == 0);
    assert(exit_status(log_input_twice_other_buffer) == 1);
    assert(exit_status(log_input_too_many_true) == 1);
    assert(exit_status(log_input_no_cond_twice) == 0);
    assert(exit_status(log_variant_two_labels) == 1);
    assert(exit_status(log_param_twice_same) == 0);
    assert(exit_status(log_param_conflict) == 1);

    char buf[256] = "";
    FILE *f = tmpfile();
    assert(f);
    sanity_failed = 0;
    emit_sanity_note(f);
    assert(ftell(f) == 0);
    sanity_failed = 1;
    emit_sanity_note(f);
    rewind(f);
    assert(fgets(buf, sizeof buf, f) && strstr(buf, "**ERROR:"));
    fclose(f);
    sanity_failed = 0;
}

/* The trial log holds what was reserved for it, far beyond the old fixed
 * 8192 records. */
static void trial_log_grows(void)
{
    const int saved = trials;
    trials = 1;
    reset_log(5);
    reserve_trials(40000);
    const Result r = {0};
    for (int i = 0; i < 40000; i++) log_trial(0, -1, i, "x", &r);
    assert(trial_log_len == 40000);
    trials = saved;
    reset_log(1);
    puts("ok   trial log grows to the reserved size");
}

/* Both loop-exit kernels against their closed forms, including no loops,
 * a zero count and a single iteration. */
static void loop_inputs(void)
{
    static const unsigned c[] = { 0, 5, 1, 1, 160, 512 };
    static const u64 loops[] = { 5, 0, 1, 7, 3, 2 };
    for (int i = 0; i < 6; i++) {
        const u64 n = c[i];
        assert(loop_exit(c[i], loops[i]) == loops[i] * n);
        assert(loop_exit_body2(c[i], loops[i]) == loops[i] * (n * (n + 1) / 2 + n));
    }
    puts("ok   loop-exit kernels match their closed forms");
}

/* Each pattern kernel on blocks of N sites: no blocks, no passes, one
 * block and three blocks twice, against a direct count.  The byte after
 * the last block is 1 and must never be counted. */
#define PATTERN_CHECK(N)                                                    \
    do {                                                                    \
        static u8 d[3 * (N) + 1];                                           \
        u64 ones = 0;                                                       \
        for (size_t i = 0; i < 3 * (size_t)(N); i++) {                      \
            d[i] = (u8)((i * 7 + i / (N)) % 3 == 0);                        \
            ones += d[i];                                                   \
        }                                                                   \
        d[3 * (N)] = 1;                                                     \
        u64 first = 0;                                                      \
        for (size_t i = 0; i < (size_t)(N); i++) first += d[i];             \
        assert(pattern_sites_##N(d, 0, 5) == 0);                            \
        assert(pattern_sites_##N(d, 3, 0) == 0);                            \
        assert(pattern_sites_##N(d, 1, 1) == first);                        \
        assert(pattern_sites_##N(d, 3, 2) == 2 * ones);                     \
    } while (0)

static void pattern_kernels(void)
{
    PATTERN_CHECK(1);
#if KERNEL_ASM
    PATTERN_CHECK(4);
    PATTERN_CHECK(16);
    PATTERN_CHECK(64);
    PATTERN_CHECK(256);
    PATTERN_CHECK(512);
#endif
    assert(pattern_kernel_available(1));
    assert(pattern_kernel_available(512) == KERNEL_ASM);
    puts("ok   pattern kernels count every site of every block, nothing past the end");
}

/* Fills a Test 5 cell into fresh buffers and validates it. */
static int t5_check(const T5Cell *c, u8 **data, T5Stats *st)
{
    *data = malloc(c->blocks * c->n);
    u8 *streams = malloc((size_t)c->n * c->l);
    assert(*data && streams);
    t5_fill(c, *data, streams);
    free(streams);
    return t5_validate(c, *data, st);
}

static void t5_bad_blocks(void)
{
    const T5Cell c = { 4, 8, 12, T5_RANDOM_WORDS };   /* 12 blocks: not whole periods */
    u8 data[48], streams[32];
    t5_fill(&c, data, streams);
}

/* Test 5's inputs: every site's stream is periodic and checked from the
 * data alone; controls have their exact counts; seeds decide the words. */
static void t5_inputs(void)
{
    T5Stats st;
    u8 *data;
    const T5Cell words = { 4, 8, 256, T5_RANDOM_WORDS };
    assert(t5_check(&words, &data, &st) == 1);
    assert(st.ones == count_ones(data, 256 * 4));
    data[17] ^= 1;                                   /* breaks site 1's period */
    assert(t5_validate(&words, data, &st) == 0);
    const u64 h1 = fnv1a64(data, 256 * 4);
    free(data);
    assert(t5_check(&words, &data, &st) == 1);
    data[17] ^= 1;
    assert(fnv1a64(data, 256 * 4) == h1);            /* same seed, same words */
    data[17] ^= 1;
    const u64 h_same = fnv1a64(data, 256 * 4);
    free(data);
    const u64 saved_seed = base_seed;
    base_seed ^= 0x5555;
    assert(t5_check(&words, &data, &st) == 1);
    assert(fnv1a64(data, 256 * 4) != h_same);        /* another base seed, other words */
    free(data);
    base_seed = saved_seed;

    const T5Cell crowded = { 512, 8, 256, T5_RANDOM_WORDS };
    assert(t5_check(&crowded, &data, &st) == 1);
    assert(st.distinct <= 256);                      /* only 256 8-bit words exist */
    free(data);

    const T5Cell db = { 1, 32, 2048, T5_DEBRUIJN };
    assert(t5_check(&db, &data, &st) == 1);
    assert(st.ones == 2048 / 2 && st.distinct == 1 && st.short_period == 0);
    free(data);

    const T5Cell ref = { 1, 65536, 65536, T5_BALANCED_REF };
    assert(t5_check(&ref, &data, &st) == 1);
    assert(st.ones == 32768);
    free(data);

    const T5Cell zero = { 1, 2, 64, T5_CONSTANT0 };
    assert(t5_check(&zero, &data, &st) == 1);
    assert(st.ones == 0 && st.short_period == 1);    /* all zeros: period 1 < 2 */
    free(data);
    const T5Cell alt = { 1, 2, 64, T5_ALTERNATING };
    assert(t5_check(&alt, &data, &st) == 1);
    assert(st.ones == 32 && st.short_period == 0);
    free(data);

    const T5Cell empty = { 4, 8, 0, T5_RANDOM_WORDS };   /* no period to rebuild */
    u8 none[1] = {0};
    assert(t5_validate(&empty, none, &st) == 0);
    assert(exit_status(t5_bad_blocks) == 1);
    puts("ok   Test 5 inputs: periodic sites, independent validator, controls, seeds");
}

int main(void)
{
    perf_leader = 42;
    perf_members = CTR_COUNT;
    perf_open_mask = (1u << CTR_COUNT) - 1;
    for (int c = 0; c < CTR_COUNT; c++) perf_slot[c] = c;

    /* Lifetime scaling incorrectly inflates the second trial to 133. */
    sample(0, 0, 0);
    sample(100, 50, 50);
    sample(100, 50, 0);
    sample(200, 150, 100);
    Result first = {0}, second = {0};
    perf_start();
    perf_stop(&first);
    perf_start();
    perf_stop(&second);
    for (int c = 0; c < CTR_COUNT; c++) {
        assert(first.counts[c] == 100);
        assert(second.counts[c] == 100);
    }
    assert(sample_next == sample_count);
    puts("ok   per-trial perf scaling with changing multiplexing");
    invalid_reporting(first);
    failed_measurements();
    raw_event_reporting();
    per_row_work();
    pattern_learnability();
    bounded_list();
    test5_summary();
    measured_data();
    seeds();
    counter_metadata();
    pair_schedule();
    paired_cost();
    json_escaping();
    kernels();
    threshold_inputs();
    bias_inputs();
    dispatch_inputs();
    variant_guards();
    trial_log_grows();
    loop_inputs();
    pattern_kernels();
    t5_inputs();
    return 0;
}
