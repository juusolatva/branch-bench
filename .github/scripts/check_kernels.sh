#!/bin/sh
# Checks that the benchmark kernels in a GCC build still contain the code
# the benchmark measures.  Whether it survives depends on the compiler
# version (for the C kernels) or on the asm block staying single (for the
# assembly kernels), and losing it silently invalidates results.  x86-64
# and aarch64 only.
#
#   Test 1/4 sum_threshold, Test 2/5 count_decisions:
#       exactly one data branch, no branchless instruction (conditional
#       move/select, flag arithmetic such as setcc/sbb/adc), no SIMD.
#   Test 4 sum_branchless: no data branch (cmov/setcc allowed), no SIMD.
#   Test 3 dispatch_sequential, dispatch_random: exactly one indirect call.
#   Test 3 leaves: leaf_0 ... leaf_31 at 32 distinct addresses.
#   Test 6 correlate_distance: at least three conditional branches inside
#       its loops (A, filler and B; mode loop-branch3).
#   Every kernel except sum_branchless: no branchless instruction; every
#       kernel: no SIMD.
#   Every kernel: exactly one function body (no clone or duplicate).
#
# A data branch is a forward conditional branch that starts and lands inside
# a loop (between a backward branch and its target) and whose condition
# comes from data loaded in that loop:
#   x86-64:  the nearest preceding flag-setting instruction is a cmp/test
#            with a memory operand, or one reading a register last written
#            inside the loop by a load (mov* with a memory source);
#   aarch64: a cbz/cbnz/tbz/tbnz on such a register (written by ldr*), or a
#            b.cond whose nearest preceding flag setter (cmp, cmn, tst, ands,
#            subs, adds, bics, ccmp, ccmn) reads one; adcs/sbcs/negs/ngcs
#            in between make the branch not count.
# Registers count by family (%al/%eax/%rax, w3/x3).  The loop back-edge,
# early exits and loop guards on counters do not count.  A pass is still
# necessary, not sufficient: a load-fed compare need not be the benchmark's
# condition, so read the inner loop after codegen-relevant changes.
#
# Usage: check_kernels.sh <binary>           checks; exit 1 on any FAIL
#        check_kernels.sh --counts <binary>  prints "id data_branches
#            indirect_calls inner_loop_branches" for every kernel in
#            branch_bench.c's kernel_descs[], for comparison
#            with the JSON export's kernel records (check_export.py
#            --kernel-counts); exit 2 if a kernel is missing
# ARCH and OBJDUMP override the host architecture and objdump, e.g. for a
# cross build: ARCH=aarch64 OBJDUMP=aarch64-linux-gnu-objdump
set -eu

counts=0
if [ "${1:-}" = --counts ]; then counts=1; shift; fi
bin=${1:?usage: check_kernels.sh [--counts] <binary>}
arch=${ARCH:-$(uname -m)}
objdump=${OBJDUMP:-objdump}

case $arch in
    x86_64)
        branchless='(cmov[a-z]*|set[a-z]+|sbb[a-z]?|adc[a-z]?)[[:space:]]'
        cond_branch='j(a|ae|b|be|c|e|g|ge|l|le|na|nae|nb|nbe|nc|ne|ng|nge|nl|nle|no|np|ns|nz|o|p|pe|po|s|z)[[:space:]]'
        simd='%[xyz]mm[0-9]+'
        ;;
    aarch64)
        branchless='(csel|csinc|csinv|csneg|cset|csetm|cinc|cinv|cneg|adcs?|sbcs?|ngcs?)[[:space:]]'
        cond_branch='(b\.[a-z]+|cbz|cbnz|tbz|tbnz)[[:space:]]'
        simd='[vq][0-9]+'
        ;;
    *)
        echo "check_kernels.sh: unsupported architecture $arch" >&2
        exit 2
        ;;
esac

disassembly=$("$objdump" -d --no-show-raw-insn "$bin")

# Instructions of one function, matched with optional GCC clone suffixes
# such as .constprop.0.
body() {
    printf '%s\n' "$disassembly" |
        awk -v re="^[0-9a-f]+ <($1)(\\\\.[^>]*)?>:\$" '
            $0 ~ re          { in_fn = 1; next }
            in_fn && /^$/    { in_fn = 0 }
            in_fn            { sub(/^[^\t]*\t/, ""); print }'
}

