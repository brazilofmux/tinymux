/*! \file dbt_emit_x64.h
 * \brief Lightweight x86-64 code emitter for RV64IMD JIT compilation.
 *
 * Convention:
 *   RBX = pointer to rv64_ctx_t (guest register file + state)
 *   R12 = pointer to guest memory base
 *   R13 = pointer to block cache base
 *   RAX, RCX, RDX = scratch (never cached)
 *   RSI, RDI, R8-R11, R14, R15 = register cache (8 slots)
 *
 * All guest register operations are 64-bit (REX.W prefix) since
 * RV64 has 64-bit integer registers.  W-suffix instructions (ADDW,
 * ADDIW, etc.) use 32-bit operations followed by MOVSXD sign-extension.
 */

#ifndef DBT_EMIT_X64_H
#define DBT_EMIT_X64_H

#include "dbt.h"
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------
// Code buffer
// ---------------------------------------------------------------

struct emit_t {
    uint8_t *buf;
    uint32_t offset;
    uint32_t capacity;
};

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

static inline void emit_patch_rel32(emit_t *e, uint32_t patch_offset,
                                     uint32_t target_offset) {
    int32_t disp = static_cast<int32_t>(target_offset - (patch_offset + 4));
    memcpy(e->buf + patch_offset, &disp, 4);
}

// ---------------------------------------------------------------
// x86-64 register encoding
// ---------------------------------------------------------------

static constexpr int X64_RAX = 0;
static constexpr int X64_RCX = 1;
static constexpr int X64_RDX = 2;
static constexpr int X64_RBX = 3;
static constexpr int X64_RSP = 4;
static constexpr int X64_RBP = 5;
static constexpr int X64_RSI = 6;
static constexpr int X64_RDI = 7;
static constexpr int X64_R8  = 8;
static constexpr int X64_R9  = 9;
static constexpr int X64_R10 = 10;
static constexpr int X64_R11 = 11;
static constexpr int X64_R12 = 12;
static constexpr int X64_R13 = 13;
static constexpr int X64_R14 = 14;
static constexpr int X64_R15 = 15;

// REX prefix helpers.
//
static inline uint8_t rex(int w, int r, int x, int b) {
    return static_cast<uint8_t>(0x40 | (w << 3) | (r << 2) | (x << 1) | b);
}

static inline int reg_hi(int r) { return (r >> 3) & 1; }
static inline int reg_lo(int r) { return r & 7; }

// ModR/M byte.
//
static inline uint8_t modrm(int mod, int reg, int rm) {
    return static_cast<uint8_t>((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

// ---------------------------------------------------------------
// Guest register access (64-bit, via RBX)
// ---------------------------------------------------------------

// mov r64, [rbx + guest_reg * 8]
//
static inline void emit_load_guest(emit_t *e, int host_reg, int guest_reg) {
    int offset = guest_reg * 8;
    emit_byte(e, rex(1, reg_hi(host_reg), 0, 0));
    emit_byte(e, 0x8B);
    if (offset == 0) {
        emit_byte(e, modrm(0x00, host_reg, X64_RBX));
    } else if (offset <= 127) {
        emit_byte(e, modrm(0x01, host_reg, X64_RBX));
        emit_byte(e, static_cast<uint8_t>(offset));
    } else {
        emit_byte(e, modrm(0x02, host_reg, X64_RBX));
        emit_u32(e, static_cast<uint32_t>(offset));
    }
}

// mov [rbx + guest_reg * 8], r64
//
static inline void emit_store_guest(emit_t *e, int guest_reg, int host_reg) {
    if (guest_reg == 0) return;
    int offset = guest_reg * 8;
    emit_byte(e, rex(1, reg_hi(host_reg), 0, 0));
    emit_byte(e, 0x89);
    if (offset <= 127) {
        emit_byte(e, modrm(0x01, host_reg, X64_RBX));
        emit_byte(e, static_cast<uint8_t>(offset));
    } else {
        emit_byte(e, modrm(0x02, host_reg, X64_RBX));
        emit_u32(e, static_cast<uint32_t>(offset));
    }
}

// ---------------------------------------------------------------
// 64-bit integer register-register operations
// ---------------------------------------------------------------

// xor r64, r64 (zeroing)
static inline void emit_xor_r64(emit_t *e, int dst, int src) {
    emit_byte(e, rex(1, reg_hi(dst), 0, reg_hi(src)));
    emit_byte(e, 0x31);
    emit_byte(e, modrm(0x03, dst, src));
}

// mov r64, r64
static inline void emit_mov_r64(emit_t *e, int dst, int src) {
    if (dst == src) return;
    emit_byte(e, rex(1, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x89);
    emit_byte(e, modrm(0x03, src, dst));
}

// mov r64, imm64
static inline void emit_mov_r64_imm64(emit_t *e, int reg, uint64_t imm) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xB8 + reg_lo(reg));
    emit_u64(e, imm);
}

// mov r64, imm32 (sign-extended)
static inline void emit_mov_r64_imm32(emit_t *e, int reg, int32_t imm) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xC7);
    emit_byte(e, modrm(0x03, 0, reg));
    emit_u32(e, static_cast<uint32_t>(imm));
}

