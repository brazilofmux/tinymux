/*! \file test_reloc.cpp
 * \brief Code-blob relocation for guest code slots (#2129).
 *
 * The slot machinery moves a compiled program's code to a different guest
 * base and re-aims its blob-call JALs by the placement delta.  A decode or
 * re-encode error here is a wild jump in translated code, so the encoders
 * are checked against golden words produced by riscv64-unknown-elf-as
 * (2.42, rv64imd), not against each other.
 *
 * Layout constants mirror dbt_compile.h's one-shot arena: code region
 * [0, 0x4000), blob [0x10000, 0x40000), slots at 0x40000+ — but the
 * helpers take them as parameters, so this test needs no engine headers
 * beyond dbt_reloc.h (which is the point: the scan must stay standalone-
 * testable).
 */

#include "dbt_reloc.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

static int g_failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        g_failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

// Golden encodings from riscv64-unknown-elf-as (see file comment):
//
//   jal ra, .+8        -> 0x008000EF
//   jal x0, .+8        -> 0x0080006F
//   jal ra, .-16       -> 0xFF1FF0EF
//   jal ra, .+0x10000  -> 0x000100EF
//   jal x0, .-0x40000  -> 0x800C006F
//   auipc t0, 0x10     -> 0x00010297
//   jalr ra, 0(t0)     -> 0x000280E7
//
static constexpr uint32_t JAL_RA_P8     = 0x008000EF;
static constexpr uint32_t JAL_X0_P8     = 0x0080006F;
static constexpr uint32_t JAL_RA_M16    = 0xFF1FF0EF;
static constexpr uint32_t JAL_RA_P64K   = 0x000100EF;
static constexpr uint32_t JAL_X0_M256K  = 0x800C006F;
static constexpr uint32_t AUIPC_T0      = 0x00010297;
static constexpr uint32_t JALR_RA_T0    = 0x000280E7;
static constexpr uint32_t ADDI_NOP      = 0x00000013;   // addi x0, x0, 0
static constexpr uint32_t ECALL         = 0x00000073;

// The arena shape the engine uses (dbt_compile.h); parameters here.
static constexpr uint64_t CODE_LIMIT = 0x4000;
static constexpr uint64_t BLOB_BASE  = 0x10000;
static constexpr uint64_t BLOB_LIMIT = 0x40000;

static void push_word(std::vector<uint8_t> &code, uint32_t w) {
    uint8_t b[4];
    memcpy(b, &w, 4);
    code.insert(code.end(), b, b + 4);
}

// Encode a JAL rd, <byte offset> from scratch via the header's encoder.
static uint32_t make_jal(uint8_t rd, int32_t off) {
    uint32_t base = 0x6Fu | (static_cast<uint32_t>(rd) << 7);
    return rv_jal_with_imm(base, off);
}

