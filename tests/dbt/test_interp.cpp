/*! \file test_dbt_interp.cpp
 * \brief Guest memory bounds checks for the RV64 interpreter route.
 *
 * mem_check() gates every guest read and write in dbt_interp.cpp.  It used
 * to be `addr + len <= mem->size`, which is unsigned 64-bit arithmetic and
 * wraps: for an addr within len of UINT64_MAX the sum becomes a small
 * number, compares below size, and the caller then indexes mem->data with
 * the unwrapped addr (#1292).
 *
 * mem_check and the accessors are file-static, so this includes the
 * translation unit directly rather than linking against it.  That is also
 * why there are no stubs here -- dbt_interp.cpp depends only on
 * dbt_interp.h, dbt_decoder.h and the standard library.
 *
 * The wrap cases are the ones that discriminate: every other case below
 * behaves identically under both the old and new check, so a run that
 * passes them all proves nothing on its own.  Reverting mem_check to
 * `addr + len <= mem->size` must fail exactly the WRAP cases.
 */
// No standard headers here.  dbt_interp.cpp already includes <cstdio>,
// <cstring>, <cmath>, <climits> and <cfloat>, and pulling any of them in
// alongside the engine source breaks <type_traits> in this arrangement.
// Everything below uses only what the engine TU already provides.
//
#include "dbt_interp.cpp"

static int g_pass = 0;
static int g_fail = 0;

static void expect(bool cond, const char *what) {
    if (cond) {
        g_pass++;
    } else {
        g_fail++;
        printf("FAIL: %s\n", what);
    }
}

int main() {
    printf("=== DBT interpreter guest memory bounds ===\n\n");

    // A plain buffer rather than std::vector: this TU includes engine
    // source, whose headers collide with <vector>.
    //
    static const size_t SIZE = 1u << 20;          // 1 MB guest image
    static uint8_t backing[SIZE];
    memset(backing, 0xAB, SIZE);
    rv64_memory_t mem;
    mem.data = backing;
    mem.size = SIZE;

    // --- legitimate accesses still work (guards over-tightening) ---
    mem_write8(&mem, 0x1000, 0x5A);
    expect(mem_read8(&mem, 0x1000) == 0x5A, "in-range byte round-trips");

    mem_write64(&mem, SIZE - 8, 0x0123456789ABCDEFull);
    expect(mem_read64(&mem, SIZE - 8) == 0x0123456789ABCDEFull,
           "8-byte access ending exactly at the limit is allowed");

    expect(mem_check(&mem, SIZE, 0), "zero-length access at the limit is allowed");
    expect(mem_check(&mem, 0, SIZE), "whole-image access is allowed");

    // --- ordinary out-of-range is refused (unchanged by #1292) ---
    expect(!mem_check(&mem, SIZE - 7, 8), "8 bytes ending one past the limit is refused");
    expect(!mem_check(&mem, SIZE + 4096, 8), "far out-of-range is refused");
    expect(!mem_check(&mem, SIZE, 1), "one byte at the limit is refused");

    // --- WRAP: the #1292 cases.  These are the only ones that
    //     discriminate against the pre-fix check. ---
    expect(!mem_check(&mem, 0xFFFFFFFFFFFFFFFFull, 2),
           "WRAP addr=2^64-1 len=2 must be refused");
    expect(!mem_check(&mem, 0xFFFFFFFFFFFFFFFCull, 8),
           "WRAP addr=2^64-4 len=8 must be refused");
    expect(!mem_check(&mem, 0xFFFFFFFFFFFFF000ull, 8192),
           "WRAP addr near 2^64 with a large len must be refused");

    // A wrapping read must also return the safe value rather than index
    // the backing store, which is what the check exists to prevent.
    expect(mem_read8(&mem, 0xFFFFFFFFFFFFFFFFull) == 0,
           "WRAP read8 returns 0 rather than indexing out of bounds");
    expect(mem_read64(&mem, 0xFFFFFFFFFFFFFFFCull) == 0,
           "WRAP read64 returns 0 rather than indexing out of bounds");

    // A wrapping write must leave the image untouched.  Byte 0 is the
    // one an unwrapped index would be most likely to reach.
    const uint8_t before = backing[0];
    mem_write64(&mem, 0xFFFFFFFFFFFFFFFCull, 0xDEADBEEFDEADBEEFull);
    expect(backing[0] == before, "WRAP write64 must not modify guest memory");

    printf("\n=== dbt interp: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
