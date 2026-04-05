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

### ~~Non-atomic instance reference counting (`m_cRef`)~~ FIXED

- **File:** `mail_mod.h:377, 396`, `mail_mod.cpp:320-335, 5583-5598`
- `CMailMod::m_cRef` and `CMailModFactory::m_cRef` are now `std::atomic<uint32_t>`. `AddRef()` uses `fetch_add` (relaxed) and `Release()` uses `fetch_sub` (acq_rel) with the previous-value check, closing the decrement/zero-check race window for double-delete under concurrent access.

## Medium — Buffer Safety (New, 2026-04-04)

### ~~Off-by-one pointer arithmetic in `mail_to_list()` parsing~~ FIXED

- **File:** `mail_mod.cpp:2311, 2590, 2691`
- All three token-parsing loops (`do_expmail_to`, `mail_to_list` senderlist build, `mail_to_list` recipient iteration) now guard the trailing `tail--; if (*tail != '"') tail++;` fixup with `if (tail > head)`. A malformed lone `"` token no longer walks `tail` before `head`, eliminating the OOB read and the out-of-bounds `*tail = '\0'` write in the recipient loop.

### ~~Missing strdup() null check in `do_mail_quick()`~~ FIXED

- **File:** `mail_mod.cpp:3145`
- The `strdup(numlist.c_str())` result in `do_mail_quick()` is now checked; on allocation failure the player is notified via `RawNotify()` and the function returns without calling `mail_to_list()`.

### ~~Potential use-after-free in `shutdown()` under concurrent access~~ FIXED

- **File:** `mail_mod.cpp:5451-5465`
- `CMailMod::shutdown()` now captures `m_pIStorage` into a local, nulls the member field first, and then calls `Release()` via the local. A concurrent reader (in a future multi-threaded evaluator) sees the null before the release decrement races, closing the double-Release window called out in the report.

## Low — Code Quality

### ~~HACK comments in @mail/quick object handling~~ NOT A BUG

- **File:** `mux/modules/engine/mail.cpp:2725, 5404` (the tracker's old `mail.cpp` path predated the engine module extraction)
- Reviewed both HACK-tagged blocks. Neither is an actual architectural hack: one encodes a deliberate sender-attribution policy for object-sent mail (player → self, wizard-owned object → object, otherwise → owner, preventing spoofing through intermediate objects); the other gates interactive `@mail` subcommands to player executors because those subcommands depend on per-session state (composing buffer, folder selection) that does not exist for objects. The comments were relabeled from `HACK` to explanatory policy notes; no code change was required.
