/*! \file hir_lower_lua.cpp
 * \brief Lua 5.4 bytecode → HIR lowering.
 *
 * Two-pass approach:
 *   Pass 1: scan for basic block boundaries (branch targets).
 *   Pass 2: walk opcodes, emit HIR instructions.
 *
 * Lua register map: lua_reg[i] holds the current HIR value number
 * for Lua register i.  Updated on each register write.
 *
 * Unsupported opcodes → return -1 (caller falls back to Lua VM).
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "dbt_compile.h"
#include "engine_api.h"
#include "lua_bytecode.h"
#include "hir_lower_lua.h"

#include <cstring>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <vector>
#include <string>

// Maximum Lua registers we track.
static constexpr int MAX_LUA_REGS = 256;

// Q-register slots for Lua for-loop variables.
// Reuses compiler-internal slots 10-12 (same as softcode iter()).
// Safe because Lua lowering never calls the softcode iter() path.
//
static constexpr int QREG_LUA_IDX = 10;   // loop index variable

// Maximum code size we attempt to JIT (prevents runaway compile time).
static constexpr int MAX_LUA_CODE_SIZE = 1024;

// Maximum Lua stack size (register pressure bound).
static constexpr int MAX_LUA_STACK = 64;

// Maximum parameters.
static constexpr int MAX_LUA_PARAMS = 8;

// ---------------------------------------------------------------
// Rejection reason names (for diagnostics).
// ---------------------------------------------------------------

const char *lua_bc_reject_name(lua_bc_reject reason) {
    switch (reason) {
    case LUA_BC_ELIGIBLE:          return "eligible";
    case LUA_BC_EMPTY:             return "empty proto";
    case LUA_BC_TOO_LARGE:         return "code or stack too large";
    case LUA_BC_TOO_MANY_PARAMS:   return "too many parameters";
    case LUA_BC_HAS_CLOSURE:       return "contains OP_CLOSURE";
    case LUA_BC_HAS_VARARG:        return "contains OP_VARARG";
    case LUA_BC_HAS_TBC:           return "contains OP_TBC";
    case LUA_BC_HAS_TAILCALL:      return "contains OP_TAILCALL";
    case LUA_BC_HAS_NESTED_PROTOS: return "has nested protos";
    case LUA_BC_UNSUPPORTED_OP:    return "unsupported opcode";
    case LUA_BC_HAS_NON_INT_CONST: return "non-integer float constant";
    }
    return "unknown";
}

// ---------------------------------------------------------------
// Eligibility pre-filter.
//
// This is a fast O(n) scan over the proto's instruction stream
// that rejects protos we know we cannot compile.  It runs before
// any HIR allocation so the reject path is cheap.
//
// The supported opcode set must exactly match what
// hir_lower_lua_proto() handles in its switch statement.
// ---------------------------------------------------------------

lua_bc_reject lua_bc_eligible(const lua_bc_proto *proto) {
    if (nullptr == proto) return LUA_BC_EMPTY;

    int n = static_cast<int>(proto->code.size());
    if (n == 0) return LUA_BC_EMPTY;

    // --- Header checks ---

    if (n > MAX_LUA_CODE_SIZE) return LUA_BC_TOO_LARGE;
    if (proto->maxstacksize > MAX_LUA_STACK) return LUA_BC_TOO_LARGE;
    if (proto->numparams > MAX_LUA_PARAMS) return LUA_BC_TOO_MANY_PARAMS;

    // Nested protos mean OP_CLOSURE will appear.  Reject early without
    // scanning — the bytecode can't reference protos that don't exist.
    if (!proto->protos.empty()) return LUA_BC_HAS_NESTED_PROTOS;

    // --- Opcode scan ---
    //
    // We maintain a whitelist of opcodes the lowering handles.
    // Anything outside the whitelist → reject.
    //
    // Note: VARARGPREP is harmless (adjusts stack for main chunks)
    // and is treated as a no-op.  OP_VARARG is the actual vararg
    // access instruction and is rejected.

    for (int pc = 0; pc < n; pc++) {
        int op = proto->code[pc].opcode();
        switch (op) {

        // Data movement — handled.
        case OP_LUA_MOVE:
        case OP_LUA_LOADI:
        case OP_LUA_LOADF:
        case OP_LUA_LOADK:
        case OP_LUA_LOADFALSE:
        case OP_LUA_LFALSESKIP:
        case OP_LUA_LOADTRUE:
        case OP_LUA_LOADNIL:
            break;

        // Arithmetic — handled (integer and float).
        case OP_LUA_ADD:
        case OP_LUA_SUB:
        case OP_LUA_MUL:
        case OP_LUA_DIV:
        case OP_LUA_IDIV:
        case OP_LUA_MOD:
        case OP_LUA_UNM:
        case OP_LUA_ADDI:
        case OP_LUA_ADDK:
        case OP_LUA_SUBK:
        case OP_LUA_MULK:
            break;

        // Metamethod companions — skipped as no-ops.
        case OP_LUA_MMBIN:
        case OP_LUA_MMBINI:
        case OP_LUA_MMBINK:
            break;

        // Comparisons — handled.
        case OP_LUA_EQ:
        case OP_LUA_LT:
        case OP_LUA_LE:
        case OP_LUA_EQI:
        case OP_LUA_LTI:
        case OP_LUA_LEI:
        case OP_LUA_GTI:
        case OP_LUA_GEI:
        case OP_LUA_TEST:
        case OP_LUA_TESTSET:
            break;

        // Control flow — handled.
        case OP_LUA_JMP:
        case OP_LUA_FORPREP:
        case OP_LUA_FORLOOP:
            break;

        // Return — handled.
        case OP_LUA_RETURN:
        case OP_LUA_RETURN0:
        case OP_LUA_RETURN1:
            break;

        // Table access: mux.* bridge pattern — handled.
        case OP_LUA_GETTABUP:
        case OP_LUA_GETFIELD:
        case OP_LUA_CALL:
            break;

        // Harmless no-op (main chunk vararg prep).
        case OP_LUA_VARARGPREP:
            break;

        // --- Hard rejects ---

        case OP_LUA_CLOSURE:
            return LUA_BC_HAS_CLOSURE;

        case OP_LUA_VARARG:
            return LUA_BC_HAS_VARARG;

        case OP_LUA_TBC:
            return LUA_BC_HAS_TBC;

        case OP_LUA_TAILCALL:
            return LUA_BC_HAS_TAILCALL;

        // --- Everything else is unsupported ---
        default:
            return LUA_BC_UNSUPPORTED_OP;
        }
    }

    return LUA_BC_ELIGIBLE;
}

// ---------------------------------------------------------------
// Pass 1: find basic block boundaries
// ---------------------------------------------------------------

static void find_block_starts(const lua_bc_proto *proto,
                               std::vector<bool> &is_leader) {
    int n = static_cast<int>(proto->code.size());
    is_leader.assign(n, false);
    if (n > 0) is_leader[0] = true;

    for (int pc = 0; pc < n; pc++) {
        const lua_bc_instruction &insn = proto->code[pc];
        int op = insn.opcode();

        switch (op) {
        case OP_LUA_JMP: {
            int target = pc + 1 + insn.sJ();
            if (target >= 0 && target < n) is_leader[target] = true;
            if (pc + 1 < n) is_leader[pc + 1] = true;
            break;
        }
        case OP_LUA_FORLOOP: {
            int target = pc + 1 + insn.sBx();
            if (target >= 0 && target < n) is_leader[target] = true;
            if (pc + 1 < n) is_leader[pc + 1] = true;
            break;
        }
        case OP_LUA_FORPREP: {
            int target = pc + 1 + insn.sBx();
            if (target >= 0 && target < n) is_leader[target] = true;
            if (pc + 1 < n) is_leader[pc + 1] = true;
            break;
        }
        case OP_LUA_EQ:
        case OP_LUA_LT:
        case OP_LUA_LE:
        case OP_LUA_EQI:
        case OP_LUA_LTI:
        case OP_LUA_LEI:
        case OP_LUA_GTI:
        case OP_LUA_GEI:
        case OP_LUA_TEST:
        case OP_LUA_TESTSET:
            if (pc + 1 < n) is_leader[pc + 1] = true;
            if (pc + 2 < n) is_leader[pc + 2] = true;
            break;
        case OP_LUA_RETURN:
        case OP_LUA_RETURN0:
        case OP_LUA_RETURN1:
            if (pc + 1 < n) is_leader[pc + 1] = true;
            break;
        default:
            break;
        }
    }
}

// ---------------------------------------------------------------
// Block mapping
// ---------------------------------------------------------------

static int assign_blocks(const std::vector<bool> &is_leader,
                          std::vector<int> &pc_to_block, int n) {
    int block_count = 0;
    pc_to_block.resize(n, -1);
    for (int pc = 0; pc < n; pc++) {
        if (is_leader[pc]) block_count++;
        pc_to_block[pc] = block_count - 1;
    }
    return block_count;
}

// ---------------------------------------------------------------
// Helper: load a Lua constant into HIR.
// ---------------------------------------------------------------

static int emit_lua_constant(hir_program &h, rv_compiler &rc,
                              const lua_bc_constant &k) {
    switch (k.type) {
    case LUA_BC_TNIL:
        return h.emit_sconst(rc.pool_str("", 0), "");
    case LUA_BC_TFALSE:
        return h.emit_iconst(0);
    case LUA_BC_TTRUE:
        return h.emit_iconst(1);
    case LUA_BC_TINT:
        return h.emit_iconst(k.ival);
    case LUA_BC_TFLOAT: {
        // Integer-valued floats (e.g. 2.0) → emit as ICONST for
        // compatibility with integer arithmetic.  Non-integer floats
        // (e.g. 3.14) → emit as FCONST.
        double v = k.fval;
        if (v == floor(v) && v >= -9.22e18 && v <= 9.22e18) {
            return h.emit_iconst(static_cast<int64_t>(v));
        }
        return h.emit_fconst(v);
    }
    case LUA_BC_TSHRSTR:
    case LUA_BC_TLNGSTR: {
        uint64_t addr = rc.pool_str(k.sval.c_str(), k.sval.size());
        return h.emit_sconst(addr, k.sval);
    }
    default:
        return -1;
    }
}

// ---------------------------------------------------------------
// Helper: promote an operand to TY_FLOAT if needed.
// Returns the (possibly new) HIR value index, or -1 on error.
// ---------------------------------------------------------------

static int promote_to_float(hir_program &h, int v) {
    if (v < 0) return -1;
    if (h.ty[v] == TY_FLOAT) return v;
    if (h.ty[v] == TY_INT) {
        return h.emit(HIR_ITOF, TY_FLOAT, v);
    }
    return -1;  // TY_STRING cannot be promoted.
}

// Returns true if either operand is TY_FLOAT (i.e., need float arithmetic).
//
static bool either_float(hir_program &h, int a, int b) {
    return (a >= 0 && h.ty[a] == TY_FLOAT)
        || (b >= 0 && h.ty[b] == TY_FLOAT);
}

// ---------------------------------------------------------------
// Helper: emit a comparison + branch pattern.
// Many Lua comparison opcodes share the same structure:
//   compare → optional negate (k bit) → read JMP → emit BRC
// ---------------------------------------------------------------

static int emit_cmp_branch(hir_program &h, int cmp, int k_bit,
                            const lua_bc_proto *proto, int pc,
                            const std::vector<int> &pc_to_block,
                            int cur_hir_block, int n) {
    if (cmp < 0) return -1;
    if (k_bit) {
        cmp = h.emit(HIR_NOT, TY_INT, cmp);
        if (cmp < 0) return -1;
    }
    if (pc + 1 >= n) return -1;
    const lua_bc_instruction &jmp_insn = proto->code[pc + 1];
    if (jmp_insn.opcode() != OP_LUA_JMP) return -1;
    int true_target = pc + 2 + jmp_insn.sJ();
    int false_target = pc + 2;
    int true_blk = (true_target >= 0 && true_target < n) ? pc_to_block[true_target] : -1;
    int false_blk = (false_target >= 0 && false_target < n) ? pc_to_block[false_target] : -1;
    if (true_blk < 0 || false_blk < 0) return -1;
    h.emit(HIR_BRC, TY_VOID, cmp, false_blk, true_blk);
    h.add_edge(cur_hir_block, true_blk);
    h.add_edge(cur_hir_block, false_blk);
    return 0;  // success
}

// ---------------------------------------------------------------
// Pass 2: emit HIR
// ---------------------------------------------------------------

int hir_lower_lua_proto(hir_program &h, rv_compiler &rc,
                        const lua_bc_proto *proto) {
    if (nullptr == proto) return -1;

    int n = static_cast<int>(proto->code.size());
    if (n == 0) return -1;

    // Pass 1: find block boundaries.
    std::vector<bool> is_leader;
    find_block_starts(proto, is_leader);
    std::vector<int> pc_to_block;
    int num_blocks = assign_blocks(is_leader, pc_to_block, n);

    bool multi_block = (num_blocks > 1);

    // Allocate HIR blocks.
    if (multi_block) {
        for (int b = 1; b < num_blocks; b++) {
            int nb = h.new_block();
            if (nb < 0) return -1;
        }
    }

    // Lua register → HIR value map.
    int lua_reg[MAX_LUA_REGS];
    memset(lua_reg, -1, sizeof(lua_reg));

    int cur_hir_block = 0;
    h.cur_block = 0;
    int result_val = -1;

    for (int pc = 0; pc < n; pc++) {
        // Switch blocks if this PC is a leader.
        if (is_leader[pc] && pc > 0) {
            int new_block = pc_to_block[pc];
            if (new_block != cur_hir_block) {
                if (h.n_insns > 0) {
                    hir_kind last = h.kind[h.n_insns - 1];
                    if (last != HIR_BR && last != HIR_BRC && last != HIR_RET) {
                        h.emit(HIR_BR, TY_VOID, -1, -1, new_block);
                        h.add_edge(cur_hir_block, new_block);
                    }
                }
                cur_hir_block = new_block;
                h.cur_block = new_block;
            }
        }

        const lua_bc_instruction &insn = proto->code[pc];
        int op = insn.opcode();
        int A = insn.A();

        switch (op) {

        // ---- Data movement ----

        case OP_LUA_MOVE:
            if (lua_reg[insn.B()] < 0) return -1;
            lua_reg[A] = lua_reg[insn.B()];
            break;

        case OP_LUA_LOADI:
            lua_reg[A] = h.emit_iconst(insn.sBx());
            if (lua_reg[A] < 0) return -1;
            break;

        case OP_LUA_LOADF:
            lua_reg[A] = h.emit_iconst(insn.sBx());
            if (lua_reg[A] < 0) return -1;
            break;

        case OP_LUA_LOADK: {
            int kidx = insn.Bx();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            lua_reg[A] = emit_lua_constant(h, rc, proto->constants[kidx]);
            if (lua_reg[A] < 0) return -1;
            break;
        }

        case OP_LUA_LOADFALSE:
        case OP_LUA_LFALSESKIP:
            lua_reg[A] = h.emit_iconst(0);
            if (lua_reg[A] < 0) return -1;
            if (op == OP_LUA_LFALSESKIP) pc++;
            break;

        case OP_LUA_LOADTRUE:
            lua_reg[A] = h.emit_iconst(1);
            if (lua_reg[A] < 0) return -1;
            break;

        case OP_LUA_LOADNIL:
            for (int i = A; i <= A + insn.B(); i++) {
                lua_reg[i] = h.emit_sconst(rc.pool_str("", 0), "");
                if (lua_reg[i] < 0) return -1;
            }
            break;

        // ---- Integer arithmetic ----

#define ARITH_RR(HIR_INT_OP, HIR_FP_OP, MMOP) \
        { \
            int rb = lua_reg[insn.B()]; \
            int rc_val = lua_reg[insn.C()]; \
            if (rb < 0 || rc_val < 0) return -1; \
            if (either_float(h, rb, rc_val)) { \
                rb = promote_to_float(h, rb); \
                rc_val = promote_to_float(h, rc_val); \
                if (rb < 0 || rc_val < 0) return -1; \
                lua_reg[A] = h.emit(HIR_FP_OP, TY_FLOAT, rb, rc_val); \
            } else if (h.ty[rb] == TY_INT && h.ty[rc_val] == TY_INT) { \
                lua_reg[A] = h.emit(HIR_INT_OP, TY_INT, rb, rc_val); \
            } else { \
                return -1; \
            } \
            if (lua_reg[A] < 0) return -1; \
            h.native_ops++; \
            if (pc + 1 < n && proto->code[pc + 1].opcode() == MMOP) pc++; \
            break; \
        }

        case OP_LUA_ADD:  ARITH_RR(HIR_ADD, HIR_FADD, OP_LUA_MMBIN)
        case OP_LUA_SUB:  ARITH_RR(HIR_SUB, HIR_FSUB, OP_LUA_MMBIN)
        case OP_LUA_MUL:  ARITH_RR(HIR_MUL, HIR_FMUL, OP_LUA_MMBIN)
        case OP_LUA_IDIV: ARITH_RR(HIR_DIV, HIR_DIV, OP_LUA_MMBIN)  // IDIV always integer
        case OP_LUA_MOD:  ARITH_RR(HIR_REM, HIR_REM, OP_LUA_MMBIN)  // MOD always integer
#undef ARITH_RR

        // Lua `/` (OP_DIV) always produces a float result.
        case OP_LUA_DIV: {
            int rb = lua_reg[insn.B()];
            int rc_val = lua_reg[insn.C()];
            if (rb < 0 || rc_val < 0) return -1;
            rb = promote_to_float(h, rb);
            rc_val = promote_to_float(h, rc_val);
            if (rb < 0 || rc_val < 0) return -1;
            lua_reg[A] = h.emit(HIR_FDIV, TY_FLOAT, rb, rc_val);
            if (lua_reg[A] < 0) return -1;
            h.native_ops++;
            if (pc + 1 < n && proto->code[pc + 1].opcode() == OP_LUA_MMBIN)
                pc++;
            break;
        }

        case OP_LUA_UNM: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            if (h.ty[rb] == TY_FLOAT) {
                lua_reg[A] = h.emit(HIR_FNEG, TY_FLOAT, rb);
            } else if (h.ty[rb] == TY_INT) {
                lua_reg[A] = h.emit(HIR_NEG, TY_INT, rb);
            } else {
                return -1;
            }
            if (lua_reg[A] < 0) return -1;
            h.native_ops++;
            if (pc + 1 < n && proto->code[pc + 1].opcode() == OP_LUA_MMBIN)
                pc++;
            break;
        }

        // ---- Immediate arithmetic ----

        case OP_LUA_ADDI: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            if (h.ty[rb] == TY_FLOAT) {
                int imm_val = h.emit_fconst(static_cast<double>(insn.sC()));
                if (imm_val < 0) return -1;
                lua_reg[A] = h.emit(HIR_FADD, TY_FLOAT, rb, imm_val);
            } else if (h.ty[rb] == TY_INT) {
                int imm_val = h.emit_iconst(insn.sC());
                if (imm_val < 0) return -1;
                lua_reg[A] = h.emit(HIR_ADD, TY_INT, rb, imm_val);
            } else {
                return -1;
            }
            if (lua_reg[A] < 0) return -1;
            h.native_ops++;
            if (pc + 1 < n && proto->code[pc + 1].opcode() == OP_LUA_MMBINI)
                pc++;
            break;
        }

        // ---- Constant arithmetic ----

#define ARITH_RK(HIR_INT_OP, HIR_FP_OP) \
        { \
            int rb = lua_reg[insn.B()]; \
            if (rb < 0) return -1; \
            int kidx = insn.C(); \
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size())) \
                return -1; \
            int kval = emit_lua_constant(h, rc, proto->constants[kidx]); \
            if (kval < 0) return -1; \
            if (either_float(h, rb, kval)) { \
                rb = promote_to_float(h, rb); \
                kval = promote_to_float(h, kval); \
                if (rb < 0 || kval < 0) return -1; \
                lua_reg[A] = h.emit(HIR_FP_OP, TY_FLOAT, rb, kval); \
            } else if (h.ty[rb] == TY_INT && h.ty[kval] == TY_INT) { \
                lua_reg[A] = h.emit(HIR_INT_OP, TY_INT, rb, kval); \
            } else { \
                return -1; \
            } \
            if (lua_reg[A] < 0) return -1; \
            h.native_ops++; \
            if (pc + 1 < n && proto->code[pc + 1].opcode() == OP_LUA_MMBINK) \
                pc++; \
            break; \
        }

        case OP_LUA_ADDK: ARITH_RK(HIR_ADD, HIR_FADD)
        case OP_LUA_SUBK: ARITH_RK(HIR_SUB, HIR_FSUB)
        case OP_LUA_MULK: ARITH_RK(HIR_MUL, HIR_FMUL)
#undef ARITH_RK

        // ---- Comparisons ----
        // All share: compare → optional negate → JMP → BRC

#define CMP_RR(HIR_INT_OP, HIR_FP_OP) \
        { \
            int rb = lua_reg[A]; \
            int rc_val = lua_reg[insn.B()]; \
            if (rb < 0 || rc_val < 0) return -1; \
            int cmp; \
            if (either_float(h, rb, rc_val)) { \
                rb = promote_to_float(h, rb); \
                rc_val = promote_to_float(h, rc_val); \
                if (rb < 0 || rc_val < 0) return -1; \
                cmp = h.emit(HIR_FP_OP, TY_INT, rb, rc_val); \
            } else if (h.ty[rb] == TY_INT && h.ty[rc_val] == TY_INT) { \
                cmp = h.emit(HIR_INT_OP, TY_INT, rb, rc_val); \
            } else { \
                return -1; \
            } \
            h.native_ops++; \
            if (!multi_block) return -1; \
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block, \
                                cur_hir_block, n) < 0) return -1; \
            pc++; \
            break; \
        }

#define CMP_RI(HIR_INT_OP, HIR_FP_OP) \
        { \
            int rb = lua_reg[A]; \
            if (rb < 0) return -1; \
            int cmp; \
            if (h.ty[rb] == TY_FLOAT) { \
                int fimm = h.emit_fconst(static_cast<double>(insn.sB())); \
                if (fimm < 0) return -1; \
                cmp = h.emit(HIR_FP_OP, TY_INT, rb, fimm); \
            } else if (h.ty[rb] == TY_INT) { \
                int imm_val = h.emit_iconst(insn.sB()); \
                if (imm_val < 0) return -1; \
                cmp = h.emit(HIR_INT_OP, TY_INT, rb, imm_val); \
            } else { \
                return -1; \
            } \
            h.native_ops++; \
            if (!multi_block) return -1; \
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block, \
                                cur_hir_block, n) < 0) return -1; \
            pc++; \
            break; \
        }

        case OP_LUA_EQ:  CMP_RR(HIR_EQ, HIR_FEQ)
        case OP_LUA_LT:  CMP_RR(HIR_LT, HIR_FLT)
        case OP_LUA_LE:  CMP_RR(HIR_LE, HIR_FLE)
        case OP_LUA_EQI: CMP_RI(HIR_EQ, HIR_FEQ)
        case OP_LUA_LTI: CMP_RI(HIR_LT, HIR_FLT)
        case OP_LUA_LEI: CMP_RI(HIR_LE, HIR_FLE)

        // For GT/GE with floats, we only have FLT/FLE.
        // GT(a, b) = FLT(b, a), GE(a, b) = FLE(b, a) — swap operands.
        case OP_LUA_GTI: {
            int rb = lua_reg[A];
            if (rb < 0) return -1;
            int cmp;
            if (h.ty[rb] == TY_FLOAT) {
                int fimm = h.emit_fconst(static_cast<double>(insn.sB()));
                if (fimm < 0) return -1;
                cmp = h.emit(HIR_FLT, TY_INT, fimm, rb);  // swapped
            } else if (h.ty[rb] == TY_INT) {
                int imm_val = h.emit_iconst(insn.sB());
                if (imm_val < 0) return -1;
                cmp = h.emit(HIR_GT, TY_INT, rb, imm_val);
            } else {
                return -1;
            }
            h.native_ops++;
            if (!multi_block) return -1;
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block,
                                cur_hir_block, n) < 0) return -1;
            pc++;
            break;
        }
        case OP_LUA_GEI: {
            int rb = lua_reg[A];
            if (rb < 0) return -1;
            int cmp;
            if (h.ty[rb] == TY_FLOAT) {
                int fimm = h.emit_fconst(static_cast<double>(insn.sB()));
                if (fimm < 0) return -1;
                cmp = h.emit(HIR_FLE, TY_INT, fimm, rb);  // swapped
            } else if (h.ty[rb] == TY_INT) {
                int imm_val = h.emit_iconst(insn.sB());
                if (imm_val < 0) return -1;
                cmp = h.emit(HIR_GE, TY_INT, rb, imm_val);
            } else {
                return -1;
            }
            h.native_ops++;
            if (!multi_block) return -1;
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block,
                                cur_hir_block, n) < 0) return -1;
            pc++;
            break;
        }
#undef CMP_RR
#undef CMP_RI

        case OP_LUA_TEST: {
            int rb = lua_reg[A];
            if (rb < 0) return -1;
            int cmp = h.emit(HIR_BOOL, TY_INT, rb);
            if (!multi_block) return -1;
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block,
                                cur_hir_block, n) < 0) return -1;
            pc++;
            break;
        }

        case OP_LUA_TESTSET: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            int cmp = h.emit(HIR_BOOL, TY_INT, rb);
            lua_reg[A] = rb;  // Simplified: always copy.
            if (!multi_block) return -1;
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block,
                                cur_hir_block, n) < 0) return -1;
            pc++;
            break;
        }

        // ---- Control flow ----

        case OP_LUA_JMP: {
            int target = pc + 1 + insn.sJ();
            if (!multi_block) return -1;
            int target_blk = (target >= 0 && target < n) ? pc_to_block[target] : -1;
            if (target_blk < 0) return -1;
            h.emit(HIR_BR, TY_VOID, -1, -1, target_blk);
            h.add_edge(cur_hir_block, target_blk);
            break;
        }

        // ---- Numeric for loop ----
        //
        // Lua 5.4 for-loop registers:
        //   R(A)   = internal counter
        //   R(A+1) = limit
        //   R(A+2) = step
        //   R(A+3) = exposed index (visible in body)
        //
        // FORPREP subtracts step from init, then jumps to FORLOOP.
        // FORLOOP adds step, checks idx <= limit, branches back.
        //
        // We use STORE_Q/LOAD_Q for the loop index so that
        // hir_ssa_construct() inserts proper PHI nodes at the
        // loop header.  This matches the iter() pattern.
        //

        case OP_LUA_FORPREP: {
            if (!multi_block) return -1;
            if (lua_reg[A] < 0 || lua_reg[A + 1] < 0 || lua_reg[A + 2] < 0)
                return -1;

            // Lua 5.4 FORPREP: R(A) -= step, then jump to FORLOOP.
            // The first FORLOOP iteration will add step back, giving
            // the original init value.  We store init - step so the
            // first ADD in FORLOOP produces init.
            int init_sub = h.emit(HIR_SUB, TY_INT, lua_reg[A], lua_reg[A + 2]);
            if (init_sub < 0) return -1;

            // Store the initial index into a q-register.
            h.emit(HIR_STORE_Q, TY_VOID, init_sub, -1, QREG_LUA_IDX);

            int target = pc + 1 + insn.sBx();
            int target_blk = (target >= 0 && target < n) ? pc_to_block[target] : -1;
            if (target_blk < 0) return -1;
            h.emit(HIR_BR, TY_VOID, -1, -1, target_blk);
            h.add_edge(cur_hir_block, target_blk);
            break;
        }

        case OP_LUA_FORLOOP: {
            if (!multi_block) return -1;
            int step = lua_reg[A + 2];
            int limit = lua_reg[A + 1];
            if (step < 0 || limit < 0) return -1;

            // Load current index from q-register (becomes PHI after SSA).
            int idx = h.emit(HIR_LOAD_Q, TY_INT, -1, -1, QREG_LUA_IDX);
            if (idx < 0) return -1;

            // Increment: index = index + step.
            int new_idx = h.emit(HIR_ADD, TY_INT, idx, step);
            if (new_idx < 0) return -1;
            h.native_ops++;

            // Store updated index back to q-register.
            h.emit(HIR_STORE_Q, TY_VOID, new_idx, -1, QREG_LUA_IDX);

            // Expose the new index to subsequent instructions.
            lua_reg[A + 3] = new_idx;
            lua_reg[A] = new_idx;

            // Compare: index <= limit.
            int cmp = h.emit(HIR_LE, TY_INT, new_idx, limit);
            if (cmp < 0) return -1;
            h.native_ops++;

            // Branch: if true, loop back; else fall through.
            int loop_target = pc + 1 + insn.sBx();
            int exit_target = pc + 1;
            int loop_blk = (loop_target >= 0 && loop_target < n) ? pc_to_block[loop_target] : -1;
            int exit_blk = (exit_target >= 0 && exit_target < n) ? pc_to_block[exit_target] : -1;
            if (loop_blk < 0 || exit_blk < 0) return -1;
            h.emit(HIR_BRC, TY_VOID, cmp, exit_blk, loop_blk);
            h.add_edge(cur_hir_block, loop_blk);
            h.add_edge(cur_hir_block, exit_blk);
            break;
        }

        // ---- Return ----

        case OP_LUA_RETURN0:
            result_val = h.emit_sconst(rc.pool_str("", 0), "");
            if (result_val < 0) return -1;
            h.emit(HIR_RET, TY_VOID, result_val);
            break;

        case OP_LUA_RETURN1: {
            int rv = lua_reg[A];
            if (rv < 0) return -1;
            if (h.ty[rv] == TY_INT) {
                rv = h.emit(HIR_ITOA, TY_STRING, rv);
                if (rv < 0) return -1;
            } else if (h.ty[rv] == TY_FLOAT) {
                rv = h.emit(HIR_FTOA, TY_STRING, rv);
                if (rv < 0) return -1;
            }
            result_val = rv;
            h.emit(HIR_RET, TY_VOID, result_val);
            break;
        }

        case OP_LUA_RETURN: {
            int nret = insn.B() - 1;
            if (nret < 0) return -1;
            if (nret == 0) {
                result_val = h.emit_sconst(rc.pool_str("", 0), "");
                if (result_val < 0) return -1;
            } else {
                int rv = lua_reg[A];
                if (rv < 0) return -1;
                if (h.ty[rv] == TY_INT) {
                    rv = h.emit(HIR_ITOA, TY_STRING, rv);
                    if (rv < 0) return -1;
                } else if (h.ty[rv] == TY_FLOAT) {
                    rv = h.emit(HIR_FTOA, TY_STRING, rv);
                    if (rv < 0) return -1;
                }
                result_val = rv;
            }
            h.emit(HIR_RET, TY_VOID, result_val);
            break;
        }

        // ---- Table access: mux.* bridge pattern ----

        case OP_LUA_GETTABUP: {
            if (insn.B() != 0) return -1;  // Only _ENV.
            int kidx = insn.C();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            const lua_bc_constant &k = proto->constants[kidx];
            if (k.type != LUA_BC_TSHRSTR && k.type != LUA_BC_TLNGSTR)
                return -1;
            if (k.sval != "mux") return -1;
            uint64_t addr = rc.pool_str("mux", 3);
            lua_reg[A] = h.emit_sconst(addr, "mux");
            if (lua_reg[A] < 0) return -1;
            break;
        }

        case OP_LUA_GETFIELD: {
            int table_reg = lua_reg[insn.B()];
            if (table_reg < 0) return -1;
            int kidx = insn.C();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            const lua_bc_constant &k = proto->constants[kidx];
            if (k.type != LUA_BC_TSHRSTR && k.type != LUA_BC_TLNGSTR)
                return -1;
            if (table_reg >= 0 && h.kind[table_reg] == HIR_SCONST
                && h.sval[table_reg] == "mux") {
                std::string name = "mux." + k.sval;
                uint64_t addr = rc.pool_str(name.c_str(), name.size());
                lua_reg[A] = h.emit_sconst(addr, name);
                if (lua_reg[A] < 0) return -1;
            } else {
                return -1;
            }
            break;
        }

        case OP_LUA_CALL: {
            int func_reg = lua_reg[A];
            if (func_reg < 0) return -1;

            int nargs = insn.B() - 1;
            int nresults = insn.C() - 1;

            if (h.kind[func_reg] != HIR_SCONST) return -1;
            const std::string &fname = h.sval[func_reg];
            if (fname.substr(0, 4) != "mux.") return -1;

            std::string bridge_name = fname.substr(4);
            std::string upper_name;
            for (char c : bridge_name) {
                upper_name += static_cast<char>(toupper(static_cast<unsigned char>(c)));
            }

            std::vector<int> args;
            for (int i = 0; i < nargs; i++) {
                int areg = lua_reg[A + 1 + i];
                if (areg < 0) return -1;
                if (h.ty[areg] == TY_INT) {
                    areg = h.emit(HIR_ITOA, TY_STRING, areg);
                    if (areg < 0) return -1;
                }
                args.push_back(areg);
            }

            int fidx = engine_api_lookup(upper_name.c_str());

            int call_val;
            if (fidx > 0) {
                call_val = h.emit_call(TY_STRING, fidx,
                    args.data(), static_cast<int>(args.size()));
            } else {
                call_val = h.emit_call(TY_STRING, 0,
                    args.data(), static_cast<int>(args.size()),
                    &upper_name);
            }
            if (call_val < 0) return -1;
            h.ecalls++;

            if (nresults >= 1) {
                lua_reg[A] = call_val;
            }
            break;
        }

        // ---- No-op instructions ----
        case OP_LUA_VARARGPREP:
        case OP_LUA_MMBIN:
        case OP_LUA_MMBINI:
        case OP_LUA_MMBINK:
            break;

        // ---- Unsupported opcodes ----
        default:
            return -1;
        }
    }

    if (result_val < 0) return -1;
    h.result = result_val;
    h.needs_jit = (h.ecalls > 0 || h.native_ops > 0);

    return result_val;
}
