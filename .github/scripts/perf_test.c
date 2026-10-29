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

static void invalid_reporting(Result good)
{
    Result bad = {0};
    bad.time_ms = 1;
    bad.counts[CTR_CYCLES] = 1000; /* Invalid data must never affect clock. */
    good.counts[CTR_CYCLES] = 50000;
    good.counts[CTR_MISSES] = 5000;
    FILE *f = tmpfile();
    assert(f);
    print_miss_cost(f, &bad, &good);
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
        r.counts_valid = 1; /* failure must also invalidate a reused result */
        perf_start();
        perf_stop(&r);
        assert(!r.counts_valid);
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
    assert(zero.counts_valid);
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
    r.counts_valid = 1;
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
    r.counts_valid = 1;
    r.counts[CTR_MISSES] = misses;
    return r;
}

/* Test 6's kernel, block layout, size rule and correlation percentage. */
static void correlation_distance(void)
{
    const u8 a[3] = {1, 0, 1}, b[3] = {1, 0, 1}, fill[4] = {1, 1, 1, 1};
    assert(correlate_distance(a, b, fill, 3, 4, 2) == 64);
    assert(correlate_distance(a, b, fill, 3, 0, 2) == 16);

    static const struct { unsigned d; size_t blocks; unsigned passes; } want[] = {
        {4, 4194304, 10}, {32, 4194304, 1}, {64, 4067203, 1}, {1024, 261632, 1},
    };
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        size_t blocks;
        unsigned passes;
        d6_layout(64ULL << 22, (size_t)1 << 22, want[i].d, &blocks, &passes);
        assert(blocks == want[i].blocks && passes == want[i].passes);
    }

    assert(test6_size_ok(64, 1026) && test6_size_ok(65536, 2));
    assert(!test6_size_ok(64, 1025) && !test6_size_ok(63, 100000));
    assert(!test6_size_ok(1000, 1) && !test6_size_ok(32, 100000));

    Result corr = with_misses(100), ref = with_misses(350);
    assert(correlation_used_pct(&corr, &ref, 1000) == 50.0);
    corr = with_misses(1100); ref = with_misses(1000);   /* noise: no u64 underflow */
    assert(correlation_used_pct(&corr, &ref, 1000) == 0.0);
    corr = with_misses(0); ref = with_misses(2000);
    assert(correlation_used_pct(&corr, &ref, 1000) == 100.0);
    puts("ok   Test 6 kernel, layout and correlation");
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

static Result with_misses_cycles(u64 misses, u64 cycles)
{
    Result r = with_misses(misses);
    r.counts[CTR_CYCLES] = cycles;
    return r;
}

/* Renders the Test 6 summary for rows and points into buf. */
static void test6_summary_text(const ReportRow *rows, const D6Point *pts,
                               char *buf, size_t bufsz)
{
    FILE *f = tmpfile();
    assert(f);
    print_test6_summary(f, rows, pts);
    rewind(f);
    memset(buf, 0, bufsz);
    size_t n = fread(buf, 1, bufsz - 1, f);
    (void)n;
    assert(fclose(f) == 0);
}

static void set_points(D6Point pts[D6_DISTS], size_t blocks, unsigned passes)
{
    for (int i = 0; i < D6_DISTS; i++) {
        pts[i].blocks = blocks;
        pts[i].passes = passes;
        pts[i].work.count = (u64)blocks * passes;
        pts[i].work.unit = "block";
    }
}

/* Test 6's summary: n/a below 64K blocks per pass, multi-pass figures, the
 * 90% boundary, and no cost per miss from a short d = 4 sample. */
static void test6_summary(void)
{
    ReportRow rows[2 * D6_DISTS];
    D6Point pts[D6_DISTS];
    char out[2048];
    for (int i = 0; i < 2 * D6_DISTS; i++) {
        rows[i].label = "x";
        rows[i].r = with_misses_cycles(0, 1000000);
        rows[i].work = NULL;
    }
    /* d = 4 has 65536 blocks over 2 passes (50%); the rest are too short. */
    set_points(pts, 4096, 1);
    pts[0].blocks = 65536; pts[0].passes = 2; pts[0].work.count = 131072;
    rows[0].r = with_misses_cycles(32768, 1000000);
    rows[1].r = with_misses_cycles(65536, 2000000);
    test6_summary_text(rows, pts, out, sizeof out);
    assert(strstr(out, "**Correlation used:** d=4 50%, d=8 n/a, "));
    assert(strstr(out, "d=1024 n/a\n"));
    assert(strstr(out, "**Largest d with ≥ 90%:** none\n"));
    assert(strstr(out, "_n/a: fewer than 65536 blocks per pass"));
    assert(strstr(out, "**Cost per miss:**"));

    /* Nothing evaluated: n/a, not "none", and no cost from d = 4. */
    pts[0] = pts[1];
    test6_summary_text(rows, pts, out, sizeof out);
    assert(strstr(out, "**Largest d with ≥ 90%:** n/a\n"));
    assert(!strstr(out, "Cost per miss"));

    /* Every distance evaluated; exactly 90% counts, 89.9% does not. */
    set_points(pts, 100000, 1);
    for (int i = 0; i < D6_DISTS; i++) {
        rows[2 * i].r = with_misses(i == D6_DISTS - 1 ? 5050 : 5000);
        rows[2 * i + 1].r = with_misses(50000);
    }
    test6_summary_text(rows, pts, out, sizeof out);
    assert(strstr(out, "d=512 90%, d=1024 90%\n"));
    assert(strstr(out, "**Largest d with ≥ 90%:** 512\n"));
    assert(!strstr(out, "_n/a:"));
    puts("ok   Test 6 summary: n/a, passes, 90% boundary, cost gate");
}

