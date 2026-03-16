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
    HIR_DIV,        // src1 / src2 (signed integer division)
    HIR_REM,        // src1 % src2
    HIR_NEG,        // -src1
    HIR_ABS,        // |src1| (branchless absolute value)
    HIR_SIGN,       // sign(src1): -1, 0, or 1
    HIR_MAX,        // max(src1, src2)
    HIR_MIN,        // min(src1, src2)

    // Comparison (native RV64, result is int 0/1)
    HIR_EQ,         // src1 == src2
    HIR_NE,         // src1 != src2
    HIR_LT,         // src1 < src2
    HIR_LE,         // src1 <= src2
    HIR_GT,         // src1 > src2
    HIR_GE,         // src1 >= src2

    // Logic
    HIR_NOT,        // !src1 (int → int)
    HIR_BOOL,       // t(src1): 0→0, nonzero→1 (SNEZ)

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
    HIR_PHI,        // phi node: val = %q register number

    // Memory (M2+)
    HIR_LOAD_Q,     // load %q register: val = register number
    HIR_STORE_Q,    // store %q register: val = register number, src1 = value

    // Q-register sync: write to both SUBST slot and mudstate.
    HIR_SETQ_SYNC,  // val = register number (0-9), src1 = value (string addr)

    // Control flow (M2+)
    HIR_BR,         // unconditional branch: val = target block
    HIR_BRC,        // conditional branch: src1 = cond, val = true block, src2 = false block

    HIR_NUM_KINDS
};

