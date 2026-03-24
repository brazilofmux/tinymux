## Review: `5e239f616465f5dc0afc71086b7e74058231ab0a..HEAD`

### Findings

1. `@dbclean` can leave the running server inconsistent if the SQLite purge fails.
   Files: `mux/modules/engine/vattr.cpp:94`, `mux/modules/engine/vattr.cpp:126`
   The new implementation deletes entries from `mudstate.vattr_name_map`, `mudstate.vattr_numbers`, and `anum_table` before it knows whether `PurgeOrphanedAttrNames()` succeeded. If SQLite returns an error, the command exits after the in-memory registry has already been mutated, so the live process and the database disagree until restart. This should be reversed: purge in SQLite first, ideally in a transaction, and only then commit the in-memory removals.

2. The new `rv64_strtod` intrinsic no longer matches engine numeric parsing semantics.
   Files: `mux/modules/engine/jit_compiler.cpp:504`, `mux/lib/mathutil.cpp:407`
   `host_strtod()` now calls libc `strtod()`, but the existing engine path uses `mux_atof()`/`mux_strtod()`, which implements TinyMUX-specific parsing rules, special float strings, and a 100-byte input clamp. Any JIT/native math path that reaches `rv64_strtod` can therefore produce different results from the ECALL path. A simple example is trailing junk: `strtod("12x", ...)` yields `12`, while strict `mux_atof("12x")` rejects it and returns `0`. The intrinsic should delegate to `mux_atof()` or `mux_strtod()` instead of raw libc parsing.

3. The native FP fast path bypasses legacy non-IEEE domain guards.
   Files: `mux/modules/engine/hir_lower.cpp:2219`, `mux/modules/engine/funmath.cpp:1761`, `mux/modules/engine/funmath.cpp:1881`
   The new `HIR_FCALL1`/`HIR_FCALL2` lowering sends numeric calls like `ASIN`, `ACOS`, `POWER`, and `FMOD` straight to libm when the arguments are provably numeric. The legacy function implementations still have `#ifndef HAVE_IEEE_FP_SNAN` checks that return `"Ind"` instead of evaluating invalid domains. On non-IEEE builds, the JIT/native path now diverges from the established interpreter behavior.

### Opportunities

1. Add parity smoke tests for numeric conversion in the new FP path.
   Coverage is missing for cases that stress the parser boundary: trailing junk, `+Inf`/`-Inf`/`Ind`/`NaN`, and very long numeric strings. Those cases are exactly where `strtod()` vs `mux_atof()` differences show up.

2. Add focused tests for the new maintenance/config work.
   I did not find smoke coverage for `@dbclean` or `vlimit`. Both features are easy to regress quietly, and each now has behavior that depends on storage-layer details rather than purely local logic.
