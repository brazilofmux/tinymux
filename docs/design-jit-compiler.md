# JIT Compilation via RISC-V and Dynamic Binary Translation

## Status: Stage 4 In Progress (Stages 1-3 Substantially Complete)

Branch: `brazil`

Predecessors: `docs/PARSER.md` (study), `docs/PARSER_REPLACE.md` (AST evaluator)

External work: `~/riscv` (DBT runtime), `~/slow-32` (ISA design + toolchain history)

**As of 2026-03-11**: 585 smoke tests (84 rveval + 34 benchmarks), 5,035 lines
across 4 compiler files (+ DBT runtime). Full SSA compiler pipeline operational:
AST → HIR → SSA → optimize → linear-scan regalloc → RV64 → x86-64 JIT.
Loop compilation (iter) with SSA back-edges and PHI nodes. Tier 2 blob
(cat/strlen/strcat) working via JAL. switch/case compilation via branch chains.

## Motivation

The AST evaluator (complete in brazil) eliminated the text-level
replacement-and-reparse loop. Every softcode expression is now parsed
once into a typed AST and cached via an LRU parse cache. This is
faster than the classic `mux_exec()` stream transformer, but still
interpreted — each eval walks the tree, dispatches on node type, and
calls into the engine.

The next step is to compile softcode to native machine code. The key
insight: RISC-V is already the clean, 3-address, register-rich ISA we
would design if we were inventing a bytecode from scratch. By targeting
real RISC-V machine code and using Dynamic Binary Translation (DBT) on
non-RISC-V hosts, we get:

 - One compiler backend (RISC-V), not three (x86, ARM, RISC-V).
 - Native execution on RISC-V hosts (no translation overhead).
 - ~70% of native speed on other hosts via DBT with block/superblock
   translation, register caching, and peephole optimization.
 - A portable binary cache — compiled code stored in SQLite travels
   with the database across architectures.

## The Type Problem

MUX softcode has exactly one data type: string. `add(1,2)` takes two
strings, parses them to numbers, adds, converts the result back to a
string. `#123` is four characters. A list is a string with spaces in
it.

The compiler's job is not to optimize arithmetic — it's to figure out
when intermediate values can **avoid being strings**. If `add(mul(%q0,2),1)`
can keep the intermediate values as integers in machine registers and
only convert to string at the final output boundary, that eliminates
two `mux_atol()` calls, two `mux_ltoa()` calls, and several string
allocations.

### Type Lattice

The SSA IR uses a type lattice for each value:

```
         unknown
        /   |   \
    int64  f64  dbref
        \   |   /
         string
```

 - **Bottom-up (production)**: Each function has a known output type.
   `add()` produces int64. `fdiv()` produces f64. `name()` produces
   string. Literals are typed at parse time (`42` is int64, `3.14`
   is f64, `#123` is dbref, anything else is string).

 - **Top-down (consumption)**: Each value's type is constrained by
   how it is consumed. If the result of `add()` feeds into another
   `add()`, it stays int64. If it feeds into `cat()` or becomes final
   output, it must materialize as string.

 - **Narrowing**: The compiler intersects production and consumption
   types. If `add()` produces int64 and its consumer needs int64, no
   conversion. If the consumer needs string, insert a `to_string`
   conversion at the edge.

This is not traditional type inference — it's type *avoidance*. The
goal is to keep values in machine representation as long as possible
and only pay the string conversion cost at boundaries.

### Representation at Runtime

 - **int64**: Machine register, 64-bit signed integer.
 - **f64**: Floating-point register, IEEE 754 double.
 - **dbref**: Machine register, 32-bit signed integer (same as int64
   in registers, but semantically distinct for validation).
 - **string**: Opaque handle (pointer + length). Reference counted.
   Engine-managed allocation.

### Where Conversions Happen

Conversions are explicit SSA operations inserted by the compiler:

 - `int_to_string(r1) -> r2` — format int64 as decimal string
 - `float_to_string(r1) -> r2` — format f64 per `g_float_precision`
 - `string_to_int(r1) -> r2` — parse, zero on failure (MUX semantics)
 - `string_to_float(r1) -> r2` — parse, zero on failure
 - `dbref_to_string(r1) -> r2` — format as `#NNN`

The optimizer's job is to eliminate as many of these conversions as
possible by proving that producer and consumer agree on type.

## Multi-Language Support

