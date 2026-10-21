#!/bin/sh
set -e
gcc -std=c99 -O2 -fno-tree-vectorize -fno-if-conversion -o branch_bench branch_bench.c
exec ./branch_bench "$@"
