# ISSUES.md — Design gaps

---

## 1. Lua bridge permission audit

Each `mux.*` bridge function is compared against its softcode equivalent.
Functions marked **HOLE** bypass permission checks that softcode enforces.

### mux.notify(target, msg) / mux.pemit(target, msg)

**Softcode equivalent**: `@pemit`
**Softcode checks**: `nearby()`, `Long_Fingers()`, `Controls()`,
`page_check()`, `pemit_players` / `pemit_any` config flags.
**Bridge checks**: **NONE**.
**Verdict**: **HOLE** — any script can message any dbref, bypassing
locality, page-lock, and HAVEN.
**Fix**: Route through `do_pemit_single` or replicate its permission
logic using executor from `lua_exec_ctx`.

### mux.location(dbref)

**Softcode equivalent**: `loc()`
**Softcode checks**: `locatable(executor, it, enactor)` — respects
UNFINDABLE, nearby, see_all.
**Bridge checks**: **NONE** — raw `GetLocation`.
**Verdict**: **HOLE** — leaks location of UNFINDABLE objects.
**Fix**: Add locatable check, or route through `IObjectInfo` with
executor for permission filtering.

### mux.name(dbref)

**Softcode equivalent**: `name()`
**Softcode checks**: If `!read_rem_name`, requires `nearby_or_control`
or `isPlayer` or `Long_Fingers`.
**Bridge checks**: **NONE** — raw `GetName`.
**Verdict**: **HOLE** (when `read_rem_name` is off) — leaks names of
remote non-player objects.
**Fix**: Add the same locality/config check using executor.

### mux.isconnected(dbref)

**Softcode equivalent**: `conn()` / `connected()`
**Softcode checks**: `conn()` checks descriptor ownership or `See_All`.
`connected()` (hasflag CONNECTED) is generally visible.
**Bridge checks**: **NONE**.
**Verdict**: **Minor** — `connected()` equivalent is broadly visible in
softcode; `conn()` (connection time) is restricted but the bridge only
returns bool, not connection details. Acceptable for now.

### mux.get(dbref, attr)

**Softcode equivalent**: `get()`
**Softcode checks**: Standard attribute access via executor permissions
(ownership, zone, flags).
**Bridge checks**: Uses `GetAttribute(ctx->executor, ...)` — **same
permission path**.
**Verdict**: **OK** — matches softcode.

### mux.set(dbref, attr, value)

**Softcode equivalent**: `set()` / `attrib_set()`
**Softcode checks**: Standard attribute write via executor permissions.
**Bridge checks**: Uses `SetAttribute(ctx->executor, ...)` — **same
permission path**.
**Verdict**: **OK** — matches softcode. Errors on permission failure.

### mux.eval(expression)

**Softcode equivalent**: `eval()` in brackets
**Softcode checks**: Threads executor/caller/enactor through mux_exec.
**Bridge checks**: Uses `Eval(ctx->executor, ctx->caller, ctx->enactor,
...)` — **same permission path**.
**Verdict**: **OK** — matches softcode.

### mux.flags(dbref)

**Softcode equivalent**: `flags()`
**Softcode checks**: `flags()` calls `decode_flags` with executor as
looker, which filters DARK/hidden flags based on permissions.
**Bridge checks**: Uses `DecodeFlags(ctx->executor, ...)` — **same
permission path**.
**Verdict**: **OK** — matches softcode.

### mux.owner(dbref)

**Softcode equivalent**: `owner()`
**Softcode checks**: Generally unrestricted (owner is public info).
**Bridge checks**: None needed.
**Verdict**: **OK**.

### mux.type(dbref)

**Softcode equivalent**: `type()`
**Softcode checks**: Generally unrestricted.
**Bridge checks**: None needed.
**Verdict**: **OK**.

### mux.isplayer(dbref)

**Softcode equivalent**: `isplayer()` / `hasflag(obj, PLAYER)`
**Softcode checks**: Generally unrestricted.
**Bridge checks**: None needed.
**Verdict**: **OK**.

### mux.pennies(dbref)

**Softcode equivalent**: `pennies()`
**Softcode checks**: `pennies()` is unrestricted (public info).
**Bridge checks**: None needed.
**Verdict**: **OK**.

### mux.iswizard(dbref)

**Softcode equivalent**: `hasflag(obj, WIZARD)`
**Softcode checks**: Flag visibility rules (from `decode_flags`).
**Bridge checks**: Raw `IsWizard` — **no looker-based filtering**.
**Verdict**: **Minor** — wizard status is generally visible via `flags()`,
but the raw check doesn't respect any hypothetical flag-hiding. Low risk.

### mux.controls(who, what)

**Softcode equivalent**: `controls()`
**Softcode checks**: `controls()` softcode function itself checks if
the executor can see the answer (typically yes for own objects).
**Bridge checks**: Takes arbitrary `who`/`what` — a script could query
`mux.controls(wizard_dbref, target)` to learn wizard control
relationships without being a wizard.
**Verdict**: **HOLE** — should enforce that `who` is either the executor
or controlled by the executor.

### Error behavior

- `mux.get()` returns `nil` on permission failure — **silent**.
- `mux.set()` throws a Lua error on permission failure — **loud**.
- These should be consistent. Recommendation: return `nil, "permission
  denied"` (two-value return) for both, matching Lua conventions. Errors
  should be reserved for programming mistakes (wrong arg types).

---

## 2. Lua cache/versioning

**Current state**: In-memory LRU cache keyed by source text. No SQLite
persistence. No invalidation on attribute change. No version metadata.
Everything lost on restart.

**Key decisions needed**:

- **Persistence**: Recompile on startup is simpler and avoids versioning
  entirely. SQLite persistence is an optimization for large script
  libraries. Recommendation: defer persistence until measured startup
  cost justifies it.

- **Invalidation**: Source-text keying means content changes naturally
  produce different keys. The risk is stale entries consuming memory for
  old source text that no longer exists. LRU eviction handles this
  adequately for now.

- **Version key**: Only matters if we persist bytecode. If we recompile
  on startup, the running Lua VM version is always correct. Defer until
  persistence is implemented.

**Verdict**: The cache design is adequate for the current in-memory-only
implementation. The "design gap" collapses once we decide to defer
persistence. Document the decision and close.
