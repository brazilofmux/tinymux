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

## ~~Medium — LBUF Truncation in Accessor Functions (GitHub #704)~~ FIXED

### ~~`cwho()` and `chanusers()` silently truncate on large channels~~ FIXED

- Added optional `offset`/`limit` pagination parameters to `cwho()`, `chanusers()`, and `channels()`. Softcode can now paginate through arbitrarily large result sets.

## High — Thread Safety (New, 2026-04-04)

### Non-atomic instance reference counting (`m_cRef`)

- **File:** `comsys_mod.cpp:293-305, 3085-3098` and all Factory classes
- **Issue:** Same as mail module — individual object `m_cRef` counters are plain `uint32_t` with non-atomic increment/decrement. The `Release()` decrement-then-check pattern can race to double-delete.
- **Impact:** Use-after-free or double-delete under concurrent access.
- **Recommendation:** Change `m_cRef` to `std::atomic<uint32_t>` with `fetch_add`/`fetch_sub`.

## Low — Code Quality

### `strncpy()` usage with casts

- **File:** `comsys_mod.cpp:309, 315`
- **Issue:** `strncpy()` with casts to `char *` — verify source strings are null-terminated and destination buffers are properly sized.

### ~~No parameter validation on PlayerNuke~~ FIXED

- Added `player < 0` guard returning `MUX_E_INVALIDARG`.
