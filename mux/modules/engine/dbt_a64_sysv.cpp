/*! \file dbt_a64_sysv.cpp
 * \brief RV64IMD dynamic binary translator — AArch64 AAPCS64 backend.
 *
 * Block-at-a-time JIT translation from RV64IMD to AArch64.
 * This file implements the platform-specific parts of the DBT:
 *   - Register cache (host register assignment)
 *   - Trampoline (callee-saved setup, block dispatch)
 *   - Instruction translation (RV64 → AArch64)
 *   - Intrinsic stubs (native host function calls)
 *   - Block chaining (AArch64 B imm26 backpatching)
 *
 * AArch64 advantages over x86-64:
 *   - Hardware SDIV/UDIV (single instruction vs CQO+IDIV)
 *   - SMULH/UMULH for high-half multiply
 *   - CBZ/CBNZ for fused compare-branch-zero
 *   - CSEL/CSET for branchless conditionals
 *   - 8 argument registers (vs 6 on SysV x86-64)
 *   - SXTW for 32→64 sign extension (replaces MOVSXD)
 *
 * See docs/DBT-PORTABILITY.md for the multi-platform design.
 */

#include "dbt.h"
#include "dbt_decoder.h"
#include "dbt_emit_a64.h"
#include "dbt_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------
// AArch64 AAPCS64 host register assignments
// ---------------------------------------------------------------
//
// Pinned (callee-saved, survive across host CALLs):
//   X19 = pointer to rv64_ctx_t (guest state)
//   X20 = pointer to guest memory base
//   X21 = pointer to block cache base
//
// Register cache (8 slots):
//   Slots 0-3: X9, X10, X11, X12  (caller-saved / corruptible)
//   Slots 4-7: X22, X23, X24, X25 (callee-saved, survive CALLs)
//
// Pinned guest registers (a0-a3) go in callee-saved slots 4-7
// so they survive across intrinsic CALL stubs.
//
// Scratch: X0, X1, X2 (also AAPCS64 arg/result registers)
// FP scratch: D0, D1
// FP cache: D16-D21 (6 slots, caller-saved)
//
// AAPCS64 argument regs: X0-X7 (8 integer args)
//

static const int rc_host_regs[RC_NUM_SLOTS] = {
    A64_X9, A64_X10, A64_X11, A64_X12,
    A64_X22, A64_X23, A64_X24, A64_X25
};

// Pinned guest registers: a0-a3 (x10-x13) in slots 4-7 (callee-saved).
//
static const int rc_pinned_guest[RC_NUM_PINNED] = { 10, 11, 12, 13 };

// FP register cache — 6-slot LRU for guest FP registers in D16-D21.
// D0 and D1 are scratch (like X0 for integers).
//
static const int fc_host_xmm[FC_NUM_SLOTS] = {
    16, 17, 18, 19, 20, 21  // D16-D21
};

// AAPCS64 argument registers.
static constexpr int a64_arg_regs[8] = {
    A64_X0, A64_X1, A64_X2, A64_X3,
    A64_X4, A64_X5, A64_X6, A64_X7
};

// ---------------------------------------------------------------
// Register cache and FP cache implementations
// ---------------------------------------------------------------

static void fc_init(fp_cache_t *fc) {
    for (int i = 0; i < FC_NUM_SLOTS; i++) {
        fc->slots[i].guest_freg = -1;
        fc->slots[i].dirty = 0;
        fc->slots[i].last_use = 0;
    }
    fc->clock = 0;
}

static int fc_find(fp_cache_t *fc, int guest_freg) {
    for (int i = 0; i < FC_NUM_SLOTS; i++)
        if (fc->slots[i].guest_freg == guest_freg) return i;
    return -1;
}

static int fc_alloc(emit_t *e, fp_cache_t *fc) {
    for (int i = 0; i < FC_NUM_SLOTS; i++)
        if (fc->slots[i].guest_freg == -1) return i;
    int lru = 0;
    for (int i = 1; i < FC_NUM_SLOTS; i++)
        if (fc->slots[i].last_use < fc->slots[lru].last_use) lru = i;
    if (fc->slots[lru].dirty)
        emit_store_fp_d(e, fc->slots[lru].guest_freg, fc_host_xmm[lru]);
    fc->slots[lru].guest_freg = -1;
    fc->slots[lru].dirty = 0;
    return lru;
}

