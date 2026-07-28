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
    HIR_FCONST,     // float constant: fval = value

    // Integer arithmetic (native RV64I/M)
    HIR_ADD,        // src1 + src2
    HIR_SUB,        // src1 - src2
    HIR_MUL,        // src1 * src2
    HIR_DIV,        // src1 / src2 (signed integer division)
    HIR_REM,        // src1 % src2
    HIR_NEG,        // -src1
    // HIR_ABS removed (#1256): softcode abs() is float (fabs); the integer
    // branchless ABS codegen corrupted INT64_MIN into non-numeric text, and
    // nothing emitted HIR_ABS after #1150.  Future native iabs() must use a
    // guarded lowering (#1114), not resurrect this opcode.
    HIR_SIGN,       // sign(src1): -1, 0, or 1
    HIR_MAX,        // max(src1, src2)
    HIR_MIN,        // min(src1, src2)

    // Bitwise (native RV64)
    HIR_BAND,       // src1 & src2
    HIR_BOR,        // src1 | src2
    HIR_BXOR,       // src1 ^ src2
    HIR_BNOT,       // ~src1
    HIR_SHL,        // src1 << src2
    HIR_SHR,        // src1 >> src2 (logical)

    // Float arithmetic (native RV64D)
    HIR_FADD,       // src1 + src2 (double)
    HIR_FSUB,       // src1 - src2 (double)
    HIR_FMUL,       // src1 * src2 (double)
    HIR_FDIV,       // src1 / src2 (double)
    HIR_FNEG,       // -src1 (double)
    HIR_FSQRT,      // sqrt(src1) (double)

    // Integer comparison (native RV64, result is int 0/1)
    HIR_EQ,         // src1 == src2
    HIR_NE,         // src1 != src2
    HIR_LT,         // src1 < src2
    HIR_LE,         // src1 <= src2
    HIR_GT,         // src1 > src2
    HIR_GE,         // src1 >= src2

    // Float comparison (native RV64D, result is int 0/1)
    HIR_FEQ,        // src1 == src2 (double)
    HIR_FLT,        // src1 < src2 (double)
    HIR_FLE,        // src1 <= src2 (double)

    // Logic
    HIR_NOT,        // !src1 (int → int)
    HIR_BOOL,       // t(src1): 0→0, nonzero→1 (SNEZ)

    // Unary arithmetic
    HIR_INC,        // src1 + 1
    HIR_DEC,        // src1 - 1

    // Type conversion
    HIR_ATOI,       // string → int (inline RV64 atoi)
    HIR_ITOA,       // int → string (inline RV64 itoa)
    HIR_ITOF,       // int → float (FCVT.D.L)
    HIR_FTOI,       // float → int (FCVT.L.D, truncate toward zero)
    HIR_FTOA,       // float → string (ECALL-based)
    HIR_LUA_FTOA,   // float → string, Lua's tostring rules (#1488)
    HIR_ATOF,       // string → float (ECALL-based)

    // String comparison
    HIR_STRCMP,      // strcmp(src1, src2): -1/0/1 (inline RV64 byte compare)

    // Lua table access (dedicated ECALL, integer fast-path)
    //
    // "no string marshal" is the whole point.  The alternative mechanism --
    // a named HIR_CALL lowered to an ECALL that passed a Lua stack INDEX
    // through guest memory as a decimal string -- is what made `#t` answer
    // 22 (#1424): a string "22" is indistinguishable from the value 22, so
    // an index flowed onward as though it were the thing it points at.
    // These carry the index in a register as a typed value instead, which
    // lua_is_handle can then refuse to let escape into arithmetic (#1579).
    // The named mechanism is gone (#1519); this is the only one.
    //
    HIR_LUA_NEWTABLE, // newtable(narr, nrec): ECALL → TY_LUA_HANDLE
    HIR_LUA_LEN,    // len(tbl_idx): ECALL → TY_INT, Lua's # on a table
    HIR_LUA_GETGLOBAL, // getglobal(key_addr): ECALL → TY_LUA_HANDLE
    HIR_LUA_GETFIELD_REF, // getfield(tbl_idx, key_addr): ECALL → TY_LUA_HANDLE
    HIR_LUA_CALL_INT, // call(fn_idx, nargs, args_addr): ECALL → TY_INT
    HIR_LUA_CALL_STR, // call(fn_idx, args): ECALL → TY_STRING into an out slot
    HIR_LUA_GETFIELD, // getfield(tbl_idx, key_addr): ECALL → TY_INT
    HIR_LUA_GETFIELD_FLT, // getfield(tbl_idx, key_addr): ECALL → TY_FLOAT (bits over FMV lane)
    HIR_LUA_SETFIELD, // setfield(tbl_idx, key_addr, val): ECALL
    HIR_LUA_GETI,   // geti(tbl_idx, key): ECALL → TY_INT (no string marshal)
    HIR_LUA_SETI,   // seti(tbl_idx, key, val): ECALL (no string marshal)
    HIR_LUA_ALOAD,  // native array load: val[key] from pinned array (no ECALL)

    // Function calls
    HIR_CALL,       // ECALL to engine function
    HIR_STRCAT,     // concatenate N strings via ECALL

    // Direct FP calls to blob intrinsics (type-propagated path).
    // Bypass string marshalling — args and result are TY_FLOAT.
    // val = blob guest address of the target function.
    // func_idx = FMATH_* selector (for constant folding in optimizer).
    HIR_FCALL1,     // unary:  result = f(src1)        (sin, cos, etc.)
    HIR_FCALL2,     // binary: result = f(src1, src2)   (pow, atan2, fmod)

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
    TY_INT,         // 64-bit integer (in RV64 integer register)
    TY_FLOAT,       // 64-bit double (in RV64 FP register)
    TY_STRING,      // string (guest memory address)

    // A reference into the Lua VM -- a stack index, not a value (#1579).
    //
    // The members above name where a value *lives*, which is the right
    // lattice for MUSHCode, where everything is semantically a string.  Lua
    // is typed, and forcing it through a representation lattice is what made
    // a global holding the integer 22 and a table at stack index 22 come back
    // byte-identical: both were TY_STRING.  `#t` answering 22 was that.
    //
    // Representationally this is still a string buffer, so codegen treats it
    // exactly like TY_STRING and needs no new machinery.  Semantically it is
    // opaque: illegal in arithmetic, comparison, length and concatenation,
    // and illegal as a returned value.  Those uses now decline at lowering
    // time instead of reaching the bridge.
    //
    // That rejection is the point.  #1518 currently prevents the same wrong
    // answers, but only incidentally -- it fails closed because the bridge
    // ECALL names are unimplemented, and that protection evaporates the
    // moment #1519 implements them.  This makes it a property of the type.
    //
    TY_LUA_HANDLE,
};

