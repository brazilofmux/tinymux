/*! \file dbt_emit_a64.h
 * \brief Lightweight AArch64 code emitter for RV64IMD JIT compilation.
 *
 * Convention (AAPCS64):
 *   X19 = pointer to rv64_ctx_t (guest register file + state)
 *   X20 = pointer to guest memory base
 *   X21 = pointer to block cache base
 *   X0, X1, X2 = scratch (never cached)
 *   X9-X12, X22-X25 = register cache (8 slots)
 *   D0, D1 = FP scratch
 *   D16-D21 = FP cache (6 slots)
 *
 * All instructions are 32-bit fixed-width, little-endian.
 * Guest registers are 64-bit (RV64).  W-suffix instructions use 32-bit
 * operations followed by SXTW sign-extension.
 *
 * Reference: ~/slow-32/tools/dbt/emit_a64.h
 */

#ifndef DBT_EMIT_A64_H
#define DBT_EMIT_A64_H

#include "dbt.h"
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------
// Code buffer (same struct as x86-64 emitter)
// ---------------------------------------------------------------

struct emit_t {
    uint8_t *buf;
    uint32_t offset;
    uint32_t capacity;
};

static inline void emit_inst(emit_t *e, uint32_t inst) {
    if (e->offset + 4 <= e->capacity)
        memcpy(e->buf + e->offset, &inst, 4);
    e->offset += 4;
}

// Buffer management (same interface as x86-64).
static inline void emit_byte(emit_t *e, uint8_t b) {
    if (e->offset < e->capacity)
        e->buf[e->offset] = b;
    e->offset++;
}

static inline void emit_bytes(emit_t *e, const void *data, int len) {
    if (e->offset + len <= e->capacity)
        memcpy(e->buf + e->offset, data, len);
    e->offset += len;
}

static inline void emit_u32(emit_t *e, uint32_t val) {
    emit_bytes(e, &val, 4);
}

static inline void emit_u64(emit_t *e, uint64_t val) {
    emit_bytes(e, &val, 8);
}

static inline uint32_t emit_pos(emit_t *e) {
    return e->offset;
}

// ---------------------------------------------------------------
// AArch64 register constants
// ---------------------------------------------------------------

static constexpr int A64_X0  = 0;
static constexpr int A64_X1  = 1;
static constexpr int A64_X2  = 2;
static constexpr int A64_X3  = 3;
static constexpr int A64_X4  = 4;
static constexpr int A64_X5  = 5;
static constexpr int A64_X6  = 6;
static constexpr int A64_X7  = 7;
static constexpr int A64_X8  = 8;
static constexpr int A64_X9  = 9;
static constexpr int A64_X10 = 10;
static constexpr int A64_X11 = 11;
static constexpr int A64_X12 = 12;
static constexpr int A64_X13 = 13;
static constexpr int A64_X14 = 14;
static constexpr int A64_X15 = 15;
static constexpr int A64_X16 = 16;
static constexpr int A64_X17 = 17;
static constexpr int A64_X18 = 18;
static constexpr int A64_X19 = 19;
static constexpr int A64_X20 = 20;
static constexpr int A64_X21 = 21;
static constexpr int A64_X22 = 22;
static constexpr int A64_X23 = 23;
static constexpr int A64_X24 = 24;
static constexpr int A64_X25 = 25;
static constexpr int A64_X26 = 26;
static constexpr int A64_X27 = 27;
static constexpr int A64_X28 = 28;
static constexpr int A64_X29 = 29;  // frame pointer
static constexpr int A64_X30 = 30;  // link register (LR)
static constexpr int A64_XZR = 31;  // zero register / SP (context-dependent)
static constexpr int A64_SP  = 31;

// FP/SIMD register indices (D0-D31 for doubles).
static constexpr int A64_D0  = 0;
static constexpr int A64_D1  = 1;

// ---------------------------------------------------------------
// Condition codes
// ---------------------------------------------------------------