// add r64, r64
static inline void emit_add_r64(emit_t *e, int dst, int src) {
    emit_byte(e, rex(1, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x01);
    emit_byte(e, modrm(0x03, src, dst));
}

// sub r64, r64
static inline void emit_sub_r64(emit_t *e, int dst, int src) {
    emit_byte(e, rex(1, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x29);
    emit_byte(e, modrm(0x03, src, dst));
}

// and r64, r64
static inline void emit_and_r64(emit_t *e, int dst, int src) {
    emit_byte(e, rex(1, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x21);
    emit_byte(e, modrm(0x03, src, dst));
}

// or r64, r64
static inline void emit_or_r64(emit_t *e, int dst, int src) {
    emit_byte(e, rex(1, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x09);
    emit_byte(e, modrm(0x03, src, dst));
}

// xor r64, r64 (non-zeroing)
static inline void emit_xor_r64_op(emit_t *e, int dst, int src) {
    emit_byte(e, rex(1, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x31);
    emit_byte(e, modrm(0x03, src, dst));
}

// add r64, imm32 (sign-extended)
static inline void emit_add_r64_imm(emit_t *e, int reg, int32_t imm) {
    if (imm == 0) return;
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_byte(e, modrm(0x03, 0, reg));
        emit_byte(e, static_cast<uint8_t>(static_cast<int8_t>(imm)));
    } else if (reg == X64_RAX) {
        emit_byte(e, 0x05);
        emit_u32(e, static_cast<uint32_t>(imm));
    } else {
        emit_byte(e, 0x81);
        emit_byte(e, modrm(0x03, 0, reg));
        emit_u32(e, static_cast<uint32_t>(imm));
    }
}

// sub r64, imm32
static inline void emit_sub_r64_imm(emit_t *e, int reg, int32_t imm) {
    if (imm == 0) return;
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_byte(e, modrm(0x03, 5, reg));
        emit_byte(e, static_cast<uint8_t>(static_cast<int8_t>(imm)));
    } else {
        emit_byte(e, 0x81);
        emit_byte(e, modrm(0x03, 5, reg));
        emit_u32(e, static_cast<uint32_t>(imm));
    }
}

// and r64, imm32
static inline void emit_and_r64_imm(emit_t *e, int reg, int32_t imm) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_byte(e, modrm(0x03, 4, reg));
        emit_byte(e, static_cast<uint8_t>(static_cast<int8_t>(imm)));
    } else {
        emit_byte(e, 0x81);
        emit_byte(e, modrm(0x03, 4, reg));
        emit_u32(e, static_cast<uint32_t>(imm));
    }
}

// or r64, imm32
static inline void emit_or_r64_imm(emit_t *e, int reg, int32_t imm) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_byte(e, modrm(0x03, 1, reg));
        emit_byte(e, static_cast<uint8_t>(static_cast<int8_t>(imm)));
    } else {
        emit_byte(e, 0x81);
        emit_byte(e, modrm(0x03, 1, reg));
        emit_u32(e, static_cast<uint32_t>(imm));
    }
}