// ---------------------------------------------------------------
// FMATH selector — identifies which libm function an FCALL1/FCALL2
// represents, enabling compile-time constant folding in the optimizer.
// ---------------------------------------------------------------

enum fmath_id {
    FMATH_NONE = 0,
    // Unary (FCALL1)
    FMATH_SIN, FMATH_COS, FMATH_TAN,
    FMATH_ASIN, FMATH_ACOS, FMATH_ATAN,
    FMATH_EXP, FMATH_LOG, FMATH_LOG10,
    FMATH_SQRT, FMATH_CEIL, FMATH_FLOOR, FMATH_FABS,
    // Binary (FCALL2)
    FMATH_POW, FMATH_ATAN2, FMATH_FMOD,
    FMATH_FMAX, FMATH_FMIN,  // max()/min() float path (#1273)
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

// ---------------------------------------------------------------
// hir_slot — a parallel array that refuses an out-of-range index
// ---------------------------------------------------------------
//
// The lowering signals refusal by returning -1: hir_lower_node for an AST node
// it will not compile (#1242), emit/emit_call/emit_strcat/emit_phi/new_block on
// a capacity limit.  Every consumer that indexes a per-instruction array with
// such a value must check first, and three separate rounds of per-site guards
// (#1440, #1449, #1457/#1470) each found producers the previous round had
// missed.  #1501 is the argument for stopping there.
//
// The reason it kept recurring is that the guards are invisible at the point of
// danger: nothing about `h.ty[p]` says p might be -1, so a fast path added ahead
// of an existing guard is unprotected by construction.  That is exactly what
// #1470 found -- builtin fast paths indexing the args before the guarded ECALL
// fall-through reached them -- and near-identical loops 80 lines apart had also
// drifted (#1457).
//
// So the check moves into the subscript, where it cannot be forgotten:
//
//   * A negative or too-large index yields a scratch element instead of reading
//     or writing past the array.  `h.ty[-1]` was reading the last int of the
//     preceding member and `h.known_int[-1] = true` was writing to it.
//   * The access is recorded, and refused_index() makes the whole program
//     unusable.  This half is what keeps the change honest: making a bad
//     subscript merely *defined* would turn a sanitizer report into a silently
//     wrong compile, which is worse than the bug.  Refusing declines the
//     compile and the AST evaluator answers, the same contract h.overflowed
//     already has.
//
// Deliberately not mux_assert: it aborts in release builds, and the project is
// moving away from it.  Declining is the failure mode this codebase wants.
//
// operator[] takes int, not size_t, on purpose -- an implicit conversion of -1
// to size_t at the call site would make the sign test unreachable.
//
// The element parameter is ElemT, not T: mux_nls.h defines a function-style
// macro T(x) for the UTF-8 cast, so a parameter named T turns `T()` into
// `(reinterpret_cast<const UTF8 *>())` and the header stops compiling.
//
template <typename ElemT, int N>
struct hir_slot {
    ElemT &operator[](int i) {
        if (static_cast<unsigned int>(i) >= static_cast<unsigned int>(N)) {
            refused = true;
            scratch = ElemT();
            return scratch;
        }
        return data[i];
    }
    const ElemT &operator[](int i) const {
        if (static_cast<unsigned int>(i) >= static_cast<unsigned int>(N)) {
            refused = true;
            scratch = ElemT();
            return scratch;
        }
        return data[i];
    }

