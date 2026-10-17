#!/bin/sh
# Checks that the Test 1-3 kernels in a GCC build still contain the code the
# benchmark measures: a real conditional branch (no conditional move/select,
# no SIMD) in Tests 1-2, an indirect call in Test 3.  Whether these survive
# depends on the compiler version, and losing them silently invalidates
# results.  x86-64 and aarch64 only.
#
# Usage: check_kernels.sh <binary>
# ARCH and OBJDUMP override the host architecture and objdump, e.g. for a
# cross build: ARCH=aarch64 OBJDUMP=aarch64-linux-gnu-objdump
set -eu

bin=${1:?usage: check_kernels.sh <binary>}
arch=${ARCH:-$(uname -m)}
objdump=${OBJDUMP:-objdump}

case $arch in
    x86_64)
        cond_move='cmov[a-z]*[[:space:]]'
        cond_branch='j(a|ae|b|be|c|e|g|ge|l|le|na|nae|nb|nbe|nc|ne|ng|nge|nl|nle|no|np|ns|nz|o|p|pe|po|s|z)[[:space:]]'
        indirect_call='call[[:space:]]+\*'
        simd='%[xyz]mm[0-9]+'
        ;;
    aarch64)
        cond_move='(csel|csinc|csinv|csneg|cset|csetm|cinc|cinv|cneg)[[:space:]]'
        cond_branch='(b\.[a-z]+|cbz|cbnz|tbz|tbnz)[[:space:]]'
        indirect_call='blr[[:space:]]'
        simd='[vq][0-9]+'
        ;;
    *)
        echo "check_kernels.sh: unsupported architecture $arch" >&2
        exit 2
        ;;
esac

# Instructions of one function, matched with optional GCC clone suffixes
# such as .constprop.0.  GCC folds sum_branch and sum_threshold into one
# body listed under either name, so the caller passes both.
body() {
    "$objdump" -d --no-show-raw-insn "$bin" |
        awk -v re="^[0-9a-f]+ <($1)(\\\\.[^>]*)?>:\$" '
            $0 ~ re          { in_fn = 1; next }
            in_fn && /^$/    { in_fn = 0 }
            in_fn            { sub(/^[^\t]*\t/, ""); print }'
}

fail=0

# check <label> <function regex> <required instruction regex>
check() {
    insns=$(body "$2")
    bad=0
    if [ -z "$insns" ]; then
        echo "FAIL $1: function not found"; fail=1; return 0
    fi
    if printf '%s\n' "$insns" | grep -Eq "^$cond_move"; then
        echo "FAIL $1: conditional move/select found"; bad=1
    fi
    if printf '%s\n' "$insns" | grep -Eq "$simd"; then
        echo "FAIL $1: SIMD registers used"; bad=1
    fi
    if ! printf '%s\n' "$insns" | grep -Eq "^$3"; then
        echo "FAIL $1: required instruction missing ($3)"; bad=1
    fi
    if [ "$bad" -eq 0 ]; then echo "ok   $1"; else fail=1; fi
}

check "Test 1/4 sum_threshold/sum_branch" 'sum_threshold|sum_branch' "$cond_branch"
check "Test 2 count_decisions"            'count_decisions'           "$cond_branch"
check "Test 3 dispatch_sequential"        'dispatch_sequential'       "$indirect_call"
check "Test 3 dispatch_random"            'dispatch_random'           "$indirect_call"

exit "$fail"