// xor r64, imm32
static inline void emit_xor_r64_imm(emit_t *e, int reg, int32_t imm) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_byte(e, modrm(0x03, 6, reg));
        emit_byte(e, static_cast<uint8_t>(static_cast<int8_t>(imm)));
    } else {
        emit_byte(e, 0x81);
        emit_byte(e, modrm(0x03, 6, reg));
        emit_u32(e, static_cast<uint32_t>(imm));
    }
}

// shl r64, imm8
static inline void emit_shl_r64_imm(emit_t *e, int reg, uint8_t amt) {
    if (amt == 0) return;
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    if (amt == 1) {
        emit_byte(e, 0xD1);
        emit_byte(e, modrm(0x03, 4, reg));
    } else {
        emit_byte(e, 0xC1);
        emit_byte(e, modrm(0x03, 4, reg));
        emit_byte(e, amt);
    }
}

// shr r64, imm8
static inline void emit_shr_r64_imm(emit_t *e, int reg, uint8_t amt) {
    if (amt == 0) return;
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    if (amt == 1) {
        emit_byte(e, 0xD1);
        emit_byte(e, modrm(0x03, 5, reg));
    } else {
        emit_byte(e, 0xC1);
        emit_byte(e, modrm(0x03, 5, reg));
        emit_byte(e, amt);
    }
}

// sar r64, imm8
static inline void emit_sar_r64_imm(emit_t *e, int reg, uint8_t amt) {
    if (amt == 0) return;
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    if (amt == 1) {
        emit_byte(e, 0xD1);
        emit_byte(e, modrm(0x03, 7, reg));
    } else {
        emit_byte(e, 0xC1);
        emit_byte(e, modrm(0x03, 7, reg));
        emit_byte(e, amt);
    }
}

// shl r64, cl
static inline void emit_shl_r64_cl(emit_t *e, int reg) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xD3);
    emit_byte(e, modrm(0x03, 4, reg));
}

// shr r64, cl
static inline void emit_shr_r64_cl(emit_t *e, int reg) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xD3);
    emit_byte(e, modrm(0x03, 5, reg));
}

// sar r64, cl
static inline void emit_sar_r64_cl(emit_t *e, int reg) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xD3);
    emit_byte(e, modrm(0x03, 7, reg));
}

// imul r64, r64 (signed multiply, low 64 bits)
static inline void emit_imul_r64(emit_t *e, int dst, int src) {
    emit_byte(e, rex(1, reg_hi(dst), 0, reg_hi(src)));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xAF);
    emit_byte(e, modrm(0x03, dst, src));
}

// mul r64 (unsigned: RDX:RAX = RAX * r64)
static inline void emit_mul_r64(emit_t *e, int reg) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xF7);
    emit_byte(e, modrm(0x03, 4, reg));
}

// imul r64 (signed: RDX:RAX = RAX * r64, single-operand form)
static inline void emit_imul1_r64(emit_t *e, int reg) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xF7);
    emit_byte(e, modrm(0x03, 5, reg));
}

// cqo (sign-extend RAX into RDX:RAX for 64-bit division)
static inline void emit_cqo(emit_t *e) {
    emit_byte(e, rex(1, 0, 0, 0));
    emit_byte(e, 0x99);
}

// idiv r64 (signed divide RDX:RAX by r64)
static inline void emit_idiv_r64(emit_t *e, int reg) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xF7);
    emit_byte(e, modrm(0x03, 7, reg));
}

// div r64 (unsigned divide)
static inline void emit_div_r64(emit_t *e, int reg) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xF7);
    emit_byte(e, modrm(0x03, 6, reg));
}

// neg r64
static inline void emit_neg_r64(emit_t *e, int reg) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xF7);
    emit_byte(e, modrm(0x03, 3, reg));
}

// cmp r64, r64
static inline void emit_cmp_r64(emit_t *e, int r1, int r2) {
    emit_byte(e, rex(1, reg_hi(r2), 0, reg_hi(r1)));
    emit_byte(e, 0x39);
    emit_byte(e, modrm(0x03, r2, r1));
}

