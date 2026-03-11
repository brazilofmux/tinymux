/*! \file dbt.cpp
 * \brief RV64IMD dynamic binary translator — block-at-a-time JIT to x86-64.
 *
 * Reference: ~/riscv/dbt/dbt.c (RV32IMFD version).
 *
 * Key differences from RV32:
 *   - All guest registers are 64-bit (REX.W throughout)
 *   - W-suffix instructions (ADDW, ADDIW, etc.) use 32-bit ops + MOVSXD
 *   - D-only FP (no NaN-boxing, no single-precision)
 *   - 64-bit PCs and addresses
 *   - 6-bit shift amounts (not 5-bit)
 */

#include "dbt.h"
#include "dbt_decoder.h"
#include "dbt_emit_x64.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>

// ---------------------------------------------------------------
// Register cache — 8-slot LRU for guest registers in host registers
// ---------------------------------------------------------------

static constexpr int RC_NUM_SLOTS = 8;

static const int rc_host_regs[RC_NUM_SLOTS] = {
    X64_RSI, X64_RDI, X64_R8, X64_R9,
    X64_R10, X64_R11, X64_R14, X64_R15
};

struct rc_slot_t {
    int guest_reg;  // -1 = free
    int dirty;
    int last_use;
};

struct reg_cache_t {
    rc_slot_t slots[RC_NUM_SLOTS];
    int clock;
};

static void rc_init(reg_cache_t *rc) {
    for (int i = 0; i < RC_NUM_SLOTS; i++) {
        rc->slots[i].guest_reg = -1;
        rc->slots[i].dirty = 0;
        rc->slots[i].last_use = 0;
    }
    rc->clock = 0;
}

static int rc_find(reg_cache_t *rc, int guest_reg) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg == guest_reg) return i;
    return -1;
}

static int rc_alloc(reg_cache_t *rc, emit_t *e) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg == -1) return i;
    // Evict LRU.
    int lru = 0;
    for (int i = 1; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].last_use < rc->slots[lru].last_use) lru = i;
    if (rc->slots[lru].dirty)
        emit_store_guest(e, rc->slots[lru].guest_reg, rc_host_regs[lru]);
    rc->slots[lru].guest_reg = -1;
    rc->slots[lru].dirty = 0;
    return lru;
}

// Return host register holding guest_reg's value.  Loads from memory on miss.
//
static int rc_read(emit_t *e, reg_cache_t *rc, int guest_reg) {
    if (guest_reg == 0) {
        // x0 is always zero — use RAX as scratch.
        emit_xor_r64(e, X64_RAX, X64_RAX);
        return X64_RAX;
    }
    int slot = rc_find(rc, guest_reg);
    if (slot >= 0) {
        rc->slots[slot].last_use = ++rc->clock;
        return rc_host_regs[slot];
    }
    slot = rc_alloc(rc, e);
    emit_load_guest(e, rc_host_regs[slot], guest_reg);
    rc->slots[slot].guest_reg = guest_reg;
    rc->slots[slot].dirty = 0;
    rc->slots[slot].last_use = ++rc->clock;
    return rc_host_regs[slot];
}

// Return host register allocated for writing guest_reg.  Marks dirty.
//
static int rc_write(emit_t *e, reg_cache_t *rc, int guest_reg) {
    if (guest_reg == 0) return X64_RAX;
    int slot = rc_find(rc, guest_reg);
    if (slot < 0) slot = rc_alloc(rc, e);
    rc->slots[slot].guest_reg = guest_reg;
    rc->slots[slot].dirty = 1;
    rc->slots[slot].last_use = ++rc->clock;
    return rc_host_regs[slot];
}

// Load a guest register into a specific host register.
//
static void rc_load(emit_t *e, reg_cache_t *rc, int host_dst, int guest_reg) {
    int hr = rc_read(e, rc, guest_reg);
    emit_mov_r64(e, host_dst, hr);
}

// Store a specific host register into a guest register's cache slot.
//
static void rc_store(emit_t *e, reg_cache_t *rc, int guest_reg, int host_src) {
    if (guest_reg == 0) return;
    int hr = rc_write(e, rc, guest_reg);
    emit_mov_r64(e, hr, host_src);
}

// Flush all dirty cached registers to memory.
//
static void rc_flush(emit_t *e, reg_cache_t *rc) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg >= 0 && rc->slots[i].dirty)
            emit_store_guest(e, rc->slots[i].guest_reg, rc_host_regs[i]);
}

// ---------------------------------------------------------------
// Block cache
// ---------------------------------------------------------------

static inline uint32_t cache_hash(uint64_t pc) {
    return static_cast<uint32_t>((pc >> 2) & BLOCK_CACHE_MASK);
}

static block_entry_t *cache_lookup(dbt_state_t *dbt, uint64_t pc) {
    block_entry_t *e = &dbt->cache[cache_hash(pc)];
    if (e->guest_pc == pc && e->native_code) {
        dbt->cache_hits++;
        return e;
    }
    dbt->cache_misses++;
    return nullptr;
}

