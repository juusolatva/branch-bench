/* Regression fixture for check_kernels.sh: 64 Test 3 leaves where leaf_63
 * is an alias of leaf_62, as identical code folding would make it, so two
 * indirect-call targets share one address.  CI requires
 * "FAIL Test 3 leaves: 63 distinct addresses for 64 leaves". */
typedef unsigned long long u64;

#define LEAF_CONST(n) ((((u64)(n) + 1) * 0x9e3779b97f4a7c15ULL & 0x7ffe7ffe7ffe7ffeULL) | 0x0001000100010001ULL)

__attribute__((noinline)) u64 leaf_0(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(0); }
__attribute__((noinline)) u64 leaf_1(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(1); }
__attribute__((noinline)) u64 leaf_2(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(2); }
__attribute__((noinline)) u64 leaf_3(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(3); }
__attribute__((noinline)) u64 leaf_4(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(4); }
__attribute__((noinline)) u64 leaf_5(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(5); }
__attribute__((noinline)) u64 leaf_6(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(6); }
__attribute__((noinline)) u64 leaf_7(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(7); }
__attribute__((noinline)) u64 leaf_8(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(8); }
__attribute__((noinline)) u64 leaf_9(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(9); }
__attribute__((noinline)) u64 leaf_10(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(10); }
__attribute__((noinline)) u64 leaf_11(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(11); }
__attribute__((noinline)) u64 leaf_12(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(12); }
__attribute__((noinline)) u64 leaf_13(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(13); }
__attribute__((noinline)) u64 leaf_14(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(14); }
__attribute__((noinline)) u64 leaf_15(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(15); }
__attribute__((noinline)) u64 leaf_16(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(16); }
__attribute__((noinline)) u64 leaf_17(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(17); }
__attribute__((noinline)) u64 leaf_18(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(18); }
__attribute__((noinline)) u64 leaf_19(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(19); }
__attribute__((noinline)) u64 leaf_20(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(20); }
__attribute__((noinline)) u64 leaf_21(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(21); }
__attribute__((noinline)) u64 leaf_22(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(22); }
__attribute__((noinline)) u64 leaf_23(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(23); }
__attribute__((noinline)) u64 leaf_24(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(24); }
__attribute__((noinline)) u64 leaf_25(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(25); }
__attribute__((noinline)) u64 leaf_26(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(26); }
__attribute__((noinline)) u64 leaf_27(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(27); }
__attribute__((noinline)) u64 leaf_28(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(28); }
__attribute__((noinline)) u64 leaf_29(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(29); }
__attribute__((noinline)) u64 leaf_30(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(30); }
__attribute__((noinline)) u64 leaf_31(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(31); }
__attribute__((noinline)) u64 leaf_32(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(32); }
__attribute__((noinline)) u64 leaf_33(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(33); }
__attribute__((noinline)) u64 leaf_34(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(34); }
__attribute__((noinline)) u64 leaf_35(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(35); }
__attribute__((noinline)) u64 leaf_36(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(36); }
__attribute__((noinline)) u64 leaf_37(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(37); }
__attribute__((noinline)) u64 leaf_38(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(38); }
__attribute__((noinline)) u64 leaf_39(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(39); }
__attribute__((noinline)) u64 leaf_40(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(40); }
__attribute__((noinline)) u64 leaf_41(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(41); }
__attribute__((noinline)) u64 leaf_42(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(42); }
__attribute__((noinline)) u64 leaf_43(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(43); }
__attribute__((noinline)) u64 leaf_44(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(44); }
__attribute__((noinline)) u64 leaf_45(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(45); }
__attribute__((noinline)) u64 leaf_46(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(46); }
__attribute__((noinline)) u64 leaf_47(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(47); }
__attribute__((noinline)) u64 leaf_48(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(48); }
__attribute__((noinline)) u64 leaf_49(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(49); }
__attribute__((noinline)) u64 leaf_50(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(50); }
__attribute__((noinline)) u64 leaf_51(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(51); }
__attribute__((noinline)) u64 leaf_52(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(52); }
__attribute__((noinline)) u64 leaf_53(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(53); }
__attribute__((noinline)) u64 leaf_54(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(54); }
__attribute__((noinline)) u64 leaf_55(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(55); }
__attribute__((noinline)) u64 leaf_56(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(56); }
__attribute__((noinline)) u64 leaf_57(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(57); }
__attribute__((noinline)) u64 leaf_58(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(58); }
__attribute__((noinline)) u64 leaf_59(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(59); }
__attribute__((noinline)) u64 leaf_60(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(60); }
__attribute__((noinline)) u64 leaf_61(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(61); }
__attribute__((noinline)) u64 leaf_62(u64 x) { return ((x << 1) | (x >> 63)) ^ LEAF_CONST(62); }
u64 leaf_63(u64 x) __attribute__((alias("leaf_62")));

typedef u64 (*leaf_fn)(u64);
static leaf_fn const table[] = { leaf_0, leaf_1, leaf_2, leaf_3, leaf_4, leaf_5, leaf_6, leaf_7, leaf_8, leaf_9, leaf_10, leaf_11, leaf_12, leaf_13, leaf_14, leaf_15, leaf_16, leaf_17, leaf_18, leaf_19, leaf_20, leaf_21, leaf_22, leaf_23, leaf_24, leaf_25, leaf_26, leaf_27, leaf_28, leaf_29, leaf_30, leaf_31, leaf_32, leaf_33, leaf_34, leaf_35, leaf_36, leaf_37, leaf_38, leaf_39, leaf_40, leaf_41, leaf_42, leaf_43, leaf_44, leaf_45, leaf_46, leaf_47, leaf_48, leaf_49, leaf_50, leaf_51, leaf_52, leaf_53, leaf_54, leaf_55, leaf_56, leaf_57, leaf_58, leaf_59, leaf_60, leaf_61, leaf_62, leaf_63 };

int main(int argc, char **argv)
{
    (void)argv;
    u64 acc = 0;
    for (unsigned i = 0; i < sizeof table / sizeof table[0]; i++)
        acc = table[(i + (unsigned)argc) % (sizeof table / sizeof table[0])](acc);
    return (int)(acc & 1);
}
