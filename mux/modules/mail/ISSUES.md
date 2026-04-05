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

## ~~Medium — LBUF Truncation in Accessor Functions (GitHub #704)~~ FIXED

### ~~List-returning mail functions can truncate on large mailboxes~~ FIXED

- Added optional `offset`/`limit` pagination parameters to `maillist()`. Softcode can now paginate through arbitrarily large mailboxes.

## High — Thread Safety (New, 2026-04-04)

### Non-atomic instance reference counting (`m_cRef`)

- **File:** `mail_mod.cpp:322-329, 5559-5566` and all Factory classes
- **Issue:** While global counters were fixed to `std::atomic<uint32_t>`, individual object reference counters (`m_cRef`) remain plain `uint32_t` with non-atomic `m_cRef++` and `m_cRef--`. In `Release()`, the decrement and zero-check are not atomic, creating a race window for double-delete.
- **Impact:** Use-after-free or double-delete under concurrent access.
- **Recommendation:** Change `m_cRef` to `std::atomic<uint32_t>` with `fetch_add`/`fetch_sub`.

## Medium — Buffer Safety (New, 2026-04-04)

### ~~Off-by-one pointer arithmetic in `mail_to_list()` parsing~~ FIXED

- **File:** `mail_mod.cpp:2311, 2590, 2691`
- All three token-parsing loops (`do_expmail_to`, `mail_to_list` senderlist build, `mail_to_list` recipient iteration) now guard the trailing `tail--; if (*tail != '"') tail++;` fixup with `if (tail > head)`. A malformed lone `"` token no longer walks `tail` before `head`, eliminating the OOB read and the out-of-bounds `*tail = '\0'` write in the recipient loop.

### ~~Missing strdup() null check in `do_mail_quick()`~~ FIXED

- **File:** `mail_mod.cpp:3145`
- The `strdup(numlist.c_str())` result in `do_mail_quick()` is now checked; on allocation failure the player is notified via `RawNotify()` and the function returns without calling `mail_to_list()`.

### Potential use-after-free in `shutdown()` under concurrent access

- **File:** `mail_mod.cpp:5425-5434`
- **Issue:** `shutdown()` calls `m_pIStorage->Release()` and sets null, but has no guard against concurrent method calls that could access the pointer during the Release call.
- **Impact:** Use-after-free if another thread accesses storage during shutdown.

## Low — Code Quality

### HACK comments in @mail/quick object handling

- **File:** `mail.cpp:2719, 5398`
- **Issue:** Multiple workarounds for `@mail/quick` from objects indicate an architectural mismatch between mail delivery and object permissions.