// cmp r64, imm32 (sign-extended)
static inline void emit_cmp_r64_imm(emit_t *e, int reg, int32_t imm) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_byte(e, modrm(0x03, 7, reg));
        emit_byte(e, static_cast<uint8_t>(static_cast<int8_t>(imm)));
    } else if (reg == X64_RAX) {
        emit_byte(e, 0x3D);
        emit_u32(e, static_cast<uint32_t>(imm));
    } else {
        emit_byte(e, 0x81);
        emit_byte(e, modrm(0x03, 7, reg));
        emit_u32(e, static_cast<uint32_t>(imm));
    }
}

// test r64, r64
static inline void emit_test_r64(emit_t *e, int r1, int r2) {
    emit_byte(e, rex(1, reg_hi(r2), 0, reg_hi(r1)));
    emit_byte(e, 0x85);
    emit_byte(e, modrm(0x03, r2, r1));
}

// ---------------------------------------------------------------
// 32-bit operations for W-suffix instructions (ADDW, SUBW, etc.)
// Result must be sign-extended to 64-bit with MOVSXD.
// ---------------------------------------------------------------

// add r32, r32 (no REX.W)
static inline void emit_add_r32(emit_t *e, int dst, int src) {
    if (reg_hi(dst) || reg_hi(src))
        emit_byte(e, rex(0, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x01);
    emit_byte(e, modrm(0x03, src, dst));
}

// sub r32, r32
static inline void emit_sub_r32(emit_t *e, int dst, int src) {
    if (reg_hi(dst) || reg_hi(src))
        emit_byte(e, rex(0, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x29);
    emit_byte(e, modrm(0x03, src, dst));
}

// add r32, imm32
static inline void emit_add_r32_imm(emit_t *e, int reg, int32_t imm) {
    if (imm == 0) return;
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_byte(e, modrm(0x03, 0, reg));
        emit_byte(e, static_cast<uint8_t>(static_cast<int8_t>(imm)));
    } else {
        emit_byte(e, 0x81);
        emit_byte(e, modrm(0x03, 0, reg));
        emit_u32(e, static_cast<uint32_t>(imm));
    }
}

// shl r32, imm8
static inline void emit_shl_r32_imm(emit_t *e, int reg, uint8_t amt) {
    if (amt == 0) return;
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    if (amt == 1) {
        emit_byte(e, 0xD1);
        emit_byte(e, modrm(0x03, 4, reg));
    } else {
        emit_byte(e, 0xC1);
        emit_byte(e, modrm(0x03, 4, reg));
        emit_byte(e, amt);
    }
}

// shr r32, imm8
static inline void emit_shr_r32_imm(emit_t *e, int reg, uint8_t amt) {
    if (amt == 0) return;
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    if (amt == 1) {
        emit_byte(e, 0xD1);
        emit_byte(e, modrm(0x03, 5, reg));
    } else {
        emit_byte(e, 0xC1);
        emit_byte(e, modrm(0x03, 5, reg));
        emit_byte(e, amt);
    }
}

// sar r32, imm8
static inline void emit_sar_r32_imm(emit_t *e, int reg, uint8_t amt) {
    if (amt == 0) return;
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    if (amt == 1) {
        emit_byte(e, 0xD1);
        emit_byte(e, modrm(0x03, 7, reg));
    } else {
        emit_byte(e, 0xC1);
        emit_byte(e, modrm(0x03, 7, reg));
        emit_byte(e, amt);
    }
}

// shl r32, cl
static inline void emit_shl_r32_cl(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xD3);
    emit_byte(e, modrm(0x03, 4, reg));
}

// shr r32, cl
static inline void emit_shr_r32_cl(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xD3);
    emit_byte(e, modrm(0x03, 5, reg));
}

// sar r32, cl
static inline void emit_sar_r32_cl(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xD3);
    emit_byte(e, modrm(0x03, 7, reg));
}

// imul r32, r32 (low 32 bits)
static inline void emit_imul_r32(emit_t *e, int dst, int src) {
    if (reg_hi(dst) || reg_hi(src))
        emit_byte(e, rex(0, reg_hi(dst), 0, reg_hi(src)));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xAF);
    emit_byte(e, modrm(0x03, dst, src));
}

