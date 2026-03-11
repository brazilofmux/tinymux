/*! \file dbt_decoder.h
 * \brief RV64IMD instruction decoder.
 *
 * Inline decoder for the RISC-V RV64IMD instruction set:
 *   I — Base integer (64-bit)
 *   M — Multiply/divide
 *   D — Double-precision floating point
 *
 * Reference: ~/riscv/dbt/decoder.h (RV32IMFD version).
 * Key difference: 64-bit register width, W-suffix instructions,
 * 6-bit shift amounts, LD/SD/LWU, FCVT.L.D/FCVT.D.L, FMV.X.D/FMV.D.X.
 */

#ifndef DBT_DECODER_H
#define DBT_DECODER_H

#include <cstdint>

// Opcodes (bits [6:0])
//
constexpr uint8_t OP_LUI       = 0x37;
constexpr uint8_t OP_AUIPC     = 0x17;
constexpr uint8_t OP_JAL       = 0x6F;
constexpr uint8_t OP_JALR      = 0x67;
constexpr uint8_t OP_BRANCH    = 0x63;
constexpr uint8_t OP_LOAD      = 0x03;
constexpr uint8_t OP_STORE     = 0x23;
constexpr uint8_t OP_IMM       = 0x13;
constexpr uint8_t OP_REG       = 0x33;
constexpr uint8_t OP_IMM32     = 0x1B;  // RV64: ADDIW, SLLIW, SRLIW, SRAIW
constexpr uint8_t OP_REG32     = 0x3B;  // RV64: ADDW, SUBW, SLLW, SRLW, SRAW, M*W
constexpr uint8_t OP_FENCE     = 0x0F;
constexpr uint8_t OP_SYSTEM    = 0x73;

// RV64D opcodes
//
constexpr uint8_t OP_FP_LOAD   = 0x07;  // FLD (funct3=3)
constexpr uint8_t OP_FP_STORE  = 0x27;  // FSD (funct3=3)
constexpr uint8_t OP_FMADD     = 0x43;
constexpr uint8_t OP_FMSUB     = 0x47;
constexpr uint8_t OP_FNMSUB    = 0x4B;
constexpr uint8_t OP_FNMADD    = 0x4F;
constexpr uint8_t OP_FP        = 0x53;  // arithmetic, compare, convert, move

// Branch funct3
//
constexpr uint8_t BR_BEQ  = 0;
constexpr uint8_t BR_BNE  = 1;
constexpr uint8_t BR_BLT  = 4;
constexpr uint8_t BR_BGE  = 5;
constexpr uint8_t BR_BLTU = 6;
constexpr uint8_t BR_BGEU = 7;

// Load funct3
//
constexpr uint8_t LD_LB  = 0;
constexpr uint8_t LD_LH  = 1;
constexpr uint8_t LD_LW  = 2;
constexpr uint8_t LD_LD  = 3;  // RV64: load doubleword
constexpr uint8_t LD_LBU = 4;
constexpr uint8_t LD_LHU = 5;
constexpr uint8_t LD_LWU = 6;  // RV64: load word unsigned

// Store funct3
//
constexpr uint8_t ST_SB = 0;
constexpr uint8_t ST_SH = 1;
constexpr uint8_t ST_SW = 2;
constexpr uint8_t ST_SD = 3;  // RV64: store doubleword

// ALU immediate funct3
//
constexpr uint8_t ALU_ADDI  = 0;
constexpr uint8_t ALU_SLTI  = 2;
constexpr uint8_t ALU_SLTIU = 3;
constexpr uint8_t ALU_XORI  = 4;
constexpr uint8_t ALU_ORI   = 6;
constexpr uint8_t ALU_ANDI  = 7;
constexpr uint8_t ALU_SLLI  = 1;
constexpr uint8_t ALU_SRLI  = 5;  // also SRAI when funct6=0x10

// ALU register funct3 (+ funct7 for disambiguation)
//
constexpr uint8_t ALU_ADD  = 0;  // SUB when funct7=0x20, MUL when funct7=0x01
constexpr uint8_t ALU_SLL  = 1;  // MULH when funct7=0x01
constexpr uint8_t ALU_SLT  = 2;  // MULHSU when funct7=0x01
constexpr uint8_t ALU_SLTU = 3;  // MULHU when funct7=0x01
constexpr uint8_t ALU_XOR  = 4;  // DIV when funct7=0x01
constexpr uint8_t ALU_SRL  = 5;  // SRA when funct7=0x20, DIVU when funct7=0x01
constexpr uint8_t ALU_OR   = 6;  // REM when funct7=0x01
constexpr uint8_t ALU_AND  = 7;  // REMU when funct7=0x01

