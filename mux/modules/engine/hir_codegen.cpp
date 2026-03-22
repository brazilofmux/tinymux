/*! \file hir_codegen.cpp
 * \brief HIR to RV64 code generation.
 *
 * RV64 instruction encoding, register allocation (linear scan),
 * output buffer allocation (liveness-based), and the hir_codegen()
 * function that walks HIR and emits RV64 machine code.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "dbt_compile.h"
#include "dbt_decoder.h"
#include "engine_api.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <string>

// RV64 instruction encoding
// ---------------------------------------------------------------

static uint32_t rv_i_type(uint8_t opcode, uint8_t rd, uint8_t funct3,
                           uint8_t rs1, int32_t imm) {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15)
         | ((static_cast<uint32_t>(imm) & 0xFFF) << 20);
}

static uint32_t rv_u_type(uint8_t opcode, uint8_t rd, int32_t imm) {
    return opcode | (rd << 7) | (static_cast<uint32_t>(imm) & 0xFFFFF000);
}

static uint32_t rv_ADDI(uint8_t rd, uint8_t rs1, int32_t imm) {
    return rv_i_type(OP_IMM, rd, ALU_ADDI, rs1, imm);
}
static uint32_t rv_LUI(uint8_t rd, int32_t imm) {
    return rv_u_type(OP_LUI, rd, imm);
}
static uint32_t rv_SLLI(uint8_t rd, uint8_t rs1, int32_t shamt) {
    return rv_i_type(OP_IMM, rd, ALU_SLLI, rs1, shamt);
}
static uint32_t rv_ECALL() {
    return rv_i_type(OP_SYSTEM, 0, 0, 0, 0);
}

// R-type encoding for register-register ALU ops.
//
static uint32_t rv_r_type(uint8_t opcode, uint8_t rd, uint8_t funct3,
                           uint8_t rs1, uint8_t rs2, uint8_t funct7) {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15)
         | (rs2 << 20) | (static_cast<uint32_t>(funct7) << 25);
}
static uint32_t rv_ADD(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, ALU_ADD, rs1, rs2, 0x00);
}
static uint32_t rv_SUB(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, ALU_ADD, rs1, rs2, 0x20);
}

// B-type encoding (branches).
//
static uint32_t rv_b_type(uint8_t funct3, uint8_t rs1, uint8_t rs2,
                           int32_t imm) {
    uint32_t u = static_cast<uint32_t>(imm);
    return OP_BRANCH
         | (((u >> 11) & 1) << 7)
         | (((u >> 1) & 0xF) << 8)
         | (static_cast<uint32_t>(funct3) << 12)
         | (static_cast<uint32_t>(rs1) << 15)
         | (static_cast<uint32_t>(rs2) << 20)
         | (((u >> 5) & 0x3F) << 25)
         | (((u >> 12) & 1) << 31);
}
static uint32_t rv_BEQ(uint8_t rs1, uint8_t rs2, int32_t off) {
    return rv_b_type(BR_BEQ, rs1, rs2, off);
}
static uint32_t rv_BNE(uint8_t rs1, uint8_t rs2, int32_t off) {
    return rv_b_type(BR_BNE, rs1, rs2, off);
}
static uint32_t rv_BGE(uint8_t rs1, uint8_t rs2, int32_t off) {
    return rv_b_type(BR_BGE, rs1, rs2, off);
}
static uint32_t rv_BGEU(uint8_t rs1, uint8_t rs2, int32_t off) {
    return rv_b_type(BR_BGEU, rs1, rs2, off);
}

// S-type encoding (stores).
//
static uint32_t rv_SB(uint8_t base, uint8_t src, int32_t off) {
    uint32_t u = static_cast<uint32_t>(off);
    return OP_STORE
         | ((u & 0x1F) << 7)
         | (static_cast<uint32_t>(ST_SB) << 12)
         | (static_cast<uint32_t>(base) << 15)
         | (static_cast<uint32_t>(src) << 20)
         | (((u >> 5) & 0x7F) << 25);
}

// Load byte unsigned.
//
static uint32_t rv_LBU(uint8_t rd, uint8_t base, int32_t off) {
    return rv_i_type(OP_LOAD, rd, LD_LBU, base, off);
}

// Store doubleword.
//
static uint32_t rv_SD(uint8_t base, uint8_t src, int32_t off) {
    uint32_t u = static_cast<uint32_t>(off);
    return OP_STORE
         | ((u & 0x1F) << 7)
         | (static_cast<uint32_t>(ST_SD) << 12)
         | (static_cast<uint32_t>(base) << 15)
         | (static_cast<uint32_t>(src) << 20)
         | (((u >> 5) & 0x7F) << 25);
}

// Load doubleword.
//
static uint32_t rv_LD(uint8_t rd, uint8_t base, int32_t off) {
    return rv_i_type(OP_LOAD, rd, LD_LD, base, off);
}

// J-type encoding (JAL).
//
static uint32_t rv_JAL(uint8_t rd, int32_t imm) {
    uint32_t u = static_cast<uint32_t>(imm);
    return OP_JAL
         | (static_cast<uint32_t>(rd) << 7)
         | (((u >> 12) & 0xFF) << 12)
         | (((u >> 11) & 1) << 20)
         | (((u >> 1) & 0x3FF) << 21)
         | (((u >> 20) & 1) << 31);
}

// Inline string copy: copy NUL-terminated string from src_reg to dest_reg.
// Clobbers t0 (x5).  5 instructions (byte-by-byte loop).
//
static void rv_emit_strcpy(std::vector<uint32_t> &code,
                            uint8_t dest_reg, uint8_t src_reg) {
    constexpr uint8_t t0 = 5;
    // loop:
    size_t loop = code.size();
    code.push_back(rv_LBU(t0, src_reg, 0));             // LBU t0, 0(src)
    code.push_back(rv_SB(dest_reg, t0, 0));              // SB t0, 0(dest)
    code.push_back(rv_ADDI(src_reg, src_reg, 1));        // src++
    code.push_back(rv_ADDI(dest_reg, dest_reg, 1));      // dest++
    int32_t off = -static_cast<int32_t>((code.size() - loop) * 4);
    code.push_back(rv_BNE(t0, 0, off));                  // BNE t0, x0, loop
}

// M extension: MUL, DIV, REM.
//
static uint32_t rv_MUL(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, 0, rs1, rs2, 0x01);
}
static uint32_t rv_DIV(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, 4, rs1, rs2, 0x01);
}
static uint32_t rv_REM(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, 6, rs1, rs2, 0x01);
}

// Bitwise operations.
//
static uint32_t rv_AND(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, ALU_AND, rs1, rs2, 0x00);
}
static uint32_t rv_OR(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, ALU_OR, rs1, rs2, 0x00);
}
static uint32_t rv_XOR(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, ALU_XOR, rs1, rs2, 0x00);
}
static uint32_t rv_SLT(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, ALU_SLT, rs1, rs2, 0x00);
}
static uint32_t rv_SLL(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, ALU_SLL, rs1, rs2, 0x00);
}
static uint32_t rv_SRL(uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return rv_r_type(OP_REG, rd, ALU_SRL, rs1, rs2, 0x00);
}

// D extension: double-precision floating point.
// RV64D uses R-type with opcode=OP_FP, funct7 encodes the operation,
// and rm (funct3) = 0 (RNE) or 7 (dynamic) for arithmetic.
// FLD/FSD use I/S-type with opcode OP_FP_LOAD/OP_FP_STORE, funct3=3.
//
static uint32_t rv_FADD_D(uint8_t fd, uint8_t fs1, uint8_t fs2) {
    return rv_r_type(OP_FP, fd, 7, fs1, fs2, 0x01);  // funct7=0000001
}
static uint32_t rv_FSUB_D(uint8_t fd, uint8_t fs1, uint8_t fs2) {
    return rv_r_type(OP_FP, fd, 7, fs1, fs2, 0x05);  // funct7=0000101
}
static uint32_t rv_FMUL_D(uint8_t fd, uint8_t fs1, uint8_t fs2) {
    return rv_r_type(OP_FP, fd, 7, fs1, fs2, 0x09);  // funct7=0001001
}
static uint32_t rv_FDIV_D(uint8_t fd, uint8_t fs1, uint8_t fs2) {
    return rv_r_type(OP_FP, fd, 7, fs1, fs2, 0x0D);  // funct7=0001101
}
static uint32_t rv_FSQRT_D(uint8_t fd, uint8_t fs1) {
    return rv_r_type(OP_FP, fd, 7, fs1, 0, 0x2D);    // funct7=0101101, rs2=0
}
static uint32_t rv_FSGNJN_D(uint8_t fd, uint8_t fs1, uint8_t fs2) {
    return rv_r_type(OP_FP, fd, 1, fs1, fs2, 0x11);  // funct7=0010001, funct3=1 (FSGNJN)
}
// FNEG.D is a pseudo: FSGNJN.D fd, fs, fs
static uint32_t rv_FNEG_D(uint8_t fd, uint8_t fs) {
    return rv_FSGNJN_D(fd, fs, fs);
}
// FCVT.D.L: int64 → double (rs2=2 for L)
static uint32_t rv_FCVT_D_L(uint8_t fd, uint8_t rs1) {
    return rv_r_type(OP_FP, fd, 7, rs1, 2, 0x69);    // funct7=1101001
}
// FCVT.L.D: double → int64 (rs2=0 for L, rm=1 for RTZ)
static uint32_t rv_FCVT_L_D(uint8_t rd, uint8_t fs1) {
    return rv_r_type(OP_FP, rd, 1, fs1, 2, 0x61);    // funct7=1100001, rm=RTZ
}
// FMV.X.D: move FP bits to integer register
static uint32_t rv_FMV_X_D(uint8_t rd, uint8_t fs1) {
    return rv_r_type(OP_FP, rd, 0, fs1, 0, 0x71);    // funct7=1110001
}
// FMV.D.X: move integer bits to FP register
static uint32_t rv_FMV_D_X(uint8_t fd, uint8_t rs1) {
    return rv_r_type(OP_FP, fd, 0, rs1, 0, 0x79);    // funct7=1111001
}
// FEQ.D: float equality → integer rd
static uint32_t rv_FEQ_D(uint8_t rd, uint8_t fs1, uint8_t fs2) {
    return rv_r_type(OP_FP, rd, 2, fs1, fs2, 0x51);  // funct7=1010001, funct3=2
}
// FLT.D: float less-than → integer rd
static uint32_t rv_FLT_D(uint8_t rd, uint8_t fs1, uint8_t fs2) {
    return rv_r_type(OP_FP, rd, 1, fs1, fs2, 0x51);  // funct7=1010001, funct3=1
}
// FLE.D: float less-or-equal → integer rd
static uint32_t rv_FLE_D(uint8_t rd, uint8_t fs1, uint8_t fs2) {
    return rv_r_type(OP_FP, rd, 0, fs1, fs2, 0x51);  // funct7=1010001, funct3=0
}
// FLD: load double from memory
static uint32_t rv_FLD(uint8_t fd, uint8_t base, int32_t off) {
    return rv_i_type(OP_FP_LOAD, fd, 3, base, off);
}
// FSD: store double to memory
static uint32_t rv_FSD(uint8_t base, uint8_t fs, int32_t off) {
    // S-type encoding, same as rv_SD but with OP_FP_STORE
    return OP_FP_STORE
         | ((off & 0x1F) << 7)
         | (3 << 12)
         | (base << 15)
         | (fs << 20)
         | (((off >> 5) & 0x7F) << 25);
}

// ---------------------------------------------------------------
// Inline RISC-V atoi: parse decimal string → signed integer.
//
// Input:  addr_reg = guest address of NUL-terminated string
// Output: out_reg  = signed 64-bit integer
// Clobbers: t0(x5), t1(x6), t2(x7), t3(x28), t4(x29), addr_reg
// 19 instructions.
// ---------------------------------------------------------------

static void rv_emit_atoi(std::vector<uint32_t> &code,
                          uint8_t addr_reg, uint8_t out_reg) {
    constexpr uint8_t t0=5, t1=6, t2=7, t3=28, t4=29;

    code.push_back(rv_ADDI(t1, 0, 0));                 //  0: acc = 0
    code.push_back(rv_ADDI(t2, 0, 0));                 //  1: sign = 0
    code.push_back(rv_LBU(t0, addr_reg, 0));           //  2: load byte
    code.push_back(rv_ADDI(t3, 0, 45));                //  3: t3 = '-'
    size_t bne_sign = code.size();
    code.push_back(0);                                  //  4: BNE → skip_sign (patch)
    code.push_back(rv_ADDI(t2, 0, 1));                 //  5: sign = 1
    code.push_back(rv_ADDI(addr_reg, addr_reg, 1));    //  6: advance past '-'
    // skip_sign:
    size_t skip_sign = code.size();
    code[bne_sign] = rv_BNE(t0, t3,
        static_cast<int32_t>((skip_sign - bne_sign) * 4));

    code.push_back(rv_LBU(t0, addr_reg, 0));           //  7: (re)load byte
    // digit_loop:
    size_t digit_loop = code.size();
    code.push_back(rv_ADDI(t4, t0, -48));              //  8: digit = byte - '0'
    code.push_back(rv_ADDI(t3, 0, 10));                //  9: t3 = 10
    size_t bgeu_done = code.size();
    code.push_back(0);                                  // 10: BGEU → done (patch)
    code.push_back(rv_MUL(t1, t1, t3));                // 11: acc *= 10
    code.push_back(rv_ADD(t1, t1, t4));                 // 12: acc += digit
    code.push_back(rv_ADDI(addr_reg, addr_reg, 1));    // 13: advance
    code.push_back(rv_LBU(t0, addr_reg, 0));           // 14: load next byte
    size_t bk = code.size();
    code.push_back(rv_BEQ(0, 0,                        // 15: j digit_loop
        static_cast<int32_t>((digit_loop - bk) * 4)));
    // done:
    size_t done = code.size();
    code[bgeu_done] = rv_BGEU(t4, t3,
        static_cast<int32_t>((done - bgeu_done) * 4));

    size_t beq_pos = code.size();
    code.push_back(0);                                  // 16: BEQ → skip_neg (patch)
    code.push_back(rv_SUB(t1, 0, t1));                 // 17: negate
    size_t skip_neg = code.size();
    code[beq_pos] = rv_BEQ(t2, 0,
        static_cast<int32_t>((skip_neg - beq_pos) * 4));

    code.push_back(rv_ADDI(out_reg, t1, 0));           // 18: mv out, acc
}

// ---------------------------------------------------------------
// Inline RISC-V strcmp: compare two NUL-terminated strings.
//
// Input:  addr_a, addr_b = guest addresses of the two strings
// Output: out_reg = -1 (a<b), 0 (a==b), 1 (a>b)
// Clobbers: t0(x5), t1(x6), addr_a, addr_b
// ---------------------------------------------------------------

static void rv_emit_strcmp(std::vector<uint32_t> &code,
                           uint8_t addr_a, uint8_t addr_b,
                           uint8_t out_reg) {
    constexpr uint8_t t0 = 5, t1 = 6;

    // loop:
    size_t loop = code.size();
    code.push_back(rv_LBU(t0, addr_a, 0));              // t0 = *a
    code.push_back(rv_LBU(t1, addr_b, 0));              // t1 = *b
    size_t bne_differ = code.size();
    code.push_back(0);                                    // BNE t0, t1 → differ (patch)
    size_t beq_equal = code.size();
    code.push_back(0);                                    // BEQ t0, x0 → equal (patch)
    code.push_back(rv_ADDI(addr_a, addr_a, 1));          // a++
    code.push_back(rv_ADDI(addr_b, addr_b, 1));          // b++
    size_t j_loop = code.size();
    code.push_back(rv_BEQ(0, 0,                          // j loop
        static_cast<int32_t>((loop - j_loop) * 4)));

    // differ:  t0 != t1
    size_t differ = code.size();
    code[bne_differ] = rv_BNE(t0, t1,
        static_cast<int32_t>((differ - bne_differ) * 4));
    code.push_back(rv_SLT(out_reg, t0, t1));             // out = (a < b) ? 1 : 0
    size_t bne_done = code.size();
    code.push_back(0);                                    // BNE out, x0 → neg (patch)
    code.push_back(rv_ADDI(out_reg, 0, 1));              // out = 1 (a > b)
    size_t j_done = code.size();
    code.push_back(0);                                    // J → done (patch)

    // neg:  a < b → out = -1
    size_t neg = code.size();
    code[bne_done] = rv_BNE(out_reg, 0,
        static_cast<int32_t>((neg - bne_done) * 4));
    code.push_back(rv_SUB(out_reg, 0, out_reg));          // out = -1 (negate the 1 from SLT... wait, SLT gave 1, so -1 is correct)
    // Actually: SLT out, t0, t1 → out=1 if a<b.  We want -1 for a<b.
    // SUB out, x0, out → out = -1.  Correct.
    size_t j_done2 = code.size();
    code.push_back(0);                                    // J → done (patch)

    // equal:  both NUL
    size_t equal = code.size();
    code[beq_equal] = rv_BEQ(t0, 0,
        static_cast<int32_t>((equal - beq_equal) * 4));
    code.push_back(rv_ADDI(out_reg, 0, 0));              // out = 0

    // done:
    size_t done = code.size();
    code[j_done] = rv_BEQ(0, 0,
        static_cast<int32_t>((done - j_done) * 4));
    code[j_done2] = rv_BEQ(0, 0,
        static_cast<int32_t>((done - j_done2) * 4));
}

// ---------------------------------------------------------------
// Inline RISC-V itoa: signed integer → decimal string.
//
// Input:  val_reg = signed 64-bit integer
//         buf_reg = guest address of output buffer (≥21 bytes)
// Output: NUL-terminated string at buf_reg
// Clobbers: t0(x5), t1(x6), t2(x7), t3(x28), t4(x29),
//           t5(x30), t6(x31), buf_reg
// 30 instructions.
// ---------------------------------------------------------------

static void rv_emit_itoa(std::vector<uint32_t> &code,
                          uint8_t val_reg, uint8_t buf_reg) {
    constexpr uint8_t t0=5, t1=6, t2=7, t3=28, t4=29, t5=30, t6=31;

    code.push_back(rv_ADDI(t0, buf_reg, 0));           //  0: wr = buf
    code.push_back(rv_ADDI(t1, val_reg, 0));            //  1: t1 = val
    size_t bge_pos = code.size();
    code.push_back(0);                                  //  2: BGE → skip_neg (patch)
    code.push_back(rv_ADDI(t4, 0, 45));                //  3: t4 = '-'
    code.push_back(rv_SB(t0, t4, 0));                  //  4: write '-'
    code.push_back(rv_ADDI(t0, t0, 1));                //  5: advance wr
    code.push_back(rv_SUB(t1, 0, t1));                 //  6: negate
    // skip_neg:
    size_t skip_neg = code.size();
    code[bge_pos] = rv_BGE(t1, 0,
        static_cast<int32_t>((skip_neg - bge_pos) * 4));

    code.push_back(rv_ADDI(t5, t0, 0));                //  7: digit_start = wr
    size_t bne_nz = code.size();
    code.push_back(0);                                  //  8: BNE → digit_loop (patch)
    code.push_back(rv_ADDI(t4, 0, 48));                //  9: '0'
    code.push_back(rv_SB(t0, t4, 0));                  // 10: write '0'
    code.push_back(rv_ADDI(t0, t0, 1));                // 11: advance
    size_t beq_nul = code.size();
    code.push_back(0);                                  // 12: BEQ → nul_term (patch)

    // digit_loop:
    size_t digit_loop = code.size();
    code[bne_nz] = rv_BNE(t1, 0,
        static_cast<int32_t>((digit_loop - bne_nz) * 4));
    code.push_back(rv_ADDI(t3, 0, 10));                // 13: t3 = 10
    code.push_back(rv_REM(t2, t1, t3));                // 14: t2 = val % 10
    code.push_back(rv_DIV(t1, t1, t3));                // 15: t1 = val / 10
    code.push_back(rv_ADDI(t2, t2, 48));               // 16: '0' + digit
    code.push_back(rv_SB(t0, t2, 0));                  // 17: write digit
    code.push_back(rv_ADDI(t0, t0, 1));                // 18: advance
    code.push_back(rv_BNE(t1, 0,                       // 19: loop if more
        static_cast<int32_t>((digit_loop - (code.size())) * 4)));

    // nul_term:
    size_t nul_term = code.size();
    code[beq_nul] = rv_BEQ(0, 0,
        static_cast<int32_t>((nul_term - beq_nul) * 4));
    code.push_back(rv_SB(t0, 0, 0));                   // 20: write '\0'
    code.push_back(rv_ADDI(t6, t0, -1));               // 21: end = wr - 1

    // reverse_loop:
    size_t rev_loop = code.size();
    size_t bge_rev = code.size();
    code.push_back(0);                                  // 22: BGE → done (patch)
    code.push_back(rv_LBU(t3, t5, 0));                 // 23: t3 = *start
    code.push_back(rv_LBU(t4, t6, 0));                 // 24: t4 = *end
    code.push_back(rv_SB(t5, t4, 0));                  // 25: *start = t4
    code.push_back(rv_SB(t6, t3, 0));                  // 26: *end = t3
    code.push_back(rv_ADDI(t5, t5, 1));                // 27: start++
    code.push_back(rv_ADDI(t6, t6, -1));               // 28: end--
    code.push_back(rv_BEQ(0, 0,                        // 29: j reverse_loop
        static_cast<int32_t>((rev_loop - (code.size())) * 4)));

    // done:
    size_t done = code.size();
    code[bge_rev] = rv_BGE(t5, t6,
        static_cast<int32_t>((done - bge_rev) * 4));
}

// Load a signed 64-bit value into a register.
//
static void rv_load_i64(std::vector<uint32_t> &code, uint8_t rd, int64_t val) {
    if (val >= -2048 && val <= 2047) {
        code.push_back(rv_ADDI(rd, 0, static_cast<int32_t>(val)));
        return;
    }
    if (val >= -2147483648LL && val <= 2147483647LL) {
        uint32_t uval = static_cast<uint32_t>(static_cast<int32_t>(val));
        uint32_t hi = uval & 0xFFFFF000;
        int32_t lo = static_cast<int32_t>(uval & 0xFFF);
        if (lo & 0x800) {
            hi += 0x1000;
            lo -= 0x1000;
        }
        code.push_back(rv_LUI(rd, static_cast<int32_t>(hi)));
        if (lo) code.push_back(rv_ADDI(rd, rd, lo));
        return;
    }
    // Values beyond 32-bit: load zero (shouldn't happen for typical softcode).
    code.push_back(rv_ADDI(rd, 0, 0));
}

// Load a value into a register using LUI + ADDI.
//
static void rv_load_val(std::vector<uint32_t> &code, uint8_t rd,
                         uint64_t val) {
    if (val == 0) {
        code.push_back(rv_ADDI(rd, 0, 0));
        return;
    }

    int32_t sval = static_cast<int32_t>(val);
    if (sval >= -2048 && sval <= 2047 && val == static_cast<uint64_t>(static_cast<uint32_t>(sval))) {
        code.push_back(rv_ADDI(rd, 0, sval));
        return;
    }

    uint32_t hi = static_cast<uint32_t>(val) & 0xFFFFF000;
    int32_t lo = static_cast<int32_t>(val & 0xFFF);
    if (lo & 0x800) {
        hi += 0x1000;
        lo = lo - 0x1000;
    }
    code.push_back(rv_LUI(rd, hi));
    if (lo) code.push_back(rv_ADDI(rd, rd, lo));
}

// Emit ECALL to call a function.
//
// If func_idx > 0, uses indexed dispatch (ECALL_CALL_INDEX, a0 = index).
// Otherwise, uses string dispatch (ECALL_CALL_FUNC, a0 = name_addr).
//
static void rv_emit_call(std::vector<uint32_t> &code,
                          uint64_t name_addr, uint64_t fargs_addr,
                          int nfargs, uint64_t out_addr, int out_size,
                          int func_idx = 0) {
    if (func_idx > 0) {
        // Indexed dispatch — no string lookup at runtime.
        code.push_back(rv_ADDI(17, 0, 0x101));    // a7 = ECALL_CALL_INDEX
        rv_load_val(code, 10, func_idx);            // a0 = function index
    } else {
        // String-based dispatch (fallback).
        code.push_back(rv_ADDI(17, 0, 0x100));    // a7 = ECALL_CALL_FUNC
        rv_load_val(code, 10, name_addr);           // a0 = name
    }
    rv_load_val(code, 11, fargs_addr);             // a1 = fargs
    code.push_back(rv_ADDI(12, 0, nfargs));        // a2 = nfargs
    rv_load_val(code, 13, out_addr);               // a3 = output
    rv_load_val(code, 14, out_size);               // a4 = outsize
    code.push_back(rv_ECALL());
}

static void rv_emit_exit(std::vector<uint32_t> &code) {
    code.push_back(rv_ADDI(17, 0, ECALL_EXIT));
    code.push_back(rv_ADDI(10, 0, 0));
    code.push_back(rv_ECALL());
}

// Emit a Tier 2 call: JAL to pre-compiled blob function.
// Calling convention: a0=output, a1=fargs, a2=nfargs.
// Return value in a0 (pointer to output buffer).
//
static void rv_emit_tier2_call(std::vector<uint32_t> &code,
                                uint64_t fargs_addr, int nfargs,
                                uint64_t out_addr, uint64_t func_guest_addr) {
    rv_load_val(code, 10, out_addr);                  // a0 = output
    rv_load_val(code, 11, fargs_addr);                // a1 = fargs
    code.push_back(rv_ADDI(12, 0, nfargs));           // a2 = nfargs

    // JAL ra, target — offset relative to current PC.
    uint64_t current_pc = code.size() * 4;  // guest PC of the JAL
    int32_t offset = static_cast<int32_t>(func_guest_addr - current_pc);
    code.push_back(rv_JAL(1, offset));                // JAL ra, blob_func
}

// ---------------------------------------------------------------
// Walks the HIR instruction array and emits RV64 instructions.
// Each HIR instruction gets a "location" — either a guest memory
// address (TY_STRING) or an RV64 register (TY_INT).
// ===============================================================

struct hir_loc {
    uint64_t addr;       // guest memory address (for strings)
    uint8_t  reg;        // RV64 register (for integers)
    bool     in_reg;     // true if value is in a register
    int      spill_slot; // -1 = not spilled, >=0 = stack slot index
};

// Branch patch record for backpatching.
struct branch_patch {
    int code_idx;       // index into rc.code
    int target_blk;     // target block number
};

// ---------------------------------------------------------------
// Register allocation: linear scan over SSA live ranges
//
// Poletto-Sarkar algorithm.  Computes live intervals for all
// integer-typed SSA values, then assigns the 11 saved registers
// (s1-s11).  When register pressure exceeds 11, the interval
// ending furthest in the future is spilled to the RV64 stack.
// ---------------------------------------------------------------

// Allocatable integer registers: s1-s11 (x9, x18-x27).
static constexpr int RA_NUM_REGS = 11;
static constexpr uint8_t RA_REGS[RA_NUM_REGS] = {
    9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27
};

// Scratch register for spill/reload (s0 = x8, callee-saved).
static constexpr uint8_t RA_SCRATCH = 8;

// Second scratch for two-operand instructions (t3 = x28).
// Safe because arithmetic ops don't call atoi/itoa.
static constexpr uint8_t RA_SCRATCH2 = 28;

struct live_interval {
    int     value;      // HIR instruction index (SSA value number)
    int     start;      // program point of definition
    int     end;        // program point of last use (inclusive)
};

struct reg_alloc_result {
    uint8_t reg[HIR_MAX_INSNS];        // assigned register (0 = spilled/none)
    int     spill_slot[HIR_MAX_INSNS]; // -1 = not spilled
    int     n_spill_slots;             // total spill slots used
};

static bool needs_output_buffer(hir_program &h, int i) {
    if (h.ty[i] != TY_STRING) return false;
    switch (h.kind[i]) {
    case HIR_CALL:
    case HIR_STRCAT:
    case HIR_ITOA:
    case HIR_FTOA:
    case HIR_PHI:
    case HIR_COPY:
        return true;
    default:
        return false;
    }
}

struct output_alloc_result {
    uint64_t addr[HIR_MAX_INSNS];
};

static output_alloc_result allocate_output_buffers(rv_compiler &rc,
                                                   std::vector<live_interval> &intervals) {
    output_alloc_result result;
    memset(result.addr, 0, sizeof(result.addr));

    if (intervals.empty()) return result;

    std::sort(intervals.begin(), intervals.end(),
              [](const live_interval &a, const live_interval &b) {
                  return a.start < b.start;
              });

    struct active_entry {
        int end;
        int value;
        uint64_t addr;
    };
    std::vector<active_entry> active;
    std::vector<uint64_t> free_pool;

    for (auto &iv : intervals) {
        size_t j = 0;
        while (j < active.size()) {
            if (active[j].end >= iv.start) break;
            free_pool.push_back(active[j].addr);
            active.erase(active.begin() + j);
        }

        uint64_t addr;
        if (!free_pool.empty()) {
            addr = free_pool.back();
            free_pool.pop_back();
        } else {
            addr = rc.alloc_output();
            if (addr == 0) break;
        }

        result.addr[iv.value] = addr;
        active_entry ae = {iv.end, iv.value, addr};
        auto pos = std::lower_bound(active.begin(), active.end(), ae,
            [](const active_entry &a, const active_entry &b) {
                return a.end < b.end;
            });
        active.insert(pos, ae);
    }
    return result;
}

// Returns true if HIR instruction i produces an integer that needs
// a register.
//
static bool needs_int_reg(hir_program &h, int i) {
    switch (h.kind[i]) {
    case HIR_ICONST:
    case HIR_ATOI:
    case HIR_STRCMP:
    case HIR_LUA_GETI:
    case HIR_LUA_ALOAD:
    case HIR_ADD: case HIR_SUB: case HIR_MUL: case HIR_DIV: case HIR_REM:
    case HIR_NEG: case HIR_ABS: case HIR_SIGN:
    case HIR_MAX: case HIR_MIN:
    case HIR_EQ:  case HIR_NE:  case HIR_GT:  case HIR_LT:
    case HIR_GE:  case HIR_LE:
    case HIR_NOT: case HIR_BOOL:
    case HIR_INC: case HIR_DEC:
    case HIR_BAND: case HIR_BOR: case HIR_BXOR: case HIR_BNOT:
    case HIR_SHL: case HIR_SHR:
    case HIR_FTOI:              // float → int produces integer
    case HIR_FEQ: case HIR_FLT: case HIR_FLE:  // float cmp → int 0/1
        return true;
    case HIR_PHI:
        return h.ty[i] == TY_INT;
    case HIR_COPY:
        return h.ty[i] == TY_INT;
    default:
        return false;
    }
}

// Returns true if HIR instruction i produces a float that needs
// an FP register (spilled to guest memory).
//
static bool needs_fp_reg(hir_program &h, int i) {
    switch (h.kind[i]) {
    case HIR_FCONST:
    case HIR_FADD: case HIR_FSUB: case HIR_FMUL: case HIR_FDIV:
    case HIR_FNEG: case HIR_FSQRT:
    case HIR_ITOF:
        return true;
    case HIR_PHI:
        return h.ty[i] == TY_FLOAT;
    case HIR_COPY:
        return h.ty[i] == TY_FLOAT;
    default:
        return false;
    }
}

// Compute live intervals for values passing the filter.
//
static void compute_live_ranges(hir_program &h,
                                 std::vector<live_interval> &intervals,
                                 bool (*filter)(hir_program &, int)) {
    // Assign program points in codegen order (blocks in layout order,
    // instructions within each block in order).
    int prog_point[HIR_MAX_INSNS];
    int block_end_pp[HIR_MAX_BLOCKS];
    memset(prog_point, -1, sizeof(int) * h.n_insns);

    int pp = 0;
    for (int b = 0; b < h.n_blocks; b++) {
        if (h.block_first[b] <= h.block_last[b]) {
            for (int i = h.block_first[b]; i <= h.block_last[b]; i++) {
                if (h.blk[i] == b) {
                    prog_point[i] = pp++;
                }
            }
        }
        block_end_pp[b] = pp++;  // virtual point at end of block
    }
    int max_pp = pp;

    // Find last use program point for each value.
    int last_use[HIR_MAX_INSNS];
    memset(last_use, -1, sizeof(int) * h.n_insns);

    for (int i = 0; i < h.n_insns; i++) {
        if (prog_point[i] < 0) continue;
        int pp_i = prog_point[i];

        // src1 is always a value reference.
        if (h.src1[i] >= 0 && h.src1[i] < h.n_insns) {
            if (pp_i > last_use[h.src1[i]])
                last_use[h.src1[i]] = pp_i;
        }

        // src2 is a value reference EXCEPT for BRC (where it's a block).
        if (h.kind[i] != HIR_BRC && h.src2[i] >= 0 && h.src2[i] < h.n_insns) {
            if (pp_i > last_use[h.src2[i]])
                last_use[h.src2[i]] = pp_i;
        }

        // Call/strcat arguments.
        if (h.kind[i] == HIR_CALL || h.kind[i] == HIR_STRCAT) {
            for (int j = 0; j < h.cnargs[i]; j++) {
                int arg = h.carg[h.cbase[i] + j];
                if (arg >= 0 && arg < h.n_insns) {
                    if (pp_i > last_use[arg])
                        last_use[arg] = pp_i;
                }
            }
        }

        // PHI arguments: value is used at end of predecessor block.
        if (h.kind[i] == HIR_PHI) {
            for (int j = 0; j < h.pnargs[i]; j++) {
                int val = h.pval[h.pbase[i] + j];
                int pred_blk = h.pblk[h.pbase[i] + j];
                if (val >= 0 && val < h.n_insns &&
                    pred_blk >= 0 && pred_blk < h.n_blocks) {
                    int end_pp = block_end_pp[pred_blk];
                    if (end_pp > last_use[val])
                        last_use[val] = end_pp;
                }
            }
        }
    }

    // Loop-aware liveness extension.
    //
    // A back-edge is (latch → header) where rpo_pos[header] <= rpo_pos[latch].
    // Any value used inside the loop body must be live through the entire
    // loop — its last_use must extend to at least the latch's block_end_pp.
    // Without this, values used at the header (e.g., nwords for the loop
    // condition) get their registers reused inside the body, corrupting
    // the value on the next iteration.
    //
    if (h.n_rpo > 0) {
        for (int b = 0; b < h.n_blocks; b++) {
            for (int s = 0; s < h.block_nsucc[b]; s++) {
                int tgt = h.block_succ[b][s];
                if (tgt < 0) continue;
                // Back-edge: successor has <= RPO position.
                if (h.rpo_pos[tgt] <= h.rpo_pos[b]) {
                    int latch_end = block_end_pp[b];
                    // Extend every value used inside the loop
                    // (any block with RPO position between header and latch).
                    for (int i = 0; i < h.n_insns; i++) {
                        if (prog_point[i] < 0) continue;
                        int ib = h.blk[i];
                        if (h.rpo_pos[ib] < h.rpo_pos[tgt] ||
                            h.rpo_pos[ib] > h.rpo_pos[b]) continue;
                        // This instruction is inside the loop.
                        // Extend any operand defined outside the loop.
                        auto extend = [&](int v) {
                            if (v < 0 || v >= h.n_insns) return;
                            if (prog_point[v] < 0) return;
                            int vb = h.blk[v];
                            // Value defined outside loop (or at header).
                            if (h.rpo_pos[vb] <= h.rpo_pos[tgt]) {
                                if (latch_end > last_use[v])
                                    last_use[v] = latch_end;
                            }
                        };
                        extend(h.src1[i]);
                        if (h.kind[i] != HIR_BRC)
                            extend(h.src2[i]);
                        if (h.kind[i] == HIR_CALL || h.kind[i] == HIR_STRCAT) {
                            for (int j = 0; j < h.cnargs[i]; j++)
                                extend(h.carg[h.cbase[i] + j]);
                        }
                    }
                }
            }
        }
    }

    // The final result must survive to the end of the program.
    if (h.result >= 0 && h.result < h.n_insns && filter(h, h.result)) {
        last_use[h.result] = max_pp - 1;
    }

    // Build intervals for values passing the filter.
    intervals.clear();
    for (int i = 0; i < h.n_insns; i++) {
        if (!filter(h, i)) continue;
        if (prog_point[i] < 0) continue;  // unreachable

        int def = prog_point[i];

        // PHI: start at earliest predecessor's block end point.
        if (h.kind[i] == HIR_PHI && h.pnargs[i] > 0) {
            for (int j = 0; j < h.pnargs[i]; j++) {
                int pred_blk = h.pblk[h.pbase[i] + j];
                if (pred_blk >= 0 && pred_blk < h.n_blocks) {
                    int ep = block_end_pp[pred_blk];
                    if (ep < def) def = ep;
                }
            }
        }

        int end = (last_use[i] >= def) ? last_use[i] : def;

        intervals.push_back({i, def, end});
    }
}

// Poletto-Sarkar linear scan register allocation.
//
static reg_alloc_result linear_scan(std::vector<live_interval> &intervals) {
    reg_alloc_result result;
    memset(result.reg, 0, sizeof(result.reg));
    memset(result.spill_slot, -1, sizeof(result.spill_slot));
    result.n_spill_slots = 0;

    if (intervals.empty()) return result;

    // Sort intervals by start point.
    std::sort(intervals.begin(), intervals.end(),
              [](const live_interval &a, const live_interval &b) {
                  return a.start < b.start;
              });

    // Free register pool (stack-based for fast alloc/free).
    uint8_t free_regs[RA_NUM_REGS];
    int n_free = RA_NUM_REGS;
    for (int i = 0; i < RA_NUM_REGS; i++) {
        free_regs[i] = RA_REGS[RA_NUM_REGS - 1 - i];  // s11 at bottom
    }

    // Active intervals, sorted by end point ascending.
    // Small-N (max 11 entries), so linear insertion is fine.
    struct active_entry {
        int end;
        int value;
        uint8_t reg;
    };
    std::vector<active_entry> active;

    for (auto &iv : intervals) {
        // ExpireOldIntervals: remove intervals that ended before iv.start.
        size_t j = 0;
        while (j < active.size()) {
            if (active[j].end >= iv.start) break;  // sorted: rest are live
            // Return register to free pool.
            free_regs[n_free++] = active[j].reg;
            active.erase(active.begin() + j);
            // Don't increment j — next element shifted down.
        }

        if (n_free > 0) {
            // Assign a register.
            uint8_t reg = free_regs[--n_free];
            result.reg[iv.value] = reg;

            // Insert into active, maintaining sort by end.
            active_entry ae = {iv.end, iv.value, reg};
            auto pos = std::lower_bound(active.begin(), active.end(), ae,
                [](const active_entry &a, const active_entry &b) {
                    return a.end < b.end;
                });
            active.insert(pos, ae);
        } else {
            // Spill: evict the interval ending furthest in the future.
            auto &spill = active.back();  // largest end
            if (spill.end > iv.end) {
                // Spill the active interval, give its register to iv.
                result.reg[iv.value] = spill.reg;
                result.reg[spill.value] = 0;
                result.spill_slot[spill.value] = result.n_spill_slots++;

                // Remove spilled interval from active.
                active.pop_back();

                // Insert iv into active.
                active_entry ae = {iv.end, iv.value, result.reg[iv.value]};
                auto pos = std::lower_bound(active.begin(), active.end(), ae,
                    [](const active_entry &a, const active_entry &b) {
                        return a.end < b.end;
                    });
                active.insert(pos, ae);
            } else {
                // Spill the new interval (it ends later than everything).
                result.spill_slot[iv.value] = result.n_spill_slots++;
            }
        }
    }

    return result;
}

// Spill slot stack offset: -8*(slot+1) from SP.
static int32_t spill_offset(int slot) {
    return -8 * (slot + 1);
}

// Emit SD reg, off(sp) — store integer register to spill slot.
static void emit_spill_store(std::vector<uint32_t> &code, uint8_t reg, int slot) {
    code.push_back(rv_SD(2, reg, spill_offset(slot)));
}

// Emit LD rd, off(sp) — reload integer register from spill slot.
static void emit_spill_load(std::vector<uint32_t> &code, uint8_t rd, int slot) {
    code.push_back(rv_LD(rd, 2, spill_offset(slot)));
}

// Get the register holding integer value v, reloading from spill
// slot if necessary.  scratch = register to reload into if spilled.
//
static uint8_t ra_get_reg(rv_compiler &rc, hir_loc *loc, int v,
                           uint8_t scratch) {
    if (v < 0) return 0;
    if (loc[v].spill_slot >= 0 && !loc[v].in_reg) {
        emit_spill_load(rc.code, scratch, loc[v].spill_slot);
        return scratch;
    }
    return loc[v].reg;
}

// Set loc[i] from allocation result and optionally emit spill.
// dest = the register the value was computed into.
// Returns the destination register.
//
static void ra_set_loc(rv_compiler &rc, hir_loc *loc,
                        reg_alloc_result &alloc, int i, uint8_t computed_in) {
    uint8_t assigned = alloc.reg[i];
    int slot = alloc.spill_slot[i];

    if (assigned != 0) {
        // Value lives in a register.
        loc[i].reg = assigned;
        loc[i].in_reg = true;
        loc[i].spill_slot = -1;
    } else if (slot >= 0) {
        // Value is spilled — emit store.
        emit_spill_store(rc.code, computed_in, slot);
        loc[i].reg = 0;
        loc[i].in_reg = false;
        loc[i].spill_slot = slot;
    }
}

// Emit PHI copies: when branching from from_blk to to_blk,
// emit moves for any PHI nodes at the target block.
//
static void emit_phi_copies(hir_program &h, rv_compiler &rc,
                             hir_loc *loc, int from_blk, int to_blk) {
    if (h.block_first[to_blk] > h.block_last[to_blk]) return;
    for (int i = h.block_first[to_blk]; i <= h.block_last[to_blk]; i++) {
        if (h.blk[i] != to_blk || h.kind[i] != HIR_PHI) continue;

        // Find the PHI argument for from_blk.
        int base = h.pbase[i];
        for (int j = 0; j < h.pnargs[i]; j++) {
            if (h.pblk[base + j] != from_blk) continue;
            int val = h.pval[base + j];
            if (val < 0) break;

            bool phi_is_int = (loc[i].in_reg || loc[i].spill_slot >= 0);
            if (phi_is_int) {
                // Integer PHI (registered or spilled).
                uint8_t phi_dest = loc[i].in_reg ? loc[i].reg : RA_SCRATCH;
                uint8_t val_reg;
                if (loc[val].in_reg) {
                    val_reg = loc[val].reg;
                } else if (loc[val].spill_slot >= 0) {
                    // Spilled integer operand: reload.
                    emit_spill_load(rc.code, RA_SCRATCH2, loc[val].spill_slot);
                    val_reg = RA_SCRATCH2;
                } else {
                    // String value used as int PHI — load addr and atoi.
                    rv_load_val(rc.code, 10, loc[val].addr);
                    rv_emit_atoi(rc.code, 10, phi_dest);
                    if (loc[i].spill_slot >= 0 && !loc[i].in_reg) {
                        emit_spill_store(rc.code, phi_dest, loc[i].spill_slot);
                    }
                    break;
                }
                rc.code.push_back(rv_ADD(phi_dest, val_reg, 0));
                if (loc[i].spill_slot >= 0 && !loc[i].in_reg) {
                    emit_spill_store(rc.code, phi_dest, loc[i].spill_slot);
                }
            } else {
                // String PHI: copy string to PHI's output buffer.
                if (loc[val].in_reg) {
                    // Integer val → ITOA to PHI buffer.
                    rv_load_val(rc.code, 10, loc[i].addr);
                    rv_emit_itoa(rc.code, loc[val].reg, 10);
                } else if (loc[val].spill_slot >= 0) {
                    // Spilled integer val → reload, then ITOA.
                    emit_spill_load(rc.code, RA_SCRATCH, loc[val].spill_slot);
                    rv_load_val(rc.code, 10, loc[i].addr);
                    rv_emit_itoa(rc.code, RA_SCRATCH, 10);
                } else {
                    // String → string: byte copy.
                    rv_load_val(rc.code, 7, loc[i].addr);    // t2 = dest
                    rv_load_val(rc.code, 6, loc[val].addr);  // t1 = src
                    rv_emit_strcpy(rc.code, 7, 6);
                }
            }
            break;
        }
    }
}

void hir_codegen(hir_program &h, rv_compiler &rc) {
    // Location map: where each instruction's result lives.
    hir_loc loc[HIR_MAX_INSNS];
    memset(loc, 0, sizeof(loc));
    for (int i = 0; i < h.n_insns; i++) loc[i].spill_slot = -1;

    // Block code offsets for branch backpatching.
    int block_offset[HIR_MAX_BLOCKS];
    memset(block_offset, 0, sizeof(block_offset));
    std::vector<branch_patch> patches;

    // 1. Run register allocation for integers.
    std::vector<live_interval> int_intervals;
    compute_live_ranges(h, int_intervals, needs_int_reg);
    reg_alloc_result int_alloc = linear_scan(int_intervals);

    // 2. Run liveness-based allocation for output buffers.
    std::vector<live_interval> str_intervals;
    compute_live_ranges(h, str_intervals, needs_output_buffer);
    output_alloc_result str_alloc = allocate_output_buffers(rc, str_intervals);

    // 3. Allocate 8-byte guest memory slots for FP values.
    //    Simple bump allocation — no register caching for FP in HIR.
    //    Each FP value gets a slot; operations load/store via FLD/FSD.
    //    The DBT's x86-64 translator will optimize these into XMM regs.
    uint64_t fp_pool = (rc.str_pool + 7) & ~7ULL;  // align to 8
    for (int i = 0; i < h.n_insns; i++) {
        if (needs_fp_reg(h, i)) {
            if (fp_pool + 8 > rv_compiler::STR_LIMIT) break;
            loc[i].addr = fp_pool;
            loc[i].in_reg = false;
            loc[i].spill_slot = -1;
            fp_pool += 8;
        }
    }
    rc.str_pool = fp_pool;

    // Pre-populate loc map from allocation results.
    for (int i = 0; i < h.n_insns; i++) {
        if (needs_int_reg(h, i)) {
            loc[i].reg = int_alloc.reg[i];
            loc[i].in_reg = (loc[i].reg != 0);
            loc[i].spill_slot = int_alloc.spill_slot[i];
        }
        if (needs_output_buffer(h, i)) {
            loc[i].addr = str_alloc.addr[i];
            loc[i].in_reg = false;
        }
    }

    // Process blocks in layout order.
    for (int b = 0; b < h.n_blocks; b++) {
        block_offset[b] = static_cast<int>(rc.code.size());
        if (h.block_first[b] > h.block_last[b]) continue;

        for (int i = h.block_first[b]; i <= h.block_last[b]; i++) {
            if (h.blk[i] != b) continue;

            switch (h.kind[i]) {
            case HIR_SCONST:
                loc[i].addr = static_cast<uint64_t>(h.val[i]);
                loc[i].in_reg = false;
                break;

            case HIR_ICONST: {
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rv_load_i64(rc.code, dest, h.val[i]);
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            case HIR_ATOI: {
                int s1 = h.src1[i];
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                if (h.kind[s1] == HIR_SCONST) {
                    int64_t v = static_cast<int64_t>(
                        mux_atol(u8(h.sval[s1])));
                    rv_load_i64(rc.code, dest, v);
                } else {
                    rv_load_val(rc.code, 10, loc[s1].addr);
                    rv_emit_atoi(rc.code, 10, dest);
                }
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            case HIR_STRCMP: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                if (h.kind[s1] == HIR_SCONST && h.kind[s2] == HIR_SCONST) {
                    int r = strcmp(h.sval[s1].c_str(), h.sval[s2].c_str());
                    rv_load_i64(rc.code, dest, r < 0 ? -1 : r > 0 ? 1 : 0);
                } else {
                    rv_load_val(rc.code, 10, loc[s1].addr);
                    rv_load_val(rc.code, 11, loc[s2].addr);
                    rv_emit_strcmp(rc.code, 10, 11, dest);
                }
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            case HIR_LUA_GETI: {
                // Dedicated ECALL: a0=tbl_idx, a1=key → a0=value (int64)
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                // s1 = table stack index (known_int), s2 = integer key
                uint8_t tbl_r = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                rc.code.push_back(rv_ADDI(10, tbl_r, 0));  // a0 = tbl_idx
                uint8_t key_r = ra_get_reg(rc, loc, s2, 28);  // t3 as scratch
                rc.code.push_back(rv_ADDI(11, key_r, 0));  // a1 = key
                rc.code.push_back(rv_ADDI(17, 0, static_cast<int32_t>(ECALL_LUA_GETI_INT)));
                rc.code.push_back(rv_ECALL());
                // Result in a0 (x10). Success flag in a1 (x11) — ignored for now.
                rc.code.push_back(rv_ADDI(dest, 10, 0));   // dest = a0
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            case HIR_LUA_SETI: {
                // Dedicated ECALL: a0=tbl_idx, a1=key, a2=value
                int s1 = h.src1[i], s2 = h.src2[i];
                int s3 = static_cast<int>(h.val[i]); // 3rd operand stored in val
                uint8_t tbl_r = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                rc.code.push_back(rv_ADDI(10, tbl_r, 0));  // a0 = tbl_idx
                uint8_t key_r = ra_get_reg(rc, loc, s2, 28);
                rc.code.push_back(rv_ADDI(11, key_r, 0));  // a1 = key
                uint8_t val_r = ra_get_reg(rc, loc, s3, 29);
                rc.code.push_back(rv_ADDI(12, val_r, 0));  // a2 = value
                rc.code.push_back(rv_ADDI(17, 0, static_cast<int32_t>(ECALL_LUA_SETI_INT)));
                rc.code.push_back(rv_ECALL());
                break;
            }

            case HIR_LUA_ALOAD: {
                // Native array load: result = *(int64*)(base + (key-1)*8)
                // src1 = key (TY_INT), val = guest base address
                int s1 = h.src1[i];
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                uint8_t key_r = ra_get_reg(rc, loc, s1, RA_SCRATCH2);
                uint64_t base_addr = static_cast<uint64_t>(h.val[i]);
                // t0 = key - 1 (0-based index)
                rc.code.push_back(rv_ADDI(5, key_r, -1));
                // t0 = t0 << 3 (multiply by 8)
                rc.code.push_back(rv_SLLI(5, 5, 3));
                // Load base address into t1
                rv_load_val(rc.code, 6, base_addr);
                // t0 = base + offset
                rc.code.push_back(rv_ADD(5, 5, 6));
                // dest = *(int64*)t0
                rc.code.push_back(rv_LD(dest, 5, 0));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                h.native_ops++;
                break;
            }

            case HIR_ITOA: {
                int s1 = h.src1[i];
                uint8_t s1r = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint64_t out_addr = loc[i].addr;
                rv_load_val(rc.code, 10, out_addr);
                rv_emit_itoa(rc.code, s1r, 10);
                break;
            }

            case HIR_ADD: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_ADD(dest, r1, r2));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }
            case HIR_SUB: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_SUB(dest, r1, r2));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }
            case HIR_MUL: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_MUL(dest, r1, r2));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }
            case HIR_REM: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_REM(dest, r1, r2));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }
            case HIR_DIV: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_DIV(dest, r1, r2));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            // Bitwise operations.
#define BITOP_RR(RV_INSN) \
            { \
                int s1 = h.src1[i], s2 = h.src2[i]; \
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH); \
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2); \
                uint8_t reg = int_alloc.reg[i]; \
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0); \
                uint8_t dest = spilled ? RA_SCRATCH : reg; \
                if (!dest) break; \
                rc.code.push_back(RV_INSN(dest, r1, r2)); \
                ra_set_loc(rc, loc, int_alloc, i, dest); \
                break; \
            }
            case HIR_BAND: BITOP_RR(rv_AND)
            case HIR_BOR:  BITOP_RR(rv_OR)
            case HIR_BXOR: BITOP_RR(rv_XOR)
            case HIR_SHL:  BITOP_RR(rv_SLL)
            case HIR_SHR:  BITOP_RR(rv_SRL)
#undef BITOP_RR

            case HIR_BNOT: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                // XORI rd, rs, -1 (all-ones immediate = bitwise NOT)
                rc.code.push_back(rv_i_type(OP_IMM, dest, ALU_XORI, r1, -1));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            // ABS: branchless absolute value.
            // SRA tmp, rs, 63 (sign mask: all 1s if negative, all 0s if positive)
            // XOR dest, rs, tmp
            // SUB dest, dest, tmp
            case HIR_ABS: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                constexpr uint8_t t0 = 5;  // scratch for sign mask
                // SRAI t0, r1, 63 — arithmetic shift right by 63
                rc.code.push_back(rv_i_type(OP_IMM, t0, ALU_SRLI, r1, 63)
                                  | (0x10u << 26));  // set funct6 high bit for SRAI
                // XOR dest, r1, t0
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_XOR, r1, t0, 0));
                // SUB dest, dest, t0
                rc.code.push_back(rv_SUB(dest, dest, t0));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            // SIGN: returns -1, 0, or 1.
            // SLT t0, rs, x0   (t0 = 1 if rs < 0)
            // SLT dest, x0, rs (dest = 1 if rs > 0, i.e., 0 < rs)
            // SUB dest, dest, t0
            case HIR_SIGN: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                constexpr uint8_t t0 = 5;
                rc.code.push_back(rv_r_type(OP_REG, t0, ALU_SLT, r1, 0, 0));
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLT, 0, r1, 0));
                rc.code.push_back(rv_SUB(dest, dest, t0));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            // MAX: max(a, b) — branchless via SLT + conditional select.
            // SLT t0, r1, r2   (t0 = 1 if r1 < r2)
            // BEQ t0, x0, +8   (skip if r1 >= r2, i.e., r1 is already max)
            // MV dest, r2       (r2 is larger)
            // Otherwise dest = r1.
            // Actually simpler: compute both, select.
            // SUB t0, r1, r2
            // SRA t0, t0, 63   (sign mask: all 1s if r1 < r2)
            // AND t0, t0, SUB → use the mask to select
            // Better: just branch.
            // BLT r1, r2, +12; MV dest, r1; JAL x0, +8; MV dest, r2
            case HIR_MAX: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                // BGE r1, r2, +12 (skip to dest=r1 case when r1 >= r2)
                rc.code.push_back(rv_BGE(r1, r2, 12));
                // r1 < r2: dest = r2
                rc.code.push_back(rv_ADD(dest, r2, 0));  // MV dest, r2
                rc.code.push_back(rv_JAL(0, 8));          // skip next
                // r1 >= r2: dest = r1
                rc.code.push_back(rv_ADD(dest, r1, 0));  // MV dest, r1
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            // MIN: min(a, b) — mirror of MAX.
            case HIR_MIN: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                // BLT r1, r2, +12 (skip to dest=r1 case when r1 < r2)
                rc.code.push_back(rv_b_type(BR_BLT, r1, r2, 12));
                // r1 >= r2: dest = r2
                rc.code.push_back(rv_ADD(dest, r2, 0));  // MV dest, r2
                rc.code.push_back(rv_JAL(0, 8));          // skip next
                // r1 < r2: dest = r1
                rc.code.push_back(rv_ADD(dest, r1, 0));  // MV dest, r1
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            case HIR_EQ: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_SUB(5, r1, r2));
                rc.code.push_back(rv_i_type(OP_IMM, dest, ALU_SLTIU, 5, 1));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }
            case HIR_NE: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_SUB(5, r1, r2));
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLTU, 0, 5, 0));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }
            case HIR_GT: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLT, r2, r1, 0));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }
            case HIR_LT: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLT, r1, r2, 0));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }
            case HIR_GE: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLT, r1, r2, 0));
                rc.code.push_back(rv_i_type(OP_IMM, dest, ALU_XORI, dest, 1));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }
            case HIR_LE: {
                int s1 = h.src1[i], s2 = h.src2[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t r2 = ra_get_reg(rc, loc, s2, RA_SCRATCH2);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLT, r2, r1, 0));
                rc.code.push_back(rv_i_type(OP_IMM, dest, ALU_XORI, dest, 1));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            case HIR_NOT: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_i_type(OP_IMM, dest, ALU_SLTIU, r1, 1));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            // BOOL (t function): SNEZ — set if not equal to zero.
            // SLTU dest, x0, r1 → dest = (0 < r1) unsigned = (r1 != 0)
            case HIR_BOOL: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_r_type(OP_REG, dest, ALU_SLTU, 0, r1, 0));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            case HIR_INC: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_ADDI(dest, r1, 1));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }
            case HIR_DEC: {
                int s1 = h.src1[i];
                uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH);
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_ADDI(dest, r1, -1));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            // ---- Float arithmetic (RV64D) ----
            //
            // FP values are spilled to guest memory (8-byte aligned).
            // We load into f0/f1, compute into f0, store result.
            // The DBT's x86-64 translator handles the rest.

            case HIR_FCONST: {
                // Write the double constant into guest memory at the
                // allocated FP slot, then no codegen needed — the value
                // is already there for subsequent FLD instructions.
                uint64_t addr = loc[i].addr;
                double v = h.fval[i];
                memcpy(rc.memory.data() + addr, &v, 8);
                break;
            }

#define FP_BINOP(RV_INSN) \
            { \
                uint64_t a1 = loc[h.src1[i]].addr; \
                uint64_t a2 = loc[h.src2[i]].addr; \
                uint64_t dst = loc[i].addr; \
                rv_load_val(rc.code, RA_SCRATCH, a1); \
                rc.code.push_back(rv_FLD(0, RA_SCRATCH, 0)); \
                rv_load_val(rc.code, RA_SCRATCH, a2); \
                rc.code.push_back(rv_FLD(1, RA_SCRATCH, 0)); \
                rc.code.push_back(RV_INSN(0, 0, 1)); \
                rv_load_val(rc.code, RA_SCRATCH, dst); \
                rc.code.push_back(rv_FSD(RA_SCRATCH, 0, 0)); \
                break; \
            }

            case HIR_FADD: FP_BINOP(rv_FADD_D)
            case HIR_FSUB: FP_BINOP(rv_FSUB_D)
            case HIR_FMUL: FP_BINOP(rv_FMUL_D)
            case HIR_FDIV: FP_BINOP(rv_FDIV_D)
#undef FP_BINOP

            case HIR_FNEG: {
                uint64_t a1 = loc[h.src1[i]].addr;
                uint64_t dst = loc[i].addr;
                rv_load_val(rc.code, RA_SCRATCH, a1);
                rc.code.push_back(rv_FLD(0, RA_SCRATCH, 0));
                rc.code.push_back(rv_FNEG_D(0, 0));
                rv_load_val(rc.code, RA_SCRATCH, dst);
                rc.code.push_back(rv_FSD(RA_SCRATCH, 0, 0));
                break;
            }

            case HIR_FSQRT: {
                uint64_t a1 = loc[h.src1[i]].addr;
                uint64_t dst = loc[i].addr;
                rv_load_val(rc.code, RA_SCRATCH, a1);
                rc.code.push_back(rv_FLD(0, RA_SCRATCH, 0));
                rc.code.push_back(rv_FSQRT_D(0, 0));
                rv_load_val(rc.code, RA_SCRATCH, dst);
                rc.code.push_back(rv_FSD(RA_SCRATCH, 0, 0));
                break;
            }

            // ITOF: int64 → double.  Load int reg, FCVT.D.L, store to FP slot.
            case HIR_ITOF: {
                uint8_t r1 = ra_get_reg(rc, loc, h.src1[i], RA_SCRATCH);
                uint64_t dst = loc[i].addr;
                rc.code.push_back(rv_FCVT_D_L(0, r1));
                rv_load_val(rc.code, RA_SCRATCH, dst);
                rc.code.push_back(rv_FSD(RA_SCRATCH, 0, 0));
                break;
            }

            // FTOI: double → int64 (truncate toward zero).
            case HIR_FTOI: {
                uint64_t a1 = loc[h.src1[i]].addr;
                rv_load_val(rc.code, RA_SCRATCH, a1);
                rc.code.push_back(rv_FLD(0, RA_SCRATCH, 0));
                uint8_t reg = int_alloc.reg[i];
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                uint8_t dest = spilled ? RA_SCRATCH : reg;
                if (!dest) break;
                rc.code.push_back(rv_FCVT_L_D(dest, 0));
                ra_set_loc(rc, loc, int_alloc, i, dest);
                break;
            }

            // FTOA: double → string.  Use ECALL to format.
            case HIR_FTOA: {
                uint64_t a1 = loc[h.src1[i]].addr;
                uint64_t out_addr = loc[i].addr;
                // Load double bits into a0 via FMV.X.D.
                rv_load_val(rc.code, RA_SCRATCH, a1);
                rc.code.push_back(rv_FLD(0, RA_SCRATCH, 0));
                rc.code.push_back(rv_FMV_X_D(10, 0));  // a0 = double bits
                rv_load_val(rc.code, 11, out_addr);     // a1 = output buffer
                rv_load_val(rc.code, 17, 0x140);        // a7 = ECALL_FTOA
                rc.code.push_back(rv_ECALL());
                break;
            }

            // Float comparisons: result is integer 0/1.
#define FP_CMP(RV_INSN) \
            { \
                uint64_t a1 = loc[h.src1[i]].addr; \
                uint64_t a2 = loc[h.src2[i]].addr; \
                rv_load_val(rc.code, RA_SCRATCH, a1); \
                rc.code.push_back(rv_FLD(0, RA_SCRATCH, 0)); \
                rv_load_val(rc.code, RA_SCRATCH, a2); \
                rc.code.push_back(rv_FLD(1, RA_SCRATCH, 0)); \
                uint8_t reg = int_alloc.reg[i]; \
                bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0); \
                uint8_t dest = spilled ? RA_SCRATCH : reg; \
                if (!dest) break; \
                rc.code.push_back(RV_INSN(dest, 0, 1)); \
                ra_set_loc(rc, loc, int_alloc, i, dest); \
                break; \
            }

            case HIR_FEQ: FP_CMP(rv_FEQ_D)
            case HIR_FLT: FP_CMP(rv_FLT_D)
            case HIR_FLE: FP_CMP(rv_FLE_D)
#undef FP_CMP

            case HIR_CALL: {
                uint64_t out_addr = loc[i].addr;
                int na = h.cnargs[i];
                int base = h.cbase[i];
                std::vector<uint64_t> farg_addrs;
                for (int j = 0; j < na; j++) {
                    int ai = h.carg[base + j];
                    farg_addrs.push_back(loc[ai].addr);
                }
                uint64_t fargs_addr = rc.alloc_fargs(farg_addrs);

                if (h.tier2_addr[i]) {
                    // Tier 2: JAL to pre-compiled blob function.
                    rv_emit_tier2_call(rc.code, fargs_addr, na,
                                        out_addr, h.tier2_addr[i]);
                } else {
                    // ECALL to engine function.
                    int fidx = h.func_idx[i];
                    uint64_t name_addr = 0;
                    if (fidx == 0 && !h.call_name[i].empty()) {
                        name_addr = rc.pool_str(h.call_name[i]);
                    }
                    rv_emit_call(rc.code, name_addr, fargs_addr, na,
                                  out_addr, rv_compiler::OUT_SLOT, fidx);
                }
                break;
            }

            case HIR_STRCAT: {
                uint64_t out_addr = loc[i].addr;
                int na = h.cnargs[i];
                int base = h.cbase[i];
                std::vector<uint64_t> farg_addrs;
                for (int j = 0; j < na; j++) {
                    int ai = h.carg[base + j];
                    farg_addrs.push_back(loc[ai].addr);
                }
                uint64_t fargs_addr = rc.alloc_fargs(farg_addrs);
                uint64_t t2addr = tier2_lookup("STRCAT");
                if (t2addr) {
                    rv_emit_tier2_call(rc.code, fargs_addr, na,
                                        out_addr, t2addr);
                } else {
                    int fidx = h.func_idx[i];
                    uint64_t name_addr = fidx ? 0 : rc.pool_str("strcat");
                    rv_emit_call(rc.code, name_addr, fargs_addr, na,
                                  out_addr, rv_compiler::OUT_SLOT, fidx);
                }
                break;
            }

            case HIR_COPY: {
                int s1 = h.src1[i];
                if (s1 < 0) break;
                if (needs_int_reg(h, i)) {
                    uint8_t r1 = ra_get_reg(rc, loc, s1, RA_SCRATCH2);
                    uint8_t reg = int_alloc.reg[i];
                    bool spilled = (reg == 0 && int_alloc.spill_slot[i] >= 0);
                    uint8_t dest = spilled ? RA_SCRATCH : reg;
                    if (!dest) { loc[i] = loc[s1]; break; }
                    rc.code.push_back(rv_ADD(dest, r1, 0));
                    ra_set_loc(rc, loc, int_alloc, i, dest);
                } else {
                    loc[i] = loc[s1];
                }
                break;
            }

            case HIR_PHI:
                // Location already allocated above.
                break;

            case HIR_BRC: {
                // Conditional branch: if cond != 0, go to true_blk.
                int cond_insn = h.src1[i];
                int true_blk = static_cast<int>(h.val[i]);
                int false_blk = h.src2[i];

                uint8_t cond_reg = ra_get_reg(rc, loc, cond_insn, RA_SCRATCH);

                // Emit PHI copies for true path, then BNE.
                emit_phi_copies(h, rc, loc, b, true_blk);
                int bne_idx = static_cast<int>(rc.code.size());
                rc.code.push_back(rv_BNE(cond_reg, 0, 0));
                patches.push_back({bne_idx, true_blk});

                // Emit PHI copies for false path.
                emit_phi_copies(h, rc, loc, b, false_blk);

                // If false block is not the next in layout, emit JAL.
                if (false_blk != b + 1) {
                    int jal_idx = static_cast<int>(rc.code.size());
                    rc.code.push_back(rv_JAL(0, 0));
                    patches.push_back({jal_idx, false_blk});
                }
                break;
            }

            case HIR_BR: {
                int target = static_cast<int>(h.val[i]);

                // Emit PHI copies for target.
                emit_phi_copies(h, rc, loc, b, target);

                // If target is not the next block, emit JAL.
                if (target != b + 1) {
                    int jal_idx = static_cast<int>(rc.code.size());
                    rc.code.push_back(rv_JAL(0, 0));
                    patches.push_back({jal_idx, target});
                }
                break;
            }

            case HIR_RET:
                rv_emit_exit(rc.code);
                break;

            case HIR_SETQ_SYNC: {
                // Emit ECALL_SETQ_PACK: a0 = reg_num, a1 = value_addr, a2 = length.
                // We pass 0 for length to tell the host to use strlen() for now.
                int regnum = static_cast<int>(h.val[i]);
                int val_idx = h.src1[i];
                rc.code.push_back(rv_ADDI(17, 0, 0x130));  // a7 = ECALL_SETQ_PACK
                rv_load_val(rc.code, 10, static_cast<uint64_t>(regnum));  // a0 = regnum
                if (val_idx >= 0) {
                    rv_load_val(rc.code, 11, loc[val_idx].addr);  // a1 = value addr
                } else {
                    rv_load_val(rc.code, 11, 0);
                }
                rv_load_val(rc.code, 12, 0);  // a2 = 0 (use strlen)
                rc.code.push_back(rv_ECALL());
                break;
            }

            case HIR_NOP:
            case HIR_STORE_Q:  // consumed by SSA construction
            case HIR_LOAD_Q:   // should be COPY after SSA; harmless NOP
                break;

            default:
                break;
            }
        }
    }

    // Backpatch branch offsets.
    for (auto &p : patches) {
        int target_off = block_offset[p.target_blk];
        int branch_off = p.code_idx;
        int32_t rel = static_cast<int32_t>((target_off - branch_off) * 4);
        uint32_t insn = rc.code[branch_off];
        uint8_t opcode = insn & 0x7F;
        if (opcode == OP_BRANCH) {
            // B-type: re-encode with correct offset.
            uint8_t funct3 = (insn >> 12) & 7;
            uint8_t rs1 = (insn >> 15) & 0x1F;
            uint8_t rs2 = (insn >> 20) & 0x1F;
            rc.code[branch_off] = rv_b_type(funct3, rs1, rs2, rel);
        } else if (opcode == OP_JAL) {
            // J-type: re-encode with correct offset.
            uint8_t rd = (insn >> 7) & 0x1F;
            rc.code[branch_off] = rv_JAL(rd, rel);
        }
    }

    // Set the result location in the rv_compiler.
    int ri = h.result;
    if (ri >= 0) {
        if (loc[ri].in_reg) {
            // Final result is in a register — need ITOA.
            uint64_t out_addr = rc.alloc_output();
            rv_load_val(rc.code, 10, out_addr);
            rv_emit_itoa(rc.code, loc[ri].reg, 10);
            rc.final_out = out_addr;
        } else if (loc[ri].spill_slot >= 0) {
            // Final result is spilled — reload and ITOA.
            uint64_t out_addr = rc.alloc_output();
            emit_spill_load(rc.code, RA_SCRATCH, loc[ri].spill_slot);
            rv_load_val(rc.code, 10, out_addr);
            rv_emit_itoa(rc.code, RA_SCRATCH, 10);
            rc.final_out = out_addr;
        } else {
            rc.final_out = loc[ri].addr;
        }
    }

    // Emit exit.
    rv_emit_exit(rc.code);
}

const char *hir_kind_name(hir_kind k) {
    switch (k) {
    case HIR_NOP:        return "NOP";
    case HIR_ICONST:     return "ICONST";
    case HIR_SCONST:     return "SCONST";
    case HIR_ADD:        return "ADD";
    case HIR_SUB:        return "SUB";
    case HIR_MUL:        return "MUL";
    case HIR_DIV:        return "DIV";
    case HIR_REM:        return "REM";
    case HIR_NEG:        return "NEG";
    case HIR_ABS:        return "ABS";
    case HIR_SIGN:       return "SIGN";
    case HIR_MAX:        return "MAX";
    case HIR_MIN:        return "MIN";
    case HIR_BAND:       return "BAND";
    case HIR_BOR:        return "BOR";
    case HIR_BXOR:       return "BXOR";
    case HIR_BNOT:       return "BNOT";
    case HIR_SHL:        return "SHL";
    case HIR_SHR:        return "SHR";
    case HIR_EQ:         return "EQ";
    case HIR_NE:         return "NE";
    case HIR_LT:         return "LT";
    case HIR_LE:         return "LE";
    case HIR_GT:         return "GT";
    case HIR_GE:         return "GE";
    case HIR_NOT:        return "NOT";
    case HIR_BOOL:       return "BOOL";
    case HIR_INC:        return "INC";
    case HIR_DEC:        return "DEC";
    case HIR_ATOI:       return "ATOI";
    case HIR_STRCMP:      return "STRCMP";
    case HIR_LUA_GETI:   return "LUA_GETI";
    case HIR_LUA_SETI:   return "LUA_SETI";
    case HIR_LUA_ALOAD:  return "LUA_ALOAD";
    case HIR_ITOA:       return "ITOA";
    case HIR_ITOF:       return "ITOF";
    case HIR_FTOI:       return "FTOI";
    case HIR_FTOA:       return "FTOA";
    case HIR_ATOF:       return "ATOF";
    case HIR_FCONST:     return "FCONST";
    case HIR_FADD:       return "FADD";
    case HIR_FSUB:       return "FSUB";
    case HIR_FMUL:       return "FMUL";
    case HIR_FDIV:       return "FDIV";
    case HIR_FNEG:       return "FNEG";
    case HIR_FSQRT:      return "FSQRT";
    case HIR_FEQ:        return "FEQ";
    case HIR_FLT:        return "FLT";
    case HIR_FLE:        return "FLE";
    case HIR_CALL:       return "CALL";
    case HIR_STRCAT:     return "STRCAT";
    case HIR_RET:        return "RET";
    case HIR_COPY:       return "COPY";
    case HIR_PHI:        return "PHI";
    case HIR_LOAD_Q:     return "LOAD_Q";
    case HIR_STORE_Q:    return "STORE_Q";
    case HIR_SETQ_SYNC:  return "SETQ_SYNC";
    case HIR_BR:         return "BR";
    case HIR_BRC:        return "BRC";
    default:             return "UNKNOWN";
    }
}

static const char *hir_type_name(hir_type t) {
    switch (t) {
    case TY_VOID:   return "void";
    case TY_INT:    return "int";
    case TY_FLOAT:  return "flt";
    case TY_STRING: return "str";
    default:        return "???";
    }
}

void hir_dump(const hir_program &h) {
    printf("HIR Program: %d instructions, %d blocks\n", h.n_insns, h.n_blocks);
    printf("Result: v%d\n", h.result);

    for (int b = 0; b < h.n_blocks; b++) {
        printf("\nBLOCK %d:\n", b);
        printf("  Range: [%d, %d]\n", h.block_first[b], h.block_last[b]);
        printf("  Preds: ");
        for (int i = 0; i < h.n_pred[b]; i++) {
            printf("%d ", h.pblk[h.pred_base[b] + i]);
        }
        printf("\n  Succs: ");
        for (int i = 0; i < h.block_nsucc[b]; i++) {
            printf("%d ", h.block_succ[b][i]);
        }
        printf("\n  IDom:  %d\n", h.idom[b]);

        if (h.block_first[b] <= h.block_last[b]) {
            for (int i = h.block_first[b]; i <= h.block_last[b]; i++) {
                if (h.blk[i] != b) continue;
                printf("  v%-3d = %-10s %-4s", i, hir_kind_name(h.kind[i]), hir_type_name(h.ty[i]));

                if (h.kind[i] == HIR_ICONST) {
                    printf(" %lld", (long long)h.val[i]);
                } else if (h.kind[i] == HIR_SCONST) {
                    printf(" \"%s\" (0x%llX)", h.sval[i].c_str(), (unsigned long long)h.val[i]);
                } else if (h.kind[i] == HIR_BR) {
                    printf(" -> BLOCK %d", (int)h.val[i]);
                } else if (h.kind[i] == HIR_BRC) {
                    printf(" v%d ? -> BLOCK %d : BLOCK %d", h.src1[i], (int)h.val[i], h.src2[i]);
                } else if (h.kind[i] == HIR_PHI) {
                    printf(" Q%d { ", (int)h.val[i]);
                    for (int j = 0; j < h.pnargs[i]; j++) {
                        printf("B%d:v%d ", h.pblk[h.pbase[i] + j], h.pval[h.pbase[i] + j]);
                    }
                    printf("}");
                } else if (h.kind[i] == HIR_CALL || h.kind[i] == HIR_STRCAT) {
                    if (!h.call_name[i].empty()) printf(" %s", h.call_name[i].c_str());
                    printf(" ( ");
                    for (int j = 0; j < h.cnargs[i]; j++) {
                        printf("v%d ", h.carg[h.cbase[i] + j]);
                    }
                    printf(")");
                    if (h.tier2_addr[i]) printf(" [T2:0x%llX]", (unsigned long long)h.tier2_addr[i]);
                } else {
                    if (h.src1[i] >= 0) printf(" v%d", h.src1[i]);
                    if (h.src2[i] >= 0) printf(", v%d", h.src2[i]);
                    if (h.val[i] != 0 && h.kind[i] != HIR_COPY) printf(" imm:%lld", (long long)h.val[i]);
                }
                printf("\n");
            }
        }
    }
    printf("--- end dump ---\n");
}
