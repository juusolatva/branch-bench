#!/bin/sh
# Checks that the benchmark kernels in a GCC build still contain the code the
# benchmark measures: a real conditional branch in Tests 1-2 (no conditional
# move/select, no flag arithmetic such as setcc/sbb/adc, no SIMD), an
# indirect call in Test 3 to 32 distinct leaf functions, and no data branch
# in Test 4's branchless variant.  Whether these survive depends on the
# compiler version, and losing them silently invalidates results.  x86-64
# and aarch64 only.
#
# Tests 1-2 need a data branch: a forward conditional branch that starts and
# lands inside an innermost loop (between a backward branch and its target,
# with no other loop in between), so neither a loop back-edge, an early exit
# to the return path, nor an outer loop's guard around an unrolled inner loop
# counts.  Known branchless forms are rejected too.  A pass is necessary, not
# sufficient: a branch inside the loop need not be the one on the data.
#
# Usage: check_kernels.sh <binary>
# ARCH and OBJDUMP override the host architecture and objdump, e.g. for a
# cross build: ARCH=aarch64 OBJDUMP=aarch64-linux-gnu-objdump
set -eu

bin=${1:?usage: check_kernels.sh <binary>}
arch=${ARCH:-$(uname -m)}
objdump=${OBJDUMP:-objdump}

# Regexes reach awk through the environment (ENVIRON), never awk -v, which
# would process their backslash escapes.
case $arch in
    x86_64 | amd64)
        branchless='(cmov[a-z]*|set[a-z]+|sbb[a-z]?|adc[a-z]?)[[:space:]]'
        cond_branch='j(a|ae|b|be|c|e|g|ge|l|le|na|nae|nb|nbe|nc|ne|ng|nge|nl|nle|no|np|ns|nz|o|p|pe|po|s|z)[[:space:]]'
        uncond_branch='jmp[[:space:]]+[0-9a-f]+ <'
        indirect_call='call[[:space:]]+\*'
        simd='%[xyz]mm[0-9]+'
        ;;
    aarch64 | arm64)
        branchless='(csel|csinc|csinv|csneg|cset|csetm|cinc|cinv|cneg|adcs?|sbcs?|ngcs?)[[:space:]]'
        cond_branch='(b\.[a-z]+|cbz|cbnz|tbz|tbnz)[[:space:]]'
        uncond_branch='b[[:space:]]+[0-9a-f]+ <'
        indirect_call='blr[[:space:]]'
        simd='(^|[^[:alnum:]_])[vq][0-9]+'
        ;;
    *)
        echo "check_kernels.sh: unsupported architecture $arch" >&2
        exit 2
        ;;
esac
# x86 instruction prefixes, e.g. the "cs" padding that run.sh's alignment
# flags add; stripped so the mnemonic starts the instruction.
prefixes='((cs|ds|es|ss|fs|gs|data16|addr32|notrack|bnd|lock|rep|repz|repnz|repe|repne|rex(\.[WRXB]+)?)[[:space:]]+)+'
export branchless cond_branch uncond_branch prefixes

disasm=$("$objdump" -d --no-show-raw-insn "$bin")

# "<address>\t<instruction>" lines of one function, prefixes stripped,
# matched with optional GCC clone suffixes such as .constprop.0.
listing() {
    printf '%s\n' "$disasm" | fn=$1 awk '
        BEGIN {
            re = "^[0-9a-f]+ <(" ENVIRON["fn"] ")(\\.[^>]*)?>:$"
            pre = "^" ENVIRON["prefixes"]
        }
        $0 ~ re       { in_fn = 1; next }
        in_fn && /^$/ { in_fn = 0 }
        in_fn {
            at = $1; sub(/:$/, "", at)
            insn = $0; sub(/^[^\t]*\t/, "", insn); sub(pre, "", insn)
            print at "\t" insn
        }'
}

# Instructions only.
body() {
    listing "$1" | cut -f2-
}

# True if the function has a forward conditional branch that starts and
# lands inside an innermost loop.  A loop is a backward branch (conditional
# or not) and its target; it is innermost unless its range holds another
# loop with a different target.  Branch targets are the "<hex> <" operand.
has_loop_branch() {
    listing "$1" | awk -F'\t' '
        function hex(s,    i, n) {
            n = 0
            for (i = 1; i <= length(s); i++)
                n = n * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
            return n
        }
        BEGIN { cond = "^" ENVIRON["cond_branch"]; jmp = "^" ENVIRON["uncond_branch"] }
        {
            insn = $0; sub(/^[^\t]*\t/, "", insn)   # aarch64 has a tab after the mnemonic
            if (insn ~ cond) c = 1
            else if (insn ~ jmp) c = 0
            else next
            if (!match(insn, /[0-9a-f]+ </)) next
            n++; from[n] = hex($1); dest[n] = hex(substr(insn, RSTART, RLENGTH - 2)); is_cond[n] = c
        }
        END {
            for (j = 1; j <= n; j++) {
                if (dest[j] > from[j]) continue
                inner = 1
                for (k = 1; k <= n; k++)
                    if (k != j && dest[k] <= from[k] && dest[k] != dest[j] &&
                        dest[j] <= dest[k] && from[k] <= from[j])
                        inner = 0
                if (!inner) continue
                for (i = 1; i <= n; i++)
                    if (is_cond[i] && dest[i] > from[i] &&
                        dest[j] <= from[i] && from[i] < from[j] && dest[i] <= from[j])
                        exit 0
            }
            exit 1
        }'
}