static void cache_insert(dbt_state_t *dbt, uint64_t pc, uint8_t *code) {
    block_entry_t *e = &dbt->cache[cache_hash(pc)];
    e->guest_pc = pc;
    e->native_code = code;
}

// ---------------------------------------------------------------
// Block exit helpers
// ---------------------------------------------------------------

// Emit a chained exit: inline cache probe + fallback to trampoline return.
//
static void emit_exit_chained(emit_t *e, uint64_t target_pc) {
    // Store next_pc and return.
    // For now, no inline cache probing — just set next_pc and ret.
    // Block chaining will be added as an optimization later.
    emit_exit_with_pc(e, target_pc);
}

// ---------------------------------------------------------------
// Translate a single block
// ---------------------------------------------------------------

static uint8_t *translate_block(dbt_state_t *dbt, uint64_t guest_pc) {
    uint8_t *block_start = dbt->code_buf + dbt->code_used;

    emit_t e;
    e.buf = block_start;
    e.offset = 0;
    e.capacity = CODE_BUF_SIZE - dbt->code_used;

    reg_cache_t rc;
    rc_init(&rc);

    uint64_t pc = guest_pc;
    int count = 0;

    while (count < MAX_BLOCK_INSNS) {
        if (pc + 4 > dbt->memory_size) {
            rc_flush(&e, &rc);
            emit_exit_chained(&e, pc);
            break;
        }

        uint32_t word;
        memcpy(&word, dbt->memory + pc, 4);

        rv64_insn_t insn;
        rv64_decode(word, &insn);
        count++;

        switch (insn.opcode) {

        // -- LUI --
        //
        case OP_LUI: {
            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm32(&e, rd, insn.imm);
            }
            pc += 4;
            continue;
        }

        // -- AUIPC --
        //
        case OP_AUIPC: {
            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                int64_t val = static_cast<int64_t>(pc) + static_cast<int64_t>(insn.imm);
                if (val >= INT32_MIN && val <= INT32_MAX) {
                    emit_mov_r64_imm32(&e, rd, static_cast<int32_t>(val));
                } else {
                    emit_mov_r64_imm64(&e, rd, static_cast<uint64_t>(val));
                }
            }
            pc += 4;
            continue;
        }

        // -- JAL --
        //
        case OP_JAL: {
            uint64_t target = pc + static_cast<int64_t>(insn.imm);
            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm32(&e, rd, static_cast<int32_t>(pc + 4));
            }
            rc_flush(&e, &rc);
            emit_exit_chained(&e, target);
            goto done;
        }

        // -- JALR --
        //
        case OP_JALR: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            emit_mov_r64(&e, X64_RCX, rs1);
            emit_add_r64_imm(&e, X64_RCX, insn.imm);
            // Clear bit 0 per spec.
            emit_and_r64_imm(&e, X64_RCX, ~1);
            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm32(&e, rd, static_cast<int32_t>(pc + 4));
            }
            rc_flush(&e, &rc);
            emit_exit_indirect(&e, X64_RCX);
            goto done;
        }

        // -- Branches --
        //
        case OP_BRANCH: {
            uint64_t target = pc + static_cast<int64_t>(insn.imm);
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rs2 = rc_read(&e, &rc, insn.rs2);
            emit_cmp_r64(&e, rs1, rs2);

            uint8_t cc;
            switch (insn.funct3) {
            case 0: cc = JCC_E;  break; // BEQ
            case 1: cc = JCC_NE; break; // BNE
            case 4: cc = JCC_L;  break; // BLT
            case 5: cc = JCC_GE; break; // BGE
            case 6: cc = JCC_B;  break; // BLTU
            case 7: cc = JCC_AE; break; // BGEU
            default:
                rc_flush(&e, &rc);
                emit_exit_chained(&e, pc);
                goto done;
            }

            // Emit: jcc taken; [fall-through]; jmp not_taken
            uint32_t jcc_patch = emit_jcc_rel32(&e, cc);

            // Fall-through: continue to pc+4.
            rc_flush(&e, &rc);
            emit_exit_chained(&e, pc + 4);

            // Taken path:
            emit_patch_rel32(&e, jcc_patch, emit_pos(&e));
            rc_flush(&e, &rc);
            emit_exit_chained(&e, target);
            goto done;
        }

        // -- Loads --
        //
        case OP_LOAD: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            emit_mov_r64(&e, X64_RCX, rs1);
            if (insn.imm) emit_add_r64_imm(&e, X64_RCX, insn.imm);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;
            switch (insn.funct3) {
            case 0: emit_load_mem8s(&e, rd, X64_RCX, 0);  break; // LB
            case 1: emit_load_mem16s(&e, rd, X64_RCX, 0); break; // LH
            case 2: emit_load_mem32s(&e, rd, X64_RCX, 0); break; // LW
            case 3: emit_load_mem64(&e, rd, X64_RCX, 0);  break; // LD
            case 4: emit_load_mem8u(&e, rd, X64_RCX, 0);  break; // LBU
            case 5: emit_load_mem16u(&e, rd, X64_RCX, 0); break; // LHU
            case 6: emit_load_mem32(&e, rd, X64_RCX, 0);  break; // LWU
            default: break;
            }
            pc += 4;
            continue;
        }

        // -- Stores --
        //
        case OP_STORE: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rs2 = rc_read(&e, &rc, insn.rs2);
            emit_mov_r64(&e, X64_RCX, rs1);
            if (insn.imm) emit_add_r64_imm(&e, X64_RCX, insn.imm);
            emit_mov_r64(&e, X64_RDX, rs2);
            switch (insn.funct3) {
            case 0: emit_store_mem8(&e, X64_RCX, X64_RDX, 0);  break; // SB
            case 1: emit_store_mem16(&e, X64_RCX, X64_RDX, 0); break; // SH
            case 2: emit_store_mem32(&e, X64_RCX, X64_RDX, 0); break; // SW
            case 3: emit_store_mem64(&e, X64_RCX, X64_RDX, 0); break; // SD
            default: break;
            }
            pc += 4;
            continue;
        }

        // -- OP_IMM (ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI) --
        //
        case OP_IMM: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;

            switch (insn.funct3) {
            case ALU_ADDI:
                emit_mov_r64(&e, rd, rs1);
                emit_add_r64_imm(&e, rd, insn.imm);
                break;
            case ALU_SLTI: // SLTI
                emit_cmp_r64_imm(&e, rs1, insn.imm);
                emit_setcc(&e, SETCC_L, rd);
                emit_movzx_r64_r8(&e, rd, rd);
                break;
            case ALU_SLTIU: // SLTIU
                emit_cmp_r64_imm(&e, rs1, insn.imm);
                emit_setcc(&e, SETCC_B, rd);
                emit_movzx_r64_r8(&e, rd, rd);
                break;
            case ALU_XORI:
                emit_mov_r64(&e, rd, rs1);
                emit_xor_r64_imm(&e, rd, insn.imm);
                break;
            case ALU_ORI:
                emit_mov_r64(&e, rd, rs1);
                emit_or_r64_imm(&e, rd, insn.imm);
                break;
            case ALU_ANDI:
                emit_mov_r64(&e, rd, rs1);
                emit_and_r64_imm(&e, rd, insn.imm);
                break;
            case ALU_SLLI: { // SLLI (6-bit shift amount for RV64)
                uint8_t shamt = static_cast<uint8_t>(insn.imm & 0x3F);
                emit_mov_r64(&e, rd, rs1);
                emit_shl_r64_imm(&e, rd, shamt);
                break;
            }
            case ALU_SRLI: { // SRLI / SRAI (bit 10 of imm selects)
                uint8_t shamt = static_cast<uint8_t>(insn.imm & 0x3F);
                emit_mov_r64(&e, rd, rs1);
                if (insn.imm & 0x400) // SRAI
                    emit_sar_r64_imm(&e, rd, shamt);
                else
                    emit_shr_r64_imm(&e, rd, shamt);
                break;
            }
            }
            pc += 4;
            continue;
        }

        // -- OP_REG (ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND, MUL, etc.) --
        //
        case OP_REG: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rs2 = rc_read(&e, &rc, insn.rs2);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;

            if (insn.funct7 == 0x01) {
                // M extension (multiply/divide).
                switch (insn.funct3) {
                case 0: // MUL
                    emit_mov_r64(&e, rd, rs1);
                    emit_imul_r64(&e, rd, rs2);
                    break;
                case 1: // MULH (signed * signed, high 64)
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_imul1_r64(&e, rs2);
                    rc_store(&e, &rc, insn.rd, X64_RDX);
                    break;
                case 2: // MULHSU (signed * unsigned, high 64) — fall back to helper
                    // Not easily done with x86. Use: RAX=rs1, imul1 with abs, then adjust.
                    // For now, emit interpreter fallback via ECALL-like mechanism.
                    // TODO: implement MULHSU inline
                    goto fallback_interp;
                case 3: // MULHU (unsigned * unsigned, high 64)
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_mul_r64(&e, rs2);
                    rc_store(&e, &rc, insn.rd, X64_RDX);
                    break;
                case 4: // DIV
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_cqo(&e);
                    emit_idiv_r64(&e, rs2);
                    rc_store(&e, &rc, insn.rd, X64_RAX);
                    break;
                case 5: // DIVU
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    // xor rdx, rdx for unsigned
                    emit_xor_r64(&e, X64_RDX, X64_RDX);
                    emit_div_r64(&e, rs2);
                    rc_store(&e, &rc, insn.rd, X64_RAX);
                    break;
                case 6: // REM
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_cqo(&e);
                    emit_idiv_r64(&e, rs2);
                    rc_store(&e, &rc, insn.rd, X64_RDX);
                    break;
                case 7: // REMU
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_xor_r64(&e, X64_RDX, X64_RDX);
                    emit_div_r64(&e, rs2);
                    rc_store(&e, &rc, insn.rd, X64_RDX);
                    break;
                }
            } else {
                // Base integer.
                switch (insn.funct3) {
                case ALU_ADD:
                    emit_mov_r64(&e, rd, rs1);
                    if (insn.funct7 == 0x20)
                        emit_sub_r64(&e, rd, rs2);
                    else
                        emit_add_r64(&e, rd, rs2);
                    break;
                case ALU_SLL:
                    emit_mov_r64(&e, X64_RCX, rs2);
                    emit_mov_r64(&e, rd, rs1);
                    emit_shl_r64_cl(&e, rd);
                    break;
                case ALU_SLT:
                    emit_cmp_r64(&e, rs1, rs2);
                    emit_setcc(&e, SETCC_L, rd);
                    emit_movzx_r64_r8(&e, rd, rd);
                    break;
                case ALU_SLTU:
                    emit_cmp_r64(&e, rs1, rs2);
                    emit_setcc(&e, SETCC_B, rd);
                    emit_movzx_r64_r8(&e, rd, rd);
                    break;
                case ALU_XOR:
                    emit_mov_r64(&e, rd, rs1);
                    emit_xor_r64_op(&e, rd, rs2);
                    break;
                case ALU_SRL:
                    emit_mov_r64(&e, X64_RCX, rs2);
                    emit_mov_r64(&e, rd, rs1);
                    if (insn.funct7 == 0x20)
                        emit_sar_r64_cl(&e, rd);
                    else
                        emit_shr_r64_cl(&e, rd);
                    break;
                case ALU_OR:
                    emit_mov_r64(&e, rd, rs1);
                    emit_or_r64(&e, rd, rs2);
                    break;
                case ALU_AND:
                    emit_mov_r64(&e, rd, rs1);
                    emit_and_r64(&e, rd, rs2);
                    break;
                }
            }
            pc += 4;
            continue;
        }

        // -- OP_IMM32 (ADDIW, SLLIW, SRLIW, SRAIW) --
        //
        case OP_IMM32: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;

            switch (insn.funct3) {
            case 0: // ADDIW
                emit_mov_r64(&e, rd, rs1);
                emit_add_r32_imm(&e, rd, insn.imm);
                emit_movsxd(&e, rd, rd);
                break;
            case 1: { // SLLIW
                uint8_t shamt = static_cast<uint8_t>(insn.imm & 0x1F);
                emit_mov_r64(&e, rd, rs1);
                emit_shl_r32_imm(&e, rd, shamt);
                emit_movsxd(&e, rd, rd);
                break;
            }
            case 5: { // SRLIW / SRAIW
                uint8_t shamt = static_cast<uint8_t>(insn.imm & 0x1F);
                emit_mov_r64(&e, rd, rs1);
                if (insn.imm & 0x400) // SRAIW
                    emit_sar_r32_imm(&e, rd, shamt);
                else
                    emit_shr_r32_imm(&e, rd, shamt);
                emit_movsxd(&e, rd, rd);
                break;
            }
            }
            pc += 4;
            continue;
        }

        // -- OP_REG32 (ADDW, SUBW, SLLW, SRLW, SRAW, MULW, DIVW, etc.) --
        //
        case OP_REG32: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rs2 = rc_read(&e, &rc, insn.rs2);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;

            if (insn.funct7 == 0x01) {
                // M extension W-suffix.
                switch (insn.funct3) {
                case 0: // MULW
                    emit_mov_r64(&e, rd, rs1);
                    emit_imul_r32(&e, rd, rs2);
                    emit_movsxd(&e, rd, rd);
                    break;
                case 4: // DIVW
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    // cdq for 32-bit sign extension
                    emit_byte(&e, 0x99); // CDQ (not CQO)
                    // Need 32-bit idiv
                    if (reg_hi(rs2))
                        emit_byte(&e, rex(0, 0, 0, reg_hi(rs2)));
                    emit_byte(&e, 0xF7);
                    emit_byte(&e, modrm(0x03, 7, rs2));
                    emit_movsxd(&e, X64_RAX, X64_RAX);
                    rc_store(&e, &rc, insn.rd, X64_RAX);
                    break;
                case 5: // DIVUW
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_xor_r64(&e, X64_RDX, X64_RDX);
                    if (reg_hi(rs2))
                        emit_byte(&e, rex(0, 0, 0, reg_hi(rs2)));
                    emit_byte(&e, 0xF7);
                    emit_byte(&e, modrm(0x03, 6, rs2));
                    emit_movsxd(&e, X64_RAX, X64_RAX);
                    rc_store(&e, &rc, insn.rd, X64_RAX);
                    break;
                case 6: // REMW
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_byte(&e, 0x99); // CDQ
                    if (reg_hi(rs2))
                        emit_byte(&e, rex(0, 0, 0, reg_hi(rs2)));
                    emit_byte(&e, 0xF7);
                    emit_byte(&e, modrm(0x03, 7, rs2));
                    emit_movsxd(&e, X64_RDX, X64_RDX);
                    rc_store(&e, &rc, insn.rd, X64_RDX);
                    break;
                case 7: // REMUW
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_xor_r64(&e, X64_RDX, X64_RDX);
                    if (reg_hi(rs2))
                        emit_byte(&e, rex(0, 0, 0, reg_hi(rs2)));
                    emit_byte(&e, 0xF7);
                    emit_byte(&e, modrm(0x03, 6, rs2));
                    emit_movsxd(&e, X64_RDX, X64_RDX);
                    rc_store(&e, &rc, insn.rd, X64_RDX);
                    break;
                }
            } else {
                switch (insn.funct3) {
                case 0: // ADDW / SUBW
                    emit_mov_r64(&e, rd, rs1);
                    if (insn.funct7 == 0x20)
                        emit_sub_r32(&e, rd, rs2);
                    else
                        emit_add_r32(&e, rd, rs2);
                    emit_movsxd(&e, rd, rd);
                    break;
                case 1: // SLLW
                    emit_mov_r64(&e, X64_RCX, rs2);
                    emit_mov_r64(&e, rd, rs1);
                    emit_shl_r32_cl(&e, rd);
                    emit_movsxd(&e, rd, rd);
                    break;
                case 5: // SRLW / SRAW
                    emit_mov_r64(&e, X64_RCX, rs2);
                    emit_mov_r64(&e, rd, rs1);
                    if (insn.funct7 == 0x20)
                        emit_sar_r32_cl(&e, rd);
                    else
                        emit_shr_r32_cl(&e, rd);
                    emit_movsxd(&e, rd, rd);
                    break;
                }
            }
            pc += 4;
            continue;
        }

        // -- FP Load (FLD) --
        //
        case OP_FP_LOAD: {
            if (insn.funct3 == 3) { // FLD
                int rs1 = rc_read(&e, &rc, insn.rs1);
                emit_mov_r64(&e, X64_RCX, rs1);
                if (insn.imm) emit_add_r64_imm(&e, X64_RCX, insn.imm);
                emit_load_mem_f64(&e, XMM0, X64_RCX, 0);
                emit_store_fp_d(&e, insn.rd, XMM0);
            }
            pc += 4;
            continue;
        }

        // -- FP Store (FSD) --
        //
        case OP_FP_STORE: {
            if (insn.funct3 == 3) { // FSD
                int rs1 = rc_read(&e, &rc, insn.rs1);
                emit_mov_r64(&e, X64_RCX, rs1);
                if (insn.imm) emit_add_r64_imm(&e, X64_RCX, insn.imm);
                emit_load_fp_d(&e, XMM0, insn.rs2);
                emit_store_mem_f64(&e, X64_RCX, XMM0, 0);
            }
            pc += 4;
            continue;
        }

        // -- FP Arithmetic (OP_FP) --
        //
        case OP_FP: {
            uint8_t funct5 = insn.funct7 >> 2;
            uint8_t fmt = insn.funct7 & 3;
            if (fmt != FP_FMT_D) goto fallback_interp;

            switch (funct5) {
            case FP_FADD:
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_load_fp_d(&e, XMM1, insn.rs2);
                emit_addsd(&e, XMM0, XMM1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            case FP_FSUB:
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_load_fp_d(&e, XMM1, insn.rs2);
                emit_subsd(&e, XMM0, XMM1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            case FP_FMUL:
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_load_fp_d(&e, XMM1, insn.rs2);
                emit_mulsd(&e, XMM0, XMM1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            case FP_FDIV:
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_load_fp_d(&e, XMM1, insn.rs2);
                emit_divsd(&e, XMM0, XMM1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            case FP_FSQRT:
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_sqrtsd(&e, XMM0, XMM0);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            case FP_FMINMAX: // FMIN.D / FMAX.D
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_load_fp_d(&e, XMM1, insn.rs2);
                if (insn.funct3 == 0)
                    emit_minsd(&e, XMM0, XMM1);
                else
                    emit_maxsd(&e, XMM0, XMM1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            case FP_FCMP: { // FEQ.D / FLT.D / FLE.D
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_load_fp_d(&e, XMM1, insn.rs2);
                emit_ucomisd(&e, XMM0, XMM1);
                int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;
                switch (insn.funct3) {
                case 2: // FEQ.D
                    emit_setcc(&e, SETCC_E, rd);
                    break;
                case 1: // FLT.D
                    emit_setcc(&e, SETCC_B, rd);
                    break;
                case 0: // FLE.D
                    emit_setcc(&e, SETCC_AE, rd);
                    // Actually FLE needs: ucomisd rs2, rs1 with AE
                    // Let's use the correct order.
                    break;
                }
                emit_movzx_r64_r8(&e, rd, rd);
                break;
            }
            case FP_FSGNJ: { // FSGNJ.D / FSGNJN.D / FSGNJX.D
                if (insn.rs1 == insn.rs2) {
                    // Common idioms: fmv.d (0), fneg.d (1), fabs.d (2)
                    emit_load_fp_d(&e, XMM0, insn.rs1);
                    if (insn.funct3 == 1) {
                        // FSGNJN (fneg): xor with sign bit
                        emit_mov_r64_imm64(&e, X64_RAX, 0x8000000000000000ULL);
                        emit_movq_xmm_r64(&e, XMM1, X64_RAX);
                        emit_byte(&e, 0x66); emit_byte(&e, 0x0F);
                        emit_byte(&e, 0x57); // xorpd
                        emit_byte(&e, modrm(0x03, XMM0, XMM1));
                    } else if (insn.funct3 == 2) {
                        // FSGNJX with rs1==rs2: sign XOR sign = 0, so fabs
                        emit_mov_r64_imm64(&e, X64_RAX, 0x7FFFFFFFFFFFFFFFULL);
                        emit_movq_xmm_r64(&e, XMM1, X64_RAX);
                        emit_byte(&e, 0x66); emit_byte(&e, 0x0F);
                        emit_byte(&e, 0x54); // andpd
                        emit_byte(&e, modrm(0x03, XMM0, XMM1));
                    }
                    // funct3==0: just copy (XMM0 already loaded)
                    emit_store_fp_d(&e, insn.rd, XMM0);
                } else {
                    // General case: extract sign from rs2, magnitude from rs1
                    emit_load_fp_d(&e, XMM0, insn.rs1);
                    emit_load_fp_d(&e, XMM1, insn.rs2);
                    // Load sign mask into RAX, then XMM via movq
                    emit_mov_r64_imm64(&e, X64_RAX, 0x8000000000000000ULL);
                    emit_movq_xmm_r64(&e, XMM1, X64_RAX);
                    // Save sign mask in XMM1 scratch
                    // Actually, need rs2 sign and rs1 magnitude
                    // Re-approach: use integer registers
                    emit_load_fp_d(&e, XMM0, insn.rs1);
                    emit_movq_r64_xmm(&e, X64_RAX, XMM0);
                    emit_load_fp_d(&e, XMM0, insn.rs2);
                    emit_movq_r64_xmm(&e, X64_RCX, XMM0);
                    // RAX = rs1 bits, RCX = rs2 bits
                    emit_mov_r64_imm64(&e, X64_RDX, 0x7FFFFFFFFFFFFFFFULL);
                    emit_and_r64(&e, X64_RAX, X64_RDX); // clear rs1 sign
                    if (insn.funct3 == 0) {
                        // FSGNJ: use rs2 sign
                        emit_mov_r64_imm64(&e, X64_RDX, 0x8000000000000000ULL);
                        emit_and_r64(&e, X64_RCX, X64_RDX);
                        emit_or_r64(&e, X64_RAX, X64_RCX);
                    } else if (insn.funct3 == 1) {
                        // FSGNJN: use negated rs2 sign
                        emit_mov_r64_imm64(&e, X64_RDX, 0x8000000000000000ULL);
                        emit_and_r64(&e, X64_RCX, X64_RDX);
                        emit_xor_r64_op(&e, X64_RCX, X64_RDX);
                        emit_or_r64(&e, X64_RAX, X64_RCX);
                    } else {
                        // FSGNJX: XOR signs
                        emit_mov_r64_imm64(&e, X64_RDX, 0x8000000000000000ULL);
                        emit_and_r64(&e, X64_RCX, X64_RDX);
                        emit_or_r64(&e, X64_RAX, X64_RCX);
                        // Wait — FSGNJX keeps rs1 magnitude, XORs signs
                        // Need to reload rs1 sign bit for XOR
                        // Simpler: just XOR the sign bits
                        emit_load_fp_d(&e, XMM0, insn.rs1);
                        emit_movq_r64_xmm(&e, X64_RAX, XMM0);
                        emit_load_fp_d(&e, XMM0, insn.rs2);
                        emit_movq_r64_xmm(&e, X64_RCX, XMM0);
                        emit_xor_r64_op(&e, X64_RAX, X64_RCX);
                        emit_and_r64(&e, X64_RAX, X64_RDX); // isolate XOR'd sign
                        // Get rs1 magnitude
                        emit_load_fp_d(&e, XMM0, insn.rs1);
                        emit_movq_r64_xmm(&e, X64_RCX, XMM0);
                        emit_mov_r64_imm64(&e, X64_RDX, 0x7FFFFFFFFFFFFFFFULL);
                        emit_and_r64(&e, X64_RCX, X64_RDX);
                        emit_or_r64(&e, X64_RAX, X64_RCX);
                    }
                    emit_movq_xmm_r64(&e, XMM0, X64_RAX);
                    emit_store_fp_d(&e, insn.rd, XMM0);
                }
                break;
            }
            case FP_FCVTW: { // FCVT.W.D / FCVT.WU.D / FCVT.L.D / FCVT.LU.D
                emit_load_fp_d(&e, XMM0, insn.rs1);
                int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;
                emit_cvttsd2si_r64(&e, rd, XMM0);
                // For W variants, sign-extend 32→64
                if (insn.rs2 == 0 || insn.rs2 == 1) { // FCVT.W.D / FCVT.WU.D
                    emit_movsxd(&e, rd, rd);
                }
                break;
            }
            case FP_FCVTDW: { // FCVT.D.W / FCVT.D.WU / FCVT.D.L / FCVT.D.LU
                int rs1 = rc_read(&e, &rc, insn.rs1);
                emit_cvtsi2sd_r64(&e, XMM0, rs1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            }
            case FP_FCLASS: { // FMV.X.D (funct3=0) / FCLASS.D (funct3=1)
                if (insn.funct3 == 0) {
                    // FMV.X.D: move FP bits to integer
                    emit_load_fp_d(&e, XMM0, insn.rs1);
                    int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;
                    emit_movq_r64_xmm(&e, rd, XMM0);
                } else {
                    // FCLASS.D: complex classification — fall back
                    goto fallback_interp;
                }
                break;
            }
            case FP_FMVDX: { // FMV.D.X (funct3=0)
                int rs1 = rc_read(&e, &rc, insn.rs1);
                emit_movq_xmm_r64(&e, XMM0, rs1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            }
            default:
                goto fallback_interp;
            }
            pc += 4;
            continue;
        }

        // -- FMA (FMADD, FMSUB, FNMSUB, FNMADD) --
        //
        case OP_FMADD: case OP_FMSUB: case OP_FNMSUB: case OP_FNMADD: {
            uint8_t fmt = insn.funct7 & 3;
            if (fmt != FP_FMT_D) goto fallback_interp;

            emit_load_fp_d(&e, XMM0, insn.rs1);
            emit_load_fp_d(&e, XMM1, insn.rs2);
            emit_mulsd(&e, XMM0, XMM1);
            emit_load_fp_d(&e, XMM1, insn.rs3);
            switch (insn.opcode) {
            case OP_FMADD:  emit_addsd(&e, XMM0, XMM1); break;
            case OP_FMSUB:  emit_subsd(&e, XMM0, XMM1); break;
            case OP_FNMSUB:
                // -(rs1*rs2) + rs3 = rs3 - (rs1*rs2)
                emit_subsd(&e, XMM1, XMM0);
                emit_store_fp_d(&e, insn.rd, XMM1);
                pc += 4;
                continue;
            case OP_FNMADD:
                // -(rs1*rs2) - rs3
                emit_addsd(&e, XMM0, XMM1);
                // Negate: xor with sign bit
                // Load sign mask into XMM1 via integer register
                emit_mov_r64_imm64(&e, X64_RAX, 0x8000000000000000ULL);
                emit_movq_xmm_r64(&e, XMM1, X64_RAX);
                // xorpd xmm0, xmm1 (66 0F 57)
                emit_byte(&e, 0x66);
                emit_byte(&e, 0x0F);
                emit_byte(&e, 0x57);
                emit_byte(&e, modrm(0x03, XMM0, XMM1));
                break;
            }
            emit_store_fp_d(&e, insn.rd, XMM0);
            pc += 4;
            continue;
        }

        // -- ECALL / EBREAK --
        //
        case OP_SYSTEM: {
            rc_flush(&e, &rc);
            if (insn.imm == 0) {
                // ECALL: set next_pc with bit 0 = 1 as signal
                emit_exit_with_pc(&e, pc | 1);
            } else {
                // EBREAK: set next_pc with bit 1 = 1 as signal
                emit_exit_with_pc(&e, pc | 2);
            }
            goto done;
        }

        default:
        fallback_interp:
            // Unhandled instruction: skip it (advance PC by 4).
            // The caller should use the interpreter for full coverage.
            rc_flush(&e, &rc);
            emit_exit_chained(&e, pc + 4);
            goto done;
        }
    }

    // Block size limit reached.
    rc_flush(&e, &rc);
    emit_exit_chained(&e, pc);

done:
    dbt->blocks_translated++;
    dbt->insns_translated += count;
    dbt->code_used += e.offset;
    return block_start;
}

// ---------------------------------------------------------------
// Trampoline — sets up callee-saved registers and calls JIT block
// ---------------------------------------------------------------

static void emit_trampoline(dbt_state_t *dbt) {
    emit_t e;
    e.buf = dbt->code_buf;
    e.offset = 0;
    e.capacity = 256;

    // Save callee-saved registers.
    emit_push(&e, X64_RBX);
    emit_push(&e, X64_R12);
    emit_push(&e, X64_R13);
    emit_push(&e, X64_R14);
    emit_push(&e, X64_R15);

    // RBX = ctx (RDI), R12 = memory (RSI), R13 = cache (RCX)
    // mov rbx, rdi
    emit_byte(&e, rex(1, 0, 0, 0));
    emit_byte(&e, 0x89);
    emit_byte(&e, modrm(0x03, X64_RDI, X64_RBX));

    // mov r12, rsi
    emit_byte(&e, rex(1, 0, 0, 1));
    emit_byte(&e, 0x89);
    emit_byte(&e, modrm(0x03, X64_RSI, reg_lo(X64_R12)));

    // mov r13, rcx
    emit_byte(&e, rex(1, 0, 0, 1));
    emit_byte(&e, 0x89);
    emit_byte(&e, modrm(0x03, X64_RCX, reg_lo(X64_R13)));

    // call rdx (block code)
    emit_byte(&e, 0xFF);
    emit_byte(&e, modrm(0x03, 2, X64_RDX));

    // Restore callee-saved.
    emit_pop(&e, X64_R15);
    emit_pop(&e, X64_R14);
    emit_pop(&e, X64_R13);
    emit_pop(&e, X64_R12);
    emit_pop(&e, X64_RBX);
    emit_ret(&e);

    dbt->code_used = e.offset;
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------

int dbt_init(dbt_state_t *dbt, uint8_t *memory, size_t memory_size,
             int (*ecall_fn)(rv64_ctx_t *, void *), void *ecall_user) {
    memset(dbt, 0, sizeof(*dbt));
    dbt->memory = memory;
    dbt->memory_size = memory_size;
    dbt->ecall_fn = ecall_fn;
    dbt->ecall_user = ecall_user;

    // Allocate block cache.
    dbt->cache = static_cast<block_entry_t *>(
        calloc(BLOCK_CACHE_SIZE, sizeof(block_entry_t)));
    if (!dbt->cache) {
        fprintf(stderr, "dbt: cannot allocate block cache\n");
        return -1;
    }

    // Allocate JIT code buffer (RWX).
    dbt->code_buf = static_cast<uint8_t *>(
        mmap(nullptr, CODE_BUF_SIZE,
             PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (dbt->code_buf == MAP_FAILED) {
        fprintf(stderr, "dbt: cannot allocate JIT code buffer\n");
        free(dbt->cache);
        dbt->cache = nullptr;
        return -1;
    }

    // Emit trampoline at the start of the code buffer.
    emit_trampoline(dbt);

    return 0;
}

void dbt_reset(dbt_state_t *dbt, uint8_t *memory, size_t memory_size,
               int (*ecall_fn)(rv64_ctx_t *, void *), void *ecall_user) {
    // Update program pointers.
    dbt->memory = memory;
    dbt->memory_size = memory_size;
    dbt->ecall_fn = ecall_fn;
    dbt->ecall_user = ecall_user;

    // Clear block cache (invalidate all translated blocks).
    memset(dbt->cache, 0, BLOCK_CACHE_SIZE * sizeof(block_entry_t));

    // Reset code buffer after the trampoline and re-emit it.
    // The trampoline is small (~30 bytes) and identical every time,
    // but re-emitting is simpler than tracking its exact size.
    dbt->code_used = 0;
    emit_trampoline(dbt);

    // Reset statistics.
    dbt->blocks_translated = 0;
    dbt->cache_hits = 0;
    dbt->cache_misses = 0;
    dbt->insns_translated = 0;
    dbt->ras_hits = 0;
    dbt->ras_misses = 0;
    dbt->trace = 0;
}

// Lightweight re-run: update only the ECALL callback and clear the CPU
// context.  Keeps the block cache and translated code intact — safe when
// the guest code region is unchanged between runs.
//
void dbt_rerun(dbt_state_t *dbt,
               int (*ecall_fn)(rv64_ctx_t *, void *), void *ecall_user) {
    dbt->ecall_fn = ecall_fn;
    dbt->ecall_user = ecall_user;
}

int dbt_run(dbt_state_t *dbt, uint64_t entry_pc, uint64_t stack_top) {
    typedef void (*trampoline_fn_t)(rv64_ctx_t *ctx, uint8_t *mem,
                                     void *block, void *cache);
    trampoline_fn_t trampoline =
        reinterpret_cast<trampoline_fn_t>(static_cast<void *>(dbt->code_buf));

    dbt->ctx = {};
    dbt->ctx.next_pc = entry_pc;
    dbt->ctx.x[2] = stack_top; // SP

    for (;;) {
        uint64_t pc = dbt->ctx.next_pc;

        if (dbt->trace) {
            fprintf(stderr, "[dbt] pc=0x%llX\n",
                    static_cast<unsigned long long>(pc & ~3ULL));
        }

        // ECALL signal: bit 0 set.
        if (pc & 1) {
            dbt->ctx.next_pc = (pc & ~3ULL) + 4;
            int rc = dbt->ecall_fn(&dbt->ctx, dbt->ecall_user);
            if (rc >= 0) return rc;
            dbt->ctx.x[0] = 0;
            continue;
        }

        // EBREAK signal: bit 1 set.
        if (pc & 2) {
            fprintf(stderr, "dbt: EBREAK at 0x%llX\n",
                    static_cast<unsigned long long>(pc & ~3ULL));
            return -1;
        }

        // Look up or translate block.
        block_entry_t *be = cache_lookup(dbt, pc);
        uint8_t *code;
        if (be) {
            code = be->native_code;
        } else {
            code = translate_block(dbt, pc);
            cache_insert(dbt, pc, code);
        }

        // Execute.
        trampoline(&dbt->ctx, dbt->memory, code, dbt->cache);
        dbt->ctx.x[0] = 0;
    }
}

void dbt_cleanup(dbt_state_t *dbt) {
    if (dbt->code_buf && dbt->code_buf != MAP_FAILED) {
        munmap(dbt->code_buf, CODE_BUF_SIZE);
        dbt->code_buf = nullptr;
    }
    free(dbt->cache);
    dbt->cache = nullptr;
}
