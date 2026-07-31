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
#include <map>
#include <set>
#include <vector>
#include <string>

// Maximum Lua registers we track.
static constexpr int MAX_LUA_REGS = 256;

// Q-register slots for Lua for-loop variables.
// Reuses compiler-internal slots 10-12 (same as softcode iter()).
// Safe because Lua lowering never calls the softcode iter() path.
//
static constexpr int QREG_LUA_IDX    = 10;   // loop index variable
static constexpr int QREG_LUA_BUDGET = 12;   // back-edge iteration budget

// Maximum inline depth for nested lowering.
static constexpr int MAX_INLINE_DEPTH = 4;

// Maximum code size we attempt to JIT (prevents runaway compile time).
static constexpr int MAX_LUA_CODE_SIZE = 1024;

// Maximum Lua stack size (register pressure bound).
static constexpr int MAX_LUA_STACK = 64;

// Maximum parameters.
static constexpr int MAX_LUA_PARAMS = 8;

// ---------------------------------------------------------------
// Back-edge iteration budget.
//
// Emits HIR to decrement QREG_LUA_BUDGET at each back-edge.
// When the counter reaches zero, the combined condition forces
// loop exit — same model as MUSHcode's func_invk_lim.  The
// budget is initialized to lua_instruction_limit (default 100K)
// at function entry.
//
// The per-iteration cost is: LOAD_Q, SUB, STORE_Q, GT, BAND —
// five integer ops, no ECALL.  At GHz speed this is negligible.
//
// cond: the original loop condition (>=0 for FORLOOP/TFORLOOP),
//       or -1 for unconditional back-edges (JMP).
// Returns: combined condition value.
// ---------------------------------------------------------------

static int emit_budget_check(hir_program &h, int cond, int amount) {
    int budget = h.emit(HIR_LOAD_Q, TY_INT, -1, -1, QREG_LUA_BUDGET);
    if (budget < 0) return cond;
    // Decrement by the loop body's bytecode length, not by 1: the
    // interpreter's hook counts INSTRUCTIONS, and a per-edge tick would
    // let a compiled loop run body-length times longer than the VM
    // before tripping the same limit.
    if (amount < 1) amount = 1;
    int amt = h.emit(HIR_ICONST, TY_INT, -1, -1, amount);
    int new_budget = h.emit(HIR_SUB, TY_INT, budget, amt);
    h.emit(HIR_STORE_Q, TY_VOID, new_budget, -1, QREG_LUA_BUDGET);

    int zero_val = h.emit(HIR_ICONST, TY_INT, -1, -1, 0);
    int budget_ok = h.emit(HIR_GT, TY_INT, new_budget, zero_val);

    if (cond >= 0) {
        return h.emit(HIR_BAND, TY_INT, cond, budget_ok);
    }
    return budget_ok;
}

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
    case LUA_BC_HAS_LOOP:          return "backward branch (unbounded on host)";
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
        case OP_LUA_LOADKX:
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
        case OP_LUA_NOT:
        case OP_LUA_BNOT:
        case OP_LUA_LEN:
        case OP_LUA_CONCAT:
        case OP_LUA_ADDI:
        case OP_LUA_ADDK:
        case OP_LUA_SUBK:
        case OP_LUA_MULK:
        case OP_LUA_DIVK:
        case OP_LUA_IDIVK:
        case OP_LUA_MODK:
        case OP_LUA_BAND:
        case OP_LUA_BOR:
        case OP_LUA_BXOR:
        case OP_LUA_SHL:
        case OP_LUA_SHR:
        case OP_LUA_SHRI:
        case OP_LUA_SHLI:
        case OP_LUA_BANDK:
        case OP_LUA_BORK:
        case OP_LUA_BXORK:
        case OP_LUA_POW:
        case OP_LUA_POWK:
            break;

        // Metamethod companions — skipped as no-ops.
        case OP_LUA_MMBIN:
        case OP_LUA_MMBINI:
        case OP_LUA_MMBINK:
            break;

        // Table operations — handled via ECALL back to Lua VM.
        case OP_LUA_NEWTABLE:
        case OP_LUA_GETTABLE:
        case OP_LUA_GETTABI:
        case OP_LUA_GETFIELD:
        case OP_LUA_SETTABLE:
        case OP_LUA_SETTABI:
        case OP_LUA_SETFIELD:
        case OP_LUA_SETLIST:
            break;

        // Comparisons — handled.
        case OP_LUA_EQK:
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

        // Table/global access and function calls — handled.  TAILCALL is
        // lowered as CALL-then-RETURN: the frame-reuse the real mechanism
        // exists for does not apply when the callee runs via an ECALL doing
        // its own pcall, and the chunk-level pcall takes one result either
        // way.  A k flag (upvalues to close) declines in the lowering.
        case OP_LUA_GETTABUP:
        case OP_LUA_SETTABUP:
        case OP_LUA_GETUPVAL:
        case OP_LUA_SETUPVAL:
        case OP_LUA_SELF:
        case OP_LUA_CALL:
        case OP_LUA_TAILCALL:
            break;

        // Generic for-loop — handled via ECALL.
        case OP_LUA_TFORPREP:
        case OP_LUA_TFORCALL:
        case OP_LUA_TFORLOOP:
            break;

        // Harmless no-ops.
        case OP_LUA_VARARGPREP:
        case OP_LUA_EXTRAARG:
        case OP_LUA_CLOSE:     // no open upvalues without closures
            break;

        // --- Hard rejects ---

        case OP_LUA_CLOSURE:
            return LUA_BC_HAS_CLOSURE;

        case OP_LUA_VARARG:
            return LUA_BC_HAS_VARARG;

        case OP_LUA_TBC:
            return LUA_BC_HAS_TBC;

        // --- Everything else is unsupported ---
        default:
            return LUA_BC_UNSUPPORTED_OP;
        }
    }

    // Backward branches (#1326, partially lifted by #1732).
    //
    // Instruction and memory limits are enforced by CLuaMod::InsnCountHook,
    // which is a Lua VM hook: it does not exist on the compiled path, so an
    // unbudgeted compiled loop runs unbounded (TC009's original symptom).
    //
    // Numeric for loops (OP_FORLOOP) now compile under a back-edge budget
    // whose exhaustion ABORTS the run via ECALL_LUA_LIMITED -- the runner
    // fails over to the interpreter, which re-runs the chunk and raises its
    // own "instruction limit exceeded" through the hook, so the error
    // surface is the interpreter's verbatim.  The re-run is what shapes the
    // restrictions below: a loop proto must be RERUN-SAFE, so anything that
    // could reach outside the chunk is rejected -- calls (a rebound global
    // is arbitrary effectful code), SETTABUP (global writes), SELF (method
    // dispatch).  Chunk-local table stores are fine: TryJIT's stack
    // save/restore discards them with the chunk.  TESTSET is rejected
    // because it writes a register from inside a terminator, which the
    // store-at-write q-register routing in the lowering cannot see.  The
    // stack cap is the q-register file: loop-carried Lua registers map onto
    // q-regs 0..9.
    //
    // while/repeat (backward OP_JMP) and generic for (TFORLOOP) still
    // reject: the JMP shape needs the same budget wiring on a less regular
    // structure, and TFOR's iterator call is the dead named bridge.
    //
    bool has_back_edge = false;
    for (int pc = 0; pc < n; pc++) {
        const lua_bc_instruction &insn = proto->code[pc];
        switch (insn.opcode()) {
        // while/repeat loop back through a backward JMP; same budget,
        // same rerun-safety restrictions below as the numeric for.
        case OP_LUA_JMP:
            if (pc + 1 + insn.sJ() <= pc) has_back_edge = true;
            break;

        case OP_LUA_FORLOOP:
            has_back_edge = true;
            break;

        // Backward by construction; the iterator protocol is unsupported.
        case OP_LUA_TFORLOOP:
            return LUA_BC_HAS_LOOP;

        default:
            break;
        }
    }

    if (has_back_edge) {
        if (proto->maxstacksize > 10) return LUA_BC_HAS_LOOP;
        for (int pc = 0; pc < n; pc++) {
            switch (proto->code[pc].opcode()) {
            case OP_LUA_CALL:
            case OP_LUA_TAILCALL:
            case OP_LUA_SELF:
            case OP_LUA_SETTABUP:
            case OP_LUA_TESTSET:
                return LUA_BC_HAS_LOOP;
            default:
                break;
            }
        }
    }

    return LUA_BC_ELIGIBLE;
}

// ---------------------------------------------------------------
// Pass 1: find basic block boundaries
// ---------------------------------------------------------------

// The block leaders a single instruction induces, written into out[] (at most
// two).  find_block_starts() and lua_bool_fuse_at() both consult this, so the
// two passes cannot disagree about where the blocks are -- a disagreement is
// what #1421 was: the lowering skipped past a leader the CFG had recorded.
static int insn_leaders(const lua_bc_proto *proto, int pc, int n, int out[2]) {
    const lua_bc_instruction &insn = proto->code[pc];
    int cnt = 0;

    switch (insn.opcode()) {
    case OP_LUA_JMP:
        out[cnt++] = pc + 1 + insn.sJ();
        out[cnt++] = pc + 1;
        break;
    // Lua 5.4 numeric for: both operands are UNSIGNED Bx with an implicit
    // direction -- FORPREP skips FORWARD past the whole loop (Bx+1) when
    // the trip count is zero and otherwise falls into the body; FORLOOP
    // jumps BACK by Bx to the body.  The 5.3-era sBx read produced targets
    // tens of thousands of instructions away, which is why the (then
    // unreachable) lowering declined the moment the eligibility reject was
    // lifted (#1732).
    case OP_LUA_FORPREP:
        out[cnt++] = pc + 1 + insn.Bx() + 1;   // zero-trip skip target
        out[cnt++] = pc + 1;                   // body
        break;
    case OP_LUA_FORLOOP:
        out[cnt++] = pc + 1 - insn.Bx();       // body (back edge)
        out[cnt++] = pc + 1;                   // exit
        break;
    case OP_LUA_TFORPREP:
    case OP_LUA_TFORLOOP:
        // Rejected by eligibility; offsets kept only for the leader map.
        out[cnt++] = pc + 1 + insn.sBx();
        out[cnt++] = pc + 1;
        break;
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
        // "if (cond ~= k) then pc++" -- the skip lands at pc+2, and the JMP
        // it skipped is its own block.
        out[cnt++] = pc + 1;
        out[cnt++] = pc + 2;
        break;
    case OP_LUA_EQK:
        // #1761: EQK is "if ((R[A]==K[B]) ~= k) then pc++" -- one-instruction
        // skip, NOT the EQ+JMP fuse.  pc+1 is the skipped insn (often JMP),
        // pc+2 is the fall-through after the skip.  Omitting these leaders
        // left false_target in the SAME block as the EQK, so BRC's false
        // edge was a self-loop (dispatch-limit hang) and the fall-through
        // return was unreachable.  Masked by interpreter re-run until
        // Phase 4 removed it.
        out[cnt++] = pc + 1;
        out[cnt++] = pc + 2;
        break;
    case OP_LUA_LFALSESKIP:
        // "R[A] := false; pc++".  The skip is control flow, not a linear
        // step: the instruction it jumps over belongs to the other path.
        // Lowering it as a bare pc++ swallowed that leader whole (#1421).
        out[cnt++] = pc + 1;
        out[cnt++] = pc + 2;
        break;
    case OP_LUA_RETURN:
    case OP_LUA_RETURN0:
    case OP_LUA_RETURN1:
        // Do NOT mark pc+1 as a leader.  Lua always appends a trailing
        // return after an explicit one; treating it as a new block made
        // every returning chunk multi_block (budget STORE_Q + SSA) and
        // contributed to the #1309 hang class on otherwise linear code.
        break;
    default:
        break;
    }

    // Drop out-of-range targets; malformed bytecode is declined elsewhere.
    int keep = 0;
    for (int i = 0; i < cnt; i++) {
        if (out[i] > 0 && out[i] < n) out[keep++] = out[i];
    }
    return keep;
}

// Is `pc` the head of the four-instruction idiom Lua emits to materialize a
// condition as a value (lcode.c exp2reg / code_loadbool)?
//
//     pc   : <test>          if (cond ~= k) then pc++
//     pc+1 : JMP -> pc+3
//     pc+2 : LFALSESKIP A    R[A] := false; pc++   (skips pc+3)
//     pc+3 : LOADTRUE  A     R[A] := true
//
// The whole run is just  R[A] = (cond == k)  with no control flow, so fusing
// it to a bare comparison is both correct and branchless.  A compound
// condition (`a<b and c<d`) patches its own jump list into pc+2/pc+3, and
// then the run is a real join and must not be fused -- so require that
// nothing outside the run enters it.
//
// Only the comparison opcodes are fused.  TEST/TESTSET are excluded: TESTSET
// also copies R[B] into R[A] on the taken path, so its value is not simply
// the branch condition.
static bool lua_bool_fuse_at(const lua_bc_proto *proto, int pc, int n,
                             int *dst_reg) {
    switch (proto->code[pc].opcode()) {
    case OP_LUA_EQ:  case OP_LUA_LT:  case OP_LUA_LE:
    case OP_LUA_EQK: // same condjump+LFALSESKIP/LOADTRUE shape as EQ (#1764)
    case OP_LUA_EQI: case OP_LUA_LTI: case OP_LUA_LEI:
    case OP_LUA_GTI: case OP_LUA_GEI:
        break;
    default:
        return false;
    }
    if (pc + 3 >= n) return false;

    const lua_bc_instruction &jmp = proto->code[pc + 1];
    const lua_bc_instruction &lfs = proto->code[pc + 2];
    const lua_bc_instruction &ltr = proto->code[pc + 3];
    if (jmp.opcode() != OP_LUA_JMP) return false;
    if (pc + 2 + jmp.sJ() != pc + 3) return false;
    if (lfs.opcode() != OP_LUA_LFALSESKIP) return false;
    if (ltr.opcode() != OP_LUA_LOADTRUE) return false;
    if (lfs.A() != ltr.A()) return false;

    // No entry into pc+1 .. pc+3 from outside the run itself.
    for (int j = 0; j < n; j++) {
        if (j >= pc && j <= pc + 2) continue;   // the run's own branches
        int tgt[2];
        int cnt = insn_leaders(proto, j, n, tgt);
        for (int i = 0; i < cnt; i++) {
            if (tgt[i] >= pc + 1 && tgt[i] <= pc + 3) return false;
        }
    }

    *dst_reg = lfs.A();
    return true;
}

