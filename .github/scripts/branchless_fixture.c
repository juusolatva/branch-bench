/* Regression fixture for check_kernels.sh: sum_threshold is made branchless
 * with a shift mask, which uses no blacklisted instruction (cmov, setcc,
 * sbb, ...), so only the data-branch check can reject it.  CI builds this
 * with the benchmark's flags and requires check_kernels.sh to report
 * "FAIL Test 1/4 ... no conditional branch inside the loop". */
#include <stddef.h>

typedef unsigned long long u64;
typedef unsigned char      u8;

__attribute__((noinline))
u64 sum_threshold(const u8 *arr, size_t n, unsigned n_reps)
{
    u64 acc = 0;
    for (unsigned rep = 0; rep < n_reps; rep++)
        for (size_t i = 0; i < n; i++)
            acc += arr[i] & ((u64)0 - (u64)(arr[i] >> 7));
    return acc;
}

int main(void)
{
    static u8 arr[64];
    return (int)sum_threshold(arr, sizeof arr, 1);
}