// movsxd r64, r32 (sign-extend 32→64)
// Used after every W-suffix operation to get correct RV64 semantics.
//
static inline void emit_movsxd(emit_t *e, int dst, int src) {
    emit_byte(e, rex(1, reg_hi(dst), 0, reg_hi(src)));
    emit_byte(e, 0x63);
    emit_byte(e, modrm(0x03, dst, src));
}

// ---------------------------------------------------------------
// setCC / movzx
// ---------------------------------------------------------------

static constexpr uint8_t SETCC_E  = 0x94;
static constexpr uint8_t SETCC_NE = 0x95;
static constexpr uint8_t SETCC_L  = 0x9C;
static constexpr uint8_t SETCC_GE = 0x9D;
static constexpr uint8_t SETCC_B  = 0x92;
static constexpr uint8_t SETCC_AE = 0x93;

static inline void emit_setcc(emit_t *e, uint8_t cc, int reg) {
    // Always emit REX so regs 4-7 map to SPL/BPL/SIL/DIL.
    emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0x0F);
    emit_byte(e, cc);
    emit_byte(e, modrm(0x03, 0, reg));
}

// CMOVcc r64, r64 — conditional move (REX.W 0F 4x /r)
// cc values: CMOV_E=0x44, CMOV_NE=0x45, CMOV_L=0x4C, CMOV_GE=0x4D,
//            CMOV_B=0x42, CMOV_AE=0x43
//
static constexpr uint8_t CMOV_E  = 0x44;
static constexpr uint8_t CMOV_NE = 0x45;
static constexpr uint8_t CMOV_L  = 0x4C;
static constexpr uint8_t CMOV_GE = 0x4D;
static constexpr uint8_t CMOV_B  = 0x42;
static constexpr uint8_t CMOV_AE = 0x43;

static inline void emit_cmovcc(emit_t *e, uint8_t cc, int dst, int src) {
    emit_byte(e, rex(1, reg_hi(dst), 0, reg_hi(src)));
    emit_byte(e, 0x0F);
    emit_byte(e, cc);
    emit_byte(e, modrm(0x03, dst, src));
}

// movzx r64, r8 (zero-extend byte to 64-bit)
static inline void emit_movzx_r64_r8(emit_t *e, int dst, int src) {
    emit_byte(e, rex(1, reg_hi(dst), 0, reg_hi(src)));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xB6);
    emit_byte(e, modrm(0x03, dst, src));
}

// ---------------------------------------------------------------
// Memory access via [R12 + index + disp] (guest memory)
// ---------------------------------------------------------------

static inline void emit_sib_disp(emit_t *e, int mod_reg, int index_reg,
                                   int32_t disp) {
    int mod = (disp == 0) ? 0x00 : (disp >= -128 && disp <= 127) ? 0x01 : 0x02;
    emit_byte(e, modrm(mod, mod_reg, 0x04));
    emit_byte(e, static_cast<uint8_t>((reg_lo(index_reg) << 3) | reg_lo(X64_R12)));
    if (mod == 0x01) emit_byte(e, static_cast<uint8_t>(static_cast<int8_t>(disp)));
    else if (mod == 0x02) emit_u32(e, static_cast<uint32_t>(disp));
}

// mov r64, [R12 + idx + disp] (64-bit load)
static inline void emit_load_mem64(emit_t *e, int dst, int idx, int32_t disp) {
    emit_byte(e, rex(1, reg_hi(dst), reg_hi(idx), 1));
    emit_byte(e, 0x8B);
    emit_sib_disp(e, dst, idx, disp);
}

// mov r32, [R12 + idx + disp] (32-bit load, zero-extended)
static inline void emit_load_mem32(emit_t *e, int dst, int idx, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), reg_hi(idx), 1));
    emit_byte(e, 0x8B);
    emit_sib_disp(e, dst, idx, disp);
}

// movsx r64, dword [R12 + idx + disp] (32-bit load, sign-extended to 64)
static inline void emit_load_mem32s(emit_t *e, int dst, int idx, int32_t disp) {
    emit_byte(e, rex(1, reg_hi(dst), reg_hi(idx), 1));
    emit_byte(e, 0x63);
    emit_sib_disp(e, dst, idx, disp);
}

