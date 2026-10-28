#!/bin/sh
# Lists jumps in the benchmark kernels that cross or end on a 32-byte
# boundary.  Intel's JCC-erratum microcode (Skylake-derived CPUs) slows such
# jumps; a macro-fused cmp/test/add/sub/and/inc/dec + jcc pair counts as one
# jump.  A hit inside a kernel's hot loop can change timings; hits in setup
# code outside the loop do not matter.  Builds with run.sh's alignment flags
# (-Wa,-mbranches-within-32B-boundaries,...) have no hits at all, and CI's
# x86-64 gcc job fails on any.  x86-64 only.
#
# Usage: check_jcc.sh <binary>
# Prints one line per hit (function, address, instruction) and exits 0 with or
# without hits; exits non-zero if objdump fails or a kernel is missing, so an
# empty listing always means the kernels were checked.  GCC clone suffixes
# such as .constprop.0 are accepted; GCC folds sum_branch and sum_threshold
# into one body under either name, so one of the two suffices.
# OBJDUMP overrides the objdump binary.
set -eu

bin=${1:?usage: check_jcc.sh <binary>}

disassembly=$(${OBJDUMP:-objdump} -dw "$bin")
printf '%s\n' "$disassembly" | awk -F'\t' '
    function hex(s,    i, n) {
        n = 0
        for (i = 1; i <= length(s); i++)
            n = n * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
        return n
    }
    /^[0-9a-f]+ </ {
        fn = $0; sub(/^[0-9a-f]+ /, "", fn); sub(/:$/, "", fn)
        base = fn; sub(/^</, "", base); sub(/[.>].*$/, "", base)
        kernel = fn ~ /^<(sum_threshold|sum_branch|sum_branchless|count_decisions|dispatch_sequential|dispatch_random)(\.[^>]*)?>$/
        if (kernel) seen[base == "sum_branch" ? "sum_threshold" : base] = 1
        pop = ""; next
    }
    kernel && NF >= 3 {
        addr = $1; gsub(/[ :]/, "", addr)
        s = hex(addr); e = s + split($2, bytes, " ")
        split($3, w, " "); op = w[1]
        if (op ~ /^(j|call|ret)/) {
            f = s
            if (op ~ /^j/ && op != "jmp" && pop ~ /^(cmp|test|add|sub|and|inc|dec)/ && pe == s)
                f = ps
            if (int(f / 32) != int((e - 1) / 32) || e % 32 == 0)
                printf "%s %x: %s\n", fn, s, $3
        }
        ps = s; pe = e; pop = op
    }
    END {
        n = split("sum_threshold sum_branchless count_decisions dispatch_sequential dispatch_random", want, " ")
        for (i = 1; i <= n; i++)
            if (!(want[i] in seen)) {
                printf "check_jcc.sh: kernel %s not found\n", want[i] > "/dev/stderr"
                missing = 1
            }
        exit missing ? 2 : 0
    }'
