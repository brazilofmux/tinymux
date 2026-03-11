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

// Lightweight ECALL numbers (no function dispatch overhead).
//
static constexpr int ECALL_ATOI = 0x101;
static constexpr int ECALL_ITOA = 0x102;

// Convert a compile_result to RT_INT (integer in register).
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

    // Runtime string — emit lightweight ECALL_ATOI.
    rv_load_val(rc.code, 10, cr.addr);              // a0 = string addr
    rc.code.push_back(rv_ADDI(17, 0, ECALL_ATOI));  // a7 = 0x101
    rc.code.push_back(rv_ECALL());
    uint8_t reg = rc.alloc_int_reg();
    if (!reg) return cr;
    rc.code.push_back(rv_ADDI(reg, 10, 0));          // mv sN, a0
    rc.needs_jit = true;
    return compile_result::int_reg(reg);
}

// Convert a compile_result from RT_INT to RT_STRING.
//
static compile_result ensure_string(rv_compiler &rc, compile_result cr) {
    if (cr.type == RT_STRING) return cr;

    // RT_INT in register — emit lightweight ECALL_ITOA.
    uint64_t out_addr = rc.alloc_output();
    rc.code.push_back(rv_ADDI(10, cr.reg, 0));                  // a0 = integer
    rv_load_val(rc.code, 11, out_addr);                          // a1 = output buf
    rv_load_val(rc.code, 12, rv_compiler::OUT_SLOT);             // a2 = buf size
    rc.code.push_back(rv_ADDI(17, 0, ECALL_ITOA));              // a7 = 0x102
    rc.code.push_back(rv_ECALL());
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

    if ((upper == "ADD" || upper == "SUB") && nargs >= 2) {
        bool all_int = true;
        for (auto &ar : arg_results) {
            if (!is_int_result(ar)) { all_int = false; break; }
        }
        if (all_int && rc.int_reg_idx + nargs < 11) {
            compile_result acc = ensure_int(rc, arg_results[0]);
            if (acc.type == RT_INT) {
                bool ok = true;
                for (int i = 1; i < nargs; i++) {
                    compile_result b = ensure_int(rc, arg_results[i]);
                    if (b.type != RT_INT) { ok = false; break; }
                    uint8_t rd = rc.alloc_int_reg();
                    if (!rd) { ok = false; break; }
                    if (upper == "ADD" || i > 1) {
                        // SUB only applies to first pair; subsequent args
                        // would need different semantics.  For n-ary ADD,
                        // accumulate with ADD.
                        rc.code.push_back(rv_ADD(rd, acc.reg, b.reg));
                    } else {
                        rc.code.push_back(rv_SUB(rd, acc.reg, b.reg));
                    }
                    acc = compile_result::int_reg(rd);
                }
                if (ok) {
                    rc.native_ops++;
                    rc.needs_jit = true;
                    return acc;
                }
            }
            // Fall through to ECALL if register allocation failed.
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

// ECALL handler (same as dbt_harness.cpp).
//
static constexpr uint64_t ECALL_CALL_FUNC = 0x100;

struct eval_ctx {
    uint8_t *memory;
    size_t   memory_size;
    dbref    executor;
    dbref    caller;
    dbref    enactor;
};

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

    case 0x101: {  // ECALL_ATOI — lightweight string→integer.
        uint64_t addr = ctx->x[10];
        if (addr < ec->memory_size) {
            ctx->x[10] = static_cast<uint64_t>(
                static_cast<int64_t>(mux_atol(ec->memory + addr)));
        } else {
            ctx->x[10] = 0;
        }
        return -1;
    }

    case 0x102: {  // ECALL_ITOA — lightweight integer→string.
        int64_t val = static_cast<int64_t>(ctx->x[10]);
        uint64_t out_addr = ctx->x[11];
        uint64_t out_size = ctx->x[12];
        if (out_addr + out_size <= ec->memory_size && out_size > 0) {
            UTF8 buf[64];
            UTF8 *bufc = buf;
            safe_i64toa(val, buf, &bufc);
            *bufc = '\0';
            size_t len = static_cast<size_t>(bufc - buf);
            if (len >= out_size) len = out_size - 1;
            memcpy(ec->memory + out_addr, buf, len);
            ec->memory[out_addr + len] = '\0';
            ctx->x[10] = static_cast<uint64_t>(len);
        } else {
            ctx->x[10] = 0;
        }
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

    // Compile the expression.
    const UTF8 *expr = fargs[0];
    size_t nLen = strlen(reinterpret_cast<const char *>(expr));

    compiled_program prog = compile_expression(expr, nLen);
    if (!prog.ok) {
        safe_str(T("#-1 COMPILATION FAILED"), buff, bufc);
        return;
    }

    // If everything was constant-folded, the result is already
    // in the string pool — no need to run the JIT at all.
    if (!prog.needs_jit) {
        const UTF8 *result = prog.memory.data() + prog.out_addr;
        safe_str(result, buff, bufc);
        return;
    }

    // Run through the JIT (persistent DBT — no mmap per call).
    eval_ctx ec;
    ec.memory = prog.memory.data();
    ec.memory_size = prog.memory_size;
    ec.executor = executor;
    ec.caller = caller;
    ec.enactor = enactor;

    dbt_state_t *dbt = get_dbt(prog.memory.data(), prog.memory_size,
                                eval_ecall, &ec);
    if (!dbt) {
        safe_str(T("#-1 DBT INIT FAILED"), buff, bufc);
        return;
    }

    int rc = dbt_run(dbt, 0, rv_compiler::STACK_TOP);

    if (rc != 0) {
        safe_str(T("#-1 DBT EXECUTION ERROR"), buff, bufc);
        return;
    }

    // Copy result from guest output buffer.
    const UTF8 *result = prog.memory.data() + prog.out_addr;
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
static bool run_compiled(compiled_program &prog,
                          dbref executor, dbref caller_db, dbref enactor,
                          UTF8 *out, size_t out_size) {
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

    dbt_state_t *dbt = get_dbt(prog.memory.data(), prog.memory_size,
                                eval_ecall, &ec);
    if (!dbt) return false;

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

    // --- Benchmark 3: rveval compile-once ---
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < iterations; i++) {
        UTF8 result[256];
        // Re-init guest memory for ECALL path (outputs get overwritten).
        if (prog.needs_jit) {
            // Reset output region for clean re-run.
            memset(prog.memory.data() + rv_compiler::OUT_BASE, 0,
                   rv_compiler::OUT_LIMIT - rv_compiler::OUT_BASE);
        }
        run_compiled(prog, executor, caller, enactor, result, sizeof(result));
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