// movsx r64, word [R12 + idx + disp]
static inline void emit_load_mem16s(emit_t *e, int dst, int idx, int32_t disp) {
    emit_byte(e, rex(1, reg_hi(dst), reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xBF);
    emit_sib_disp(e, dst, idx, disp);
}

// movzx r32, word [R12 + idx + disp] (zero-extended to 64 by 32-bit dest)
static inline void emit_load_mem16u(emit_t *e, int dst, int idx, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xB7);
    emit_sib_disp(e, dst, idx, disp);
}

// movsx r64, byte [R12 + idx + disp]
static inline void emit_load_mem8s(emit_t *e, int dst, int idx, int32_t disp) {
    emit_byte(e, rex(1, reg_hi(dst), reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xBE);
    emit_sib_disp(e, dst, idx, disp);
}

// movzx r32, byte [R12 + idx + disp]
static inline void emit_load_mem8u(emit_t *e, int dst, int idx, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xB6);
    emit_sib_disp(e, dst, idx, disp);
}

// mov [R12 + idx + disp], r64 (64-bit store)
static inline void emit_store_mem64(emit_t *e, int idx, int src, int32_t disp) {
    emit_byte(e, rex(1, reg_hi(src), reg_hi(idx), 1));
    emit_byte(e, 0x89);
    emit_sib_disp(e, src, idx, disp);
}

// mov [R12 + idx + disp], r32 (32-bit store)
static inline void emit_store_mem32(emit_t *e, int idx, int src, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(src), reg_hi(idx), 1));
    emit_byte(e, 0x89);
    emit_sib_disp(e, src, idx, disp);
}

// mov word [R12 + idx + disp], r16
static inline void emit_store_mem16(emit_t *e, int idx, int src, int32_t disp) {
    emit_byte(e, 0x66);
    emit_byte(e, rex(0, reg_hi(src), reg_hi(idx), 1));
    emit_byte(e, 0x89);
    emit_sib_disp(e, src, idx, disp);
}

// mov byte [R12 + idx + disp], r8
static inline void emit_store_mem8(emit_t *e, int idx, int src, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(src), reg_hi(idx), 1));
    emit_byte(e, 0x88);
    emit_sib_disp(e, src, idx, disp);
}

// ---------------------------------------------------------------
// Control flow
// ---------------------------------------------------------------

static constexpr uint8_t JCC_E  = 0x04;
static constexpr uint8_t JCC_NE = 0x05;
static constexpr uint8_t JCC_L  = 0x0C;
static constexpr uint8_t JCC_GE = 0x0D;
static constexpr uint8_t JCC_B  = 0x02;
static constexpr uint8_t JCC_AE = 0x03;

// jmp rel32 (placeholder — patch later)
static inline uint32_t emit_jmp_rel32(emit_t *e) {
    emit_byte(e, 0xE9);
    uint32_t patch = emit_pos(e);
    emit_u32(e, 0);
    return patch;
}

// jcc rel32 (placeholder — patch later)
static inline uint32_t emit_jcc_rel32(emit_t *e, uint8_t cc) {
    emit_byte(e, 0x0F);
    emit_byte(e, 0x80 + cc);
    uint32_t patch = emit_pos(e);
    emit_u32(e, 0);
    return patch;
}

static inline void emit_ret(emit_t *e) {
    emit_byte(e, 0xC3);
}

static inline void emit_push(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0x50 + reg_lo(reg));
}

static inline void emit_pop(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0x58 + reg_lo(reg));
}

// mov [rsp + disp], r64  — for stack-passed arguments to host calls.
// RSP as ModR/M base requires a SIB byte (base=RSP, no index).
//
static inline void emit_store_rsp(emit_t *e, int reg, int disp) {
    emit_byte(e, rex(1, reg_hi(reg), 0, 0));
    emit_byte(e, 0x89);
    if (disp == 0) {
        emit_byte(e, modrm(0x00, reg, 0x04));  // SIB follows
        emit_byte(e, 0x24);                     // SIB: base=RSP, no index
    } else if (disp >= -128 && disp <= 127) {
        emit_byte(e, modrm(0x01, reg, 0x04));
        emit_byte(e, 0x24);
        emit_byte(e, static_cast<uint8_t>(static_cast<int8_t>(disp)));
    } else {
        emit_byte(e, modrm(0x02, reg, 0x04));
        emit_byte(e, 0x24);
        emit_u32(e, static_cast<uint32_t>(disp));
    }
}

