# ISSUES.md — Active bugs and pending fixes

Tracking AST parser bugs, JIT parity issues, backport items, and
design gaps on the brazil branch.

---

## AST Parser Bugs

### 1. EV_FCHECK not consumed after first function call in SEQUENCE

**Status**: Open.
**Affects**: TC001 (cmd_say.mux).
**Symptom**: `say add(1,1) add(1,1) [add(1,1)]` produces `"2 2 2"` instead
of `"2 add(1,1) 2"`.  The classic parser only evaluates the first bare
function call, then clears FCHECK.
**Root cause**: The AST evaluator's SEQUENCE handler strips EV_FCHECK
after the first child, but do_say's argument evaluation path may not flow
through the SEQUENCE handler correctly.
**Classic parser hash**: F5D08777EA6D0F93BDADAE7D2504087DFBA85F7D.
**AST parser hash**: 29690A58F305FDB9043CA0E4617523F7EDF6BE80.

### 2. Brace groups lost when re-parsing bare text

**Status**: Open.
**Affects**: TC010 (parser_fn.mux).
**Symptom**: `eval(if(1,{outer {inner} text}))` produces `outer  text`
(double space, inner braces gone) instead of `outer {inner} text`.
**Root cause**: When `fun_eval` calls mux_exec on the result string,
the Ragel scanner may not create AST_BRACEGROUP nodes during re-parse,
or the evaluator drops them.
**Expected hash**: 84523E0A869449C01525577B22DD2B5CBA57138B.

---

## Backport Items (master / 2.13)

### 3. strip_fancy_quotes — smart quotes in %0

**Status**: Open.
**Commit**: 7eb138de (Normalize smart quotes for @listen/^-listen/@filter
pattern matching).
**Problem**: The normalization strips smart quotes from `%0` in @aahear
triggers, not just from the pattern matching comparison.
**Desired behavior**: Smart quotes shown in output AND in `%0`, but ASCII
`"` in @listen patterns still matches smart quotes.
**Fix needed**: Both brazil and master/2.13.

### 4. GANL pure virtual on shutdown

**Status**: Fixed on brazil.  Needs backport to master.
**Fix**: Skip process_output when reason==ServerShutdown in
onConnectionClose.

### 5. GANL double-free on QUIT

**Status**: Fixed on brazil.  Needs backport to master.
**Fix**: Route QUIT through ganl_close_connection instead of
shutdownsock in bsd.cpp.

---

## JIT Parity (--enable-jit)

### 6. setq/setr q-register sync

**Status**: Guarded out (jit_can_handle rejects expressions with setq/setr).
**Problem**: JIT's setr() stores to internal q-registers, not
mudstate.global_regs.  Need ECALL-based sync or direct write-through.

### 7. Expression length limit

**Status**: Workaround (nLen < 256 guard).
**Problem**: Compiler hangs on expressions > ~1200 bytes.  Likely
quadratic behavior in HIR lowering or codegen.

### 8. SIGABRT on shutdown (intermittent)

**Status**: Seen intermittently during smoke tests with JIT enabled.
**Needs**: Reproduction and stack trace.

---

## Design Gaps

### 9. Lua privilege model

**Status**: Design gap.
**Affects**: `lua()`, `@lua`, future `@trigger` integration, queued execution.
**Problem**: Scripts live on object attributes, but bridge operations run
with the triggering player as executor.  The design does not yet pin down
how object ownership, `FN_PRIV`-like behavior, queued execution, and
trigger routing should compose.
**Risk**: Privilege semantics can drift into inconsistent special cases
once Lua is embedded in multiple invocation paths.

### 10. Lua cache/versioning

**Status**: Design gap.
**Affects**: `lua_cache` and future `code_cache` integration.
**Problem**: Cached bytecode and native code are keyed by source hash,
object, attr, and blob hash, but not by an explicit compatibility key for
the Lua VM version/build.
**Risk**: Cache reuse across Lua upgrades or build changes can silently
produce wrong results.
