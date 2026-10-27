#!/bin/sh
# Lists jumps in the benchmark kernels that cross or end on a 32-byte
# boundary.  Intel's JCC-erratum microcode (Skylake-derived CPUs) slows such
# jumps; a macro-fused cmp/test/add/sub/and/inc/dec + jcc pair counts as one
# jump.  A hit inside a kernel's hot loop can change timings; hits in setup
# code outside the loop do not matter.  Layout depends on the compiler, so
# this is a local check, not a CI one.  x86-64 only.
#
# Usage: check_jcc.sh <binary>
# Prints one line per hit (function, address, instruction) and exits 0 with or
# without hits; exits non-zero if objdump fails.
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
        pop = ""; next
    }
    fn ~ /^<(sum_threshold|sum_branch|sum_branchless|count_decisions|dispatch_sequential|dispatch_random)>$/ && NF >= 3 {
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
    }'
