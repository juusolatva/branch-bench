#!/bin/sh
set -e
flags="-std=c99 -O2 -fno-tree-vectorize -fno-if-conversion"
# x86-64: keep every jump in the kernels off 32-byte boundaries, so Intel's
# JCC-erratum microcode cannot slow some kernels and not others, and timings
# don't depend on where edits elsewhere in the file move the kernels.
case $(uname -m) in
    x86_64 | amd64)
        flags="$flags -Wa,-mbranches-within-32B-boundaries,-malign-branch=jcc+fused+jmp+call+ret+indirect" ;;
esac
commit=$(git describe --always --dirty 2>/dev/null) || commit=""
# shellcheck disable=SC2086  # $flags is meant to split into words
gcc $flags -DBUILD_FLAGS="\"$flags\"" -DBUILD_COMMIT="\"$commit\"" -o branch_bench branch_bench.c
exec ./branch_bench "$@"
