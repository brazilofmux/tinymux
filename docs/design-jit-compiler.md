# JIT Compilation via RISC-V and Dynamic Binary Translation

## Status: Design

Branch: `brazil` (future)

Predecessors: `docs/PARSER.md` (study), `docs/PARSER_REPLACE.md` (AST evaluator)

External work: `~/riscv` (DBT runtime), `~/slow-32` (ISA design + toolchain history)

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
Ragel scanner --> AST (LRU parse cache)          [done - brazil branch]
    |
    | execution counter >= threshold?
    | no: interpret via AST evaluator (Tier 0)
    | yes:
    v
AST --> SSA IR                                    [new]
    |
    v
Type inference (lattice narrowing)                [new]
    |
    v
SSA optimization passes                          [new]
    (constant folding, dead code elimination,
     redundant attribute fetch hoisting,
     string concatenation chain reduction,
     conversion elimination)
    |
    v
Lowering + instruction selection (BURG)           [new]
    |
    v
Register allocation (infinite regs --> RV regs)   [new]
    |
    v
RISC-V machine code (RV64IMD)                     [new]
    |
    v
Tier 1: memory cache     -or-     Tier 2: SQLite  [new]
    |                                  |
    v                                  v
Execution:
    RISC-V host  --> direct execution
    ARM/x86 host --> DBT runtime (from ~/riscv)
```

### RISC-V Target: RV64IMD

The target ISA is RV64IMD — Integer, Multiply, Double-precision float.
This provides:

 - 64-bit integer arithmetic (sufficient for dbref, timestamps, counters)
 - Hardware multiply/divide
 - IEEE 754 double-precision floating point (MUX float semantics)
 - 31 general-purpose registers + zero register
 - Clean encoding, no legacy complications

### Dynamic Binary Translation (from ~/riscv)

On non-RISC-V hosts, the DBT runtime translates RISC-V blocks to
native code at execution time:

 - **Block translation**: Translate basic blocks on first execution,
   cache the native translation.
 - **Superblocks**: Chain hot blocks across branches to reduce
   translation overhead and enable cross-block optimization.
 - **Register caching**: Map frequently-used RISC-V registers to host
   registers across block boundaries.
 - **Peephole optimization**: Pattern-match and simplify common
   instruction sequences in the translated output.

The DBT layer is host-specific (one implementation per target
architecture) but the input is always the same RISC-V binary. This
inverts the traditional cross-compilation problem: instead of N
compiler backends, you have N thin translation layers.

### What Gets Compiled vs. Runtime Calls

Not every softcode function becomes inline machine code. The split:

**Compiled (inline RISC-V)**:
 - Arithmetic: `add()`, `sub()`, `mul()`, `div()`, `mod()`, comparisons
 - Logic: `and()`, `or()`, `not()`, `t()`
 - Control flow: `if()`, `ifelse()`, `switch()`, `iter()`, `while()`
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

For TinyMUX, this means we don't need to design an ISA or build an
assembler. We emit RISC-V machine code directly (or via a thin
encoding layer), store it in SQLite, and the DBT runtime from ~/riscv
handles execution on any host.

## Implementation Stages

### Stage 1: DBT Runtime Integration

Port the ~/riscv DBT runtime into libmux.so (or a companion shared
library). This is the foundation — without it, RISC-V code can't
execute on x86/ARM hosts.

Deliverable: A function `dbt_execute(const uint8_t* riscv_code,
size_t len, RuntimeContext* ctx)` that runs RISC-V binary on any
host.

Validation: Run existing ~/riscv test cases through the integrated
runtime. Verify correctness and performance targets (~70% native).

### Stage 2: Engine API / Function Pointer Table

Define the stable ABI between compiled code and the game engine.
This is a struct of function pointers:

```c
struct EngineAPI {
    // String operations
    StringHandle (*alloc_string)(const char* data, size_t len);
    void         (*free_string)(StringHandle h);
    const char*  (*string_data)(StringHandle h);
    size_t       (*string_len)(StringHandle h);

    // Database access
    StringHandle (*get_attr)(dbref obj, const char* attr, dbref executor);
    bool         (*set_attr)(dbref obj, const char* attr, StringHandle val,
                             dbref executor);

    // Object queries
    dbref        (*owner)(dbref obj);
    dbref        (*location)(dbref obj);
    StringHandle (*name)(dbref obj);
    bool         (*controls)(dbref who, dbref what);

    // Side effects
    void         (*notify)(dbref target, StringHandle msg);
    void         (*pemit)(dbref executor, dbref target, StringHandle msg);

    // u() — invoke another attribute's compiled/interpreted code
    StringHandle (*ucall)(dbref obj, const char* attr, dbref executor,
                          dbref caller, dbref enactor,
                          StringHandle* args, int nargs);