static int fc_read(emit_t *e, fp_cache_t *fc, int guest_freg) {
    int slot = fc_find(fc, guest_freg);
    if (slot >= 0) {
        fc->slots[slot].last_use = ++fc->clock;
        return fc_host_xmm[slot];
    }
    slot = fc_alloc(e, fc);
    emit_load_fp_d(e, fc_host_xmm[slot], guest_freg);
    fc->slots[slot].guest_freg = guest_freg;
    fc->slots[slot].dirty = 0;
    fc->slots[slot].last_use = ++fc->clock;
    return fc_host_xmm[slot];
}

static int fc_write(emit_t *e, fp_cache_t *fc, int guest_freg) {
    int slot = fc_find(fc, guest_freg);
    if (slot < 0) slot = fc_alloc(e, fc);
    fc->slots[slot].guest_freg = guest_freg;
    fc->slots[slot].dirty = 1;
    fc->slots[slot].last_use = ++fc->clock;
    return fc_host_xmm[slot];
}

static void fc_flush(emit_t *e, fp_cache_t *fc) {
    for (int i = 0; i < FC_NUM_SLOTS; i++)
        if (fc->slots[i].guest_freg >= 0 && fc->slots[i].dirty)
            emit_store_fp_d(e, fc->slots[i].guest_freg, fc_host_xmm[i]);
}

static void rc_init(reg_cache_t *rc) {
    for (int i = 0; i < RC_NUM_SLOTS; i++) {
        rc->slots[i].guest_reg = -1;
        rc->slots[i].dirty = 0;
        rc->slots[i].last_use = 0;
        rc->slots[i].pinned = 0;
    }
    rc->clock = 0;
}

// Pinned registers are in slots 4-7 (callee-saved X22-X25).
static void rc_init_pinned(reg_cache_t *rc) {
    rc_init(rc);
    for (int i = 0; i < RC_NUM_PINNED; i++) {
        rc->slots[4 + i].guest_reg = rc_pinned_guest[i];
        rc->slots[4 + i].dirty = 0;
        rc->slots[4 + i].last_use = 0;
        rc->slots[4 + i].pinned = 1;
    }
}

static int rc_find(reg_cache_t *rc, int guest_reg) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg == guest_reg) return i;
    return -1;
}

static int rc_alloc(reg_cache_t *rc, emit_t *e) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg == -1 && !rc->slots[i].pinned)
            return i;
    int lru = -1;
    for (int i = 0; i < RC_NUM_SLOTS; i++) {
        if (rc->slots[i].pinned) continue;
        if (lru < 0 || rc->slots[i].last_use < rc->slots[lru].last_use)
            lru = i;
    }
    if (lru < 0) lru = 0;
    if (rc->slots[lru].dirty)
        emit_store_guest(e, rc->slots[lru].guest_reg, rc_host_regs[lru]);
    rc->slots[lru].guest_reg = -1;
    rc->slots[lru].dirty = 0;
    return lru;
}

static int rc_read(emit_t *e, reg_cache_t *rc, int guest_reg) {
    if (guest_reg == 0) {
        // x0 is always zero — MOV X0, XZR.
        emit_mov_r64(e, A64_X0, A64_XZR);
        return A64_X0;
    }
    int slot = rc_find(rc, guest_reg);
    if (slot >= 0) {
        rc->slots[slot].last_use = ++rc->clock;
        return rc_host_regs[slot];
    }
    slot = rc_alloc(rc, e);
    emit_load_guest(e, rc_host_regs[slot], guest_reg);
    rc->slots[slot].guest_reg = guest_reg;
    rc->slots[slot].dirty = 0;
    rc->slots[slot].last_use = ++rc->clock;
    return rc_host_regs[slot];
}

static int rc_write(emit_t *e, reg_cache_t *rc, int guest_reg) {
    if (guest_reg == 0) return A64_X0;
    int slot = rc_find(rc, guest_reg);
    if (slot < 0) slot = rc_alloc(rc, e);
    rc->slots[slot].guest_reg = guest_reg;
    rc->slots[slot].dirty = 1;
    rc->slots[slot].last_use = ++rc->clock;
    return rc_host_regs[slot];
}