static constexpr uint8_t A64_COND_EQ = 0x0;
static constexpr uint8_t A64_COND_NE = 0x1;
static constexpr uint8_t A64_COND_CS = 0x2;  // unsigned >=
static constexpr uint8_t A64_COND_CC = 0x3;  // unsigned <
static constexpr uint8_t A64_COND_MI = 0x4;
static constexpr uint8_t A64_COND_PL = 0x5;
static constexpr uint8_t A64_COND_VS = 0x6;
static constexpr uint8_t A64_COND_VC = 0x7;
static constexpr uint8_t A64_COND_HI = 0x8;  // unsigned >
static constexpr uint8_t A64_COND_LS = 0x9;  // unsigned <=
static constexpr uint8_t A64_COND_GE = 0xA;
static constexpr uint8_t A64_COND_LT = 0xB;
static constexpr uint8_t A64_COND_GT = 0xC;
static constexpr uint8_t A64_COND_LE = 0xD;
static constexpr uint8_t A64_COND_AL = 0xE;

// ---------------------------------------------------------------
// Logical immediate encoding
// ---------------------------------------------------------------
//
// AArch64 logical immediates use a complex encoding for repeating
// bit patterns.  Returns false for non-encodable values (0, ~0).
//
static inline bool a64_encode_logical_imm64(uint64_t val, uint32_t *enc) {
    if (val == 0 || val == ~0ULL) return false;

    // Try each element size: 2, 4, 8, 16, 32, 64 bits.
    for (int size = 2; size <= 64; size *= 2) {
        uint64_t mask = (size == 64) ? ~0ULL : ((1ULL << size) - 1);
        uint64_t elem = val & mask;

        // Check that the value is a repeating pattern of this element.
        bool repeats = true;
        for (int i = size; i < 64; i += size) {
            if (((val >> i) & mask) != elem) { repeats = false; break; }
        }
        if (!repeats) continue;

        // Count trailing ones in the rotated element.
        // Find rotation: rotate elem right until bit 0 is 1 and the
        // highest set bit is contiguous.
        for (int rot = 0; rot < size; rot++) {
            uint64_t rotated = ((elem >> rot) | (elem << (size - rot))) & mask;
            // Check if rotated is a contiguous run of ones from bit 0.
            uint64_t ones = rotated;
            if (ones == 0) continue;
            int count = 0;
            while (ones & 1) { count++; ones >>= 1; }
            if (ones != 0) continue;  // not contiguous

            // Found: count ones starting from bit 0 after rotating by rot.
            int N = (size == 64) ? 1 : 0;
            int immr = (size - rot) % size;
            // imms encodes the ones count and element size.
            // For size=64: imms = count - 1
            // For size<64: imms = ((-size * 2) & 0x3F) | (count - 1)
            int imms;
            if (size == 64) {
                imms = count - 1;
            } else {
                imms = ((-size * 2) & 0x3F) | (count - 1);
            }
            *enc = (N << 12) | (immr << 6) | (imms & 0x3F);
            return true;
        }
    }
    return false;
}

// 32-bit variant: mask to 32 bits, use N=0.
static inline bool a64_encode_logical_imm32(uint32_t val, uint32_t *enc) {
    if (val == 0 || val == 0xFFFFFFFF) return false;
    // Extend to 64-bit repeating pattern and encode with N=0.
    uint64_t val64 = (static_cast<uint64_t>(val) << 32) | val;
    uint32_t result;
    if (!a64_encode_logical_imm64(val64, &result)) return false;
    // N must be 0 for 32-bit.
    if (result & (1 << 12)) return false;
    *enc = result;
    return true;
}

// ---------------------------------------------------------------
// Data movement — registers
// ---------------------------------------------------------------

// MOV Xd, Xs (via ORR Xd, XZR, Xs)
static inline void emit_mov_r64(emit_t *e, int rd, int rs) {
    if (rd == rs) return;
    emit_inst(e, 0xAA0003E0 | (rs << 16) | rd);
}

// MOV Wd, Ws (via ORR Wd, WZR, Ws) — 32-bit, zero-extends to 64
static inline void emit_mov_r32(emit_t *e, int rd, int rs) {
    emit_inst(e, 0x2A0003E0 | (rs << 16) | rd);
}

// ---------------------------------------------------------------
// Data movement — immediates
// ---------------------------------------------------------------

// MOVZ Xd, #imm16, LSL #shift  (shift = 0, 16, 32, 48)
static inline void emit_movz_x64(emit_t *e, int rd, uint16_t imm16, int shift) {
    int hw = shift / 16;
    emit_inst(e, 0xD2800000 | (hw << 21) | (imm16 << 5) | rd);
}

