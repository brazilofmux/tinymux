/*! \file dbt_interp.cpp
 * \brief RV64IMD reference interpreter.
 *
 * Straightforward fetch-decode-execute loop.  Used for correctness
 * testing — every test runs on both interpreter and (future) DBT,
 * results must match.
 *
 * Reference: ~/riscv/dbt/interp.c (RV32IMFD version).
 * Key differences from reference:
 *   - 64-bit registers (uint64_t x[], uint64_t pc)
 *   - RV64 W-suffix instructions (OP_IMM32, OP_REG32)
 *   - 6-bit shift amounts for 64-bit shifts
 *   - LD/SD/LWU load/store
 *   - D-only (no single-precision F): no NaN-boxing
 *   - RV64-specific converts: FCVT.L.D, FCVT.LU.D, FCVT.D.L, FCVT.D.LU
 *   - RV64-specific move: FMV.X.D, FMV.D.X
 *   - ECALL dispatches to caller-provided callback (not Linux syscalls)
 */

#include "dbt_interp.h"
#include "dbt_decoder.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <climits>
#include <cfloat>

// ---------------------------------------------------------------
// FP helpers (D-only, no NaN-boxing)
// ---------------------------------------------------------------

static inline double fp_unbox_d(uint64_t v) {
    double d;
    memcpy(&d, &v, 8);
    return d;
}

static inline uint64_t fp_box_d(double d) {
    uint64_t bits;
    memcpy(&bits, &d, 8);
    return bits;
}

// ---------------------------------------------------------------
// Memory access — little-endian, bounds-checked
// ---------------------------------------------------------------

static inline bool mem_check(rv64_memory_t *mem, uint64_t addr, size_t len) {
    return addr + len <= mem->size;
}

static inline uint8_t mem_read8(rv64_memory_t *mem, uint64_t addr) {
    if (!mem_check(mem, addr, 1)) {
        fprintf(stderr, "rv64: read8 out of bounds at 0x%llX\n",
                (unsigned long long)addr);
        return 0;
    }
    return mem->data[addr];
}

static inline uint16_t mem_read16(rv64_memory_t *mem, uint64_t addr) {
    if (!mem_check(mem, addr, 2)) {
        fprintf(stderr, "rv64: read16 out of bounds at 0x%llX\n",
                (unsigned long long)addr);
        return 0;
    }
    uint16_t val;
    memcpy(&val, mem->data + addr, 2);
    return val;
}

static inline uint32_t mem_read32(rv64_memory_t *mem, uint64_t addr) {
    if (!mem_check(mem, addr, 4)) {
        fprintf(stderr, "rv64: read32 out of bounds at 0x%llX\n",
                (unsigned long long)addr);
        return 0;
    }
    uint32_t val;
    memcpy(&val, mem->data + addr, 4);
    return val;
}

static inline uint64_t mem_read64(rv64_memory_t *mem, uint64_t addr) {
    if (!mem_check(mem, addr, 8)) {
        fprintf(stderr, "rv64: read64 out of bounds at 0x%llX\n",
                (unsigned long long)addr);
        return 0;
    }
    uint64_t val;
    memcpy(&val, mem->data + addr, 8);
    return val;
}

static inline void mem_write8(rv64_memory_t *mem, uint64_t addr, uint8_t val) {
    if (!mem_check(mem, addr, 1)) {
        fprintf(stderr, "rv64: write8 out of bounds at 0x%llX\n",
                (unsigned long long)addr);
        return;
    }
    mem->data[addr] = val;
}

static inline void mem_write16(rv64_memory_t *mem, uint64_t addr, uint16_t val) {
    if (!mem_check(mem, addr, 2)) {
        fprintf(stderr, "rv64: write16 out of bounds at 0x%llX\n",
                (unsigned long long)addr);
        return;
    }
    memcpy(mem->data + addr, &val, 2);
}

static inline void mem_write32(rv64_memory_t *mem, uint64_t addr, uint32_t val) {
    if (!mem_check(mem, addr, 4)) {
        fprintf(stderr, "rv64: write32 out of bounds at 0x%llX\n",
                (unsigned long long)addr);
        return;
    }
    memcpy(mem->data + addr, &val, 4);
}

static inline void mem_write64(rv64_memory_t *mem, uint64_t addr, uint64_t val) {
    if (!mem_check(mem, addr, 8)) {
        fprintf(stderr, "rv64: write64 out of bounds at 0x%llX\n",
                (unsigned long long)addr);
        return;
    }
    memcpy(mem->data + addr, &val, 8);
}