static void test_imm_codec(void) {
    // Decode golden words.
    CHECK(rv_jal_imm(JAL_RA_P8) == 8, "jal ra,+8 decodes %d", rv_jal_imm(JAL_RA_P8));
    CHECK(rv_jal_imm(JAL_X0_P8) == 8, "jal x0,+8 decodes %d", rv_jal_imm(JAL_X0_P8));
    CHECK(rv_jal_imm(JAL_RA_M16) == -16, "jal ra,-16 decodes %d", rv_jal_imm(JAL_RA_M16));
    CHECK(rv_jal_imm(JAL_RA_P64K) == 0x10000, "jal ra,+64K decodes %d", rv_jal_imm(JAL_RA_P64K));
    CHECK(rv_jal_imm(JAL_X0_M256K) == -0x40000, "jal x0,-256K decodes %d", rv_jal_imm(JAL_X0_M256K));

    // Re-encode golden words from their decoded imms — must be identical.
    CHECK(rv_jal_with_imm(JAL_RA_P8, 8) == JAL_RA_P8, "reencode +8");
    CHECK(rv_jal_with_imm(JAL_RA_M16, -16) == JAL_RA_M16, "reencode -16");
    CHECK(rv_jal_with_imm(JAL_X0_M256K, -0x40000) == JAL_X0_M256K, "reencode -256K");

    // Encode from scratch — must match the assembler.
    CHECK(make_jal(1, 8) == JAL_RA_P8, "make_jal ra,+8 = 0x%08X", make_jal(1, 8));
    CHECK(make_jal(0, 8) == JAL_X0_P8, "make_jal x0,+8 = 0x%08X", make_jal(0, 8));
    CHECK(make_jal(1, -16) == JAL_RA_M16, "make_jal ra,-16 = 0x%08X", make_jal(1, -16));
    CHECK(make_jal(1, 0x10000) == JAL_RA_P64K, "make_jal ra,+64K = 0x%08X", make_jal(1, 0x10000));
    CHECK(make_jal(0, -0x40000) == JAL_X0_M256K, "make_jal x0,-256K = 0x%08X", make_jal(0, -0x40000));

    // Roundtrip across the full displacement range at coarse stride, plus
    // the exact boundaries.
    for (int64_t imm = -(1 << 20); imm < (1 << 20); imm += 4094) {
        int64_t even = imm & ~1LL;
        uint32_t w = make_jal(1, static_cast<int32_t>(even));
        CHECK(rv_jal_imm(w) == even, "roundtrip %lld -> %d",
              (long long)even, rv_jal_imm(w));
    }
    CHECK(rv_jal_imm(make_jal(1, -(1 << 20))) == -(1 << 20), "min imm");
    CHECK(rv_jal_imm(make_jal(1, (1 << 20) - 2)) == (1 << 20) - 2, "max imm");

    // Range predicate.
    CHECK(rv_jal_imm_ok(-(1 << 20)), "min in range");
    CHECK(rv_jal_imm_ok((1 << 20) - 2), "max in range");
    CHECK(!rv_jal_imm_ok((1 << 20)), "2^20 out of range");
    CHECK(!rv_jal_imm_ok(-(1 << 20) - 2), "-2^20-2 out of range");
    CHECK(!rv_jal_imm_ok(7), "odd out of range");
}

static void test_scan_classifies(void) {
    std::vector<uint32_t> ext;

    // A representative program: intra forward JAL, ALU noise, a blob call,
    // an intra back-JAL (loop), a second blob call, exit.
    std::vector<uint8_t> code;
    push_word(code, make_jal(0, 8));                  // 0x00: intra +8
    push_word(code, ADDI_NOP);                        // 0x04
    push_word(code, make_jal(1, BLOB_BASE + 0x120 - 0x08));  // 0x08: blob
    push_word(code, make_jal(0, -8));                 // 0x0C: intra loop
    push_word(code, make_jal(1, BLOB_BASE + 0x400 - 0x10));  // 0x10: blob
    push_word(code, ECALL);                           // 0x14

    int8_t rc = rv_scan_extern_jals(code.data(), code.size(), 0,
                                    CODE_LIMIT, BLOB_BASE, BLOB_LIMIT, ext);
    CHECK(rc == RV_RELOC_OK, "mixed program classifies OK (rc=%d)", rc);
    CHECK(ext.size() == 2, "two extern JALs found (%zu)", ext.size());
    CHECK(ext.size() == 2 && ext[0] == 0x08 && ext[1] == 0x10,
          "extern offsets 0x08/0x10");

    // Intra target beyond code_size but inside the region is still intra:
    // the whole 16 KB window moves as a unit.
    code.clear();
    push_word(code, make_jal(0, 0x3000));
    rc = rv_scan_extern_jals(code.data(), code.size(), 0,
                             CODE_LIMIT, BLOB_BASE, BLOB_LIMIT, ext);
    CHECK(rc == RV_RELOC_OK && ext.empty(), "in-region JAL is intra");

    // JALR pins.
    code.clear();
    push_word(code, JALR_RA_T0);
    rc = rv_scan_extern_jals(code.data(), code.size(), 0,
                             CODE_LIMIT, BLOB_BASE, BLOB_LIMIT, ext);
    CHECK(rc == RV_RELOC_PINNED, "JALR pins (rc=%d)", rc);

    // AUIPC pins.
    code.clear();
    push_word(code, AUIPC_T0);
    rc = rv_scan_extern_jals(code.data(), code.size(), 0,
                             CODE_LIMIT, BLOB_BASE, BLOB_LIMIT, ext);
    CHECK(rc == RV_RELOC_PINNED, "AUIPC pins (rc=%d)", rc);

    // A JAL into no-man's-land (between code region and blob) pins.
    code.clear();
    push_word(code, make_jal(1, 0x8000));            // -> str pool, bogus
    rc = rv_scan_extern_jals(code.data(), code.size(), 0,
                             CODE_LIMIT, BLOB_BASE, BLOB_LIMIT, ext);
    CHECK(rc == RV_RELOC_PINNED, "JAL into pools pins (rc=%d)", rc);

    // A JAL to a negative target pins.
    code.clear();
    push_word(code, make_jal(0, -8));                // target -8 from pc 0
    rc = rv_scan_extern_jals(code.data(), code.size(), 0,
                             CODE_LIMIT, BLOB_BASE, BLOB_LIMIT, ext);
    CHECK(rc == RV_RELOC_PINNED, "JAL below zero pins (rc=%d)", rc);
}