static void rc_load(emit_t *e, reg_cache_t *rc, int host_dst, int guest_reg) {
    int hr = rc_read(e, rc, guest_reg);
    emit_mov_r64(e, host_dst, hr);
}

static void rc_store(emit_t *e, reg_cache_t *rc, int guest_reg, int host_src) {
    if (guest_reg == 0) return;
    int hr = rc_write(e, rc, guest_reg);
    emit_mov_r64(e, hr, host_src);
}

static void rc_flush(emit_t *e, reg_cache_t *rc) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg >= 0 && rc->slots[i].dirty)
            emit_store_guest(e, rc->slots[i].guest_reg, rc_host_regs[i]);
}

static void rc_invalidate_reload(emit_t *e, reg_cache_t *rc) {
    for (int i = 0; i < RC_NUM_SLOTS; i++) {
        rc->slots[i].guest_reg = -1;
        rc->slots[i].dirty = 0;
        rc->slots[i].last_use = 0;
        rc->slots[i].pinned = 0;
    }
    rc->clock = 0;
    for (int i = 0; i < RC_NUM_PINNED; i++) {
        rc->slots[4 + i].guest_reg = rc_pinned_guest[i];
        rc->slots[4 + i].dirty = 0;
        rc->slots[4 + i].last_use = 0;
        rc->slots[4 + i].pinned = 1;
        emit_load_guest(e, rc_host_regs[4 + i], rc_pinned_guest[i]);
    }
}

// Forward declaration.
static void emit_exit_chained(emit_t *e, dbt_state_t *dbt, uint64_t target_pc);

// ---------------------------------------------------------------
// Intrinsic stubs (AAPCS64)
// ---------------------------------------------------------------

static void emit_load_ctx_reg(emit_t *e, int host_reg, int guest_reg) {
    emit_load_guest(e, host_reg, guest_reg);
}

static void emit_store_ctx_reg(emit_t *e, int guest_reg, int host_reg) {
    emit_store_guest(e, guest_reg, host_reg);
}

static void emit_guest_to_host(emit_t *e, int host_reg) {
    emit_add_r64(e, host_reg, host_reg, A64_X20);
}

static void emit_intrinsic_return(emit_t *e) {
    for (int i = 0; i < RC_NUM_PINNED; i++) {
        emit_load_guest(e, rc_host_regs[4 + i], rc_pinned_guest[i]);
    }
    emit_load_ctx_reg(e, A64_X0, 1);  // ra
    emit_exit_indirect(e, A64_X0);
}

// Prologue: STP X29, X30, [SP, #-16]!; MOV X29, SP
static void emit_stub_prologue(emit_t *e) {
    emit_stp_pre(e, A64_X29, A64_X30, A64_SP, -16);
    emit_mov_r64(e, A64_X29, A64_SP);
}

static void emit_stub_epilogue(emit_t *e) {
    emit_mov_r64(e, A64_SP, A64_X29);
    emit_ldp_post(e, A64_X29, A64_X30, A64_SP, 16);
}

static void emit_call_host(emit_t *e, void *fn) {
    emit_mov_r64_imm64(e, A64_X8, reinterpret_cast<uint64_t>(fn));
    emit_blr(e, A64_X8);
}

// ---- Individual intrinsic stubs ----

