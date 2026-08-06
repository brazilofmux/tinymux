/*! \file dbt_reloc.h
 * \brief Code-blob relocation for guest code slots (#2129).
 *
 * A compiled program's code is *almost* position-independent: internal
 * control flow is PC-relative (B-type branches and JAL), data references
 * are absolute LUI+ADDI into regions that never move (STR/FARGS pools,
 * stack frames via s0, the blob's data), and hir_codegen emits neither
 * JALR nor AUIPC.  The only position-dependent bytes are JALs whose
 * target lies outside the program's own code region — the Tier 2 blob
 * calls.  Those are derivable without relocation records: the code blob
 * is a pure stream of 4-byte instructions (rv_compiler::code is a
 * vector of encoded words, never data), so a linear decode can classify
 * every JAL by target range and re-aim the external ones when the blob
 * is copied to a different guest base.
 *
 * That is what lets run_cached_program keep MANY programs resident in
 * one DBT: each gets its own guest code slot, the PC-keyed block cache
 * disambiguates them for free, and the str/fargs *data* keeps swapping
 * at its canonical addresses (data does not affect translation
 * validity).  Programs that ever fail this scan — a future lowering
 * emitting JALR/AUIPC or a JAL into an unexpected region — are PINNED:
 * they run only at the canonical base, which is exactly the pre-#2129
 * behaviour, so the scan is a safety valve rather than a correctness
 * gamble.
 *
 * Self-contained (no engine headers) so tests/dbt can exercise it
 * standalone; included by dbt_compile.h for the compiler pipeline.
 */

#ifndef DBT_RELOC_H
#define DBT_RELOC_H

#include <cstdint>
#include <cstring>
#include <vector>

enum rv_reloc_class : int8_t {
    RV_RELOC_UNSCANNED = 0,   // not yet classified
    RV_RELOC_OK,              // movable; extern JAL offsets recorded
    RV_RELOC_PINNED           // canonical base only
};

// Signed byte offset encoded in a J-type (JAL) instruction.
// imm[20|10:1|11|19:12] lives at bits 31|30:21|20|19:12.
//
inline int32_t rv_jal_imm(uint32_t w) {
    uint32_t imm = (((w >> 31) & 0x1u)   << 20)
                 | (((w >> 12) & 0xFFu)  << 12)
                 | (((w >> 20) & 0x1u)   << 11)
                 | (((w >> 21) & 0x3FFu) << 1);
    // Sign-extend from bit 20.
    return static_cast<int32_t>(imm << 11) >> 11;
}

// True iff `off` is encodable as a JAL displacement: 21-bit signed, even.
//
inline bool rv_jal_imm_ok(int64_t off) {
    return off >= -(1 << 20) && off < (1 << 20) && 0 == (off & 1);
}

// Re-encode a JAL's displacement, preserving rd and opcode.  The caller
// must have checked rv_jal_imm_ok().
//
inline uint32_t rv_jal_with_imm(uint32_t w, int32_t imm) {
    uint32_t u = static_cast<uint32_t>(imm);
    return (w & 0xFFFu)
         | (((u >> 20) & 0x1u)   << 31)
         | (((u >> 1)  & 0x3FFu) << 21)
         | (((u >> 11) & 0x1u)   << 20)
         | (((u >> 12) & 0xFFu)  << 12);
}

// Classify a code blob for relocation.  `code_base` is the guest address
// of code[0]; a JAL target inside [0, code_region_limit) is intra-region
// (its displacement is preserved when the whole region moves), a target
// inside [extern_lo, extern_hi) — the blob — is recorded in
// `extern_offsets` as a byte offset into `code` for later re-aiming.
// Anything else, or any JALR/AUIPC at all, pins the program.
//
inline int8_t rv_scan_extern_jals(const uint8_t *code, size_t code_size,
                                  uint64_t code_base,
                                  uint64_t code_region_limit,
                                  uint64_t extern_lo, uint64_t extern_hi,
                                  std::vector<uint32_t> &extern_offsets) {
    extern_offsets.clear();
    for (size_t o = 0; o + 4 <= code_size; o += 4) {
        uint32_t w;
        memcpy(&w, code + o, 4);
        const uint32_t op = w & 0x7Fu;
        if (0x17u == op || 0x67u == op) {       // AUIPC / JALR
            return RV_RELOC_PINNED;
        }
        if (0x6Fu != op) {                       // not JAL
            continue;
        }
        const int64_t tgt = static_cast<int64_t>(code_base)
                          + static_cast<int64_t>(o) + rv_jal_imm(w);
        if (0 <= tgt && tgt < static_cast<int64_t>(code_region_limit)) {
            continue;                            // intra — moves with the blob
        }
        if (static_cast<int64_t>(extern_lo) <= tgt
            && tgt < static_cast<int64_t>(extern_hi)) {
            extern_offsets.push_back(static_cast<uint32_t>(o));
            continue;
        }
        return RV_RELOC_PINNED;                  // JAL into no-man's-land
    }
    return RV_RELOC_OK;
}

// Re-aim one extern JAL for a code region moved by `delta` bytes: the
// target is fixed, the PC shifted, so the displacement shrinks by delta.
// Returns false if the new displacement is not encodable (the caller
// should pin the program rather than emit a wild jump).
//
inline bool rv_jal_relocate(uint32_t *w, int64_t delta) {
    const int64_t imm = static_cast<int64_t>(rv_jal_imm(*w)) - delta;
    if (!rv_jal_imm_ok(imm)) {
        return false;
    }
    *w = rv_jal_with_imm(*w, static_cast<int32_t>(imm));
    return true;
}

#endif // DBT_RELOC_H
