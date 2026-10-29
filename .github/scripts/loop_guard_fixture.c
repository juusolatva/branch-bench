/* Regression fixture for check_kernels.sh: sum_threshold's sum is
 * branchless (shift mask), but the inner loop keeps a forward branch on its
 * counter.  That guard starts and lands inside the loop, so the old
 * "any forward branch inside a loop" rule accepted it; the data-branch rule
 * must not, because its condition is not loaded from the array.  CI
 * requires "FAIL Test 1/4 ...: expected 1 data branch(es), found 0". */
#include <stddef.h>

typedef unsigned long long u64;
typedef unsigned char      u8;

__attribute__((noinline))
u64 sum_threshold(const u8 *arr, size_t n, unsigned n_reps)
{
    u64 acc = 0;
    for (unsigned rep = 0; rep < n_reps; rep++)
        for (size_t i = 0; i < n; i++) {
            acc += arr[i] & ((u64)0 - (u64)(arr[i] >> 7));
            if (i == 1000) acc ^= 1;
        }
    return acc;
}

int main(void)
{
    static u8 arr[64];
    return (int)sum_threshold(arr, sizeof arr, 1);
}
