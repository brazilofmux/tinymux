# Mail Module — Open Issues

Updated: 2026-03-27

## ~~High — Missing Error Handling in COM Interface Acquisition~~ FIXED

### ~~Unchecked `mux_CreateInstance()` calls in `FinalConstruct()`~~ FIXED

- All five core interface acquisitions now check `MUX_FAILED(mr)` and return early on failure, consistent with the `IID_ILog` pattern.

## Medium — Thread Safety

### ~~Global reference counters are not thread-safe~~ FIXED

- Changed `g_cComponents` and `g_cServerLocks` from `uint32_t` to `std::atomic<uint32_t>`.

## Medium — Memory Management

### ~~Possible malloc/free vs new/delete mismatch~~ FALSE ALARM

- `list` is allocated via `strdup()` (which uses `malloc`) at all call sites. `free()` is correct.

### ~~Partial interface cleanup on `FinalConstruct()` failure~~ FALSE ALARM

- Constructor initializes all interface pointers to `nullptr`. On `FinalConstruct` failure, the caller calls `Release()` which invokes the destructor. The destructor null-checks each pointer individually before releasing — partially-acquired interfaces are cleaned up correctly.

## Medium — LBUF Truncation in Accessor Functions (GitHub #704)

### List-returning mail functions can truncate on large mailboxes

- **Related to:** Comsys `cwho()`/`chanusers()` truncation (same root cause).
- **Root cause:** Functions like `maillist()` write into a single LBUF with no pagination. Large mailboxes can exceed LBUF_SIZE, silently dropping results.
- **Fix:** Add `offset`/`limit` parameters to list-returning mail accessors so callers can paginate through large result sets.
- **Status:** Open

## Low — Code Quality

### HACK comments in @mail/quick object handling

- **File:** `mail.cpp:2719, 5398`
- **Issue:** Multiple workarounds for `@mail/quick` from objects indicate an architectural mismatch between mail delivery and object permissions.