// ---------------------------------------------------------------
// Sign-extension helpers
// ---------------------------------------------------------------

// Sign-extend a 32-bit value to 64 bits.
//
static inline uint64_t sext32(uint32_t v) {
    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v)));
}

// ---------------------------------------------------------------
// 128-bit multiply helpers for MULH/MULHSU/MULHU
// ---------------------------------------------------------------

#ifdef __SIZEOF_INT128__

static inline uint64_t mulh_ss(uint64_t a, uint64_t b) {
    __int128 result = static_cast<__int128>(static_cast<int64_t>(a))
                    * static_cast<__int128>(static_cast<int64_t>(b));
    return static_cast<uint64_t>(static_cast<__uint128_t>(result) >> 64);
}

static inline uint64_t mulh_su(uint64_t a, uint64_t b) {
    __int128 result = static_cast<__int128>(static_cast<int64_t>(a))
                    * static_cast<__int128>(static_cast<__uint128_t>(b));
    return static_cast<uint64_t>(static_cast<__uint128_t>(result) >> 64);
}

static inline uint64_t mulh_uu(uint64_t a, uint64_t b) {
    __uint128_t result = static_cast<__uint128_t>(a)
                       * static_cast<__uint128_t>(b);
    return static_cast<uint64_t>(result >> 64);
}

#else

// Fallback: split into 32-bit halves.
//
static inline uint64_t mulh_uu(uint64_t a, uint64_t b) {
    uint64_t a_lo = a & 0xFFFFFFFF;
    uint64_t a_hi = a >> 32;
    uint64_t b_lo = b & 0xFFFFFFFF;
    uint64_t b_hi = b >> 32;

    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;

    uint64_t carry = ((p0 >> 32) + (p1 & 0xFFFFFFFF) + (p2 & 0xFFFFFFFF)) >> 32;
    return p3 + (p1 >> 32) + (p2 >> 32) + carry;
}

static inline uint64_t mulh_ss(uint64_t a, uint64_t b) {
    int negate = 0;
    if (static_cast<int64_t>(a) < 0) { a = -a; negate ^= 1; }
    if (static_cast<int64_t>(b) < 0) { b = -b; negate ^= 1; }
    uint64_t hi = mulh_uu(a, b);
    uint64_t lo = a * b;
    if (negate) {
        // Negate 128-bit result: ~hi:~lo + 1
        lo = ~lo + 1;
        hi = ~hi + (lo == 0 ? 1 : 0);
    }
    return hi;
}

static inline uint64_t mulh_su(uint64_t a, uint64_t b) {
    int negate = 0;
    if (static_cast<int64_t>(a) < 0) { a = -a; negate = 1; }
    uint64_t hi = mulh_uu(a, b);
    uint64_t lo = a * b;
    if (negate) {
        lo = ~lo + 1;
        hi = ~hi + (lo == 0 ? 1 : 0);
    }
    return hi;
}

#endif // __SIZEOF_INT128__

// ---------------------------------------------------------------
// Interpreter main loop
// ---------------------------------------------------------------