The compilation pipeline is language-agnostic below the AST level.
Different source languages produce different ASTs but lower to the
same SSA IR, target the same RISC-V backend, and run in the same
sandbox.

### Lua as a Second Language

Lua is a natural candidate:

 - Already has real types (number, string, boolean, table, nil) — the
   type inference problem is simpler than for MUXcode.
 - Well-understood compilation (LuaJIT exists as a reference, though
   we are not embedding it).
 - Tables provide data structures that MUXcode lacks.
 - Same sandbox constraint: a Lua function calling `set(obj, attr, value)`
   goes through the same engine API as MUXcode.

The Lua frontend produces SSA IR with richer type information than
MUXcode (Lua variables have declared types or can be inferred from
usage). This means Lua code compiles more efficiently — fewer
speculative conversions, more values stay in machine types.

### Shared Infrastructure

```
MUXcode source ----> MUX AST ----\
                                  +---> SSA IR ---> RISC-V ---> SQLite cache
Lua source --------> Lua AST ---/                      |
                                                        v
                                                   DBT runtime
```

Both languages:
 - Share the same SSA optimization passes
 - Share the same RISC-V code generator
 - Share the same SQLite code cache
 - Share the same DBT runtime
 - Share the same engine API (function pointer table)
 - Run in the same sandbox with the same side-effect ordering

## Sandbox Contract

The compiled path must be **observationally equivalent** to the
interpreted path. This is the hard constraint that governs everything.

### What "Equivalent" Means

For any softcode expression, given the same game state:

 1. The compiled version produces the **same output string**.
 2. The compiled version causes the **same mutations** to game state
    (attributes, flags, object locations, pennies, queue entries,
    mail, channels) in the **same order**.
 3. The compiled version has the **same side-effect visibility** —
    if interpreted MUXcode can observe a `set()` from a preceding
    statement, compiled code must too.
 4. The compiled version respects the **same permission checks** —
    `controls()`, `can_see()`, lock evaluation all go through the
    engine, not reimplemented in compiled code.

### How the Sandbox is Enforced

 - All game state mutations go through the engine API. Compiled code
   cannot directly modify `db[]`, attribute storage, or descriptor
   state. It calls into the engine via the function pointer table.
 - The function pointer table is the **only** interface between
   compiled code and the game engine. It is set up at invocation
   time and contains pointers to the same functions the interpreter
   calls.
 - Compiled code runs with the same `executor`/`caller`/`enactor`
   context as interpreted code. These are passed as parameters.
 - String allocation/deallocation goes through the engine's allocator,
   not `malloc`. This ensures buffer pools and leak detection work
   the same way.
 - The DBT runtime enforces memory isolation — compiled code cannot
   access arbitrary memory outside its sandbox (stack, registers,
   and engine API calls only).

## Tiered Caching Strategy

Not all softcode is worth compiling. The caching strategy has three
tiers based on execution frequency:

### Tier 0: Interpret Only

 - Code executed fewer than N times (configurable threshold, e.g. 8).
 - Uses the existing AST evaluator with LRU parse cache.
 - An execution counter on each parse cache entry tracks invocations.
 - Zero compilation overhead. This is the right tier for:
   - One-off commands (`think`, `@pemit`, interactive use)
   - Rarely-triggered `@startup` or `@daily` code
   - Code being actively developed (`@edit` cycles)

### Tier 1: Memory-Cached Compilation

 - Code that crosses the execution threshold.
 - Compiled to RISC-V, stored in an in-memory cache (not SQLite).
 - Lost on `@restart` — recompiled on demand after restart.
 - Appropriate for moderately hot code where compilation pays off
   within a single server session but isn't worth persisting.
 - Memory cache has a size limit; LRU eviction when full.

### Tier 2: SQLite-Persisted Compilation

 - Code that remains hot across multiple server sessions.
 - Promotion from Tier 1 after sustained use (e.g., still in memory
   cache after K eviction sweeps, or accessed M times total).
 - Compiled RISC-V binary stored in SQLite, keyed by source text
   hash (SHA-1 or similar).
 - Survives `@restart`, `@shutdown`/start cycles, and database
   migration across architectures.
 - The fully-compiled database: over time, all frequently-used
   softcode migrates to Tier 2. The database becomes a compiled
   program stored in SQLite.