// Relocate a program to `slot_base` the way materialize_code_slot does,
// then verify every JAL's absolute target: intra targets must have moved
// with the code, extern targets must not have moved at all.
static void test_relocate_targets(void) {
    struct jal_site { size_t off; uint64_t canonical_target; bool extern_jal; };
    std::vector<jal_site> sites;

    std::vector<uint8_t> code;
    push_word(code, make_jal(0, 8));
    sites.push_back({0x00, 0x08, false});
    push_word(code, ADDI_NOP);
    push_word(code, make_jal(1, static_cast<int32_t>(BLOB_BASE + 0x120 - 0x08)));
    sites.push_back({0x08, BLOB_BASE + 0x120, true});
    push_word(code, make_jal(0, -8));
    sites.push_back({0x0C, 0x04, false});
    push_word(code, make_jal(1, static_cast<int32_t>(BLOB_LIMIT - 4 - 0x10)));
    sites.push_back({0x10, BLOB_LIMIT - 4, true});
    push_word(code, ECALL);

    std::vector<uint32_t> ext;
    int8_t rc = rv_scan_extern_jals(code.data(), code.size(), 0,
                                    CODE_LIMIT, BLOB_BASE, BLOB_LIMIT, ext);
    CHECK(rc == RV_RELOC_OK && ext.size() == 2, "fixture scans OK");

    // The slot bases the engine actually uses, including the farthest one.
    const uint64_t bases[] = { 0x40000, 0x44000, 0x48000, 0x4C000,
                               0x60000, 0x64000 };
    for (uint64_t slot_base : bases) {
        std::vector<uint8_t> placed = code;
        const int64_t delta = static_cast<int64_t>(slot_base);
        bool ok = true;
        for (uint32_t off : ext) {
            uint32_t w;
            memcpy(&w, placed.data() + off, 4);
            ok = ok && rv_jal_relocate(&w, delta);
            memcpy(placed.data() + off, &w, 4);
        }
        CHECK(ok, "relocation encodable at base 0x%llX",
              (unsigned long long)slot_base);

        for (const jal_site &s : sites) {
            uint32_t w;
            memcpy(&w, placed.data() + s.off, 4);
            uint64_t pc = slot_base + s.off;
            uint64_t tgt = pc + static_cast<int64_t>(rv_jal_imm(w));
            uint64_t want = s.extern_jal ? s.canonical_target
                                         : slot_base + s.canonical_target;
            CHECK(tgt == want,
                  "base 0x%llX off 0x%zX: target 0x%llX want 0x%llX",
                  (unsigned long long)slot_base, s.off,
                  (unsigned long long)tgt, (unsigned long long)want);
            // rd and opcode untouched.
            uint32_t orig;
            memcpy(&orig, code.data() + s.off, 4);
            CHECK((w & 0xFFFu) == (orig & 0xFFFu), "rd/opcode preserved");
        }
    }

    // A delta that pushes the displacement out of JAL range must refuse,
    // not wrap: from base 0x110000 the blob at 0x10000 is exactly -1 MB - e.
    {
        uint32_t w = make_jal(1, static_cast<int32_t>(BLOB_BASE) - 0x08);
        CHECK(!rv_jal_relocate(&w, 0x110000),
              "out-of-range relocation refuses");
        CHECK(rv_jal_imm(w) == static_cast<int32_t>(BLOB_BASE) - 0x08,
              "refused relocation leaves the word untouched");
    }
}

int main(void) {
    test_imm_codec();
    test_scan_classifies();
    test_relocate_targets();

    if (g_failures) {
        printf("test_reloc: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    printf("test_reloc: all tests passed\n");
    return 0;
}