// call rax
static inline void emit_call_rax(emit_t *e) {
    emit_byte(e, 0xFF);
    emit_byte(e, 0xD0);
}

// call rel32 (placeholder — patch later, same as jmp rel32 but opcode E8)
static inline uint32_t emit_call_rel32(emit_t *e) {
    emit_byte(e, 0xE8);
    uint32_t patch = emit_pos(e);
    emit_u32(e, 0);
    return patch;
}

// cmp qword [rbx + disp32], sign-extended imm32
// Used for checking ctx fields against known constants.
static inline void emit_cmp_ctx_imm32(emit_t *e, int ctx_offset, int32_t imm) {
    emit_byte(e, rex(1, 0, 0, 0));      // REX.W
    emit_byte(e, 0x81);                  // CMP r/m64, imm32
    emit_byte(e, modrm(0x02, 7, X64_RBX)); // /7 = CMP, [rbx + disp32]
    emit_u32(e, static_cast<uint32_t>(ctx_offset));
    emit_u32(e, static_cast<uint32_t>(imm));
}

// ---------------------------------------------------------------
// Block exit helpers
// ---------------------------------------------------------------

// Store next_pc and return to trampoline.
//
static inline void emit_exit_with_pc(emit_t *e, uint64_t next_pc) {
    // mov qword [rbx + CTX_NEXT_PC_OFF], imm32 (sign-extended)
    // Works for addresses that fit in int32_t.  For larger PCs, use
    // mov rax, imm64 / mov [rbx+off], rax.
    //
    if (static_cast<int64_t>(next_pc) >= INT32_MIN &&
        static_cast<int64_t>(next_pc) <= INT32_MAX) {
        emit_byte(e, rex(1, 0, 0, 0));
        emit_byte(e, 0xC7);
        emit_byte(e, modrm(0x02, 0, X64_RBX));
        emit_u32(e, CTX_NEXT_PC_OFF);
        emit_u32(e, static_cast<uint32_t>(next_pc));
    } else {
        emit_mov_r64_imm64(e, X64_RAX, next_pc);
        // mov [rbx + CTX_NEXT_PC_OFF], rax
        emit_byte(e, rex(1, 0, 0, 0));
        emit_byte(e, 0x89);
        emit_byte(e, modrm(0x02, X64_RAX, X64_RBX));
        emit_u32(e, CTX_NEXT_PC_OFF);
    }
    emit_ret(e);
}

// Store r64 as next_pc and return (for indirect jumps).
//
static inline void emit_exit_indirect(emit_t *e, int host_reg) {
    // mov [rbx + CTX_NEXT_PC_OFF], host_reg
    emit_byte(e, rex(1, reg_hi(host_reg), 0, 0));
    emit_byte(e, 0x89);
    emit_byte(e, modrm(0x02, host_reg, X64_RBX));
    emit_u32(e, CTX_NEXT_PC_OFF);
    emit_ret(e);
}

// ---------------------------------------------------------------
// SSE/FP emitter functions for RV64D
// ---------------------------------------------------------------

static constexpr int XMM0 = 0;
static constexpr int XMM1 = 1;

// movsd xmm, [rbx + CTX_FP_OFF + freg*8]
static inline void emit_load_fp_d(emit_t *e, int xmm, int freg) {
    int off = CTX_FP_OFF + freg * 8;
    emit_byte(e, 0xF2);
    emit_byte(e, 0x0F);
    emit_byte(e, 0x10);
    if (off <= 127) {
        emit_byte(e, modrm(0x01, xmm, X64_RBX));
        emit_byte(e, static_cast<uint8_t>(off));
    } else {
        emit_byte(e, modrm(0x02, xmm, X64_RBX));
        emit_u32(e, static_cast<uint32_t>(off));
    }
}