fail=0

# check <label> <function regex> <requirement>
# requirement: loop-branch (Tests 1-2), branchless (Test 4's branchless
# variant: no data branch; arithmetic such as setcc/sbb is expected there),
# or a regex for an instruction that must be present.
check() {
    insns=$(body "$2")
    bad=0
    if [ -z "$insns" ]; then
        echo "FAIL $1: function not found"; fail=1; return 0
    fi
    if [ "$3" != branchless ] && printf '%s\n' "$insns" | grep -Eq "^$branchless"; then
        echo "FAIL $1: branchless instruction found (conditional move/select or flag arithmetic)"; bad=1
    fi
    # Symbol names in <...> annotations could look like registers.
    if printf '%s\n' "$insns" | sed 's/<[^>]*>//g' | grep -Eq "$simd"; then
        echo "FAIL $1: SIMD registers used"; bad=1
    fi
    case $3 in
        loop-branch)
            if ! has_loop_branch "$2"; then
                echo "FAIL $1: no conditional branch inside the loop (only back-edges, exits or outer-loop guards)"; bad=1
            fi ;;
        branchless)
            if has_loop_branch "$2"; then
                echo "FAIL $1: conditional branch inside the loop"; bad=1
            fi ;;
        *)
            if ! printf '%s\n' "$insns" | grep -Eq "^$3"; then
                echo "FAIL $1: required instruction missing ($3)"; bad=1
            fi ;;
    esac
    if [ "$bad" -eq 0 ]; then echo "ok   $1"; else fail=1; fi
}

# Test 3's FOR_EACH_LEAF defines 32 leaves.  Identical code folding would
# give some of them one address (an alias) or turn them into jumps to another
# leaf (a thunk, without its own ret), shrinking the set of branch targets.
check_leaves() {
    label="Test 3 leaf_0..leaf_31"
    syms=$("$objdump" -t "$bin" | awk '/ F / && $NF ~ /^leaf_[0-9]+$/ { print $1, $NF }')
    names=$(printf '%s\n' "$syms" | awk 'NF { n++ } END { print n + 0 }')
    addrs=$(printf '%s\n' "$syms" | awk 'NF { print $1 }' | sort -u | wc -l)
    # One pass: count leaf listings that end without a ret.
    noret=$(printf '%s\n' "$disasm" | awk '
        BEGIN {
            re = "^[0-9a-f]+ <leaf_[0-9]+(\\.[^>]*)?>:$"
            pre = "^" ENVIRON["prefixes"]
        }
        function close_leaf() { if (in_leaf && !has_ret) noret++; in_leaf = 0 }
        $0 ~ re       { close_leaf(); in_leaf = 1; has_ret = 0; next }
        /^$/          { close_leaf(); next }
        in_leaf {
            insn = $0; sub(/^[^\t]*\t/, "", insn); sub(pre, "", insn)
            if (insn ~ /^ret/) has_ret = 1
        }
        END { close_leaf(); print noret + 0 }')
    if [ "$names" -ne 32 ] || [ "$addrs" -ne 32 ] || [ "$noret" -ne 0 ]; then
        echo "FAIL $label: want 32 distinct leaves with their own ret, found $names symbols, $addrs addresses, $noret without ret"
        fail=1
    else
        echo "ok   $label"
    fi
}

# GCC folds sum_branch and sum_threshold into one body listed under either
# name.  Each name that is listed is checked on its own, so an unfolded
# build cannot pass on one function's branch while the other lost its own.
check_test1() {
    found=0
    for fn in sum_threshold sum_branch; do
        if [ -n "$(body "$fn")" ]; then
            found=1
            check "Test 1/4 $fn" "$fn" loop-branch
        fi
    done
    if [ "$found" -eq 0 ]; then
        echo "FAIL Test 1/4 sum_threshold/sum_branch: function not found"; fail=1
    fi
}

check_test1
check "Test 2 count_decisions"            'count_decisions'           loop-branch
check "Test 3 dispatch_sequential"        'dispatch_sequential'       "$indirect_call"
check "Test 3 dispatch_random"            'dispatch_random'           "$indirect_call"
check_leaves
check "Test 4 sum_branchless"             'sum_branchless'            branchless

exit "$fail"
