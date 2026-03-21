# JIT Performance Investigation

**Date:** 2026-03-20
**Status:** Root cause found and fixed; optimization roadmap defined

## Summary

The JIT compiler introduced a **17,000x slowdown** on `u()` calls to
remote objects compared to the AST interpreter.  The root cause was
**re-entrant DBT execution**: when JIT code ECALLed into `u()`, the
nested `mux_exec()` re-entered `jit_eval()` and created a full DBT
context (~50ms setup/teardown) for the inner expression.  A simple
re-entrancy guard that falls back to the AST interpreter for nested
calls eliminated the bottleneck completely.

### Fix applied

`jit_compiler.cpp`: added `s_jit_depth` guard at `jit_eval()` entry.
When `s_jit_depth > 0`, return false (fall back to AST).  RAII guard
decrements on scope exit.

### Results after fix

| Function | Before | After | Speedup |
|---|---|---|---|
| `u(#21/bbtime)` | 51ms | 0.028ms | **1,821x** |
| `u(#21/valid_groups,#4,read)` | 87ms | 0.052ms | **1,673x** |
| `u(#21/FN_UNREAD_LIST,#4)` | 50ms | 0.005ms | **10,000x** |

## Test Environment

- **Database:** Production game snapshot (`netmux.0320-165246.sqlite`)
- **Player:** #4 (Stephen)
- **Tool:** `muxscript --readonly` against the snapshot
- **BBS:** Myrddin's Global BBS v4.0.6 (object #22, data pocket #21)
- **Jobs:** Job Global Object (object #24)

### How to reproduce

```sh
# Extract the game database
mkdir -p /tmp/realgame && cd /tmp/realgame
tar xzf ~/tinymux/netmux.0320-165246.tar.gz
cp data/netmux.0320-165246.sqlite data/netmux.sqlite
ln -s /home/sdennis/tinymux/mux/game/bin bin
cp /home/sdennis/tinymux/mux/game/alias.conf .
cp /home/sdennis/tinymux/mux/game/compat.conf .
mkdir -p text

# Build with JIT
cd ~/tinymux/mux
./configure --enable-realitylvls --enable-wodrealms --enable-jit
make clean && make && make install

# Benchmark
cd /tmp/realgame
echo 'think bbtime=[benchmark(u(#21/bbtime),100)]
think valid_groups=[benchmark(u(#21/valid_groups,#4,read),100)]' \
| ./bin/muxscript --readonly -g . -c netmux.conf -p 1

# Build WITHOUT JIT and compare
cd ~/tinymux/mux
./configure --enable-realitylvls --enable-wodrealms
make clean && make && make install

cd /tmp/realgame
rm -f data/netmux.sqlite-shm data/netmux.sqlite-wal
echo 'think bbtime=[benchmark(u(#21/bbtime),100)]
think valid_groups=[benchmark(u(#21/valid_groups,#4,read),100)]' \
| ./bin/muxscript --readonly -g . -c netmux.conf -p 1
```

## Benchmark Results

All times are for 100 iterations via `benchmark()`.

### With `--enable-jit`

| Expression | 100 calls | Per call | Notes |
|---|---|---|---|
| `add(1,2)` | 0.002s | 0.02ms | Builtin baseline |
| `extract(time(),1,3)` | 0.010s | 0.10ms | Builtin inline |
| `u(me/testfn)` [add(1,2)] | 0.010s | 0.10ms | u() on self — fast |
| `u(#21/fn_extract)` [new attr] | 0.001s | 0.01ms | u() remote, no JIT cache |
| `get(#21/bbtime)` | 0.010s | 0.10ms | Raw fetch — fast |
| `u(#21/get_group,1)` | 0.050s | 0.50ms | u() remote, pre-existing |
| `u(#21/FN_UNREAD_LIST,#4)` | 5.07s | **50ms** | u() remote, pre-existing |
| `u(#21/bbtime)` | 5.10s | **51ms** | u() remote, pre-existing |
| `u(#21/valid_groups,#4,read)` | 8.68s | **87ms** | u() remote, pre-existing |

### Without `--enable-jit`

| Expression | 100 calls | Per call |
|---|---|---|
| `u(#21/bbtime)` | 0.0003s | **0.003ms** |
| `u(#21/fn_extract)` | 0.0001s | 0.001ms |
| `u(#21/valid_groups,#4,read)` | 0.004s | **0.04ms** |

### Slowdown factor (JIT vs no-JIT)

| Function | JIT | No JIT | Factor |
|---|---|---|---|
| `u(#21/bbtime)` | 51ms | 0.003ms | **17,000x** |
| `u(#21/valid_groups,#4,read)` | 87ms | 0.04ms | **2,175x** |
| `u(#21/fn_extract)` [new attr] | 0.01ms | 0.001ms | 10x |

## Key Observations

### 1. Only pre-existing attributes are slow

Freshly-created attributes with **identical bodies** (`extract(time(),
1, 3)`) are fast even with JIT enabled.  The slow attrs have cached
JIT blobs in the SQLite database from prior game runs.

### 2. The JIT cache IS being hit

`jitstats()` after benchmarking shows:
```
cache_hit_mem=19  cache_hit_sqlite=2  cache_miss=3
compile_ok=3  compile_fail=0
```
The compiled code is found in cache and reused.  The 51ms cost is in
**executing the cached blob**, not in compiling it.

### 3. `get()` is fast, `u()` is slow

`get(#21/bbtime)` = 0.1ms (just fetches the raw text).
`u(#21/bbtime)` = 51ms (fetches, parses, evaluates).
The bottleneck is in the parse/eval path when JIT is active.

### 4. `u(me/...)` is fast, `u(#21/...)` is slow

Same function body, same JIT build.  `u()` on self = 0.1ms,
`u()` on a different object = 51ms.  The cross-object path
does something the self path doesn't.

### 5. Real-world impact

Every `+bbread` calls `valid_groups` at least once (87ms).
`+bbscan` calls it plus iterates groups calling `FN_UNREAD_LIST`
(50ms each).  A simple BBS operation that should take <1ms takes
200-400ms — noticeable as a hang on a live game.

## Architecture Context

The JIT pipeline (from `docs/design-jit-compiler.md` and memory notes):
```
softcode → AST → HIR → SSA → optimize → RV64 codegen → x86-64 DBT
```

- **Tier 1**: AST interpreter (always available)
- **Tier 2**: JIT-compiled native code (RV64 → x86-64 DBT)
- Compiled blobs are cached in SQLite (`jit_cache` table?)
- LRU parse cache (1024 entries) for AST trees
- `ecalls` in jitstats = calls back into the engine from JIT code

## Investigation Areas

### A. SQLite blob load cost — RULED OUT
Clearing the entire `code_cache` table and re-running benchmarks
produced identical 51ms results.  Fresh compilation is equally
slow.  The cost is in execution, not cache loading.

### B. DBT interpretation overhead — RULED OUT
Small functions (`add(1,2)`) execute in 0.02ms through the DBT.
The DBT itself is fast.  The cost is specific to nested execution.

### C. ECALL overhead — PARTIALLY CORRECT
ECALLs themselves are cheap.  But ECALLs into functions that call
`mux_exec()` (like `u()`, `iter()`, `switch()`) trigger nested
`jit_eval()` re-entry, which is catastrophically expensive.  The
ECALL count correlates with slowness because more ECALLs = more
re-entry opportunities.

### D. Self vs remote object path — EXPLAINED
`u(me/...)` appeared fast because muxscript's `&attr obj=value`
evaluates the value before storing.  All "self" test attrs had
literal bodies (no function calls), so the inner `mux_exec()`
never triggered JIT.  The self-vs-remote difference was an
artifact of the test methodology, not a real code path difference.

### E. Stale cache entries — EXPLAINED
Same artifact.  Freshly-created attrs had evaluated (literal)
bodies that don't trigger inner JIT.  Pre-existing attrs had
real function call bodies that trigger nested JIT.  The "stale"
appearance was because only pre-existing attrs had real softcode.

### Root Cause: Nested DBT execution
The actual bottleneck:

```
Outer mux_exec("u(#21/bbtime)")
  → jit_eval() → compile_cached() → run_cached_program()
    → DBT starts, executes RV64 code
      → ECALL into fun_u()         [~0.001ms]
        → fetch attr body "extract(time(), 1, 3)"
        → mux_exec(body)
          → jit_eval()              [RE-ENTRANT!]
            → compile_cached()      [cache hit, ~0.001ms]
            → run_cached_program()  [FULL DBT SETUP: ~50ms]
              → DBT starts AGAIN
              → ECALL into extract() / time()
              → DBT exits
            ← result
          ← result
        ← result
      ← ECALL returns
    → DBT exits
  ← result
```

`run_cached_program()` allocates/resets guest memory, initializes
the DBT context, and runs the RV64→x86-64 translator.  This
~50ms cost is acceptable for a top-level expression but
catastrophic when paid per-ECALL in nested evaluation.

**Fix**: `s_jit_depth` guard at `jit_eval()` entry.  Nested calls
fall back to AST interpreter (~0.003ms instead of ~50ms).

## BBS Object Map

For reference when profiling:

| Dbref | Name | Role |
|---|---|---|
| #2 | Master Room | Contains all global objects |
| #21 | bbpocket | BBS data store + functions |
| #22 | BBS - Myrddin's v4.0.6 | BBS command handler ($-commands) |
| #23 | (group) | BBS Group 1 |
| #24 | Job Global Object | +jobs command handler |
| #44 | (group) | BBS Group 2 |
| #45 | (group) | BBS Group 3 (wizard-only) |

### Key BBS softcode on #21

| Attribute | Body | Per-call (JIT) |
|---|---|---|
| `bbtime` | `extract(time(), 1, 3)` | 51ms → 0.028ms |
| `get_group` | `switch(isnum(%0),1,extract(v(groups),%0,1),locate(#21,%0,ni))` | 0.5ms → 0.052ms |
| `valid_groups` | `iter(v(groups),switch(and(u(##/can%1,%0),...),1,##))` | 87ms → 0.052ms |
| `fn_unread_list` | `sort(iter(setdiff(get(%1/mess_lst),...),member(...,##)))` | 50ms → 0.005ms |
| `fn_msg_flags` | `switch([member(get(%0/bb_read),%2)]:...,0:*,U,*:1,T)` | (not measured) |

## Optimization Roadmap

The re-entrancy guard is the immediate fix.  The following optimizations
can further reduce ECALL overhead, listed from easiest to hardest:

### Level 1: Re-entrancy guard (DONE)

**Cost:** 1 line of code.  **Benefit:** 1,000-10,000x on nested calls.

When `jit_eval()` is entered recursively (via ECALL → `mux_exec()`),
fall back to the AST interpreter.  The AST evaluator handles the inner
expression in ~0.003ms vs the DBT's ~50ms context setup.

Every avoided re-entrant `jit_eval()` saves ~50ms.  This is the
safety net that makes all other optimizations optional rather than
urgent.

### Level 2: Expand Tier 2 coverage

**Cost:** Medium — add more co_* wrappers and builtin implementations.
**Benefit:** Eliminates ECALLs for covered functions.

Tier 2 functions (`co_first`, `co_rest`, `add`, `mul`, etc.) are
pre-compiled RV64 blobs called via JAL — no ECALL, no host transition.
Extending Tier 2 to cover more builtins (`time()`, `secs()`, `name()`,
`get()`, `member()`, etc.) eliminates the ECALL entirely for those
calls.  The inner expression stays in guest code, no re-entry question.

**Priority targets** (most-used in BBS/Jobs softcode):
- `time()`, `secs()` — trivial, no side effects
- `get()`, `xget()` — attribute fetch (needs guest↔host string I/O)
- `name()`, `owner()`, `flags()` — object metadata
- `member()`, `words()`, `extract()` — list operations

### Level 3: u() inlining at compile time

**Cost:** High — requires attr resolution at compile time + cache
invalidation.  **Benefit:** Eliminates the ECALL for `u()` entirely.

When the JIT compiles `u(#21/bbtime)`, it can:
1. Resolve `#21/bbtime` at compile time
2. Fetch the body: `extract(time(), 1, 3)`
3. Compile the body inline into the caller's RV64 code
4. The `u()` call becomes a JAL into the inlined code

The hard part is cache invalidation: if `#21/bbtime` is modified
after compilation, the cached blob is stale.  Solutions:
- Version stamp per attr, checked at call site (cheap guard)
- Invalidate cache entries that reference the modified attr
- Accept staleness and rely on periodic cache flush

### Level 4: Persistent/shared DBT context

**Cost:** Very high — architectural change to DBT lifecycle.
**Benefit:** Makes re-entrant JIT execution cheap.

Instead of allocating a fresh 1MB guest memory + DBT state per
`run_cached_program()` call, maintain a persistent DBT context
that nested calls can reuse.  Guest stack frames would be pushed
for each nested entry, similar to how native function calls work.

This would allow the re-entrancy guard to be lifted entirely —
nested `jit_eval()` would be as fast as top-level.  But it
requires rethinking guest memory layout, stack management, and
the DBT block cache lifecycle.

### Measurement methodology

All measurements use `muxscript --readonly` against the production
database snapshot.  This gives repeatable, isolated results without
running a live game server.

```sh
# Basic benchmark pattern
echo 'think result=[benchmark(u(#21/attr,args),N)]' \
| ./bin/muxscript --readonly -g /tmp/realgame -c netmux.conf -p 1

# Compare JIT vs no-JIT by rebuilding:
# --enable-jit:  ./configure --enable-realitylvls --enable-wodrealms --enable-jit
# no JIT:        ./configure --enable-realitylvls --enable-wodrealms

# JIT stats after benchmarking:
echo 'think [jitstats()]' \
| ./bin/muxscript --readonly -g /tmp/realgame -c netmux.conf -p 1
```