static void find_block_starts(const lua_bc_proto *proto,
                               std::vector<bool> &is_leader) {
    int n = static_cast<int>(proto->code.size());
    is_leader.assign(n, false);
    if (n > 0) is_leader[0] = true;

    for (int pc = 0; pc < n; pc++) {
        int dst;
        if (lua_bool_fuse_at(proto, pc, n, &dst)) {
            // Fused to a value in pass 2 -- the run has no control flow, so
            // it must not induce leaders here either.
            pc += 3;
            continue;
        }
        int tgt[2];
        int cnt = insn_leaders(proto, pc, n, tgt);
        for (int i = 0; i < cnt; i++) is_leader[tgt[i]] = true;
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

// The mux.* SENTINEL: `mux` and `mux.args` are lowered as SCONSTs holding
// their own NAMES, not as Lua values.  The separate provenance bit is
// essential: `"mux.args"` is also perfectly ordinary Lua string text and
// must never enter the CARGS fast path just because its bytes match.
//
static inline bool lua_is_mux_sentinel(const hir_program &h, int v) {
    return v >= 0 && h.kind[v] == HIR_SCONST && h.lua_mux_sentinel[v];
}

// ---------------------------------------------------------------
// Helper: coerce a return value to TY_STRING for HIR_RET.
//
// Known ICONST/FCONST fold to SCONST digit strings so pure `return 42`
// can take the folded (needs_jit=false) path.  Runtime ITOA/FTOA is only
// used when the value is not a compile-time constant (#1309).
// ---------------------------------------------------------------

// Render a double the way Lua's own tostring does (lobject.c tostringbuff).
// Lua formats with LUA_NUMBER_FMT -- "%.14g" for the double build -- and then
// appends ".0" to anything that came out looking like an integer, so a float
// whose value happens to be integral prints as "3.0" and stays distinguishable
// from the integer 3.  The compiled path used "%.17g" and never appended,
// so every integral float lost its subtype and every other float printed more
// digits than the interpreter (#1488).
//
void lua_format_double(double d, char *buf, size_t sz) {
    mux_snprintf(reinterpret_cast<UTF8 *>(buf), sz, T("%.14g"), d);
    if (buf[strspn(buf, "-0123456789")] == '\0') {
        size_t len = strlen(buf);
        if (len + 3 <= sz) {
            buf[len]     = '.';
            buf[len + 1] = '0';
            buf[len + 2] = '\0';
        }
    }
}

static int return_as_string(hir_program &h, rv_compiler &rc, int rv) {
    if (rv < 0) return -1;
    // A mux table sentinel is not the string containing its name.
    if (lua_is_mux_sentinel(h, rv)) return -1;
    // A CALL_VAL result is a live Lua value on the stack: marshal with
    // fun_lua rules at the softcode boundary (#1764 shape 2).  Other
    // handles (tables, callables) still must not escape as decimal indices.
    if (h.ty[rv] == TY_LUA_HANDLE) {
        if (h.kind[rv] == HIR_LUA_CALL_VAL) {
            h.needs_jit = true;
            return h.emit(HIR_LUA_MARSHAL, TY_STRING, rv);
        }
        return -1;
    }
    if (h.ty[rv] == TY_STRING) {
        return rv;
    }
    if (h.ty[rv] == TY_INT) {
        if (h.kind[rv] == HIR_ICONST) {
            char buf[32];
            mux_snprintf(reinterpret_cast<UTF8 *>(buf), sizeof(buf), T("%lld"),
                     static_cast<long long>(h.val[rv]));
            uint64_t addr = rc.pool_str(buf, strlen(buf));
            return h.emit_sconst(addr, buf);
        }
        // Runtime ITOA has no sval.  Without needs_jit the folded path
        // would return that empty string -- `return not a` answered ""
        // while the interpreter answered "0"/"1".
        h.needs_jit = true;
        return h.emit(HIR_ITOA, TY_STRING, rv);
    }
    if (h.ty[rv] == TY_FLOAT) {
        if (h.kind[rv] == HIR_FCONST) {
            char buf[64];
            lua_format_double(h.fval[rv], buf, sizeof(buf));
            uint64_t addr = rc.pool_str(buf, strlen(buf));
            return h.emit_sconst(addr, buf);
        }
        // Lua float, so Lua's rendering -- not HIR_FTOA, which formats the
        // MUX way and would drop the ".0" at run time just as the fold used
        // to at compile time (#1488).
        h.needs_jit = true;
        return h.emit(HIR_LUA_FTOA, TY_STRING, rv);
    }
    return rv;
}

// The return half of a lowered OP_TAILCALL: `return f(...)` is the call the
// OP_LUA_CALL case just emitted, then this.  One helper because the call
// body has two successful exits (the direct ECALL path and the general
// path) and both must finish the same way.
//
static int lua_tailcall_ret(hir_program &h, rv_compiler &rc, int v,
                            int &result_val) {
    int rv = return_as_string(h, rc, v);
    if (rv < 0) return -1;
    h.emit(HIR_RET, TY_VOID, rv);
    if (result_val < 0) {
        result_val = rv;
    }
    return 0;
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
    case LUA_BC_TFLOAT:
        // A Lua float stays a float even when its value is integral.  This
        // used to demote 2.0 to ICONST "for compatibility with integer
        // arithmetic", but Lua 5.4's integer/float distinction is observable
        // -- tostring(2.0) is "2.0", math.type(2.0) is "float", and a float
        // operand makes the whole expression float.  Demoting it made
        // `a * 1.0` an integer multiply and `a + 0.0` print "3" (#1488).
        return h.emit_fconst(k.fval);
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

// A CALL_STR result is a Lua value marshalled to text the way fun_lua
// renders a chunk return (nil→"", bool→"0"/"1", else tolstring).  That is
// correct only where the value leaves Lua.  Inside the chunk the Lua type
// is gone: truthiness, ==, and arithmetic then run under softcode / string
// rules and answer wrongly for values that are truthy in Lua but MUSH-falsy
// as text ("0", "", integer 0), or for bool/nil that only look right by
// coincidence of the marshal.  #1764: treat the producer as provenance and
// refuse to consume it under Lua semantics -- the interpreter answers.
//
// The mux.* sentinel fiction is what buys the
// native CARGS fast path -- GETTABI on the "mux.args" sentinel becomes an
// ALOAD with no ECALL -- and it is sound only while the sentinel is
// consumed AS a sentinel.  The moment one reaches an operation that treats
// it as a value, the program is working with the string "mux.args".
//
// It had leaked into five of them (#1795), every one executing and silent:
// type() said "string", tostring() said "mux.args", concat spliced the
// name in where Lua raises, == compared it to the literal text, and #mux
// answered 3 -- #1424's shape (a length taken over the NAME of a thing)
// on a different value class.
//
// Same rule as lua_is_marshalled_str and lua_is_nil below: a
// representation that lies about its type must be refused by every
// consumer that would believe it.
static inline bool lua_is_marshalled_str(const hir_program &h, int v) {
    return v >= 0 && h.kind[v] == HIR_LUA_CALL_STR;
}

// Lua truthiness provenance.  HIR collapses false and integer 0 to the
// same ICONST 0, and nil and "" to the same empty SCONST.  HIR_BOOL is
// integer SNEZ (softcode truthiness), so `local a=0 if a` answered falsy
// while Lua requires truthy.  Tags restore the distinction at TEST/NOT:
//
//   VALUE   — numbers, real strings, floats: always truthy in Lua
//   BOOL    — 0/1 with boolean semantics (LOADTRUE/FALSE, comparisons)
//   NIL     — always falsy (LOADNIL, or a known-absent plain-table read)
//   UNKNOWN — tag lost or never known (loop-carried LOAD_Q, …).  TEST/NOT
//             must decline rather than default to VALUE: the VALUE default
//             is the unsafe direction (lost BOOL/NIL → silent truthy, the
//             bug class #1765 fixed, re-entering through the back door).
//             #1768.
//
enum lua_truth : uint8_t {
    LUA_TRUTH_VALUE   = 0,
    LUA_TRUTH_BOOL    = 1,
    LUA_TRUTH_NIL     = 2,
    LUA_TRUTH_UNKNOWN = 3,
};

typedef std::map<int, lua_truth> lua_truth_map;

static void lua_truth_set(lua_truth_map &m, int v, lua_truth t) {
    if (v >= 0) {
        m[v] = t;
    }
}

// Tag a HIR value produced from a Lua pool constant.  TNIL/TFALSE/TTRUE
// share HIR representations with "" / 0 / 1; without the tag, EQK and
// TEST invent softcode semantics (nil == "" was true compiled).
//
static void lua_truth_tag_constant(lua_truth_map &truth,
                                   const lua_bc_constant &k, int v) {
    if (v < 0) {
        return;
    }
    switch (k.type) {
    case LUA_BC_TNIL:
        lua_truth_set(truth, v, LUA_TRUTH_NIL);
        break;
    case LUA_BC_TFALSE:
    case LUA_BC_TTRUE:
        lua_truth_set(truth, v, LUA_TRUTH_BOOL);
        break;
    default:
        break;
    }
}

// Lua TYPE classes, for equality.  Lua's == is false across types --
// 0 ~= false, "5" ~= 5, nil ~= "" -- but HIR represents false and 0 as
// the same ICONST, nil and "" as the same empty SCONST, and coerces
// strings to numbers when comparing.  Equality must therefore compare
// types before representations.
//
enum lua_type_class {
    LUA_TC_NIL,
    LUA_TC_BOOL,
    LUA_TC_NUMBER,
    LUA_TC_STRING,
    LUA_TC_OTHER,     // handles etc. -- refused before this matters
};

static lua_type_class lua_type_class_of_const(const lua_bc_constant &k) {
    switch (k.type) {
    case LUA_BC_TNIL:      return LUA_TC_NIL;
    case LUA_BC_TFALSE:
    case LUA_BC_TTRUE:     return LUA_TC_BOOL;
    case LUA_BC_TINT:
    case LUA_BC_TFLOAT:    return LUA_TC_NUMBER;
    case LUA_BC_TSHRSTR:
    case LUA_BC_TLNGSTR:   return LUA_TC_STRING;
    default:               return LUA_TC_OTHER;
    }
}

static lua_truth lua_truth_of(const hir_program &h, const lua_truth_map &m,
                              int v) {
    if (v < 0) {
        return LUA_TRUTH_UNKNOWN;
    }
    lua_truth_map::const_iterator it = m.find(v);
    if (it != m.end()) {
        return it->second;
    }
    // Comparison / NOT / BOOL results are 0/1 booleans even without a mark.
    switch (h.kind[v]) {
    case HIR_EQ: case HIR_NE: case HIR_LT: case HIR_LE:
    case HIR_GT: case HIR_GE:
    case HIR_FEQ: case HIR_FLT: case HIR_FLE:
    case HIR_BOOL: case HIR_NOT:
        return LUA_TRUTH_BOOL;
    // Loop-carried q-register reloads (#1732): the value came from a
    // store in another block; its truth class was not carried with it.
    case HIR_LOAD_Q:
        return LUA_TRUTH_UNKNOWN;
    default:
        // Untagged ICONST/arith/SCONST: VALUE is correct for numbers and
        // real strings.  Producers of BOOL/NIL must tag explicitly.
        return LUA_TRUTH_VALUE;
    }
}

// A value KNOWN to be Lua nil at lowering: LOADNIL, or a known-absent
// read of a plain table with a closed key set.  nil is representable in
// HIR only as the empty SCONST, which is also a real "" -- the truth tag
// is what tells them apart, so every consumer Lua would reject on nil
// must consult it here.
//
// Producers are easy to get right and consumers are easy to forget: the
// first cut of this change taught `"a" .. t[2]` to answer "a" where Lua
// raises, `#t[2]` to answer 0, and tostring(t[2]) to answer "" instead of
// "nil".  A new value class needs a consumer audit, not just correct
// producers (#1751 review note).
//
static inline bool lua_is_nil(const hir_program &h, const lua_truth_map &m,
                              int v) {
    return lua_truth_of(h, m, v) == LUA_TRUTH_NIL;
}

// Lua type class of a lowered VALUE.  The truth tag carries exactly the
// distinctions HIR erases; everything else follows the HIR type.
//
static lua_type_class lua_type_class_of_value(const hir_program &h,
                                              const lua_truth_map &m,
                                              int v) {
    if (v < 0) return LUA_TC_OTHER;
    // A mux.* sentinel is really a TABLE; classifying it by its SCONST
    // representation would compare it against the literal "mux.args".
    if (lua_is_mux_sentinel(h, v)) return LUA_TC_OTHER;
    const lua_truth t = lua_truth_of(h, m, v);
    if (t == LUA_TRUTH_NIL)     return LUA_TC_NIL;
    if (t == LUA_TRUTH_BOOL)    return LUA_TC_BOOL;
    if (t == LUA_TRUTH_UNKNOWN) return LUA_TC_OTHER;   // caller declines
    switch (h.ty[v]) {
    case TY_INT:
    case TY_FLOAT:  return LUA_TC_NUMBER;
    case TY_STRING: return LUA_TC_STRING;
    default:        return LUA_TC_OTHER;
    }
}

// Encode a non-handle operand for HIR_LUA_EQ (kind in val): 0=int, 1=str,
// 3=nil, 4=bool.  Returns the rhs HIR value index, or -1 if unencodable.
// #1835: NIL/BOOL must not collapse to kind 1/0 via empty SCONST / ICONST.
//
static int lua_eq_kind_of_value(hir_program &h, const lua_truth_map &m,
                                int v, int *kind_out) {
    const lua_truth tr = lua_truth_of(h, m, v);
    if (tr == LUA_TRUTH_NIL) {
        *kind_out = 3;
        return h.emit_iconst(0);
    }
    if (tr == LUA_TRUTH_BOOL) {
        *kind_out = 4;
        return v;
    }
    if (h.kind[v] == HIR_SCONST) {
        *kind_out = 1;
        return v;
    }
    if (h.ty[v] == TY_INT) {
        *kind_out = 0;
        return v;
    }
    return -1;
}

static int promote_to_float(hir_program &h, const lua_truth_map &truth, int v) {
    if (v < 0) return -1;
    if (lua_is_marshalled_str(h, v)) return -1;
    if (lua_is_mux_sentinel(h, v)) return -1;
    // nil is not a number; ATOI("") would invent 0.
    if (lua_truth_of(h, truth, v) == LUA_TRUTH_NIL) return -1;
    if (h.ty[v] == TY_FLOAT) return v;
    if (h.ty[v] == TY_INT) {
        return h.emit(HIR_ITOF, TY_FLOAT, v);
    }
    if (h.ty[v] == TY_STRING) {
        int as_int = h.emit(HIR_ATOI, TY_INT, v);
        if (as_int < 0) return -1;
        return h.emit(HIR_ITOF, TY_FLOAT, as_int);
    }
    return -1;
}

// ---------------------------------------------------------------
// Helper: promote an operand to TY_INT if needed.
// TY_STRING → HIR_ATOI.  TY_INT passes through.
// TY_FLOAT returns -1 (use promote_to_float instead).
// ---------------------------------------------------------------

static int promote_to_int(hir_program &h, const lua_truth_map &truth, int v) {
    if (v < 0) return -1;
    if (lua_is_marshalled_str(h, v)) return -1;
    if (lua_is_mux_sentinel(h, v)) return -1;
    if (lua_truth_of(h, truth, v) == LUA_TRUTH_NIL) return -1;
    if (h.ty[v] == TY_INT) return v;
    if (h.ty[v] == TY_STRING) return h.emit(HIR_ATOI, TY_INT, v);
    return -1;
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

static inline bool lua_reg_in_range(int idx);   // defined with pass 2

// A Lua handle is a reference into the VM, not a value (#1579).  Arithmetic,
// comparison, length, concatenation and returning are all illegal on one, so
// decline the chunk rather than let a stack index flow on as though it were
// the thing it points at -- which is what made `#t` answer 22 (#1424).
//
// Declining here is strictly better than the run-time bail that #1518's
// fail-closed intercept produces today: it costs no compile-and-run, and it
// does not depend on the bridge ECALL names staying unimplemented.
//
// What a handle refers to -- its REFERENT -- tracked per HIR value while
// lowering.  The type system says "handle"; this says handle to WHAT, which
// is the question two decision sites had each been answering by inspecting
// provenance:
//
//   * OP_LUA_GETFIELD chose GETFIELD_REF vs GETFIELD_INT by whether the
//     table handle's producing instruction was GETGLOBAL, and
//   * OP_LUA_CALL chose CALL_INT vs CALL_STR by walking back to the SCONST
//     key that named the callee and consulting a whitelist -- which then had
//     to gate BOTH branches (d5e5e86e0), or a name skipping one could fall
//     through and claim the other's result type.
//
// Now the claim is made once, where the handle is created, and the decision
// sites read it.  A claim is ELIGIBILITY, not soundness: every ECALL
// verifies at runtime (lua_isfunction before calling, lua_isinteger and
// LUA_TSTRING on results) and declines to the interpreter on a miss, so a
// wrong claim costs a bail, never a wrong answer.  That is what lets
// TY_STRING be the open default for a name nothing here knows -- a
// game-defined global that does return a string compiles and runs, and one
// that does not declines exactly where it always did.
//
// A library member that is a VALUE rather than a function -- math.pi,
// math.maxinteger.  Reading one takes the value directly (GETFIELD_INT /
// GETFIELD_FLT on the library table) instead of a reference nothing could
// consume.  Function members deliberately do NOT appear here: their return
// claims stay in lua_call_claim, so each fact lives once.
//
struct lua_lib_value {
    const char *name;
    hir_type ty;    // TY_INT or TY_FLOAT
};

static const lua_lib_value k_lua_math_values[] = {
    {"maxinteger", TY_INT},
    {"mininteger", TY_INT},
    {"pi",         TY_FLOAT},
    {"huge",       TY_FLOAT},
    {nullptr,      TY_VOID},
};

struct lua_referent {
    // Field reads: false means members are values (GETFIELD_INT -- the
    // NEWTABLE shape), true means members are references (GETFIELD_REF --
    // the library shape).  GETGLOBAL results are the only handles whose
    // fields are taken by reference, same as the provenance test chose.
    bool fields_are_refs = false;

    // Calls: may one be attempted, and what does it claim to return?
    // TY_INT and TY_STRING are the two marshallings that exist; a handle
    // that never received a callable claim declines the call at compile
    // time, which is where a NEWTABLE or a nested-field handle lands.
    bool callable = false;
    hir_type returns = TY_VOID;

    // The callee may have side effects the player can observe
    // (mux.notify/pemit/set/eval).  Recorded for diagnostics and for
    // any future purity analysis; it is NOT a compile-time refuse.
    // #1751 Phase 4 deleted post-entry re-run, so an effect delivered
    // compiled is not re-delivered by a silent interpreter retry — the
    // reason #1750 made these ineligible.  Both routes share the same
    // bridge C functions under the same permissions now.
    bool effectful = false;

    // Known VALUE members, for a recognized standard-library table; null
    // for everything else.  Like every claim here it is eligibility only:
    // a game that rebinds math.pi to a string declines at the runtime
    // check, not answers wrongly.
    const lua_lib_value *values = nullptr;

    // PROVEN plain: created by NEWTABLE in this chunk and never passed as
    // a call argument since.  Only such a table may take the typed
    // integer-keyed fast reads (GETI / GETFIELD_INT / GETFIELD_FLT):
    // their ok=0 claim-miss path continues with 0, which is sound only
    // when no metamethod can fire and no non-integer value can have been
    // stored -- both guaranteed by construction here (compiled stores are
    // integer-only, plain tables have no __index) and by NOTHING for a
    // handle from anywhere else.  #1751's Phase 1 review proved the miss:
    // an interpreter-installed __index on a global made compiled `T[1]`
    // answer 0 where the interpreter answered "s", silently.  Escaping
    // as a call argument clears the proof: the callee can setmetatable.
    bool plain_proven = false;

    // Closed integer-key set for a plain table: every compiled store used
    // a constant key recorded in int_keys.  A dynamic-key store clears
    // keys_closed.  When closed, a GETI of a constant key not in the set
    // is known-absent at lowering — emit nil, no post-entry GETI_INT
    // miss (the residual loud site for `return t[2]` after `t[1]=5`).
    bool keys_closed = false;
    std::set<int64_t> int_keys;
};

static hir_type lua_lib_value_type(const lua_referent &t,
                                   const std::string &key) {
    if (nullptr == t.values) return TY_VOID;
    for (const lua_lib_value *v = t.values; v->name != nullptr; v++) {
        if (key == v->name) return v->ty;
    }
    return TY_VOID;
}

typedef std::map<int, lua_referent> lua_ref_map;

static lua_referent lua_referent_of(const lua_ref_map &m, int v) {
    lua_ref_map::const_iterator it = m.find(v);
    return (it == m.end()) ? lua_referent() : it->second;
}

// Record a constant integer-key store on a plain table.  No-op if the
// handle is not plain or keys are already open.
//
static void lua_note_int_key_store(lua_ref_map &m, int tbl, int64_t key) {
    lua_ref_map::iterator it = m.find(tbl);
    if (it == m.end() || !it->second.plain_proven) {
        return;
    }
    if (!it->second.keys_closed) {
        return;
    }
    it->second.int_keys.insert(key);
}

// A non-constant integer key store: the closed set is no longer complete.
//
static void lua_note_dynamic_int_key_store(lua_ref_map &m, int tbl) {
    lua_ref_map::iterator it = m.find(tbl);
    if (it == m.end() || !it->second.plain_proven) {
        return;
    }
    it->second.keys_closed = false;
    it->second.int_keys.clear();
}

// The standard-library knowledge, in one place: what does calling NAME
// return?  HIR result types are static and Lua's are not -- math.max(3,9)
// and tostring(42) take the same argument shapes and return different
// types -- so the name is the only thing that carries it, and this list is
// a claim about the standard library.  Names claiming TY_INT stay few and
// deliberate; everything else claims TY_STRING, the open default the
// runtime check makes safe.
//
static hir_type lua_call_claim(const std::string &name) {
    static const char *kIntReturning[] = {
        "floor", "ceil", "max", "min", "abs", "tointeger",
        "len", "byte", "maxinteger", "mininteger",
        "tonumber",   // integer when the argument is an integer literal
    };
    for (const char *n : kIntReturning) {
        if (name == n) return TY_INT;
    }
    // FLOAT-returning stdlib names.  No CALL_FLT marshalling exists, so
    // a FLOAT claim makes the call ineligible at lowering and the
    // interpreter answers -- silently, with the right subtype.  Before
    // this list, sqrt claimed the STRING default and its miss was a
    // post-entry fail (smoke TC046 under Phase 0).
    static const char *kFloatReturning[] = {
        "sqrt", "exp", "log", "sin", "cos", "tan",
        "asin", "acos", "atan", "fmod", "rad", "deg",
        "random",   // float in the no-argument form
    };
    for (const char *n : kFloatReturning) {
        if (name == n) return TY_FLOAT;
    }
    return TY_STRING;
}

static inline bool lua_is_handle(const hir_program &h, int v) {
    return v >= 0 && h.ty[v] == TY_LUA_HANDLE;
}

static int emit_cmp_branch(hir_program &h, int cmp, int k_bit,
                            const lua_bc_proto *proto, int &pc,
                            const std::vector<int> &pc_to_block,
                            int cur_hir_block, int n,
                            int *lua_reg, bool multi_block) {
    if (cmp < 0) return -1;
    // Lua's conditional ops are "if (cond ~= k) then pc++", and that pc++
    // skips the JMP which follows.  So the JMP is taken exactly when
    // cond == k, and falling through to pc+2 is the cond != k case.
    // true_target below is the JMP's destination, so the branch condition
    // must be (cond == k): negate when k is 0, not when it is 1 (#1486).
    //
    // EQK uses the same condjump+JMP shape as EQ/EQI (luaK_codeeq →
    // condjump), so it shares this polarity.  An older lowering treated
    // EQK as a bare skip to pc+1/pc+2 with inverted k; that path is gone.
    if (!k_bit) {
        cmp = h.emit(HIR_NOT, TY_INT, cmp);
        if (cmp < 0) return -1;
    }

    // The condition used as a *value* rather than as a branch: `cmp` already
    // is (cond == k), which is exactly what the LFALSESKIP/LOADTRUE pair
    // computes, so drop the whole run and keep the comparison (#1421).
    int dst;
    if (lua_reg != nullptr && lua_bool_fuse_at(proto, pc, n, &dst)) {
        if (!lua_reg_in_range(dst)) return -1;
        lua_reg[dst] = cmp;
        pc += 2;    // LFALSESKIP and LOADTRUE; the caller steps over the JMP
        return 0;
    }

    // Only a real branch needs more than one block.  This guard sits after the
    // fuse on purpose: fusing removes the chunk's only branch, so `return a<b`
    // is single-block by construction and would otherwise decline here.
    if (!multi_block) return -1;
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

// Defensive bound for composite register indices (A + offset).  Bare A/B/C
// operands are 8-bit (< MAX_LUA_REGS == 256) and always index lua_reg[]
// safely, but ranges — LOADNIL's R(A)..R(A+B), CONCAT/SETLIST/CALL argument
// and result runs, and the TFOR result registers R(A+4)..R(A+3+C) — can
// reach ~R(A+4+255) with crafted operands, past the end of lua_reg[].
// Well-formed Lua 5.4 compiler output keeps every register < maxstacksize
// (<= MAX_LUA_STACK == 64), so this guard only fires on malformed bytecode;
// bail to the Lua VM (return -1) rather than overrun the map.  (Crafted
// bytecode can't reach this lowering through the text-only sandbox today,
// but the translation must stay memory-safe regardless.)
//
static inline bool lua_reg_in_range(int idx) {
    return idx >= 0 && idx < MAX_LUA_REGS;
}

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

    // HIR value → referent claim, for TY_LUA_HANDLE values.  Keyed by HIR
    // value id, so it is indifferent to blocks and to which Lua register a
    // handle currently sits in.  A handle with no entry gets the default:
    // fields are values, calls decline.
    lua_ref_map lua_ref;

    // HIR value → Lua truth class (see lua_truth).  Distinguishes false
    // from integer 0 and nil from "" so TEST/NOT follow Lua, not HIR_BOOL.
    lua_truth_map truth_tag;

    // Loop-carried value routing (#1732).  A plain HIR value crosses a
    // block boundary only under dominance, and the transition below drops
    // everything else -- which is correct for diamonds and fatal for
    // loops: the accumulator in `for i=1,4 do s=s+i end` is written in the
    // body and read by the next iteration.  Loop protos therefore route
    // Lua registers through q-registers (reg r → qreg r), the one kind of
    // traffic hir_ssa_construct PHI-converts -- the same road the numeric
    // for's own index already takes via QREG_LUA_IDX.
    //
    // Backing rule: a register joins the backed set only when the ENTRY
    // block stores it (every path executes the entry block, so every later
    // LOAD_Q is dominated by a store), or when FORLOOP itself stores the
    // loop variable (every reader is dominated by the latch).  A register
    // first written elsewhere stays plain and keeps today's drop-then-
    // decline behavior: reloading it would read whatever the surrounding
    // command left in the MUSH %q register on paths that never stored it.
    // Backed stores are integers only; a backed register going non-int
    // declines the chunk rather than leaving a stale int in the qreg.
    // The loop VARIABLE needs its backing declared up front: the body is
    // lowered BEFORE the FORLOOP that writes R(A)/R(A+3) (linear pc
    // order), so without the pre-scan the body's read of the loop var
    // found -1 and declined.  Sound because every path into the body --
    // and into the exit block -- passes the latch, whose STORE_Qs
    // dominate every reload.
    bool proto_has_loop = false;
    bool forloop_backed[10] = { false, false, false, false, false,
                                false, false, false, false, false };
    for (int i = 0; i < n; i++) {
        const int sop = proto->code[i].opcode();
        if (sop == OP_LUA_FORLOOP) {
            proto_has_loop = true;
            int fa = proto->code[i].A();
            // Only the VISIBLE index (A+3) is materialized; R(A) is 5.4's
            // internal counter and nothing here ever produces it.
            if (fa + 3 < 10) {
                forloop_backed[fa + 3] = true;
            }
        } else if (sop == OP_LUA_JMP
                   && i + 1 + proto->code[i].sJ() <= i) {
            // while/repeat: a backward JMP makes this a loop proto too.
            proto_has_loop = true;
        }
    }
    bool qreg_backed[10] = { false, false, false, false, false,
                             false, false, false, false, false };
    bool entry_backing_sealed = false;
    int limited_blk = -1;
    // Register state at the moment the entry block was left.  FORLOOP's
    // static-bounds test reads its ICONSTs from here: by the latch,
    // lua_reg[] holds LOAD_Q reloads, and a reload is one iteration
    // fresher than an entry constant but buries the constness.
    int entry_final[MAX_LUA_REGS];
    memset(entry_final, -1, sizeof(entry_final));


    // Snapshot of lua_reg as it stood on entry to the current block, so a
    // block transition can tell which registers this block wrote.  See the
    // dominance note at the transition below (#1422).
    int blk_entry_reg[MAX_LUA_REGS];
    memcpy(blk_entry_reg, lua_reg, sizeof(blk_entry_reg));

    int cur_hir_block = 0;
    h.cur_block = 0;
    int result_val = -1;

    // The shared bail block: its ECALL aborts the whole run to the
    // interpreter.  Two producers branch here -- back-edge budget
    // exhaustion, and FORPREP's runtime bounds guard -- and both bails
    // are rerun-safe: loop protos exclude persistent effects, and the
    // bounds guard runs before any body effect exists at all.
    auto ensure_limited_blk = [&]() -> bool {
        if (limited_blk >= 0) return true;
        limited_blk = h.new_block();
        if (limited_blk < 0) return false;
        int save_blk = h.cur_block;
        h.cur_block = limited_blk;
        h.emit(HIR_LUA_LIMITED, TY_VOID);
        int dead = h.emit_sconst(rc.pool_str("", 0), "");
        if (dead < 0) return false;
        h.emit(HIR_RET, TY_VOID, dead);
        h.cur_block = save_blk;
        return true;
    };

    // Back-edge budget guard, shared by FORLOOP and backward JMP so the
    // two cannot drift (#1457's lesson): decrement by the loop body's
    // instruction count, and branch to the shared limited block on
    // exhaustion rather than exiting the loop with a wrong partial
    // result (#1732).  Leaves the current block set to a fresh
    // continuation block for the caller's own terminator.  Returns
    // false on emission failure.
    auto emit_backedge_guard = [&](int body_len) -> bool {
        int budget_ok = emit_budget_check(h, -1, body_len);
        if (budget_ok < 0) return false;
        if (!ensure_limited_blk()) return false;
        int cont_blk = h.new_block();
        if (cont_blk < 0) return false;
        h.emit(HIR_BRC, TY_VOID, budget_ok, limited_blk, cont_blk);
        h.add_edge(cur_hir_block, limited_blk);
        h.add_edge(cur_hir_block, cont_blk);
        h.cur_block = cont_blk;
        cur_hir_block = cont_blk;
        return true;
    };

    // Entry-backing seal: decide, once, which registers the q-reg
    // machinery may reload (see the backing rule above), and freeze
    // entry_final.  Called from the first block transition -- or from
    // FORPREP's runtime-bounds path, which SPLITS the entry block with
    // branches and must finalize entry state before the split so the
    // next transition's drop-compare does not mistake entry writes for
    // block-local ones.
    auto seal_entry_backing = [&]() {
        if (entry_backing_sealed) return;
        for (int r = 0; r < 10 && r < MAX_LUA_REGS; r++) {
            if (qreg_backed[r]
                && (lua_reg[r] < 0 || h.ty[lua_reg[r]] != TY_INT)) {
                qreg_backed[r] = false;
            }
        }
        memcpy(entry_final, lua_reg, sizeof(entry_final));
        entry_backing_sealed = true;
    };

    // Initialize back-edge budget counter for loop DoS protection.
    // Uses the same limit as the Lua interpreter's instruction hook.
    // Only needed for multi-block programs (which can have loops).
    //
    // Read at RUN time via a dedicated ECALL, never baked as an ICONST of
    // mudconf.lua_instruction_limit (#1745).  A compiled program is cached
    // in memory and persisted in code_cache, so a baked value is the limit
    // that happened to be configured at compile time, forever -- @admin
    // changes reported Set. and changed nothing, which is #1613's bug
    // arriving on the compiled path.  The test-config runtime-bounds case
    // is what caught it, the first time the default-on flip put the
    // compiled path in its way.
    if (multi_block) {
        int budget_init = h.emit(HIR_LUA_INSN_BUDGET, TY_INT);
        h.emit(HIR_STORE_Q, TY_VOID, budget_init, -1, QREG_LUA_BUDGET);
    }

    // Pinned table tracking for array optimization.
    // When a for-loop body accesses t[i] where i is the loop variable,
    // we pin the table's array into guest memory before the loop.
    int pinned_tbl_reg = -1;     // Lua register of pinned table (-1 = none)
    int pinned_count_val = -1;   // HIR value holding element count

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
                // A plain HIR value is usable across blocks only where its
                // defining block dominates the use.  Nothing merges values
                // at a join: hir_ssa_construct() inserts PHIs only for
                // q-register traffic, which is exactly why the numeric for
                // loop routes its index through STORE_Q/LOAD_Q (see the
                // FORPREP comment below).  The entry block dominates every
                // reachable block; no other block here is known to.  So
                // drop any register this block wrote before leaving it --
                // the "< 0" guards then decline the chunk instead of
                // compiling a wrong answer (#1422).
                //
                // Without this, the last-lowered write simply won.  A
                // not-taken branch's assignment leaked past the join
                // (`if x>2 then t=2 end` yielded 2 for x=1), and a loop
                // body's update was invisible to the next iteration
                // (`while i<10 do i=i+1 end` yielded 0, not 10).
                if (cur_hir_block != 0) {
                    for (int r = 0; r < MAX_LUA_REGS; r++) {
                        if (lua_reg[r] != blk_entry_reg[r]) {
                            lua_reg[r] = -1;
                        }
                    }
                }
                if (proto_has_loop) {
                    // Leaving the entry block for the first time: seal
                    // the backed set (see seal_entry_backing; FORPREP's
                    // runtime-bounds path may have sealed already).
                    seal_entry_backing();
                    // Every backed register enters the new block through
                    // its qreg; SSA turns the loads into PHIs over the
                    // stores on each incoming path.  ALWAYS -- an entry
                    // value dominates every block, but dominance is
                    // availability, not currency: inside the loop the
                    // entry constant is one iteration stale, which made
                    // `s=s+i` compute 0+i forever.  FORLOOP gets the
                    // ICONSTs its static-bounds test needs from
                    // entry_final[], not from these reloads.
                    cur_hir_block = new_block;
                    h.cur_block = new_block;
                    for (int r = 0; r < 10 && r < MAX_LUA_REGS; r++) {
                        if (!qreg_backed[r] && !forloop_backed[r]) continue;
                        int lv = h.emit(HIR_LOAD_Q, TY_INT, -1, -1, r);
                        if (lv < 0) return -1;
                        h.known_int[lv] = true;
                        // Truth class did not cross the block boundary
                        // with the integer payload (#1768).
                        lua_truth_set(truth_tag, lv, LUA_TRUTH_UNKNOWN);
                        lua_reg[r] = lv;
                    }
                    memcpy(blk_entry_reg, lua_reg, sizeof(blk_entry_reg));
                } else {
                    memcpy(blk_entry_reg, lua_reg, sizeof(blk_entry_reg));
                    cur_hir_block = new_block;
                    h.cur_block = new_block;
                }
            }
        }

        const lua_bc_instruction &insn = proto->code[pc];
        int op = insn.opcode();
        int A = insn.A();

        // Snapshot for the store-at-write hook below (loop protos only).
        int pre_reg[MAX_LUA_REGS];
        if (proto_has_loop) {
            memcpy(pre_reg, lua_reg, sizeof(pre_reg));
        }

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

        // LOADF loads a *float* whose value is the signed immediate.  It
        // carried an integer immediate, and the lowering took it at face
        // value and emitted an integer constant -- so `return 3.0` produced
        // the integer 3, and every Lua constant expression that folds to an
        // integral float (`4/2`, `2^3`, `7.0//2.0`, `1e3`, `-3.0`) lost its
        // float subtype before the JIT ever did any arithmetic.  It also made
        // `a * 1.0` an integer multiply (#1488).
        case OP_LUA_LOADF:
            lua_reg[A] = h.emit_fconst(static_cast<double>(insn.sBx()));
            if (lua_reg[A] < 0) return -1;
            break;

        case OP_LUA_LOADK: {
            int kidx = insn.Bx();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            const lua_bc_constant &kc = proto->constants[kidx];
            lua_reg[A] = emit_lua_constant(h, rc, kc);
            if (lua_reg[A] < 0) return -1;
            lua_truth_tag_constant(truth_tag, kc, lua_reg[A]);
            break;
        }

        case OP_LUA_LOADKX: {
            // Extended constant: index is in the following EXTRAARG instruction.
            if (pc + 1 >= n) return -1;
            int kidx = proto->code[pc + 1].Ax();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            const lua_bc_constant &kc = proto->constants[kidx];
            lua_reg[A] = emit_lua_constant(h, rc, kc);
            if (lua_reg[A] < 0) return -1;
            lua_truth_tag_constant(truth_tag, kc, lua_reg[A]);
            pc++;  // skip EXTRAARG
            break;
        }

        case OP_LUA_LOADFALSE:
            lua_reg[A] = h.emit_iconst(0);
            if (lua_reg[A] < 0) return -1;
            // Boolean false, not integer 0 — same ICONST, different TEST.
            lua_truth_set(truth_tag, lua_reg[A], LUA_TRUTH_BOOL);
            break;

        // "R[A] := false; pc++".  When this pairs with LOADTRUE purely to
        // turn a condition into a value, the run is fused away before we get
        // here (lua_bool_fuse_at).  What is left is a genuine two-way join,
        // so the skip has to be an explicit branch.  Lowering it as a linear
        // pc++ stepped over a block leader: the skipped path's entire body
        // was emitted into this block and the other block was left empty, so
        // the chunk returned the false arm's value -- or, once every RET got
        // its own output slot, nothing at all (#1421).
        case OP_LUA_LFALSESKIP: {
            lua_reg[A] = h.emit_iconst(0);
            if (lua_reg[A] < 0) return -1;
            lua_truth_set(truth_tag, lua_reg[A], LUA_TRUTH_BOOL);
            if (!multi_block) return -1;
            int target = pc + 2;
            int target_blk = (target > 0 && target < n) ? pc_to_block[target] : -1;
            if (target_blk < 0) return -1;
            h.emit(HIR_BR, TY_VOID, -1, -1, target_blk);
            h.add_edge(cur_hir_block, target_blk);
            break;
        }

        case OP_LUA_LOADTRUE:
            lua_reg[A] = h.emit_iconst(1);
            if (lua_reg[A] < 0) return -1;
            lua_truth_set(truth_tag, lua_reg[A], LUA_TRUTH_BOOL);
            break;

        case OP_LUA_LOADNIL:
            for (int i = A; i <= A + insn.B(); i++) {
                if (!lua_reg_in_range(i)) return -1;
                // Empty SCONST is also the representation of "": only the
                // NIL tag makes TEST falsy here and leaves literal "" truthy.
                lua_reg[i] = h.emit_sconst(rc.pool_str("", 0), "");
                if (lua_reg[i] < 0) return -1;
                lua_truth_set(truth_tag, lua_reg[i], LUA_TRUTH_NIL);
            }
            break;

        // ---- Integer arithmetic ----

#define ARITH_RR(HIR_INT_OP, HIR_FP_OP, MMOP) \
        { \
            int rb = lua_reg[insn.B()]; \
            int rc_val = lua_reg[insn.C()]; \
            if (rb < 0 || rc_val < 0) return -1; \
            if (lua_is_handle(h, rb) || lua_is_handle(h, rc_val)) return -1; \
            if (either_float(h, rb, rc_val)) { \
                rb = promote_to_float(h, truth_tag, rb); \
                rc_val = promote_to_float(h, truth_tag, rc_val); \
                if (rb < 0 || rc_val < 0) return -1; \
                lua_reg[A] = h.emit(HIR_FP_OP, TY_FLOAT, rb, rc_val); \
            } else if (h.ty[rb] == TY_INT && h.ty[rc_val] == TY_INT) { \
                lua_reg[A] = h.emit(HIR_INT_OP, TY_INT, rb, rc_val); \
            } else { \
                rb = promote_to_int(h, truth_tag, rb); \
                rc_val = promote_to_int(h, truth_tag, rc_val); \
                if (rb < 0 || rc_val < 0) return -1; \
                lua_reg[A] = h.emit(HIR_INT_OP, TY_INT, rb, rc_val); \
            } \
            if (lua_reg[A] < 0) return -1; \
            h.native_ops++; \
            /* Do NOT pc++ past MMBIN here: the for-loop already advances
             * pc, so an extra increment skips the following RETURN and
             * leaves the chunk without a proper HIR_RET (#1309 hang).
             * MMBIN* cases below are intentional no-ops. */ \
            (void)MMOP; \
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
            rb = promote_to_float(h, truth_tag, rb);
            rc_val = promote_to_float(h, truth_tag, rc_val);
            if (rb < 0 || rc_val < 0) return -1;
            lua_reg[A] = h.emit(HIR_FDIV, TY_FLOAT, rb, rc_val);
            if (lua_reg[A] < 0) return -1;
            h.native_ops++;
            // MMBIN follows as a no-op case — do not double-advance pc.
            break;
        }

        // Lua `^` (OP_POW) always produces a float.  Native FCALL2 to the
        // No HAVE_IEEE_FP_SNAN guard here, deliberately (#1556).
        //
        // Softcode POWER declines the native path when that macro is undefined
        // (hir_lower.cpp) because fun_power has a *MUX output convention* on
        // such builds: a negative base yields the literal string "Ind" rather
        // than whatever the FP library produces.  That is softcode's answer
        // format, not a trap-avoidance measure.
        //
        // Lua has no such convention.  luai_numpow (lua54/llimits.h) is
        // `(b == 2) ? a*a : pow(a, b)` on every platform, so the Lua
        // *interpreter* calls pow() directly whether or not the macro is
        // defined.  Mirroring softcode's guard here would therefore make the
        // compiled path diverge from the Lua interpreter, which is the opposite
        // of what the guard achieves for softcode.
        //
        // Measured on a build with HAVE_IEEE_FP_SNAN forced off: softcode
        // power(-2,0.5) answers "Ind" while Lua (-2)^0.5 answers "nan", in the
        // interpreter, with no JIT involved.  The two languages disagree by
        // design and the compiled path must follow Lua, not softcode.
        //
        // tier-2 `pow` blob — same path softcode power() uses — not the
        // string-bridge ECALL __LUA_POW.  That ECALL read both args with
        // atof(farg_cstr()), but HIR_CALL marshals TY_FLOAT as the raw
        // double storage address; low bytes are 0x00 so atof sees "" → 0
        // and every runtime ^ became pow(0,0) == 1 (#1538).  Constant ^
        // never hit it (Lua folds those to LOADF).
        case OP_LUA_POW: {
            int rb = lua_reg[insn.B()];
            int rc_val = lua_reg[insn.C()];
            if (rb < 0 || rc_val < 0) return -1;
            rb = promote_to_float(h, truth_tag, rb);
            rc_val = promote_to_float(h, truth_tag, rc_val);
            if (rb < 0 || rc_val < 0) return -1;
            uint64_t addr = tier2_sym_addr("pow");
            if (!addr) return -1;
            lua_reg[A] = h.emit(HIR_FCALL2, TY_FLOAT, rb, rc_val,
                                static_cast<int64_t>(addr));
            if (lua_reg[A] < 0) return -1;
            h.func_idx[lua_reg[A]] = FMATH_POW;
            h.native_ops++;
            break;
        }

        case OP_LUA_UNM: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            if (h.ty[rb] == TY_FLOAT) {
                lua_reg[A] = h.emit(HIR_FNEG, TY_FLOAT, rb);
            } else if (h.ty[rb] == TY_INT) {
                lua_reg[A] = h.emit(HIR_NEG, TY_INT, rb);
            } else if (h.ty[rb] == TY_STRING) {
                rb = promote_to_int(h, truth_tag, rb);
                if (rb < 0) return -1;
                lua_reg[A] = h.emit(HIR_NEG, TY_INT, rb);
            } else {
                return -1;
            }
            if (lua_reg[A] < 0) return -1;
            h.native_ops++;
            break;
        }

        // ---- Bitwise operations ----

#define BITOP_RR(HIR_OP) \
        { \
            int rb = lua_reg[insn.B()]; \
            int rc_val = lua_reg[insn.C()]; \
            if (rb < 0 || rc_val < 0) return -1; \
            if (lua_is_handle(h, rb) || lua_is_handle(h, rc_val)) return -1; \
            rb = promote_to_int(h, truth_tag, rb); \
            rc_val = promote_to_int(h, truth_tag, rc_val); \
            if (rb < 0 || rc_val < 0) return -1; \
            lua_reg[A] = h.emit(HIR_OP, TY_INT, rb, rc_val); \
            if (lua_reg[A] < 0) return -1; \
            h.native_ops++; \
            /* MMBIN is a no-op case — do not double-advance pc (#1309). */ \
            break; \
        }

        case OP_LUA_BAND: BITOP_RR(HIR_BAND)
        case OP_LUA_BOR:  BITOP_RR(HIR_BOR)
        case OP_LUA_BXOR: BITOP_RR(HIR_BXOR)
        case OP_LUA_SHL:  BITOP_RR(HIR_SHL)
        case OP_LUA_SHR:  BITOP_RR(HIR_SHR)
#undef BITOP_RR

        case OP_LUA_BNOT: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            rb = promote_to_int(h, truth_tag, rb);
            if (rb < 0) return -1;
            lua_reg[A] = h.emit(HIR_BNOT, TY_INT, rb);
            if (lua_reg[A] < 0) return -1;
            h.native_ops++;
            break;
        }

        // SHRI/SHLI: shift by immediate (sC field).
        case OP_LUA_SHRI: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            rb = promote_to_int(h, truth_tag, rb);
            if (rb < 0) return -1;
            int imm = h.emit_iconst(insn.sC());
            if (imm < 0) return -1;
            lua_reg[A] = h.emit(HIR_SHR, TY_INT, rb, imm);
            if (lua_reg[A] < 0) return -1;
            h.native_ops++;
            break;
        }

        case OP_LUA_SHLI: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            rb = promote_to_int(h, truth_tag, rb);
            if (rb < 0) return -1;
            int imm = h.emit_iconst(insn.sC());
            if (imm < 0) return -1;
            lua_reg[A] = h.emit(HIR_SHL, TY_INT, rb, imm);
            if (lua_reg[A] < 0) return -1;
            h.native_ops++;
            break;
        }

        // Bitwise with constant (BANDK/BORK/BXORK).
