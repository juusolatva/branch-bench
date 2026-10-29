/*
 * fake_perf.c — LD_PRELOAD shim that fakes perf counters and cpufreq sysfs
 *
 * CI runners are VMs without a hardware PMU or cpufreq, so the code paths
 * that use them never run there.  This shim lets the coverage build reach
 * them anyway:
 *
 *  - perf_event_open(2) through syscall() returns a /dev/null fd instead of
 *    a real counter, for the generic events named in FAKE_PERF_EVENTS
 *    (comma-separated: branches, misses, cycles; default all three).  Other
 *    events fail with ENOENT.
 *  - ioctl() on the group leader enables, disables and resets the group, and
 *    read() returns counts derived from the time the group was enabled:
 *    1 branch and 3 cycles per ns, 1 miss per 20 ns.  The first three reads
 *    exercise the error paths: a group that never ran, counters that were
 *    time-shared, and a short read.
 *  - fopen() of /proc/cpuinfo, of cpufreq's cpuinfo_max_freq and
 *    scaling_governor, and of topology's thread_siblings_list returns
 *    fixed contents.
 *
 * Counts are for coverage only and mean nothing.  Not for measurements.
 *
 * Build: gcc -shared -fPIC -o fake_perf.so fake_perf.c -ldl
 *        (not -pedantic: casting dlsym()'s result to a function pointer is
 *        what POSIX requires but ISO C forbids)
 * Use:   LD_PRELOAD=$PWD/fake_perf.so ./branch_bench_cov ...
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>

#define MAX_FAKE 8

static int      n_fake = 0;
static int      leader = -1;
static uint64_t config_of[MAX_FAKE];
static uint64_t elapsed_ns = 0;     /* time enabled since the last reset */
static uint64_t total_enabled_ns = 0;
static uint64_t total_running_ns = 0;
static uint64_t trial_running_ns = 0;
static uint64_t enabled_at = 0;     /* 0 while disabled */
static int      reads = 0;
static int      sample_pending = 0;

static uint64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

typedef struct {
    uint64_t    config;
    const char *name;
    uint64_t    mul;
    uint64_t    div;
} FakeEvent;

static const FakeEvent fake_events[] = {
    { PERF_COUNT_HW_BRANCH_INSTRUCTIONS, "branches", 1,  1 },
    { PERF_COUNT_HW_BRANCH_MISSES,       "misses",   1, 20 },
    { PERF_COUNT_HW_CPU_CYCLES,          "cycles",   3,  1 },
};

#define NUM_FAKE_EVENTS ((int)(sizeof fake_events / sizeof fake_events[0]))

static const FakeEvent *find_event(uint64_t config)
{
    for (int i = 0; i < NUM_FAKE_EVENTS; i++)
        if (fake_events[i].config == config)
            return &fake_events[i];
    return NULL;
}

static int event_allowed(uint64_t config)
{
    const FakeEvent *ev = find_event(config);
    if (!ev) return 0;
    const char *list = getenv("FAKE_PERF_EVENTS");
    if (!list) return 1;
    size_t len = strlen(ev->name);
    for (const char *p = list; (p = strstr(p, ev->name)) != NULL; p += len)
        if ((p == list || p[-1] == ',') && (p[len] == ',' || p[len] == '\0'))
            return 1;
    return 0;
}

static long fake_perf_event_open(const struct perf_event_attr *attr, int group_fd)
{
    if (attr->type != PERF_TYPE_HARDWARE || !event_allowed(attr->config) ||
        n_fake == MAX_FAKE || group_fd != leader) {
        errno = ENOENT;
        return -1;
    }
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    if (leader < 0) leader = fd;
    config_of[n_fake] = attr->config;
    n_fake++;
    return fd;
}

