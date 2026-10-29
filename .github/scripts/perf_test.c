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
    return 0;
}
