/* Regression fixture for check_kernels.sh: sum_threshold's data branch is
 * in an asm block, but only for one element, inside a C loop that GCC
 * unrolls, so the binary has two copies of the asm data branch.  asm
 * volatile does not stop that; the kernel must keep the whole loop in one
 * asm block.  CI requires
 * "FAIL Test 1/4 sum_threshold: expected 1 data branch(es), found 2" (or more). */
#include <stddef.h>

typedef unsigned long long u64;
typedef unsigned char      u8;

__attribute__((noinline))
u64 sum_threshold(const u8 *arr, size_t n, unsigned n_reps)
{
    u64 acc = 0;
    for (unsigned rep = 0; rep < n_reps; rep++) {
#pragma GCC unroll 2
        for (size_t i = 0; i < n; i++) {
            u64 v;
#if defined(__x86_64__)
            __asm__ __volatile__(
                "movzbl (%[p]), %k[v]\n\t"
                "cmp $128, %k[v]\n\t"
                "jbe 1f\n\t"
                "add %[v], %[acc]\n"
                "1:"
                : [acc] "+r"(acc), [v] "=&r"(v)
                : [p] "r"(arr + i)
                : "cc", "memory");
#elif defined(__aarch64__)
            __asm__ __volatile__(
                "ldrb %w[v], [%[p]]\n\t"
                "cmp %w[v], #128\n\t"
                "b.ls 1f\n\t"
                "add %[acc], %[acc], %[v]\n"
                "1:"
                : [acc] "+r"(acc), [v] "=&r"(v)
                : [p] "r"(arr + i)
                : "cc", "memory");
#else
#error "x86-64 and aarch64 only"
#endif
        }
    }
    return acc;
}

int main(void)
{
    static u8 arr[64];
    return (int)sum_threshold(arr, sizeof arr, 1);
}