### Cache Invalidation

 - `&attr obj=new code` or `@set obj/attr=new code` invalidates the
   cache entry for that attribute (both memory and SQLite).
 - Invalidation is keyed by source text hash, not object/attribute.
   If two attributes have identical source text, they share the
   same compiled binary (content-addressed).
 - `@dbclean` / `@purge` do not affect the code cache — compiled
   entries are keyed by content, not by attrnum.

## Architecture

### Pipeline (Detailed)

```
softcode text
    |
    v
Ragel scanner --> AST (LRU parse cache)          [done]
    |
    v
AST --> HIR lowering (hir_lower)                  [done]
    |
    v
CFG + SSA construction (hir_ssa)                  [done]
    |
    v
SSA optimization: fold + copy_prop + CSE + DCE     [done]
    (constant folding for 28+ functions,
     copy propagation, CSE, dead code elimination,
     LICM for loop-invariant hoisting)
    |
    v
Linear-scan register allocation (Poletto-Sarkar)  [done]
    |
    v
HIR --> RV64 code generation (hir_codegen)        [done]
    |
    +--- Tier 1: native RV64 (add/sub/mul/etc.)   [done]
    +--- Tier 2: JAL to pre-compiled blob          [done, 3 functions]
    +--- ECALL: engine function dispatch           [done]
    |
    v
256-entry LRU compile cache                       [done]
    |
    v
x86-64 DBT: block chaining, register cache,      [done]
            fusion, diamond merge, RAS
```

### RISC-V Target: RV64IMD

The target ISA is RV64IMD — Integer, Multiply, Double-precision float.
This provides:

 - 64-bit integer arithmetic (sufficient for dbref, timestamps, counters)
 - Hardware multiply/divide
 - IEEE 754 double-precision floating point (MUX float semantics)
 - 31 general-purpose registers + zero register
 - Clean encoding, no legacy complications

### Dynamic Binary Translation

On non-RISC-V hosts, the DBT runtime translates RV64IMD blocks to
native code at execution time. The initial host target is **x86-64**
(the only host with a proven translator in ~/riscv). ARM64 host
support is a future addition.

 - **Block translation**: Translate basic blocks on first execution,
   cache the native translation.
 - **Superblocks**: Chain hot blocks across branches to reduce
   translation overhead and enable cross-block optimization.
 - **Register caching**: Map frequently-used RV64 registers to host
   registers across block boundaries. 8-slot LRU cache using
   RSI/RDI/R8-R11/R14/R15 on x86-64.
 - **Instruction fusion**: LUI+ADDI → single MOV imm32. AUIPC+ADDI
   → LEA. AUIPC+JALR → direct CALL. SLT+branch → single Jcc.
 - **Diamond merge**: Short forward branches (≤16 bytes of guest
   code) translated as conditional moves instead of branches.
 - **Return address stack**: Predict JALR returns to avoid indirect
   branch overhead.

The DBT layer is host-specific (one implementation per target
architecture) but the input is always the same RV64IMD binary. This
inverts the traditional cross-compilation problem: instead of N
compiler backends, you have N thin translation layers.

On native RISC-V hosts, compiled code executes directly — no
translation needed. The DBT layer is bypassed entirely.

### What Gets Compiled vs. Runtime Calls

Not every softcode function becomes inline machine code. The split:

**Compiled (inline RISC-V)**:
 - Arithmetic: `add()`, `sub()`, `mul()`, `div()`, `mod()`, comparisons
 - Logic: `and()`, `or()`, `not()`, `t()`
 - Control flow: `if()`, `ifelse()`, `switch()`/`case()`, `switchall()`/
   `caseall()`, `cand()`/`cor()`, `candbool()`/`corbool()`, `iter()` — all
   7 NOEVAL functions are compiled
 - String building: literal concatenation, `%r`, `%b`, `%t`
 - Register access: `%q0`-`%q9`, `setr()`, `setq()`
 - Type conversions: `int_to_string`, `string_to_int`, etc.

**Runtime calls (call into engine via function pointer table)**:
 - Database access: `get()`, `set()`, `u()`, `v()`, `xget()`
 - Object operations: `tel()`, `create()`, `name()`, `owner()`
 - I/O: `pemit()`, `remit()`, `oemit()`
 - String functions with complex semantics: `edit()`, `match()`, `regmatch()`
 - Permission checks: `controls()`, `can_see()`, lock evaluation
 - Anything touching the descriptor/connection layer