long syscall(long number, ...)
{
    va_list ap;
    long a[6];
    va_start(ap, number);
    for (int i = 0; i < 6; i++) a[i] = va_arg(ap, long);
    va_end(ap);

    if (number == __NR_perf_event_open)
        return fake_perf_event_open((const struct perf_event_attr *)a[0], (int)a[3]);

    static long (*real)(long, ...);
    if (!real) real = (long (*)(long, ...))dlsym(RTLD_NEXT, "syscall");
    return real(number, a[0], a[1], a[2], a[3], a[4], a[5]);
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    if (fd >= 0 && fd == leader) {
        if (request == PERF_EVENT_IOC_RESET) {
            elapsed_ns = 0;
            trial_running_ns = 0;
            if (enabled_at) enabled_at = mono_ns();
        } else if (request == PERF_EVENT_IOC_ENABLE) {
            if (!enabled_at) enabled_at = mono_ns();
        } else if (request == PERF_EVENT_IOC_DISABLE) {
            if (enabled_at) {
                elapsed_ns += mono_ns() - enabled_at;
                reads++;
                trial_running_ns = reads == 1 ? 0 : reads == 2 ? elapsed_ns / 2 : elapsed_ns;
                total_enabled_ns += elapsed_ns;
                total_running_ns += trial_running_ns;
                sample_pending = 1;
            }
            enabled_at = 0;
        }
        return 0;
    }

    static int (*real)(int, unsigned long, ...);
    if (!real) real = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
    return real(fd, request, arg);
}

ssize_t read(int fd, void *buf, size_t count)
{
    if (fd >= 0 && fd == leader) {
        /* nr, time_enabled, time_running, then one value per member */
        uint64_t v[3 + MAX_FAKE];
        size_t want = (size_t)(3 + n_fake) * sizeof(uint64_t);
        v[0] = (uint64_t)n_fake;
        v[1] = total_enabled_ns;
        v[2] = total_running_ns;
        for (int i = 0; i < n_fake; i++) {
            uint64_t ran = trial_running_ns;
            const FakeEvent *ev = find_event(config_of[i]);
            v[3 + i] = ev ? (ran * ev->mul) / ev->div : 0;
        }
        if (sample_pending && reads == 3) want -= sizeof(uint64_t);
        sample_pending = 0;
        if (want > count) want = count;
        memcpy(buf, v, want);
        return (ssize_t)want;
    }

    static ssize_t (*real)(int, void *, size_t);
    if (!real) real = (ssize_t (*)(int, void *, size_t))dlsym(RTLD_NEXT, "read");
    return real(fd, buf, count);
}

/* An empty Model line (skipped), then Hardware and model name entries; the
 * " @ " base clock must be replaced by the cpufreq maximum.  The quotes in
 * the model name must be escaped in the JSON export; microcode and the
 * hypervisor flag are recorded there too. */
static char cpuinfo[] =
    "processor\t: 0\n"
    "Model\t\t: \n"
    "Hardware\t: Fake SoC\n"
    "model name\t: Fake \"Quoted\" CPU 1234 @ 2.40GHz\n"
    "microcode\t: 0xf4\n"
    "flags\t\t: fpu hypervisor\n"
    "Model\t\t: Not used after model name\n";

/* Per-CPU maximum clock in kHz: a lower value, a higher one, an unparsable
 * one, then lower ones again. */
static char khz_low[]  = "2400000\n";
static char khz_high[] = "3600000\n";
static char khz_bad[]  = "n/a\n";
static char governor[] = "performance\n";
static char siblings[] = "0,4\n";

FILE *fopen(const char *path, const char *mode)
{
    long cpu;
    char tail[32];
    if (strcmp(path, "/proc/cpuinfo") == 0)
        return fmemopen(cpuinfo, strlen(cpuinfo), "r");
    if (sscanf(path, "/sys/devices/system/cpu/cpu%ld/cpufreq/%31s", &cpu, tail) == 2 &&
        strcmp(tail, "cpuinfo_max_freq") == 0) {
        char *s = cpu == 1 ? khz_high : cpu == 2 ? khz_bad : khz_low;
        return fmemopen(s, strlen(s), "r");
    }
    if (sscanf(path, "/sys/devices/system/cpu/cpu%ld/cpufreq/%31s", &cpu, tail) == 2 &&
        strcmp(tail, "scaling_governor") == 0)
        return fmemopen(governor, strlen(governor), "r");
    if (sscanf(path, "/sys/devices/system/cpu/cpu%ld/topology/%31s", &cpu, tail) == 2 &&
        strcmp(tail, "thread_siblings_list") == 0)
        return fmemopen(siblings, strlen(siblings), "r");

    static FILE *(*real)(const char *, const char *);
    if (!real) real = (FILE *(*)(const char *, const char *))dlsym(RTLD_NEXT, "fopen");
    return real(path, mode);
}