#define BITOP_RK(HIR_OP) \
        { \
            int rb = lua_reg[insn.B()]; \
            if (rb < 0) return -1; \
            if (lua_is_handle(h, rb)) return -1; \
            rb = promote_to_int(h, truth_tag, rb); \
            if (rb < 0) return -1; \
            int kidx = insn.C(); \
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size())) \
                return -1; \
            int kval = emit_lua_constant(h, rc, proto->constants[kidx]); \
            if (kval < 0 || h.ty[kval] != TY_INT) return -1; \
            lua_reg[A] = h.emit(HIR_OP, TY_INT, rb, kval); \
            if (lua_reg[A] < 0) return -1; \
            h.native_ops++; \
            break; \
        }

        case OP_LUA_BANDK: BITOP_RK(HIR_BAND)
        case OP_LUA_BORK:  BITOP_RK(HIR_BOR)
        case OP_LUA_BXORK: BITOP_RK(HIR_BXOR)
#undef BITOP_RK

        // ---- Logical NOT ----

        case OP_LUA_NOT: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            // Marshalled CALL_STR text is not a Lua value: not("0") and
            // not(false→"0") disagree, and the type is gone (#1764).
            if (lua_is_marshalled_str(h, rb)) return -1;
            // CALL_VAL handle: ask the VM (only nil/false are falsy).
            if (lua_is_handle(h, rb) && h.kind[rb] == HIR_LUA_CALL_VAL) {
                int tb = h.emit(HIR_LUA_TOBOOL, TY_INT, rb);
                if (tb < 0) return -1;
                lua_reg[A] = h.emit(HIR_NOT, TY_INT, tb);
                if (lua_reg[A] < 0) return -1;
                lua_truth_set(truth_tag, lua_reg[A], LUA_TRUTH_BOOL);
                h.ecalls++;
                h.native_ops++;
                break;
            }
            if (lua_is_handle(h, rb)) return -1;
            // NOT in Lua: false and nil → true (1), everything else → false (0).
            // HIR_NOT is integer zero-test — correct only for BOOL tags.
            // VALUE (including integer 0 and "") is always truthy → NOT 0.
            // NIL → NOT 1.  UNKNOWN (tag lost) declines rather than lie (#1768).
            // Result is itself a boolean.
            const lua_truth tr = lua_truth_of(h, truth_tag, rb);
            if (tr == LUA_TRUTH_UNKNOWN) {
                return -1;
            }
            if (tr == LUA_TRUTH_NIL) {
                lua_reg[A] = h.emit_iconst(1);
            } else if (tr == LUA_TRUTH_VALUE) {
                lua_reg[A] = h.emit_iconst(0);
            } else if (h.ty[rb] == TY_INT) {
                // BOOL: 0 → 1, nonzero → 0.
                if (h.kind[rb] == HIR_ICONST) {
                    lua_reg[A] = h.emit_iconst(h.val[rb] == 0 ? 1 : 0);
                } else {
                    lua_reg[A] = h.emit(HIR_NOT, TY_INT, rb);
                    h.native_ops++;
                }
            } else {
                return -1;
            }
            if (lua_reg[A] < 0) return -1;
            lua_truth_set(truth_tag, lua_reg[A], LUA_TRUTH_BOOL);
            break;
        }

        // ---- String length (ECALL back to Lua VM) ----

        case OP_LUA_LEN: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            // Lua raises "attempt to get length of a nil value"; the
            // empty SCONST would measure 0 instead.
            if (lua_is_nil(h, truth_tag, rb)) return -1;
            // #mux answered 3 -- strlen of the sentinel's NAME.  #1424's
            // shape on a different value class (#1795).  The mux.args
            // sentinel has its own arity path further down; this refuses
            // the rest.
            if (lua_is_mux_sentinel(h, rb) && h.sval[rb] != "mux.args") {
                return -1;
            }
            // `#` on a VM reference is what returned 22 for a three-element
            // table: the stack index, measured as though it were the value
            // (#1424, #1579).  Declining kept it correct; asking the VM
            // makes it fast as well, and the index never leaves a register
            // where something could measure it as text.
            if (lua_is_handle(h, rb)) {
                lua_reg[A] = h.emit(HIR_LUA_LEN, TY_INT, rb);
                if (lua_reg[A] < 0) return -1;
                h.known_int[lua_reg[A]] = true;
                h.ecalls++;
                break;
            }

            // A mux.* table is carried through lowering as an SCONST holding
            // its NAME -- "mux.args" is a sentinel, not text the program can
            // see.  It is not a handle, and it IS TY_STRING, so both guards
            // above wave it through and a naive STRLEN would measure the
            // sentinel: `#mux.args` answered 8 (strlen "mux.args") where the
            // interpreter answers the argument count (TC020 under #1326).
            //
            // `#mux.args` is the call's ncargs.  Softcode already parks that
            // count in SUBST_NCARGS for `%+`; reuse it so nested production
            // brackets can compile this shape instead of declining forever.
            //
            if (  lua_is_mux_sentinel(h, rb)
               && h.kind[rb] == HIR_SCONST
               && h.sval[rb] == "mux.args") {
                uint64_t addr = rv_compiler::SUBST_BASE
                    + static_cast<uint64_t>(rv_compiler::SUBST_NCARGS)
                      * rv_compiler::SUBST_SLOT;
                h.needs_jit = true;
                int sref = h.emit_sref(addr);
                if (sref < 0) {
                    return -1;
                }
                lua_reg[A] = h.emit(HIR_ATOI, TY_INT, sref);
                if (lua_reg[A] < 0) {
                    return -1;
                }
                h.known_int[lua_reg[A]] = true;
                break;
            }
            // Other mux.* sentinels still have no length semantics here.
            //
            if (lua_is_mux_sentinel(h, rb)) {
                return -1;
            }

            // For TY_STRING: emit strlen-like ECALL.
            // For other types: would need lua_State to call __len metamethod.
            if (h.ty[rb] == TY_STRING) {
                // Use engine API STRLEN function if available.
                int fidx = engine_api_lookup("STRLEN");
                if (fidx > 0) {
                    int args[] = { rb };
                    lua_reg[A] = h.emit_call(TY_STRING, fidx, args, 1);
                    if (lua_reg[A] < 0) return -1;
                    h.known_int[lua_reg[A]] = true;
                    h.ecalls++;
                } else {
                    return -1;
                }
            } else {
                return -1;  // Table/userdata length needs lua_State.
            }
            break;
        }

        // ---- String concatenation ----

        case OP_LUA_CONCAT: {
            // OP_CONCAT A B: concatenate B values starting at R(A),
            // result in R(A).
            int nvals = insn.B();
            if (nvals < 1) return -1;
            // Concatenating a handle would splice a stack index into the
            // text (#1579).
            for (int ci = 0; ci < nvals; ci++) {
                if (!lua_reg_in_range(A + ci)) return -1;
                if (lua_is_handle(h, lua_reg[A + ci])) return -1;
                // A sentinel is a table in Lua; concatenating it raises,
                // and splicing its NAME in would answer "xmux.args".
                if (lua_is_mux_sentinel(h, lua_reg[A + ci])) return -1;
                // Lua raises "attempt to concatenate a nil value"; the
                // empty SCONST would splice in as "".
                if (lua_is_nil(h, truth_tag, lua_reg[A + ci])) return -1;
            }
            if (nvals == 1) {
                // Single value — no-op (just ensure it's a string).
                int rv = lua_reg[A];
                if (rv < 0) return -1;
                if (h.ty[rv] == TY_INT) {
                    lua_reg[A] = h.emit(HIR_ITOA, TY_STRING, rv);
                } else if (h.ty[rv] == TY_FLOAT) {
                    lua_reg[A] = h.emit(HIR_FTOA, TY_STRING, rv);
                }
                break;
            }

            // Convert all operands to strings, then emit HIR_STRCAT.
            std::vector<int> str_args;
            for (int j = 0; j < nvals; j++) {
                if (!lua_reg_in_range(A + j)) return -1;
                int rv = lua_reg[A + j];
                if (rv < 0) return -1;
                if (h.ty[rv] == TY_INT) {
                    rv = h.emit(HIR_ITOA, TY_STRING, rv);
                    if (rv < 0) return -1;
                } else if (h.ty[rv] == TY_FLOAT) {
                    rv = h.emit(HIR_FTOA, TY_STRING, rv);
                    if (rv < 0) return -1;
                }
                str_args.push_back(rv);
            }

            lua_reg[A] = h.emit_strcat(str_args.data(),
                static_cast<int>(str_args.size()));
            if (lua_reg[A] < 0) return -1;
            h.ecalls++;
            break;
        }

        // ---- Table operations (ECALL back to Lua VM) ----
        //
        // Tables live on the Lua stack, referenced by stack index.
        // NEWTABLE creates a table and returns its stack index (TY_INT).
        // GETI/SETI/GETFIELD/SETFIELD operate via ECALL, marshalling
        // values between guest memory and the Lua stack.

        case OP_LUA_NEWTABLE: {
            // A = dest register, B = array hint, C = hash hint.
            // Extra size info may be in a following EXTRAARG instruction.
            int narr = insn.B();
            int nrec = insn.C();
            // Emit ECALL_LUA_NEWTABLE: a0=narr, a1=nrec → a0=stack_idx.
            int v_narr = h.emit_iconst(narr);
            int v_nrec = h.emit_iconst(nrec);
            if (v_narr < 0 || v_nrec < 0) return -1;

            // Dedicated opcode, not a named HIR_CALL.  The named form went
            // through an ECALL that marshalled the stack index as a decimal
            // string; nothing ever completed through it and it is gone
            // (#1519).  This keeps the index in a register, typed.
            lua_reg[A] = h.emit(HIR_LUA_NEWTABLE, TY_LUA_HANDLE,
                                v_narr, v_nrec);
            if (lua_reg[A] < 0) return -1;
            // Mark this as known-integer (it's a stack index).
            h.known_int[lua_reg[A]] = true;
            // A table born here is PROVEN plain until it escapes as a
            // call argument; see lua_referent::plain_proven.  keys_closed
            // starts true with an empty int_keys set — every constant-key
            // store is recorded until a dynamic-key store opens it.
            {
                lua_referent nt;
                nt.plain_proven = true;
                nt.keys_closed = true;
                lua_ref[lua_reg[A]] = nt;
            }
            h.ecalls++;
            break;
        }

        case OP_LUA_GETTABI: {
            // A = dest, B = table register, C = integer key.
            int tbl = lua_reg[insn.B()];
            if (tbl < 0) return -1;

            // mux.args[N] (1-based) → softcode CARGS slot N-1.  The mux.*
            // bridge lowers `mux`/`args` to SCONST sentinels rather than a
            // live Lua table; treating those as stack indices for
            // HIR_LUA_GETI caused runaway DBT dispatch (#1309).
            //
            if (lua_is_mux_sentinel(h, tbl)) {
                if (h.sval[tbl] == "mux.args") {
                    int key = insn.C();
                    if (key < 1 || key > rv_compiler::MAX_CARGS) {
                        return -1;
                    }
                    uint64_t carg_addr = rv_compiler::CARGS_BASE
                        + static_cast<uint64_t>(key - 1)
                          * rv_compiler::CARGS_SLOT;
                    h.needs_jit = true;
                    lua_reg[A] = h.emit_sref(carg_addr);
                    if (lua_reg[A] < 0) return -1;
                    break;
                }
                // Other mux.* tables are not indexable on the JIT path yet.
                if (h.sval[tbl].rfind("mux.", 0) == 0) {
                    return -1;
                }
            }

            // Typed fast read requires the PLAIN PROOF (#1751 Phase 1):
            // GETI's integer claim is sound only for a table this chunk
            // built and never let escape.  Any other handle -- a global,
            // a member, an escaped local -- may carry a metatable or
            // non-integer values, and the claim-miss path would continue
            // with 0: a silent wrong answer, proved in review.  Chunk is
            // ineligible instead; the interpreter answers.
            const lua_referent tref = lua_referent_of(lua_ref, tbl);
            if (!tref.plain_proven) {
                return -1;
            }

            const int64_t ikey = static_cast<int64_t>(insn.C());
            // Known-absent under a closed key set: the integer slot cannot
            // carry nil, and continuing with 0 was a silent wrong answer.
            // Emit LOADNIL's representation at lowering — no post-entry
            // GETI_INT miss.
            if (tref.keys_closed
                && tref.int_keys.find(ikey) == tref.int_keys.end()) {
                lua_reg[A] = h.emit_sconst(rc.pool_str("", 0), "");
                if (lua_reg[A] < 0) return -1;
                lua_truth_set(truth_tag, lua_reg[A], LUA_TRUTH_NIL);
                break;
            }

            int key = h.emit_iconst(insn.C());
            if (key < 0) return -1;

            // Use integer fast-path: returns TY_INT directly, no string.
            lua_reg[A] = h.emit(HIR_LUA_GETI, TY_INT, tbl, key);
            if (lua_reg[A] < 0) return -1;
            h.ecalls++;
            break;
        }

        case OP_LUA_SETTABI: {
            // A = table register, B = integer key, C = value register --
            // or, when k is set, a CONSTANT index rather than a register.
            // Reading lua_reg[C] in that case yields -1 and the chunk
            // declines, which is why `t[1]=5` did: the 5 is a constant.
            int tbl = lua_reg[A];
            if (tbl < 0) return -1;
            int val;
            if (insn.k()) {
                if (insn.C() < 0
                 || insn.C() >= static_cast<int>(proto->constants.size())) {
                    return -1;
                }
                const lua_bc_constant &kv = proto->constants[insn.C()];
                if (kv.type != LUA_BC_TINT) return -1;   // ints only, as below
                val = h.emit_iconst(kv.ival);
            } else {
                val = lua_reg[insn.C()];
            }
            if (val < 0) return -1;

            // Integer values only.  The dedicated ECALL carries the value
            // in a register (a2), so there is nowhere for a string to ride;
            // the named form it replaces stringified everything and never
            // completed (#1519).  Decline the rest rather than invent a
            // marshalling for it -- the interpreter answers, correctly.
            if (h.ty[val] != TY_INT) return -1;
            if (lua_is_handle(h, val)) return -1;

            int key = h.emit_iconst(insn.B());
            if (key < 0) return -1;

            // In a loop proto the run may be re-run on the interpreter
            // after budget exhaustion, so stores must be chunk-local: a
            // store into a global-shaped table would happen twice (#1732).
            if (proto_has_loop
                && lua_referent_of(lua_ref, tbl).fields_are_refs) {
                return -1;
            }

            // Third operand rides in val[]; see hir_val_operand() in hir.h,
            // which the liveness walker consults so the register holding the
            // stored value is not recycled before the ECALL reads it.
            if (h.emit(HIR_LUA_SETI, TY_VOID, tbl, key, val) < 0) return -1;
            // SETTABI's key is always the constant insn.B().
            lua_note_int_key_store(lua_ref, tbl, static_cast<int64_t>(insn.B()));
            h.ecalls++;
            break;
        }

        case OP_LUA_SETLIST: {
            // A = table register, B = number of values, k+C = offset.
            // Values are in R(A+1)..R(A+B).
            int tbl = lua_reg[A];
            if (tbl < 0) return -1;
            // Same rerun-safety rule as SETTABI (#1732).
            if (proto_has_loop
                && lua_referent_of(lua_ref, tbl).fields_are_refs) {
                return -1;
            }
            int nvals = insn.B();
            int offset = insn.C();
            // k flag indicates extra offset from following EXTRAARG.
            if (insn.k() && pc + 1 < n) {
                offset += proto->code[pc + 1].Ax() * (1 << 8);
                // Don't skip EXTRAARG here — it will be skipped as
                // unsupported if we don't handle it, but SETLIST
                // consumed the info.
            }

            for (int j = 1; j <= nvals; j++) {
                if (!lua_reg_in_range(A + j)) return -1;
                int val = lua_reg[A + j];
                if (val < 0) return -1;

                // Integer elements only, as for OP_LUA_SETTABI: the
                // dedicated ECALL carries the value in a register, so there
                // is nowhere for a string to ride.  A constructor holding
                // anything else declines and the interpreter answers.
                if (h.ty[val] != TY_INT) return -1;
                if (lua_is_handle(h, val)) return -1;

                int key = h.emit_iconst(offset + j);
                if (key < 0) return -1;

                // Third operand rides in val[]; hir_val_operand() in hir.h
                // is what keeps the liveness walker from recycling the
                // register before the ECALL reads it.
                if (h.emit(HIR_LUA_SETI, TY_VOID, tbl, key, val) < 0) {
                    return -1;
                }
                lua_note_int_key_store(lua_ref, tbl,
                    static_cast<int64_t>(offset + j));
                h.ecalls++;
            }
            break;
        }

        // GETTABLE: A = dest, B = table register, C = key register.
        case OP_LUA_GETTABLE: {
            int tbl = lua_reg[insn.B()];
            int key = lua_reg[insn.C()];
            if (tbl < 0 || key < 0) return -1;

            // mux.args[k] with compile-time integer key → CARGS (see GETTABI).
            //
            if (  lua_is_mux_sentinel(h, tbl)
               && h.kind[tbl] == HIR_SCONST
               && h.sval[tbl] == "mux.args"
               && h.kind[key] == HIR_ICONST) {
                int k = static_cast<int>(h.val[key]);
                if (k < 1 || k > rv_compiler::MAX_CARGS) {
                    return -1;
                }
                uint64_t carg_addr = rv_compiler::CARGS_BASE
                    + static_cast<uint64_t>(k - 1) * rv_compiler::CARGS_SLOT;
                h.needs_jit = true;
                lua_reg[A] = h.emit_sref(carg_addr);
                if (lua_reg[A] < 0) return -1;
                break;
            }

            if (h.ty[key] == TY_INT && pinned_tbl_reg >= 0
                && insn.B() == pinned_tbl_reg) {
                // Pinned array: native memory load, no ECALL.
                lua_reg[A] = h.emit(HIR_LUA_ALOAD, TY_INT, key, -1,
                    static_cast<int64_t>(rv_compiler::LUA_ARRAY_BASE));
                if (lua_reg[A] < 0) return -1;
                h.native_ops++;
                h.ecalls--;  // replaces an ECALL
            } else if (h.ty[key] == TY_INT) {
                // Same plain-proof rule as GETTABI.
                const lua_referent tref = lua_referent_of(lua_ref, tbl);
                if (!tref.plain_proven) {
                    return -1;
                }
                // Constant key under a closed set: known-absent → nil.
                if (h.kind[key] == HIR_ICONST
                    && tref.keys_closed
                    && tref.int_keys.find(h.val[key]) == tref.int_keys.end()) {
                    lua_reg[A] = h.emit_sconst(rc.pool_str("", 0), "");
                    if (lua_reg[A] < 0) return -1;
                    lua_truth_set(truth_tag, lua_reg[A], LUA_TRUTH_NIL);
                    break;
                }
                // Integer key: use fast-path ECALL, returns TY_INT.
                lua_reg[A] = h.emit(HIR_LUA_GETI, TY_INT, tbl, key);
                if (lua_reg[A] < 0) return -1;
            } else {
                // String/float key: use general ECALL path.
                if (h.ty[key] == TY_FLOAT) {
                    key = h.emit(HIR_FTOA, TY_STRING, key);
                    if (key < 0) return -1;
                }
                // No handler exists for a general (string/float key)
                // table read; emitting the named ECALL could only fail
                // loudly post-entry.  Ineligible at lowering instead
                // (#1751 rule 1): the interpreter answers.
                return -1;
            }
            h.ecalls++;
            break;
        }

        // GETFIELD: A = dest, B = table register, C = key constant index.
        // (General case — not the mux.* bridge pattern, which is handled
        // separately via GETTABUP+GETFIELD.)
        case OP_LUA_GETFIELD: {
            int table_reg = lua_reg[insn.B()];
            if (table_reg < 0) return -1;
            int kidx = insn.C();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            const lua_bc_constant &k = proto->constants[kidx];
            if (k.type != LUA_BC_TSHRSTR && k.type != LUA_BC_TLNGSTR)
                return -1;

            // mux.* routing.  ONLY mux.args stays on the SCONST sentinel:
            // GETTABI on the "mux.args" marker is the native CARGS/ALOAD
            // fast path.  Every other mux member goes through the REAL
            // global table, so a compiled mux.eval(...) pcalls the same
            // bridge C function the interpreter calls -- semantics correct
            // by construction.  The old path mapped the NAME onto the
            // softcode function table instead (mux.eval -> softcode
            // eval(obj,attr), mux.name -> name() wanting a "#dbref"), which
            // default-on exposed the day smoke first ran it compiled
            // (#1745: TC013/TC014).
            const bool mux_sentinel =
                lua_is_mux_sentinel(h, table_reg)
                && h.sval[table_reg] == "mux";
            if (mux_sentinel && k.sval == "args") {
                std::string name = "mux." + k.sval;
                uint64_t addr = rc.pool_str(name.c_str(), name.size());
                lua_reg[A] = h.emit_sconst(addr, name);
                if (lua_reg[A] < 0) return -1;
                h.lua_mux_sentinel[lua_reg[A]] = true;
            } else {
                if (mux_sentinel) {
                    // Materialize the real global in place of the marker.
                    uint64_t mk = rc.pool_str("mux", 3);
                    int mkey = h.emit_sconst(mk, "mux");
                    if (mkey < 0) return -1;
                    int mh = h.emit(HIR_LUA_GETGLOBAL, TY_LUA_HANDLE, mkey);
                    if (mh < 0) return -1;
                    lua_referent g;
                    g.fields_are_refs = true;   // members are functions
                    g.callable = false;         // the table itself is not
                    lua_ref[mh] = g;
                    h.ecalls++;
                    table_reg = mh;
                }
                // General table field access via the dedicated ECALL.  The
                // key travels as an ADDRESS into the program's own string
                // pool; only the integer value comes back, in a register.
                // Non-integer fields decline inside the handler.
                if (!lua_is_handle(h, table_reg)) return -1;
                uint64_t key_addr = rc.pool_str(k.sval.c_str(), k.sval.size());
                int key_val = h.emit_sconst(key_addr, k.sval);
                if (key_val < 0) return -1;
                // Which variant depends on what the table IS -- its
                // referent.  A library table's members are functions, so
                // take a reference; a data table's members are values, so
                // take the value.  The claim was recorded where the handle
                // was created; a member reference gets its own claim here,
                // from the member's name, so a later call reads it instead
                // of walking back to this key.
                const lua_referent tref = lua_referent_of(lua_ref, table_reg);
                const hir_type vty = lua_lib_value_type(tref, k.sval);
                if (TY_VOID != vty) {
                    // A known VALUE member of a library table -- math.pi,
                    // math.maxinteger -- so take the value itself; a
                    // reference would be a handle nothing downstream can
                    // consume.  The runtime check keeps a rebound member
                    // honest: wrong type, decline.
                    lua_reg[A] = h.emit(
                        (TY_FLOAT == vty) ? HIR_LUA_GETFIELD_FLT
                                          : HIR_LUA_GETFIELD,
                        vty, table_reg, key_val);
                    if (lua_reg[A] < 0) return -1;
                    if (TY_INT == vty) {
                        h.known_int[lua_reg[A]] = true;
                    }
                    h.ecalls++;
                    break;
                }
                // The typed value read (GETFIELD_INT) needs the PLAIN
                // PROOF exactly as GETI does; a reference read does not
                // (a handle result carries no value claim to miss).
                if (!tref.fields_are_refs
                    && !tref.plain_proven) {
                    return -1;
                }
                lua_reg[A] = h.emit(
                    tref.fields_are_refs ? HIR_LUA_GETFIELD_REF
                                         : HIR_LUA_GETFIELD,
                    tref.fields_are_refs ? TY_LUA_HANDLE : TY_INT,
                    table_reg, key_val);
                if (lua_reg[A] < 0) return -1;
                if (tref.fields_are_refs) {
                    lua_referent m;
                    m.callable = true;
                    m.returns = lua_call_claim(k.sval);
                    // Bridge members that act on the world; see the
                    // effectful field's comment.  eval is on the list
                    // because it runs arbitrary softcode -- pemit inside
                    // a mux.eval doubled exactly like a direct pemit in
                    // the adversarial probe.
                    m.effectful = (k.sval == "notify" || k.sval == "pemit"
                                || k.sval == "set" || k.sval == "eval");
                    lua_ref[lua_reg[A]] = m;
                }
                h.known_int[lua_reg[A]] = true;
                h.ecalls++;
            }
            break;
        }

        // SETTABLE: A = table register, B = key register, C = value register.
        case OP_LUA_SETTABLE: {
            int tbl = lua_reg[A];
            int key = lua_reg[insn.B()];
            int val = lua_reg[insn.C()];
            if (tbl < 0 || key < 0 || val < 0) return -1;
            if (h.ty[key] == TY_INT) {
                key = h.emit(HIR_ITOA, TY_STRING, key);
                if (key < 0) return -1;
            }
            if (h.ty[val] == TY_INT) {
                val = h.emit(HIR_ITOA, TY_STRING, val);
                if (val < 0) return -1;
            } else if (h.ty[val] == TY_FLOAT) {
                val = h.emit(HIR_FTOA, TY_STRING, val);
                if (val < 0) return -1;
            }
            // No handler exists for a runtime-keyed store; the named
            // ECALL could only fail loudly post-entry -- which is what
            // turned the plain `t[i]=v` loop into a loud diverge under
            // Phase 0.  Ineligible at lowering instead (#1751 rule 1).
            return -1;
        }

        // SETFIELD: A = table register, B = key constant index, C = value register.
        case OP_LUA_SETFIELD: {
            int tbl = lua_reg[A];
            if (tbl < 0) return -1;
            // C is a CONSTANT index, not a register, when k is set -- the
            // same shape that made `t[1]=5` decline on OP_LUA_SETTABI.
            int val;
            if (insn.k()) {
                if (insn.C() < 0
                 || insn.C() >= static_cast<int>(proto->constants.size())) {
                    return -1;
                }
                const lua_bc_constant &kv = proto->constants[insn.C()];
                if (kv.type != LUA_BC_TINT) return -1;
                val = h.emit_iconst(kv.ival);
            } else {
                val = lua_reg[insn.C()];
            }
            if (val < 0) return -1;
            int kidx = insn.B();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            const lua_bc_constant &k = proto->constants[kidx];
            if (k.type != LUA_BC_TSHRSTR && k.type != LUA_BC_TLNGSTR)
                return -1;
            // Integer values only, as for the integer-keyed stores: the
            // ECALL carries the value in a register.
            if (h.ty[val] != TY_INT) return -1;
            if (lua_is_handle(h, val)) return -1;
            if (!lua_is_handle(h, tbl)) return -1;

            // Same rerun-safety rule as SETTABI (#1732).
            if (proto_has_loop
                && lua_referent_of(lua_ref, tbl).fields_are_refs) {
                return -1;
            }

            uint64_t key_addr = rc.pool_str(k.sval.c_str(), k.sval.size());
            int key_val = h.emit_sconst(key_addr, k.sval);
            if (key_val < 0) return -1;
            // Value rides in val[]; hir_val_operand() knows about SETFIELD
            // as well as SETI, which is what keeps the register alive.
            if (h.emit(HIR_LUA_SETFIELD, TY_VOID, tbl, key_val, val) < 0) {
                return -1;
            }
            h.ecalls++;
            break;
        }

        // ---- Immediate arithmetic ----

        case OP_LUA_ADDI: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            // promote_to_int already refuses CALL_STR; guard the float/int
            // arms too so a marshalled result cannot fall into ITOF/ADD.
            if (lua_is_marshalled_str(h, rb)) return -1;
            if (h.ty[rb] == TY_FLOAT) {
                int imm_val = h.emit_fconst(static_cast<double>(insn.sC()));
                if (imm_val < 0) return -1;
                lua_reg[A] = h.emit(HIR_FADD, TY_FLOAT, rb, imm_val);
            } else if (h.ty[rb] == TY_INT) {
                int imm_val = h.emit_iconst(insn.sC());
                if (imm_val < 0) return -1;
                lua_reg[A] = h.emit(HIR_ADD, TY_INT, rb, imm_val);
            } else if (h.ty[rb] == TY_STRING) {
                rb = promote_to_int(h, truth_tag, rb);
                if (rb < 0) return -1;
                int imm_val = h.emit_iconst(insn.sC());
                if (imm_val < 0) return -1;
                lua_reg[A] = h.emit(HIR_ADD, TY_INT, rb, imm_val);
            } else {
                return -1;
            }
            if (lua_reg[A] < 0) return -1;
            h.native_ops++;
            break;
        }

        // ---- Constant arithmetic ----