// MOVK Xd, #imm16, LSL #shift
static inline void emit_movk_x64(emit_t *e, int rd, uint16_t imm16, int shift) {
    int hw = shift / 16;
    emit_inst(e, 0xF2800000 | (hw << 21) | (imm16 << 5) | rd);
}

// Build a full 64-bit immediate in rd.  Uses MOVZ + up to 3 MOVK,
// skipping zero halfwords.
//
static inline void emit_mov_r64_imm64(emit_t *e, int rd, uint64_t imm) {
    // Find the first non-zero halfword.
    int first = -1;
    for (int i = 0; i < 4; i++) {
        if ((imm >> (i * 16)) & 0xFFFF) {
            if (first < 0) first = i;
        }
    }
    if (first < 0) {
        // All zero: MOVZ Xd, #0
        emit_movz_x64(e, rd, 0, 0);
        return;
    }

    emit_movz_x64(e, rd, static_cast<uint16_t>((imm >> (first * 16)) & 0xFFFF),
                   first * 16);
    for (int i = first + 1; i < 4; i++) {
        uint16_t hw = static_cast<uint16_t>((imm >> (i * 16)) & 0xFFFF);
        if (hw) emit_movk_x64(e, rd, hw, i * 16);
    }
}

// Load a sign-extended 32-bit immediate.  Uses MOVZ/MOVN + optional MOVK.
//
static inline void emit_mov_r64_imm32(emit_t *e, int rd, int32_t imm) {
    if (imm >= 0) {
        emit_mov_r64_imm64(e, rd, static_cast<uint64_t>(static_cast<uint32_t>(imm)));
    } else {
        // MOVN Xd, #~(low16), LSL #0
        uint64_t uval = static_cast<uint64_t>(imm);
        emit_mov_r64_imm64(e, rd, uval);
    }
}

// ---------------------------------------------------------------
// 64-bit integer ALU — register
// ---------------------------------------------------------------

static inline void emit_add_r64(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x8B000000 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_sub_r64(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0xCB000000 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_and_r64(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x8A000000 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_orr_r64(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0xAA000000 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_eor_r64(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0xCA000000 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_neg_r64(emit_t *e, int rd, int rm) {
    emit_sub_r64(e, rd, A64_XZR, rm);
}

// MUL Xd, Xn, Xm (via MADD Xd, Xn, Xm, XZR)
static inline void emit_mul_r64(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x9B007C00 | (rm << 16) | (rn << 5) | rd);
}

// SMULH Xd, Xn, Xm — signed high multiply (upper 64 bits of 128-bit product)
static inline void emit_smulh(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x9B400000 | (rm << 16) | (0x1F << 10) | (rn << 5) | rd);
}

// UMULH Xd, Xn, Xm — unsigned high multiply
static inline void emit_umulh(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x9BC00000 | (rm << 16) | (0x1F << 10) | (rn << 5) | rd);
}

// SDIV Xd, Xn, Xm — hardware signed divide (single instruction!)
static inline void emit_sdiv_r64(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x9AC00C00 | (rm << 16) | (rn << 5) | rd);
}

// UDIV Xd, Xn, Xm — hardware unsigned divide
static inline void emit_udiv_r64(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x9AC00800 | (rm << 16) | (rn << 5) | rd);
}

// MSUB Xd, Xn, Xm, Xa — Xa - Xn*Xm (used for remainder)
static inline void emit_msub_r64(emit_t *e, int rd, int rn, int rm, int ra) {
    emit_inst(e, 0x9B008000 | (rm << 16) | (ra << 10) | (rn << 5) | rd);
}

// ---------------------------------------------------------------
// 64-bit integer ALU — immediate
// ---------------------------------------------------------------

// ADD Xd, Xn, #imm12
static inline void emit_add_r64_imm(emit_t *e, int rd, int rn, uint32_t imm12) {
    emit_inst(e, 0x91000000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rd);
}

// SUB Xd, Xn, #imm12
static inline void emit_sub_r64_imm(emit_t *e, int rd, int rn, uint32_t imm12) {
    emit_inst(e, 0xD1000000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rd);
}

