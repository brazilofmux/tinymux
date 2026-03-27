# Comsys Module — Open Issues

Updated: 2026-03-27

## High — Missing Error Handling in COM Interface Acquisition

### Unchecked `mux_CreateInstance()` calls in `FinalConstruct()`

- **File:** `comsys_mod.cpp:142-160`
- **Issue:** Five `mux_CreateInstance()` calls for `IID_INotify`, `IID_IObjectInfo`, `IID_IAttributeAccess`, `IID_IEvaluator`, and `IID_IPermissions` do not check return values. Null pointers will be dereferenced if any creation fails.
- **Fix:** Check `MUX_FAILED(mr)` after each call.

## Medium — Thread Safety

### Global reference counters are not thread-safe

- **File:** `comsys_mod.cpp:37-38`
- **Issue:** `g_cComponents` and `g_cServerLocks` are plain `uint32_t` with unprotected increment/decrement. Same pattern as mail module.

## Medium — Data Integrity

### Channel consistency checks unimplemented

- **File:** `comsys_mod.cpp:2942`
- **Issue:** `// TODO: Channel consistency checks.` — `dbck()` method is empty. If the channel list or membership becomes corrupted, there is no validation or repair path.

## Low — Code Quality

### `strncpy()` usage with casts

- **File:** `comsys_mod.cpp:309, 315`
- **Issue:** `strncpy()` with casts to `char *` — verify source strings are null-terminated and destination buffers are properly sized.

### No parameter validation on PlayerNuke

- **File:** `comsys_mod.cpp` (around line 2900-2920)
- **Issue:** Relies on caller ensuring player dbref is valid. No range validation before use.