The compiled code calls into the engine for runtime operations using
a stable ABI — a function pointer table passed to the compiled code
at invocation. The table is the sandbox boundary.

## Relationship to ~/slow-32 and ~/riscv

The `~/slow-32` project (7-8 months) explored the full design space:
custom 3-address ISA, LLVM backend, four emulators (including QEMU TCG
and the DBT approach), multiple source languages (C, C++, Free Pascal,
ANS Forth, dBASE III+, Lua, Lisp, BASIC), and a Thompson-style
self-hosting bootstrap (740-line emulator, kernel.s32x, prelude.fth,
stages 01-06 to a proper C compiler with doubles and 64-bit integers).

The key lesson from slow-32: RISC-V already is the clean ISA that
slow-32 was converging toward. The `~/riscv` project crystallized this
— use the gcc cross-compiler targeting RV64IMD, then DBT to the host.
The custom ISA is unnecessary when a real, well-supported ISA has the
same properties.

Both projects are **studies and reference implementations**, not
dependencies. TinyMUX's execution environment is fundamentally
different from a microcontroller profile:

 - **No guest memory model**: Compiled softcode doesn't do pointer
   arithmetic, heap allocation, or file I/O in the guest. The "memory"
   is a register file (q-registers, loop state) and engine API calls.
 - **No ELF loading**: Code is emitted directly into byte buffers,
   not compiled as separate ELF binaries. No linker, no crt0, no
   guest libc.
 - **ECALL = Engine API**: Instead of Linux syscall numbers, ECALL
   dispatches to the EngineAPI function pointer table (get_attr,
   set_attr, notify, etc.). The sandbox boundary.
 - **RV64 not RV32**: ~/riscv targets RV32IMFD (32-bit integers,
   32-bit pointers). TinyMUX needs RV64IMD — 64-bit integers for
   timestamps, counters, and pennies; 64-bit pointers for calling
   into the host engine API on 64-bit hosts. The decoder and
   translator must be ported from 32-bit to 64-bit register width.

What carries over from ~/riscv unchanged:

 - **DBT techniques**: Block translation, superblock formation,
   register caching (LRU), instruction fusion (LUI+ADDI, AUIPC+ADDI,
   AUIPC+JALR, SLT+branch), diamond merge for short forward branches,
   block chaining via inline cache probes, return address stack
   prediction.
 - **Host register convention**: RBX = context pointer, R12 = memory
   base, R13 = cache base, 8-slot LRU register cache in RSI/RDI/
   R8-R11/R14/R15. (Adapted for 64-bit guest registers.)
 - **Block cache**: Direct-mapped hash table for translated blocks.
 - **Interpreter**: Reference interpreter for correctness testing
   and debugging, runs the same RV64IMD code without translation.

What must be written fresh for TinyMUX:

 - **RV64 decoder**: Widen all register reads/writes from 32 to 64
   bits. Add RV64-specific instructions: ADDIW, ADDW, SUBW, SLLW,
   SRLW, SRAW, MULW, DIVW, REMW (and unsigned variants) — the W-suffix
   instructions that operate on the lower 32 bits with sign extension.
   LD/SD (64-bit load/store) replace LW/SW as the primary width.
 - **RV64 x86-64 emitter**: Guest registers are now 64-bit, so the
   emitter uses full 64-bit host register operations (REX.W prefixes
   throughout). The 8-slot register cache maps 64-bit guest registers
   to 64-bit host registers — same LRU logic, wider values.
 - **ECALL dispatch**: Replace Linux syscall routing with EngineAPI
   dispatch. Each ECALL number maps to an EngineAPI function pointer.
   Arguments in a0-a7, return value in a0 — same calling convention,
   different dispatch table.
 - **Simplified memory model**: No guest heap, no W^X enforcement.
   Compiled code accesses a small, fixed-size context struct (q-regs,
   loop counters, executor/caller/enactor) via a base pointer. All
   game state access goes through ECALL.

## Implementation Stages

### Proof of Concept → Production Compiler (Complete)

The initial proof of concept (AST → direct RV64 emission) validated
the end-to-end architecture. It has since evolved into a full SSA
compiler with control flow, register allocation, and Tier 2 blob
support. The "PoC" label no longer applies — this is the production
compiler.

