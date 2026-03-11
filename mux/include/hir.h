/*! \file hir.h
 * \brief High-level Intermediate Representation for the softcode compiler.
 *
 * Parallel-array design: instruction index = value number.
 * Same architecture as ~/slow-32/selfhost stage05 HIR.
 *
 * M1: single basic block, no PHI, no control flow.
 * M2+: multiple blocks, PHI nodes, SSA construction.
 */

#ifndef HIR_H
#define HIR_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------
// Instruction kinds
// ---------------------------------------------------------------

enum hir_kind {
    HIR_NOP,

    // Constants
    HIR_ICONST,     // integer constant: val = value
    HIR_SCONST,     // string constant: val = guest address, sval = string

    // Arithmetic (native RV64)
    HIR_ADD,        // src1 + src2
    HIR_SUB,        // src1 - src2
    HIR_MUL,        // src1 * src2
    HIR_REM,        // src1 % src2
    HIR_NEG,        // -src1

    // Comparison (native RV64, result is int 0/1)
    HIR_EQ,         // src1 == src2
    HIR_NE,         // src1 != src2
    HIR_LT,         // src1 < src2
    HIR_LE,         // src1 <= src2
    HIR_GT,         // src1 > src2
    HIR_GE,         // src1 >= src2

    // Logic
    HIR_NOT,        // !src1 (int → int)

    // Unary arithmetic
    HIR_INC,        // src1 + 1
    HIR_DEC,        // src1 - 1

    // Type conversion
    HIR_ATOI,       // string → int (inline RV64 atoi)
    HIR_ITOA,       // int → string (inline RV64 itoa)

    // Function calls
    HIR_CALL,       // ECALL to engine function
    HIR_STRCAT,     // concatenate N strings via ECALL

    // Control flow
    HIR_RET,        // return/exit program

    // SSA (M2+)
    HIR_COPY,       // src1 = source value
    HIR_PHI,        // phi node

    // Memory (M2+)
    HIR_LOAD_Q,     // load %q register
    HIR_STORE_Q,    // store %q register

    // Control flow (M2+)
    HIR_BR,         // unconditional branch
    HIR_BRC,        // conditional branch

    HIR_NUM_KINDS
};

// ---------------------------------------------------------------
// Type lattice
// ---------------------------------------------------------------

enum hir_type {
    TY_VOID,
    TY_INT,         // 64-bit integer (in RV64 register)
    TY_STRING,      // string (guest memory address)
};

// ---------------------------------------------------------------
// HIR program — parallel arrays
// ---------------------------------------------------------------

static constexpr int HIR_MAX_INSNS  = 4096;
static constexpr int HIR_MAX_BLOCKS = 256;
static constexpr int HIR_MAX_CARGS  = 2048;

struct hir_program {
    // Per-instruction arrays.
    hir_kind kind[HIR_MAX_INSNS];
    hir_type ty[HIR_MAX_INSNS];
    int      src1[HIR_MAX_INSNS];       // operand 1 (insn index, -1 = none)
    int      src2[HIR_MAX_INSNS];       // operand 2 (insn index, -1 = none)
    int64_t  val[HIR_MAX_INSNS];        // immediate or metadata
    int      blk[HIR_MAX_INSNS];        // containing basic block

    // String values for SCONST (compile-time known strings).
    // Indexed by instruction index.  Empty for non-SCONST insns.
    std::string sval[HIR_MAX_INSNS];

    // Known-integer flag: true if a TY_STRING result is known to
    // parse as an integer (e.g., ECALL result from strlen/eq/gt).
    bool known_int[HIR_MAX_INSNS];

    int n_insns;

    // Call arguments (flattened array).
    // For HIR_CALL/HIR_STRCAT at instruction i:
    //   cbase[i] = starting index into carg[]
    //   cnargs[i] = number of arguments
    //   carg[cbase[i]..cbase[i]+cnargs[i]-1] = argument insn indices
    //
    int carg[HIR_MAX_CARGS];
    int cbase[HIR_MAX_INSNS];
    int cnargs[HIR_MAX_INSNS];
    int n_cargs;

    // For HIR_CALL: function index (engine_api index) or 0 for string-based.
    int func_idx[HIR_MAX_INSNS];

    // Basic blocks (M2+; M1 uses block 0 only).
    int n_blocks;

    // Final result instruction index.
    int result;

    // Statistics.
    int folds;
    int ecalls;
    int native_ops;
    bool needs_jit;

    void init() {
        n_insns = 0;
        n_cargs = 0;
        n_blocks = 1;
        result = -1;
        folds = 0;
        ecalls = 0;
        native_ops = 0;
        needs_jit = false;
    }

    // Emit an instruction, return its index.
    int emit(hir_kind k, hir_type t, int s1 = -1, int s2 = -1,
             int64_t v = 0) {
        if (n_insns >= HIR_MAX_INSNS) return -1;
        int i = n_insns++;
        kind[i] = k;
        ty[i] = t;
        src1[i] = s1;
        src2[i] = s2;
        val[i] = v;
        blk[i] = 0;
        cbase[i] = 0;
        cnargs[i] = 0;
        func_idx[i] = 0;
        sval[i].clear();
        known_int[i] = false;
        return i;
    }

    // Emit a string constant.
    int emit_sconst(uint64_t addr, const std::string &s) {
        int i = emit(HIR_SCONST, TY_STRING, -1, -1,
                     static_cast<int64_t>(addr));
        if (i >= 0) sval[i] = s;
        return i;
    }

    // Emit an integer constant.
    int emit_iconst(int64_t v) {
        return emit(HIR_ICONST, TY_INT, -1, -1, v);
    }

    // Emit a function call with arguments.
    int emit_call(hir_type ret_ty, int fidx,
                  const int *args, int nargs) {
        int i = emit(HIR_CALL, ret_ty);
        if (i < 0) return -1;
        if (n_cargs + nargs > HIR_MAX_CARGS) return -1;
        func_idx[i] = fidx;
        cbase[i] = n_cargs;
        cnargs[i] = nargs;
        for (int j = 0; j < nargs; j++) {
            carg[n_cargs++] = args[j];
        }
        return i;
    }

    // Emit a strcat with arguments.
    int emit_strcat(const int *args, int nargs) {
        int i = emit(HIR_STRCAT, TY_STRING);
        if (i < 0) return -1;
        if (n_cargs + nargs > HIR_MAX_CARGS) return -1;
        cbase[i] = n_cargs;
        cnargs[i] = nargs;
        for (int j = 0; j < nargs; j++) {
            carg[n_cargs++] = args[j];
        }
        return i;
    }

    // Is instruction i a compile-time constant?
    bool is_const(int i) const {
        return i >= 0 && (kind[i] == HIR_ICONST || kind[i] == HIR_SCONST);
    }

    // Is instruction i provably integer-valued?
    bool is_int(int i) const {
        if (i < 0) return false;
        if (ty[i] == TY_INT) return true;
        if (known_int[i]) return true;
        // SCONST that parses as integer.
        if (kind[i] == HIR_SCONST && !sval[i].empty()) {
            const char *s = sval[i].c_str();
            if (*s == '-') s++;
            if (*s == '\0') return false;
            while (*s >= '0' && *s <= '9') s++;
            return *s == '\0';
        }
        return false;
    }

    // Get string value of a constant (SCONST or ICONST formatted).
    std::string const_str(int i) const {
        if (kind[i] == HIR_SCONST) return sval[i];
        if (kind[i] == HIR_ICONST) return std::to_string(val[i]);
        return "";
    }
};

#endif // HIR_H
