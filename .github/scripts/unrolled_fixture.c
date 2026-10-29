/* Regression fixture for check_kernels.sh: the real sum_threshold body with
 * its inner loop unrolled, so the data branch is duplicated.  A duplicated
 * site changes what the benchmark measures, so CI requires
 * "FAIL Test 1/4 ...: expected 1 data branch(es), found <2 or more>". */
#include <stddef.h>

typedef unsigned long long u64;
typedef unsigned char      u8;

__attribute__((noinline))
u64 sum_threshold(const u8 *arr, size_t n, unsigned n_reps)
{
    u64 acc = 0;
    for (unsigned rep = 0; rep < n_reps; rep++) {
#pragma GCC unroll 2
        for (size_t i = 0; i < n; i++)
            if (arr[i] > 128)
                acc += arr[i];
    }
    return acc;
}

int main(void)
{
    static u8 arr[64];
    return (int)sum_threshold(arr, sizeof arr, 1);
}