#define ARITH_RK(HIR_INT_OP, HIR_FP_OP) \
        { \
            int rb = lua_reg[insn.B()]; \
            if (rb < 0) return -1; \
            if (lua_is_handle(h, rb)) return -1; \
            int kidx = insn.C(); \
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size())) \
                return -1; \
            int kval = emit_lua_constant(h, rc, proto->constants[kidx]); \
            if (kval < 0) return -1; \
            if (either_float(h, rb, kval)) { \
                rb = promote_to_float(h, truth_tag, rb); \
                kval = promote_to_float(h, truth_tag, kval); \
                if (rb < 0 || kval < 0) return -1; \
                lua_reg[A] = h.emit(HIR_FP_OP, TY_FLOAT, rb, kval); \
            } else if (h.ty[rb] == TY_INT && h.ty[kval] == TY_INT) { \
                lua_reg[A] = h.emit(HIR_INT_OP, TY_INT, rb, kval); \
            } else { \
                rb = promote_to_int(h, truth_tag, rb); \
                kval = promote_to_int(h, truth_tag, kval); \
                if (rb < 0 || kval < 0) return -1; \
                lua_reg[A] = h.emit(HIR_INT_OP, TY_INT, rb, kval); \
            } \
            if (lua_reg[A] < 0) return -1; \
            h.native_ops++; \
            break; \
        }

        case OP_LUA_ADDK:  ARITH_RK(HIR_ADD, HIR_FADD)
        case OP_LUA_SUBK:  ARITH_RK(HIR_SUB, HIR_FSUB)
        case OP_LUA_MULK:  ARITH_RK(HIR_MUL, HIR_FMUL)
        case OP_LUA_IDIVK: ARITH_RK(HIR_DIV, HIR_DIV)
        case OP_LUA_MODK:  ARITH_RK(HIR_REM, HIR_REM)
