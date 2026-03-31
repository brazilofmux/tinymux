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

// Inline CALL: emit a native BLR to an already-translated callee.
//
// The callee's translated code ends with RET (BR X30), which returns
// to the instruction after our BLR.  We save X30 (link register) on
// the native stack before BLR and restore it after, so that the caller
// can eventually RET back to the trampoline.
//
// After the callee returns, we check ctx.next_pc against the expected
// return PC.  If it matches (hot path), execution continues inline.
// If not (cold path), a side exit stub falls back to the dispatch loop.
//
static bool try_emit_inline_call(emit_t *e, reg_cache_t *rc, fp_cache_t *fc,
                                 dbt_state_t *dbt,
                                 uint64_t target_pc, block_entry_t *callee,
                                 uint64_t return_pc,
                                 side_exit_t *side_exits,
                                 int *num_side_exits) {
    (void)target_pc;
    if (!callee || *num_side_exits >= MAX_SIDE_EXITS) return false;

    // Flush cached registers — callee reads from ctx.
    rc_flush(e, rc);
    fc_flush(e, fc);

    // Store ra = return_pc in ctx (callee's JALR reads this).
    emit_mov_r64_imm64(e, A64_X0, return_pc);
    emit_store_guest(e, 1, A64_X0);  // x1 = ra

    // Save X30 (link register) on the native stack.
    emit_stp_pre(e, A64_X29, A64_X30, A64_SP, -16);

    // Load callee's native code address and BLR.
    uint8_t *callee_code = callee->native_code;
    emit_mov_r64_imm64(e, A64_X0, reinterpret_cast<uint64_t>(callee_code));
    emit_blr(e, A64_X0);

    // Restore X30 (link register) from the native stack.
    emit_ldp_post(e, A64_X29, A64_X30, A64_SP, 16);

    // Check: did the callee return to the expected PC?
    // If ctx.next_pc != return_pc → cold exit.
    emit_cmp_ctx_imm32(e, CTX_NEXT_PC_OFF, static_cast<int32_t>(return_pc));
    uint32_t bne_cold = emit_b_cond(e, A64_COND_NE, 0);

    // Hot path: callee returned normally.  Invalidate register cache
    // since the callee may have modified any guest register.
    rc_invalidate_reload(e, rc);

    side_exits[*num_side_exits].jcc_patch = bne_cold;
    side_exits[*num_side_exits].target_pc = 0;  // sentinel: cold exit
    side_exits[*num_side_exits].expected_next_pc = return_pc;
    (*num_side_exits)++;
    dbt->inline_calls++;
    return true;
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
// Translate a single block — with instruction fusion
// ---------------------------------------------------------------
//
// Straight-line per-instruction translation with register cache.
// Instruction fusion: LUI+ADDI, AUIPC+ADDI, LUI+JALR, AUIPC+JALR,
// LUI/AUIPC+LOAD/STORE, SLT+BEQ/BNE peepholes.
// At any branch/jump, flush register cache and exit.

uint8_t *dbt_backend_translate_block(dbt_state_t *dbt, uint64_t guest_pc) {
    uint8_t *intrinsic = try_emit_intrinsic(dbt, guest_pc);
    if (intrinsic) return intrinsic;

    uint8_t *block_start = dbt->code_buf + dbt->code_used;

    emit_t e;
    e.buf = block_start;
    e.offset = 0;
    e.capacity = CODE_BUF_SIZE - dbt->code_used;

    reg_cache_t rc;
    rc_init_pinned(&rc);

    fp_cache_t fc;
    fc_init(&fc);

    // -- Superblock: self-loop detection --
    //
    // Scan forward from guest_pc looking for a branch/JAL back to
    // guest_pc.  If found, the block contains a self-loop and we
    // can keep the entire loop body in one native block.
    //
    uint32_t warm_entry = 0;
    bool self_loop = false;
    {
        uint64_t scan_pc = guest_pc;
        int used[32] = {0};
        bool past_first_branch = false;
        for (int i = 0; i < MAX_BLOCK_INSNS && scan_pc + 4 <= dbt->memory_size; i++) {
            uint32_t w;
            memcpy(&w, dbt->memory + scan_pc, 4);
            rv64_insn_t si;
            rv64_decode(w, &si);
            if (!past_first_branch) {
                if (si.rs1) used[si.rs1] = 1;
                if ((si.opcode == OP_REG || si.opcode == OP_BRANCH || si.opcode == OP_STORE) && si.rs2)
                    used[si.rs2] = 1;
            }
            if (si.opcode == OP_BRANCH) {
                uint64_t target = scan_pc + static_cast<int64_t>(si.imm);
                if (target == guest_pc) { self_loop = true; break; }
                if (si.imm < 0) break;
                past_first_branch = true;
                scan_pc = target;
                continue;
            }
            if (si.opcode == OP_JAL) {
                if (si.rd != 0) {
                    past_first_branch = true;
                    scan_pc += 4;
                    continue;
                }
                uint64_t target = scan_pc + static_cast<int64_t>(si.imm);
                if (target == guest_pc) { self_loop = true; break; }
                if (si.imm < 0) break;
                if (si.imm > 0 && target + 4 <= dbt->memory_size) {
                    past_first_branch = true;
                    scan_pc = target;
                    continue;
                }
                break;
            }
            if (si.opcode == OP_JALR) {
                if (si.rd == 0 && si.rs1 == 1 && si.imm == 0) {
                    scan_pc += 4;
                    continue;
                }
                break;
            }
            if (si.opcode == OP_SYSTEM) break;
            scan_pc += 4;
        }
        if (self_loop) {
            // Pre-load frequently used registers into the cache.
            int loaded = 0;
            for (int r = 1; r < 32 && loaded < RC_NUM_SLOTS; r++) {
                if (used[r]) { rc_read(&e, &rc, r); loaded++; }
            }

            // Align warm_entry to 16-byte boundary (AArch64 fetch unit).
            // All AArch64 instructions are 4 bytes, so we pad with NOPs.
            uintptr_t abs_cur = reinterpret_cast<uintptr_t>(e.buf) + emit_pos(&e);
            uint32_t pad_needed = ((abs_cur + 15) & ~(uintptr_t)15) - abs_cur;
            uint32_t n_nops = pad_needed / 4;
            for (uint32_t i = 0; i < n_nops; i++) {
                emit_inst(&e, 0xD503201F);  // NOP
            }

            warm_entry = emit_pos(&e);
        }
    }

    side_exit_t side_exits[MAX_SIDE_EXITS];
    int num_side_exits = 0;

    uint64_t pc = guest_pc;
    int count = 0;

    while (count < MAX_BLOCK_INSNS) {
        if (pc + 4 > dbt->memory_size) {
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            emit_exit_chained(&e, dbt, pc);
            break;
        }

        uint32_t word;
        memcpy(&word, dbt->memory + pc, 4);

        rv64_insn_t insn;
        rv64_decode(word, &insn);
        count++;

        // -- Peek-ahead for instruction fusion --
        rv64_insn_t next;
        bool have_next = false;
        if (pc + 8 <= dbt->memory_size) {
            uint32_t next_word;
            memcpy(&next_word, dbt->memory + pc + 4, 4);
            rv64_decode(next_word, &next);
            have_next = true;
        }

        // -- Fusion: SLT/SLTI/SLTU/SLTIU + BEQ/BNE against x0 --
        // Reuse the compare flags to branch directly, avoiding a
        // redundant test of the SLT result register.
        if (have_next
            && ((insn.opcode == OP_REG && insn.funct7 == 0
                 && (insn.funct3 == ALU_SLT || insn.funct3 == ALU_SLTU))
                || (insn.opcode == OP_IMM
                    && (insn.funct3 == ALU_SLTI || insn.funct3 == ALU_SLTIU)))
            && next.opcode == OP_BRANCH
            && (next.funct3 == 0 || next.funct3 == 1)
            && ((next.rs1 == insn.rd && next.rs2 == 0)
                || (next.rs2 == insn.rd && next.rs1 == 0))) {
            uint64_t branch_pc = pc + 4;
            uint64_t target = branch_pc + static_cast<int64_t>(next.imm);

            bool is_unsigned = (insn.opcode == OP_REG)
                ? (insn.funct3 == ALU_SLTU)
                : (insn.funct3 == ALU_SLTIU);

            // Emit the comparison.
            if (insn.opcode == OP_REG) {
                int rs1 = rc_read(&e, &rc, insn.rs1);
                int rs2 = rc_read(&e, &rc, insn.rs2);
                emit_cmp_r64(&e, rs1, rs2);
            } else {
                int rs1 = rc_read(&e, &rc, insn.rs1);
                emit_mov_r64_imm64(&e, A64_X0, static_cast<uint64_t>(
                    static_cast<int64_t>(insn.imm)));
                emit_cmp_r64(&e, rs1, A64_X0);
            }

            // Preserve the SLT result in rd if needed.
            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_cset(&e, rd, is_unsigned ? A64_COND_CC : A64_COND_LT);
            }

            // Determine the AArch64 condition for the fused branch.
            // BNE rd,x0 means "branch if SLT result != 0" = "branch if LT"
            // BEQ rd,x0 means "branch if SLT result == 0" = "branch if GE"
            uint8_t cond;
            if (next.funct3 == 1) { // BNE
                cond = is_unsigned ? A64_COND_CC : A64_COND_LT;
            } else { // BEQ
                cond = is_unsigned ? A64_COND_CS : A64_COND_GE;
            }

            // Diamond merge: branch-over-one → CSEL (branchless)
            if (next.imm == 8 && pc + 12 <= dbt->memory_size) {
                uint32_t skip_word;
                memcpy(&skip_word, dbt->memory + pc + 8, 4);
                rv64_insn_t skip;
                rv64_decode(skip_word, &skip);

                // Invert condition for CSEL: if branch IS taken, skip
                // the instruction, so CSEL selects the old value.
                uint8_t csel_cond = cond ^ 1;

                bool can_predicate = false;
                if (skip.opcode == OP_IMM && skip.rd != 0
                    && (skip.funct3 == ALU_ADDI || skip.funct3 == ALU_XORI
                        || skip.funct3 == ALU_ORI || skip.funct3 == ALU_ANDI)) {
                    can_predicate = true;
                }
                if (skip.opcode == OP_REG && skip.rd != 0
                    && skip.funct7 != 0x01
                    && (skip.funct3 == ALU_ADD || skip.funct3 == ALU_XOR
                        || skip.funct3 == ALU_OR || skip.funct3 == ALU_AND)) {
                    can_predicate = true;
                }
                if (skip.opcode == OP_LUI && skip.rd != 0) {
                    can_predicate = true;
                }

                if (can_predicate) {
                    // Compute skip instruction result into X0 (scratch).
                    if (skip.opcode == OP_LUI) {
                        emit_mov_r64_imm32(&e, A64_X0, skip.imm);
                    } else if (skip.opcode == OP_IMM) {
                        int hr_src = rc_read(&e, &rc, skip.rs1);
                        emit_mov_r64(&e, A64_X0, hr_src);
                        switch (skip.funct3) {
                        case ALU_ADDI:
                            if (skip.imm >= 0 && skip.imm < 4096)
                                emit_add_r64_imm(&e, A64_X0, A64_X0,
                                    static_cast<uint32_t>(skip.imm));
                            else {
                                emit_mov_r64_imm64(&e, A64_X1, static_cast<uint64_t>(
                                    static_cast<int64_t>(skip.imm)));
                                emit_add_r64(&e, A64_X0, A64_X0, A64_X1);
                            }
                            break;
                        case ALU_XORI:
                            if (!emit_eor_r64_imm(&e, A64_X0, A64_X0,
                                    static_cast<uint64_t>(
                                        static_cast<int64_t>(skip.imm)))) {
                                emit_mov_r64_imm64(&e, A64_X1, static_cast<uint64_t>(
                                    static_cast<int64_t>(skip.imm)));
                                emit_eor_r64(&e, A64_X0, A64_X0, A64_X1);
                            }
                            break;
                        case ALU_ORI:
                            if (!emit_orr_r64_imm(&e, A64_X0, A64_X0,
                                    static_cast<uint64_t>(
                                        static_cast<int64_t>(skip.imm)))) {
                                emit_mov_r64_imm64(&e, A64_X1, static_cast<uint64_t>(
                                    static_cast<int64_t>(skip.imm)));
                                emit_orr_r64(&e, A64_X0, A64_X0, A64_X1);
                            }
                            break;
                        case ALU_ANDI:
                            if (!emit_and_r64_imm(&e, A64_X0, A64_X0,
                                    static_cast<uint64_t>(
                                        static_cast<int64_t>(skip.imm)))) {
                                emit_mov_r64_imm64(&e, A64_X1, static_cast<uint64_t>(
                                    static_cast<int64_t>(skip.imm)));
                                emit_inst(&e, 0x8A000000 | (A64_X1 << 16)
                                          | (A64_X0 << 5) | A64_X0);
                            }
                            break;
                        }
                    } else { // OP_REG
                        int hr_s1 = rc_read(&e, &rc, skip.rs1);
                        int hr_s2 = rc_read(&e, &rc, skip.rs2);
                        emit_mov_r64(&e, A64_X0, hr_s1);
                        switch (skip.funct3) {
                        case ALU_ADD:
                            if (skip.funct7 == 0x20)
                                emit_sub_r64(&e, A64_X0, A64_X0, hr_s2);
                            else
                                emit_add_r64(&e, A64_X0, A64_X0, hr_s2);
                            break;
                        case ALU_XOR:
                            emit_eor_r64(&e, A64_X0, A64_X0, hr_s2);
                            break;
                        case ALU_OR:
                            emit_orr_r64(&e, A64_X0, A64_X0, hr_s2);
                            break;
                        case ALU_AND:
                            emit_inst(&e, 0x8A000000 | (hr_s2 << 16)
                                      | (A64_X0 << 5) | A64_X0);
                            break;
                        }
                    }

                    // Re-emit the comparison (skip instruction may have
                    // clobbered flags via ADD/SUB).
                    if (insn.opcode == OP_REG) {
                        int rs1 = rc_read(&e, &rc, insn.rs1);
                        int rs2 = rc_read(&e, &rc, insn.rs2);
                        emit_cmp_r64(&e, rs1, rs2);
                    } else {
                        int rs1 = rc_read(&e, &rc, insn.rs1);
                        emit_mov_r64_imm64(&e, A64_X1, static_cast<uint64_t>(
                            static_cast<int64_t>(insn.imm)));
                        emit_cmp_r64(&e, rs1, A64_X1);
                    }

                    if (insn.rd) {
                        int rd = rc_write(&e, &rc, insn.rd);
                        emit_cset(&e, rd, is_unsigned ? A64_COND_CC : A64_COND_LT);
                    }

                    // CSEL: rd = branch_taken ? old_rd : X0 (new value)
                    int hr_rd = rc_read(&e, &rc, skip.rd);
                    emit_csel(&e, hr_rd, hr_rd, A64_X0, csel_cond);

                    int slot = rc_find(&rc, skip.rd);
                    if (slot >= 0) {
                        rc.slots[slot].dirty = 1;
                        rc.slots[slot].last_use = ++rc.clock;
                    }

                    dbt_trace_fusion(dbt, pc, "slt_branch_diamond");
                    pc += 12;
                    count += 2;
                    dbt->insns_fused++;
                    continue;
                }
            }

            // Superblock: SLT+branch back-edge to loop start.
            if (self_loop && target == guest_pc) {
                rc_flush(&e, &rc); fc_flush(&e, &fc);
                uint32_t bcond_patch = emit_b_cond(&e, cond, 0);
                emit_patch_b19(&e, bcond_patch, warm_entry);
                emit_exit_chained(&e, dbt, branch_pc + 4);
                dbt->insns_fused++;
                count++;
                goto done;
            }

            // Superblock: SLT+branch side exit (forward).
            if (self_loop && next.imm > 0
                && num_side_exits < MAX_SIDE_EXITS
                && count < MAX_BLOCK_INSNS - 5) {
                uint32_t bcond_patch = emit_b_cond(&e, cond, 0);
                side_exits[num_side_exits].jcc_patch = bcond_patch;
                side_exits[num_side_exits].target_pc = target;
                side_exits[num_side_exits].expected_next_pc = 0;
                memcpy(side_exits[num_side_exits].snapshot, rc.slots,
                       sizeof(rc.slots));
                num_side_exits++;
                pc = branch_pc + 4;
                count++;
                dbt->insns_fused++;
                continue;
            }

            // Non-diamond SLT+branch fusion (non-superblock).
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            uint32_t bcond_patch = emit_b_cond(&e, cond, 0);
            emit_exit_chained(&e, dbt, branch_pc + 4);
            emit_patch_b19(&e, bcond_patch, emit_pos(&e));
            emit_exit_chained(&e, dbt, target);
            dbt_trace_fusion(dbt, pc, "slt_branch");
            dbt->insns_fused++;
            count++;
            goto done;
        }

        // -- Fusion: LUI/AUIPC + LOAD/STORE with computed address --
        if (have_next
            && (insn.opcode == OP_LUI || insn.opcode == OP_AUIPC)
            && insn.rd) {
            int64_t base = (insn.opcode == OP_AUIPC)
                ? static_cast<int64_t>(pc)
                : 0;
            uint64_t addr = static_cast<uint64_t>(
                base + static_cast<int64_t>(insn.imm)
                     + static_cast<int64_t>(next.imm));

            if (next.opcode == OP_LOAD && next.rs1 == insn.rd) {
                // Preserve LUI/AUIPC result in rd when load writes elsewhere.
                if (insn.rd != next.rd) {
                    int au_rd = rc_write(&e, &rc, insn.rd);
                    emit_mov_r64_imm64(&e, au_rd,
                        static_cast<uint64_t>(
                            base + static_cast<int64_t>(insn.imm)));
                }

                emit_mov_r64_imm64(&e, A64_X0, addr);
                int rd = next.rd ? rc_write(&e, &rc, next.rd) : A64_X1;
                switch (next.funct3) {
                case LD_LB:  emit_load_mem8s(&e, rd, A64_X0);  break;
                case LD_LH:  emit_load_mem16s(&e, rd, A64_X0); break;
                case LD_LW:  emit_load_mem32s(&e, rd, A64_X0); break;
                case LD_LD:  emit_load_mem64(&e, rd, A64_X0);  break;
                case LD_LBU: emit_load_mem8u(&e, rd, A64_X0);  break;
                case LD_LHU: emit_load_mem16u(&e, rd, A64_X0); break;
                case LD_LWU: emit_load_mem32(&e, rd, A64_X0);  break;
                default: goto no_addr_fusion;
                }
                dbt_trace_fusion(dbt, pc,
                    insn.opcode == OP_AUIPC ? "auipc_load" : "lui_load");
                pc += 8;
                count++;
                dbt->insns_fused++;
                continue;
            }

            if (next.opcode == OP_STORE && next.rs1 == insn.rd) {
                // Preserve LUI/AUIPC result in rd.
                int au_rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm64(&e, au_rd,
                    static_cast<uint64_t>(
                        base + static_cast<int64_t>(insn.imm)));

                // Value to store: handle x0 (zero register) safely.
                int rs2;
                if (next.rs2 == 0) {
                    emit_mov_r64(&e, A64_X1, A64_XZR);
                    rs2 = A64_X1;
                } else {
                    rs2 = rc_read(&e, &rc, next.rs2);
                    emit_mov_r64(&e, A64_X1, rs2);
                }

                emit_mov_r64_imm64(&e, A64_X0, addr);
                switch (next.funct3) {
                case ST_SB: emit_store_mem8(&e, A64_X0, A64_X1);  break;
                case ST_SH: emit_store_mem16(&e, A64_X0, A64_X1); break;
                case ST_SW: emit_store_mem32(&e, A64_X0, A64_X1); break;
                case ST_SD: emit_store_mem64(&e, A64_X0, A64_X1); break;
                default: goto no_addr_fusion;
                }
                dbt_trace_fusion(dbt, pc,
                    insn.opcode == OP_AUIPC ? "auipc_store" : "lui_store");
                pc += 8;
                count++;
                dbt->insns_fused++;
                continue;
            }
        }
no_addr_fusion:

        switch (insn.opcode) {

        // -- LUI (with LUI+ADDI and LUI+JALR fusion) --
        case OP_LUI: {
            // Fusion: LUI rd + JALR rs1=rd → direct jump/call
            uint64_t target_u64;
            uint64_t return_pc;
            if (have_next
                && dbt_resolve_direct_jalr_target(pc, insn, next,
                                                   &target_u64, &return_pc)) {
                // Materialize LUI result if JALR writes a different rd.
                if (insn.rd != next.rd && insn.rd != 0) {
                    int rd = rc_write(&e, &rc, insn.rd);
                    emit_mov_r64_imm32(&e, rd, insn.imm);
                }
                if (next.rd) {
                    int rd = rc_write(&e, &rc, next.rd);
                    emit_mov_r64_imm64(&e, rd, return_pc);
                }
                // Try inline call for JAL ra (function call).
                if (next.rd == 1) {
                    block_entry_t *be = dbt_cache_lookup(dbt, target_u64);
                    if (try_emit_inline_call(&e, &rc, &fc, dbt, target_u64,
                                              be, return_pc, side_exits,
                                              &num_side_exits)) {
                        dbt_trace_fusion(dbt, pc, "lui_jalr_inline");
                        dbt->insns_fused++;
                        pc = return_pc;
                        count++;
                        continue;
                    }
                }
                rc_flush(&e, &rc); fc_flush(&e, &fc);
                emit_exit_chained(&e, dbt, target_u64);
                dbt_trace_fusion(dbt, pc, "lui_jalr");
                dbt->insns_fused++;
                count++;
                goto done;
            }

            // Fusion: LUI rd + ADDI rd,rd,lower → MOV rd, imm32
            if (have_next && insn.rd
                && next.opcode == OP_IMM && next.funct3 == ALU_ADDI
                && next.rd == insn.rd && next.rs1 == insn.rd) {
                int64_t val = static_cast<int64_t>(insn.imm)
                            + static_cast<int64_t>(next.imm);
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm64(&e, rd, static_cast<uint64_t>(val));
                dbt_trace_fusion(dbt, pc, "lui_addi");
                pc += 8;
                count++;
                dbt->insns_fused++;
                continue;
            }

            // Unfused LUI.
            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm32(&e, rd, insn.imm);
            }
            pc += 4;
            continue;
        }

        // -- AUIPC (with AUIPC+ADDI and AUIPC+JALR fusion) --
        case OP_AUIPC: {
            // Fusion: AUIPC rd + JALR rs1=rd → direct jump/call
            uint64_t target_u64;
            uint64_t return_pc;
            if (have_next
                && dbt_resolve_direct_jalr_target(pc, insn, next,
                                                   &target_u64, &return_pc)) {
                if (insn.rd != next.rd && insn.rd != 0) {
                    int rd = rc_write(&e, &rc, insn.rd);
                    int64_t val = static_cast<int64_t>(pc)
                                + static_cast<int64_t>(insn.imm);
                    emit_mov_r64_imm64(&e, rd, static_cast<uint64_t>(val));
                }
                if (next.rd) {
                    int rd = rc_write(&e, &rc, next.rd);
                    emit_mov_r64_imm64(&e, rd, return_pc);
                }
                // Try inline call for JAL ra (function call).
                if (next.rd == 1) {
                    block_entry_t *be = dbt_cache_lookup(dbt, target_u64);
                    if (try_emit_inline_call(&e, &rc, &fc, dbt, target_u64,
                                              be, return_pc, side_exits,
                                              &num_side_exits)) {
                        dbt_trace_fusion(dbt, pc, "auipc_jalr_inline");
                        dbt->insns_fused++;
                        pc = return_pc;
                        count++;
                        continue;
                    }
                }
                rc_flush(&e, &rc); fc_flush(&e, &fc);
                emit_exit_chained(&e, dbt, target_u64);
                dbt_trace_fusion(dbt, pc, "auipc_jalr");
                dbt->insns_fused++;
                count++;
                goto done;
            }

            // Fusion: AUIPC rd + ADDI rd,rd,lower → MOV rd, pc+imm
            if (have_next && insn.rd
                && next.opcode == OP_IMM && next.funct3 == ALU_ADDI
                && next.rd == insn.rd && next.rs1 == insn.rd) {
                int64_t val = static_cast<int64_t>(pc)
                            + static_cast<int64_t>(insn.imm)
                            + static_cast<int64_t>(next.imm);
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm64(&e, rd, static_cast<uint64_t>(val));
                dbt_trace_fusion(dbt, pc, "auipc_addi");
                pc += 8;
                count++;
                dbt->insns_fused++;
                continue;
            }

            // Unfused AUIPC.
            if (insn.rd) {
                int64_t val = static_cast<int64_t>(pc)
                            + static_cast<int64_t>(insn.imm);
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm64(&e, rd, static_cast<uint64_t>(val));
            }
            pc += 4;
            continue;
        }

        // -- JAL --
        case OP_JAL: {
            uint64_t target = pc + static_cast<int64_t>(insn.imm);

            // Superblock: unconditional backward jump to loop start.
            if (self_loop && insn.rd == 0 && target == guest_pc) {
                rc_flush(&e, &rc); fc_flush(&e, &fc);
                uint32_t b_patch = emit_b(&e, 0);
                emit_patch_b26(&e, b_patch, warm_entry);
                goto done;
            }

            // Superblock: forward unconditional jump — follow inline.
            if (self_loop && insn.rd == 0 && insn.imm > 0) {
                pc = target;
                continue;
            }

            // Inline CALL: if this is a function call (JAL ra) and the
            // target is already translated, emit a native BLR instead
            // of exiting the block.
            if (insn.rd == 1) {
                block_entry_t *be = dbt_cache_lookup(dbt, target);
                if (try_emit_inline_call(&e, &rc, &fc, dbt, target, be,
                                          pc + 4, side_exits,
                                          &num_side_exits)) {
                    dbt_trace_fusion(dbt, pc, "inline_call");
                    pc += 4;
                    count++;
                    continue;
                }
            }

            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm64(&e, rd, pc + 4);
            }
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            emit_exit_chained(&e, dbt, target);
            goto done;
        }

        // -- JALR --
        case OP_JALR: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            // X0 = rs1 + imm (target)
            if (insn.imm) {
                emit_add_r64_imm(&e, A64_X0, rs1,
                                  static_cast<uint32_t>(insn.imm & 0xFFF));
                // Handle negative imm: if imm is negative, use SUB
                if (insn.imm < 0) {
                    emit_mov_r64_imm64(&e, A64_X1, static_cast<uint64_t>(
                        static_cast<int64_t>(insn.imm)));
                    emit_add_r64(&e, A64_X0, rs1, A64_X1);
                } else {
                    emit_add_r64_imm(&e, A64_X0, rs1, insn.imm & 0xFFF);
                }
            } else {
                emit_mov_r64(&e, A64_X0, rs1);
            }
            // Clear bit 0 per JALR spec.
            if (!emit_and_r64_imm(&e, A64_X0, A64_X0, ~1ULL)) {
                emit_mov_r64_imm64(&e, A64_X1, ~1ULL);
                emit_and_r64(&e, A64_X0, A64_X0, A64_X1);
            }
            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm64(&e, rd, pc + 4);
            }
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            emit_exit_indirect(&e, A64_X0);
            goto done;
        }

        // -- BRANCH --
        case OP_BRANCH: {
            uint64_t target = pc + static_cast<int64_t>(insn.imm);

            // Conditional branch condition code.
            uint8_t cond;
            switch (insn.funct3) {
            case BR_BEQ:  cond = A64_COND_EQ; break;
            case BR_BNE:  cond = A64_COND_NE; break;
            case BR_BLT:  cond = A64_COND_LT; break;
            case BR_BGE:  cond = A64_COND_GE; break;
            case BR_BLTU: cond = A64_COND_CC; break;
            case BR_BGEU: cond = A64_COND_CS; break;
            default:
                rc_flush(&e, &rc); fc_flush(&e, &fc);
                emit_exit_with_pc(&e, pc + 4);
                goto done;
            }

            // Superblock: back-edge to loop start → B.cond to warm_entry.
            if (self_loop && target == guest_pc) {
                int rs1 = rc_read(&e, &rc, insn.rs1);
                int rs2 = rc_read(&e, &rc, insn.rs2);
                rc_flush(&e, &rc); fc_flush(&e, &fc);
                emit_cmp_r64(&e, rs1, rs2);
                uint32_t bcond_patch = emit_b_cond(&e, cond, 0);
                emit_patch_b19(&e, bcond_patch, warm_entry);
                // Fall-through = loop exit.
                emit_exit_chained(&e, dbt, pc + 4);
                goto done;
            }

            // Superblock side exit: forward branch within self-loop.
            // Record taken path as cold stub, continue with fall-through.
            if (self_loop && insn.imm > 0
                && num_side_exits < MAX_SIDE_EXITS
                && count < MAX_BLOCK_INSNS - 4) {
                int rs1 = rc_read(&e, &rc, insn.rs1);
                int rs2 = rc_read(&e, &rc, insn.rs2);
                emit_cmp_r64(&e, rs1, rs2);
                uint32_t bcond_patch = emit_b_cond(&e, cond, 0);
                side_exits[num_side_exits].jcc_patch = bcond_patch;
                side_exits[num_side_exits].target_pc = target;
                side_exits[num_side_exits].expected_next_pc = 0;
                memcpy(side_exits[num_side_exits].snapshot, rc.slots,
                       sizeof(rc.slots));
                num_side_exits++;
                pc += 4;
                continue;
            }

            // Normal branch: terminate block with two exits.
            {
                int rs1 = rc_read(&e, &rc, insn.rs1);
                int rs2 = rc_read(&e, &rc, insn.rs2);
                emit_cmp_r64(&e, rs1, rs2);
            }
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            uint32_t bcond_patch = emit_b_cond(&e, cond, 0);
            // Fall-through: not taken → pc+4
            emit_exit_chained(&e, dbt, pc + 4);
            // Taken:
            emit_patch_b19(&e, bcond_patch, emit_pos(&e));
            emit_exit_chained(&e, dbt, target);
            goto done;
        }

        // -- LOAD --
        case OP_LOAD: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            // Compute address: X0 = rs1 + imm
            if (insn.imm) {
                emit_mov_r64_imm64(&e, A64_X0, static_cast<uint64_t>(
                    static_cast<int64_t>(insn.imm)));
                emit_add_r64(&e, A64_X0, rs1, A64_X0);
            } else {
                emit_mov_r64(&e, A64_X0, rs1);
            }
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : A64_X1;
            switch (insn.funct3) {
            case LD_LB:  emit_load_mem8s(&e, rd, A64_X0);  break;
            case LD_LH:  emit_load_mem16s(&e, rd, A64_X0); break;
            case LD_LW:  emit_load_mem32s(&e, rd, A64_X0); break;
            case LD_LD:  emit_load_mem64(&e, rd, A64_X0);  break;
            case LD_LBU: emit_load_mem8u(&e, rd, A64_X0);  break;
            case LD_LHU: emit_load_mem16u(&e, rd, A64_X0); break;
            case LD_LWU: emit_load_mem32(&e, rd, A64_X0);  break;
            }
            pc += 4;
            continue;
        }

        // -- STORE --
        case OP_STORE: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rs2 = rc_read(&e, &rc, insn.rs2);
            // Move value to X1 first — rc_read for x0 returns A64_X0,
            // which would be clobbered by the address calculation below.
            emit_mov_r64(&e, A64_X1, rs2);
            // Address: X0 = rs1 + imm
            if (insn.imm) {
                emit_mov_r64_imm64(&e, A64_X0, static_cast<uint64_t>(
                    static_cast<int64_t>(insn.imm)));
                emit_add_r64(&e, A64_X0, rs1, A64_X0);
            } else {
                emit_mov_r64(&e, A64_X0, rs1);
            }
            switch (insn.funct3) {
            case ST_SB: emit_store_mem8(&e, A64_X0, A64_X1);  break;
            case ST_SH: emit_store_mem16(&e, A64_X0, A64_X1); break;
            case ST_SW: emit_store_mem32(&e, A64_X0, A64_X1); break;
            case ST_SD: emit_store_mem64(&e, A64_X0, A64_X1); break;
            }
            pc += 4;
            continue;
        }

        // -- IMM (64-bit ALU with immediate) --
        case OP_IMM: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : A64_X1;

            switch (insn.funct3) {
            case ALU_ADDI:
                if (insn.imm >= 0 && insn.imm < 4096) {
                    emit_add_r64_imm(&e, rd, rs1, insn.imm);
                } else if (insn.imm < 0 && insn.imm > -4096) {
                    emit_sub_r64_imm(&e, rd, rs1, -insn.imm);
                } else {
                    emit_mov_r64_imm64(&e, A64_X0, static_cast<uint64_t>(
                        static_cast<int64_t>(insn.imm)));
                    emit_add_r64(&e, rd, rs1, A64_X0);
                }
                break;
            case ALU_SLTI:
                emit_mov_r64_imm64(&e, A64_X0, static_cast<uint64_t>(
                    static_cast<int64_t>(insn.imm)));
                emit_cmp_r64(&e, rs1, A64_X0);
                emit_cset(&e, rd, A64_COND_LT);
                break;
            case ALU_SLTIU:
                emit_mov_r64_imm64(&e, A64_X0, static_cast<uint64_t>(
                    static_cast<int64_t>(insn.imm)));
                emit_cmp_r64(&e, rs1, A64_X0);
                emit_cset(&e, rd, A64_COND_CC);  // unsigned <
                break;
            case ALU_XORI:
                if (!emit_eor_r64_imm(&e, rd, rs1, static_cast<uint64_t>(
                        static_cast<int64_t>(insn.imm)))) {
                    emit_mov_r64_imm64(&e, A64_X0, static_cast<uint64_t>(
                        static_cast<int64_t>(insn.imm)));
                    emit_eor_r64(&e, rd, rs1, A64_X0);
                }
                break;
            case ALU_ORI:
                if (!emit_orr_r64_imm(&e, rd, rs1, static_cast<uint64_t>(
                        static_cast<int64_t>(insn.imm)))) {
                    emit_mov_r64_imm64(&e, A64_X0, static_cast<uint64_t>(
                        static_cast<int64_t>(insn.imm)));
                    emit_orr_r64(&e, rd, rs1, A64_X0);
                }
                break;
            case ALU_ANDI:
                if (!emit_and_r64_imm(&e, rd, rs1, static_cast<uint64_t>(
                        static_cast<int64_t>(insn.imm)))) {
                    emit_mov_r64_imm64(&e, A64_X0, static_cast<uint64_t>(
                        static_cast<int64_t>(insn.imm)));
                    emit_and_r64(&e, rd, rs1, A64_X0);
                }
                break;
            case ALU_SLLI:
                emit_lsl_r64_imm(&e, rd, rs1, insn.imm & 63);
                break;
            case ALU_SRLI:
                if (insn.funct7 & 0x20) {
                    // SRAI
                    emit_asr_r64_imm(&e, rd, rs1, insn.imm & 63);
                } else {
                    emit_lsr_r64_imm(&e, rd, rs1, insn.imm & 63);
                }
                break;
            }
            pc += 4;
            continue;
        }

        // -- REG (64-bit ALU register-register) --
        case OP_REG: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rs2 = rc_read(&e, &rc, insn.rs2);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : A64_X1;

            if (insn.funct7 == 0x01) {
                // M extension
                switch (insn.funct3) {
                case 0: // MUL
                    emit_mul_r64(&e, rd, rs1, rs2);
                    break;
                case 1: // MULH
                    emit_smulh(&e, rd, rs1, rs2);
                    break;
                case 2: { // MULHSU — signed × unsigned high
                    // SMULH treats both as signed. To get signed×unsigned:
                    // If rs2's sign bit is set, SMULH interprets it as negative
                    // (off by 2^64), so we add rs1 to correct.
                    // If rs1 is negative, SMULH treats rs2 correctly for the
                    // signed interpretation, but the unsigned interpretation
                    // of rs2 differs when rs2 >= 2^63, which is the case
                    // above. Net: MULHSU = SMULH(rs1,rs2) + (rs2<0 ? rs1 : 0)
                    emit_smulh(&e, rd, rs1, rs2);
                    // ASR X0, rs2, #63 → 0 if rs2 positive, -1 if negative
                    emit_inst(&e, 0x937FFC00 | (rs2 << 5) | A64_X0);
                    // AND X0, X0, rs1 → rs1 if rs2 was negative, 0 otherwise
                    emit_inst(&e, 0x8A000000 | (rs1 << 16) | (A64_X0 << 5) | A64_X0);
                    // ADD rd, rd, X0
                    emit_add_r64(&e, rd, rd, A64_X0);
                    break;
                }
                case 3: // MULHU
                    emit_umulh(&e, rd, rs1, rs2);
                    break;
                case 4: { // DIV — RISC-V: div-by-0 → -1, overflow → INT64_MIN
                    // CBZ rs2, .zero
                    uint32_t zchk = emit_cbz_x64(&e, rs2, 0);
                    emit_sdiv_r64(&e, rd, rs1, rs2);
                    uint32_t done = emit_b(&e, 0);
                    // .zero: rd = -1 via ORN Xd, XZR, XZR
                    emit_patch_b19(&e, zchk, emit_pos(&e));
                    emit_inst(&e, 0xAA2003E0 | rd);  // ORN Xd, XZR, XZR
                    emit_patch_b26(&e, done, emit_pos(&e));
                    break;
                }
                case 5: { // DIVU — RISC-V: div-by-0 → UINT64_MAX
                    uint32_t zchk = emit_cbz_x64(&e, rs2, 0);
                    emit_udiv_r64(&e, rd, rs1, rs2);
                    uint32_t done = emit_b(&e, 0);
                    emit_patch_b19(&e, zchk, emit_pos(&e));
                    emit_inst(&e, 0xAA2003E0 | rd);  // ORN Xd, XZR, XZR
                    emit_patch_b26(&e, done, emit_pos(&e));
                    break;
                }
                case 6: { // REM — RISC-V: rem-by-0 → rs1
                    uint32_t zchk = emit_cbz_x64(&e, rs2, 0);
                    emit_sdiv_r64(&e, A64_X0, rs1, rs2);
                    emit_msub_r64(&e, rd, A64_X0, rs2, rs1);
                    uint32_t done = emit_b(&e, 0);
                    emit_patch_b19(&e, zchk, emit_pos(&e));
                    emit_mov_r64(&e, rd, rs1);
                    emit_patch_b26(&e, done, emit_pos(&e));
                    break;
                }
                case 7: { // REMU — RISC-V: remu-by-0 → rs1
                    uint32_t zchk = emit_cbz_x64(&e, rs2, 0);
                    emit_udiv_r64(&e, A64_X0, rs1, rs2);
                    emit_msub_r64(&e, rd, A64_X0, rs2, rs1);
                    uint32_t done = emit_b(&e, 0);
                    emit_patch_b19(&e, zchk, emit_pos(&e));
                    emit_mov_r64(&e, rd, rs1);
                    emit_patch_b26(&e, done, emit_pos(&e));
                    break;
                }
                }
            } else {
                switch (insn.funct3) {
                case ALU_ADD:
                    if (insn.funct7 == 0x20)
                        emit_sub_r64(&e, rd, rs1, rs2);  // SUB
                    else
                        emit_add_r64(&e, rd, rs1, rs2);  // ADD
                    break;
                case ALU_SLL:
                    emit_lslv_r64(&e, rd, rs1, rs2);
                    break;
                case ALU_SLT:
                    emit_cmp_r64(&e, rs1, rs2);
                    emit_cset(&e, rd, A64_COND_LT);
                    break;
                case ALU_SLTU:
                    emit_cmp_r64(&e, rs1, rs2);
                    emit_cset(&e, rd, A64_COND_CC);
                    break;
                case ALU_XOR:
                    emit_eor_r64(&e, rd, rs1, rs2);
                    break;
                case ALU_SRL:
                    if (insn.funct7 == 0x20)
                        emit_asrv_r64(&e, rd, rs1, rs2);  // SRA
                    else
                        emit_lsrv_r64(&e, rd, rs1, rs2);  // SRL
                    break;
                case ALU_OR:
                    emit_orr_r64(&e, rd, rs1, rs2);
                    break;
                case ALU_AND:
                    emit_and_r64(&e, rd, rs1, rs2);
                    break;
                }
            }
            pc += 4;
            continue;
        }

        // -- IMM32 (32-bit ALU with immediate, sign-extend result) --
        case OP_IMM32: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : A64_X1;

            switch (insn.funct3) {
            case ALU_ADDI: {  // ADDIW
                if (insn.imm >= 0 && insn.imm < 4096) {
                    emit_inst(&e, 0x11000000 | ((insn.imm & 0xFFF) << 10)
                              | (rs1 << 5) | rd);  // ADD Wd, Wn, #imm
                } else if (insn.imm < 0 && insn.imm > -4096) {
                    emit_inst(&e, 0x51000000 | (((-insn.imm) & 0xFFF) << 10)
                              | (rs1 << 5) | rd);  // SUB Wd, Wn, #imm
                } else {
                    emit_mov_r64_imm32(&e, A64_X0, insn.imm);
                    emit_add_r32(&e, rd, rs1, A64_X0);
                }
                emit_sxtw(&e, rd, rd);
                break;
            }
            case ALU_SLLI:  // SLLIW
                emit_lsl_r32_imm(&e, rd, rs1, insn.imm & 31);
                emit_sxtw(&e, rd, rd);
                break;
            case ALU_SRLI:
                if (insn.funct7 & 0x20) {
                    // SRAIW
                    emit_asr_r32_imm(&e, rd, rs1, insn.imm & 31);
                } else {
                    // SRLIW
                    emit_lsr_r32_imm(&e, rd, rs1, insn.imm & 31);
                }
                emit_sxtw(&e, rd, rd);
                break;
            }
            pc += 4;
            continue;
        }

        // -- REG32 (32-bit ALU register-register, sign-extend result) --
        case OP_REG32: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rs2 = rc_read(&e, &rc, insn.rs2);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : A64_X1;

            if (insn.funct7 == 0x01) {
                // M extension (32-bit)
                switch (insn.funct3) {
                case 0: // MULW
                    emit_mul_r32(&e, rd, rs1, rs2);
                    break;
                case 4: { // DIVW — RISC-V: div-by-0 → -1
                    // CBZ Wrs2, .zero (32-bit zero test)
                    uint32_t zchk = emit_pos(&e);
                    emit_inst(&e, 0x34000000 | rs2);  // CBZ Wt, +0 (patched)
                    emit_sdiv_r32(&e, rd, rs1, rs2);
                    uint32_t done = emit_b(&e, 0);
                    emit_patch_b19(&e, zchk, emit_pos(&e));
                    emit_inst(&e, 0xAA2003E0 | rd);  // ORN Xd, XZR, XZR = -1
                    emit_patch_b26(&e, done, emit_pos(&e));
                    break;
                }
                case 5: { // DIVUW — RISC-V: div-by-0 → all-ones
                    uint32_t zchk = emit_pos(&e);
                    emit_inst(&e, 0x34000000 | rs2);  // CBZ Wt, +0
                    emit_udiv_r32(&e, rd, rs1, rs2);
                    uint32_t done = emit_b(&e, 0);
                    emit_patch_b19(&e, zchk, emit_pos(&e));
                    emit_inst(&e, 0xAA2003E0 | rd);  // ORN Xd, XZR, XZR
                    emit_patch_b26(&e, done, emit_pos(&e));
                    break;
                }
                case 6: { // REMW — RISC-V: rem-by-0 → rs1
                    uint32_t zchk = emit_pos(&e);
                    emit_inst(&e, 0x34000000 | rs2);  // CBZ Wt, +0
                    emit_sdiv_r32(&e, A64_X0, rs1, rs2);
                    // MSUB Wd, Wn, Wm, Wa (32-bit)
                    emit_inst(&e, 0x1B008000 | (rs2 << 16) | (rs1 << 10)
                              | (A64_X0 << 5) | rd);
                    uint32_t done = emit_b(&e, 0);
                    emit_patch_b19(&e, zchk, emit_pos(&e));
                    emit_mov_r64(&e, rd, rs1);
                    emit_patch_b26(&e, done, emit_pos(&e));
                    break;
                }
                case 7: { // REMUW — RISC-V: remu-by-0 → rs1
                    uint32_t zchk = emit_pos(&e);
                    emit_inst(&e, 0x34000000 | rs2);  // CBZ Wt, +0
                    emit_udiv_r32(&e, A64_X0, rs1, rs2);
                    emit_inst(&e, 0x1B008000 | (rs2 << 16) | (rs1 << 10)
                              | (A64_X0 << 5) | rd);
                    uint32_t done = emit_b(&e, 0);
                    emit_patch_b19(&e, zchk, emit_pos(&e));
                    emit_mov_r64(&e, rd, rs1);
                    emit_patch_b26(&e, done, emit_pos(&e));
                    break;
                }
                }
            } else {
                switch (insn.funct3) {
                case ALU_ADD:
                    if (insn.funct7 == 0x20)
                        emit_sub_r32(&e, rd, rs1, rs2);  // SUBW
                    else
                        emit_add_r32(&e, rd, rs1, rs2);  // ADDW
                    break;
                case ALU_SLL:  // SLLW
                    emit_lslv_r32(&e, rd, rs1, rs2);
                    break;
                case ALU_SRL:
                    if (insn.funct7 == 0x20)
                        emit_asrv_r32(&e, rd, rs1, rs2);  // SRAW
                    else
                        emit_lsrv_r32(&e, rd, rs1, rs2);  // SRLW
                    break;
                }
            }
            emit_sxtw(&e, rd, rd);
            pc += 4;
            continue;
        }

        // -- FP LOAD (FLD) --
        case OP_FP_LOAD: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            // Address: X0 = rs1 + imm
            if (insn.imm) {
                emit_mov_r64_imm64(&e, A64_X0, static_cast<uint64_t>(
                    static_cast<int64_t>(insn.imm)));
                emit_add_r64(&e, A64_X0, rs1, A64_X0);
            } else {
                emit_mov_r64(&e, A64_X0, rs1);
            }
            int fd = fc_write(&e, &fc, insn.rd);
            emit_load_mem_f64(&e, fd, A64_X0);
            pc += 4;
            continue;
        }

        // -- FP STORE (FSD) --
        case OP_FP_STORE: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int fs2 = fc_read(&e, &fc, insn.rs2);
            if (insn.imm) {
                emit_mov_r64_imm64(&e, A64_X0, static_cast<uint64_t>(
                    static_cast<int64_t>(insn.imm)));
                emit_add_r64(&e, A64_X0, rs1, A64_X0);
            } else {
                emit_mov_r64(&e, A64_X0, rs1);
            }
            emit_store_mem_f64(&e, A64_X0, fs2);
            pc += 4;
            continue;
        }

        // -- FP arithmetic/convert/compare --
        case OP_FP: {
            uint8_t funct5 = insn.funct7 >> 2;

            switch (funct5) {
            case FP_FADD: {
                int fs1 = fc_read(&e, &fc, insn.rs1);
                int fs2 = fc_read(&e, &fc, insn.rs2);
                int fd = fc_write(&e, &fc, insn.rd);
                emit_fadd_d(&e, fd, fs1, fs2);
                break;
            }
            case FP_FSUB: {
                int fs1 = fc_read(&e, &fc, insn.rs1);
                int fs2 = fc_read(&e, &fc, insn.rs2);
                int fd = fc_write(&e, &fc, insn.rd);
                emit_fsub_d(&e, fd, fs1, fs2);
                break;
            }
            case FP_FMUL: {
                int fs1 = fc_read(&e, &fc, insn.rs1);
                int fs2 = fc_read(&e, &fc, insn.rs2);
                int fd = fc_write(&e, &fc, insn.rd);
                emit_fmul_d(&e, fd, fs1, fs2);
                break;
            }
            case FP_FDIV: {
                int fs1 = fc_read(&e, &fc, insn.rs1);
                int fs2 = fc_read(&e, &fc, insn.rs2);
                int fd = fc_write(&e, &fc, insn.rd);
                emit_fdiv_d(&e, fd, fs1, fs2);
                break;
            }
            case FP_FSQRT: {
                int fs1 = fc_read(&e, &fc, insn.rs1);
                int fd = fc_write(&e, &fc, insn.rd);
                emit_fsqrt_d(&e, fd, fs1);
                break;
            }
            case FP_FSGNJ: {
                int fs1 = fc_read(&e, &fc, insn.rs1);
                int fs2 = fc_read(&e, &fc, insn.rs2);
                int fd = fc_write(&e, &fc, insn.rd);
                switch (insn.funct3) {
                case 0: // FSGNJ.D — copy sign of fs2
                    if (insn.rs1 == insn.rs2) {
                        emit_fmov_d(&e, fd, fs1);  // FMV.D
                    } else {
                        // ABS(fs1) with sign of fs2: use bit manipulation
                        emit_fabs_d(&e, fd, fs1);
                        emit_fmov_x64_d(&e, A64_X0, fs2);
                        // Test sign bit of fs2
                        emit_cmp_r64_imm(&e, A64_X0, 0);
                        uint32_t skip = emit_b_cond(&e, A64_COND_GE, 0);
                        emit_fneg_d(&e, fd, fd);
                        emit_patch_b19(&e, skip, emit_pos(&e));
                    }
                    break;
                case 1: // FSGNJN.D — negate sign of fs2
                    if (insn.rs1 == insn.rs2) {
                        emit_fneg_d(&e, fd, fs1);  // FNEG.D
                    } else {
                        emit_fabs_d(&e, fd, fs1);
                        emit_fmov_x64_d(&e, A64_X0, fs2);
                        emit_cmp_r64_imm(&e, A64_X0, 0);
                        uint32_t skip = emit_b_cond(&e, A64_COND_LT, 0);
                        emit_fneg_d(&e, fd, fd);
                        emit_patch_b19(&e, skip, emit_pos(&e));
                    }
                    break;
                case 2: // FSGNJX.D — XOR sign bits, preserve magnitude of fs1
                    if (insn.rs1 == insn.rs2) {
                        emit_fabs_d(&e, fd, fs1);  // FABS.D
                    } else {
                        // Extract sign bit of fs2, XOR into sign bit of fs1.
                        emit_fmov_x64_d(&e, A64_X0, fs1);
                        emit_fmov_x64_d(&e, A64_X1, fs2);
                        // Isolate sign bit of fs2: AND X1, X1, #(1<<63)
                        emit_inst(&e, 0x92410021);  // AND X1, X1, #0x8000000000000000
                        // XOR only the sign bit into X0
                        emit_eor_r64(&e, A64_X0, A64_X0, A64_X1);
                        emit_fmov_d_x64(&e, fd, A64_X0);
                    }
                    break;
                }
                break;
            }
            case FP_FMINMAX: {
                int fs1 = fc_read(&e, &fc, insn.rs1);
                int fs2 = fc_read(&e, &fc, insn.rs2);
                int fd = fc_write(&e, &fc, insn.rd);
                if (insn.funct3 == 0)
                    emit_fmin_d(&e, fd, fs1, fs2);
                else
                    emit_fmax_d(&e, fd, fs1, fs2);
                break;
            }
            case FP_FCMP: {
                int fs1 = fc_read(&e, &fc, insn.rs1);
                int fs2 = fc_read(&e, &fc, insn.rs2);
                int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : A64_X1;
                emit_fcmp_d(&e, fs1, fs2);
                switch (insn.funct3) {
                case 0: // FLE.D
                    emit_cset(&e, rd, A64_COND_LS);
                    break;
                case 1: // FLT.D
                    emit_cset(&e, rd, A64_COND_CC);
                    break;
                case 2: // FEQ.D
                    emit_cset(&e, rd, A64_COND_EQ);
                    break;
                }
                break;
            }
            case FP_FCVTW: {
                // FCVT int ← double
                int fs1 = fc_read(&e, &fc, insn.rs1);
                int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : A64_X1;
                if (insn.rs2 == 0) {
                    // FCVT.W.D — double to signed 32-bit
                    emit_fcvtzs_x64_d(&e, rd, fs1);
                    emit_sxtw(&e, rd, rd);
                } else if (insn.rs2 == 1) {
                    // FCVT.WU.D — double to unsigned 32-bit
                    // FCVTZU Xd, Dn (unsigned conversion)
                    emit_inst(&e, 0x9E790000 | (fs1 << 5) | rd);
                    // Zero-extend 32→64: use MOV Wd, Wd
                    emit_mov_r32(&e, rd, rd);
                } else if (insn.rs2 == 2) {
                    // FCVT.L.D — double to signed 64-bit
                    emit_fcvtzs_x64_d(&e, rd, fs1);
                } else {
                    // FCVT.LU.D — double to unsigned 64-bit
                    // Use unsigned variant (FCVTZU)
                    emit_inst(&e, 0x9E790000 | (fs1 << 5) | rd);
                }
                break;
            }
            case FP_FCVTDW: {
                // FCVT double ← int
                int rs1 = rc_read(&e, &rc, insn.rs1);
                int fd = fc_write(&e, &fc, insn.rd);
                if (insn.rs2 == 0) {
                    // FCVT.D.W — signed 32-bit to double
                    emit_sxtw(&e, A64_X0, rs1);
                    emit_scvtf_d_x64(&e, fd, A64_X0);
                } else if (insn.rs2 == 1) {
                    // FCVT.D.WU — unsigned 32-bit to double
                    emit_mov_r32(&e, A64_X0, rs1);  // zero-extend
                    // UCVTF Dd, Xn (unsigned int to double)
                    emit_inst(&e, 0x9E630000 | (A64_X0 << 5) | fd);
                } else if (insn.rs2 == 2) {
                    // FCVT.D.L — signed 64-bit to double
                    emit_scvtf_d_x64(&e, fd, rs1);
                } else {
                    // FCVT.D.LU — unsigned 64-bit to double
                    // UCVTF Dd, Xn
                    emit_inst(&e, 0x9E630000 | (rs1 << 5) | fd);
                }
                break;
            }
            case FP_FCLASS: {
                if (insn.funct3 == 0) {
                    // FMV.X.D — move double bits to integer
                    int fs1 = fc_read(&e, &fc, insn.rs1);
                    int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : A64_X1;
                    emit_fmov_x64_d(&e, rd, fs1);
                } else {
                    // FCLASS.D (funct3==1) — not yet implemented.
                    // Exit to dispatcher to avoid wrong architectural state.
                    rc_flush(&e, &rc); fc_flush(&e, &fc);
                    emit_exit_with_pc(&e, pc);
                    goto done;
                }
                break;
            }
            case FP_FMVDX: {
                // FMV.D.X — move integer bits to double
                int rs1 = rc_read(&e, &rc, insn.rs1);
                int fd = fc_write(&e, &fc, insn.rd);
                emit_fmov_d_x64(&e, fd, rs1);
                break;
            }
            default:
                // Unhandled FP opcode — exit to dispatcher.
                rc_flush(&e, &rc); fc_flush(&e, &fc);
                emit_exit_with_pc(&e, pc);
                goto done;
            }
            pc += 4;
            continue;
        }

        // -- SYSTEM --
        case OP_SYSTEM: {
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            if (insn.imm == 0) {
                // ECALL — set bit 0 signal
                emit_exit_with_pc(&e, pc | 1);
            } else if (insn.imm == 1) {
                // EBREAK — set bit 1 signal
                emit_exit_with_pc(&e, pc | 2);
            } else {
                emit_exit_with_pc(&e, pc);
            }
            goto done;
        }

        // -- FENCE (no-op on single-threaded DBT) --
        case OP_FENCE:
            pc += 4;
            continue;

        default:
            // Unknown opcode — exit to dispatcher.
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            emit_exit_with_pc(&e, pc);
            goto done;
        }
    }

    // Max instructions reached — flush and exit.
    rc_flush(&e, &rc); fc_flush(&e, &fc);
    emit_exit_chained(&e, dbt, pc);

done:
    // Emit cold stubs for superblock side exits.
    for (int i = 0; i < num_side_exits; i++) {
        emit_patch_b19(&e, side_exits[i].jcc_patch, emit_pos(&e));
        if (side_exits[i].target_pc == 0) {
            // Cold exit from inline CALL: callee returned with
            // unexpected next_pc.  RET to the dispatch loop.
            emit_ret(&e);
        } else {
            // Normal side exit: restore dirty registers from snapshot,
            // then chained exit to the taken-path target.
            for (int j = 0; j < RC_NUM_SLOTS; j++) {
                if (side_exits[i].snapshot[j].guest_reg >= 0
                    && side_exits[i].snapshot[j].dirty) {
                    emit_store_guest(&e, side_exits[i].snapshot[j].guest_reg,
                                     rc_host_regs[j]);
                }
            }
            emit_exit_chained(&e, dbt, side_exits[i].target_pc);
        }
    }

    if (e.offset > e.capacity) return nullptr;

    dbt->code_used += e.offset;
    dbt->blocks_translated++;
    dbt->insns_translated += count;
    if (self_loop) {
        dbt->superblock_count++;
        dbt->side_exits_total += num_side_exits;
    }
    return block_start;
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
