/* Regression fixture for check_jcc.sh (x86-64 only): every kernel it looks
 * for, as stubs.  sum_threshold's 2-byte jne ends exactly on a 32-byte
 * boundary, which the JCC-erratum microcode penalizes; every other jump and
 * ret is clear of one.  Build without run.sh's alignment flags; CI requires
 * check_jcc.sh to list exactly one hit, in <sum_threshold>. */
    .text
    .globl main
main:
    xorl %eax, %eax
    ret

    .p2align 5
    .globl sum_threshold
sum_threshold:
    .fill 30, 1, 0x90
    jne sum_threshold
    ret

    .p2align 4
    .globl sum_branchless
sum_branchless:
    ret

    .p2align 4
    .globl count_decisions
count_decisions:
    ret

    .p2align 4
    .globl dispatch_indexed
dispatch_indexed:
    ret

    .p2align 4
    .globl correlate_distance
correlate_distance:
    ret

    .section .note.GNU-stack,"",@progbits