#undef ARITH_RK

        // DIVK: Lua `/` with constant — always float.
        case OP_LUA_DIVK: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            int kidx = insn.C();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            int kval = emit_lua_constant(h, rc, proto->constants[kidx]);
            if (kval < 0) return -1;
            rb = promote_to_float(h, truth_tag, rb);
            kval = promote_to_float(h, truth_tag, kval);
            if (rb < 0 || kval < 0) return -1;
            lua_reg[A] = h.emit(HIR_FDIV, TY_FLOAT, rb, kval);
            if (lua_reg[A] < 0) return -1;
            h.native_ops++;
            break;
        }

        // POWK: exponentiation with constant K — always float.  Same
        // native FCALL2 as OP_POW (#1538); do not string-bridge.
        // Same reasoning as OP_LUA_POW above: no HAVE_IEEE_FP_SNAN guard,
        // because Lua's interpreter has no "Ind" convention to match (#1556).
        case OP_LUA_POWK: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            int kidx = insn.C();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            int kval = emit_lua_constant(h, rc, proto->constants[kidx]);
            if (kval < 0) return -1;
            rb = promote_to_float(h, truth_tag, rb);
            kval = promote_to_float(h, truth_tag, kval);
            if (rb < 0 || kval < 0) return -1;
            uint64_t addr = tier2_sym_addr("pow");
            if (!addr) return -1;
            lua_reg[A] = h.emit(HIR_FCALL2, TY_FLOAT, rb, kval,
                                static_cast<int64_t>(addr));
            if (lua_reg[A] < 0) return -1;
            h.func_idx[lua_reg[A]] = FMATH_POW;
            h.native_ops++;
            break;
        }

        // ---- Comparisons ----
        // All share: compare → optional negate → JMP → BRC