static void emit_stub_slen(emit_t *e) {
    emit_stub_prologue(e);
    emit_load_ctx_reg(e, A64_X0, 10);
    emit_guest_to_host(e, A64_X0);
    emit_call_host(e, reinterpret_cast<void *>(strlen));
    emit_store_ctx_reg(e, 10, A64_X0);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

static void emit_stub_scopy(emit_t *e) {
    emit_stub_prologue(e);
    emit_load_ctx_reg(e, A64_X22, 10);  // save guest dst (callee-saved)

    emit_load_ctx_reg(e, A64_X0, 10);
    emit_guest_to_host(e, A64_X0);
    emit_load_ctx_reg(e, A64_X1, 11);
    emit_guest_to_host(e, A64_X1);
    emit_call_host(e, reinterpret_cast<void *>(strcpy));

    emit_mov_r64(e, A64_X0, A64_X0);  // host_dst returned by strcpy
    emit_call_host(e, reinterpret_cast<void *>(strlen));

    emit_add_r64(e, A64_X0, A64_X0, A64_X22);
    emit_store_ctx_reg(e, 10, A64_X0);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

static void emit_stub_memcpy(emit_t *e) {
    emit_stub_prologue(e);
    emit_load_ctx_reg(e, A64_X22, 10);

    emit_load_ctx_reg(e, A64_X0, 10);
    emit_guest_to_host(e, A64_X0);
    emit_load_ctx_reg(e, A64_X1, 11);
    emit_guest_to_host(e, A64_X1);
    emit_load_ctx_reg(e, A64_X2, 12);
    emit_call_host(e, reinterpret_cast<void *>(memcpy));

    emit_store_ctx_reg(e, 10, A64_X22);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

static void emit_stub_memcmp(emit_t *e) {
    emit_stub_prologue(e);
    emit_load_ctx_reg(e, A64_X0, 10);
    emit_guest_to_host(e, A64_X0);
    emit_load_ctx_reg(e, A64_X1, 11);
    emit_guest_to_host(e, A64_X1);
    emit_load_ctx_reg(e, A64_X2, 12);
    emit_call_host(e, reinterpret_cast<void *>(memcmp));
    emit_sxtw(e, A64_X0, A64_X0);
    emit_store_ctx_reg(e, 10, A64_X0);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

static void emit_stub_memset(emit_t *e) {
    emit_stub_prologue(e);
    emit_load_ctx_reg(e, A64_X22, 10);

    emit_load_ctx_reg(e, A64_X0, 10);
    emit_guest_to_host(e, A64_X0);
    emit_load_ctx_reg(e, A64_X1, 11);
    emit_load_ctx_reg(e, A64_X2, 12);
    emit_call_host(e, reinterpret_cast<void *>(memset));

    emit_store_ctx_reg(e, 10, A64_X22);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

static void emit_stub_memswap(emit_t *e) {
    emit_stub_prologue(e);

    emit_load_ctx_reg(e, A64_X0, 10);
    emit_guest_to_host(e, A64_X0);
    emit_load_ctx_reg(e, A64_X1, 11);
    emit_guest_to_host(e, A64_X1);
    emit_load_ctx_reg(e, A64_X2, 12);

    // CBZ x2, done
    uint32_t cbz_done = emit_cbz_x64(e, A64_X2, 0);

    // .loop: swap one byte at a time
    uint32_t loop_top = emit_pos(e);
    // LDRB W3, [X0]
    emit_inst(e, 0x39400003 | (A64_X0 << 5));
    // LDRB W4, [X1]
    emit_inst(e, 0x39400004 | (A64_X1 << 5));
    // STRB W4, [X0]
    emit_inst(e, 0x39000004 | (A64_X0 << 5));
    // STRB W3, [X1]
    emit_inst(e, 0x39000003 | (A64_X1 << 5));

    emit_add_r64_imm(e, A64_X0, A64_X0, 1);
    emit_add_r64_imm(e, A64_X1, A64_X1, 1);
    emit_sub_r64_imm(e, A64_X2, A64_X2, 1);

    int32_t loop_back = static_cast<int32_t>(loop_top) - static_cast<int32_t>(emit_pos(e));
    emit_cbnz_x64(e, A64_X2, loop_back);

    // .done:
    emit_patch_b19(e, cbz_done, emit_pos(e));

    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// ---- FP intrinsic stubs ----

static constexpr int CTX_FA0_OFF = CTX_FP_OFF + 10 * 8;
static constexpr int CTX_FA1_OFF = CTX_FP_OFF + 11 * 8;

static void emit_load_ctx_fp(emit_t *e, int dreg, int ctx_off) {
    uint32_t scaled = ctx_off / 8;
    emit_inst(e, 0xFD400000 | (scaled << 10) | (A64_X19 << 5) | dreg);
}

static void emit_store_ctx_fp(emit_t *e, int ctx_off, int dreg) {
    uint32_t scaled = ctx_off / 8;
    emit_inst(e, 0xFD000000 | (scaled << 10) | (A64_X19 << 5) | dreg);
}

static void emit_stub_fp_d_d(void *ev, void *fn) {
    emit_t *e = static_cast<emit_t *>(ev);
    emit_stub_prologue(e);
    emit_load_ctx_fp(e, 0, CTX_FA0_OFF);
    emit_call_host(e, fn);
    emit_store_ctx_fp(e, CTX_FA0_OFF, 0);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

static void emit_stub_fp_dd_d(void *ev, void *fn) {
    emit_t *e = static_cast<emit_t *>(ev);
    emit_stub_prologue(e);
    emit_load_ctx_fp(e, 0, CTX_FA0_OFF);
    emit_load_ctx_fp(e, 1, CTX_FA1_OFF);
    emit_call_host(e, fn);
    emit_store_ctx_fp(e, CTX_FA0_OFF, 0);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

static void emit_stub_strtod(void *ev, void *fn) {
    emit_t *e = static_cast<emit_t *>(ev);
    emit_stub_prologue(e);
    emit_load_ctx_reg(e, A64_X0, 10);
    emit_guest_to_host(e, A64_X0);
    emit_call_host(e, fn);
    emit_store_ctx_fp(e, CTX_FA0_OFF, 0);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

static void emit_stub_fval(void *ev, void *fn) {
    emit_t *e = static_cast<emit_t *>(ev);
    emit_stub_prologue(e);
    emit_load_ctx_reg(e, A64_X0, 10);
    emit_guest_to_host(e, A64_X0);
    emit_load_ctx_fp(e, 0, CTX_FA0_OFF);
    emit_call_host(e, fn);
    emit_store_ctx_reg(e, 10, A64_X0);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

static void emit_stub_ftoa_round(void *ev, void *fn) {
    emit_t *e = static_cast<emit_t *>(ev);
    emit_stub_prologue(e);
    emit_load_ctx_reg(e, A64_X0, 10);
    emit_guest_to_host(e, A64_X0);
    emit_load_ctx_fp(e, 0, CTX_FA0_OFF);
    emit_load_ctx_reg(e, A64_X1, 11);
    emit_call_host(e, fn);
    emit_store_ctx_reg(e, 10, A64_X0);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// ---- Generic co_* stub emitter ----

static void emit_stub_co_generic(void *ev, void *fn,
                                  int nargs, uint8_t ptr_mask) {
    emit_t *e = static_cast<emit_t *>(ev);
    emit_stub_prologue(e);

    int n_reg   = (nargs <= 8) ? nargs : 8;
    int n_stack = (nargs > 8) ? (nargs - 8) : 0;

    if (n_stack > 0) {
        int stack_bytes = ((n_stack * 8 + 15) & ~15);
        emit_sub_r64_imm(e, A64_SP, A64_SP, stack_bytes);
        for (int i = nargs - 1; i >= 8; i--) {
            emit_load_ctx_reg(e, A64_X8, 10 + i);
            if (ptr_mask & (1 << i))
                emit_guest_to_host(e, A64_X8);
            emit_str_x64_imm(e, A64_X8, A64_SP, (i - 8) * 8);
        }
    }

    for (int i = n_reg - 1; i >= 0; i--) {
        emit_load_ctx_reg(e, a64_arg_regs[i], 10 + i);
        if (ptr_mask & (1 << i))
            emit_guest_to_host(e, a64_arg_regs[i]);
    }

    emit_call_host(e, fn);
    emit_store_ctx_reg(e, 10, A64_X0);

    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

#define DEFINE_CO_EMITTER(name, nargs, ptr_mask) \
    static void emit_stub_##name(void *e, void *fn) { \
        emit_stub_co_generic(e, fn, nargs, ptr_mask); \
    }

DEFINE_CO_EMITTER(co_2p, 2, 0x01)
DEFINE_CO_EMITTER(co_3pp, 3, 0x03)
DEFINE_CO_EMITTER(co_4pp, 4, 0x03)
DEFINE_CO_EMITTER(co_pos, 4, 0x05)
DEFINE_CO_EMITTER(co_3p, 3, 0x01)
DEFINE_CO_EMITTER(co_5pp, 5, 0x03)
DEFINE_CO_EMITTER(co_member, 5, 0x05)
DEFINE_CO_EMITTER(co_6pp, 6, 0x03)
DEFINE_CO_EMITTER(co_7pp, 7, 0x03)
DEFINE_CO_EMITTER(co_8ppp, 8, 0x0B)

static void emit_stub_slen_w(void *ev, void *) { emit_stub_slen(static_cast<emit_t *>(ev)); }
static void emit_stub_scopy_w(void *ev, void *) { emit_stub_scopy(static_cast<emit_t *>(ev)); }
static void emit_stub_memcpy_w(void *ev, void *) { emit_stub_memcpy(static_cast<emit_t *>(ev)); }
static void emit_stub_memcmp_w(void *ev, void *) { emit_stub_memcmp(static_cast<emit_t *>(ev)); }
static void emit_stub_memset_w(void *ev, void *) { emit_stub_memset(static_cast<emit_t *>(ev)); }
static void emit_stub_memswap_w(void *ev, void *) { emit_stub_memswap(static_cast<emit_t *>(ev)); }

static uint8_t *try_emit_intrinsic(dbt_state_t *dbt, uint64_t guest_pc) {
    for (int i = 0; i < dbt->num_intrinsics; i++) {
        if (dbt->intrinsics[i].guest_addr == guest_pc) {
            uint8_t *block_start = dbt->code_buf + dbt->code_used;
            emit_t e;
            e.buf = block_start;
            e.offset = 0;
            e.capacity = CODE_BUF_SIZE - dbt->code_used;

            dbt->intrinsics[i].emitter(&e, dbt->intrinsics[i].host_fn);
            if (e.offset > e.capacity) return nullptr;

            dbt->code_used += e.offset;
            dbt->intrinsic_hits++;
            dbt_cache_insert(dbt, guest_pc, block_start);
            return block_start;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------
// Intrinsic registration
// ---------------------------------------------------------------

typedef void (*generic_emitter_fn)(void *e, void *fn);

static generic_emitter_fn s_emitter_table[] = {
    emit_stub_slen_w,      // DBT_EMIT_SLEN
    emit_stub_scopy_w,     // DBT_EMIT_SCOPY
    emit_stub_memcpy_w,    // DBT_EMIT_MEMCPY
    emit_stub_memcmp_w,    // DBT_EMIT_MEMCMP
    emit_stub_memset_w,    // DBT_EMIT_MEMSET
    emit_stub_memswap_w,   // DBT_EMIT_MEMSWAP
    emit_stub_co_3p,       // DBT_EMIT_CO_3P
    emit_stub_co_4pp,      // DBT_EMIT_CO_4PP
    emit_stub_co_pos,      // DBT_EMIT_CO_POS
    emit_stub_co_5pp,      // DBT_EMIT_CO_5PP
    emit_stub_co_member,   // DBT_EMIT_CO_MEMBER
    emit_stub_co_6pp,      // DBT_EMIT_CO_6PP
    emit_stub_co_2p,       // DBT_EMIT_CO_2P
    emit_stub_co_3pp,      // DBT_EMIT_CO_3PP
    emit_stub_co_7pp,      // DBT_EMIT_CO_7PP
    emit_stub_co_8ppp,     // DBT_EMIT_CO_8PPP
    emit_stub_fp_d_d,      // DBT_EMIT_FP_D_D
    emit_stub_fp_dd_d,     // DBT_EMIT_FP_DD_D
    emit_stub_strtod,      // DBT_EMIT_STRTOD
    emit_stub_fval,        // DBT_EMIT_FVAL
    emit_stub_ftoa_round,  // DBT_EMIT_FTOA_ROUND
};

void dbt_register_intrinsic(dbt_state_t *dbt, uint64_t guest_addr,
                             dbt_emitter_id emitter_id, void *host_fn) {
    if (dbt->num_intrinsics >= dbt_state_t::MAX_INTRINSICS) return;
    if (!guest_addr) return;
    auto &slot = dbt->intrinsics[dbt->num_intrinsics++];
    slot.guest_addr = guest_addr;
    slot.emitter = s_emitter_table[emitter_id];
    slot.host_fn = host_fn;
}

// ---------------------------------------------------------------
// Block chaining — AArch64 B imm26 backpatching
// ---------------------------------------------------------------

void dbt_backend_backpatch_jmp(uint8_t *code_buf, uint32_t patch_offset,
                                uint8_t *target) {
    int32_t byte_diff = static_cast<int32_t>(target - (code_buf + patch_offset));
    int32_t imm26 = byte_diff >> 2;
    uint32_t inst = 0x14000000 | (imm26 & 0x03FFFFFF);
    memcpy(code_buf + patch_offset, &inst, 4);
}

static void emit_exit_chained(emit_t *e, dbt_state_t *dbt,
                               uint64_t target_pc) {
    block_entry_t *be = dbt_cache_lookup(dbt, target_pc);
    bool known = (be != nullptr);

    uint32_t b_patch = emit_b(e, 0);  // placeholder

    if (known) {
        uint32_t target_off = static_cast<uint32_t>(be->native_code - e->buf);
        emit_patch_b26(e, b_patch, target_off);
    } else {
        uint32_t abs_offset = static_cast<uint32_t>(e->buf - dbt->code_buf) + b_patch;
        uint32_t stub_pos = emit_pos(e);
        emit_patch_b26(e, b_patch, stub_pos);

        uint32_t stub_abs = static_cast<uint32_t>(e->buf - dbt->code_buf) + stub_pos;
        dbt->patches.push_back(patch_site_t{abs_offset, stub_abs, target_pc});
        dbt->chain_misses++;
        emit_exit_with_pc(e, target_pc);
    }
}

// ---------------------------------------------------------------
// Translate a single block — STUB
// ---------------------------------------------------------------
//
// TODO: Full RV64→AArch64 per-instruction translation.
//
// The Lenovo ARM Chromebook's Claude Code instance will implement
// this incrementally, testing each RV64 opcode as it goes.
//
// Intrinsic stubs (Tier 2 blob functions) work now — they're
// handled by try_emit_intrinsic above.

uint8_t *dbt_backend_translate_block(dbt_state_t *dbt, uint64_t guest_pc) {
    uint8_t *intrinsic = try_emit_intrinsic(dbt, guest_pc);
    if (intrinsic) return intrinsic;

    // No general instruction translation yet — return nullptr so the
    // dispatch loop falls back to the interpreter.
    return nullptr;
}

// ---------------------------------------------------------------
// Trampoline — AArch64 AAPCS64
// ---------------------------------------------------------------

void dbt_backend_emit_trampoline(dbt_state_t *dbt) {
    emit_t e;
    e.buf = dbt->code_buf;
    e.offset = 0;
    e.capacity = 512;

    // Save callee-saved registers and LR.
    emit_stp_pre(&e, A64_X29, A64_X30, A64_SP, -16);
    emit_stp_pre(&e, A64_X19, A64_X20, A64_SP, -16);
    emit_stp_pre(&e, A64_X21, A64_X22, A64_SP, -16);
    emit_stp_pre(&e, A64_X23, A64_X24, A64_SP, -16);
    emit_stp_pre(&e, A64_X25, A64_X26, A64_SP, -16);

    // Args: X0=ctx, X1=memory, X2=block, X3=cache
    emit_mov_r64(&e, A64_X19, A64_X0);   // ctx
    emit_mov_r64(&e, A64_X20, A64_X1);   // memory base
    emit_mov_r64(&e, A64_X21, A64_X3);   // cache

    // Pre-load pinned guest registers: a0→X22, a1→X23, a2→X24, a3→X25
    for (int i = 0; i < RC_NUM_PINNED; i++) {
        emit_load_guest(&e, rc_host_regs[4 + i], rc_pinned_guest[i]);
    }

    // Call block (X2 = block code pointer)
    emit_blr(&e, A64_X2);

    // Post-store pinned guest registers.
    for (int i = 0; i < RC_NUM_PINNED; i++) {
        emit_store_guest(&e, rc_pinned_guest[i], rc_host_regs[4 + i]);
    }

    // Restore callee-saved (reverse order).
    emit_ldp_post(&e, A64_X25, A64_X26, A64_SP, 16);
    emit_ldp_post(&e, A64_X23, A64_X24, A64_SP, 16);
    emit_ldp_post(&e, A64_X21, A64_X22, A64_SP, 16);
    emit_ldp_post(&e, A64_X19, A64_X20, A64_SP, 16);
    emit_ldp_post(&e, A64_X29, A64_X30, A64_SP, 16);

    emit_ret(&e);

    dbt->code_used = e.offset;
}
