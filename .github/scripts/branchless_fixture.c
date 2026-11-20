/* Regression fixture for check_kernels.sh: sum_threshold is made branchless
 * with a shift mask, which uses no blacklisted instruction (cmov, setcc,
 * sbb, ...), so only the data-branch check can reject it.  sum_branch keeps
 * its data branch, as an unfolded build of the benchmark would, so the check
 * must look at each function on its own: run on both bodies together, it
 * would find sum_branch's branch and pass.  CI builds this with the
 * benchmark's flags and requires check_kernels.sh to report "FAIL Test 1/4
 * sum_threshold: no conditional branch inside the loop" and "ok   Test 1/4
 * sum_branch". */
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

__attribute__((noinline))
u64 sum_branch(const u8 *arr, size_t n, unsigned n_reps)
{
    u64 acc = 0;
    for (unsigned rep = 0; rep < n_reps; rep++)
        for (size_t i = 0; i < n; i++)
            if (arr[i] > 128)
                acc += arr[i];
    return acc;
}

int main(void)
{
    static u8 arr[64];
    return (int)(sum_threshold(arr, sizeof arr, 1) + sum_branch(arr, sizeof arr, 1));
}
