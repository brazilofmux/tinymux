# ISSUES.md — Active bugs and pending fixes

Tracking backport items and design gaps on the brazil branch.

---

## Backport Items (master / 2.13)

### 1. strip_fancy_quotes — smart quotes in %0

**Status**: Open.
**Commit**: 7eb138de (Normalize smart quotes for @listen/^-listen/@filter
pattern matching).
**Problem**: The normalization strips smart quotes from `%0` in @aahear
triggers, not just from the pattern matching comparison.
**Desired behavior**: Smart quotes shown in output AND in `%0`, but ASCII
`"` in @listen patterns still matches smart quotes.
**Fix needed**: Both brazil and master/2.13.

---

## Design Gaps

### 2. Lua privilege model

**Status**: Design gap.
**Affects**: `lua()`, `@lua`, future `@trigger` integration, queued execution.
**Problem**: Scripts live on object attributes, but bridge operations run
with the triggering player as executor.  The design does not yet pin down
how object ownership, `FN_PRIV`-like behavior, queued execution, and
trigger routing should compose.
**Risk**: Privilege semantics can drift into inconsistent special cases
once Lua is embedded in multiple invocation paths.

### 3. Lua cache/versioning

**Status**: Design gap.
**Affects**: `lua_cache` and future `code_cache` integration.
**Problem**: Cached bytecode and native code are keyed by source hash,
object, attr, and blob hash, but not by an explicit compatibility key for
the Lua VM version/build.
**Risk**: Cache reuse across Lua upgrades or build changes can silently
produce wrong results.
