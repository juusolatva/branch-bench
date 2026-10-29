/* Regression fixture for check_kernels.sh: 32 Test 3 leaves where leaf_31
 * is an alias of leaf_30, as identical code folding would make it, so two
 * indirect-call targets share one address.  CI requires
 * "FAIL Test 3 leaves: 31 distinct addresses for 32 leaves". */
typedef unsigned long long u64;

__attribute__((noinline)) u64 leaf_0(u64 x) { return x ^ ((u64)(0) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_1(u64 x) { return x ^ ((u64)(1) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_2(u64 x) { return x ^ ((u64)(2) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_3(u64 x) { return x ^ ((u64)(3) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_4(u64 x) { return x ^ ((u64)(4) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_5(u64 x) { return x ^ ((u64)(5) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_6(u64 x) { return x ^ ((u64)(6) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_7(u64 x) { return x ^ ((u64)(7) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_8(u64 x) { return x ^ ((u64)(8) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_9(u64 x) { return x ^ ((u64)(9) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_10(u64 x) { return x ^ ((u64)(10) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_11(u64 x) { return x ^ ((u64)(11) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_12(u64 x) { return x ^ ((u64)(12) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_13(u64 x) { return x ^ ((u64)(13) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_14(u64 x) { return x ^ ((u64)(14) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_15(u64 x) { return x ^ ((u64)(15) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_16(u64 x) { return x ^ ((u64)(16) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_17(u64 x) { return x ^ ((u64)(17) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_18(u64 x) { return x ^ ((u64)(18) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_19(u64 x) { return x ^ ((u64)(19) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_20(u64 x) { return x ^ ((u64)(20) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_21(u64 x) { return x ^ ((u64)(21) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_22(u64 x) { return x ^ ((u64)(22) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_23(u64 x) { return x ^ ((u64)(23) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_24(u64 x) { return x ^ ((u64)(24) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_25(u64 x) { return x ^ ((u64)(25) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_26(u64 x) { return x ^ ((u64)(26) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_27(u64 x) { return x ^ ((u64)(27) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_28(u64 x) { return x ^ ((u64)(28) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_29(u64 x) { return x ^ ((u64)(29) * 0x9e3779b97f4a7c15ULL); }
__attribute__((noinline)) u64 leaf_30(u64 x) { return x ^ ((u64)(30) * 0x9e3779b97f4a7c15ULL); }
u64 leaf_31(u64 x) __attribute__((alias("leaf_30")));

int main(void)
{
    return (int)leaf_31(1);
}