// AND Xd, Xn, #imm (logical immediate encoding)
// Returns false if imm is not encodable.
static inline bool emit_and_r64_imm(emit_t *e, int rd, int rn, uint64_t imm) {
    uint32_t enc;
    if (!a64_encode_logical_imm64(imm, &enc)) return false;
    emit_inst(e, 0x92000000 | (enc << 10) | (rn << 5) | rd);
    return true;
}

// ORR Xd, Xn, #imm
static inline bool emit_orr_r64_imm(emit_t *e, int rd, int rn, uint64_t imm) {
    uint32_t enc;
    if (!a64_encode_logical_imm64(imm, &enc)) return false;
    emit_inst(e, 0xB2000000 | (enc << 10) | (rn << 5) | rd);
    return true;
}

// EOR Xd, Xn, #imm
static inline bool emit_eor_r64_imm(emit_t *e, int rd, int rn, uint64_t imm) {
    uint32_t enc;
    if (!a64_encode_logical_imm64(imm, &enc)) return false;
    emit_inst(e, 0xD2000000 | (enc << 10) | (rn << 5) | rd);
    return true;
}

// ---------------------------------------------------------------
// Shifts — 64-bit
// ---------------------------------------------------------------

// LSL Xd, Xn, #shift (via UBFM Xd, Xn, #(64-shift), #(63-shift))
static inline void emit_lsl_r64_imm(emit_t *e, int rd, int rn, uint8_t shift) {
    if (shift == 0) { emit_mov_r64(e, rd, rn); return; }
    int immr = (64 - shift) & 63;
    int imms = 63 - shift;
    emit_inst(e, 0xD3400000 | (immr << 16) | (imms << 10) | (rn << 5) | rd);
}

// LSR Xd, Xn, #shift (via UBFM Xd, Xn, #shift, #63)
static inline void emit_lsr_r64_imm(emit_t *e, int rd, int rn, uint8_t shift) {
    emit_inst(e, 0xD340FC00 | (shift << 16) | (rn << 5) | rd);
}

// ASR Xd, Xn, #shift (via SBFM Xd, Xn, #shift, #63)
static inline void emit_asr_r64_imm(emit_t *e, int rd, int rn, uint8_t shift) {
    emit_inst(e, 0x9340FC00 | (shift << 16) | (rn << 5) | rd);
}

// LSLV Xd, Xn, Xm — shift left by register
static inline void emit_lslv_r64(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x9AC02000 | (rm << 16) | (rn << 5) | rd);
}

// LSRV Xd, Xn, Xm
static inline void emit_lsrv_r64(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x9AC02400 | (rm << 16) | (rn << 5) | rd);
}

// ASRV Xd, Xn, Xm
static inline void emit_asrv_r64(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x9AC02800 | (rm << 16) | (rn << 5) | rd);
}

// ---------------------------------------------------------------
// 32-bit integer ALU (for RV64 W-suffix instructions)
// ---------------------------------------------------------------

