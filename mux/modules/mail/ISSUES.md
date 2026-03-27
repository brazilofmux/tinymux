# Mail Module — Open Issues

Updated: 2026-03-27

## ~~High — Missing Error Handling in COM Interface Acquisition~~ FIXED

### ~~Unchecked `mux_CreateInstance()` calls in `FinalConstruct()`~~ FIXED

- All five core interface acquisitions now check `MUX_FAILED(mr)` and return early on failure, consistent with the `IID_ILog` pattern.

## Medium — Thread Safety

### Global reference counters are not thread-safe

- **File:** `mail_mod.cpp:57-58`
- **Issue:** `g_cComponents` and `g_cServerLocks` are plain `uint32_t` with unprotected increment/decrement in constructors and destructors. Race condition if modules are loaded/unloaded from multiple threads.

## Medium — Memory Management

### Possible malloc/free vs new/delete mismatch

- **File:** `mail_mod.cpp:2524-2525`
- **Issue:** `free(list)` called on a `UTF8 *` — verify the allocation matches (should be `MEMALLOC`/`MEMFREE` or `malloc`/`free`, not `new`/`delete`).

### Partial interface cleanup on `FinalConstruct()` failure

- **File:** `mail_mod.cpp:5573-5581`
- **Issue:** If `FinalConstruct()` fails partway, `Release()` is called on the object. The destructor checks each interface for null individually, which is correct — but the FinalConstruct error path doesn't release interfaces already acquired before the failing call.

## Low — Code Quality

### HACK comments in @mail/quick object handling

- **File:** `mail.cpp:2719, 5398`
- **Issue:** Multiple workarounds for `@mail/quick` from objects indicate an architectural mismatch between mail delivery and object permissions.
