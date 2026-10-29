#!/bin/sh
# Checks that the benchmark kernels in a GCC build still contain the code
# the benchmark measures.  Whether it survives depends on the compiler
# version (for the C kernels) or on the asm block staying single (for the
# assembly kernels), and losing it silently invalidates results.  x86-64
# and aarch64 only.
#
#   Test 1/4 sum_threshold, Test 2 count_decisions:
#       exactly one data branch, no branchless instruction (conditional
#       move/select, flag arithmetic such as setcc/sbb/adc), no SIMD.
#   Test 4 sum_branchless: no data branch (cmov/setcc allowed), no SIMD.
#   Test 3 dispatch_indexed: exactly one indirect call.
#   Test 3 leaves: leaf_0 ... leaf_63 at 64 distinct addresses, all with the
#       same instruction shape (padding, prefixes, comments and immediates
#       ignored), so every target does the same work.  A retpoline or
#       indirect-thunk build turns the call into a direct call to a thunk,
#       which the indirect-call check rejects by design.
#   Test 5 pattern_sites_N (N = 1, 4, 16, 64, 256, 512): N data branches,
#       whose loads read offsets 0 ... N-1 of the block, each once, and a
#       block pointer advanced by N (mode sites), so every site reads its
#       own stream.  A build without pattern_sites_4 but with
#       pattern_sites_1 is a C-kernel build (only N = 1); N > 1 is skipped.
#   Test 6 loop_exit, loop_exit_body2: two nested loops whose inner loop
#       has exactly one conditional branch (its back-edge, the branch every
#       trip count uses) and no memory access or call (mode loop-exit).
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
# "name data_branches indirect_calls inner_loop_branches back_edges
# innermost_cond innermost_hot".  inner_loop_branches counts the branch,
# call and return instructions of the hot loop: the innermost loop that
# reads memory or makes an indirect call (so not, e.g., GCC's empty loop
# for n == 0), else of the innermost loop.  back_edges counts backward
# conditional branches; innermost_cond the conditional branches of the
# innermost loop and innermost_hot whether it reads memory or calls
# (mode loop-exit).
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
        function flush(    i, l, best, k, lo, hi, data_branches, indirect_calls, inner, back, small, icond, ihot) {
            if (!n) return
            data_branches = indirect_calls = inner = back = icond = ihot = 0
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
                if (data_fed(bi[k], dest[best])) data_branches++
            }
            small = 0
            for (l = 1; l <= nb; l++) {
                if (dest[l] >= from[l]) continue
                back++
                if (!small || from[l] - dest[l] < from[small] - dest[small]) small = l
            }
            if (small) {
                for (i = 1; i <= n; i++)
                    if (addr[i] >= dest[small] && addr[i] <= from[small] && insn[i] ~ cond) icond++
                ihot = hot(dest[small], from[small])
            }
            best = 0
            for (l = 1; l <= nb; l++)
                if (dest[l] < from[l] && hot(dest[l], from[l]) &&
                    (!best || from[l] - dest[l] < from[best] - dest[best])) best = l
            if (!best) best = small
            if (best) {
                lo = dest[best]; hi = from[best]
                for (i = 1; i <= n; i++)
                    if (addr[i] >= lo && addr[i] <= hi && insn[i] ~ anybranch) inner++
            }
            printf "%s %d %d %d %d %d %d\n", name, data_branches, indirect_calls, inner, back, icond, ihot
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

# Prints the number of distinct addresses of leaf_0 ... leaf_63, then how
# many of those symbols exist.
leaves() {
    nm "$bin" | awk '$3 ~ /^leaf_[0-9]+$/ { n++; if (!($1 in seen)) { seen[$1] = 1; d++ } }
                     END { printf "%d %d\n", d, n }'
}

