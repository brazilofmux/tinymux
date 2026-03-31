# Comsys Module — Open Issues

Updated: 2026-03-27

## ~~High — Missing Error Handling in COM Interface Acquisition~~ FIXED

### ~~Unchecked `mux_CreateInstance()` calls in `FinalConstruct()`~~ FIXED

- All five core interface acquisitions now check `MUX_FAILED(mr)` and return early on failure, consistent with the `IID_ILog` pattern.

## Medium — Thread Safety

### ~~Global reference counters are not thread-safe~~ FIXED

- Changed `g_cComponents` and `g_cServerLocks` from `uint32_t` to `std::atomic<uint32_t>`.

## Medium — Data Integrity

### ~~Channel consistency checks unimplemented~~ FIXED

- `dbck()` now prunes channel users whose dbrefs are no longer valid players, reassigns channel ownership to GOD when the owner is destroyed, and removes comsys entries for destroyed players.

## Medium — LBUF Truncation in Accessor Functions (GitHub #704)

### `cwho()` and `chanusers()` silently truncate on large channels

- **Reported by:** Shangrila (LBUF_SIZE=16000, still truncating on channels with 2000+ users)
- **Root cause:** All list-returning comsys accessors (`cwho`, `chanusers`) write into a single LBUF with no pagination. When the output exceeds LBUF_SIZE, results are silently dropped.
- **Scope:** This is a general problem affecting any accessor that returns an unbounded list — comsys and mail both have functions that can exceed buffer limits at scale.
- **Fix:** Add `offset`/`limit` parameters to list-returning functions (`cwho`, `chanusers`, and mail equivalents like `maillist`) so callers can paginate through large result sets. This is the only sustainable solution — increasing LBUF_SIZE just moves the cliff.
- **Workaround (2.13):** Increase LBUF_SIZE (requires `dbconvert` unload/load cycle). Softcode can iterate `lwho()` and check per-player channel membership individually.
- **Status:** Open

## Low — Code Quality

### `strncpy()` usage with casts

- **File:** `comsys_mod.cpp:309, 315`
- **Issue:** `strncpy()` with casts to `char *` — verify source strings are null-terminated and destination buffers are properly sized.

### ~~No parameter validation on PlayerNuke~~ FIXED

- Added `player < 0` guard returning `MUX_E_INVALIDARG`.
