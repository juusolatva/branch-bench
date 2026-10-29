/* Regression fixture for check_kernels.sh: pattern_sites_4's four site
 * branches read offsets 0, 1, 1 and 3, so two sites share one stream and
 * site 2's stream is never read.  The data-branch count is still 4.  CI
 * requires
 * "FAIL Test 5 pattern_sites_4: site offsets 0,1,1,3, expected 0..3 once each". */
#include <stddef.h>

typedef unsigned long long u64;
typedef unsigned char      u8;

#if defined(__x86_64__)
#  define SITE(off) "cmpb $0, " #off "(%[p])\n\tje 3f\n\tinc %[acc]\n3:\n\t"
#elif defined(__aarch64__)
#  define SITE(off) "ldrb %w[v], [%[p], #" #off "]\n\tcbz %w[v], 3f\n\tadd %[acc], %[acc], #1\n3:\n\t"
#else
#  error "x86-64 and aarch64 only"
#endif

__attribute__((noinline))
u64 pattern_sites_4(const u8 *data, size_t blocks, unsigned passes)
{
    u64 acc = 0;
    const u8 *p;
    unsigned r = passes;
#if defined(__x86_64__)
    __asm__ __volatile__(
        "test %[b], %[b]\n\t"
        "jz 4f\n\t"
        "test %k[r], %k[r]\n\t"
        "jz 4f\n"
        "1:\n\t"
        "mov %[arr], %[p]\n"
        "2:\n\t"
        SITE(0) SITE(1) SITE(1) SITE(3)
        "add $4, %[p]\n\t"
        "cmp %[end], %[p]\n\t"
        "jne 2b\n\t"
        "dec %k[r]\n\t"
        "jnz 1b\n"
        "4:"
        : [acc] "+r"(acc), [p] "=&r"(p), [r] "+r"(r)
        : [arr] "r"(data), [end] "r"(data + 4 * blocks), [b] "r"(blocks)
        : "cc", "memory");
#else
    u64 v;
    __asm__ __volatile__(
        "cbz %[b], 4f\n\t"
        "cbz %w[r], 4f\n"
        "1:\n\t"
        "mov %[p], %[arr]\n"
        "2:\n\t"
        SITE(0) SITE(1) SITE(1) SITE(3)
        "add %[p], %[p], #4\n\t"
        "cmp %[p], %[end]\n\t"
        "b.ne 2b\n\t"
        "subs %w[r], %w[r], #1\n\t"
        "b.ne 1b\n"
        "4:"
        : [acc] "+r"(acc), [p] "=&r"(p), [v] "=&r"(v), [r] "+r"(r)
        : [arr] "r"(data), [end] "r"(data + 4 * blocks), [b] "r"(blocks)
        : "cc", "memory");
#endif
    return acc;
}

int main(void)
{
    static u8 data[64];
    return (int)pattern_sites_4(data, sizeof data / 4, 1);
}