int rv64_interp_run(rv64_state_t *state, rv64_memory_t *mem,
                    rv64_ecall_fn ecall_handler, void *ecall_user_data) {
    rv64_insn_t insn;

    for (;;) {
        // Fetch
        //
        if (!mem_check(mem, state->pc, 4)) {
            fprintf(stderr, "rv64: fetch out of bounds at PC=0x%llX\n",
                    (unsigned long long)state->pc);
            return -1;
        }
        uint32_t word = mem_read32(mem, state->pc);

        // Decode
        //
        rv64_decode(word, &insn);

        uint64_t next_pc = state->pc + 4;
        state->insn_count++;

        // Sign-extend immediate to 64 bits for use in 64-bit operations.
        //
        int64_t imm64 = static_cast<int64_t>(insn.imm);

        // Execute
        //
        switch (insn.opcode) {

        case OP_LUI:
            if (insn.rd) {
                state->x[insn.rd] = static_cast<uint64_t>(imm64);
            }
            break;

        case OP_AUIPC:
            if (insn.rd) {
                state->x[insn.rd] = state->pc + static_cast<uint64_t>(imm64);
            }
            break;

        case OP_JAL:
            if (insn.rd) {
                state->x[insn.rd] = next_pc;
            }
            next_pc = state->pc + static_cast<uint64_t>(imm64);
            break;

        case OP_JALR: {
            uint64_t target = (state->x[insn.rs1] + static_cast<uint64_t>(imm64)) & ~UINT64_C(1);
            if (insn.rd) {
                state->x[insn.rd] = next_pc;
            }
            next_pc = target;
            break;
        }

        case OP_BRANCH: {
            uint64_t a = state->x[insn.rs1];
            uint64_t b = state->x[insn.rs2];
            bool taken = false;
            switch (insn.funct3) {
            case BR_BEQ:  taken = (a == b); break;
            case BR_BNE:  taken = (a != b); break;
            case BR_BLT:  taken = (static_cast<int64_t>(a) < static_cast<int64_t>(b)); break;
            case BR_BGE:  taken = (static_cast<int64_t>(a) >= static_cast<int64_t>(b)); break;
            case BR_BLTU: taken = (a < b); break;
            case BR_BGEU: taken = (a >= b); break;
            default:
                fprintf(stderr, "rv64: illegal branch funct3=%d at PC=0x%llX\n",
                        insn.funct3, (unsigned long long)state->pc);
                return -1;
            }
            if (taken) {
                next_pc = state->pc + static_cast<uint64_t>(imm64);
            }
            break;
        }

        case OP_LOAD: {
            uint64_t addr = state->x[insn.rs1] + static_cast<uint64_t>(imm64);
            uint64_t val = 0;
            switch (insn.funct3) {
            case LD_LB:  val = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(mem_read8(mem, addr)))); break;
            case LD_LH:  val = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(mem_read16(mem, addr)))); break;
            case LD_LW:  val = sext32(mem_read32(mem, addr)); break;
            case LD_LD:  val = mem_read64(mem, addr); break;
            case LD_LBU: val = mem_read8(mem, addr); break;
            case LD_LHU: val = mem_read16(mem, addr); break;
            case LD_LWU: val = mem_read32(mem, addr); break;
            default:
                fprintf(stderr, "rv64: illegal load funct3=%d at PC=0x%llX\n",
                        insn.funct3, (unsigned long long)state->pc);
                return -1;
            }
            if (insn.rd) {
                state->x[insn.rd] = val;
            }
            break;
        }

        case OP_STORE: {
            uint64_t addr = state->x[insn.rs1] + static_cast<uint64_t>(imm64);
            switch (insn.funct3) {
            case ST_SB: mem_write8(mem, addr, static_cast<uint8_t>(state->x[insn.rs2])); break;
            case ST_SH: mem_write16(mem, addr, static_cast<uint16_t>(state->x[insn.rs2])); break;
            case ST_SW: mem_write32(mem, addr, static_cast<uint32_t>(state->x[insn.rs2])); break;
            case ST_SD: mem_write64(mem, addr, state->x[insn.rs2]); break;
            default:
                fprintf(stderr, "rv64: illegal store funct3=%d at PC=0x%llX\n",
                        insn.funct3, (unsigned long long)state->pc);
                return -1;
            }
            break;
        }

        // OP_IMM — 64-bit ALU immediate operations.
        // Shift amounts are 6 bits (imm[5:0]) for 64-bit shifts.
        //
        case OP_IMM: {
            uint64_t src = state->x[insn.rs1];
            uint64_t result = 0;
            switch (insn.funct3) {
            case ALU_ADDI:  result = src + static_cast<uint64_t>(imm64); break;
            case ALU_SLTI:  result = (static_cast<int64_t>(src) < imm64) ? 1 : 0; break;
            case ALU_SLTIU: result = (src < static_cast<uint64_t>(imm64)) ? 1 : 0; break;
            case ALU_XORI:  result = src ^ static_cast<uint64_t>(imm64); break;
            case ALU_ORI:   result = src | static_cast<uint64_t>(imm64); break;
            case ALU_ANDI:  result = src & static_cast<uint64_t>(imm64); break;
            case ALU_SLLI:
                result = src << (insn.imm & 0x3F);
                break;
            case ALU_SRLI:
                if (insn.funct7 & 0x20) {
                    // SRAI — arithmetic shift right
                    result = static_cast<uint64_t>(static_cast<int64_t>(src) >> (insn.imm & 0x3F));
                } else {
                    // SRLI — logical shift right
                    result = src >> (insn.imm & 0x3F);
                }
                break;
            }
            if (insn.rd) {
                state->x[insn.rd] = result;
            }
            break;
        }

        // OP_REG — 64-bit ALU register-register operations.
        //
        case OP_REG: {
            uint64_t a = state->x[insn.rs1];
            uint64_t b = state->x[insn.rs2];
            uint64_t result = 0;

            if (insn.funct7 == 0x01) {
                // M extension (64-bit)
                //
                switch (insn.funct3) {
                case 0: // MUL
                    result = a * b;
                    break;
                case 1: // MULH (signed x signed, high 64)
                    result = mulh_ss(a, b);
                    break;
                case 2: // MULHSU (signed x unsigned, high 64)
                    result = mulh_su(a, b);
                    break;
                case 3: // MULHU (unsigned x unsigned, high 64)
                    result = mulh_uu(a, b);
                    break;
                case 4: // DIV
                    if (b == 0) {
                        result = UINT64_MAX;
                    } else if (static_cast<int64_t>(a) == INT64_MIN && static_cast<int64_t>(b) == -1) {
                        result = static_cast<uint64_t>(INT64_MIN);
                    } else {
                        result = static_cast<uint64_t>(static_cast<int64_t>(a) / static_cast<int64_t>(b));
                    }
                    break;
                case 5: // DIVU
                    result = (b == 0) ? UINT64_MAX : a / b;
                    break;
                case 6: // REM
                    if (b == 0) {
                        result = a;
                    } else if (static_cast<int64_t>(a) == INT64_MIN && static_cast<int64_t>(b) == -1) {
                        result = 0;
                    } else {
                        result = static_cast<uint64_t>(static_cast<int64_t>(a) % static_cast<int64_t>(b));
                    }
                    break;
                case 7: // REMU
                    result = (b == 0) ? a : a % b;
                    break;
                }
            } else {
                // Base I extension (64-bit)
                //
                switch (insn.funct3) {
                case ALU_ADD:
                    result = (insn.funct7 & 0x20) ? a - b : a + b;
                    break;
                case ALU_SLL:
                    result = a << (b & 0x3F);
                    break;
                case ALU_SLT:
                    result = (static_cast<int64_t>(a) < static_cast<int64_t>(b)) ? 1 : 0;
                    break;
                case ALU_SLTU:
                    result = (a < b) ? 1 : 0;
                    break;
                case ALU_XOR:
                    result = a ^ b;
                    break;
                case ALU_SRL:
                    if (insn.funct7 & 0x20) {
                        result = static_cast<uint64_t>(static_cast<int64_t>(a) >> (b & 0x3F));
                    } else {
                        result = a >> (b & 0x3F);
                    }
                    break;
                case ALU_OR:
                    result = a | b;
                    break;
                case ALU_AND:
                    result = a & b;
                    break;
                }
            }
            if (insn.rd) {
                state->x[insn.rd] = result;
            }
            break;
        }

        // OP_IMM32 — RV64 W-suffix immediate operations.
        // Operate on lower 32 bits, sign-extend result to 64.
        //
        case OP_IMM32: {
            uint32_t src = static_cast<uint32_t>(state->x[insn.rs1]);
            uint32_t result = 0;
            switch (insn.funct3) {
            case 0: // ADDIW
                result = src + static_cast<uint32_t>(insn.imm);
                break;
            case 1: // SLLIW
                result = src << (insn.imm & 0x1F);
                break;
            case 5: // SRLIW / SRAIW
                if (insn.funct7 & 0x20) {
                    result = static_cast<uint32_t>(static_cast<int32_t>(src) >> (insn.imm & 0x1F));
                } else {
                    result = src >> (insn.imm & 0x1F);
                }
                break;
            default:
                fprintf(stderr, "rv64: illegal OP_IMM32 funct3=%d at PC=0x%llX\n",
                        insn.funct3, (unsigned long long)state->pc);
                return -1;
            }
            if (insn.rd) {
                state->x[insn.rd] = sext32(result);
            }
            break;
        }

        // OP_REG32 — RV64 W-suffix register-register operations.
        // Operate on lower 32 bits, sign-extend result to 64.
        //
        case OP_REG32: {
            uint32_t a = static_cast<uint32_t>(state->x[insn.rs1]);
            uint32_t b = static_cast<uint32_t>(state->x[insn.rs2]);
            uint32_t result = 0;

            if (insn.funct7 == 0x01) {
                // M extension W-suffix
                //
                switch (insn.funct3) {
                case 0: // MULW
                    result = a * b;
                    break;
                case 4: // DIVW
                    if (b == 0) {
                        result = UINT32_MAX;
                    } else if (static_cast<int32_t>(a) == INT32_MIN && static_cast<int32_t>(b) == -1) {
                        result = static_cast<uint32_t>(INT32_MIN);
                    } else {
                        result = static_cast<uint32_t>(static_cast<int32_t>(a) / static_cast<int32_t>(b));
                    }
                    break;
                case 5: // DIVUW
                    result = (b == 0) ? UINT32_MAX : a / b;
                    break;
                case 6: // REMW
                    if (b == 0) {
                        result = a;
                    } else if (static_cast<int32_t>(a) == INT32_MIN && static_cast<int32_t>(b) == -1) {
                        result = 0;
                    } else {
                        result = static_cast<uint32_t>(static_cast<int32_t>(a) % static_cast<int32_t>(b));
                    }
                    break;
                case 7: // REMUW
                    result = (b == 0) ? a : a % b;
                    break;
                default:
                    fprintf(stderr, "rv64: illegal OP_REG32 M funct3=%d at PC=0x%llX\n",
                            insn.funct3, (unsigned long long)state->pc);
                    return -1;
                }
            } else {
                // Base I extension W-suffix
                //
                switch (insn.funct3) {
                case ALU_ADD: // ADDW / SUBW
                    result = (insn.funct7 & 0x20) ? a - b : a + b;
                    break;
                case ALU_SLL: // SLLW
                    result = a << (b & 0x1F);
                    break;
                case ALU_SRL: // SRLW / SRAW
                    if (insn.funct7 & 0x20) {
                        result = static_cast<uint32_t>(static_cast<int32_t>(a) >> (b & 0x1F));
                    } else {
                        result = a >> (b & 0x1F);
                    }
                    break;
                default:
                    fprintf(stderr, "rv64: illegal OP_REG32 funct3=%d at PC=0x%llX\n",
                            insn.funct3, (unsigned long long)state->pc);
                    return -1;
                }
            }
            if (insn.rd) {
                state->x[insn.rd] = sext32(result);
            }
            break;
        }

        case OP_FENCE:
            // No-op in single-threaded mode.
            break;

        case OP_SYSTEM:
            if (insn.imm == 0 && insn.funct3 == 0) {
                // ECALL
                //
                state->pc = next_pc;
                int rc = ecall_handler(state, ecall_user_data);
                if (rc >= 0) {
                    return rc;
                }
                state->x[0] = 0;
                continue;
            } else if (insn.imm == 1 && insn.funct3 == 0) {
                // EBREAK
                //
                fprintf(stderr, "rv64: EBREAK at PC=0x%llX (%llu instructions)\n",
                        (unsigned long long)state->pc,
                        (unsigned long long)state->insn_count);
                return -1;
            } else if (insn.funct3 >= 1 && insn.funct3 <= 3) {
                // CSR read/write: CSRRW(1), CSRRS(2), CSRRC(3)
                //
                uint32_t csr_addr = insn.imm & 0xFFF;
                uint64_t csr_val = 0;
                switch (csr_addr) {
                case 0x001: csr_val = state->fcsr & 0x1F; break;
                case 0x002: csr_val = (state->fcsr >> 5) & 0x7; break;
                case 0x003: csr_val = state->fcsr & 0xFF; break;
                default:
                    fprintf(stderr, "rv64: unsupported CSR 0x%03X at PC=0x%llX\n",
                            csr_addr, (unsigned long long)state->pc);
                    return -1;
                }
                uint64_t new_val = csr_val;
                uint64_t rs1_val = state->x[insn.rs1];
                switch (insn.funct3) {
                case 1: new_val = rs1_val; break;
                case 2: new_val = csr_val | rs1_val; break;
                case 3: new_val = csr_val & ~rs1_val; break;
                }
                if (insn.rd) {
                    state->x[insn.rd] = csr_val;
                }
                switch (csr_addr) {
                case 0x001: state->fcsr = (state->fcsr & ~0x1Fu) | (static_cast<uint32_t>(new_val) & 0x1F); break;
                case 0x002: state->fcsr = (state->fcsr & ~0xE0u) | ((static_cast<uint32_t>(new_val) & 0x7) << 5); break;
                case 0x003: state->fcsr = static_cast<uint32_t>(new_val) & 0xFF; break;
                }
            } else if (insn.funct3 >= 5 && insn.funct3 <= 7) {
                // CSRRWI(5), CSRRSI(6), CSRRCI(7) — rs1 field is zimm
                //
                uint32_t csr_addr = insn.imm & 0xFFF;
                uint64_t csr_val = 0;
                switch (csr_addr) {
                case 0x001: csr_val = state->fcsr & 0x1F; break;
                case 0x002: csr_val = (state->fcsr >> 5) & 0x7; break;
                case 0x003: csr_val = state->fcsr & 0xFF; break;
                default:
                    fprintf(stderr, "rv64: unsupported CSR 0x%03X at PC=0x%llX\n",
                            csr_addr, (unsigned long long)state->pc);
                    return -1;
                }
                uint64_t new_val = csr_val;
                uint64_t zimm = insn.rs1;
                switch (insn.funct3) {
                case 5: new_val = zimm; break;
                case 6: new_val = csr_val | zimm; break;
                case 7: new_val = csr_val & ~zimm; break;
                }
                if (insn.rd) {
                    state->x[insn.rd] = csr_val;
                }
                switch (csr_addr) {
                case 0x001: state->fcsr = (state->fcsr & ~0x1Fu) | (static_cast<uint32_t>(new_val) & 0x1F); break;
                case 0x002: state->fcsr = (state->fcsr & ~0xE0u) | ((static_cast<uint32_t>(new_val) & 0x7) << 5); break;
                case 0x003: state->fcsr = static_cast<uint32_t>(new_val) & 0xFF; break;
                }
            } else {
                fprintf(stderr, "rv64: illegal SYSTEM funct3=%d at PC=0x%llX\n",
                        insn.funct3, (unsigned long long)state->pc);
                return -1;
            }
            break;

        // ---------------------------------------------------------------
        // RV64D — Double-precision floating point
        // ---------------------------------------------------------------

        case OP_FP_LOAD: {
            uint64_t addr = state->x[insn.rs1] + static_cast<uint64_t>(imm64);
            if (insn.funct3 == 3) {
                // FLD
                state->f[insn.rd] = mem_read64(mem, addr);
            } else {
                fprintf(stderr, "rv64: illegal FP load funct3=%d at PC=0x%llX\n",
                        insn.funct3, (unsigned long long)state->pc);
                return -1;
            }
            break;
        }

        case OP_FP_STORE: {
            uint64_t addr = state->x[insn.rs1] + static_cast<uint64_t>(imm64);
            if (insn.funct3 == 3) {
                // FSD
                mem_write64(mem, addr, state->f[insn.rs2]);
            } else {
                fprintf(stderr, "rv64: illegal FP store funct3=%d at PC=0x%llX\n",
                        insn.funct3, (unsigned long long)state->pc);
                return -1;
            }
            break;
        }

        case OP_FMADD: case OP_FMSUB: case OP_FNMSUB: case OP_FNMADD: {
            int fmt = insn.funct7 & 3;
            if (fmt != FP_FMT_D) {
                fprintf(stderr, "rv64: non-double FMA fmt=%d at PC=0x%llX\n",
                        fmt, (unsigned long long)state->pc);
                return -1;
            }
            double a = fp_unbox_d(state->f[insn.rs1]);
            double b = fp_unbox_d(state->f[insn.rs2]);
            double c = fp_unbox_d(state->f[insn.rs3]);
            double r;
            switch (insn.opcode) {
            case OP_FMADD:  r =  a * b + c; break;
            case OP_FMSUB:  r =  a * b - c; break;
            case OP_FNMSUB: r = -a * b + c; break;
            case OP_FNMADD: r = -a * b - c; break;
            default: r = 0; break;
            }
            state->f[insn.rd] = fp_box_d(r);
            break;
        }

        case OP_FP: {
            int funct5 = insn.funct7 >> 2;
            int fmt = insn.funct7 & 3;

            if (fmt != FP_FMT_D) {
                fprintf(stderr, "rv64: non-double FP fmt=%d funct5=0x%02X at PC=0x%llX\n",
                        fmt, funct5, (unsigned long long)state->pc);
                return -1;
            }

            switch (funct5) {
            case FP_FADD: {
                double a = fp_unbox_d(state->f[insn.rs1]);
                double b = fp_unbox_d(state->f[insn.rs2]);
                state->f[insn.rd] = fp_box_d(a + b);
                break;
            }
            case FP_FSUB: {
                double a = fp_unbox_d(state->f[insn.rs1]);
                double b = fp_unbox_d(state->f[insn.rs2]);
                state->f[insn.rd] = fp_box_d(a - b);
                break;
            }
            case FP_FMUL: {
                double a = fp_unbox_d(state->f[insn.rs1]);
                double b = fp_unbox_d(state->f[insn.rs2]);
                state->f[insn.rd] = fp_box_d(a * b);
                break;
            }
            case FP_FDIV: {
                double a = fp_unbox_d(state->f[insn.rs1]);
                double b = fp_unbox_d(state->f[insn.rs2]);
                state->f[insn.rd] = fp_box_d(a / b);
                break;
            }
            case FP_FSQRT: {
                double a = fp_unbox_d(state->f[insn.rs1]);
                state->f[insn.rd] = fp_box_d(sqrt(a));
                break;
            }
            case FP_FSGNJ: {
                uint64_t a = state->f[insn.rs1];
                uint64_t b = state->f[insn.rs2];
                uint64_t r;
                switch (insn.funct3) {
                case 0: r = (a & 0x7FFFFFFFFFFFFFFFULL) | (b & 0x8000000000000000ULL); break;
                case 1: r = (a & 0x7FFFFFFFFFFFFFFFULL) | ((~b) & 0x8000000000000000ULL); break;
                case 2: r = (a & 0x7FFFFFFFFFFFFFFFULL) | ((a ^ b) & 0x8000000000000000ULL); break;
                default: r = a; break;
                }
                state->f[insn.rd] = r;
                break;
            }
            case FP_FMINMAX: {
                double a = fp_unbox_d(state->f[insn.rs1]);
                double b = fp_unbox_d(state->f[insn.rs2]);
                double r;
                if (insn.funct3 == 0) {
                    // FMIN.D
                    if (std::isnan(a) && std::isnan(b)) {
                        uint64_t cn = 0x7FF8000000000000ULL;
                        memcpy(&r, &cn, 8);
                    } else if (std::isnan(a)) {
                        r = b;
                    } else if (std::isnan(b)) {
                        r = a;
                    } else if (a == 0 && b == 0) {
                        uint64_t sa, sb;
                        memcpy(&sa, &a, 8);
                        memcpy(&sb, &b, 8);
                        r = (sa & 0x8000000000000000ULL) ? a : b;
                    } else {
                        r = (a < b) ? a : b;
                    }
                } else {
                    // FMAX.D
                    if (std::isnan(a) && std::isnan(b)) {
                        uint64_t cn = 0x7FF8000000000000ULL;
                        memcpy(&r, &cn, 8);
                    } else if (std::isnan(a)) {
                        r = b;
                    } else if (std::isnan(b)) {
                        r = a;
                    } else if (a == 0 && b == 0) {
                        uint64_t sa, sb;
                        memcpy(&sa, &a, 8);
                        memcpy(&sb, &b, 8);
                        r = (sa & 0x8000000000000000ULL) ? b : a;
                    } else {
                        r = (a > b) ? a : b;
                    }
                }
                state->f[insn.rd] = fp_box_d(r);
                break;
            }
            case FP_FCMP: {
                double a = fp_unbox_d(state->f[insn.rs1]);
                double b = fp_unbox_d(state->f[insn.rs2]);
                uint64_t r = 0;
                switch (insn.funct3) {
                case 2: r = (!std::isnan(a) && !std::isnan(b) && a == b) ? 1 : 0; break;
                case 1: r = (!std::isnan(a) && !std::isnan(b) && a < b)  ? 1 : 0; break;
                case 0: r = (!std::isnan(a) && !std::isnan(b) && a <= b) ? 1 : 0; break;
                }
                if (insn.rd) {
                    state->x[insn.rd] = r;
                }
                break;
            }
            case FP_FCVTW: {
                // FCVT.W.D (rs2=0), FCVT.WU.D (rs2=1),
                // FCVT.L.D (rs2=2), FCVT.LU.D (rs2=3)
                //
                double a = fp_unbox_d(state->f[insn.rs1]);
                uint64_t r = 0;
                switch (insn.rs2) {
                case 0: {
                    // FCVT.W.D — double to signed int32, sign-extend to 64
                    int32_t v;
                    if (std::isnan(a)) v = INT32_MAX;
                    else if (a >= 2147483648.0) v = INT32_MAX;
                    else if (a < -2147483648.0) v = INT32_MIN;
                    else v = static_cast<int32_t>(a);
                    r = sext32(static_cast<uint32_t>(v));
                    break;
                }
                case 1: {
                    // FCVT.WU.D — double to unsigned int32, sign-extend to 64
                    uint32_t v;
                    if (std::isnan(a) || a < 0.0) v = 0;
                    else if (a >= 4294967296.0) v = UINT32_MAX;
                    else v = static_cast<uint32_t>(a);
                    r = sext32(v);
                    break;
                }
                case 2: {
                    // FCVT.L.D — double to signed int64
                    int64_t v;
                    if (std::isnan(a)) v = INT64_MAX;
                    else if (a >= 9223372036854775808.0) v = INT64_MAX;
                    else if (a < -9223372036854775808.0) v = INT64_MIN;
                    else v = static_cast<int64_t>(a);
                    r = static_cast<uint64_t>(v);
                    break;
                }
                case 3: {
                    // FCVT.LU.D — double to unsigned int64
                    uint64_t v;
                    if (std::isnan(a) || a < 0.0) v = 0;
                    else if (a >= 18446744073709551616.0) v = UINT64_MAX;
                    else v = static_cast<uint64_t>(a);
                    r = v;
                    break;
                }
                }
                if (insn.rd) {
                    state->x[insn.rd] = r;
                }
                break;
            }
            case FP_FCVTDW: {
                // FCVT.D.W (rs2=0), FCVT.D.WU (rs2=1),
                // FCVT.D.L (rs2=2), FCVT.D.LU (rs2=3)
                //
                switch (insn.rs2) {
                case 0:
                    state->f[insn.rd] = fp_box_d(static_cast<double>(static_cast<int32_t>(state->x[insn.rs1])));
                    break;
                case 1:
                    state->f[insn.rd] = fp_box_d(static_cast<double>(static_cast<uint32_t>(state->x[insn.rs1])));
                    break;
                case 2:
                    state->f[insn.rd] = fp_box_d(static_cast<double>(static_cast<int64_t>(state->x[insn.rs1])));
                    break;
                case 3:
                    state->f[insn.rd] = fp_box_d(static_cast<double>(state->x[insn.rs1]));
                    break;
                }
                break;
            }
            case FP_FCLASS: {
                if (insn.funct3 == 1) {
                    // FCLASS.D — classify double into 10-bit mask
                    double a = fp_unbox_d(state->f[insn.rs1]);
                    uint64_t bits;
                    memcpy(&bits, &a, 8);
                    uint32_t sign = static_cast<uint32_t>(bits >> 63);
                    uint32_t exp = static_cast<uint32_t>((bits >> 52) & 0x7FF);
                    uint64_t frac = bits & 0xFFFFFFFFFFFFFULL;
                    uint32_t cls = 0;
                    if (exp == 0x7FF && frac != 0) {
                        cls = (frac & 0x8000000000000ULL) ? (1 << 9) : (1 << 8);
                    } else if (exp == 0x7FF) {
                        cls = sign ? (1 << 0) : (1 << 7);
                    } else if (exp == 0 && frac == 0) {
                        cls = sign ? (1 << 3) : (1 << 4);
                    } else if (exp == 0) {
                        cls = sign ? (1 << 2) : (1 << 5);
                    } else {
                        cls = sign ? (1 << 1) : (1 << 6);
                    }
                    if (insn.rd) {
                        state->x[insn.rd] = cls;
                    }
                } else if (insn.funct3 == 0) {
                    // FMV.X.D — move FP bits to integer register (RV64 only)
                    if (insn.rd) {
                        state->x[insn.rd] = state->f[insn.rs1];
                    }
                }
                break;
            }
            case FP_FMVDX: {
                if (insn.funct3 == 0) {
                    // FMV.D.X — move integer bits to FP register (RV64 only)
                    state->f[insn.rd] = state->x[insn.rs1];
                }
                break;
            }
            default:
                fprintf(stderr, "rv64: unhandled FP funct5=0x%02X at PC=0x%llX\n",
                        funct5, (unsigned long long)state->pc);
                return -1;
            }
            break;
        }

        default:
            fprintf(stderr, "rv64: illegal opcode 0x%02X at PC=0x%llX\n",
                    insn.opcode, (unsigned long long)state->pc);
            return -1;
        }

        state->x[0] = 0;  // x0 is always zero
        state->pc = next_pc;
    }
}