// movsd [rbx + CTX_FP_OFF + freg*8], xmm
static inline void emit_store_fp_d(emit_t *e, int freg, int xmm) {
    int off = CTX_FP_OFF + freg * 8;
    emit_byte(e, 0xF2);
    emit_byte(e, 0x0F);
    emit_byte(e, 0x11);
    if (off <= 127) {
        emit_byte(e, modrm(0x01, xmm, X64_RBX));
        emit_byte(e, static_cast<uint8_t>(off));
    } else {
        emit_byte(e, modrm(0x02, xmm, X64_RBX));
        emit_u32(e, static_cast<uint32_t>(off));
    }
}

// FP memory access via [R12 + idx + disp]

// movsd xmm, [R12 + idx + disp]
static inline void emit_load_mem_f64(emit_t *e, int xmm, int idx, int32_t disp) {
    emit_byte(e, 0xF2);
    emit_byte(e, rex(0, 0, reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0x10);
    emit_sib_disp(e, xmm, idx, disp);
}

// movsd [R12 + idx + disp], xmm
static inline void emit_store_mem_f64(emit_t *e, int idx, int xmm, int32_t disp) {
    emit_byte(e, 0xF2);
    emit_byte(e, rex(0, 0, reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0x11);
    emit_sib_disp(e, xmm, idx, disp);
}

// SSE scalar double operations
static inline void emit_sse_sd(emit_t *e, uint8_t op, int dst, int src) {
    emit_byte(e, 0xF2);
    emit_byte(e, 0x0F);
    emit_byte(e, op);
    emit_byte(e, modrm(0x03, dst, src));
}

static inline void emit_addsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x58, d, s); }
static inline void emit_subsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x5C, d, s); }
static inline void emit_mulsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x59, d, s); }
static inline void emit_divsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x5E, d, s); }
static inline void emit_sqrtsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x51, d, s); }
static inline void emit_minsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x5D, d, s); }
static inline void emit_maxsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x5F, d, s); }

// movsd xmm, xmm — register-to-register move (no-op if same)
static inline void emit_movsd_xmm(emit_t *e, int dst, int src) {
    if (dst != src) emit_sse_sd(e, 0x10, dst, src);
}

// ucomisd xmm, xmm (66 0F 2E)
static inline void emit_ucomisd(emit_t *e, int xmm1, int xmm2) {
    emit_byte(e, 0x66);
    emit_byte(e, 0x0F);
    emit_byte(e, 0x2E);
    emit_byte(e, modrm(0x03, xmm1, xmm2));
}

// cvtsi2sd xmm, r64 (F2 REX.W 0F 2A) — 64-bit int to double
static inline void emit_cvtsi2sd_r64(emit_t *e, int xmm, int r64) {
    emit_byte(e, 0xF2);
    emit_byte(e, rex(1, 0, 0, reg_hi(r64)));
    emit_byte(e, 0x0F);
    emit_byte(e, 0x2A);
    emit_byte(e, modrm(0x03, xmm, r64));
}

// cvttsd2si r64, xmm (F2 REX.W 0F 2C) — double to 64-bit int (truncate)
static inline void emit_cvttsd2si_r64(emit_t *e, int r64, int xmm) {
    emit_byte(e, 0xF2);
    emit_byte(e, rex(1, reg_hi(r64), 0, 0));
    emit_byte(e, 0x0F);
    emit_byte(e, 0x2C);
    emit_byte(e, modrm(0x03, r64, xmm));
}

// movq r64, xmm (66 REX.W 0F 7E) — FMV.X.D
static inline void emit_movq_r64_xmm(emit_t *e, int r64, int xmm) {
    emit_byte(e, 0x66);
    emit_byte(e, rex(1, 0, 0, reg_hi(r64)));
    emit_byte(e, 0x0F);
    emit_byte(e, 0x7E);
    emit_byte(e, modrm(0x03, xmm, r64));
}

// movq xmm, r64 (66 REX.W 0F 6E) — FMV.D.X
static inline void emit_movq_xmm_r64(emit_t *e, int xmm, int r64) {
    emit_byte(e, 0x66);
    emit_byte(e, rex(1, 0, 0, reg_hi(r64)));
    emit_byte(e, 0x0F);
    emit_byte(e, 0x6E);
    emit_byte(e, modrm(0x03, xmm, r64));
}

#endif // DBT_EMIT_X64_H