// CMP_RR: == / order on a marshalled CALL_STR result compares text, not
// the Lua value (0 == "0", false == "0", ...).  Refuse those (#1764).
// Stack handles (CALL_VAL and any TY_LUA_HANDLE) use the VM for EQ only
// (HIR_LUA_EQ); order comparisons still decline.
//
// #1835: EQK/EQI already type-class gate and encode NIL/BOOL kinds; the
// register-register path did neither — mixed types ATOI-coerced to true,
// and a NIL-tagged empty SCONST was sent as kind 1 (string "").
//
#define CMP_RR(HIR_INT_OP, HIR_FP_OP) \
        { \
            int rb = lua_reg[A]; \
            int rc_val = lua_reg[insn.B()]; \
            if (rb < 0 || rc_val < 0) return -1; \
            if (lua_is_handle(h, rb) || lua_is_handle(h, rc_val)) { \
                if ((HIR_INT_OP) != HIR_EQ) return -1; \
                int lhs = rb, rhs = rc_val; \
                int kind = 2; \
                if (lua_is_handle(h, rb) && lua_is_handle(h, rc_val)) { \
                    kind = 2; \
                } else if (lua_is_handle(h, rb)) { \
                    lhs = rb; \
                    rhs = lua_eq_kind_of_value(h, truth_tag, rc_val, &kind); \
                    if (rhs < 0) return -1; \
                } else if (lua_is_handle(h, rc_val)) { \
                    lhs = rc_val; \
                    rhs = lua_eq_kind_of_value(h, truth_tag, rb, &kind); \
                    if (rhs < 0) return -1; \
                } else { \
                    return -1; \
                } \
                int cmp = h.emit(HIR_LUA_EQ, TY_INT, lhs, rhs, kind); \
                if (cmp < 0) return -1; \
                h.ecalls++; \
                if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block, \
                                    cur_hir_block, n, lua_reg, multi_block) < 0) \
                    return -1; \
                pc++; \
                break; \
            } \
            if (lua_is_marshalled_str(h, rb) || lua_is_marshalled_str(h, rc_val)) \
                return -1; \
            if (lua_is_mux_sentinel(h, rb) || lua_is_mux_sentinel(h, rc_val)) \
                return -1; \
            /* Type-class gate (#1835 / #1770): Lua == is false across */ \
            /* types; order raises on mixed types.  HIR erases BOOL/0 and */ \
            /* NIL/"" and the old promote_to_int path made "5"==5 true. */ \
            const lua_type_class ltc = \
                lua_type_class_of_value(h, truth_tag, rb); \
            const lua_type_class rtc = \
                lua_type_class_of_value(h, truth_tag, rc_val); \
            if (ltc == LUA_TC_OTHER || rtc == LUA_TC_OTHER) { \
                return -1; \
            } \
            if ((HIR_INT_OP) == HIR_EQ) { \
                if (ltc != rtc || ltc == LUA_TC_NIL) { \
                    int cmp = h.emit_iconst( \
                        (ltc == LUA_TC_NIL && rtc == LUA_TC_NIL) ? 1 : 0); \
                    if (cmp < 0) return -1; \
                    h.native_ops++; \
                    if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, \
                                        pc_to_block, cur_hir_block, n, \
                                        lua_reg, multi_block) < 0) \
                        return -1; \
                    pc++; \
                    break; \
                } \
            } else { \
                /* Order: interpreter raises on mixed / non-orderable. */ \
                if (ltc != rtc) return -1; \
                if (ltc != LUA_TC_NUMBER && ltc != LUA_TC_STRING) return -1; \
            } \
            int cmp; \
            if (either_float(h, rb, rc_val)) { \
                rb = promote_to_float(h, truth_tag, rb); \
                rc_val = promote_to_float(h, truth_tag, rc_val); \
                if (rb < 0 || rc_val < 0) return -1; \
                cmp = h.emit(HIR_FP_OP, TY_INT, rb, rc_val); \
            } else if (h.ty[rb] == TY_INT && h.ty[rc_val] == TY_INT) { \
                cmp = h.emit(HIR_INT_OP, TY_INT, rb, rc_val); \
            } else if (h.ty[rb] == TY_STRING && h.ty[rc_val] == TY_STRING) { \
                int sc = h.emit(HIR_STRCMP, TY_INT, rb, rc_val); \
                if (sc < 0) return -1; \
                int zero = h.emit_iconst(0); \
                cmp = h.emit(HIR_INT_OP, TY_INT, sc, zero); \
            } else { \
                /* Same type class but HIR types differ (e.g. int vs float */ \
                /* already handled).  Decline rather than cross-coerce. */ \
                return -1; \
            } \
            h.native_ops++; \
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block, \
                                cur_hir_block, n, lua_reg, multi_block) < 0) \
                return -1; \
            pc++; \
            break; \
        }

#define CMP_RI(HIR_INT_OP, HIR_FP_OP) \
        { \
            int rb = lua_reg[A]; \
            if (rb < 0) return -1; \
            if (lua_is_handle(h, rb)) return -1; \
            if (lua_is_nil(h, truth_tag, rb)) return -1; \
            if (lua_is_mux_sentinel(h, rb)) return -1; \
            int cmp; \
            if (h.ty[rb] == TY_FLOAT) { \
                int fimm = h.emit_fconst(static_cast<double>(insn.sB())); \
                if (fimm < 0) return -1; \
                cmp = h.emit(HIR_FP_OP, TY_INT, rb, fimm); \
            } else if (h.ty[rb] == TY_INT) { \
                int imm_val = h.emit_iconst(insn.sB()); \
                if (imm_val < 0) return -1; \
                cmp = h.emit(HIR_INT_OP, TY_INT, rb, imm_val); \
            } else if (h.ty[rb] == TY_STRING) { \
                rb = promote_to_int(h, truth_tag, rb); \
                if (rb < 0) return -1; \
                int imm_val = h.emit_iconst(insn.sB()); \
                if (imm_val < 0) return -1; \
                cmp = h.emit(HIR_INT_OP, TY_INT, rb, imm_val); \
            } else { \
                return -1; \
            } \
            h.native_ops++; \
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block, \
                                cur_hir_block, n, lua_reg, multi_block) < 0) \
                return -1; \
            pc++; \
            break; \
        }

        case OP_LUA_EQ:  CMP_RR(HIR_EQ, HIR_FEQ)
        case OP_LUA_LT:  CMP_RR(HIR_LT, HIR_FLT)
        case OP_LUA_LE:  CMP_RR(HIR_LE, HIR_FLE)

        // EQK: equality with constant from pool.
        //
        // Lua's condjump always pairs EQK with a following JMP (same shape
        // as EQ/EQI).  Value materialisation is EQK+JMP+LFALSESKIP+LOADTRUE
        // and is fused via emit_cmp_branch / lua_bool_fuse_at.  Older code
        // treated EQK as a bare one-instruction skip to pc+1/pc+2 and
        // declined the fuse shape, so `return x == "0"` never compiled
        // once x was a CALL_VAL handle (#1764 residual).
        //
        case OP_LUA_EQK: {
            int rb = lua_reg[A];
            if (rb < 0) return -1;
            // See CMP_RR: marshalled CALL_STR is text, not a Lua value (#1764).
            if (lua_is_marshalled_str(h, rb)) return -1;
            int kidx = insn.B();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            const lua_bc_constant &kconst = proto->constants[kidx];
            // Stack-handle left-hand side (CALL_VAL etc.): ask the VM so type
            // distinctions survive (tostring(0) == "0" true; tonumber("17")
            // == "17" false).  Match on TY_LUA_HANDLE, not kind==CALL_VAL:
            // SSA copies / reloads keep the type but not the producer kind.
            //
            if (lua_is_handle(h, rb)) {
                int kind = -1;
                int rhs = -1;
                if (kconst.type == LUA_BC_TNIL) {
                    kind = 3;
                    rhs = h.emit_iconst(0);
                } else if (kconst.type == LUA_BC_TFALSE) {
                    kind = 4;
                    rhs = h.emit_iconst(0);
                } else if (kconst.type == LUA_BC_TTRUE) {
                    kind = 4;
                    rhs = h.emit_iconst(1);
                } else if (kconst.type == LUA_BC_TINT) {
                    kind = 0;
                    rhs = h.emit_iconst(kconst.ival);
                } else if (kconst.type == LUA_BC_TFLOAT) {
                    // Float constant kind not wired for HIR_LUA_EQ yet.
                    return -1;
                } else if (  kconst.type == LUA_BC_TSHRSTR
                          || kconst.type == LUA_BC_TLNGSTR) {
                    kind = 1;
                    rhs = emit_lua_constant(h, rc, kconst);
                } else {
                    return -1;
                }
                if (rhs < 0) return -1;
                int cmp = h.emit(HIR_LUA_EQ, TY_INT, rb, rhs, kind);
                if (cmp < 0) return -1;
                h.ecalls++;
                // emit_cmp_branch owns k-bit polarity (EQ convention) and
                // the LFALSESKIP/LOADTRUE fuse for `return x == K`.
                if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block,
                                    cur_hir_block, n, lua_reg, multi_block) < 0)
                    return -1;
                pc++;
                break;
            }
            // nil is the empty SCONST in HIR, which is also a real "".
            // EQK's string path would make nil == "" true.  Resolve against
            // the pool type (and the lhs tag) before emitting STRCMP:
            //   nil == nil  → true
            //   nil == anything else (incl. "") → false
            // Same for false vs integer 0 once BOOL is tagged on constants.
            // Lua == is false across TYPES, and HIR erases the ones that
            // matter: false and 0 are the same ICONST, nil and "" the same
            // empty SCONST, and the numeric path would coerce "5" to 5.
            // So compare type classes first; a mismatch is constant false
            // whatever the representations say.  (The nil-only version of
            // this check still let `0 == false`, `1 == true` and
            // `"5" == 5` answer true -- measured.)
            const lua_type_class lhs_tc =
                lua_type_class_of_value(h, truth_tag, rb);
            const lua_type_class rhs_tc = lua_type_class_of_const(kconst);
            if (lhs_tc == LUA_TC_OTHER || rhs_tc == LUA_TC_OTHER) {
                return -1;
            }
            const bool lhs_nil = (lhs_tc == LUA_TC_NIL);
            const bool rhs_nil = (rhs_tc == LUA_TC_NIL);
            if (lhs_tc != rhs_tc || lhs_nil) {
                // Different types -> false.  Same type and both nil ->
                // true (nil has exactly one value).
                int cmp = h.emit_iconst((lhs_nil && rhs_nil) ? 1 : 0);
                if (cmp < 0) return -1;
                h.native_ops++;
                if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block,
                                    cur_hir_block, n, lua_reg, multi_block) < 0)
                    return -1;
                pc++;
                break;
            }
            int kval = emit_lua_constant(h, rc, kconst);
            if (kval < 0) return -1;
            lua_truth_tag_constant(truth_tag, kconst, kval);
            int cmp;
            if (either_float(h, rb, kval)) {
                rb = promote_to_float(h, truth_tag, rb);
                kval = promote_to_float(h, truth_tag, kval);
                if (rb < 0 || kval < 0) return -1;
                cmp = h.emit(HIR_FEQ, TY_INT, rb, kval);
            } else if (h.ty[rb] == TY_INT && h.ty[kval] == TY_INT) {
                cmp = h.emit(HIR_EQ, TY_INT, rb, kval);
            } else if (h.ty[rb] == TY_STRING && h.ty[kval] == TY_STRING) {
                int sc = h.emit(HIR_STRCMP, TY_INT, rb, kval);
                if (sc < 0) return -1;
                int zero = h.emit_iconst(0);
                cmp = h.emit(HIR_EQ, TY_INT, sc, zero);
            } else {
                rb = promote_to_int(h, truth_tag, rb);
                kval = promote_to_int(h, truth_tag, kval);
                if (rb < 0 || kval < 0) return -1;
                cmp = h.emit(HIR_EQ, TY_INT, rb, kval);
            }
            if (cmp < 0) return -1;
            h.native_ops++;
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block,
                                cur_hir_block, n, lua_reg, multi_block) < 0)
                return -1;
            pc++;
            break;
        }

        // EQI is equality against a small integer immediate -- a NUMBER.
        // Lua == is false across types, so a non-number lhs answers false
        // rather than coercing (`local a=false if a==0` answered true;
        // `local a="5" if a==5` likewise).  Order comparisons (LTI/LEI/
        // GTI/GEI) are different: Lua RAISES on mismatched types, so they
        // keep declining via CMP_RI's guards.
        case OP_LUA_EQI: {
            int rb = lua_reg[A];
            if (rb < 0) return -1;
            if (lua_is_handle(h, rb)) return -1;
            if (lua_is_marshalled_str(h, rb)) return -1;
            const lua_type_class lhs_tc =
                lua_type_class_of_value(h, truth_tag, rb);
            if (lhs_tc == LUA_TC_OTHER) return -1;
            if (lhs_tc != LUA_TC_NUMBER) {
                int cmp = h.emit_iconst(0);
                if (cmp < 0) return -1;
                if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block,
                                    cur_hir_block, n, nullptr,
                                    multi_block) < 0) {
                    return -1;
                }
                pc++;
                break;
            }
            CMP_RI(HIR_EQ, HIR_FEQ)
        }
        case OP_LUA_LTI: CMP_RI(HIR_LT, HIR_FLT)
        case OP_LUA_LEI: CMP_RI(HIR_LE, HIR_FLE)

        // For GT/GE with floats, we only have FLT/FLE.
        // GT(a, b) = FLT(b, a), GE(a, b) = FLE(b, a) — swap operands.
        case OP_LUA_GTI: {
            int rb = lua_reg[A];
            if (rb < 0) return -1;
            // nil has no order; empty SCONST would promote to 0.
            if (lua_is_nil(h, truth_tag, rb)) return -1;
            int cmp;
            if (h.ty[rb] == TY_FLOAT) {
                int fimm = h.emit_fconst(static_cast<double>(insn.sB()));
                if (fimm < 0) return -1;
                cmp = h.emit(HIR_FLT, TY_INT, fimm, rb);  // swapped
            } else if (h.ty[rb] == TY_INT) {
                int imm_val = h.emit_iconst(insn.sB());
                if (imm_val < 0) return -1;
                cmp = h.emit(HIR_GT, TY_INT, rb, imm_val);
            } else if (h.ty[rb] == TY_STRING) {
                rb = promote_to_int(h, truth_tag, rb);
                if (rb < 0) return -1;
                int imm_val = h.emit_iconst(insn.sB());
                if (imm_val < 0) return -1;
                cmp = h.emit(HIR_GT, TY_INT, rb, imm_val);
            } else {
                return -1;
            }
            h.native_ops++;
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block,
                                cur_hir_block, n, lua_reg, multi_block) < 0)
                return -1;
            pc++;
            break;
        }
        case OP_LUA_GEI: {
            int rb = lua_reg[A];
            if (rb < 0) return -1;
            if (lua_is_nil(h, truth_tag, rb)) return -1;
            int cmp;
            if (h.ty[rb] == TY_FLOAT) {
                int fimm = h.emit_fconst(static_cast<double>(insn.sB()));
                if (fimm < 0) return -1;
                cmp = h.emit(HIR_FLE, TY_INT, fimm, rb);  // swapped
            } else if (h.ty[rb] == TY_INT) {
                int imm_val = h.emit_iconst(insn.sB());
                if (imm_val < 0) return -1;
                cmp = h.emit(HIR_GE, TY_INT, rb, imm_val);
            } else if (h.ty[rb] == TY_STRING) {
                rb = promote_to_int(h, truth_tag, rb);
                if (rb < 0) return -1;
                int imm_val = h.emit_iconst(insn.sB());
                if (imm_val < 0) return -1;
                cmp = h.emit(HIR_GE, TY_INT, rb, imm_val);
            } else {
                return -1;
            }
            h.native_ops++;
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block,
                                cur_hir_block, n, lua_reg, multi_block) < 0)
                return -1;
            pc++;
            break;
        }
