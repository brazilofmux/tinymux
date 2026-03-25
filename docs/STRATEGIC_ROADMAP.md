# Strategic Roadmap: TinyMUX 2.14+

**Updated:** 2026-03-25
**Objective:** Transition TinyMUX from a traditional interpreted server to a
JIT-first architecture where the compiler is the primary execution engine.

## Architecture Invariants

The AST parser/scanner/lexer pipeline is permanent — it is the prerequisite
for everything the JIT/DBT does.  Certain constructs (the top half of
FN_NOEVAL functions like `iter`, `switch`, `if`) must remain AST-driven
because they control evaluation of their arguments.  But the *bodies* and
*leaves* of those constructs are JIT targets.

The five-way module split (driver, engine, mail, comsys, libmux) gets
sharper over time, not weaker.  Each boundary is a maintenance firewall.
The COM interfaces are the contracts.

## Current State

*Authoritative sources for current numbers are the code itself.  This
section is a snapshot — check the code when planning.*

- **AST evaluator:** Complete.  Ragel -G2 scanner, LRU parse cache (1024
  entries).  Classic parser deleted.
- **JIT/DBT pipeline:** Softcode → AST → HIR → SSA → optimize → RV64
  codegen → x86-64 DBT.  Tier 2 blob with co_* Ragel intrinsics and
  ECALL support for leaf host operations.
- **Tier 2 blob:** 92 mapped functions in `s_tier2_map[]`, 98 entries in
  the allowlist (`jit_compiler.cpp`).  50+ pre-compiled native intrinsics.
  ECALL pattern established for `Good_obj`, `chr`/`ord` NFC, with
  `ecall1`/`ecall2` inline asm helpers in the blob.
- **Tier 3 u() inlining:** Complete with re-entrancy guard, dependency
  tracking, staleness checks.
- **Lua JIT:** Inside engine.so.  83 opcodes supported per CHANGES.md
  (design doc says 73/83 lowered — the delta is opcodes handled by
  fallback or folded into existing lowerings).  XMM FP cache, SQLite
  persistent cache.

### Blocked Tier 2 functions (parity not proven)

These remain in `s_tier2_map[]` but are excluded from `s_allowlist[]`
because their rv64 implementations diverge from the server's Unicode
and/or color-aware behavior:

| Function | Divergence |
|----------|-----------|
| SORT | Shellsort vs DUCET collation for `u`/`U`/`c`/`C` sort types |
| SECURE | Byte-level escaping vs `co_transform` (ANSI-color-preserving) |
| SQUISH | Byte-level delimiter vs `co_compress` (ANSI-aware) |
| TRANSLATE | Byte-level char mapping vs `translate_string` (color-aware) |
| STRMATCH, MATCH, GRAB, GRABALL | ASCII `tolower` vs `mux_strlwr` (Unicode case fold) |

Unblocking these is a prerequisite for Phase 4.

## Phase 1: Sharpen Native/Guest Division (Near-term)

**Principle:** Maintain a single copy of each algorithm where possible.
Use ECALLs to call the native version unless performance demands a guest
duplicate.  The ECALL cost is acceptable for leaf operations that don't
risk re-entrancy.

- Unblock remaining blocked functions (table above) via co_* wrappers
  or ECALLs.  Each must demonstrate parity with the server's output for
  its full input domain (Unicode, ANSI color, edge cases).
- Continue removing dead rv64_* code superseded by co_* routing.
- Build native helper functions that both fun_* and ECALL handlers share.
  The fun_* interface is not exposed to the JIT — ECALLs call the helpers
  directly.

## Phase 2a: ARM DBT (Medium-term)

Port the existing ARM DBT (`~/slow-32/tools/dbt`) into the TinyMUX
framework.  This is adaptation of proven code, not greenfield — the
translator is written and understood.  ARM is the priority because of
the installed base (Apple Silicon, Raspberry Pi, AWS Graviton).

- Adapt the existing ARM→x86-64 translator to the TinyMUX DBT harness.
- Validate with the Tier 2 blob and JIT-compiled softcode.
- Second backend alongside x86-64; proves the architecture is portable.

## Phase 2b: Native RISC-V (Later)