    ElemT data[N];

    // Absorbs a refused access.  Reset on every refusal so a read cannot see
    // what an earlier refused write left behind.
    mutable ElemT scratch;

    // mutable so a const subscript can still record the refusal; a const read
    // through a bad index is exactly as much of a defect as a write.
    //
    // Initialised here as well as in hir_program::init(), so a slot that is
    // ever added without being added to HIR_INSN_SLOTS still starts clean.
    // Both compile entry points construct a fresh hir_program and call init()
    // exactly once, so the two are redundant today by design.
    mutable bool refused = false;
};

// hir_slot hands back a value-initialised element for a refused subscript, so
// what T() means decides whether a refused read can be mistaken for real data.
// Both enums are ordered so that zero is the inert case -- HIR_NOP is not a
// constant, TY_VOID is not a value-carrying type -- which keeps a refused read
// from steering a consumer down a live branch before refused_index() is
// consulted.  Asserted rather than assumed, because it is an ordering property
// of two enums maintained for other reasons (#1501).
//
static_assert(HIR_NOP == static_cast<hir_kind>(0),
              "hir_slot's refused element must not read as a live hir_kind");
static_assert(TY_VOID == static_cast<hir_type>(0),
              "hir_slot's refused element must not read as a live hir_type");

// The per-instruction slots, listed once and expanded twice: the refusal reset
// in init() and the aggregation in refused_index().
//
// The declarations themselves are deliberately NOT generated from this list --
// several carry documentation worth more than the deduplication, and burying it
// in a macro body would cost more than it saves.  So the list is the single
// place that reset and aggregation agree on, not a single source of truth for
// the struct: adding a slot means adding it here too.  `refused` also has a
// default member initialiser, so a slot missing from this list still starts
// false on a fresh program -- it just would not be reported.
//
#define HIR_INSN_SLOTS(X)          \
    X(hir_kind, kind)              \
    X(hir_type, ty)                \
    X(int,      src1)              \
    X(int,      src2)              \
    X(int64_t,  val)               \
    X(int,      blk)               \
    X(bool,     known_int)         \
    X(bool,     known_float)       \
    X(bool,     runtime_ref)       \
    X(double,   fval)              \
    X(int,      cbase)             \
    X(int,      cnargs)            \
    X(int,      func_idx)          \
    X(uint64_t, tier2_addr)        \
    X(int,      pbase)             \
    X(int,      pnargs)

struct hir_program {
    // Per-instruction arrays.  See HIR_INSN_SLOTS / hir_slot above: the
    // subscript refuses an out-of-range index rather than trusting callers.
    hir_slot<hir_kind, HIR_MAX_INSNS> kind;
    hir_slot<hir_type, HIR_MAX_INSNS> ty;
    hir_slot<int, HIR_MAX_INSNS>      src1;   // operand 1 (insn index, -1 = none)
    hir_slot<int, HIR_MAX_INSNS>      src2;   // operand 2 (insn index, -1 = none)
    hir_slot<int64_t, HIR_MAX_INSNS>  val;    // immediate or metadata
    hir_slot<int, HIR_MAX_INSNS>      blk;    // containing basic block