/* Renders the Test 5 summary into buf. */
static void test5_summary_text(const ReportRow *rows, const Work *w, size_t len,
                               char *buf, size_t bufsz)
{
    FILE *f = tmpfile();
    assert(f);
    print_test5_summary(f, rows, w, len);
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
    rows[2].r.counts_valid = 0;
    test5_summary_text(rows, &w, 65536, out, sizeof out);
    assert(!strstr(out, "Periods at"));
    test5_summary_text(rows, &w, 2048, out, sizeof out);
    assert(strstr(out, "_n/a: --size below 65536"));
    assert(!strstr(out, "Random vs") && !strstr(out, "Periods at"));
    puts("ok   Test 5 summary: list, none, missing counts, n/a");
}

static u64 count_ones(const u8 *p, size_t n)
{
    u64 ones = 0;
    for (size_t i = 0; i < n; i++) ones += p[i];
    return ones;
}

/* Tests 5 and 6 feed their kernels the data their labels describe:
 * whole de Bruijn periods and the seeded random row; B a copy of A in each
 * correlated row and independently seeded in each reference row. */
static void measured_data(void)
{
    const int saved_leader = perf_leader;
    const unsigned saved_reps = reps;
    perf_leader = -1;   /* MEASURE without counters */
    reps = 1;

    static u8 buf[4096], expect[4096];
    const Result none = {0};
    ReportRow rows5[DB_ORDERS + 1];
    for (int i = 0; i <= DB_ORDERS; i++) {
        rows5[i].label = "x";
        rows5[i].r = none;
        rows5[i].work = NULL;
    }
    assert(measure_patterns(buf, sizeof buf, rows5) == 1);
    for (int i = 0; i < DB_ORDERS; i++)
        assert(rows5[i].r.result == sizeof buf / 2);   /* half ones per period */
    fill_random_bits(expect, sizeof expect, DB_RANDOM_SEED);
    assert(rows5[DB_ORDERS].r.result == count_ones(expect, sizeof expect));

    static u8 a[1024], b[1024], ref_b[1024];
    const u64 total = 1024ULL * 1026;
    ReportRow rows6[2 * D6_DISTS];
    D6Point pts[D6_DISTS];
    for (int i = 0; i < 2 * D6_DISTS; i++) {
        rows6[i].label = "x";
        rows6[i].r = none;
        rows6[i].work = NULL;
    }
    assert(D6_SEED_A != D6_SEED_B);
    fill_random_bits(a, sizeof a, D6_SEED_A);
    measure_distances(a, b, sizeof a, total, rows6, pts);
    for (int i = 0; i < D6_DISTS; i++) {
        const unsigned d = (unsigned)D6_MIN_DIST << i;
        size_t blocks;
        unsigned passes;
        d6_layout(total, sizeof a, d, &blocks, &passes);
        assert(pts[i].blocks == blocks && pts[i].passes == passes);
        assert(pts[i].work.count == (u64)blocks * passes);
        assert(rows6[2 * i].work == &pts[i].work && rows6[2 * i + 1].work == &pts[i].work);
        /* Per block: A adds 1 and B 3 when set, each filler 2. */
        fill_random_bits(ref_b, blocks, D6_SEED_B);
        const u64 ones_a = count_ones(a, blocks);
        const u64 fillers = 2ULL * d * blocks;
        assert(rows6[2 * i].r.result == passes * (fillers + 4 * ones_a));
        assert(rows6[2 * i + 1].r.result ==
               passes * (fillers + ones_a + 3 * count_ones(ref_b, blocks)));
    }

    perf_leader = saved_leader;
    reps = saved_reps;
    puts("ok   Test 5 and 6 rows measure the data their labels describe");
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
    correlation_distance();
    bounded_list();
    test6_summary();
    test5_summary();
    measured_data();
    return 0;
}