One-to-one RV64 JIT — guest code runs directly on RISC-V hardware
without DBT translation overhead.  On RV64 hosts, the Tier 2 blob and
JIT-compiled code execute natively.  Blocked on access to RISC-V
hardware for testing (QEMU is a fallback but not ideal).

## Phase 3: JIT-by-Default (1-2 years)

**Gate:** All blocked functions (Phase 1 table) must have proven parity.
No function may remain in the allowlist with known Unicode/collation
divergence.

- `--enable-jit` goes away.  JIT is unconditionally on.  You cannot build
  TinyMUX without JIT.
- Dead fun_* functions (unreachable because the JIT handles them) are
  removed from the engine.
- The server remains compatible, but the fun_* implementation becomes
  ephemeral — built up by the compiler as needed on top of JIT'd code,
  ECALLs, and intrinsics.
- The JIT has deep knowledge of what each fun_* function *did*.  That
  knowledge is baked into the compiler, not a runtime dispatch table.

## Phase 4: Concurrency (Long-term / Research)

Either solve the re-entrant DBT guard or spin up multiple threads, each
with its own DBT environment.

This phase requires substantial prerequisite design work beyond what is
listed here.  Known areas:

- **Mutable engine state ownership.**  Process-global mutable structures
  (`mudstate`, `s_attr_mod_counts`, parser state, JIT caches) must be
  audited and either moved to `thread_local`, protected with locks, or
  redesigned.  `s_attr_mod_counts` in `db.cpp` is one example; there are
  others throughout the engine.
- **SQLite write coordination.**  The WAL-mode database is single-writer.
  Concurrent softcode evaluation that triggers `s_*()` write-through
  accessors needs a coordination strategy (serialized writes, per-thread
  write buffers, or read-only worker threads with a writer thread).
- **Scope of "concurrent evaluation."**  What can run in parallel?
  Read-only softcode (pure functions, no side effects) is the easiest
  target.  Anything that touches `@set`, `@create`, `@trigger`, mail,
  comsys, or the connection manager needs careful design.
- **`mod_count` for optimistic reads.**  The per-attribute modification
  counter system is designed for this — a thread reads attrs, records
  mod_counts, evaluates, and checks for staleness before committing
  output.  But the full protocol is not yet specified.

**Outcome:** Concurrent softcode evaluation on multiple cores.  This is
a research-grade problem and will require iterative design documents
before implementation begins.

## Resolved Questions

1. **Lua module placement.**  Lua is permanently inside `engine.so`.
   It is a first-class language alongside MUXcode.  Extracting it to a
   separate module would add an unnecessary COM hop — the Lua VM needs
   direct access to engine state, and the DBT would have to ECALL to
   engine and then COM-call to a Lua module.  If the MUXcode parser
   lives in the engine, then Lua does too.

2. **Backend ordering.**  ARM DBT first, then native RISC-V.
   - ARM has the larger installed base (Apple Silicon, Raspberry Pi,
     cloud ARM instances).
   - An ARM DBT already exists in `~/slow-32/tools/dbt` — adaptation
     and testing, not greenfield development.
   - Native RISC-V is conceptually a one-to-one pass (guest IS host)
     but lacks hardware to test on today.  QEMU is an option but ARM
     is the higher-priority target.

## Design Principles

1. **Single source of truth.**  Unless performance demands it, keep one
   implementation of each algorithm (on the native side) and ECALL to it.
   Duplication on the RV64 side is a deliberate performance trade-off,
   not the default.

2. **Sharpen boundaries.**  Every change should make the driver/engine/
   module/libmux boundaries cleaner, not muddier.

3. **No re-entrancy on the hot path.**  ECALLs are fine for leaf
   operations (database lookups, Unicode normalization, format
   conversion).  ECALLs that could trigger softcode evaluation
   (and thus re-enter the JIT) are forbidden from Tier 2.

4. **The blob is curated.**  Functions enter the Tier 2 blob because
   they earn their place — either as performance-critical native guest
   code or as thin ECALL wrappers that keep surrounding code in the JIT.
   Dead code gets removed.

5. **Parity before promotion.**  No function moves to Tier 2 or gets
   its fun_* removed until its JIT path produces identical output to
   the server for the full input domain.  The smoke suite is the
   minimum bar; Unicode and color edge cases need targeted tests.