const char *hir_kind_name(hir_kind k);

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
static constexpr int HIR_MAX_PARGS  = 4096;
static constexpr int HIR_MAX_PREDS  = 2048;
static constexpr int HIR_NUM_QREGS  = 14;   // 0-9 = user %q, 10-13 = compiler internal

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
    std::string call_name[HIR_MAX_INSNS];

    // Known-integer flag: true if a TY_STRING result is known to
    // parse as an integer (e.g., ECALL result from strlen/eq/gt).
    bool known_int[HIR_MAX_INSNS];

    // Runtime-reference flag: true if an SCONST points to a
    // runtime-populated address (CARGS_BASE, SUBST_BASE) rather
    // than a true compile-time constant.  Prevents constant folding.
    bool runtime_ref[HIR_MAX_INSNS];

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

    // For HIR_CALL: Tier 2 blob guest address, or 0 for ECALL.
    uint64_t tier2_addr[HIR_MAX_INSNS];

    // PHI arguments (flattened array, M2+).
    // For HIR_PHI at instruction i:
    //   pbase[i] = starting index into pblk[]/pval[]
    //   pnargs[i] = number of PHI arguments
    //   pblk[pbase[i]+j] = predecessor block
    //   pval[pbase[i]+j] = value (insn index) from that predecessor
    //
    int pblk[HIR_MAX_PARGS];
    int pval[HIR_MAX_PARGS];
    int pbase[HIR_MAX_INSNS];
    int pnargs[HIR_MAX_INSNS];
    int n_pargs;

    // ---------------------------------------------------------------
    // Basic blocks (M2+; M1 uses block 0 only).
    // ---------------------------------------------------------------

    int n_blocks;
    int cur_block;                          // current block during lowering
    int block_first[HIR_MAX_BLOCKS];    // first insn in block (computed by hir_build_cfg)
    int block_last[HIR_MAX_BLOCKS];     // last insn in block (inclusive, computed by hir_build_cfg)

    // CFG edges.
    int block_succ[HIR_MAX_BLOCKS][2];  // successors (-1 = none)
    int block_nsucc[HIR_MAX_BLOCKS];    // number of successors (0-2)

    // Predecessor list (flattened).
    int pred_list[HIR_MAX_PREDS];
    int pred_base[HIR_MAX_BLOCKS];
    int n_pred[HIR_MAX_BLOCKS];
    int n_pred_total;

    // Reverse post order.
    int rpo[HIR_MAX_BLOCKS];            // blocks in RPO
    int rpo_pos[HIR_MAX_BLOCKS];        // position in RPO for each block
    int n_rpo;

    // Dominator tree.
    int idom[HIR_MAX_BLOCKS];           // immediate dominator (-1 = none)

    // Final result instruction index.
    int result;

    // Statistics.
    int folds;
    int ecalls;
    int tier2_calls;
    int native_ops;
    bool needs_jit;

    void init() {
        n_insns = 0;
        n_cargs = 0;
        n_pargs = 0;
        n_blocks = 1;
        cur_block = 0;
        n_pred_total = 0;
        n_rpo = 0;
        result = -1;
        folds = 0;
        ecalls = 0;
        tier2_calls = 0;
        native_ops = 0;
        needs_jit = false;

        // Initialize block 0.
        block_succ[0][0] = block_succ[0][1] = -1;
        block_nsucc[0] = 0;
        pred_base[0] = 0;
        n_pred[0] = 0;
        idom[0] = -1;
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
        blk[i] = cur_block;
        cbase[i] = 0;
        cnargs[i] = 0;
        func_idx[i] = 0;
        tier2_addr[i] = 0;
        pbase[i] = 0;
        pnargs[i] = 0;
        sval[i].clear();
        call_name[i].clear();
        known_int[i] = false;
        runtime_ref[i] = false;
        return i;
    }

    // Emit a string constant.
    int emit_sconst(uint64_t addr, const std::string &s) {
        int i = emit(HIR_SCONST, TY_STRING, -1, -1,
                     static_cast<int64_t>(addr));
        if (i >= 0) sval[i] = s;
        return i;
    }

    // Emit a runtime string reference (CARGS/SUBST slot).
    // Same as emit_sconst but marked as non-constant to prevent folding.
    int emit_sref(uint64_t addr) {
        int i = emit_sconst(addr, "");
        if (i >= 0) runtime_ref[i] = true;
        return i;
    }

    // Emit an integer constant.
    int emit_iconst(int64_t v) {
        return emit(HIR_ICONST, TY_INT, -1, -1, v);
    }

    // Emit a function call with arguments.
    int emit_call(hir_type ret_ty, int fidx,
                  const int *args, int nargs,
                  const std::string *fallback_name = nullptr) {
        int i = emit(HIR_CALL, ret_ty);
        if (i < 0) return -1;
        if (n_cargs + nargs > HIR_MAX_CARGS) return -1;
        func_idx[i] = fidx;
        if (fallback_name && fidx == 0) {
            call_name[i] = *fallback_name;
        }
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

    // Emit a PHI node with arguments.
    int emit_phi(hir_type t, int qreg,
                 const int *blocks, const int *vals, int nargs) {
        int i = emit(HIR_PHI, t, -1, -1, qreg);
        if (i < 0) return -1;
        if (n_pargs + nargs > HIR_MAX_PARGS) return -1;
        pbase[i] = n_pargs;
        pnargs[i] = nargs;
        for (int j = 0; j < nargs; j++) {
            pblk[n_pargs] = blocks[j];
            pval[n_pargs] = vals[j];
            n_pargs++;
        }
        return i;
    }

    // Allocate a new basic block.  Returns block index.
    // Does NOT switch cur_block — caller must set it explicitly.
    int new_block() {
        if (n_blocks >= HIR_MAX_BLOCKS) return -1;
        int b = n_blocks++;
        block_succ[b][0] = block_succ[b][1] = -1;
        block_nsucc[b] = 0;
        pred_base[b] = 0;
        n_pred[b] = 0;
        idom[b] = -1;
        return b;
    }

    // Add a CFG edge from block src to block dst.
    void add_edge(int src, int dst) {
        if (block_nsucc[src] < 2) {
            block_succ[src][block_nsucc[src]++] = dst;
        }
    }

    // Is instruction i a compile-time constant?
    bool is_const(int i) const {
        return i >= 0 && (kind[i] == HIR_ICONST || kind[i] == HIR_SCONST)
               && !runtime_ref[i];
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

// SSA construction (hir_ssa.cpp).
void hir_build_cfg(hir_program &h);
void hir_ssa_construct(hir_program &h);

// SSA optimization (hir_opt.cpp).
void hir_const_fold(hir_program &h);
void hir_copy_prop(hir_program &h);
void hir_cse(hir_program &h);
void hir_dce(hir_program &h);
void hir_licm(hir_program &h);
void hir_optimize(hir_program &h);

#endif // HIR_H
