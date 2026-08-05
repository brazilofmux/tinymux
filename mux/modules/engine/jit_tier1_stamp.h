#ifndef JIT_TIER1_STAMP_H
#define JIT_TIER1_STAMP_H

// Per-translation-unit build stamps for the tier 1 JIT (#2061).
//
// The persisted SQLite code_cache is keyed on blob_hash (s_blob_version), and
// the invalidation model is a three-legged stool:
//
//   softlib.rv64 changes   -> the cached RV64 CALLS INTO it, and the call is
//                             emitted as a PC-relative JAL to a resolved blob
//                             address (hir_codegen.cpp:2262).  Move a blob
//                             function and cached code jumps into the middle
//                             of something else.  MUST invalidate; already
//                             covered by hashing the header, image and entry
//                             table.
//
//   tier 1 JIT changes     -> it EMITS DIFFERENT RV64 for the same softcode.
//                             MUST invalidate.  This header is that leg.
//
//   the DBT changes        -> it only EXECUTES the stored RV64; nothing about
//                             it is baked into the cached artifact, which
//                             holds guest RV64 and never host code.  A DBT
//                             bug is a DBT bug with or without the cache.
//                             MUST NOT invalidate -- throwing the cache away
//                             for a dbt_*.cpp change defeats the purpose of
//                             persisting it.
//
// The tier 1 leg used to be a single __DATE__/__TIME__ in jit_compiler.cpp,
// whose comment assumed "the JIT requires a clean rebuild ... this file is
// recompiled every such build".  Under incremental make that is false, and
// false in exactly the case it was defending against: a codegen change in
// ANOTHER translation unit is precisely when jit_compiler.cpp is NOT
// recompiled.  Measured -- touching hir_lower.cpp rebuilt hir_lower.eo and
// left jit_compiler.eo untouched, so blob_hash did not move and every
// previously persisted entry still matched.  That served the previous build's
// compiled output and contaminated a #2052 retest.
//
// So each tier 1 unit carries its own stamp and all of them fold into the
// hash.  A stamp updates when ITS OWN unit is recompiled, which is what makes
// this track incremental builds.  Headers need no stamp: changing one
// recompiles every unit that includes it, and those stamps move.
//
// ADDING A TIER 1 SOURCE FILE MEANS ADDING ITS STAMP HERE AND IN THE FOLD IN
// jit_compiler.cpp.  Forgetting reintroduces exactly the bug above, silently,
// for that file only.  The list is short and deliberately explicit rather
// than a glob, because "everything in the directory" would sweep the dbt_*
// units back in and re-add the leg that must not be there.
//
// Deliberately NOT stamped, with reasons, so the boundary is a decision
// rather than an oversight:
//
//   dbt*.cpp            execution, not production -- see above.
//   hir_lower_lua.cpp   feeds the Lua JIT, a different artifact with its own
//                       cache; it does not emit RV64 for this one.
//   ast_scan.cpp        generated from ast_scan.rl and made read-only by the
//                       build, so it cannot carry a stamp.  Residual gap: a
//                       scanner change that alters the AST for the same source
//                       without touching ast.cpp would not move the key.  In
//                       practice .rl changes land with ast.cpp changes, and
//                       JIT_COMPILER_VERSION remains the backstop for the rest.

extern const char TIER1_STAMP_AST[];
extern const char TIER1_STAMP_HIR_LOWER[];
extern const char TIER1_STAMP_HIR_SSA[];
extern const char TIER1_STAMP_HIR_OPT[];
extern const char TIER1_STAMP_HIR_CODEGEN[];

// Each unit defines its own stamp with this, so __DATE__/__TIME__ expand in
// the unit being compiled rather than in whoever reads them.
#define TIER1_STAMP_DEFINE(sym) const char sym[] = __DATE__ " " __TIME__

#endif // JIT_TIER1_STAMP_H