    // ... (extended as needed)
};
```

Deliverable: Header file with the API struct. Wrapper implementations
in the engine that delegate to existing functions.

Validation: Write a hand-coded RISC-V test function that calls
through the API and verifies results match the interpreter.

### Stage 3: SSA IR Definition

Define the SSA intermediate representation:

 - **Value types**: int64, f64, dbref, string (handle), void
 - **Operations**: arithmetic, comparison, conversion, call, phi,
   branch, string_concat, string_alloc, string_free
 - **Blocks**: basic blocks with phi nodes at entry, terminator at exit
 - **Side-effect ordering**: call nodes carry a memory token (like
   LLVM's `memory` operand) to enforce ordering
 - **Type annotations**: each value carries its type from the lattice

Deliverable: C++ data structures for the IR (`SSAValue`, `SSABlock`,
`SSAFunction`). Printer that dumps human-readable IR for debugging.

Validation: Manually construct SSA for a few test expressions, verify
the printer output is sensible.

### Stage 4: AST-to-SSA Lowering

Walk the AST and emit SSA IR:

 - Literal nodes → SSA constants (typed: int64/f64/string)
 - Function calls → SSA call nodes (inline for compiled functions,
   engine API call for runtime functions)
 - `%q0`-`%q9` → SSA load/store through a register file passed in
   the runtime context
 - `if()`/`ifelse()` → conditional branches + phi nodes
 - `iter()` → loop with phi for `%i0`, `inum()` as loop counter
 - `u()` → engine API `ucall` with argument marshaling
 - %-substitutions → loads from the execution context struct

Type inference runs during or immediately after lowering: each value
gets a type from the lattice based on production rules, then
consumption constraints propagate backward to insert or eliminate
conversions.

Deliverable: `ast_to_ssa(ASTNode* root) -> SSAFunction*`

Validation: Compile simple expressions (`add(1,2)`, `if(t(%q0),yes,no)`,
`iter(a b c,strlen(%i0))`) and verify SSA output. Compare interpreted
vs. SSA-interpreted results for a suite of test expressions.

### Stage 5: SSA Optimization Passes

Implement in order of impact:

 1. **Conversion elimination** — the biggest win. Prove that adjacent
    numeric operations can share machine types without string
    round-trips.
 2. **Constant folding** — evaluate pure functions with constant
    arguments at compile time. `add(1,2)` → `3`.
 3. **Dead code elimination** — remove values that are computed but
    never used (common in MUXcode with side-effect-only branches).
 4. **Common subexpression elimination** — `get(obj/attr)` called
    twice in the same expression becomes one call + reuse, if no
    intervening `set()` could modify it.
 5. **Loop-invariant code motion** — hoist `get()` calls out of
    `iter()` / `while()` bodies when the attribute isn't modified
    inside the loop.

Each pass is independently testable. Verify SSA before/after and
confirm behavioral equivalence.

### Stage 6: RISC-V Code Generation

 - **Instruction selection**: BURG tree matcher maps SSA operations
   to RISC-V instruction sequences. Arithmetic maps 1:1. Conversions
   map to call sequences. String operations map to engine API calls.
 - **Register allocation**: Linear scan or graph coloring over the
   RV64IMD register file (31 integer + 32 FP). Spill to stack frame.
   Reserve registers for the engine API pointer, stack pointer, and
   runtime context pointer.
 - **Calling convention**: Engine API calls follow the standard
   RISC-V calling convention (a0-a7 for arguments, a0-a1 for return
   values). This means the DBT layer's register mapping works
   naturally.
 - **Code emission**: Emit RISC-V binary into a byte buffer.
   Relocations for engine API calls resolved at link time (offsets
   into the function pointer table).

Deliverable: `ssa_to_riscv(SSAFunction*) -> CompiledCode` where
`CompiledCode` is a byte buffer + metadata (entry point offset,
relocation table, source hash).

Validation: Compile test expressions, execute via DBT runtime,
compare results against interpreter. Run the full smoke test suite
with compiled eval and verify all 489 tests pass.

### Stage 7: SQLite Code Cache

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

### Stage 8: Tiered Promotion

Add execution counters to the AST parse cache. Promotion policy:

 - **0 → Tier 0**: Always. Every expression starts interpreted.
 - **Tier 0 → Tier 1**: After N executions (e.g., 8). Compile to
   RISC-V, store in memory cache.
 - **Tier 1 → Tier 2**: After sustained use (e.g., still in memory
   cache after M minutes, or total executions > K). Persist to SQLite.
 - **Tier 2 → eviction**: `@cleancache` command, or manual. Normally
   Tier 2 entries live forever (they're small and the database is
   the compilation artifact).

### Stage 9: Lua Frontend (Optional, Future)

Add a Lua parser that produces SSA IR using the same infrastructure.
Lua attributes (marked by convention or flag) are parsed by the Lua
frontend instead of the MUXcode scanner. Everything downstream —
optimization, codegen, caching, execution — is shared.