    // String values for SCONST (compile-time known strings).
    // Indexed by instruction index.  Empty for non-SCONST insns.
    // Vectors sized to n_insns — avoids constructing 4096 strings.
    std::vector<std::string> sval;
    std::vector<std::string> call_name;

    // Guest addresses of every runtime substitution/carg reference emitted
    // (via emit_sref).  Classified by compile_expression into a per-program
    // mask so run_cached_program populates only the CARGS/SUBST slots the
    // program actually reads, not all ~45 of them every call.
    std::vector<uint64_t> sref_addrs;

    // Known-integer flag: true if a TY_STRING result is known to
    // parse as an integer (e.g., ECALL result from strlen/eq/gt).
    hir_slot<bool, HIR_MAX_INSNS> known_int;

    // Known-float flag: true if a TY_STRING result is known to
    // parse as a floating-point number (e.g., ECALL result from
    // sin/cos/fdiv).  Enables downstream float promotion without
    // runtime string parsing.
    hir_slot<bool, HIR_MAX_INSNS> known_float;

    // Runtime-reference flag: true if an SCONST points to a
    // runtime-populated address (CARGS_BASE, SUBST_BASE) rather
    // than a true compile-time constant.  Prevents constant folding.
    hir_slot<bool, HIR_MAX_INSNS> runtime_ref;

    // Float values for FCONST (compile-time known doubles).
    hir_slot<double, HIR_MAX_INSNS> fval;

    int n_insns;

    // Call arguments (flattened array).
    // For HIR_CALL/HIR_STRCAT at instruction i:
    //   cbase[i] = starting index into carg[]
    //   cnargs[i] = number of arguments
    //   carg[cbase[i]..cbase[i]+cnargs[i]-1] = argument insn indices
    //
    int carg[HIR_MAX_CARGS];
    hir_slot<int, HIR_MAX_INSNS> cbase;
    hir_slot<int, HIR_MAX_INSNS> cnargs;
    int n_cargs;

    // For HIR_CALL: function index (engine_api index) or 0 for string-based.
    hir_slot<int, HIR_MAX_INSNS> func_idx;

    // For HIR_CALL: Tier 2 blob guest address, or 0 for ECALL.
    hir_slot<uint64_t, HIR_MAX_INSNS> tier2_addr;

    // PHI arguments (flattened array, M2+).
    // For HIR_PHI at instruction i:
    //   pbase[i] = starting index into pblk[]/pval[]
    //   pnargs[i] = number of PHI arguments
    //   pblk[pbase[i]+j] = predecessor block
    //   pval[pbase[i]+j] = value (insn index) from that predecessor
    //
    int pblk[HIR_MAX_PARGS];
    int pval[HIR_MAX_PARGS];
    hir_slot<int, HIR_MAX_INSNS> pbase;
    hir_slot<int, HIR_MAX_INSNS> pnargs;
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

    // DFS timestamps on the dominator tree for O(1) dominance queries.
    // dominates(a, b) iff dom_in[a] <= dom_in[b] && dom_out[b] <= dom_out[a].
    int dom_in[HIR_MAX_BLOCKS];
    int dom_out[HIR_MAX_BLOCKS];

    // Final result instruction index.
    int result;

    // Statistics.
    int folds;
    int ecalls;
    int tier2_calls;
    int native_ops;
    bool needs_jit;

    // Extra static watermarks from inlined u()/ulocal() bodies (#1056).
    // Outer AST max-depth / funccall-count miss the body; accumulate
    // body depth/count here so compile_expression can add them to the
    // program watermarks used by run_cached_program / jit_eval.
    int inline_extra_depth;
    int inline_extra_calls;

