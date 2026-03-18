# ISSUES.md — Active bugs and pending fixes

Scratchpad for tracking AST parser bugs, JIT parity issues, and
backport items discovered during the JIT compiler work (brazil branch).

---

## AST Parser Bugs (no JIT involvement)

### 1. EV_FCHECK not consumed after first function call in SEQUENCE

**Status**: Identified, not fixed.
**Affects**: TC001 (cmd_say.mux), any expression with multiple bare function
calls at top level.
**Symptom**: `say add(1,1) add(1,1) [add(1,1)]` produces `"2 2 2"` instead
of `"2 add(1,1) 2"`.
**Root cause**: The AST evaluator's SEQUENCE handler (ast.cpp ~line 1869)
strips EV_FCHECK after the first child, but do_say's argument evaluation
path may not flow through the SEQUENCE handler correctly.  The classic
parser only checks the first `(` as a function call, then clears FCHECK.
**Classic parser hash**: F5D08777EA6D0F93BDADAE7D2504087DFBA85F7D (straight
quotes, both Unicode and non-Unicode after strip_fancy_quotes).
**AST parser hash**: 29690A58F305FDB9043CA0E4617523F7EDF6BE80 (all three
add() calls evaluated).

### 2. Brace groups lost when re-parsing bare text

**Status**: Identified, not fixed.
**Affects**: TC010 (parser_fn.mux).
**Symptom**: `eval(if(1,{outer {inner} text}))` produces `outer  text`
(double space, inner braces gone) instead of `outer {inner} text`.
**Root cause**: When `fun_eval` calls mux_exec on the result string
`outer {inner} text`, the AST parser/evaluator loses the `{inner}` brace
group.  The Ragel scanner may not create AST_BRACEGROUP nodes during
re-parse, or the evaluator drops them.  The AST_BRACEGROUP handler in
ast_eval_node (line ~1799) is never reached during smoke tests.
**Correct output**: `outer {inner} text` (confirmed on two classic-parser
games).
**Expected hash**: 84523E0A869449C01525577B22DD2B5CBA57138B.

### 3. Malformed %q< angle substitution fallback regression