**What exists** (dbt_compile.cpp, 3725 lines + hir.h/hir_ssa/hir_opt):
 - Full HIR-based SSA pipeline: AST → HIR → SSA → optimize → codegen
 - Constant folding for 28+ functions at compile time
 - Type tracking (TY_INT / TY_STRING) with inline RV64 atoi/itoa
 - Native RV64 arithmetic: add, sub, mul, div, rem, abs, sign, max,
   min, inc, dec, eq, ne, lt, le, gt, ge, not, bool
 - Control flow: if/ifelse → BRC+PHI, cand/cor → short-circuit chains,
   switch/case → branch-chain codegen with ECALL pattern matching,
   iter() → multi-block loop with SSA back-edges and PHI nodes
 - SSA: CFG, RPO, dominator tree, PHI insertion, renaming (%q0-%q9),
   loop-aware liveness analysis for cross-iteration register safety
 - Linear-scan register allocation (Poletto-Sarkar, 11 regs, spill/reload)
 - 256-entry LRU compile cache (skip recompilation on repeat calls)
 - Block cache persistence via dbt_rerun (skip re-translation)
 - Tier 2 blob: cat/strlen/strcat via JAL (no ECALL boundary crossing)
 - Softcode functions: rvcall(), rveval(), rvbench()

**Benchmark results** (production cache path, 10K iterations):
 - Folded expressions: 0.03-0.05 us/call (10-20x faster than AST eval)
 - ECALL expressions: 0.39-0.50 us/call (at parity or faster than AST eval)
 - Tier 2 JAL: 0.47-0.56 us/call (at parity, ECALL cost dominates)
 - Native arithmetic chains: 0.40-0.42 us/call (20% faster than AST eval)
 - Native AST eval baseline: 0.3-1.0 us/call

### Stage 1: RV64IMD DBT Runtime ✅ (2 items remaining)

RV64IMD dynamic binary translator, fresh implementation informed by
~/riscv's proven patterns.

**Location**: `mux/modules/engine/` — compiled into `engine.so`.

Files: `dbt_decoder.h`, `dbt_interp.cpp`, `dbt.cpp` (1559 lines),
`dbt_emit_x64.h`, `dbt_elf64.cpp`, `dbt_harness.cpp`

**1a. RV64IMD decoder** — ✅ COMPLETE

`dbt_decoder.h`: Full RV64IMD instruction decode. Covers RV64I base
(including W-suffix: ADDIW, ADDW, SUBW, SLLW, SRLW, SRAW), RV64M
(MUL/MULH/DIV/REM + W variants), RV64D (all FP ops including FMA,
conversions, sign injection, comparisons). FENCE recognized but
no-op. Only RVC (compressed) intentionally omitted.

**1b. Reference interpreter** — ✅ COMPLETE

`dbt_interp.cpp` (931 lines): Full RV64IMD interpreter with 1:1
instruction parity with the decoder. 128-bit multiply helpers for
MULH variants. Bounds-checked memory access. Full FP state (frm,
fflags). ECALL dispatches to callback function pointer.

**1c. x86-64 translator** — ✅ COMPLETE (2 optimizations deferred)

`dbt.cpp` (1559 lines): Block-at-a-time translation of all RV64IMD
instructions to x86-64. Correct for all instruction groups.

Optimization state:
 - ✅ 8-slot LRU register cache (RSI/RDI/R8-R11/R14/R15)
 - ✅ 4 pinned registers: a0→RSI, a1→RDI, a2→R8, a3→R9
 - ✅ Pinned register persistence across chained blocks
 - ✅ Block chaining: direct JMP between translated blocks, backpatching
 - ✅ Instruction fusion: LUI+ADDI, AUIPC+ADDI, AUIPC+JALR
 - ✅ Diamond merge: branch-over-one → CMOVcc (ADDI/ADD/SUB/AND/OR/XOR/LUI)
 - ✅ RAS: JAL rd=x1 pushes, JALR rs1=x1 pops + inline cache probe
 - ✅ AUIPC 32-bit immediate compression
 - ✅ Immediate size optimization (imm8 short forms)
 - ✅ Zero-register special handling (x0 never stored)
 - ✅ FP sign-injection idioms (fmv.d/fneg.d/fabs.d)
 - ✅ Register aliasing safety: rd==rs2 detection for commutative/non-commutative ops
 - ❌ Superblocks (cross-branch block extension) — lower priority
 - ❌ Native intrinsic stubs (x86-64 memcpy/strlen/memswap callable from RV64) — lower priority

**1d. Block cache** — ✅ COMPLETE