# analyze <function regex>: prints one line per matching function body:
# "name data_branches indirect_calls inner_loop_branches loop_branches".
# loop_branches counts forward conditional branches inside a loop of any
# kind (mode loop-branch3).  inner_loop_branches counts the branch, call
# and return instructions of the hot loop: the innermost loop that reads
# memory or makes an indirect call (so not, e.g., GCC's empty loop for
# n == 0).
analyze() {
    printf '%s\n' "$disassembly" | awk -v re="^[0-9a-f]+ <($1)(\\\\.[^>]*)?>:\$" \
        -v cond="^$cond_branch" -v arch="$arch" '
        function hex(s,    i, n) {
            n = 0
            for (i = 1; i <= length(s); i++)
                n = n * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
            return n
        }
        function trim(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); return s }
        function mnem(ins,    w) { split(ins, w, /[ \t]+/); return w[1] }
        function operands(ins) { return trim(substr(ins, length(mnem(ins)) + 1)) }
        # Operands split at top-level commas (not inside () or []).
        function split_ops(s, arr,    n, i, c, depth, cur) {
            n = 0; depth = 0; cur = ""
            for (i = 1; i <= length(s); i++) {
                c = substr(s, i, 1)
                if (c == "(" || c == "[") depth++
                else if (c == ")" || c == "]") depth--
                if (c == "," && depth == 0) { arr[++n] = trim(cur); cur = "" }
                else cur = cur c
            }
            if (trim(cur) != "") arr[++n] = trim(cur)
            return n
        }
        # Register family: %al/%ax/%eax/%rax -> a, %r8b/%r8d/%r8 -> r8,
        # w3/x3 -> 3; "" for anything else.
        function fam(r) {
            if (arch == "aarch64") return r ~ /^[wx][0-9]+$/ ? substr(r, 2) : ""
            if (r !~ /^%/) return ""
            r = substr(r, 2)
            if (r ~ /^r[0-9]+[bwd]?$/) { sub(/[bwd]$/, "", r); return r }
            if (length(r) == 3 && r ~ /^[re]/) r = substr(r, 2)
            if (r ~ /^[abcd][xlh]$/) return substr(r, 1, 1)
            if (r ~ /^(si|di|bp|sp)l?$/) return substr(r, 1, 2)
            return ""
        }
        # Register families read in an operand string (into regs[]).
        function reg_fams(s, regs,    n, i, tok, f, k) {
            delete regs
            n = split(s, tok, /[^%a-z0-9]+/)
            k = 0
            for (i = 1; i <= n; i++) {
                f = fam(tok[i])
                if (f != "") regs[++k] = f
            }
            return k
        }
        # Family of the register an instruction writes, or "".
        function dst_fam(ins,    m, arr, n) {
            m = mnem(ins)
            n = split_ops(operands(ins), arr)
            if (!n) return ""
            if (arch == "aarch64") {
                if (m ~ /^(st|cmp|cmn|tst|ccmp|ccmn|b|bl|blr|br|cbz|cbnz|tbz|tbnz|ret|nop)/ || m ~ /^b\./) return ""
                return fam(arr[1])
            }
            if (m ~ /^(cmp|test|j|call|ret|push|nop|bt)/) return ""
            return arr[n] ~ /^%[a-z0-9]+$/ ? fam(arr[n]) : ""
        }
        function is_load(ins,    arr) {
            if (arch == "aarch64") return mnem(ins) ~ /^ld/
            return mnem(ins) ~ /^mov/ && split_ops(operands(ins), arr) >= 1 && arr[1] ~ /\(/
        }
        # True if family f was last written by a load between index j-1 and
        # the loop start address.
        function loaded(f, j, start,    k) {
            for (k = j - 1; k >= 1 && addr[k] >= start; k--)
                if (dst_fam(insn[k]) == f) return is_load(insn[k])
            return 0
        }
        function any_loaded(s, j, start,    regs, n, i) {
            n = reg_fams(s, regs)
            for (i = 1; i <= n; i++)
                if (loaded(regs[i], j, start)) return 1
            return 0
        }
        # Is the conditional branch at index i fed by data loaded in its loop?
        function data_fed(i, start,    m, j, mj, arr) {
            m = mnem(insn[i])
            if (arch == "aarch64" && m ~ /^(cbz|cbnz|tbz|tbnz)$/) {
                split_ops(operands(insn[i]), arr)
                return loaded(fam(arr[1]), i, start)
            }
            for (j = i - 1; j >= 1 && addr[j] >= start; j--) {
                mj = mnem(insn[j])
                if (arch == "aarch64") {
                    if (mj ~ /^(cmp|cmn|tst|ands|subs|adds|bics|ccmp|ccmn)$/)
                        return any_loaded(operands(insn[j]), j, start)
                    if (mj ~ /^(adcs|sbcs|negs|ngcs)$/) return 0
                    continue
                }
                if (mj ~ /^(mov|lea|nop|xchg|push|pop|cs|data16|cmov|set|j)/) continue
                if (mj !~ /^(cmp|test)/) return 0
                if (operands(insn[j]) ~ /\(/) return 1
                return any_loaded(operands(insn[j]), j, start)
            }
            return 0
        }
        # Does [lo, hi] read memory or make an indirect call?
        function hot(lo, hi,    i, m) {
            for (i = 1; i <= n; i++) {
                if (addr[i] < lo || addr[i] > hi) continue
                m = mnem(insn[i])
                if (insn[i] ~ indirect) return 1
                if (arch == "aarch64" && m ~ /^ld/) return 1
                if (arch != "aarch64" && m !~ /^(lea|nop|cs|data16)/ && operands(insn[i]) ~ /\(/) return 1
            }
            return 0
        }
        function flush(    i, l, best, k, lo, hi, data_branches, indirect_calls, inner, loop_branches) {
            if (!n) return
            data_branches = indirect_calls = inner = loop_branches = 0
            nb = 0
            for (i = 1; i <= n; i++) {
                if (insn[i] ~ indirect) indirect_calls++
                if (insn[i] !~ cond || !match(insn[i], /[0-9a-f]+ </)) continue
                nb++; bi[nb] = i; from[nb] = addr[i]
                dest[nb] = hex(substr(insn[i], RSTART, RLENGTH - 2))
            }
            for (k = 1; k <= nb; k++) {
                if (dest[k] <= from[k]) continue
                best = 0
                for (l = 1; l <= nb; l++)
                    if (dest[l] < from[l] && dest[l] <= from[k] && from[k] < from[l] && dest[k] <= from[l])
                        if (!best || from[l] - dest[l] < from[best] - dest[best]) best = l
                if (!best) continue
                loop_branches++
                if (data_fed(bi[k], dest[best])) data_branches++
            }
            best = 0
            for (l = 1; l <= nb; l++)
                if (dest[l] < from[l] && hot(dest[l], from[l]) &&
                    (!best || from[l] - dest[l] < from[best] - dest[best])) best = l
            if (best) {
                lo = dest[best]; hi = from[best]
                for (i = 1; i <= n; i++)
                    if (addr[i] >= lo && addr[i] <= hi && insn[i] ~ anybranch) inner++
            }
            printf "%s %d %d %d %d\n", name, data_branches, indirect_calls, inner, loop_branches
            n = 0
        }
        BEGIN {
            if (arch == "aarch64") {
                indirect = "^blr[ \t]"
                anybranch = "^(b|bl|blr|br|ret|cbz|cbnz|tbz|tbnz)([ \t]|$)|^b\\."
            } else {
                indirect = "^call[ \t]+\\*"
                anybranch = "^(j[a-z]*|call|ret)([ \t]|$)"
            }
        }
        $0 ~ re         { flush(); in_fn = 1; name = $2; gsub(/[<>:]/, "", name); next }
        in_fn && /^$/   { in_fn = 0; flush(); next }
        in_fn {
            a = $1; sub(/:$/, "", a)
            line = $0; sub(/^[^\t]*\t/, "", line); gsub(/[ \t]+/, " ", line)
            n++; addr[n] = hex(a); insn[n] = trim(line)
        }
        END { flush() }'
}

# Prints the number of distinct addresses of leaf_0 ... leaf_31, then how
# many of those symbols exist.
leaves() {
    nm "$bin" | awk '$3 ~ /^leaf_[0-9]+$/ { n++; if (!($1 in seen)) { seen[$1] = 1; d++ } }
                     END { printf "%d %d\n", d, n }'
}

if [ "$counts" -eq 1 ]; then
    missing=0
    for k in sum_threshold sum_branchless count_decisions dispatch_sequential dispatch_random correlate_distance; do
        line=$(analyze "$k" | awk 'NR == 1')
        if [ -z "$line" ]; then
            echo "check_kernels.sh: kernel $k not found" >&2; missing=1; continue
        fi
        set -- $line
        echo "$k $2 $3 $4"
    done
    exit $((missing * 2))
fi

fail=0

# check <label> <function regex> <mode> [N]
#   modes: data-branch N, no-data-branch, indirect-call N, loop-branch3
check() {
    label=$1 re=$2 mode=$3 want=${4:-}
    insns=$(body "$re")
    bad=0
    if [ -z "$insns" ]; then
        echo "FAIL $label: function not found"; fail=1; return 0
    fi
    # Per function name (clone suffixes stripped): its body count; across
    # bodies: the first data, indirect and loop count that differs from the
    # wanted one, else the first body's.  Each body is checked on its own.
    set -- $(analyze "$re" | awk -v mode="$mode" -v want="${want:-0}" '
        { base = $1; sub(/\..*/, "", base); if (++per[base] > bodies) bodies = per[base] }
        NR == 1 { d = $2; c = $3; l = $5 }
        mode == "data-branch"    && $2 != want { d = $2 }
        mode == "no-data-branch" && $2 != 0    { d = $2 }
        mode == "indirect-call"  && $3 != want { c = $3 }
        mode == "loop-branch3"   && $5 < 3     { l = $5 }
        END { print bodies + 0, d + 0, c + 0, l + 0 }')
    bodies=$1 data=$2 indirect=$3 loops=$4
    if [ "$bodies" -ne 1 ]; then
        echo "FAIL $label: $bodies bodies (clone or duplicate)"; bad=1
    fi
    if [ "$mode" != no-data-branch ] && printf '%s\n' "$insns" | grep -Eq "^$branchless"; then
        echo "FAIL $label: branchless instruction found (conditional move/select or flag arithmetic)"; bad=1
    fi
    if printf '%s\n' "$insns" | grep -Eq "$simd"; then
        echo "FAIL $label: SIMD registers used"; bad=1
    fi
    case $mode in
        data-branch)
            if [ "$data" -ne "$want" ]; then
                echo "FAIL $label: expected $want data branch(es), found $data"; bad=1
            fi ;;
        no-data-branch)
            if [ "$data" -ne 0 ]; then
                echo "FAIL $label: data-dependent branch found ($data)"; bad=1
            fi ;;
        indirect-call)
            if [ "$indirect" -ne "$want" ]; then
                echo "FAIL $label: expected $want indirect call(s), found $indirect"; bad=1
            fi ;;
        loop-branch3)
            if [ "$loops" -lt 3 ]; then
                echo "FAIL $label: fewer than three conditional branches inside the loops"; bad=1
            fi ;;
    esac
    if [ "$bad" -eq 0 ]; then echo "ok   $label"; else fail=1; fi
}

check "Test 1/4 sum_threshold"            'sum_threshold'             data-branch 1
check "Test 2/5 count_decisions"          'count_decisions'           data-branch 1
check "Test 4 sum_branchless"             'sum_branchless'            no-data-branch
check "Test 3 dispatch_sequential"        'dispatch_sequential'       indirect-call 1
check "Test 3 dispatch_random"            'dispatch_random'           indirect-call 1
check "Test 6 correlate_distance"         'correlate_distance'        loop-branch3

set -- $(leaves)
if [ "$2" -ne 32 ]; then
    echo "FAIL Test 3 leaves: found $2 of 32 leaves"; fail=1
elif [ "$1" -ne 32 ]; then
    echo "FAIL Test 3 leaves: $1 distinct addresses for 32 leaves"; fail=1
else
    echo "ok   Test 3 leaves"
fi

exit "$fail"