**Status**: Identified, not fixed.  Backport needed.
**Affects**: TC024 (parser_fn.mux), classic parser (2.13.0.10+), AST parser.
**Bisected**: c4ae95f7 (Implement named global registers #351).
**Symptom**: `eval({prefix [add(1,2)] %q<oops suffix})` — `%q<oops` fallback
behavior changed.
**Before named registers** (.4, .6): `%q<` consumed as invalid register →
output `{prefix 3 oops suffix}`.
**After named registers** (.10): `%q<` starts angle-bracket parse, gives up,
leaves `<` → output `{prefix 3 <oops suffix}`.
**Correct output**: `{prefix 3 oops suffix}` (pre-named-registers behavior).
**Expected hash**: A3B1B656C920B0E8A672358310E8AFC072B21E1C.
**AST parser hash**: 1B3EB8C217AE9D88AC879D127840E4CF66393851.
**Backport**: Fix `%q<` fallback in classic parser (2.13) and AST parser.

---

## Backport Items (master / 2.13)

### 4. strip_fancy_quotes — smart quotes in %0

**Status**: Needs review.
**Commit**: 7eb138de (Normalize smart quotes for @listen/^-listen/@filter
pattern matching).
**Problem**: The normalization strips smart quotes from `%0` in @aahear
triggers, not just from the pattern matching comparison.  The intent was:
display retains smart quotes, pattern matching normalizes.  The actual
behavior: `%0` receives straight quotes.
**Desired behavior**: Smart quotes shown in output AND in `%0`, but ASCII
`"` in @listen patterns still matches smart quotes.
**Backport**: Fix needed in both brazil and master/2.13.

### 5. GANL pure virtual on shutdown

**Status**: Fixed on brazil, needs backport.
**Fix**: Skip process_output when reason==ServerShutdown in
onConnectionClose.

### 6. GANL double-free on QUIT

**Status**: Fixed on brazil, needs backport.
**Fix**: Route QUIT through ganl_close_connection instead of
shutdownsock in bsd.cpp.

### 7. CacheClose segfault on shutdown

**Status**: Fixed on brazil (brazil-only issue due to engine/driver split).
**Fix**: Move drv_CacheClose() before pGameEngine->Release().

---

## JIT Parity (--enable-jit)

### 8. setq/setr q-register sync

**Status**: Guarded out (jit_can_handle rejects expressions with setq/setr).
**Problem**: JIT's setr() stores to internal q-registers, not
mudstate.global_regs.  Need ECALL-based sync or direct write-through.

### 9. Expression length limit

**Status**: Workaround (nLen < 256 guard).
**Problem**: Compiler hangs on expressions > ~1200 bytes.  Needs
investigation — likely quadratic behavior in HIR lowering or codegen.

### 10. SIGABRT on shutdown (intermittent)

**Status**: Seen intermittently during smoke tests with JIT enabled.
**Needs**: Reproduction and stack trace.

---

## Recent Review Findings (2026-03-18)

### 11. `NOBLEED` regression in Stage 6 output pipeline

**Status**: Fixed on brazil (2026-03-18).
**Affects**: ANSI player output path after Stage 6 renderer switchover.
**Commit area**: `aaa5ac63e` / `92723905a`.
**Problem**: `queue_string()` now routes ANSI output through
`co_render_truecolor()`, `co_render_ansi256()`, `co_render_ansi16()`, and
`co_render_html()`, but no longer threads the player's `NOBLEED` flag into
the renderer selection/transition logic.
**Evidence**:
- `mux/src/net.cpp` now selects Stage 6 renderers directly.
- Legacy `convert_color()` accepted `fNoBleed` and adjusted the client-normal
  transition accordingly.
**Behavioral risk**: ANSI users with `NOBLEED` can observe different reset
semantics at line boundaries than before the migration.
**Fix**: Added explicit `bNoBleed` handling to the ANSI16 / ANSI256 /
TRUECOLOR Stage 6 renderers, restored the white-baseline client model, and
wired `queue_string()` to pass the player's `NOBLEED` flag through.
**Coverage**: Added focused `tests/color_ops` cases for mid-string reset to
white and trailing restore-to-white behavior.

### 12. `co_render_html()` can emit invalidly nested tags

**Status**: Fixed on brazil (2026-03-18).
**Affects**: HTML output when color changes inside an active style span
(`B`, `U`, `I`, `S`).
**Commit area**: `92723905a`.
**Problem**: The new one-pass HTML renderer closes `</COLOR>` while style
tags remain open, then opens a new `<COLOR ...>` tag without temporarily
closing and reopening the active style tags. This can create crossing tag
boundaries.
**Example shape**: Bold red text followed by bold blue text can become
`<COLOR ...><B>X</COLOR><COLOR ...>Y</B>`, which is structurally invalid.
**Prior behavior**: The old `convert_to_html()` implementation used a
two-pass stack/list approach specifically to compute valid nesting.
**Fix**: `emit_html()` now closes active style tags around any color boundary,
closes/reopens the color wrapper, then reopens still-active styles so the
resulting HTML remains well-nested.
**Coverage**: Added targeted `tests/color_ops` cases for color change inside
bold text and entering color while bold is already active.

### 13. HTML renderer tests miss the nesting regression

**Status**: Fixed on brazil (2026-03-18).
**Affects**: `tests/color_ops/test_color_ops.c`.
**Problem**: New tests validate tag presence, escaping, RGB formatting, and
PUA stripping, but do not assert correct nesting across mixed style+color
transitions.
**Fix**: Added focused cases for:
- color change while `intense` remains active
- color change while underline/blink/inverse remain active
- adjacent open/close transitions that would cross if emitted in one pass

### 14. Ragel standalone test harness broken by justify API change

**Status**: Fixed on brazil (2026-03-18).
**Affects**: `ragel/Makefile` target `test_harness`.
**Commit area**: `0e101690e`.
**Problem**: `co_center()`, `co_ljust()`, and `co_rjust()` gained the
`bTrunc` parameter, but `ragel/test_harness.c` still calls the old
signatures.
**Verification**: `make -C ragel test_harness` fails with “too few arguments
to function” for all three helpers.
**Fix**: Updated the harness call sites to pass `bTrunc`, added explicit
coverage for the new non-truncating behavior, and linked the standalone
target against the extracted ASCII transliteration tables required by the
new `co_render_ascii()` implementation.
**Verification**: `make -C ragel test_harness` and `ragel/test_harness` now
pass.

---

## Lua / JIT Design Questions

### 15. Mixed Lua VM and JIT state model is underspecified

**Status**: Design gap.
**Affects**: `docs/design-lua-server.md` Phase 2, any future Lua-to-HIR work.
**Problem**: The current design says Lua bytecode lowers into HIR and falls
back to ECALLs for unsupported features, but it does not specify how Lua VM
state remains coherent across native JIT execution and interpreter fallback.
This is the hard architectural issue for closures, varargs, tables,
metatables, generic calls, and upvalues.
**Risk**: Without a precise state model, the JIT path may violate the
sandbox contract's requirement for identical output, side-effect ordering,
and visibility relative to interpreted execution.
**Needed design work**: Define a canonical ownership model for registers,
stack, upvalues, tables, and resumable control flow when execution bounces
between compiled HIR and the stock Lua VM.

### 16. Lua privilege model needs sharper definition before implementation

**Status**: Design gap.
**Affects**: `lua()`, `@lua`, future `@trigger` integration, queued execution.
**Problem**: The design says scripts live on object attributes, but bridge
operations run with the triggering player as executor unless special function
semantics apply. That is directionally correct, but the design does not yet
pin down how object ownership, `FN_PRIV`-like behavior, queued execution, and
trigger routing should compose.
**Risk**: Privilege semantics can drift into inconsistent special cases once
Lua is embedded in multiple invocation paths.
**Needed design work**: Define one authority model and apply it uniformly to
direct calls, queued calls, triggers, and future event hooks.

### 17. DBT needs floating-point (RV64D) support for Lua JIT

**Status**: DBT layer complete.  HIR `TY_FLOAT` + Lua float lowering remain.
**Affects**: Lua JIT coverage — `OP_DIV`, `OP_POW`, `math.*` calls, mixed
int/float arithmetic all require FP.
**Background**: The DBT currently implements RV64IMD (integer + multiply +
double-precision FP).  The RV64D translation was added as part of the Lua
JIT pipeline work.  `dbt.cpp` now translates:
- FLD/FSD (double load/store via guest memory)
- FADD.D/FSUB.D/FMUL.D/FDIV.D/FSQRT.D (→ SSE2 addsd/subsd/mulsd/divsd/sqrtsd)
- FMIN.D/FMAX.D (→ SSE2 minsd/maxsd)
- FEQ.D/FLT.D/FLE.D (→ ucomisd + setcc)
- FSGNJ.D/FSGNJN.D/FSGNJX.D (fmv.d/fneg.d/fabs.d idioms + general case)
- FCVT.W.D/FCVT.D.W/FCVT.L.D/FCVT.D.L (→ cvttsd2si/cvtsi2sd)
- FMV.X.D/FMV.D.X (→ movq between GPR and XMM)
- FMADD/FMSUB/FNMSUB/FNMADD (emulated as mul+add/sub + sign flip)
- FCLASS.D falls back to interpreter

`rv64_ctx_t` has `double f[32]` at offset 528.  Guest FP registers are
spilled/loaded from context via `emit_load_fp_d`/`emit_store_fp_d` (no
XMM register cache — always load/store, which is fine for the current
workload).

**Remaining work**: The HIR has only `TY_INT` and `TY_STRING`.  Adding
`TY_FLOAT` and float-specialized Lua→HIR lowering (Phase 3 in
design-lua-jit.md) would let the HIR emit native FP code instead of
routing float operations through ECALLs.  The DBT is ready — the gap
is in the HIR type system and the Lua lowering pass.

### 18. Lua cache/versioning story is incomplete

**Status**: Design gap.
**Affects**: `lua_cache` and future `code_cache` integration.
**Problem**: The design keys cached bytecode and native code by source hash,
object, attr, and blob hash, but does not include an explicit compatibility
key for the Lua VM version/build details that determine bytecode and `Proto`
layout stability.
**Risk**: Cache reuse across Lua upgrades or build changes can become
silently invalid.
**Needed design work**: Add explicit cache-version metadata covering Lua
major/minor/build compatibility and codegen assumptions.