1024-entry direct-mapped hash table. O(1) lookup. Statistics tracked
(cache_hits, cache_misses, blocks_translated, insns_translated).
Persistent across runs via dbt_rerun() (callback update without
cache invalidation).

**1e. Test suite** — PARTIAL

84 rveval smoke tests + 34 benchmarks cover the compiler+DBT stack.
Missing: standalone low-level DBT tests for instruction correctness
(int64 edge cases, W-suffix sign-extension, FP corner cases, register
cache spill/restore, ECALL argument passing). Cross-compiled C test
programs work via ELF loader but no systematic test suite.

**1f. ELF loader** — ✅ COMPLETE

`dbt_elf64.cpp`: Loads RV64 ELF binaries for testing with cross-
compiled programs. Not used in the production softcode path.

#### Stage 1 Remaining Work

Two deferred optimizations, neither blocking. Both are well-understood
patterns from ~/riscv.

**1c-vi. Superblock formation** (LOWER PRIORITY)

Extend basic blocks across branch points by speculating on the
likely path (e.g., loop back-edges are likely taken). The extended
block includes side-exit stubs that restore state and transfer to
the other path. Builds on block chaining + register persistence.

**1c-vii. Native intrinsic stubs** (LOWER PRIORITY)

When the DBT translates a JAL/JALR to a known Tier 2 function
address, emit inline x86-64 instead of interpreting the RV64 byte
loops. Candidates: memcpy, strlen, memswap. These are byte-level
operations where native x86 REP MOVSB / SCASB vastly outperforms
interpreted RV64. Learned from ~/slow-32.

This is distinct from the Tier 2 blob: Tier 2 gives us RV64
implementations callable via JAL (no ECALL boundary). Intrinsic
stubs go further — the DBT recognizes the call target and replaces
the entire RV64 function body with a native x86-64 sequence.

**1e-ii. Standalone DBT test suite** (SHOULD HAVE)

Instruction-level tests independent of the softcode compiler:
int64 edge cases, W-suffix sign-extension, FP corner cases,
register cache under pressure, cross-compiled C programs via
riscv64-unknown-elf-gcc.

### Stage 2: Engine API / Function Dispatch ✅

#### Stage 2a: Indexed Function Dispatch — ✅ COMPLETE

Replaced string-based ECALL dispatch with O(1) indexed lookup.
The compiler resolves function names to integer indices at compile
time; the ECALL handler uses `engine_api_table[index]`.

Files:
 - `engine_api.h` — ECALL constants, table declaration, lookup API
 - `functions.cpp` — `engine_api_init()` builds flat FUN* array
 - `dbt_compile.cpp` — emits ECALL_CALL_INDEX (0x101) when known
 - `dbt_harness.cpp` — handles both 0x100 and 0x101 dispatch

ECALL convention:
 - `a7 = 0x100` (ECALL_CALL_FUNC): a0 = name ptr (string fallback)
 - `a7 = 0x101` (ECALL_CALL_INDEX): a0 = function index (O(1) lookup)
 - `a7 = 93` (ECALL_EXIT): a0 = exit code

Performance impact (cached path):
 - ECALL dispatch: 0.49→0.41 us (16% faster, 1-ECALL expr)
 - 2-ECALL expr: 0.64→0.50 us (22% faster)
 - Mixed expr: 0.48→0.43 us (10% faster)
 - JIT now **faster than native C++ eval** on mixed expressions

#### Stage 2b: High-Level Engine API (FUTURE)

For Tier 2 functions that need to call back into the engine (database
access, object queries, side effects), define a struct of function
pointers. Not needed for current Tier 2 functions (cat/strlen/strcat
are pure string ops with no engine callbacks).

### Stage 3: Softcode Compiler (SSA Pipeline) — Substantially Complete

Architecture follows the same parallel-array HIR design proven in
~/slow-32/selfhost (14K-line C compiler with full SSA pipeline).

#### IR Design

**Parallel-array HIR** (`hir.h`, 353 lines, instruction index = value number):
 - `kind[]`, `ty[]`, `src1[]`, `src2[]`, `val[]`, `blk[]`
 - `carg[]`/`cbase[]`/`cnargs[]` — flattened call arguments
 - `pblk[]`/`pval[]`/`pbase[]`/`pnargs[]` — flattened PHI arguments
 - `tier2_addr[]` — Tier 2 blob guest address per CALL insn
 - `known_int[]` — flag for string-typed results known to parse as int

