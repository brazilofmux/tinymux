// stubs.cpp — Provide symbols required by sqlitedb.cpp that live in engine.so.
// The standalone test harness doesn't link the engine, so we stub these out.

#include <cstdint>

// JIT mod_count stubs — not exercised in standalone DB tests.
uint32_t attr_mod_count_get(int, int) { return 0; }
void     attr_mod_count_invalidate_all() {}
