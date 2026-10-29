/* Regression fixture for check_kernels.sh: loop_exit's inner loop holds
 * two copies of its loop branch (unrolled by two), so the trip count no
 * longer runs on one branch address.  CI requires
 * "FAIL Test 6 loop_exit: expected 1 conditional branch in the inner loop, found 2". */
typedef unsigned long long u64;

__attribute__((noinline))
u64 loop_exit(unsigned c, u64 loops)
{
    u64 acc = 0;
    u64 i;
#if defined(__x86_64__)
    __asm__ __volatile__(
        "test %[l], %[l]\n\t"
        "jz 4f\n"
        "1:\n\t"
        "mov %k[c], %k[i]\n"
        "2:\n\t"
        "inc %[acc]\n\t"
        "dec %[i]\n\t"
        "jz 3f\n\t"
        "inc %[acc]\n\t"
        "dec %[i]\n\t"
        "jnz 2b\n"
        "3:\n\t"
        "dec %[l]\n\t"
        "jnz 1b\n"
        "4:"
        : [acc] "+r"(acc), [i] "=&r"(i), [l] "+r"(loops)
        : [c] "r"(c)
        : "cc");
#elif defined(__aarch64__)
    __asm__ __volatile__(
        "cbz %[l], 4f\n"
        "1:\n\t"
        "mov %w[i], %w[c]\n"
        "2:\n\t"
        "add %[acc], %[acc], #1\n\t"
        "subs %[i], %[i], #1\n\t"
        "b.eq 3f\n\t"
        "add %[acc], %[acc], #1\n\t"
        "subs %[i], %[i], #1\n\t"
        "b.ne 2b\n"
        "3:\n\t"
        "subs %[l], %[l], #1\n\t"
        "b.ne 1b\n"
        "4:"
        : [acc] "+r"(acc), [i] "=&r"(i), [l] "+r"(loops)
        : [c] "r"(c)
        : "cc");
#else
#error "x86-64 and aarch64 only"
#endif
    return acc;
}

int main(void)
{
    return (int)loop_exit(4, 2);
}