**32 instruction kinds** (see `hir.h` enum):
 - Constants: ICONST, SCONST
 - Arithmetic: ADD, SUB, MUL, DIV, REM, NEG, ABS, SIGN, MAX, MIN
 - Comparison: EQ, NE, LT, LE, GT, GE (return int 0/1)
 - Logic: NOT, BOOL (SNEZ)
 - Unary: INC, DEC
 - Conversion: ATOI (inline RV64), ITOA (inline RV64)
 - Calls: CALL (ECALL or Tier 2 JAL), STRCAT
 - SSA: COPY, PHI
 - Memory: LOAD_Q, STORE_Q (%q registers)
 - Control: BR, BRC, RET
 - Marker: NOP

**Type lattice**: TY_INT (64-bit in register), TY_STRING (guest
memory address), TY_VOID (side-effect only).

#### Files

```
mux/include/hir.h                  — HIR definition (355 lines)
mux/modules/engine/dbt_compile.cpp — lowering + codegen + caching (3731 lines)
mux/modules/engine/hir_ssa.cpp     — SSA construction (453 lines)
mux/modules/engine/hir_opt.cpp     — SSA optimization passes (654 lines)
```

Pipeline in `compile_expression()`:
AST → hir_lower → hir_build_cfg → hir_ssa_construct → hir_optimize
→ hir_codegen → guest memory → DBT

#### Milestones

 - ✅ **M1: HIR + lowering** — parallel-array IR, AST→HIR for
   literals/calls/sequences/arithmetic, RV64 codegen from HIR.
   576 smoke tests pass. Folded exprs 0.03 us.

 - ✅ **M2: SSA construction** — CFG, RPO, dominator tree
   (Cooper-Harvey-Kennedy), dominance frontiers, PHI insertion,
   renaming. %q0-%q9 registers are SSA variables. `hir_ssa.cpp`
   (411 lines). setq/r rveval tests pass.

 - ✅ **M3: SSA optimization** — constant folding (28+ functions
   + algebraic identities), copy propagation (chain resolution),
   CSE (dominator-based duplicate elimination), DCE (reachability
   marking). Multi-pass convergence (3 rounds). LICM hoists
   loop-invariant pure ops to preheader. `hir_opt.cpp`. Folded
   exprs still 0.03 us.

 - ✅ **M4: Control flow** — if/ifelse → BRC + 3 blocks + merge PHI.
   cand/cor/candbool/corbool → short-circuit chains with forward BRC
   jumps. switch/case → branch-chain codegen with ECALL pattern
   matching and fallthrough. iter() → 7-block loop (init, header,
   body, first-iter, not-first, latch, exit) with SSA back-edges,
   PHI nodes for inum and accumulator, loop-aware register allocation.
   Constant conditions folded at compile time (no blocks emitted).

 - ✅ **M5: Register allocation** — linear scan (Poletto-Sarkar)
   over SSA live ranges. 11 allocatable integer regs (s1-s11).
   Spill/reload to stack slots via scratch register (s0).
   Loop-aware liveness: values used inside loops but defined outside
   have live ranges extended to the latch block, preventing
   cross-iteration register corruption.

 - ✅ **M6: Advanced optimizations** — CSE (common subexpression
   elimination) and LICM (loop-invariant code motion) implemented.
   CSE replaces duplicate pure instructions with COPYs using
   dominator-based validity check.  LICM hoists loop-invariant
   pure operations to the preheader.  Both limited to pure ops
   (arithmetic, comparison, conversion) — ECALLs are excluded.
   Nested iter() would need qreg stacking (currently flat).
   All 7 NOEVAL functions compiled: if, ifelse, cand/candbool,
   cor/corbool, switch/case/switchall/caseall, iter.

Actual total: ~5,193 lines (vs. 2,500-3,500 estimated).

#### What NOT to Build

 - No BURG instruction selection — RV64 is regular enough for
   direct pattern matching.
 - No instruction scheduling — x86-64 OoO handles this downstream.
 - No interprocedural optimization — each rveval() is one unit.

### Stage 4: Tier 2 — RISC-V Function Library — In Progress

The architectural insight: softcode support functions do not need
to exist as C++ `fun_*()` functions in engine.so. Each function is
either absorbed into the compiler (Tier 1 — constant folding,
native arithmetic) or cross-compiled to a RISC-V binary blob that
compiled softcode calls via JAL instead of ECALL (Tier 2).