# Prints how many distinct instruction shapes the leaves leaf_0 ... leaf_63
# have: padding lines (nop, xchg %ax,%ax, data16 ... nop, which objdump
# lists after ret up to the next symbol) dropped, cs/ds/data16 prefixes
# (run.sh's ret alignment) and objdump comments ("# ..." on x86-64,
# "// ..." on aarch64, both after whitespace) removed, and immediates
# masked ($0x..., #0x..., #16), which also masks aarch64 shift and rotate
# amounts.  An empty shape counts as a failure.
leaf_shapes() {
    printf '%s\n' "$disassembly" | awk '
        /^[0-9a-f]+ <leaf_[0-9]+>:$/ { name = $2; in_leaf = 1; shape[name] = ""; next }
        in_leaf && /^$/              { in_leaf = 0; next }
        in_leaf {
            line = $0; sub(/^[^\t]*\t/, "", line)
            sub(/[ \t]+(# |\/\/).*$/, "", line)
            gsub(/[ \t]+/, " ", line); sub(/^ /, "", line); sub(/ $/, "", line)
            while (line ~ /^(cs|ds|data16) /) sub(/^[a-z0-9]+ /, "", line)
            if (line ~ /^(nop|xchg %ax,%ax)/ || line == "") next
            gsub(/\$?0x[0-9a-f]+|#(0x)?[0-9a-f]+/, "IMM", line)
            shape[name] = shape[name] line ";"
        }
        END {
            for (k in shape) {
                if (shape[k] == "") empty = 1
                if (!(shape[k] in seen)) { seen[shape[k]] = 1; d++ }
            }
            print empty ? 0 : d + 0
        }'
}

# Prints the data-load offsets of one pattern kernel in order (comma list,
# decimal), then the immediate added to their base register (the block
# advance), or "none".
site_info() {
    body "$1" | awk -v arch="$arch" '
        function num(s) { return s ~ /^0x/ ? strtonum_(substr(s, 3)) : s + 0 }
        function strtonum_(h,    i, n) {
            n = 0
            for (i = 1; i <= length(h); i++) n = n * 16 + index("0123456789abcdef", substr(h, i, 1)) - 1
            return n
        }
        {
            line = $0; gsub(/[ \t]+/, " ", line); sub(/^ /, "", line)
            while (line ~ /^(cs|ds|data16) /) sub(/^[a-z0-9]+ /, "", line)   # alignment prefixes
            if (arch == "aarch64" && line ~ /^ldrb /) {
                m = line; sub(/^[^[]*\[/, "", m); sub(/\].*$/, "", m)
                split(m, part, /, */); base = part[1]
                off = part[2] == "" ? 0 : num(substr(part[2], 2))
                list = list (list == "" ? "" : ",") off
            } else if (arch != "aarch64" && line ~ /^cmpb \$0x0,/) {
                m = line; sub(/^cmpb \$0x0,/, "", m)
                off = m; sub(/\(.*$/, "", off); off = off == "" ? 0 : num(off)
                base = m; sub(/^[^(]*\(/, "", base); sub(/\).*$/, "", base)
                list = list (list == "" ? "" : ",") off
            } else {
                adds[++na] = line
            }
        }
        END {
            adv = "none"
            for (i = 1; i <= na; i++) {
                if (arch == "aarch64" && adds[i] ~ ("^add " base ", " base ", #")) {
                    v = adds[i]; sub(/^.*#/, "", v); adv = num(v)
                }
                if (arch != "aarch64" && adds[i] ~ ("^add \\$0x[0-9a-f]+," base "$")) {
                    v = adds[i]; sub(/^add \$/, "", v); sub(/,.*$/, "", v); adv = num(v)
                }
            }
            print (list == "" ? "none" : list), adv
        }'
}

# 1 if this is a C-kernel build: pattern_sites_1 without pattern_sites_4.
c_kernel_build() {
    [ -n "$(body pattern_sites_1)" ] && [ -z "$(body pattern_sites_4)" ]
}

pattern_kernels="pattern_sites_1 pattern_sites_4 pattern_sites_16 pattern_sites_64 pattern_sites_256 pattern_sites_512"

if [ "$counts" -eq 1 ]; then
    missing=0
    c_build=0
    if c_kernel_build; then c_build=1; fi
    for k in sum_threshold sum_branchless count_decisions dispatch_indexed $pattern_kernels loop_exit loop_exit_body2; do
        if [ "$c_build" -eq 1 ] && [ "${k#pattern_sites_}" != "$k" ] && [ "$k" != pattern_sites_1 ]; then
            continue
        fi
        # The body with the exact name, else the first clone.
        line=$(analyze "$k" | awk -v k="$k" '$1 == k { print; found = 1; exit }
                                          !first { first = $0 }
                                          END { if (!found && first) print first }')
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
#   modes: data-branch N, no-data-branch, indirect-call N, loop-exit, sites N
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
        NR == 1 { d = $2; c = $3; b = $5; ic = $6; ih = $7 }
        (mode == "data-branch" || mode == "sites") && $2 != want { d = $2 }
        mode == "no-data-branch" && $2 != 0    { d = $2 }
        mode == "indirect-call"  && $3 != want { c = $3 }
        mode == "loop-exit" && ($5 != 2 || $6 != 1 || $7 != 0) { b = $5; ic = $6; ih = $7 }
        END { print bodies + 0, d + 0, c + 0, b + 0, ic + 0, ih + 0 }')
    bodies=$1 data=$2 indirect=$3 back=$4 icond=$5 ihot=$6
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
        sites)
            set -- $(site_info "$re")
            expected=$(awk -v n="$want" 'BEGIN { for (i = 0; i < n; i++) printf "%s%d", i ? "," : "", i }')
            sorted=$(printf '%s\n' "$1" | tr , '\n' | sort -n | paste -sd, -)
            if [ "$data" -ne "$want" ]; then
                echo "FAIL $label: expected $want data branch(es), found $data"; bad=1
            elif [ "$sorted" != "$expected" ]; then
                echo "FAIL $label: site offsets $1, expected 0..$((want - 1)) once each"; bad=1
            elif [ "$2" != "$want" ]; then
                echo "FAIL $label: block pointer advanced by $2, expected $want"; bad=1
            fi ;;
        no-data-branch)
            if [ "$data" -ne 0 ]; then
                echo "FAIL $label: data-dependent branch found ($data)"; bad=1
            fi ;;
        indirect-call)
            if [ "$indirect" -ne "$want" ]; then
                echo "FAIL $label: expected $want indirect call(s), found $indirect"; bad=1
            fi ;;
        loop-exit)
            if [ "$back" -ne 2 ]; then
                echo "FAIL $label: expected 2 nested loops, found $back backward branch(es)"; bad=1
            elif [ "$icond" -ne 1 ]; then
                echo "FAIL $label: expected 1 conditional branch in the inner loop, found $icond"; bad=1
            elif [ "$ihot" -ne 0 ]; then
                echo "FAIL $label: the inner loop reads memory or calls"; bad=1
            fi ;;
    esac
    if [ "$bad" -eq 0 ]; then echo "ok   $label"; else fail=1; fi
}

check "Test 1/4 sum_threshold"            'sum_threshold'             data-branch 1
check "Test 2 count_decisions"            'count_decisions'           data-branch 1
check "Test 4 sum_branchless"             'sum_branchless'            no-data-branch
check "Test 3 dispatch_indexed"           'dispatch_indexed'          indirect-call 1
if c_kernel_build; then
    check "Test 5 pattern_sites_1"        'pattern_sites_1'           sites 1
    echo "skip Test 5 pattern_sites_4-512: C-kernel build (only N = 1)"
else
    for k in $pattern_kernels; do
        check "Test 5 $k" "$k" sites "${k#pattern_sites_}"
    done
fi
check "Test 6 loop_exit"                  'loop_exit'                 loop-exit
check "Test 6 loop_exit_body2"            'loop_exit_body2'           loop-exit

set -- $(leaves)
shapes=$(leaf_shapes)
if [ "$2" -ne 64 ]; then
    echo "FAIL Test 3 leaves: found $2 of 64 leaves"; fail=1
elif [ "$1" -ne 64 ]; then
    echo "FAIL Test 3 leaves: $1 distinct addresses for 64 leaves"; fail=1
elif [ "$shapes" -ne 1 ]; then
    echo "FAIL Test 3 leaves: $shapes instruction shapes for 64 leaves"; fail=1
else
    echo "ok   Test 3 leaves"
fi

exit "$fail"