    // Set when any capacity limit (HIR_MAX_INSNS/BLOCKS/PARGS/CARGS) is
    // hit during lowering.  The -1 an overflowing emit/new_block returns
    // otherwise flows into instruction/block indices unchecked (#859);
    // compile_expression checks this and aborts to the interpreter.
    bool overflowed;

    // True once any per-instruction subscript refused an out-of-range index
    // (#1501).  Read alongside overflowed at every point that decides whether
    // a compile may proceed; the two mean the same thing to a caller -- this
    // program is not safe to use -- and differ only in what noticed.
    //
    // overflowed is set by the *producer* of a -1, so it catches capacity
    // limits and the #1242 unknown-node refusal.  This one is set by the
    // *consumer*, so it also catches a refusal that no producer flagged, which
    // is the half the per-site guards kept missing.
    //
    bool refused_index() const {
        bool r = false;
#define HIR_SLOT_OR(ElemT, name) r = r || name.refused;
        HIR_INSN_SLOTS(HIR_SLOT_OR)
#undef HIR_SLOT_OR
        return r;
    }

    void init() {
#define HIR_SLOT_RESET(ElemT, name) name.refused = false;
        HIR_INSN_SLOTS(HIR_SLOT_RESET)
#undef HIR_SLOT_RESET
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
        inline_extra_depth = 0;
        inline_extra_calls = 0;
        overflowed = false;

        sval.clear();
        call_name.clear();
        sref_addrs.clear();

        // Initialize block 0.
        block_first[0] = 0;
        block_last[0] = -1;  // empty until hir_build_cfg computes ranges
        block_succ[0][0] = block_succ[0][1] = -1;
        block_nsucc[0] = 0;
        pred_base[0] = 0;
        n_pred[0] = 0;
        idom[0] = -1;
        n_pred[0] = 0;
        idom[0] = -1;
    }

    // Emit an instruction, return its index.
    int emit(hir_kind k, hir_type t, int s1 = -1, int s2 = -1,
             int64_t v = 0) {
        if (n_insns >= HIR_MAX_INSNS) { overflowed = true; return -1; }
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
        sval.emplace_back();
        call_name.emplace_back();
        known_int[i] = false;
        known_float[i] = false;
        runtime_ref[i] = false;
        fval[i] = 0.0;
        return i;
    }