**Tier 1 (compiler intelligence)**: Functions where the compiler
reasons about types and semantics. Currently: add, sub, mul, div,
rem, abs, sign, max, min, inc, dec, eq, ne, lt, le, gt, ge, not,
bool, atoi, itoa, strlen, cat, strcat, mid, first, rest, words,
pos, strmatch, comp, floor, ceil, trunc, round, bound, fdiv, idiv.

**Tier 2 (RISC-V library)**: Function bodies cross-compiled to
RV64 via `riscv64-unknown-elf-gcc -march=rv64imd -O2 -nostdlib`.
Loaded as a binary blob into guest memory at BLOB_BASE (0x10000).
Compiled softcode calls via JAL — same address space, no ECALL
boundary crossing.

Currently implemented (3 functions in `mux/rv64/src/softlib.c`):
 - `rv64_cat` — concatenate with space separators
 - `rv64_strlen` — return string length as decimal string
 - `rv64_strcat` — concatenate without separators

Build toolchain:
 - `mux/rv64/Makefile` — cross-compile softlib.c → softlib.elf
 - `mux/src/tools/rv64strip.cpp` — extract .text → softlib.rv64
 - `mux/rv64/rv64blob.h` — blob header format (entry table)
 - `mux/rv64/src/softlib.ld` — linker script (base 0x10000)
 - `tier2_load()` in dbt_compile.cpp — lazy-loads blob on first compile
 - `tier2_lookup()` — maps MUX function name → blob guest address
 - `rv_emit_tier2_call()` — emits a0/a1/a2 setup + JAL ra,target

Benchmark (BENCH030-034, cached path vs AST eval baseline):
 - cat(rand,rand): 0.47us (tier2=1, ecalls=2) vs 0.49us native
 - strlen(cat(rand,rand)): 0.49us (tier2=2) vs 0.61us native
 - strcat(rand,rand): 0.48us (tier2=1) vs 0.52us native

Performance is near-parity because ECALL calls to rand() dominate.
The real win comes when Tier 2 handles more complex functions or
when expressions chain multiple Tier 2 calls without ECALL.

Next Tier 2 candidates: sort(), match(), edit(), regex —
anything too complex for compile-time reasoning. (iter() is now
compiled directly as a multi-block loop, not a Tier 2 call.)

**Intrinsics (native fast-path)**: See Stage 1 item 1c-vii. The
DBT recognizes specific Tier 2 call targets and replaces the RV64
function body with native x86-64 sequences. Candidates: memcpy,
strlen, memswap. Deferred.

**ECALL (escape hatch)**: Only for operations needing host state —
database access, network I/O, @pemit, object manipulation.

### Stage 5: SQLite Code Cache

Schema addition:

```sql
CREATE TABLE code_cache (
    source_hash  BLOB PRIMARY KEY,  -- SHA-1 of source text
    riscv_code   BLOB NOT NULL,     -- compiled RISC-V binary
    metadata     BLOB,              -- entry point, relocations, types
    compile_time INTEGER,           -- when compiled (for diagnostics)
    hit_count    INTEGER DEFAULT 0  -- access tracking for eviction
);
```

Integration points:
 - `mux_exec()` checks the cache before interpreting.
 - `atr_add_raw_LEN()` invalidates cache entries when source changes.
 - `@dump` checkpoints the code cache with the rest of the database.
 - Cache can be rebuilt from source at any time (it's a pure cache).

### Stage 6: Tiered Promotion

Add execution counters to the AST parse cache. Promotion policy:

 - **0 → Tier 0**: Always. Every expression starts interpreted.
 - **Tier 0 → Tier 1**: After N executions (e.g., 8). Compile to
   RISC-V, store in memory cache.
 - **Tier 1 → Tier 2**: After sustained use (e.g., still in memory
   cache after M minutes, or total executions > K). Persist to SQLite.
 - **Tier 2 → eviction**: `@cleancache` command, or manual. Normally
   Tier 2 entries live forever (they're small and the database is
   the compilation artifact).

### Stage 7: Lua Frontend (Optional, Future)

Add a Lua parser that produces SSA IR using the same infrastructure.
Lua attributes (marked by convention or flag) are parsed by the Lua
frontend instead of the MUXcode scanner. Everything downstream —
optimization, codegen, caching, execution — is shared.