// FP funct5 (funct7 >> 2)
//
constexpr uint8_t FP_FADD   = 0x00;
constexpr uint8_t FP_FSUB   = 0x01;
constexpr uint8_t FP_FMUL   = 0x02;
constexpr uint8_t FP_FDIV   = 0x03;
constexpr uint8_t FP_FSQRT  = 0x0B;
constexpr uint8_t FP_FSGNJ  = 0x04;
constexpr uint8_t FP_FMINMAX = 0x05;
constexpr uint8_t FP_FCMP   = 0x14;
constexpr uint8_t FP_FCVTW  = 0x18;  // FCVT.W.D, FCVT.WU.D, FCVT.L.D, FCVT.LU.D
constexpr uint8_t FP_FCVTDW = 0x1A;  // FCVT.D.W, FCVT.D.WU, FCVT.D.L, FCVT.D.LU
constexpr uint8_t FP_FCLASS = 0x1C;  // FCLASS.D, FMV.X.D
constexpr uint8_t FP_FMVDX  = 0x1E;  // FMV.D.X

// FP format (funct7 & 3): 1 = double
//
constexpr uint8_t FP_FMT_D = 1;

// Decoded instruction
//
struct rv64_insn_t {
    uint8_t  opcode;   // bits [6:0]
    uint8_t  rd;       // destination register
    uint8_t  rs1;      // source register 1
    uint8_t  rs2;      // source register 2
    uint8_t  rs3;      // source register 3 (R4-type: FMA) = funct7[6:2]
    uint8_t  funct3;   // function code (bits [14:12])
    uint8_t  funct7;   // function code (bits [31:25])
    int32_t  imm;      // decoded immediate (sign-extended to 32-bit;
                        // widen to int64_t when applied to 64-bit regs)
};

// Decode a 32-bit RV64IMD instruction word.
// Returns the instruction length (always 4).
//
static inline int rv64_decode(uint32_t word, rv64_insn_t *insn) {
    insn->opcode = word & 0x7F;
    insn->rd     = (word >> 7) & 0x1F;
    insn->funct3 = (word >> 12) & 0x07;
    insn->rs1    = (word >> 15) & 0x1F;
    insn->rs2    = (word >> 20) & 0x1F;
    insn->rs3    = (word >> 27) & 0x1F;
    insn->funct7 = (word >> 25) & 0x7F;

    // Decode immediate based on instruction format.
    //
    switch (insn->opcode) {
    case OP_LUI:
    case OP_AUIPC:
        // U-type: imm[31:12]
        //
        insn->imm = static_cast<int32_t>(word & 0xFFFFF000);
        break;

    case OP_JAL:
        // J-type: imm[20|10:1|11|19:12]
        //
        insn->imm = static_cast<int32_t>(
            ((word >> 31) ? 0xFFF00000 : 0) |
            (word & 0x000FF000) |
            ((word >> 9) & 0x00000800) |
            ((word >> 20) & 0x000007FE)
        );
        break;

    case OP_BRANCH:
        // B-type: imm[12|10:5|4:1|11]
        //
        insn->imm = static_cast<int32_t>(
            ((word >> 31) ? 0xFFFFF000 : 0) |
            ((word << 4) & 0x00000800) |
            ((word >> 20) & 0x000007E0) |
            ((word >> 7) & 0x0000001E)
        );
        break;

    case OP_STORE:
    case OP_FP_STORE:
        // S-type: imm[11:5|4:0]
        //
        insn->imm = static_cast<int32_t>(
            ((word >> 31) ? 0xFFFFF000 : 0) |
            ((word >> 20) & 0xFE0) |
            ((word >> 7) & 0x1F)
        );
        break;

    case OP_FMADD: case OP_FMSUB: case OP_FNMSUB: case OP_FNMADD:
    case OP_FP:
        // R/R4-type: no immediate
        //
        insn->imm = 0;
        break;

    default:
        // I-type: imm[11:0] (covers LOAD, FP_LOAD, IMM, IMM32, JALR, SYSTEM)
        //
        insn->imm = static_cast<int32_t>(word) >> 20;
        break;
    }

    return 4;
}

#endif // DBT_DECODER_H