#undef CMP_RR
#undef CMP_RI

        case OP_LUA_TEST: {
            int rb = lua_reg[A];
            if (rb < 0) return -1;
            // HIR_BOOL is integer SNEZ / softcode truthiness.  A CALL_STR
            // result is type-erased text: "0", "" and integer-0-as-text are
            // all truthy in Lua and falsy under that test (#1764).  Decline
            // rather than lie; CALL_VAL uses the VM's truthiness instead.
            if (lua_is_marshalled_str(h, rb)) return -1;
            int cmp;
            if (lua_is_handle(h, rb) && h.kind[rb] == HIR_LUA_CALL_VAL) {
                cmp = h.emit(HIR_LUA_TOBOOL, TY_INT, rb);
                if (cmp < 0) return -1;
                h.ecalls++;
            } else if (lua_is_handle(h, rb)) {
                return -1;
            } else {
                // Lua: only nil and false are falsy.  VALUE (0, 0.0, "") is
                // always truthy; BOOL uses HIR_BOOL; NIL is always falsy.
                // UNKNOWN declines — do not invent VALUE (#1768).
                const lua_truth tr = lua_truth_of(h, truth_tag, rb);
                if (tr == LUA_TRUTH_UNKNOWN) {
                    return -1;
                }
                if (tr == LUA_TRUTH_NIL) {
                    cmp = h.emit_iconst(0);
                } else if (tr == LUA_TRUTH_VALUE) {
                    cmp = h.emit_iconst(1);
                } else if (h.ty[rb] == TY_INT) {
                    if (h.kind[rb] == HIR_ICONST) {
                        cmp = h.emit_iconst(h.val[rb] != 0 ? 1 : 0);
                    } else {
                        cmp = h.emit(HIR_BOOL, TY_INT, rb);
                        h.native_ops++;
                    }
                } else {
                    return -1;
                }
            }
            if (cmp < 0) return -1;
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block,
                                cur_hir_block, n, nullptr, multi_block) < 0)
                return -1;
            pc++;
            break;
        }

        case OP_LUA_TESTSET: {
            int rb = lua_reg[insn.B()];
            if (rb < 0) return -1;
            if (lua_is_marshalled_str(h, rb)) return -1;
            int cmp;
            if (lua_is_handle(h, rb) && h.kind[rb] == HIR_LUA_CALL_VAL) {
                cmp = h.emit(HIR_LUA_TOBOOL, TY_INT, rb);
                if (cmp < 0) return -1;
                h.ecalls++;
            } else if (lua_is_handle(h, rb)) {
                return -1;
            } else {
                const lua_truth tr = lua_truth_of(h, truth_tag, rb);
                if (tr == LUA_TRUTH_UNKNOWN) {
                    return -1;
                }
                if (tr == LUA_TRUTH_NIL) {
                    cmp = h.emit_iconst(0);
                } else if (tr == LUA_TRUTH_VALUE) {
                    cmp = h.emit_iconst(1);
                } else if (h.ty[rb] == TY_INT) {
                    if (h.kind[rb] == HIR_ICONST) {
                        cmp = h.emit_iconst(h.val[rb] != 0 ? 1 : 0);
                    } else {
                        cmp = h.emit(HIR_BOOL, TY_INT, rb);
                        h.native_ops++;
                    }
                } else {
                    return -1;
                }
            }
            if (cmp < 0) return -1;
            lua_reg[A] = rb;  // Simplified: always copy.
            if (emit_cmp_branch(h, cmp, insn.k(), proto, pc, pc_to_block,
                                cur_hir_block, n, nullptr, multi_block) < 0)
                return -1;
            pc++;
            break;
        }

        // ---- Control flow ----

        case OP_LUA_JMP: {
            int target = pc + 1 + insn.sJ();
            if (!multi_block) return -1;
            int target_blk = (target >= 0 && target < n) ? pc_to_block[target] : -1;
            if (target_blk < 0) return -1;

            // Back edge (while/repeat): the budget guard, then the jump.
            // The shape this replaces folded exhaustion into an exit to
            // the fall-through -- leaving the loop early and CONTINUING
            // with a wrong partial result, the exact defect the FORLOOP
            // budget had (#1732).  The guard aborts to the interpreter
            // instead.
            if (target <= pc) {
                if (!emit_backedge_guard(pc - target + 1)) return -1;
            }

            h.emit(HIR_BR, TY_VOID, -1, -1, target_blk);
            h.add_edge(cur_hir_block, target_blk);
            break;
        }

        // ---- Numeric for loop ----
        //
        // Lua 5.4 for-loop registers:
        //   R(A)   = internal counter (never materialized here)
        //   R(A+1) = limit
        //   R(A+2) = step
        //   R(A+3) = exposed index (visible in body)
        //
        // 5.4 semantics, not 5.3's: FORPREP sets R(A+3)=init and FALLS
        // INTO the body (jumping forward past FORLOOP only when the trip
        // count is zero); FORLOOP steps the index and jumps BACK on
        // continue.  The 5.3-shaped lowering this replaces (init-step
        // pre-subtraction, sBx offsets) was written against the wrong VM
        // and had never executed behind the #1326 reject.
        //
        // STATIC BOUNDS ONLY in this first cut: init, limit and step must
        // all be integer constants, so the trip direction, the zero-trip
        // decision, and freedom from wraparound are compile-time facts --
        // 5.4's own counter model exists precisely because a naive
        // idx<=limit test misbehaves at the integer edge, and declining
        // the edge is cheaper than reproducing the counter.  The index
        // rides QREG_LUA_IDX so hir_ssa_construct() gives it a real PHI.

        case OP_LUA_FORPREP: {
            if (!multi_block) return -1;
            if (!lua_reg_in_range(A + 3)) return -1;
            int init = lua_reg[A];
            int limit = lua_reg[A + 1];
            int step = lua_reg[A + 2];
            if (init < 0 || limit < 0 || step < 0) return -1;
            // The STEP must be a constant either way: it fixes the trip
            // direction, and with it which comparison FORLOOP emits.
            // step 0 is a runtime error; the interpreter raises it.
            if (h.kind[step] != HIR_ICONST) return -1;
            const int64_t vs = h.val[step];
            if (0 == vs) return -1;
            // Stay far from the int64 edge so idx+step cannot wrap.
            const int64_t kEdge = INT64_C(1) << 62;
            if (vs > kEdge || vs < -kEdge) return -1;

            int body_target = pc + 1;
            int skip_target = pc + 1 + insn.Bx() + 1;
            int body_blk = (body_target < n) ? pc_to_block[body_target] : -1;
            int skip_blk = (skip_target < n) ? pc_to_block[skip_target] : -1;
            if (body_blk < 0 || skip_blk < 0) return -1;
            if (A + 3 >= 10) return -1;

            if (h.kind[init] == HIR_ICONST
                && h.kind[limit] == HIR_ICONST) {
                // STATIC bounds: trip direction, zero-trip, and freedom
                // from wraparound are compile-time facts, and the entry
                // block stays whole.
                const int64_t vi = h.val[init];
                const int64_t vl = h.val[limit];
                if (vi > kEdge || vi < -kEdge || vl > kEdge
                    || vl < -kEdge) {
                    return -1;
                }
                // The body's first pass reads the index before any
                // FORLOOP runs, so it must be stored on the entry side.
                h.emit(HIR_STORE_Q, TY_VOID, init, -1, QREG_LUA_IDX);
                h.emit(HIR_STORE_Q, TY_VOID, init, -1, A + 3);
                const bool zero_trip = (vs > 0) ? (vi > vl) : (vi < vl);
                int target_blk = zero_trip ? skip_blk : body_blk;
                h.emit(HIR_BR, TY_VOID, -1, -1, target_blk);
                h.add_edge(cur_hir_block, target_blk);
                break;
            }

            // RUNTIME bounds (#1732): `for i=1,n`.  The zero-trip test
            // and the wraparound guard become branches.  A value outside
            // the int64 safety margin bails to the limited block -- a
            // decline, not an error: the interpreter re-runs the chunk
            // with its own counter model, and nothing has executed yet,
            // so the bail is rerun-safe by position alone.
            if (h.ty[init] != TY_INT || lua_is_handle(h, init)) return -1;
            if (h.ty[limit] != TY_INT || lua_is_handle(h, limit)) return -1;

            // This path SPLITS the entry block, so entry state must be
            // finalized first: seal the backing, and re-snapshot
            // blk_entry_reg so the next transition's drop-compare sees
            // the split blocks as the same logical entry -- its values
            // dominate the body exactly as block 0's do.
            seal_entry_backing();
            memcpy(blk_entry_reg, lua_reg, sizeof(blk_entry_reg));

            int e_hi = h.emit_iconst(kEdge);
            int e_lo = h.emit_iconst(-kEdge);
            if (e_hi < 0 || e_lo < 0) return -1;
            int b1 = h.emit(HIR_LT, TY_INT, init, e_hi);
            int b2 = h.emit(HIR_GT, TY_INT, init, e_lo);
            int b3 = h.emit(HIR_LT, TY_INT, limit, e_hi);
            int b4 = h.emit(HIR_GT, TY_INT, limit, e_lo);
            int b12 = h.emit(HIR_BAND, TY_INT, b1, b2);
            int b34 = h.emit(HIR_BAND, TY_INT, b3, b4);
            int bounds_ok = h.emit(HIR_BAND, TY_INT, b12, b34);
            if (bounds_ok < 0) return -1;

            h.emit(HIR_STORE_Q, TY_VOID, init, -1, QREG_LUA_IDX);
            h.emit(HIR_STORE_Q, TY_VOID, init, -1, A + 3);

            if (!ensure_limited_blk()) return -1;
            int cont_blk = h.new_block();
            if (cont_blk < 0) return -1;
            h.emit(HIR_BRC, TY_VOID, bounds_ok, limited_blk, cont_blk);
            h.add_edge(cur_hir_block, limited_blk);
            h.add_edge(cur_hir_block, cont_blk);
            h.cur_block = cont_blk;
            cur_hir_block = cont_blk;

            // Runtime zero-trip: run the body iff init is on the limit's
            // side of the direction the constant step fixes.
            int cond_run = h.emit((vs > 0) ? HIR_LE : HIR_GE,
                                  TY_INT, init, limit);
            if (cond_run < 0) return -1;
            h.emit(HIR_BRC, TY_VOID, cond_run, skip_blk, body_blk);
            h.add_edge(cur_hir_block, body_blk);
            h.add_edge(cur_hir_block, skip_blk);
            break;
        }

        case OP_LUA_FORLOOP: {
            if (!multi_block) return -1;
            if (!lua_reg_in_range(A + 3)) return -1;
            // Bounds come from entry_final[], the register state frozen
            // at the entry block's exit: lua_reg[] holds LOAD_Q reloads
            // by now, and the static-bounds test below needs to SEE the
            // ICONSTs FORPREP already vetted.
            int step = entry_final[A + 2];
            int limit = entry_final[A + 1];
            if (step < 0 || limit < 0) return -1;

            // Load current index from q-register (becomes PHI after SSA).
            int idx = h.emit(HIR_LOAD_Q, TY_INT, -1, -1, QREG_LUA_IDX);
            // Loop index is always a number, but the reload has no truth
            // tag by construction — mark UNKNOWN so a future if-on-idx
            // cannot invent VALUE after a lost BOOL (#1768).
            if (idx >= 0) {
                lua_truth_set(truth_tag, idx, LUA_TRUTH_UNKNOWN);
            }
            if (idx < 0) return -1;

            // Increment: index = index + step.
            int new_idx = h.emit(HIR_ADD, TY_INT, idx, step);
            if (new_idx < 0) return -1;
            h.native_ops++;

            // Store updated index back to q-register.
            h.emit(HIR_STORE_Q, TY_VOID, new_idx, -1, QREG_LUA_IDX);

            // Expose the new index to subsequent instructions and back it
            // in the visible register's own qreg.  FORLOOP is the only
            // terminator that writes a register, so the store-at-write
            // hook cannot see this; every reader -- the body, the exit
            // block -- is dominated by this latch, and the first pass
            // reads the STORE_Q FORPREP emitted, so the backing the
            // pre-scan declared is stored on every path (#1732).
            lua_reg[A + 3] = new_idx;
            if (A + 3 >= 10) return -1;
            h.emit(HIR_STORE_Q, TY_VOID, new_idx, -1, A + 3);

            // Continue test.  The step is an ICONST -- FORPREP declined
            // anything else -- so the direction is static; FORPREP's edge
            // bound is what keeps new_idx from wrapping first.
            if (h.kind[step] != HIR_ICONST) return -1;
            int cmp = h.emit((h.val[step] > 0) ? HIR_LE : HIR_GE,
                             TY_INT, new_idx, limit);
            if (cmp < 0) return -1;
            h.native_ops++;

            // Back-edge budget (#1732); see emit_backedge_guard.  The
            // body is Bx instructions plus this FORLOOP.
            if (!emit_backedge_guard(insn.Bx() + 1)) return -1;

            // Branch: if true, loop back; else fall through.  5.4 encodes
            // the back edge as an unsigned Bx: pc -= Bx.
            int loop_target = pc + 1 - insn.Bx();
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
        //
        // Lua always appends a trailing OP_RETURN / RETURN0 after an
        // explicit return (and after the last statement of a chunk).
        // That second return is dead once we have already emitted HIR_RET
        // for the real value.  Updating result_val from it overwrote a
        // correct "hello" / 42 with an empty SCONST, so folded Lua JIT
        // programs always produced empty strings (#1309).
        //
        // Emit HIR_RET for every opcode (keeps block structure), but only
        // the first return value becomes h.result.

        case OP_LUA_RETURN0: {
            int rv = h.emit_sconst(rc.pool_str("", 0), "");
            if (rv < 0) return -1;
            h.emit(HIR_RET, TY_VOID, rv);
            if (result_val < 0) {
                result_val = rv;
            }
            break;
        }

        case OP_LUA_RETURN1: {
            int rv = return_as_string(h, rc, lua_reg[A]);
            if (rv < 0) return -1;
            h.emit(HIR_RET, TY_VOID, rv);
            if (result_val < 0) {
                result_val = rv;
            }
            break;
        }

        case OP_LUA_RETURN: {
            int nret = insn.B() - 1;
            if (nret < 0) {
                // B == 0 is "return all values from A up" (in-top).  The
                // one shape that produces it here is the dead trailing
                // return Lua appends after OP_TAILCALL, whose lowering
                // already emitted the real HIR_RET and claimed result_val;
                // give it RETURN0's shape so the block still terminates.
                // Any other multret return stays declined.
                if (0 == pc
                    || OP_LUA_TAILCALL != proto->code[pc - 1].opcode()) {
                    return -1;
                }
                int dead = h.emit_sconst(rc.pool_str("", 0), "");
                if (dead < 0) return -1;
                h.emit(HIR_RET, TY_VOID, dead);
                if (result_val < 0) {
                    result_val = dead;
                }
                break;
            }
            int rv;
            if (nret == 0) {
                rv = h.emit_sconst(rc.pool_str("", 0), "");
                if (rv < 0) return -1;
            } else {
                rv = return_as_string(h, rc, lua_reg[A]);
                if (rv < 0) return -1;
            }
            h.emit(HIR_RET, TY_VOID, rv);
            if (result_val < 0) {
                result_val = rv;
            }
            break;
        }

        // ---- Upvalue access ----
        // We reject nested protos, so the only upvalue is _ENV (index 0).

        case OP_LUA_GETUPVAL: {
            // A = dest, B = upvalue index.
            // For the main chunk, upvalue 0 = _ENV (global table).
            if (insn.B() != 0) return -1;  // Non-_ENV upvalue.
            // Push _ENV onto Lua stack via __lua_getglobal equivalent.
            // Actually, just reject — GETUPVAL on _ENV is rare; scripts
            // use GETTABUP for _ENV[key] access which is already handled.
            // If someone does `local g = _ENV`, they get GETUPVAL.
            std::string name("__lua_getenv");
            int args[1];
            int dummy = h.emit_iconst(0);
            args[0] = dummy;
            lua_reg[A] = h.emit_call(TY_STRING, 0, args, 1, &name);
            if (lua_reg[A] < 0) return -1;
            h.ecalls++;
            break;
        }

        case OP_LUA_SETUPVAL: {
            // A = source register, B = upvalue index.
            // Setting _ENV is unusual and dangerous. Reject.
            return -1;
        }

        // ---- Global access, method calls, and function calls ----

        case OP_LUA_GETTABUP: {
            if (insn.B() != 0) return -1;  // Only _ENV (upvalue 0).
            int kidx = insn.C();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            const lua_bc_constant &k = proto->constants[kidx];
            if (k.type != LUA_BC_TSHRSTR && k.type != LUA_BC_TLNGSTR)
                return -1;

            if (k.sval == "mux") {
                // mux.* bridge pattern — sentinel for GETFIELD+CALL.
                uint64_t addr = rc.pool_str("mux", 3);
                lua_reg[A] = h.emit_sconst(addr, "mux");
                if (lua_reg[A] < 0) return -1;
                h.lua_mux_sentinel[lua_reg[A]] = true;
            } else {
                // General global access: _ENV[key] via ECALL.
                // Pushes table/function onto Lua stack, returns stack
                // index as string.  Lua stack cleanup is handled by
                // TryJIT's save/restore around RunCompiled.
                uint64_t key_addr = rc.pool_str(k.sval.c_str(), k.sval.size());
                int key_val = h.emit_sconst(key_addr, k.sval);
                if (key_val < 0) return -1;
                lua_reg[A] = h.emit(HIR_LUA_GETGLOBAL, TY_LUA_HANDLE,
                                    key_val);
                if (lua_reg[A] < 0) return -1;
                // A global's referent is not knowable here -- `math` is a
                // table, `tostring` is a function, and a game can rebind
                // either -- so claim both capabilities and let each use's
                // runtime check settle it: field reads take references,
                // calls are allowed with the result type the name claims.
                lua_referent g;
                g.fields_are_refs = true;
                g.callable = true;
                g.returns = lua_call_claim(k.sval);
                if (k.sval == "math") {
                    g.values = k_lua_math_values;
                }
                lua_ref[lua_reg[A]] = g;
                h.known_int[lua_reg[A]] = true;
                h.ecalls++;
            }
            break;
        }

        // SETTABUP: set _ENV[key] = value.
        case OP_LUA_SETTABUP: {
            if (insn.A() != 0) return -1;  // Only _ENV.
            int kidx = insn.B();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            const lua_bc_constant &k = proto->constants[kidx];
            if (k.type != LUA_BC_TSHRSTR && k.type != LUA_BC_TLNGSTR)
                return -1;
            int val = lua_reg[insn.C()];
            if (val < 0) return -1;
            if (h.ty[val] == TY_INT) {
                val = h.emit(HIR_ITOA, TY_STRING, val);
                if (val < 0) return -1;
            } else if (h.ty[val] == TY_FLOAT) {
                val = h.emit(HIR_FTOA, TY_STRING, val);
                if (val < 0) return -1;
            }
            uint64_t key_addr = rc.pool_str(k.sval.c_str(), k.sval.size());
            int key_val = h.emit_sconst(key_addr, k.sval);
            if (key_val < 0) return -1;
            // Global writes have no handler; ineligible at lowering
            // (#1751 rule 1) rather than a guaranteed post-entry fail.
            return -1;
        }

        // SELF: A = dest, B = table register, C = method key constant.
        // R(A+1) := R(B); R(A) := R(B)[K(C)]
        case OP_LUA_SELF: {
            if (!lua_reg_in_range(A + 1)) return -1;
            int tbl = lua_reg[insn.B()];
            if (tbl < 0) return -1;
            // Copy table to R(A+1) for method call.
            lua_reg[A + 1] = tbl;
            // Load method: t[key].
            int kidx = insn.C();
            if (kidx < 0 || kidx >= static_cast<int>(proto->constants.size()))
                return -1;
            const lua_bc_constant &k = proto->constants[kidx];
            if (k.type != LUA_BC_TSHRSTR && k.type != LUA_BC_TLNGSTR)
                return -1;
            uint64_t key_addr = rc.pool_str(k.sval.c_str(), k.sval.size());
            int key_val = h.emit_sconst(key_addr, k.sval);
            if (key_val < 0) return -1;
            // Method dispatch has no handler; ineligible at lowering
            // (#1751 rule 1).
            return -1;
        }

        case OP_LUA_TAILCALL:
            // `return f(...)`: the call below, then the return the helper
            // emits at each successful exit.  The real tail-call mechanism
            // reuses the caller's frame; nothing here does -- the callee
            // runs via an ECALL doing its own pcall -- and the chunk-level
            // pcall asks for one result either way, so a plain call
            // observes the same thing.  k set means upvalues to close,
            // which the CLOSURE reject should make impossible; decline
            // rather than assume.  C is frame correction for the frame
            // reuse that is not happening.
            if (insn.k()) return -1;
            // fall through
        case OP_LUA_CALL: {
            int func_reg = lua_reg[A];
            if (func_reg < 0) return -1;

            int nargs = insn.B() - 1;
            // TAILCALL has no C-encoded result count: it returns what the
            // callee returns, of which the chunk boundary keeps one.
            int nresults = (OP_LUA_TAILCALL == op) ? 1 : insn.C() - 1;
            if (nargs < 0) return -1;  // Variable args not supported.

            // Direct call on a handle with a callable claim.  CALL_INT and
            // CALL_STR share one argument encoding (nargs, argkind bits,
            // arg registers) and differ only in how the result comes back:
            // in a register, or marshalled into an output slot whose SIZE
            // the ECALL is told rather than assumes (#1679).  Which one to
            // emit is the handle's claimed result type, recorded where the
            // handle was created -- one claim, so the two variants cannot
            // disagree about a name the way the twin gated branches this
            // replaces could (d5e5e86e0).
            //
            // Arguments may be integers, CONSTANT strings, floats --
            // constant or runtime -- or Lua HANDLES, with TWO kind bits
            // per argument telling codegen and the handler what each
            // register carries (0 integer, 1 string address, 2 double as
            // raw bits over the FMV.X.D lane, 3 stack reference).  Floats
            // travel honestly rather than as rendered text because
            // coercion would lie to a type-sensitive callee:
            // math.type("3.0") is nil, not "float".  A handle argument is
            // the index for a lua_pushvalue -- the one use of a handle
            // that is ABOUT the thing it points at (#1579), which is what
            // table.insert(t,4) needs.  A runtime string argument would
            // need its own guest buffer and is left for when something
            // needs it.
            //
            // nresults == 0 is a call FOR the effect -- table.insert --
            // and takes CALL_VOID: no result register, no result-type
            // claim to check, and the destination Lua registers become
            // dead exactly as the VM's would.
            const lua_referent fref = lua_referent_of(lua_ref, func_reg);
            // A FLOAT-returning claim has no marshalling; ineligible at
            // lowering, interpreter answers with the right subtype.
            if (fref.callable && TY_FLOAT == fref.returns) {
                return -1;
            }
            // Effectful callees (mux.pemit/set/eval/…) are eligible: the
            // compiled path runs the same bridge C functions as the
            // interpreter.  Pre-Phase-4 they were ineligible because a
            // later runtime decline re-ran the chunk and doubled effects.
            // The string form keeps its historical one-argument floor; the
            // integer form and the effect-only form allow zero.
            const int min_args =
                (0 == nresults || TY_INT == fref.returns) ? 0 : 1;
            if (fref.callable
                && (0 == nresults || 1 == nresults)
                && nargs >= min_args && nargs <= 3) {
                int cargs[3] = { -1, -1, -1 };
                int kinds = 0;
                bool ok = true;
                for (int i = 0; i < nargs && ok; i++) {
                    if (!lua_reg_in_range(A + 1 + i)) { ok = false; break; }
                    int areg = lua_reg[A + 1 + i];
                    if (areg < 0) { ok = false; break; }
                    // nil as an argument would arrive as "" -- tostring(nil)
                    // is "nil", not "".  No kind encodes nil, so decline.
                    if (lua_is_nil(h, truth_tag, areg)) { ok = false; break; }
                    // A sentinel would travel as the string "mux.args";
                    // type() answered "string", tostring() the name.
                    if (lua_is_mux_sentinel(h, areg)) { ok = false; break; }
                    if (lua_is_handle(h, areg)) {
                        kinds |= (3 << (2 * i));   // stack reference
                        // The callee may setmetatable the table: the
                        // plain proof does not survive an escape.
                        lua_ref[areg].plain_proven = false;
                    } else if (h.ty[areg] == TY_INT) {
                        // integer: kind 0
                    } else if (h.kind[areg] == HIR_SCONST) {
                        kinds |= (1 << (2 * i));   // string address
                    } else if (h.ty[areg] == TY_FLOAT) {
                        kinds |= (2 << (2 * i));   // double, raw bits
                    } else {
                        ok = false; break;
                    }
                    cargs[i] = areg;
                }
                if (ok) {
                    // Arguments ride the carg[] list; val[] carries only
                    // the kind bits.
                    if (0 == nresults) {
                        if (h.emit_lua_call(HIR_LUA_CALL_VOID, TY_VOID,
                                func_reg, cargs, nargs, kinds) < 0) {
                            return -1;
                        }
                        lua_reg[A] = -1;
                    } else if (TY_INT == fref.returns) {
                        lua_reg[A] = h.emit_lua_call(HIR_LUA_CALL_INT,
                            TY_INT, func_reg, cargs, nargs, kinds);
                        if (lua_reg[A] < 0) return -1;
                        h.known_int[lua_reg[A]] = true;
                    } else {
                        // Default claim: keep the Lua value on the VM stack
                        // as a handle.  Marshal only at chunk return
                        // (HIR_LUA_MARSHAL).  Consumers use Lua semantics
                        // (TOBOOL, pushvalue args) instead of softcode text.
                        lua_reg[A] = h.emit_lua_call(HIR_LUA_CALL_VAL,
                            TY_LUA_HANDLE, func_reg, cargs, nargs, kinds);
                        if (lua_reg[A] < 0) return -1;
                    }
                    h.ecalls++;
                    for (int i = A + 1; i < A + 1 + nargs; i++) {
                        if (lua_reg_in_range(i)) lua_reg[i] = -1;
                    }
                    if (OP_LUA_TAILCALL == op
                        && lua_tailcall_ret(h, rc, lua_reg[A],
                                            result_val) < 0) {
                        return -1;
                    }
                    break;
                }
            }

            // A call the typed CALL_INT/STR/VOID path cannot encode --
            // arity above three, argument shapes outside the kind
            // encoding, an unclaimed callee -- has no honest compiled
            // form.  The original Phase 2 draft resurrected the named
            // __lua_call here with the handle ITOA'd through guest
            // memory and every argument STRINGIFIED; review measured a
            // table argument arriving in string.format as its stack
            // index rendered in decimal (#1424's exact resurrection) and
            // the plan itself names string lies a non-goal.  Until a
            // TYPED general-call encoding exists, these shapes are
            // ineligible at lowering (#1751 rule 1): the interpreter
            // answers, silently and correctly.
            return -1;
            if (OP_LUA_TAILCALL == op
                && lua_tailcall_ret(h, rc, lua_reg[A], result_val) < 0) {
                return -1;
            }
            break;
        }

        // ---- Generic for-loop (TFOR) ----
        //
        // R(A) = iterator function (Lua stack ref)
        // R(A+1) = invariant state (Lua stack ref)
        // R(A+2) = control variable
        // R(A+3) = to-be-closed (not used without TBC)
        // R(A+4)... = iterator results (key, value, ...)

        case OP_LUA_TFORPREP: {
            // Jump forward to TFORLOOP for initial nil check.
            if (!multi_block) return -1;
            int target = pc + 1 + insn.sBx();
            int target_blk = (target >= 0 && target < n) ? pc_to_block[target] : -1;
            if (target_blk < 0) return -1;
            h.emit(HIR_BR, TY_VOID, -1, -1, target_blk);
            h.add_edge(cur_hir_block, target_blk);
            break;
        }

        case OP_LUA_TFORCALL: {
            // Call iterator: R(A+4),...,R(A+3+C) = R(A)(R(A+1), R(A+2))
            if (!lua_reg_in_range(A + 4)) return -1;
            int iter_func = lua_reg[A];
            int iter_state = lua_reg[A + 1];
            int iter_control = lua_reg[A + 2];
            if (iter_func < 0 || iter_state < 0) return -1;

            // Control variable might be nil (empty string) on first call.
            if (iter_control < 0) {
                iter_control = h.emit_sconst(rc.pool_str("", 0), "");
                if (iter_control < 0) return -1;
            }

            // Convert control to string if needed.
            if (h.ty[iter_control] == TY_INT) {
                iter_control = h.emit(HIR_ITOA, TY_STRING, iter_control);
                if (iter_control < 0) return -1;
            }

            // TFOR is rejected at eligibility; if that ever lifts, the
            // iterator call needs a real typed encoding, not the dead
            // named bridge.  Ineligible (#1751 rule 1).
            return -1;
        }

        case OP_LUA_TFORLOOP: {
            // if R(A+4) ~= nil then R(A+2) = R(A+4); jump back
            if (!multi_block) return -1;
            if (!lua_reg_in_range(A + 4)) return -1;
            int first_result = lua_reg[A + 4];
            if (first_result < 0) return -1;

            // Check if first result is empty (nil in string form).
            // Use STRLEN-like check: if the string is empty, done.
            int fidx = engine_api_lookup("STRLEN");
            int len_val;
            if (fidx > 0) {
                int args[] = { first_result };
                len_val = h.emit_call(TY_STRING, fidx, args, 1);
                if (len_val < 0) return -1;
                h.known_int[len_val] = true;
            } else {
                return -1;
            }

            // ATOI the length, check if > 0.
            int len_int = h.emit(HIR_ATOI, TY_INT, len_val);
            if (len_int < 0) return -1;
            int zero = h.emit_iconst(0);
            int cmp = h.emit(HIR_GT, TY_INT, len_int, zero);
            if (cmp < 0) return -1;

            // Back-edge budget check.  (TFOR protos are rejected at
            // eligibility; this fold-into-the-condition shape is the one
            // #1732 replaced elsewhere and must be reworked like FORLOOP
            // if TFOR is ever lifted.)
            cmp = emit_budget_check(h, cmp, 1);

            // If non-nil: set control = first_result, loop back.
            int loop_target = pc + 1 + insn.sBx();
            int exit_target = pc + 1;
            int loop_blk = (loop_target >= 0 && loop_target < n) ? pc_to_block[loop_target] : -1;
            int exit_blk = (exit_target >= 0 && exit_target < n) ? pc_to_block[exit_target] : -1;
            if (loop_blk < 0 || exit_blk < 0) return -1;

            // Update control variable.
            lua_reg[A + 2] = first_result;

            h.emit(HIR_BRC, TY_VOID, cmp, exit_blk, loop_blk);
            h.add_edge(cur_hir_block, loop_blk);
            h.add_edge(cur_hir_block, exit_blk);
            break;
        }

        // ---- No-op instructions ----
        case OP_LUA_CLOSE:     // No open upvalues without closures.
        case OP_LUA_VARARGPREP:
        case OP_LUA_EXTRAARG:
        case OP_LUA_MMBIN:
        case OP_LUA_MMBINI:
        case OP_LUA_MMBINK:
            break;

        // ---- Unsupported opcodes ----
        default:
            return -1;
        }

        // Store-at-write (#1732): mirror this instruction's register
        // writes into the q-registers, so the value crosses the next
        // block boundary as PHI-convertible q-reg traffic.  Terminator
        // opcodes are skipped -- their block is already closed, and the
        // only terminator that writes a register, FORLOOP, stores its own
        // writes inside its case.  Comparisons that FUSE (lua_bool_fuse)
        // also skip; their write stays plain and declines on a later
        // cross-block read rather than answering wrongly.
        if (proto_has_loop) {
            bool is_terminator;
            switch (op) {
            case OP_LUA_JMP: case OP_LUA_FORPREP: case OP_LUA_FORLOOP:
            case OP_LUA_RETURN: case OP_LUA_RETURN0: case OP_LUA_RETURN1:
            case OP_LUA_EQ: case OP_LUA_EQK: case OP_LUA_EQI:
            case OP_LUA_LT: case OP_LUA_LE: case OP_LUA_LTI:
            case OP_LUA_LEI: case OP_LUA_GTI: case OP_LUA_GEI:
            case OP_LUA_TEST:
                is_terminator = true;
                break;
            default:
                is_terminator = false;
                break;
            }
            if (!is_terminator) {
                for (int r = 0; r < 10 && r < MAX_LUA_REGS; r++) {
                    if (lua_reg[r] == pre_reg[r] || lua_reg[r] < 0) {
                        continue;
                    }
                    const bool is_int =
                        (h.ty[lua_reg[r]] == TY_INT)
                        && !lua_is_handle(h, lua_reg[r]);
                    if (0 == cur_hir_block && !entry_backing_sealed) {
                        if (is_int) {
                            h.emit(HIR_STORE_Q, TY_VOID, lua_reg[r], -1, r);
                            qreg_backed[r] = true;
                        } else if (forloop_backed[r]) {
                            // The loop variable's register holding a
                            // non-int before the loop would leave the
                            // reload machinery a stale value; decline.
                            return -1;
                        }
                    } else if (qreg_backed[r] || forloop_backed[r]) {
                        // A backed register going non-int would leave a
                        // stale integer for the next reload to resurrect.
                        if (!is_int) return -1;
                        h.emit(HIR_STORE_Q, TY_VOID, lua_reg[r], -1, r);
                    }
                }
            }
        }
    }

    if (result_val < 0) return -1;

    // Every block a branch can reach must have been lowered into.  A
    // reachable-but-empty block means some opcode's control flow was modelled
    // as a linear step and swallowed a block leader whole: the skipped path's
    // body ends up in the wrong block, and the block the branch actually
    // targets is a hole.  That was #1421 (OP_LFALSESKIP), and the same shape
    // is available to any future opcode that advances pc by hand.  Catching
    // it here makes the whole class decline instead of silently answering
    // with the other arm's value (#1501).
    {
        bool has_insn[HIR_MAX_BLOCKS];
        memset(has_insn, 0, sizeof(has_insn));
        for (int i = 0; i < h.n_insns; i++) {
            int b = h.blk[i];
            if (b >= 0 && b < HIR_MAX_BLOCKS) has_insn[b] = true;
        }
        for (int b = 0; b < h.n_blocks && b < HIR_MAX_BLOCKS; b++) {
            if (!has_insn[b]) continue;    // b unreachable itself; harmless
            for (int s = 0; s < h.block_nsucc[b]; s++) {
                int t = h.block_succ[b][s];
                if (t >= 0 && t < h.n_blocks && !has_insn[t]) return -1;
            }
        }
    }

    h.result = result_val;
    // ecalls/native_ops force a runtime path.  Also keep needs_jit if
    // lowering already set it (mux.args → CARGS srefs have no ecall/native
    // count but must not take the folded path with empty sval) (#1309).
    //
    if (h.ecalls > 0 || h.native_ops > 0) {
        h.needs_jit = true;
    } else if (!h.sref_addrs.empty()) {
        h.needs_jit = true;
    }

    return result_val;
}