    // Emit a float constant.
    int emit_fconst(double v) {
        int i = emit(HIR_FCONST, TY_FLOAT);
        if (i >= 0) fval[i] = v;
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
    // Only record sref_addrs / runtime_ref on success so capacity overflow
    // does not leave a guest address that was never emitted.
    int emit_sref(uint64_t addr) {
        int i = emit_sconst(addr, "");
        if (i >= 0) {
            runtime_ref[i] = true;
            sref_addrs.push_back(addr);
        }
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
        if (n_cargs + nargs > HIR_MAX_CARGS) { overflowed = true; return -1; }
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
        if (n_cargs + nargs > HIR_MAX_CARGS) { overflowed = true; return -1; }
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
        if (n_pargs + nargs > HIR_MAX_PARGS) { overflowed = true; return -1; }
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
        if (n_blocks >= HIR_MAX_BLOCKS) { overflowed = true; return -1; }
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

    // Is instruction i provably float-valued?
    bool is_float(int i) const {
        if (i < 0) return false;
        if (ty[i] == TY_FLOAT) return true;
        if (known_float[i]) return true;
        // SCONST that parses as a float (contains '.', 'e', or 'E').
        if (kind[i] == HIR_SCONST && !sval[i].empty()) {
            const char *s = sval[i].c_str();
            if (*s == '-' || *s == '+') s++;
            if (*s == '\0') return false;
            bool has_digit = false;
            bool has_dot = false;
            while ((*s >= '0' && *s <= '9') || *s == '.') {
                if (*s == '.') has_dot = true;
                else has_digit = true;
                s++;
            }
            if (*s == 'e' || *s == 'E') {
                s++;
                if (*s == '+' || *s == '-') s++;
                while (*s >= '0' && *s <= '9') s++;
            }
            return has_digit && *s == '\0';
        }
        return false;
    }

    // Is instruction i provably numeric (int or float)?
    bool is_numeric(int i) const {
        return is_int(i) || is_float(i);
    }

    // Get string value of a constant (SCONST or ICONST formatted).
    //
    // Guarded explicitly rather than leaning on the slot: sval is a
    // std::vector, so sval[-1] is undefined behaviour that hir_slot cannot
    // intercept.  Reaching it requires kind[i] to read as HIR_SCONST, which a
    // refused subscript will not do -- but that is a property of HIR_NOP being
    // the zero value, which is exactly the kind of thing an enum reorder
    // silently changes.  The static_asserts below pin it; this guard means the
    // function is correct even if they are ever relaxed.
    //
    std::string const_str(int i) const {
        if (i < 0 || i >= n_insns) return "";
        if (kind[i] == HIR_SCONST) return sval[i];
        if (kind[i] == HIR_ICONST) return std::to_string(val[i]);
        return "";
    }
};

// Third-operand accessor.
//
// Almost every instruction keeps its operands in src1/src2, with the
// CALL/STRCAT and PHI side arrays as the two documented exceptions that
// operand walkers already special-case.  HIR_LUA_SETI is a third
// exception and a much easier one to miss: seti/setfield park the VALUE
// here; CALL_INT packs nargs in the low 8 bits and the *second* arg's
// instruction index above that (src2 holds the first arg).  A walker that
// misses either treats the operand as dead -- DCE / regalloc reuse -- while
// the ECALL still expects it (#1711 was that shape for field keys).
//
// val[] is a plain integer for every other opcode -- for HIR_LUA_ALOAD it
// is a guest ADDRESS -- so this must stay strictly gated on the opcode.
//
// Returns the operand's instruction index, or -1 when there is none.
// CALL_INT exposes only arg1 this way; a true N-ary operand list is the
// longer-term fix the #1519 call path argues for.
//
// Operand slots, so a pass can name where an operand LIVES without knowing
// which opcode put it there (#1519).
//
// Every operand of every instruction is reachable through hir_operand_count
// / hir_operand_get / hir_operand_set.  Passes that walk operands --
// copy propagation, DCE, liveness in the register allocator -- used to
// hand-roll the same sequence: src1, src2, hir_val_operand(), then the
// carg[] loop.  Three copies of one layout, and each new operand shape had
// to be added to all three.  CALL_INT's second argument was invisible to
// liveness for exactly that reason until 20d39472f.
//
// Adding a shape now means teaching these three functions, once.
//
enum hir_operand_slot {
    HIR_SLOT_SRC1 = 0,
    HIR_SLOT_SRC2 = 1,
    HIR_SLOT_VAL  = 2,   // SETI/SETFIELD value, CALL_INT arg1 (packed)
    HIR_SLOT_ARG  = 3,   // carg[]/pbase[] lists start here
};

inline int hir_operand_count(const hir_program &h, int i);
inline int hir_operand_get(const hir_program &h, int i, int slot);
inline void hir_operand_set(hir_program &h, int i, int slot, int r);

inline int hir_val_operand(const hir_program &h, int i) {
    if (i < 0 || i >= h.n_insns) return -1;
    if (h.kind[i] == HIR_LUA_SETI || h.kind[i] == HIR_LUA_SETFIELD) {
        int v = static_cast<int>(h.val[i]);
        return (v >= 0 && v < h.n_insns) ? v : -1;
    }
    if (h.kind[i] == HIR_LUA_CALL_INT || h.kind[i] == HIR_LUA_CALL_STR) {
        // One layout for both: low 8 = nargs, next 8 = argkind bits, from
        // 16 up = a1_insn + 1 (0 meaning "no second argument").  They differ
        // in RESULT type only.
        //
        int a1i = static_cast<int>(h.val[i] >> 16) - 1;
        return (a1i >= 0 && a1i < h.n_insns) ? a1i : -1;
    }
    return -1;
}

// Number of operand slots on instruction i.  ARG slots follow the fixed
// three, so slot >= HIR_SLOT_ARG indexes carg[]/pbase[].
//
inline int hir_operand_count(const hir_program &h, int i) {
    if (i < 0 || i >= h.n_insns) return 0;
    int n = HIR_SLOT_ARG;
    if (h.kind[i] == HIR_CALL || h.kind[i] == HIR_STRCAT) {
        n += h.cnargs[i];
    } else if (h.kind[i] == HIR_PHI) {
        n += h.pnargs[i];
    }
    return n;
}

inline int hir_operand_get(const hir_program &h, int i, int slot) {
    if (i < 0 || i >= h.n_insns) return -1;
    switch (slot) {
    case HIR_SLOT_SRC1: return h.src1[i];
    case HIR_SLOT_SRC2:
        // BRC keeps a BLOCK NUMBER in src2, not an instruction reference.
        // DCE knew that and skipped it; nothing else did.  Encoding it here
        // is the point of this accessor -- a pass that walks operands
        // should not have to know which opcodes lie about their fields.
        if (h.kind[i] == HIR_BRC) return -1;
        return h.src2[i];
    case HIR_SLOT_VAL:  return hir_val_operand(h, i);
    default: break;
    }
    const int j = slot - HIR_SLOT_ARG;
    if (h.kind[i] == HIR_CALL || h.kind[i] == HIR_STRCAT) {
        if (j < 0 || j >= h.cnargs[i]) return -1;
        return h.carg[h.cbase[i] + j];
    }
    if (h.kind[i] == HIR_PHI) {
        if (j < 0 || j >= h.pnargs[i]) return -1;
        return h.pval[h.pbase[i] + j];
    }
    return -1;
}

// Write an operand back in whatever encoding its slot uses.
//
// The VAL slot is why this exists rather than a bare pointer: SETI and
// SETFIELD keep a plain instruction index there, but CALL_INT PACKS nargs
// into the low 8 bits with the operand above it.  Copy propagation used to
// write `h.val[i] = r` unconditionally, which preserves neither -- a latent
// hazard rather than an observed failure (I could not construct a chunk
// where resolve_copy fires on a call argument), and precisely the kind that
// a per-pass hand-rolled walk keeps re-introducing.
//
inline void hir_operand_set(hir_program &h, int i, int slot, int r) {
    if (i < 0 || i >= h.n_insns) return;
    switch (slot) {
    case HIR_SLOT_SRC1: h.src1[i] = r; return;
    case HIR_SLOT_SRC2:
        if (h.kind[i] == HIR_BRC) return;   // block number, not an operand
        h.src2[i] = r;
        return;
    case HIR_SLOT_VAL:
        if (h.kind[i] == HIR_LUA_SETI || h.kind[i] == HIR_LUA_SETFIELD) {
            h.val[i] = r;
        } else if (h.kind[i] == HIR_LUA_CALL_INT
                || h.kind[i] == HIR_LUA_CALL_STR) {
            const int64_t low = h.val[i] & 0xFFFF;   // nargs + argkinds
            h.val[i] = low | (static_cast<int64_t>(r + 1) << 16);
        }
        return;
    default: break;
    }
    const int j = slot - HIR_SLOT_ARG;
    if (h.kind[i] == HIR_CALL || h.kind[i] == HIR_STRCAT) {
        if (j >= 0 && j < h.cnargs[i]) h.carg[h.cbase[i] + j] = r;
        return;
    }
    if (h.kind[i] == HIR_PHI) {
        if (j >= 0 && j < h.pnargs[i]) h.pval[h.pbase[i] + j] = r;
    }
}

// SSA construction (hir_ssa.cpp).
void hir_build_cfg(hir_program &h);
void hir_ssa_construct(hir_program &h);

// O(1) dominance query using DFS timestamps on the dominator tree.
// Returns true if block blk_d dominates block blk_b.
inline bool hir_dominates(const hir_program &h, int blk_d, int blk_b) {
    return h.dom_in[blk_d] <= h.dom_in[blk_b]
        && h.dom_out[blk_b] <= h.dom_out[blk_d];
}

// SSA optimization (hir_opt.cpp).
void hir_const_fold(hir_program &h);
void hir_copy_prop(hir_program &h);
void hir_gvn(hir_program &h);
void hir_dce(hir_program &h);
void hir_licm(hir_program &h);
void hir_peephole(hir_program &h);
void hir_superblock(hir_program &h);
void hir_optimize(hir_program &h);

#endif // HIR_H
