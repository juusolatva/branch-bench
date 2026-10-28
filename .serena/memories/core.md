# branch_bench — core

Single-file C99 microbenchmark measuring branch-misprediction cost. `AGENTS.md` (imported by `CLAUDE.md`) is the only full guide: build flags, CLI, verification, CI, conventions 1–8, TODO. Read it first; don't copy its rules into memories, so the two can't drift.

## Serena tool notes (not in AGENTS.md)
- On a Windows checkout Serena's clangd parses `branch_bench.c` without `__linux__` (no `HAVE_PERF`): `perf_add`, `perf_fd`, `cpuinfo_model_value` etc. are invisible, and `find_symbol('perf_init')`/`perf_stop` return the empty `#else` stubs, so symbol edits would hit the stubs. Edit perf and cpuinfo code with plain file tools (`replace_content`, Read/Edit), or run Serena from Linux/WSL.
- Test 3 leaves exist only via macro expansion: symbol tools show `FOR_EACH_LEAF/leaf_N` located on the `FOR_EACH_LEAF(DEF_LEAF)` line. Edit the `DEF_LEAF`/`FOR_EACH_LEAF` text, never `leaf_N` symbols.
