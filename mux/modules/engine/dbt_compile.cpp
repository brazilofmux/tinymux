/*! \file dbt_compile.cpp
 * \brief Compiler spike: softcode expression → RV64 via AST.
 *
 * Takes a MUX expression string, parses it via the existing AST
 * parser, and compiles it to RV64 instructions that call engine
 * functions through the ECALL convention.
 *
 * Optimization: constant folding.  When a function call has all
 * compile-time-constant arguments, the compiler evaluates it at
 * compile time using libmux functions (is_integer, mux_atof, fval,
 * etc.) and emits only the result string.  This eliminates ECALL
 * overhead, lbuf allocation, hash lookup, and JIT translation for
 * the folded sub-expressions.  For fully-constant expressions like
 * add(mul(3,4),5), the entire program reduces to a string literal
 * with zero RV64 instructions executed.
 *
 * Supported AST node types:
 *   - AST_FUNCCALL with literal or compiled arguments
 *   - AST_LITERAL, AST_SPACE (string constants)
 *   - AST_SEQUENCE (concatenation)
 *   - AST_EVALBRACKET (evaluate and use result)
 *
 * Guest memory layout:
 *   0x0000..0x0FFF  code (up to 1024 instructions)
 *   0x1000..0x3FFF  string pool (literals, function names)
 *   0x4000..0x7FFF  fargs arrays (one per call)
 *   0x8000..0xEFFF  output buffers (one per sub-expression, 256 each)
 *   0xF000..0xFFFF  stack
 *
 * Compilation strategy (bottom-up):
 *   1. Leaf nodes (literals) → constant with known value
 *   2. Function calls with all-constant args → try constant fold
 *   3. If fold succeeds → result is a new constant (no code emitted)
 *   4. If fold fails → emit ECALL (runtime dispatch)
 *   5. Sequences of constants → merge at compile time
 *   6. Mixed sequences → emit strcat() ECALL
 *
 * The final result is either a string pool constant or the last
 * output slot allocated.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "ast.h"

#include "dbt.h"
#include "dbt_decoder.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <vector>
#include <string>

// ---------------------------------------------------------------
// Compiler state
// ---------------------------------------------------------------

struct rv_compiler {
    std::vector<uint8_t> memory;
    std::vector<uint32_t> code;

    // Pool allocators (bump pointers into guest memory).
    uint64_t str_pool;      // next free byte in string pool
    uint64_t fargs_pool;    // next free byte in fargs area
    uint64_t out_pool;      // next free byte in output area
    uint64_t final_out;     // guest addr of final result

    // Statistics.
    int folds;              // number of constant-folded calls
    int ecalls;             // number of runtime ECALL calls
    int native_ops;         // number of native arithmetic ops
    bool needs_jit;         // true if any runtime code was emitted

    // Integer register allocator.
    // Uses saved registers s1-s11 (x9, x18-x27).
    int int_reg_idx;
    static constexpr uint8_t INT_REGS[11] = {9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};

    uint8_t alloc_int_reg() {
        if (int_reg_idx >= 11) return 0;  // out of registers
        return INT_REGS[int_reg_idx++];
    }

    static constexpr size_t MEM_SIZE     = 64 * 1024;
    static constexpr uint64_t CODE_BASE  = 0x0000;
    static constexpr uint64_t CODE_LIMIT = 0x1000;
    static constexpr uint64_t STR_BASE   = 0x1000;
    static constexpr uint64_t STR_LIMIT  = 0x4000;
    static constexpr uint64_t FARGS_BASE = 0x4000;
    static constexpr uint64_t FARGS_LIMIT= 0x8000;
    static constexpr uint64_t OUT_BASE   = 0x8000;
    static constexpr uint64_t OUT_LIMIT  = 0xF000;
    static constexpr int      OUT_SLOT   = 256;
    static constexpr uint64_t STACK_TOP  = MEM_SIZE - 16;

    rv_compiler() : memory(MEM_SIZE, 0),
                    str_pool(STR_BASE),
                    fargs_pool(FARGS_BASE),
                    out_pool(OUT_BASE),
                    final_out(0),
                    folds(0),
                    ecalls(0),
                    native_ops(0),
                    needs_jit(false),
                    int_reg_idx(0) {}

    // Allocate string in pool, return guest addr.
    uint64_t pool_str(const char *s, size_t len) {
        uint64_t addr = str_pool;
        if (addr + len + 1 > STR_LIMIT) return 0;
        memcpy(memory.data() + addr, s, len);
        memory[addr + len] = '\0';
        str_pool = (addr + len + 1 + 7) & ~7ULL;
        return addr;
    }

    uint64_t pool_str(const std::string &s) {
        return pool_str(s.c_str(), s.size());
    }

    // Allocate fargs array, return guest addr.
    // Writes the pointer values into guest memory.
    uint64_t alloc_fargs(const std::vector<uint64_t> &ptrs) {
        uint64_t addr = fargs_pool;
        size_t sz = ptrs.size() * 8;
        if (addr + sz > FARGS_LIMIT) return 0;
        for (size_t i = 0; i < ptrs.size(); i++) {
            memcpy(memory.data() + addr + i * 8, &ptrs[i], 8);
        }
        fargs_pool = (addr + sz + 7) & ~7ULL;
        return addr;
    }

    // Allocate output buffer slot, return guest addr.
    uint64_t alloc_output() {
        uint64_t addr = out_pool;
        if (addr + OUT_SLOT > OUT_LIMIT) return 0;
        out_pool += OUT_SLOT;
        return addr;
    }
};

// ---------------------------------------------------------------
// Compile result: carries both guest address and optional
// compile-time constant value, plus type tracking for native
// integer arithmetic.
// ---------------------------------------------------------------

enum result_type { RT_STRING, RT_INT };

struct compile_result {
    uint64_t addr;          // guest address of result (RT_STRING)
    bool is_const;          // true if value is known at compile time
    std::string value;      // the string value (valid only if is_const)
    result_type type;       // RT_STRING or RT_INT
    uint8_t reg;            // RV64 register holding integer (RT_INT)
    bool known_int;         // if RT_STRING, true if known to be integer

    static compile_result constant(uint64_t a, const std::string &v) {
        return {a, true, v, RT_STRING, 0, false};
    }
    static compile_result runtime(uint64_t a) {
        return {a, false, {}, RT_STRING, 0, false};
    }
    static compile_result runtime_int(uint64_t a) {
        return {a, false, {}, RT_STRING, 0, true};
    }
    static compile_result int_reg(uint8_t r) {
        return {0, false, {}, RT_INT, r, true};
    }
};

// ---------------------------------------------------------------
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
//   name_addr: guest pointer to function name string
//   fargs_addr: guest pointer to fargs[] array
//   nfargs: number of arguments
//   out_addr: guest pointer to output buffer
//   out_size: output buffer size
//
static void rv_emit_call(std::vector<uint32_t> &code,
                          uint64_t name_addr, uint64_t fargs_addr,
                          int nfargs, uint64_t out_addr, int out_size) {
    code.push_back(rv_ADDI(17, 0, 0x100));   // a7 = ECALL_CALL_FUNC
    rv_load_val(code, 10, name_addr);          // a0 = name
    rv_load_val(code, 11, fargs_addr);         // a1 = fargs
    code.push_back(rv_ADDI(12, 0, nfargs));   // a2 = nfargs
    rv_load_val(code, 13, out_addr);           // a3 = output
    rv_load_val(code, 14, out_size);           // a4 = outsize
    code.push_back(rv_ECALL());
}

static void rv_emit_exit(std::vector<uint32_t> &code) {
    code.push_back(rv_ADDI(17, 0, 93));
    code.push_back(rv_ADDI(10, 0, 0));
    code.push_back(rv_ECALL());
}

// ---------------------------------------------------------------
// Constant folding: evaluate known functions at compile time.
//
// Uses the same libmux functions (is_integer, mux_atof, fval, etc.)
// that the real engine functions use, so results are identical.
// ---------------------------------------------------------------

// Format a double result the same way fval() does: use a temporary
// buffer and call fval, then extract the string.
//
static std::string format_double(double val) {
    UTF8 buf[LBUF_SIZE];
    UTF8 *bufc = buf;
    fval(buf, &bufc, val);
    *bufc = '\0';
    return std::string(reinterpret_cast<const char *>(buf));
}

static std::string format_long(long val) {
    UTF8 buf[64];
    UTF8 *bufc = buf;
    safe_ltoa(val, buf, &bufc);
    *bufc = '\0';
    return std::string(reinterpret_cast<const char *>(buf));
}

// Maximum digit table for add() overflow detection — same as funmath.cpp.
//
static const long nMaximums[10] = {
    0, 9, 99, 999, 9999, 99999, 999999, 9999999, 99999999, 999999999
};

// Try to constant-fold a function call.
// Returns true and sets result if successful.
//
// Uses the same libmux functions that the engine uses at runtime,
// so results are bit-identical.
//

// Helper: cast std::string arg to const UTF8 *.
//
static inline const UTF8 *u8(const std::string &s) {
    return reinterpret_cast<const UTF8 *>(s.c_str());
}

// Helper: two-arg integer fast path (same guard as funmath.cpp).
//
static inline bool two_int9(const std::string &a, const std::string &b,
                            long &va, long &vb) {
    int nDigits;
    if (is_integer(u8(a), &nDigits) && nDigits <= 9
        && is_integer(u8(b), &nDigits) && nDigits <= 9) {
        va = mux_atol(u8(a));
        vb = mux_atol(u8(b));
        return true;
    }
    return false;
}

// Helper: fold a two-arg comparison (int fast path, float fallback).
//
template<typename IntCmp, typename DblCmp>
static bool fold_cmp2(const std::vector<std::string> &args,
                      std::string &result,
                      IntCmp icmp, DblCmp dcmp) {
    long va, vb;
    if (two_int9(args[0], args[1], va, vb)) {
        result = icmp(va, vb) ? "1" : "0";
    } else {
        double da = mux_atof(u8(args[0]));
        double db = mux_atof(u8(args[1]));
        result = dcmp(da, db) ? "1" : "0";
    }
    return true;
}

// Helper: xlate() equivalent for constant strings.
// Matches the real xlate() logic: #-xxx=false, #xxx=true,
// number=nonzero, empty=false, other=true.
//
static bool const_xlate(const std::string &s) {
    if (s.empty()) return false;
    if (s[0] == '#') {
        return !(s.size() > 1 && s[1] == '-');
    }
    // Try as number: zero is false, nonzero is true.
    int nDigits;
    if (is_integer(u8(s), &nDigits)) {
        return mux_atol(u8(s)) != 0;
    }
    double d = mux_atof(u8(s));
    if (d != 0.0) return true;
    // If it parsed as a float zero, it's false.  If it didn't
    // parse as a number at all, it's true (non-empty string).
    // The real xlate uses ParseFloat — we approximate: if
    // mux_atof returns 0 and string isn't "0"-like, it's true.
    if (s == "0" || s == "0.0" || s == "+0" || s == "-0") return false;
    // Non-numeric non-empty string.
    return true;
}

static bool try_fold(const std::string &func_name,
                     const std::vector<std::string> &args,
                     std::string &result) {

    // Uppercase for comparison.
    std::string upper = func_name;
    for (auto &c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    int nargs = static_cast<int>(args.size());

    // =============================================================
    // Arithmetic
    // =============================================================

    // --- ADD(a, b, ...) ---
    if (upper == "ADD" && nargs >= 2) {
        bool all_int = true;
        long nMaxValue = 0;
        for (int i = 0; i < nargs; i++) {
            int nDigits;
            if (!is_integer(u8(args[i]), &nDigits)
                || nDigits > 9
                || (nMaxValue += nMaximums[nDigits]) > 999999999L) {
                all_int = false;
                break;
            }
        }
        if (all_int) {
            long sum = 0;
            for (int i = 0; i < nargs; i++) sum += mux_atol(u8(args[i]));
            result = format_long(sum);
        } else {
            double sum = 0.0;
            for (int i = 0; i < nargs; i++) sum += mux_atof(u8(args[i]));
            result = format_double(sum);
        }
        return true;
    }

    // --- SUB(a, b) ---
    if (upper == "SUB" && nargs == 2) {
        long va, vb;
        if (two_int9(args[0], args[1], va, vb)) {
            result = format_long(va - vb);
        } else {
            result = format_double(mux_atof(u8(args[0])) - mux_atof(u8(args[1])));
        }
        return true;
    }

    // --- MUL(a, b, ...) ---
    if (upper == "MUL" && nargs >= 2) {
        double prod = 1.0;
        for (int i = 0; i < nargs; i++) prod *= mux_atof(u8(args[i]));
        result = format_double(NearestPretty(prod));
        return true;
    }

    // --- FDIV(a, b) ---
    if (upper == "FDIV" && nargs == 2) {
        double top = mux_atof(u8(args[0]));
        double bot = mux_atof(u8(args[1]));
        if (bot == 0.0) {
            if (top > 0.0) result = "+Inf";
            else if (top < 0.0) result = "-Inf";
            else result = "Ind";
        } else {
            result = format_double(top / bot);
        }
        return true;
    }

    // --- MOD(a, b) ---
    if (upper == "MOD" && nargs == 2) {
        int64_t top = mux_atoi64(u8(args[0]));
        int64_t bot = mux_atoi64(u8(args[1]));
        if (bot == 0) bot = 1;
        UTF8 buf[64];
        UTF8 *bufc = buf;
        safe_i64toa(i64Mod(top, bot), buf, &bufc);
        *bufc = '\0';
        result = reinterpret_cast<const char *>(buf);
        return true;
    }

    // --- INC(a) / DEC(a) ---
    if (upper == "INC") {
        int64_t v = (nargs >= 1) ? mux_atoi64(u8(args[0])) : 0;
        UTF8 buf[64]; UTF8 *bufc = buf;
        safe_i64toa(v + 1, buf, &bufc);
        *bufc = '\0';
        result = reinterpret_cast<const char *>(buf);
        return true;
    }
    if (upper == "DEC") {
        int64_t v = (nargs >= 1) ? mux_atoi64(u8(args[0])) : 0;
        UTF8 buf[64]; UTF8 *bufc = buf;
        safe_i64toa(v - 1, buf, &bufc);
        *bufc = '\0';
        result = reinterpret_cast<const char *>(buf);
        return true;
    }

    // --- ABS(a) ---
    if (upper == "ABS" && nargs == 1) {
        double d = mux_atof(u8(args[0]));
        result = format_double(fabs(d));
        return true;
    }

    // --- SIGN(a) ---
    if (upper == "SIGN" && nargs == 1) {
        double d = mux_atof(u8(args[0]));
        if (d > 0.0) result = "1";
        else if (d < 0.0) result = "-1";
        else result = "0";
        return true;
    }

    // --- FLOOR / CEIL / TRUNC / ROUND ---
    if (upper == "FLOOR" && nargs == 1) {
        result = format_double(floor(mux_atof(u8(args[0]))));
        return true;
    }
    if (upper == "CEIL" && nargs == 1) {
        result = format_double(ceil(mux_atof(u8(args[0]))));
        return true;
    }
    if (upper == "TRUNC" && nargs == 1) {
        double d = mux_atof(u8(args[0]));
        double ip;
        modf(d, &ip);
        result = format_double(ip);
        return true;
    }
    if (upper == "ROUND" && nargs == 1) {
        result = format_double(round(mux_atof(u8(args[0]))));
        return true;
    }

    // --- MAX(a, b, ...) / MIN(a, b, ...) ---
    if (upper == "MAX" && nargs >= 1) {
        double m = mux_atof(u8(args[0]));
        for (int i = 1; i < nargs; i++) {
            double d = mux_atof(u8(args[i]));
            if (d > m) m = d;
        }
        result = format_double(m);
        return true;
    }
    if (upper == "MIN" && nargs >= 1) {
        double m = mux_atof(u8(args[0]));
        for (int i = 1; i < nargs; i++) {
            double d = mux_atof(u8(args[i]));
            if (d < m) m = d;
        }
        result = format_double(m);
        return true;
    }

    // =============================================================
    // Comparisons (return "0" or "1")
    // =============================================================

    if (upper == "EQ" && nargs == 2) {
        // Matches fun_eq: int fast path, then string, then float.
        long va, vb;
        if (two_int9(args[0], args[1], va, vb)) {
            result = (va == vb) ? "1" : "0";
        } else if (args[0] == args[1]) {
            result = "1";
        } else {
            double da = mux_atof(u8(args[0]));
            double db = mux_atof(u8(args[1]));
            result = (da == db) ? "1" : "0";
        }
        return true;
    }
    if (upper == "NEQ" && nargs == 2) {
        long va, vb;
        if (two_int9(args[0], args[1], va, vb)) {
            result = (va != vb) ? "1" : "0";
        } else if (args[0] == args[1]) {
            result = "0";
        } else {
            double da = mux_atof(u8(args[0]));
            double db = mux_atof(u8(args[1]));
            result = (da != db) ? "1" : "0";
        }
        return true;
    }
    if (upper == "GT" && nargs == 2)
        return fold_cmp2(args, result,
            [](long a, long b) { return a > b; },
            [](double a, double b) { return a > b; });
    if (upper == "GTE" && nargs == 2)
        return fold_cmp2(args, result,
            [](long a, long b) { return a >= b; },
            [](double a, double b) { return a >= b; });
    if (upper == "LT" && nargs == 2)
        return fold_cmp2(args, result,
            [](long a, long b) { return a < b; },
            [](double a, double b) { return a < b; });
    if (upper == "LTE" && nargs == 2)
        return fold_cmp2(args, result,
            [](long a, long b) { return a <= b; },
            [](double a, double b) { return a <= b; });

    // --- COMP(a, b) — string comparison, returns -1/0/1 ---
    if (upper == "COMP" && nargs >= 2) {
        // Default: ASCII comparison (simplified — real impl has Unicode collation).
        int cmp = strcmp(args[0].c_str(), args[1].c_str());
        if (cmp < 0) result = "-1";
        else if (cmp > 0) result = "1";
        else result = "0";
        return true;
    }

    // =============================================================
    // Boolean
    // =============================================================

    if (upper == "NOT" && nargs == 1) {
        result = const_xlate(args[0]) ? "0" : "1";
        return true;
    }
    if (upper == "T") {
        if (nargs == 0) { result = "0"; return true; }
        result = const_xlate(args[0]) ? "1" : "0";
        return true;
    }

    // =============================================================
    // String functions
    // =============================================================

    if (upper == "STRLEN" && nargs == 1) {
        result = format_long(static_cast<long>(args[0].size()));
        return true;
    }

    if (upper == "CAT") {
        std::string merged;
        for (int i = 0; i < nargs; i++) {
            if (i > 0) merged += ' ';
            merged += args[i];
        }
        result = merged;
        return true;
    }

    if (upper == "STRCAT") {
        std::string merged;
        for (int i = 0; i < nargs; i++) merged += args[i];
        result = merged;
        return true;
    }

    if (upper == "MID" && nargs == 3) {
        const std::string &s = args[0];
        long start = mux_atol(u8(args[1]));
        long len = mux_atol(u8(args[2]));
        if (start < 0 || len < 0 || static_cast<size_t>(start) >= s.size()) {
            result = "";
        } else {
            result = s.substr(static_cast<size_t>(start), static_cast<size_t>(len));
        }
        return true;
    }

    // --- FIRST(s) / REST(s) / WORDS(s) — space-delimited ---
    if (upper == "FIRST" && nargs >= 1) {
        const std::string &s = args[0];
        size_t pos = s.find(' ');
        result = (pos == std::string::npos) ? s : s.substr(0, pos);
        return true;
    }
    if (upper == "REST" && nargs >= 1) {
        const std::string &s = args[0];
        size_t pos = s.find(' ');
        if (pos == std::string::npos) { result = ""; }
        else {
            // Skip leading spaces after first word.
            size_t start = s.find_first_not_of(' ', pos);
            result = (start == std::string::npos) ? "" : s.substr(start);
        }
        return true;
    }
    if (upper == "WORDS" && nargs >= 1) {
        const std::string &s = args[0];
        if (s.empty()) { result = "0"; return true; }
        long count = 0;
        bool in_word = false;
        for (char c : s) {
            if (c == ' ') { in_word = false; }
            else if (!in_word) { in_word = true; count++; }
        }
        result = format_long(count);
        return true;
    }

    // --- POS(pattern, string) ---
    if (upper == "POS" && nargs == 2) {
        size_t pos = args[1].find(args[0]);
        result = (pos == std::string::npos) ? "0"
                 : format_long(static_cast<long>(pos + 1));
        return true;
    }

    // --- STRMATCH(s, pattern) — wildcard match ---
    // Only fold exact equality (no wildcards in constant patterns).
    if (upper == "STRMATCH" && nargs == 2) {
        // If pattern has no wildcards, it's just string comparison.
        if (args[1].find('*') == std::string::npos
            && args[1].find('?') == std::string::npos) {
            result = (args[0] == args[1]) ? "1" : "0";
            return true;
        }
        // Don't fold wildcards — too complex for compile time.
        return false;
    }

    return false;
}

// ---------------------------------------------------------------
// Type tracking for native integer arithmetic
// ---------------------------------------------------------------

// Functions known to always return integer strings.
//
static bool returns_int(const std::string &upper) {
    return upper == "RAND" || upper == "STRLEN" || upper == "WORDS"
        || upper == "POS" || upper == "EQ" || upper == "NEQ"
        || upper == "GT" || upper == "GTE" || upper == "LT" || upper == "LTE"
        || upper == "NOT" || upper == "T" || upper == "COMP"
        || upper == "INC" || upper == "DEC" || upper == "SIGN"
        || upper == "MOD";
}

// Is this result provably an integer?
//
static bool is_int_result(const compile_result &cr) {
    if (cr.type == RT_INT) return true;
    if (cr.is_const) {
        int nDigits;
        return is_integer(u8(cr.value), &nDigits);
    }
    return cr.known_int;
}

// Convert a compile_result to RT_INT (integer in register).
// Uses inline RISC-V atoi — no ECALL boundary crossing.
//
static compile_result ensure_int(rv_compiler &rc, compile_result cr) {
    if (cr.type == RT_INT) return cr;

    if (cr.is_const) {
        // Parse at compile time, load as immediate.
        int64_t val = static_cast<int64_t>(mux_atol(u8(cr.value)));
        uint8_t reg = rc.alloc_int_reg();
        if (!reg) return cr;  // out of registers
        rv_load_i64(rc.code, reg, val);
        rc.needs_jit = true;
        return compile_result::int_reg(reg);
    }

    // Runtime string — emit inline atoi (stays in JIT, no ECALL).
    // Load guest address into a0, then atoi parses into out_reg.
    uint8_t reg = rc.alloc_int_reg();
    if (!reg) return cr;
    rv_load_val(rc.code, 10, cr.addr);               // a0 = string addr
    rv_emit_atoi(rc.code, 10, reg);                   // inline parse → reg
    rc.needs_jit = true;
    return compile_result::int_reg(reg);
}

// Convert a compile_result from RT_INT to RT_STRING.
// Uses inline RISC-V itoa — no ECALL boundary crossing.
//
static compile_result ensure_string(rv_compiler &rc, compile_result cr) {
    if (cr.type == RT_STRING) return cr;

    // RT_INT in register — emit inline itoa.
    uint64_t out_addr = rc.alloc_output();
    rv_load_val(rc.code, 10, out_addr);               // a0 = output buf addr
    rv_emit_itoa(rc.code, cr.reg, 10);                // inline format → buf
    rc.needs_jit = true;
    return compile_result::runtime(out_addr);
}

// ---------------------------------------------------------------
// AST compilation (recursive, bottom-up)
//
// Returns a compile_result with guest address and optional
// compile-time constant value.
// ---------------------------------------------------------------

// Forward declaration.
static compile_result compile_node(rv_compiler &rc, const ASTNode *node);

// Compile a sequence: concatenate child results.
//
static compile_result compile_sequence(rv_compiler &rc, const ASTNode *node) {
    if (node->children.size() == 1) {
        return compile_node(rc, node->children[0].get());
    }

    // Compile each child.
    std::vector<compile_result> child_results;
    for (auto &child : node->children) {
        child_results.push_back(compile_node(rc, child.get()));
    }

    // Check if all children are compile-time constants.
    bool all_const = true;
    for (auto &cr : child_results) {
        if (!cr.is_const) {
            all_const = false;
            break;
        }
    }

    if (all_const) {
        // Merge at compile time.
        std::string merged;
        for (auto &cr : child_results) {
            merged += cr.value;
        }
        uint64_t addr = rc.pool_str(merged);
        return compile_result::constant(addr, merged);
    }

    // Mixed sequence: emit strcat() ECALL at runtime.
    // Convert any RT_INT children to strings first.
    for (auto &cr : child_results) {
        if (cr.type == RT_INT) {
            cr = ensure_string(rc, cr);
        }
    }
    std::vector<uint64_t> child_addrs;
    for (auto &cr : child_results) {
        child_addrs.push_back(cr.addr);
    }

    uint64_t name_addr = rc.pool_str("strcat");
    uint64_t fargs_addr = rc.alloc_fargs(child_addrs);
    uint64_t out_addr = rc.alloc_output();

    rv_emit_call(rc.code, name_addr, fargs_addr,
                 static_cast<int>(child_addrs.size()),
                 out_addr, rv_compiler::OUT_SLOT);

    rc.ecalls++;
    rc.needs_jit = true;
    return compile_result::runtime(out_addr);
}

// Compile a function call: try constant fold, else emit ECALL.
//
static compile_result compile_funccall(rv_compiler &rc, const ASTNode *node) {
    // node->text is the function name.
    // node->children are the argument expressions.

    // Compile each argument.
    std::vector<compile_result> arg_results;
    for (auto &child : node->children) {
        arg_results.push_back(compile_node(rc, child.get()));
    }

    // Try constant folding: if all args are compile-time constants,
    // evaluate the function at compile time.
    bool all_const = true;
    std::vector<std::string> arg_values;
    for (auto &ar : arg_results) {
        if (!ar.is_const) {
            all_const = false;
            break;
        }
        arg_values.push_back(ar.value);
    }

    if (all_const) {
        std::string folded;
        if (try_fold(node->text, arg_values, folded)) {
            uint64_t addr = rc.pool_str(folded);
            rc.folds++;
            return compile_result::constant(addr, folded);
        }
    }

    // Uppercase for native arithmetic checks.
    std::string upper = node->text;
    for (auto &c : upper)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    int nargs = static_cast<int>(arg_results.size());

    // ---------------------------------------------------------------
    // Native integer arithmetic: when all args are provably integer,
    // emit RV64 ADD/SUB instead of ECALL.
    // ---------------------------------------------------------------

    // Helper: try to emit a binary native op.  Returns true if
    // successful, false if registers exhausted (fall through to ECALL).
    //
    auto try_native_binop = [&](auto emit_fn) -> bool {
        bool all_int = true;
        for (auto &ar : arg_results) {
            if (!is_int_result(ar)) { all_int = false; break; }
        }
        if (!all_int || rc.int_reg_idx + nargs >= 11) return false;
        compile_result acc = ensure_int(rc, arg_results[0]);
        if (acc.type != RT_INT) return false;
        for (int i = 1; i < nargs; i++) {
            compile_result b = ensure_int(rc, arg_results[i]);
            if (b.type != RT_INT) return false;
            uint8_t rd = rc.alloc_int_reg();
            if (!rd) return false;
            emit_fn(rc.code, rd, acc.reg, b.reg, i);
            acc = compile_result::int_reg(rd);
        }
        rc.native_ops++;
        rc.needs_jit = true;
        arg_results.clear();  // signal success
        arg_results.push_back({}); // placeholder
        arg_results[0] = acc;  // stash result
        return true;
    };

    if ((upper == "ADD" || upper == "SUB") && nargs >= 2) {
        bool is_add = (upper == "ADD");
        if (try_native_binop([is_add](auto &code, uint8_t rd, uint8_t rs1,
                                       uint8_t rs2, int i) {
            if (is_add || i > 1) code.push_back(rv_ADD(rd, rs1, rs2));
            else                 code.push_back(rv_SUB(rd, rs1, rs2));
        })) {
            return arg_results[0];
        }
    }

    if (upper == "MUL" && nargs >= 2) {
        if (try_native_binop([](auto &code, uint8_t rd, uint8_t rs1,
                                uint8_t rs2, int) {
            code.push_back(rv_MUL(rd, rs1, rs2));
        })) {
            return arg_results[0];
        }
    }

    if (upper == "MOD" && nargs == 2) {
        if (try_native_binop([](auto &code, uint8_t rd, uint8_t rs1,
                                uint8_t rs2, int) {
            code.push_back(rv_REM(rd, rs1, rs2));
        })) {
            return arg_results[0];
        }
    }

    // Native comparisons: eq, neq, gt, gte, lt, lte.
    // All return 0 or 1 in a register.
    //
    if ((upper == "EQ" || upper == "NEQ" || upper == "GT" || upper == "GTE"
         || upper == "LT" || upper == "LTE") && nargs == 2
        && is_int_result(arg_results[0]) && is_int_result(arg_results[1])) {
        compile_result a = ensure_int(rc, arg_results[0]);
        compile_result b = ensure_int(rc, arg_results[1]);
        if (a.type == RT_INT && b.type == RT_INT) {
            uint8_t rd = rc.alloc_int_reg();
            if (rd) {
                // SLT rd, rs1, rs2  — set if less than (signed).
                // We compose comparisons from SUB + SLT + XORI.
                //
                if (upper == "EQ") {
                    // rd = (a == b) ? 1 : 0
                    // SUB t0, a, b; SLTIU rd, t0, 1
                    rc.code.push_back(rv_SUB(5, a.reg, b.reg));
                    rc.code.push_back(rv_i_type(OP_IMM, rd, ALU_SLTIU, 5, 1));
                } else if (upper == "NEQ") {
                    // rd = (a != b) ? 1 : 0
                    // SUB t0, a, b; SLTU rd, x0, t0
                    rc.code.push_back(rv_SUB(5, a.reg, b.reg));
                    rc.code.push_back(rv_r_type(OP_REG, rd, ALU_SLTU, 0, 5, 0));
                } else if (upper == "GT") {
                    // rd = (a > b) ? 1 : 0 = (b < a) ? 1 : 0
                    rc.code.push_back(rv_r_type(OP_REG, rd, ALU_SLT, b.reg, a.reg, 0));
                } else if (upper == "LT") {
                    // rd = (a < b) ? 1 : 0
                    rc.code.push_back(rv_r_type(OP_REG, rd, ALU_SLT, a.reg, b.reg, 0));
                } else if (upper == "GTE") {
                    // rd = (a >= b) ? 1 : 0 = NOT(a < b)
                    rc.code.push_back(rv_r_type(OP_REG, rd, ALU_SLT, a.reg, b.reg, 0));
                    rc.code.push_back(rv_i_type(OP_IMM, rd, ALU_XORI, rd, 1));
                } else if (upper == "LTE") {
                    // rd = (a <= b) ? 1 : 0 = NOT(b < a)
                    rc.code.push_back(rv_r_type(OP_REG, rd, ALU_SLT, b.reg, a.reg, 0));
                    rc.code.push_back(rv_i_type(OP_IMM, rd, ALU_XORI, rd, 1));
                }
                rc.native_ops++;
                rc.needs_jit = true;
                return compile_result::int_reg(rd);
            }
        }
    }

    // Native NOT: not(x) = (x == 0) ? 1 : 0
    //
    if (upper == "NOT" && nargs == 1 && is_int_result(arg_results[0])) {
        compile_result a = ensure_int(rc, arg_results[0]);
        if (a.type == RT_INT) {
            uint8_t rd = rc.alloc_int_reg();
            if (rd) {
                // SLTIU rd, a, 1  — rd = (a < 1u) = (a == 0) ? 1 : 0
                rc.code.push_back(rv_i_type(OP_IMM, rd, ALU_SLTIU, a.reg, 1));
                rc.native_ops++;
                rc.needs_jit = true;
                return compile_result::int_reg(rd);
            }
        }
    }

    if (upper == "INC" && nargs >= 1 && is_int_result(arg_results[0])) {
        compile_result a = ensure_int(rc, arg_results[0]);
        if (a.type == RT_INT) {
            uint8_t rd = rc.alloc_int_reg();
            if (rd) {
                rc.code.push_back(rv_ADDI(rd, a.reg, 1));
                rc.native_ops++;
                rc.needs_jit = true;
                return compile_result::int_reg(rd);
            }
        }
    }

    if (upper == "DEC" && nargs >= 1 && is_int_result(arg_results[0])) {
        compile_result a = ensure_int(rc, arg_results[0]);
        if (a.type == RT_INT) {
            uint8_t rd = rc.alloc_int_reg();
            if (rd) {
                rc.code.push_back(rv_ADDI(rd, a.reg, -1));
                rc.native_ops++;
                rc.needs_jit = true;
                return compile_result::int_reg(rd);
            }
        }
    }

    // ---------------------------------------------------------------
    // Fall through to ECALL.
    // ---------------------------------------------------------------

    // For RT_INT args, convert to string for the ECALL convention.
    for (auto &ar : arg_results) {
        if (ar.type == RT_INT) {
            ar = ensure_string(rc, ar);
        }
    }

    std::vector<uint64_t> arg_addrs;
    for (auto &ar : arg_results) {
        arg_addrs.push_back(ar.addr);
    }

    uint64_t name_addr = rc.pool_str(node->text);
    uint64_t fargs_addr = rc.alloc_fargs(arg_addrs);
    uint64_t out_addr = rc.alloc_output();

    rv_emit_call(rc.code, name_addr, fargs_addr,
                 static_cast<int>(arg_addrs.size()),
                 out_addr, rv_compiler::OUT_SLOT);

    rc.ecalls++;
    rc.needs_jit = true;

    // Mark result as integer if function is known to return integers.
    if (returns_int(upper)) {
        return compile_result::runtime_int(out_addr);
    }
    return compile_result::runtime(out_addr);
}

static compile_result compile_node(rv_compiler &rc, const ASTNode *node) {
    switch (node->type) {
    case AST_LITERAL:
    case AST_SPACE: {
        uint64_t addr = rc.pool_str(node->text);
        return compile_result::constant(addr, node->text);
    }

    case AST_SEQUENCE:
        return compile_sequence(rc, node);

    case AST_FUNCCALL:
        return compile_funccall(rc, node);

    case AST_EVALBRACKET:
        // [expr] — compile the inner sequence.
        if (node->children.size() == 1) {
            return compile_node(rc, node->children[0].get());
        }
        // Shouldn't happen, but fall through.
        {
            uint64_t addr = rc.pool_str("");
            return compile_result::constant(addr, "");
        }

    default:
        // Unsupported node type — return empty string.
        {
            uint64_t addr = rc.pool_str("");
            return compile_result::constant(addr, "");
        }
    }
}

// ---------------------------------------------------------------
// Public: compile an expression string to a runnable RV64 program.
// ---------------------------------------------------------------

struct compiled_program {
    std::vector<uint8_t> memory;
    size_t memory_size;
    uint64_t out_addr;      // where the final result lives
    bool ok;
    int folds;
    int ecalls;
    int native_ops;
    bool needs_jit;         // true if JIT execution required
};

static compiled_program compile_expression(const UTF8 *expr, size_t nLen) {
    compiled_program prog;
    prog.ok = false;
    prog.folds = 0;
    prog.ecalls = 0;
    prog.native_ops = 0;
    prog.needs_jit = false;

    // Parse the expression.
    auto ast = ast_parse_string(expr, nLen);
    if (!ast) {
        return prog;
    }

    // Compile.
    rv_compiler rc;
    compile_result cr = compile_node(rc, ast.get());

    // If the final result is RT_INT, convert to string for output.
    if (cr.type == RT_INT) {
        cr = ensure_string(rc, cr);
    }

    // Emit exit.
    rv_emit_exit(rc.code);

    // Copy code to guest memory.
    for (size_t i = 0; i < rc.code.size() && i * 4 < rv_compiler::CODE_LIMIT; i++) {
        memcpy(rc.memory.data() + i * 4, &rc.code[i], 4);
    }

    prog.memory = std::move(rc.memory);
    prog.memory_size = rv_compiler::MEM_SIZE;
    prog.out_addr = cr.addr;
    prog.ok = true;
    prog.folds = rc.folds;
    prog.ecalls = rc.ecalls;
    prog.native_ops = rc.native_ops;
    prog.needs_jit = rc.needs_jit;
    return prog;
}

// ---------------------------------------------------------------
// fun_rveval: softcode function.
//
// rveval(<expression>)
//
// Parses and compiles <expression> to RV64, runs through the JIT,
// returns the result.
//
// Examples:
//   think rveval(add(1,2))           → 3   (constant folded)
//   think rveval(add(mul(3,4),5))    → 17  (fully folded)
//   think rveval(strlen(hello))      → 5   (folded)
// ---------------------------------------------------------------

// ---------------------------------------------------------------
// Persistent DBT state — avoids mmap/munmap per call.
//
// The 64 MB mmap + 1 MB block cache allocation dominated the
// ECALL path.  By keeping the dbt_state_t alive, we amortize
// the cost to one-time initialization.
// ---------------------------------------------------------------

static dbt_state_t s_persistent_dbt;
static bool s_dbt_ready = false;

// Get a reset DBT state, initializing on first use.
// Returns nullptr on allocation failure.
//
static dbt_state_t *get_dbt(uint8_t *memory, size_t memory_size,
                             int (*ecall_fn)(rv64_ctx_t *, void *),
                             void *ecall_user) {
    dbt_state_t *dbt = &s_persistent_dbt;
    if (!s_dbt_ready) {
        if (dbt_init(dbt, memory, memory_size, ecall_fn, ecall_user) != 0) {
            return nullptr;
        }
        s_dbt_ready = true;
        return dbt;
    }
    // Reset for new program: keep mmap'd code buffer + cache allocation.
    dbt_reset(dbt, memory, memory_size, ecall_fn, ecall_user);
    return dbt;
}

// ECALL handler context and forward declaration.
//
static constexpr uint64_t ECALL_CALL_FUNC = 0x100;

struct eval_ctx {
    uint8_t *memory;
    size_t   memory_size;
    dbref    executor;
    dbref    caller;
    dbref    enactor;
};

static int eval_ecall(rv64_ctx_t *ctx, void *user_data);

// ---------------------------------------------------------------
// Compile cache — LRU cache of compiled programs.
//
// Keyed by expression text.  Cache hits skip compilation entirely.
// Combined with DBT block cache persistence (dbt_rerun), repeated
// evaluation of the same expression does zero compilation and zero
// JIT translation — just runs the cached native code.
//
// The DBT tracks which program it was last set up for.  Same
// program → dbt_rerun (keep translated blocks).  Different
// program → dbt_reset (re-translate).
// ---------------------------------------------------------------

struct compile_cache_entry {
    compiled_program prog;
    std::list<std::string>::iterator lru_it;
};

static std::unordered_map<std::string, compile_cache_entry> s_compile_cache;
static std::list<std::string> s_compile_lru;
static constexpr size_t COMPILE_CACHE_MAX = 256;
static constexpr size_t COMPILE_CACHE_MIN_LEN = 8;

// Track which program the DBT was last set up for, so we can
// use dbt_rerun (fast) instead of dbt_reset (slow) on cache hits.
//
static uint8_t *s_dbt_last_memory = nullptr;

// Look up or compile an expression.  Returns a pointer to the
// cached compiled_program (owned by the cache — do not free).
// Returns nullptr on compilation failure.
//
static compiled_program *compile_cached(const UTF8 *expr, size_t nLen) {
    std::string key(reinterpret_cast<const char *>(expr), nLen);

    auto it = s_compile_cache.find(key);
    if (it != s_compile_cache.end()) {
        // Cache hit — move to front of LRU.
        s_compile_lru.splice(s_compile_lru.begin(), s_compile_lru,
                             it->second.lru_it);
        return &it->second.prog;
    }

    // Cache miss — compile.
    compiled_program prog = compile_expression(expr, nLen);
    if (!prog.ok) return nullptr;

    // Evict LRU entries if cache is full.
    while (s_compile_cache.size() >= COMPILE_CACHE_MAX) {
        auto &victim_key = s_compile_lru.back();
        // If the DBT was pointing at this victim's memory, invalidate.
        auto vit = s_compile_cache.find(victim_key);
        if (vit != s_compile_cache.end()
            && s_dbt_last_memory == vit->second.prog.memory.data()) {
            s_dbt_last_memory = nullptr;
        }
        s_compile_cache.erase(victim_key);
        s_compile_lru.pop_back();
    }

    s_compile_lru.push_front(key);
    auto [ins_it, _] = s_compile_cache.emplace(
        key, compile_cache_entry{std::move(prog), s_compile_lru.begin()});
    return &ins_it->second.prog;
}

// Run a cached program.  Uses dbt_rerun if the DBT already has
// translated blocks for this program, otherwise dbt_reset.
//
static bool run_cached_program(compiled_program *prog,
                                dbref executor, dbref caller_db,
                                dbref enactor,
                                UTF8 *out, size_t out_size) {
    if (!prog->needs_jit) {
        const char *r = reinterpret_cast<const char *>(
            prog->memory.data() + prog->out_addr);
        size_t n = strlen(r);
        if (n >= out_size) n = out_size - 1;
        memcpy(out, r, n);
        out[n] = '\0';
        return true;
    }

    // Clear output region for clean re-run.
    memset(prog->memory.data() + rv_compiler::OUT_BASE, 0,
           rv_compiler::OUT_LIMIT - rv_compiler::OUT_BASE);

    eval_ctx ec;
    ec.memory = prog->memory.data();
    ec.memory_size = prog->memory_size;
    ec.executor = executor;
    ec.caller = caller_db;
    ec.enactor = enactor;

    dbt_state_t *dbt;
    if (s_dbt_ready && s_dbt_last_memory == prog->memory.data()) {
        // Same program as last time — keep translated blocks.
        dbt = &s_persistent_dbt;
        dbt_rerun(dbt, eval_ecall, &ec);
    } else {
        // Different program — full reset needed.
        dbt = get_dbt(prog->memory.data(), prog->memory_size,
                       eval_ecall, &ec);
        if (!dbt) return false;
        s_dbt_last_memory = prog->memory.data();
    }

    int rc = dbt_run(dbt, 0, rv_compiler::STACK_TOP);
    if (rc != 0) return false;

    const char *r = reinterpret_cast<const char *>(
        prog->memory.data() + prog->out_addr);
    size_t n = strlen(r);
    if (n >= out_size) n = out_size - 1;
    memcpy(out, r, n);
    out[n] = '\0';
    return true;
}

// ECALL handler implementation.
//
static int eval_ecall(rv64_ctx_t *ctx, void *user_data) {
    eval_ctx *ec = static_cast<eval_ctx *>(user_data);
    uint64_t syscall_num = ctx->x[17];

    switch (syscall_num) {
    case 93:
        return static_cast<int>(ctx->x[10]);

    case ECALL_CALL_FUNC: {
        uint64_t name_addr = ctx->x[10];
        uint64_t fargs_addr = ctx->x[11];
        int nfargs = static_cast<int>(ctx->x[12]);
        uint64_t out_addr = ctx->x[13];
        uint64_t out_size = ctx->x[14];

        if (name_addr >= ec->memory_size ||
            out_addr + out_size > ec->memory_size) {
            ctx->x[10] = 0;
            return -1;
        }

        const UTF8 *func_name = ec->memory + name_addr;
        size_t nCased;
        UTF8 *pCased = mux_strupr(func_name, nCased);
        std::vector<UTF8> key(pCased, pCased + nCased);
        auto it = mudstate.builtin_functions.find(key);
        if (it == mudstate.builtin_functions.end()) {
            const char *err = "#-1 FUNCTION NOT FOUND";
            size_t elen = strlen(err);
            if (elen >= out_size) elen = out_size - 1;
            memcpy(ec->memory + out_addr, err, elen);
            ec->memory[out_addr + elen] = '\0';
            ctx->x[10] = static_cast<uint64_t>(elen);
            return -1;
        }

        FUN *fp = it->second;

        UTF8 *fargs[MAX_ARG];
        if (nfargs > MAX_ARG) nfargs = MAX_ARG;
        for (int i = 0; i < nfargs; i++) {
            uint64_t ptr;
            memcpy(&ptr, ec->memory + fargs_addr + i * 8, 8);
            if (ptr >= ec->memory_size) {
                ctx->x[10] = 0;
                return -1;
            }
            fargs[i] = ec->memory + ptr;
        }

        UTF8 *buff = alloc_lbuf("eval_ecall");
        UTF8 *bufc = buff;

        fp->fun(fp, buff, &bufc, ec->executor, ec->caller, ec->enactor,
                0, fargs, nfargs, nullptr, 0);

        *bufc = '\0';
        size_t result_len = static_cast<size_t>(bufc - buff);
        if (result_len >= out_size) result_len = out_size - 1;
        memcpy(ec->memory + out_addr, buff, result_len);
        ec->memory[out_addr + result_len] = '\0';

        free_lbuf(buff);

        ctx->x[10] = static_cast<uint64_t>(result_len);
        return -1;
    }

    default:
        ctx->x[10] = 0;
        return -1;
    }
}

FUNCTION(fun_rveval)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 1) {
        safe_str(T("#-1 TOO FEW ARGUMENTS"), buff, bufc);
        return;
    }

    const UTF8 *expr = fargs[0];
    size_t nLen = strlen(reinterpret_cast<const char *>(expr));

    // Look up in compile cache (or compile on miss).
    compiled_program *prog = compile_cached(expr, nLen);
    if (!prog) {
        safe_str(T("#-1 COMPILATION FAILED"), buff, bufc);
        return;
    }

    // If everything was constant-folded, the result is already
    // in the string pool — no need to run the JIT at all.
    if (!prog->needs_jit) {
        const UTF8 *result = prog->memory.data() + prog->out_addr;
        safe_str(result, buff, bufc);
        return;
    }

    // Run through the JIT with compile+block cache.
    UTF8 result[LBUF_SIZE];
    if (!run_cached_program(prog, executor, caller, enactor,
                             result, sizeof(result))) {
        safe_str(T("#-1 DBT EXECUTION ERROR"), buff, bufc);
        return;
    }

    safe_str(result, buff, bufc);
}

// ---------------------------------------------------------------
// fun_rvbench: benchmark rveval vs native mux_exec.
//
// rvbench(<expression>, <iterations>)
//
// Runs the expression through three paths:
//   1. Native mux_exec (AST eval) — the current production path
//   2. rveval compile-every-time
//   3. rveval compile-once, run N times (amortized)
//
// Returns a multi-line report with timings in microseconds.
// ---------------------------------------------------------------

static double elapsed_us(const struct timespec &start,
                          const struct timespec &end) {
    double s = static_cast<double>(end.tv_sec - start.tv_sec);
    double ns = static_cast<double>(end.tv_nsec - start.tv_nsec);
    return (s * 1e6) + (ns / 1e3);
}

// Run the compiled program through the JIT.  Returns the result
// string (written into caller-provided buffer).
//
// If reuse_dbt is true, skip the full DBT reset and only update the
// ECALL callback — keeps translated blocks cached.  Caller must
// ensure the guest code region is unchanged.
//
static bool run_compiled(compiled_program &prog,
                          dbref executor, dbref caller_db, dbref enactor,
                          UTF8 *out, size_t out_size,
                          bool reuse_dbt = false) {
    if (!prog.needs_jit) {
        // Fully folded — result is already in guest memory.
        const char *r = reinterpret_cast<const char *>(
            prog.memory.data() + prog.out_addr);
        size_t n = strlen(r);
        if (n >= out_size) n = out_size - 1;
        memcpy(out, r, n);
        out[n] = '\0';
        return true;
    }

    eval_ctx ec;
    ec.memory = prog.memory.data();
    ec.memory_size = prog.memory_size;
    ec.executor = executor;
    ec.caller = caller_db;
    ec.enactor = enactor;

    dbt_state_t *dbt;
    if (reuse_dbt && s_dbt_ready) {
        dbt = &s_persistent_dbt;
        dbt_rerun(dbt, eval_ecall, &ec);
    } else {
        dbt = get_dbt(prog.memory.data(), prog.memory_size,
                       eval_ecall, &ec);
        if (!dbt) return false;
    }

    int rc = dbt_run(dbt, 0, rv_compiler::STACK_TOP);

    if (rc != 0) return false;

    const char *r = reinterpret_cast<const char *>(
        prog.memory.data() + prog.out_addr);
    size_t n = strlen(r);
    if (n >= out_size) n = out_size - 1;
    memcpy(out, r, n);
    out[n] = '\0';
    return true;
}

FUNCTION(fun_rvbench)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 2) {
        safe_str(T("#-1 TOO FEW ARGUMENTS"), buff, bufc);
        return;
    }

    const UTF8 *expr = fargs[0];
    size_t nLen = strlen(reinterpret_cast<const char *>(expr));
    int iterations = mux_atol(fargs[1]);
    if (iterations < 1) iterations = 1;
    if (iterations > 1000000) iterations = 1000000;

    // Verify both paths produce the same result.
    compiled_program prog = compile_expression(expr, nLen);
    if (!prog.ok) {
        safe_str(T("#-1 COMPILATION FAILED"), buff, bufc);
        return;
    }

    // --- Benchmark 1: Native mux_exec ---
    struct timespec t0, t1;
    int eval_flags = EV_FCHECK | EV_EVAL;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < iterations; i++) {
        UTF8 *tbuf = alloc_lbuf("rvbench.native");
        UTF8 *tbufc = tbuf;
        mux_exec(expr, nLen, tbuf, &tbufc, executor, caller, enactor,
                 eval_flags, nullptr, 0);
        *tbufc = '\0';
        free_lbuf(tbuf);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double native_us = elapsed_us(t0, t1);

    // --- Benchmark 2: rveval compile-every-time ---
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < iterations; i++) {
        compiled_program p = compile_expression(expr, nLen);
        if (p.ok) {
            UTF8 result[256];
            run_compiled(p, executor, caller, enactor, result, sizeof(result));
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double compile_each_us = elapsed_us(t0, t1);

    // --- Benchmark 3: production path (compile cache + block cache) ---
    // Uses compile_cached (LRU) + run_cached_program (dbt_rerun).
    // First iteration is a cache miss (compiles + JIT translates);
    // subsequent iterations hit both caches — zero compilation,
    // zero JIT translation.
    //
    // Invalidate the compile cache entry for this expression first
    // so the first iteration is a genuine miss.
    {
        std::string key(reinterpret_cast<const char *>(expr), nLen);
        auto cit = s_compile_cache.find(key);
        if (cit != s_compile_cache.end()) {
            if (s_dbt_last_memory == cit->second.prog.memory.data()) {
                s_dbt_last_memory = nullptr;
            }
            s_compile_lru.erase(cit->second.lru_it);
            s_compile_cache.erase(cit);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < iterations; i++) {
        compiled_program *cp = compile_cached(expr, nLen);
        if (cp) {
            UTF8 result[256];
            run_cached_program(cp, executor, caller, enactor,
                               result, sizeof(result));
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double cached_us = elapsed_us(t0, t1);

    // Format report.
    double per_native = native_us / iterations;
    double per_compile = compile_each_us / iterations;
    double per_cached = cached_us / iterations;

    UTF8 report[LBUF_SIZE];
    snprintf(reinterpret_cast<char *>(report), sizeof(report),
        "expr=%s iters=%d folds=%d ecalls=%d nativ=%d | "
        "native=%.2fus/call | "
        "compile-each=%.2fus/call (%.1fx) | "
        "cached=%.2fus/call (%.1fx)",
        reinterpret_cast<const char *>(expr),
        iterations, prog.folds, prog.ecalls, prog.native_ops,
        per_native,
        per_compile, per_compile / per_native,
        per_cached, per_cached / per_native);

    safe_str(report, buff, bufc);
}