static inline void emit_add_r32(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x0B000000 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_sub_r32(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x4B000000 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_mul_r32(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x1B007C00 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_sdiv_r32(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x1AC00C00 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_udiv_r32(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x1AC00800 | (rm << 16) | (rn << 5) | rd);
}

// SXTW Xd, Wn — sign-extend 32→64 (via SBFM Xd, Xn, #0, #31)
static inline void emit_sxtw(emit_t *e, int rd, int rn) {
    emit_inst(e, 0x93407C00 | (rn << 5) | rd);
}

// 32-bit shifts by immediate
static inline void emit_lsl_r32_imm(emit_t *e, int rd, int rn, uint8_t shift) {
    if (shift == 0) { emit_mov_r32(e, rd, rn); return; }
    int immr = (32 - shift) & 31;
    int imms = 31 - shift;
    emit_inst(e, 0x53000000 | (immr << 16) | (imms << 10) | (rn << 5) | rd);
}

static inline void emit_lsr_r32_imm(emit_t *e, int rd, int rn, uint8_t shift) {
    emit_inst(e, 0x53007C00 | (shift << 16) | (rn << 5) | rd);
}

static inline void emit_asr_r32_imm(emit_t *e, int rd, int rn, uint8_t shift) {
    emit_inst(e, 0x13007C00 | (shift << 16) | (rn << 5) | rd);
}

// 32-bit shifts by register
static inline void emit_lslv_r32(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x1AC02000 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_lsrv_r32(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x1AC02400 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_asrv_r32(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x1AC02800 | (rm << 16) | (rn << 5) | rd);
}

// ---------------------------------------------------------------
// Compare and conditional
// ---------------------------------------------------------------

// CMP Xn, Xm (SUBS XZR, Xn, Xm)
static inline void emit_cmp_r64(emit_t *e, int rn, int rm) {
    emit_inst(e, 0xEB00001F | (rm << 16) | (rn << 5));
}

// CMP Xn, #imm12 (SUBS XZR, Xn, #imm12)
static inline void emit_cmp_r64_imm(emit_t *e, int rn, uint32_t imm12) {
    emit_inst(e, 0xF100001F | ((imm12 & 0xFFF) << 10) | (rn << 5));
}

// CMP Wn, Wm (32-bit)
static inline void emit_cmp_r32(emit_t *e, int rn, int rm) {
    emit_inst(e, 0x6B00001F | (rm << 16) | (rn << 5));
}

// TST Xn, Xm (ANDS XZR, Xn, Xm)
static inline void emit_tst_r64(emit_t *e, int rn, int rm) {
    emit_inst(e, 0xEA00001F | (rm << 16) | (rn << 5));
}

// CSET Xd, cond (via CSINC Xd, XZR, XZR, invert(cond))
static inline void emit_cset(emit_t *e, int rd, uint8_t cond) {
    emit_inst(e, 0x9A9F07E0 | ((cond ^ 1) << 12) | rd);
}

// CSEL Xd, Xn, Xm, cond
static inline void emit_csel(emit_t *e, int rd, int rn, int rm, uint8_t cond) {
    emit_inst(e, 0x9A800000 | (rm << 16) | (cond << 12) | (rn << 5) | rd);
}

// ---------------------------------------------------------------
// Load/Store — context access (immediate offset from ctx pointer)
// ---------------------------------------------------------------

// LDR Xt, [Xn, #imm12*8] — 64-bit load, unsigned offset (scaled by 8)
static inline void emit_ldr_x64_imm(emit_t *e, int rt, int rn, uint32_t byte_off) {
    uint32_t scaled = byte_off / 8;
    emit_inst(e, 0xF9400000 | (scaled << 10) | (rn << 5) | rt);
}

// STR Xt, [Xn, #imm12*8] — 64-bit store, unsigned offset
static inline void emit_str_x64_imm(emit_t *e, int rt, int rn, uint32_t byte_off) {
    uint32_t scaled = byte_off / 8;
    emit_inst(e, 0xF9000000 | (scaled << 10) | (rn << 5) | rt);
}

// LDR Wt, [Xn, #imm12*4] — 32-bit load (zero-extend)
static inline void emit_ldr_w32_imm(emit_t *e, int rt, int rn, uint32_t byte_off) {
    uint32_t scaled = byte_off / 4;
    emit_inst(e, 0xB9400000 | (scaled << 10) | (rn << 5) | rt);
}

// STR Wt, [Xn, #imm12*4]
static inline void emit_str_w32_imm(emit_t *e, int rt, int rn, uint32_t byte_off) {
    uint32_t scaled = byte_off / 4;
    emit_inst(e, 0xB9000000 | (scaled << 10) | (rn << 5) | rt);
}

// Guest register access: load/store via context pointer (X19).
// CTX_X_OFF = 0, so guest reg n is at [X19 + n*8].
//
static inline void emit_load_guest(emit_t *e, int host_reg, int guest_reg) {
    emit_ldr_x64_imm(e, host_reg, A64_X19, CTX_X_OFF + guest_reg * 8);
}

static inline void emit_store_guest(emit_t *e, int guest_reg, int host_reg) {
    emit_str_x64_imm(e, host_reg, A64_X19, CTX_X_OFF + guest_reg * 8);
}

// ---------------------------------------------------------------
// Load/Store — guest memory (base + index register)
// ---------------------------------------------------------------
//
// Guest memory base is in X20.  Address = X20 + Xindex.
// For all sizes, use register-offset addressing: [X20, Xm]
// (SXTW variant for 32-bit indices if needed).

// LDR Xt, [X20, Xm] — 64-bit load from guest memory
static inline void emit_load_mem64(emit_t *e, int rt, int idx) {
    // LDR Xt, [X20, Xm, LSL #0] — unscaled register offset
    emit_inst(e, 0xF8606800 | (idx << 16) | (A64_X20 << 5) | rt);
}

// STR Xt, [X20, Xm]
static inline void emit_store_mem64(emit_t *e, int idx, int rt) {
    emit_inst(e, 0xF8206800 | (idx << 16) | (A64_X20 << 5) | rt);
}

// LDRSW Xt, [X20, Xm] — 32-bit load, sign-extend to 64
static inline void emit_load_mem32s(emit_t *e, int rt, int idx) {
    emit_inst(e, 0xB8A06800 | (idx << 16) | (A64_X20 << 5) | rt);
}

// LDR Wt, [X20, Xm] — 32-bit load, zero-extend
static inline void emit_load_mem32(emit_t *e, int rt, int idx) {
    emit_inst(e, 0xB8606800 | (idx << 16) | (A64_X20 << 5) | rt);
}

// STR Wt, [X20, Xm]
static inline void emit_store_mem32(emit_t *e, int idx, int rt) {
    emit_inst(e, 0xB8206800 | (idx << 16) | (A64_X20 << 5) | rt);
}

// LDRH Wt, [X20, Xm] — 16-bit load, zero-extend
static inline void emit_load_mem16u(emit_t *e, int rt, int idx) {
    emit_inst(e, 0x78606800 | (idx << 16) | (A64_X20 << 5) | rt);
}

// LDRSH Xt, [X20, Xm] — 16-bit load, sign-extend to 64
static inline void emit_load_mem16s(emit_t *e, int rt, int idx) {
    emit_inst(e, 0x78A06800 | (idx << 16) | (A64_X20 << 5) | rt);
}

// STRH Wt, [X20, Xm]
static inline void emit_store_mem16(emit_t *e, int idx, int rt) {
    emit_inst(e, 0x78206800 | (idx << 16) | (A64_X20 << 5) | rt);
}

// LDRB Wt, [X20, Xm] — 8-bit load, zero-extend
static inline void emit_load_mem8u(emit_t *e, int rt, int idx) {
    emit_inst(e, 0x38606800 | (idx << 16) | (A64_X20 << 5) | rt);
}

// LDRSB Xt, [X20, Xm] — 8-bit load, sign-extend to 64
static inline void emit_load_mem8s(emit_t *e, int rt, int idx) {
    emit_inst(e, 0x38A06800 | (idx << 16) | (A64_X20 << 5) | rt);
}

// STRB Wt, [X20, Xm]
static inline void emit_store_mem8(emit_t *e, int idx, int rt) {
    emit_inst(e, 0x38206800 | (idx << 16) | (A64_X20 << 5) | rt);
}

// ---------------------------------------------------------------
// Branches
// ---------------------------------------------------------------

// B offset — unconditional branch, ±128MB range.
// offset is in bytes from the B instruction.
// Returns the byte offset of the instruction for patching.
//
static inline uint32_t emit_b(emit_t *e, int32_t byte_offset) {
    uint32_t pos = emit_pos(e);
    int32_t imm26 = byte_offset >> 2;
    emit_inst(e, 0x14000000 | (imm26 & 0x03FFFFFF));
    return pos;
}

// B.cond offset — conditional branch, ±1MB range.
static inline uint32_t emit_b_cond(emit_t *e, uint8_t cond, int32_t byte_offset) {
    uint32_t pos = emit_pos(e);
    int32_t imm19 = byte_offset >> 2;
    emit_inst(e, 0x54000000 | ((imm19 & 0x7FFFF) << 5) | (cond & 0xF));
    return pos;
}

// CBZ Xt, offset — branch if zero (64-bit), ±1MB
static inline uint32_t emit_cbz_x64(emit_t *e, int rt, int32_t byte_offset) {
    uint32_t pos = emit_pos(e);
    int32_t imm19 = byte_offset >> 2;
    emit_inst(e, 0xB4000000 | ((imm19 & 0x7FFFF) << 5) | rt);
    return pos;
}

// CBNZ Xt, offset — branch if non-zero (64-bit)
static inline uint32_t emit_cbnz_x64(emit_t *e, int rt, int32_t byte_offset) {
    uint32_t pos = emit_pos(e);
    int32_t imm19 = byte_offset >> 2;
    emit_inst(e, 0xB5000000 | ((imm19 & 0x7FFFF) << 5) | rt);
    return pos;
}

// BR Xn — indirect branch
static inline void emit_br(emit_t *e, int rn) {
    emit_inst(e, 0xD61F0000 | (rn << 5));
}

// BLR Xn — branch with link (call)
static inline void emit_blr(emit_t *e, int rn) {
    emit_inst(e, 0xD63F0000 | (rn << 5));
}

// RET — return (BR X30)
static inline void emit_ret(emit_t *e) {
    emit_inst(e, 0xD65F03C0);
}

// NOP
static inline void emit_nop(emit_t *e) {
    emit_inst(e, 0xD503201F);
}

// ---------------------------------------------------------------
// Branch patching
// ---------------------------------------------------------------

// Patch a B instruction at patch_offset to jump to target_offset.
// Both offsets are relative to e->buf.
//
static inline void emit_patch_b26(emit_t *e, uint32_t patch_offset,
                                   uint32_t target_offset) {
    int32_t byte_diff = static_cast<int32_t>(target_offset - patch_offset);
    int32_t imm26 = byte_diff >> 2;
    uint32_t inst = 0x14000000 | (imm26 & 0x03FFFFFF);
    memcpy(e->buf + patch_offset, &inst, 4);
}

// Patch a B.cond / CBZ / CBNZ at patch_offset (preserves opcode/cond,
// replaces imm19 field).
//
static inline void emit_patch_b19(emit_t *e, uint32_t patch_offset,
                                   uint32_t target_offset) {
    int32_t byte_diff = static_cast<int32_t>(target_offset - patch_offset);
    int32_t imm19 = byte_diff >> 2;
    uint32_t orig;
    memcpy(&orig, e->buf + patch_offset, 4);
    // Clear imm19 field (bits 23:5) and replace.
    orig &= ~(0x7FFFF << 5);
    orig |= ((imm19 & 0x7FFFF) << 5);
    memcpy(e->buf + patch_offset, &orig, 4);
}

// ---------------------------------------------------------------
// Stack — STP/LDP pairs (for trampoline save/restore)
// ---------------------------------------------------------------

// STP Xt1, Xt2, [SP, #imm]! — pre-index store pair
static inline void emit_stp_pre(emit_t *e, int rt1, int rt2, int rn, int32_t byte_off) {
    int32_t imm7 = byte_off / 8;
    emit_inst(e, 0xA9800000 | ((imm7 & 0x7F) << 15) | (rt2 << 10) | (rn << 5) | rt1);
}

// LDP Xt1, Xt2, [SP], #imm — post-index load pair
static inline void emit_ldp_post(emit_t *e, int rt1, int rt2, int rn, int32_t byte_off) {
    int32_t imm7 = byte_off / 8;
    emit_inst(e, 0xA8C00000 | ((imm7 & 0x7F) << 15) | (rt2 << 10) | (rn << 5) | rt1);
}

// ---------------------------------------------------------------
// Floating-point — D-register (64-bit double) operations
// ---------------------------------------------------------------

// LDR Dt, [Xn, #imm12*8] — load double from immediate offset
static inline void emit_load_fp_d(emit_t *e, int dt, int guest_freg) {
    uint32_t byte_off = CTX_FP_OFF + guest_freg * 8;
    uint32_t scaled = byte_off / 8;
    emit_inst(e, 0xFD400000 | (scaled << 10) | (A64_X19 << 5) | dt);
}

// STR Dt, [Xn, #imm12*8] — store double to immediate offset
static inline void emit_store_fp_d(emit_t *e, int guest_freg, int dt) {
    uint32_t byte_off = CTX_FP_OFF + guest_freg * 8;
    uint32_t scaled = byte_off / 8;
    emit_inst(e, 0xFD000000 | (scaled << 10) | (A64_X19 << 5) | dt);
}

// LDR Dt, [X20, Xm] — load double from guest memory
static inline void emit_load_mem_f64(emit_t *e, int dt, int idx) {
    emit_inst(e, 0xFC606800 | (idx << 16) | (A64_X20 << 5) | dt);
}

// STR Dt, [X20, Xm] — store double to guest memory
static inline void emit_store_mem_f64(emit_t *e, int idx, int dt) {
    emit_inst(e, 0xFC206800 | (idx << 16) | (A64_X20 << 5) | dt);
}

// FP arithmetic (double precision)
static inline void emit_fadd_d(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x1E602800 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_fsub_d(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x1E603800 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_fmul_d(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x1E600800 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_fdiv_d(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x1E601800 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_fsqrt_d(emit_t *e, int rd, int rn) {
    emit_inst(e, 0x1E61C000 | (rn << 5) | rd);
}

// FMINNM Dd, Dn, Dm — IEEE 754 minNum (number-preferred for NaN)
// Matches RISC-V FMIN.D semantics.
static inline void emit_fmin_d(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x1E607800 | (rm << 16) | (rn << 5) | rd);
}

// FMAXNM Dd, Dn, Dm — IEEE 754 maxNum (number-preferred for NaN)
// Matches RISC-V FMAX.D semantics.
static inline void emit_fmax_d(emit_t *e, int rd, int rn, int rm) {
    emit_inst(e, 0x1E606800 | (rm << 16) | (rn << 5) | rd);
}

static inline void emit_fneg_d(emit_t *e, int rd, int rn) {
    emit_inst(e, 0x1E614000 | (rn << 5) | rd);
}

static inline void emit_fabs_d(emit_t *e, int rd, int rn) {
    emit_inst(e, 0x1E60C000 | (rn << 5) | rd);
}

// FMOV Dd, Ds — register-to-register double move
static inline void emit_fmov_d(emit_t *e, int rd, int rn) {
    if (rd == rn) return;
    emit_inst(e, 0x1E604000 | (rn << 5) | rd);
}

// FCMP Dn, Dm
static inline void emit_fcmp_d(emit_t *e, int rn, int rm) {
    emit_inst(e, 0x1E602000 | (rm << 16) | (rn << 5));
}

// FP ↔ integer conversions

// SCVTF Dd, Xn — signed int64 to double
static inline void emit_scvtf_d_x64(emit_t *e, int dd, int xn) {
    emit_inst(e, 0x9E620000 | (xn << 5) | dd);
}

// FCVTZS Xd, Dn — double to signed int64 (truncate toward zero)
static inline void emit_fcvtzs_x64_d(emit_t *e, int xd, int dn) {
    emit_inst(e, 0x9E780000 | (dn << 5) | xd);
}

// FMOV Xd, Dn — bitwise move double to int64
static inline void emit_fmov_x64_d(emit_t *e, int xd, int dn) {
    emit_inst(e, 0x9E660000 | (dn << 5) | xd);
}

// FMOV Dd, Xn — bitwise move int64 to double
static inline void emit_fmov_d_x64(emit_t *e, int dd, int xn) {
    emit_inst(e, 0x9E670000 | (xn << 5) | dd);
}

// ---------------------------------------------------------------
// Block exit helpers
// ---------------------------------------------------------------

// Store next_pc = immediate and return to trampoline.
static inline void emit_exit_with_pc(emit_t *e, uint64_t next_pc) {
    // Load immediate into X0 (scratch), store to ctx.next_pc, RET.
    emit_mov_r64_imm64(e, A64_X0, next_pc);
    emit_str_x64_imm(e, A64_X0, A64_X19, CTX_NEXT_PC_OFF);
    emit_ret(e);
}

// Store next_pc = host_reg and return to trampoline.
static inline void emit_exit_indirect(emit_t *e, int host_reg) {
    emit_str_x64_imm(e, host_reg, A64_X19, CTX_NEXT_PC_OFF);
    emit_ret(e);
}

// Compare ctx field against immediate (for inline call return check).
static inline void emit_cmp_ctx_imm32(emit_t *e, int ctx_offset, int32_t imm) {
    // Load ctx field into X0 (scratch), compare against immediate.
    emit_ldr_x64_imm(e, A64_X0, A64_X19, ctx_offset);
    emit_mov_r64_imm32(e, A64_X1, imm);
    emit_cmp_r64(e, A64_X0, A64_X1);
}

#endif // DBT_EMIT_A64_H
