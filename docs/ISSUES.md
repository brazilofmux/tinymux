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
