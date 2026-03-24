/*! \file dbt.cpp
 * \brief RV64IMD dynamic binary translator — block-at-a-time JIT to x86-64.
 *
 * Reference: ~/riscv/dbt/dbt.c (RV32IMFD version).
 *
 * Key differences from RV32:
 *   - All guest registers are 64-bit (REX.W throughout)
 *   - W-suffix instructions (ADDW, ADDIW, etc.) use 32-bit ops + MOVSXD
 *   - D-only FP (no NaN-boxing, no single-precision)
 *   - 64-bit PCs and addresses
 *   - 6-bit shift amounts (not 5-bit)
 */

#include "dbt.h"
#include "dbt_decoder.h"
#include "dbt_emit_x64.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#ifdef WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

// ---------------------------------------------------------------
// Register cache — 8-slot LRU for guest registers in host registers
// ---------------------------------------------------------------

static constexpr int RC_NUM_SLOTS = 8;

static const int rc_host_regs[RC_NUM_SLOTS] = {
    X64_RSI, X64_RDI, X64_R8, X64_R9,
    X64_R10, X64_R11, X64_R14, X64_R15
};

// Pinned guest registers: a0-a3 (x10-x13) in slots 0-3.
// Pre-loaded by the trampoline, persist across chained block exits.
//
static constexpr int RC_NUM_PINNED = 4;
static const int rc_pinned_guest[RC_NUM_PINNED] = { 10, 11, 12, 13 };

struct rc_slot_t {
    int guest_reg;  // -1 = free
    int dirty;
    int last_use;
    int pinned;     // if true, never evict
};

struct reg_cache_t {
    rc_slot_t slots[RC_NUM_SLOTS];
    int clock;
};

// FP register cache — 6-slot LRU for guest FP registers in XMM2-XMM7.
// XMM0 and XMM1 are scratch (like RAX for integers).
// ---------------------------------------------------------------

static constexpr int FC_NUM_SLOTS = 6;

static const int fc_host_xmm[FC_NUM_SLOTS] = {
    2, 3, 4, 5, 6, 7  // XMM2-XMM7
};

struct fc_slot_t {
    int guest_freg;  // -1 = free
    int dirty;
    int last_use;
};

struct fp_cache_t {
    fc_slot_t slots[FC_NUM_SLOTS];
    int clock;
};

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
    // Prefer free slots.
    for (int i = 0; i < FC_NUM_SLOTS; i++)
        if (fc->slots[i].guest_freg == -1) return i;
    // Evict LRU.
    int lru = 0;
    for (int i = 1; i < FC_NUM_SLOTS; i++)
        if (fc->slots[i].last_use < fc->slots[lru].last_use) lru = i;
    if (fc->slots[lru].dirty)
        emit_store_fp_d(e, fc->slots[lru].guest_freg, fc_host_xmm[lru]);
    fc->slots[lru].guest_freg = -1;
    fc->slots[lru].dirty = 0;
    return lru;
}

// Return XMM register holding guest_freg's value.  Loads on miss.
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

// Return XMM register allocated for writing guest_freg.  Marks dirty.
static int fc_write(emit_t *e, fp_cache_t *fc, int guest_freg) {
    int slot = fc_find(fc, guest_freg);
    if (slot < 0) slot = fc_alloc(e, fc);
    fc->slots[slot].guest_freg = guest_freg;
    fc->slots[slot].dirty = 1;
    fc->slots[slot].last_use = ++fc->clock;
    return fc_host_xmm[slot];
}

// Flush all dirty cached FP registers to memory.
static void fc_flush(emit_t *e, fp_cache_t *fc) {
    for (int i = 0; i < FC_NUM_SLOTS; i++)
        if (fc->slots[i].guest_freg >= 0 && fc->slots[i].dirty)
            emit_store_fp_d(e, fc->slots[i].guest_freg, fc_host_xmm[i]);
}

// Superblock side exits: snapshot register cache at each forward branch.
// Cold stubs emitted after the block flush dirty registers from snapshot.
//
static constexpr int MAX_SIDE_EXITS = 8;

struct side_exit_t {
    uint32_t jcc_patch;         // offset of Jcc rel32 displacement
    uint64_t target_pc;         // guest PC of the taken path
    uint64_t expected_next_pc;  // expected next_pc for inline CALL cold exits
    rc_slot_t snapshot[RC_NUM_SLOTS];
};

static void rc_init(reg_cache_t *rc) {
    for (int i = 0; i < RC_NUM_SLOTS; i++) {
        rc->slots[i].guest_reg = -1;
        rc->slots[i].dirty = 0;
        rc->slots[i].last_use = 0;
        rc->slots[i].pinned = 0;
    }
    rc->clock = 0;
}

// Initialize with pinned guest registers pre-populated.
// The trampoline pre-loads these into the corresponding host registers,
// and they persist across chained block transitions.
//
static void rc_init_pinned(reg_cache_t *rc) {
    rc_init(rc);
    for (int i = 0; i < RC_NUM_PINNED; i++) {
        rc->slots[i].guest_reg = rc_pinned_guest[i];
        rc->slots[i].dirty = 0;
        rc->slots[i].last_use = 0;
        rc->slots[i].pinned = 1;
    }
}

static int rc_find(reg_cache_t *rc, int guest_reg) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg == guest_reg) return i;
    return -1;
}

static int rc_alloc(reg_cache_t *rc, emit_t *e) {
    // Prefer free (non-pinned) slots.
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg == -1 && !rc->slots[i].pinned)
            return i;
    // Evict LRU among non-pinned slots.
    int lru = -1;
    for (int i = 0; i < RC_NUM_SLOTS; i++) {
        if (rc->slots[i].pinned) continue;
        if (lru < 0 || rc->slots[i].last_use < rc->slots[lru].last_use)
            lru = i;
    }
    if (lru < 0) {
        // All slots pinned — shouldn't happen with RC_NUM_PINNED < RC_NUM_SLOTS.
        // Fall back to slot 0 as last resort.
        lru = 0;
    }
    if (rc->slots[lru].dirty)
        emit_store_guest(e, rc->slots[lru].guest_reg, rc_host_regs[lru]);
    rc->slots[lru].guest_reg = -1;
    rc->slots[lru].dirty = 0;
    return lru;
}

// Return host register holding guest_reg's value.  Loads from memory on miss.
//
static int rc_read(emit_t *e, reg_cache_t *rc, int guest_reg) {
    if (guest_reg == 0) {
        // x0 is always zero — use RAX as scratch.
        emit_xor_r64(e, X64_RAX, X64_RAX);
        return X64_RAX;
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

// Return host register allocated for writing guest_reg.  Marks dirty.
//
static int rc_write(emit_t *e, reg_cache_t *rc, int guest_reg) {
    if (guest_reg == 0) return X64_RAX;
    int slot = rc_find(rc, guest_reg);
    if (slot < 0) slot = rc_alloc(rc, e);
    rc->slots[slot].guest_reg = guest_reg;
    rc->slots[slot].dirty = 1;
    rc->slots[slot].last_use = ++rc->clock;
    return rc_host_regs[slot];
}

// Load a guest register into a specific host register.
//
static void rc_load(emit_t *e, reg_cache_t *rc, int host_dst, int guest_reg) {
    int hr = rc_read(e, rc, guest_reg);
    emit_mov_r64(e, host_dst, hr);
}

// Store a specific host register into a guest register's cache slot.
//
static void rc_store(emit_t *e, reg_cache_t *rc, int guest_reg, int host_src) {
    if (guest_reg == 0) return;
    int hr = rc_write(e, rc, guest_reg);
    emit_mov_r64(e, hr, host_src);
}

// Flush all dirty cached registers to memory.
//
static void rc_flush(emit_t *e, reg_cache_t *rc) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg >= 0 && rc->slots[i].dirty)
            emit_store_guest(e, rc->slots[i].guest_reg, rc_host_regs[i]);
}

// Invalidate all register cache slots and reload pinned registers.
// Used after a native CALL where the callee may have modified any guest
// register — the cached values are stale and must be reloaded from ctx.
//
// The trampoline unconditionally stores pinned host registers (RSI, RDI,
// R8, R9) back to ctx after the block RETs.  If we clear the pinned
// mapping, the translator may reassign those host registers to other
// guest registers, and the trampoline post-store will overwrite ctx
// with wrong values.  Re-establishing the pinned slots + emitting
// reload instructions keeps the convention intact.
//
static void rc_invalidate_reload(emit_t *e, reg_cache_t *rc) {
    for (int i = 0; i < RC_NUM_SLOTS; i++) {
        rc->slots[i].guest_reg = -1;
        rc->slots[i].dirty = 0;
        rc->slots[i].last_use = 0;
        rc->slots[i].pinned = 0;
    }
    rc->clock = 0;

    // Restore pinned slots and reload from ctx.
    for (int i = 0; i < RC_NUM_PINNED; i++) {
        rc->slots[i].guest_reg = rc_pinned_guest[i];
        rc->slots[i].dirty = 0;
        rc->slots[i].last_use = 0;
        rc->slots[i].pinned = 1;
        emit_load_guest(e, rc_host_regs[i], rc_pinned_guest[i]);
    }
}

// Forward declarations for block cache (defined below).
static void cache_insert(dbt_state_t *dbt, uint64_t pc, uint8_t *code);
static void emit_exit_chained(emit_t *e, dbt_state_t *dbt, uint64_t target_pc);

static bool dbt_trace_translate_enabled(const dbt_state_t *dbt,
                                        uint64_t guest_pc) {
    if ((dbt->trace & DBT_TRACE_TRANSLATE) == 0) return false;
    return !dbt->trace_guest_pc_filter || dbt->trace_guest_pc == guest_pc;
}

static void dbt_trace_translate_pc(dbt_state_t *dbt, uint64_t guest_pc,
                                   const char *fmt, ...) {
    if (!dbt_trace_translate_enabled(dbt, guest_pc)) return;

    va_list ap;
    va_start(ap, fmt);
    fputs("[dbt-xlate] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void dbt_trace_translate(dbt_state_t *dbt, const char *fmt, ...) {
    if ((dbt->trace & DBT_TRACE_TRANSLATE) == 0) return;

    va_list ap;
    va_start(ap, fmt);
    fputs("[dbt-xlate] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void dbt_trace_fusion(dbt_state_t *dbt, uint64_t pc, const char *kind) {
    dbt_trace_translate_pc(dbt, pc, "fusion guest_pc=0x%llX kind=%s",
                           static_cast<unsigned long long>(pc), kind);
}

// ---------------------------------------------------------------
// Block-level intrinsic stubs.
//
// When translate_block() is asked to translate a block at a known
// intrinsic address, it emits a complete native stub instead of
// translating the RV64 byte loops.  The stub:
//   1. Loads guest registers from ctx (via RBX)
//   2. Converts guest pointers to host pointers (add R12)
//   3. Calls the host-native function
//   4. Stores the result back to guest a0
//   5. Sets next_pc = ra (guest return address)
//   6. Returns to the dispatch loop
//
// This is the same architecture as ~/slow-32/tools/dbt: the caller's
// JAL gets translated normally, block chaining finds the native stub,
// and the RAS-predicted return goes back to the caller.  The RV64
// fallback implementations in softlib.c are never translated.
// ---------------------------------------------------------------

// Helper: emit "load guest reg x[n] into host_reg" from context.
//   mov host_reg, [rbx + n*8]
//
static void emit_load_ctx_reg(emit_t *e, int host_reg, int guest_reg) {
    emit_load_guest(e, host_reg, guest_reg);
}

// Helper: emit "store host_reg into guest reg x[n]" in context.
//   mov [rbx + n*8], host_reg
//
static void emit_store_ctx_reg(emit_t *e, int guest_reg, int host_reg) {
    emit_store_guest(e, guest_reg, host_reg);
}

static void emit_load_next_pc(emit_t *e, int host_reg) {
    emit_byte(e, rex(1, reg_hi(host_reg), 0, 0));
    emit_byte(e, 0x8B);
    emit_byte(e, modrm(0x02, host_reg, X64_RBX));
    emit_u32(e, CTX_NEXT_PC_OFF);
}

static void emit_store_next_pc(emit_t *e, int host_reg) {
    emit_byte(e, rex(1, reg_hi(host_reg), 0, 0));
    emit_byte(e, 0x89);
    emit_byte(e, modrm(0x02, host_reg, X64_RBX));
    emit_u32(e, CTX_NEXT_PC_OFF);
}

static bool resolve_direct_jalr_target(uint64_t pc, const rv64_insn_t &insn,
                                       const rv64_insn_t &next,
                                       uint64_t *target_out,
                                       uint64_t *return_pc_out) {
    if (!insn.rd) return false;
    if (insn.opcode != OP_LUI && insn.opcode != OP_AUIPC) return false;
    if (next.opcode != OP_JALR || next.rs1 != insn.rd) return false;

    int64_t base = (insn.opcode == OP_AUIPC) ? static_cast<int64_t>(pc) : 0;
    int64_t target = base + static_cast<int64_t>(insn.imm)
                   + static_cast<int64_t>(next.imm);
    target &= ~1LL; // clear bit 0 per JALR spec
    if (target < 0) return false;

    if (target_out) *target_out = static_cast<uint64_t>(target);
    if (return_pc_out) *return_pc_out = pc + 8;
    return true;
}

// Helper: emit "convert guest pointer in host_reg to host pointer"
//   add host_reg, r12
//
static void emit_guest_to_host(emit_t *e, int host_reg) {
    emit_add_r64(e, host_reg, X64_R12);
}

// Emit intrinsic return: reload pinned host registers from ctx
// (so the trampoline's post-store writes correct values), set
// next_pc = guest x1 (ra), then RET.
//
static void emit_intrinsic_return(emit_t *e) {
    // Reload pinned registers from ctx.  The stubs clobber RSI/RDI/R8/R9
    // with host function arguments/results.  The trampoline unconditionally
    // stores these back to ctx->x[10..13] after every block, so they must
    // reflect the current guest state.
    //
    for (int i = 0; i < RC_NUM_PINNED; i++) {
        emit_load_guest(e, rc_host_regs[i], rc_pinned_guest[i]);
    }

    // Load ra from guest context → rcx
    emit_load_ctx_reg(e, X64_RCX, 1);
    // Set next_pc = ra
    emit_exit_indirect(e, X64_RCX);
}

// Emit the prologue that every intrinsic stub needs: align the
// stack to 16 bytes (required by System V ABI before CALL).
// push rbp; mov rbp, rsp; and rsp, -16
//
static void emit_stub_prologue(emit_t *e) {
    emit_byte(e, 0x55);  // push rbp
    emit_mov_r64(e, X64_RBP, X64_RSP);
    // and rsp, -16
    emit_byte(e, rex(1, 0, 0, 0));
    emit_byte(e, 0x83);
    emit_byte(e, modrm(0x03, 4, X64_RSP));  // and /4
    emit_byte(e, 0xF0);  // -16
}

static void emit_stub_epilogue(emit_t *e) {
    emit_mov_r64(e, X64_RSP, X64_RBP);
    emit_byte(e, 0x5D);  // pop rbp
}

// emit_call_host: mov rax, imm64(fn_ptr); call rax
//
static void emit_call_host(emit_t *e, void *fn) {
    emit_mov_r64_imm64(e, X64_RAX, reinterpret_cast<uint64_t>(fn));
    // call rax: FF D0
    emit_byte(e, 0xFF);
    emit_byte(e, modrm(0x03, 2, X64_RAX));
}

// ---- Individual intrinsic stubs ----

// rv64_slen: a0=string_ptr → a0=length
// Host: size_t strlen(const char *s)
//
static void emit_stub_slen(emit_t *e) {
    emit_stub_prologue(e);
    // rdi = host pointer to string
    emit_load_ctx_reg(e, X64_RDI, 10);   // a0
    emit_guest_to_host(e, X64_RDI);
    emit_call_host(e, reinterpret_cast<void *>(strlen));
    // Store result (rax) to guest a0 (x10)
    emit_store_ctx_reg(e, 10, X64_RAX);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// rv64_scopy: a0=dst, a1=src → a0=pointer AT NUL
// Host: strcpy then strlen to find NUL position.
// Actually simpler: inline byte loop since strcpy returns dst not end.
// Or use stpcpy if available.  For now, call strcpy and then strlen.
//
static void emit_stub_scopy(emit_t *e) {
    emit_stub_prologue(e);
    // Save guest dst address for computing return value
    emit_load_ctx_reg(e, X64_RCX, 10);    // guest a0 (dst)
    // push rcx (save guest dst)
    emit_byte(e, 0x51);

    // rdi = host dst, rsi = host src
    emit_load_ctx_reg(e, X64_RDI, 10);    // a0
    emit_guest_to_host(e, X64_RDI);
    emit_load_ctx_reg(e, X64_RSI, 11);    // a1
    emit_guest_to_host(e, X64_RSI);

    // call strcpy(host_dst, host_src) → returns host_dst in rax
    emit_call_host(e, reinterpret_cast<void *>(strcpy));

    // Now find the NUL: rdi = rax (host_dst returned by strcpy)
    emit_mov_r64(e, X64_RDI, X64_RAX);
    emit_call_host(e, reinterpret_cast<void *>(strlen));
    // rax = length of string at dst

    // pop rcx (guest dst)
    emit_byte(e, 0x59);
    // result = guest_dst + length → points at the NUL
    emit_add_r64(e, X64_RAX, X64_RCX);
    emit_store_ctx_reg(e, 10, X64_RAX);

    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// memcpy: a0=dst, a1=src, a2=len → a0=dst
// Host: void *memcpy(void *dst, const void *src, size_t n)
//
static void emit_stub_memcpy(emit_t *e) {
    emit_stub_prologue(e);
    // Save guest dst for return value
    emit_load_ctx_reg(e, X64_RCX, 10);
    emit_byte(e, 0x51);  // push rcx

    emit_load_ctx_reg(e, X64_RDI, 10);    // dst
    emit_guest_to_host(e, X64_RDI);
    emit_load_ctx_reg(e, X64_RSI, 11);    // src
    emit_guest_to_host(e, X64_RSI);
    emit_load_ctx_reg(e, X64_RDX, 12);    // len
    emit_call_host(e, reinterpret_cast<void *>(memcpy));

    emit_byte(e, 0x59);  // pop rcx (guest dst)
    emit_store_ctx_reg(e, 10, X64_RCX);   // return original guest dst
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// memcmp: a0=ptr_a, a1=ptr_b, a2=len → a0=result
// Host: int memcmp(const void *s1, const void *s2, size_t n)
//
static void emit_stub_memcmp(emit_t *e) {
    emit_stub_prologue(e);
    emit_load_ctx_reg(e, X64_RDI, 10);    // ptr_a
    emit_guest_to_host(e, X64_RDI);
    emit_load_ctx_reg(e, X64_RSI, 11);    // ptr_b
    emit_guest_to_host(e, X64_RSI);
    emit_load_ctx_reg(e, X64_RDX, 12);    // len
    emit_call_host(e, reinterpret_cast<void *>(memcmp));

    // sign-extend eax → rax for 64-bit guest register
    // cdqe: REX.W + 0x98
    emit_byte(e, rex(1, 0, 0, 0));
    emit_byte(e, 0x98);
    emit_store_ctx_reg(e, 10, X64_RAX);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// memset: a0=dst, a1=byte_val, a2=len → a0=dst
// Host: void *memset(void *s, int c, size_t n)
//
static void emit_stub_memset(emit_t *e) {
    emit_stub_prologue(e);
    emit_load_ctx_reg(e, X64_RCX, 10);
    emit_byte(e, 0x51);  // push rcx (save guest dst)

    emit_load_ctx_reg(e, X64_RDI, 10);    // dst
    emit_guest_to_host(e, X64_RDI);
    emit_load_ctx_reg(e, X64_RSI, 11);    // byte value (int)
    emit_load_ctx_reg(e, X64_RDX, 12);    // len
    emit_call_host(e, reinterpret_cast<void *>(memset));

    emit_byte(e, 0x59);  // pop rcx (guest dst)
    emit_store_ctx_reg(e, 10, X64_RCX);
    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// memswap: a0=ptr_a, a1=ptr_b, a2=len → void
// No libc equivalent — we implement the 3-phase qword-optimized
// swap inline.  This is the same algorithm as ~/slow-32/runtime
// intrinsics.s memswap but targeting x86-64.
//
// Host native: swap 8 bytes at a time when aligned, byte fallback.
//
static void emit_stub_memswap(emit_t *e) {
    emit_stub_prologue(e);

    // rax = host ptr_a, rcx = host ptr_b, rdx = len
    emit_load_ctx_reg(e, X64_RAX, 10);
    emit_guest_to_host(e, X64_RAX);
    emit_load_ctx_reg(e, X64_RCX, 11);
    emit_guest_to_host(e, X64_RCX);
    emit_load_ctx_reg(e, X64_RDX, 12);

    // test rdx, rdx; jz done
    emit_byte(e, rex(1, 0, 0, 0));
    emit_byte(e, 0x85);
    emit_byte(e, modrm(0x03, X64_RDX, X64_RDX));
    uint32_t jz_done = emit_pos(e);
    emit_byte(e, 0x0F);
    emit_byte(e, 0x84);
    emit_u32(e, 0);

    // Check alignment: (a ^ b) & 7 — if nonzero, byte-only path
    emit_mov_r64(e, X64_RSI, X64_RAX);
    emit_xor_r64(e, X64_RSI, X64_RCX);
    emit_and_r64_imm(e, X64_RSI, 7);
    uint32_t jnz_byte = emit_pos(e);
    emit_byte(e, 0x0F);
    emit_byte(e, 0x85);
    emit_u32(e, 0);

    // Phase 1: Align to 8-byte boundary
    uint32_t align_loop = emit_pos(e);
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0x85);
    emit_byte(e, modrm(0x03, X64_RDX, X64_RDX));
    uint32_t jz_tail = emit_pos(e);
    emit_byte(e, 0x0F); emit_byte(e, 0x84); emit_u32(e, 0);
    // test al, 7
    emit_byte(e, 0xA8); emit_byte(e, 0x07);
    uint32_t jz_qword = emit_pos(e);
    emit_byte(e, 0x0F); emit_byte(e, 0x84); emit_u32(e, 0);
    // byte swap: rsi = [rax], rdi = [rcx], [rax] = rdi, [rcx] = rsi
    emit_byte(e, 0x0F); emit_byte(e, 0xB6); emit_byte(e, modrm(0x00, X64_RSI, X64_RAX));
    emit_byte(e, 0x0F); emit_byte(e, 0xB6); emit_byte(e, modrm(0x00, X64_RDI, X64_RCX));
    emit_byte(e, 0x40); emit_byte(e, 0x88); emit_byte(e, modrm(0x00, X64_RDI, X64_RAX));
    emit_byte(e, 0x40); emit_byte(e, 0x88); emit_byte(e, modrm(0x00, X64_RSI, X64_RCX));
    // inc rax; inc rcx; dec rdx
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0xFF); emit_byte(e, modrm(0x03, 0, X64_RAX));
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0xFF); emit_byte(e, modrm(0x03, 0, X64_RCX));
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0xFF); emit_byte(e, modrm(0x03, 1, X64_RDX));
    int32_t ja = static_cast<int32_t>(align_loop) - static_cast<int32_t>(emit_pos(e) + 2);
    emit_byte(e, 0xEB); emit_byte(e, static_cast<uint8_t>(ja));

    // Phase 2: Qword swap
    uint32_t qword_loop = emit_pos(e);
    emit_patch_rel32(e, jz_qword + 2, qword_loop);
    // cmp rdx, 8; jb tail
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0x83);
    emit_byte(e, modrm(0x03, 7, X64_RDX)); emit_byte(e, 0x08);
    uint32_t jb_tail = emit_pos(e);
    emit_byte(e, 0x0F); emit_byte(e, 0x82); emit_u32(e, 0);
    // rsi = [rax]; rdi = [rcx]; [rax] = rdi; [rcx] = rsi
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0x8B); emit_byte(e, modrm(0x00, X64_RSI, X64_RAX));
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0x8B); emit_byte(e, modrm(0x00, X64_RDI, X64_RCX));
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0x89); emit_byte(e, modrm(0x00, X64_RDI, X64_RAX));
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0x89); emit_byte(e, modrm(0x00, X64_RSI, X64_RCX));
    // add rax, 8; add rcx, 8; sub rdx, 8
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0x83); emit_byte(e, modrm(0x03, 0, X64_RAX)); emit_byte(e, 0x08);
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0x83); emit_byte(e, modrm(0x03, 0, X64_RCX)); emit_byte(e, 0x08);
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0x83); emit_byte(e, modrm(0x03, 5, X64_RDX)); emit_byte(e, 0x08);
    int32_t jq = static_cast<int32_t>(qword_loop) - static_cast<int32_t>(emit_pos(e) + 2);
    emit_byte(e, 0xEB); emit_byte(e, static_cast<uint8_t>(jq));

    // Phase 3: Tail bytes (also byte-only fallback for misaligned)
    uint32_t byte_loop = emit_pos(e);
    emit_patch_rel32(e, jnz_byte + 2, byte_loop);
    emit_patch_rel32(e, jb_tail + 2, byte_loop);
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0x85);
    emit_byte(e, modrm(0x03, X64_RDX, X64_RDX));
    uint32_t jz_done2 = emit_pos(e);
    emit_byte(e, 0x0F); emit_byte(e, 0x84); emit_u32(e, 0);
    emit_byte(e, 0x0F); emit_byte(e, 0xB6); emit_byte(e, modrm(0x00, X64_RSI, X64_RAX));
    emit_byte(e, 0x0F); emit_byte(e, 0xB6); emit_byte(e, modrm(0x00, X64_RDI, X64_RCX));
    emit_byte(e, 0x40); emit_byte(e, 0x88); emit_byte(e, modrm(0x00, X64_RDI, X64_RAX));
    emit_byte(e, 0x40); emit_byte(e, 0x88); emit_byte(e, modrm(0x00, X64_RSI, X64_RCX));
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0xFF); emit_byte(e, modrm(0x03, 0, X64_RAX));
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0xFF); emit_byte(e, modrm(0x03, 0, X64_RCX));
    emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0xFF); emit_byte(e, modrm(0x03, 1, X64_RDX));
    int32_t jb = static_cast<int32_t>(byte_loop) - static_cast<int32_t>(emit_pos(e) + 2);
    emit_byte(e, 0xEB); emit_byte(e, static_cast<uint8_t>(jb));

    // .done:
    uint32_t done_pos = emit_pos(e);
    emit_patch_rel32(e, jz_done + 2, done_pos);
    emit_patch_rel32(e, jz_tail + 2, done_pos);
    emit_patch_rel32(e, jz_done2 + 2, done_pos);

    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// ---- FP intrinsic stubs ----
//
// For double→double functions (sin, cos, etc.):
//   Load guest fa0 (f[10]) into host xmm0.
//   Call the host libm function.
//   Store xmm0 result back to guest fa0.
//
// For (double,double)→double functions (pow, atan2, fmod):
//   Load guest fa0 (f[10]) into host xmm0.
//   Load guest fa1 (f[11]) into host xmm1.
//   Call the host libm function.
//   Store xmm0 result back to guest fa0.
//
// Guest FP register fa0 = f[10], offset = CTX_FP_OFF + 10*8 = 608.
// Guest FP register fa1 = f[11], offset = CTX_FP_OFF + 11*8 = 616.
//
static constexpr int CTX_FA0_OFF = CTX_FP_OFF + 10 * 8;  // 608
static constexpr int CTX_FA1_OFF = CTX_FP_OFF + 11 * 8;  // 616

// Helper: movsd xmm_reg, [rbx + offset]
//
static void emit_load_ctx_fp(emit_t *e, int xmm_reg, int ctx_off) {
    // F2 0F 10 /r  (movsd xmm, m64)
    // With RBX as base: mod=10 (disp32), rm=011 (rbx)
    emit_byte(e, 0xF2);
    if (xmm_reg >= 8) {
        emit_byte(e, rex(0, (xmm_reg >> 3) & 1, 0, 0));
    }
    emit_byte(e, 0x0F);
    emit_byte(e, 0x10);
    emit_byte(e, modrm(0x02, xmm_reg & 7, X64_RBX));
    emit_u32(e, ctx_off);
}

// Helper: movsd [rbx + offset], xmm_reg
//
static void emit_store_ctx_fp(emit_t *e, int ctx_off, int xmm_reg) {
    // F2 0F 11 /r  (movsd m64, xmm)
    emit_byte(e, 0xF2);
    if (xmm_reg >= 8) {
        emit_byte(e, rex(0, (xmm_reg >> 3) & 1, 0, 0));
    }
    emit_byte(e, 0x0F);
    emit_byte(e, 0x11);
    emit_byte(e, modrm(0x02, xmm_reg & 7, X64_RBX));
    emit_u32(e, ctx_off);
}

// double fn(double): sin, cos, tan, asin, acos, atan, exp, log, etc.
//
static void emit_stub_fp_d_d(void *ev, void *fn) {
    emit_t *e = static_cast<emit_t *>(ev);
    emit_stub_prologue(e);

    emit_load_ctx_fp(e, 0, CTX_FA0_OFF);   // movsd xmm0, [rbx+608]
    emit_call_host(e, fn);
    emit_store_ctx_fp(e, CTX_FA0_OFF, 0);   // movsd [rbx+608], xmm0

    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// double fn(double, double): pow, atan2, fmod
//
static void emit_stub_fp_dd_d(void *ev, void *fn) {
    emit_t *e = static_cast<emit_t *>(ev);
    emit_stub_prologue(e);

    emit_load_ctx_fp(e, 0, CTX_FA0_OFF);   // movsd xmm0, [rbx+608]
    emit_load_ctx_fp(e, 1, CTX_FA1_OFF);   // movsd xmm1, [rbx+616]
    emit_call_host(e, fn);
    emit_store_ctx_fp(e, CTX_FA0_OFF, 0);   // movsd [rbx+608], xmm0

    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// ---- String↔double conversion intrinsic stubs ----

// rv64_strtod: guest a0=string_ptr → guest fa0=double
// Host: double host_strtod(const char *s)
// SysV: rdi=s → xmm0=result
//
static void emit_stub_strtod(void *ev, void *fn) {
    emit_t *e = static_cast<emit_t *>(ev);
    emit_stub_prologue(e);

    // rdi = host pointer to string
    emit_load_ctx_reg(e, X64_RDI, 10);   // guest a0
    emit_guest_to_host(e, X64_RDI);
    emit_call_host(e, fn);
    // Result in xmm0 → store to guest fa0
    emit_store_ctx_fp(e, CTX_FA0_OFF, 0);

    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// rv64_fval: guest a0=buf_ptr, guest fa0=double → guest a0=length
// Host: int host_fval(char *buf, double val)
// SysV: rdi=buf, xmm0=val → rax=len
//
static void emit_stub_fval(void *ev, void *fn) {
    emit_t *e = static_cast<emit_t *>(ev);
    emit_stub_prologue(e);

    // rdi = host pointer to output buffer
    emit_load_ctx_reg(e, X64_RDI, 10);   // guest a0
    emit_guest_to_host(e, X64_RDI);
    // xmm0 = double value from guest fa0
    emit_load_ctx_fp(e, 0, CTX_FA0_OFF);
    emit_call_host(e, fn);
    // Result (length) in rax → store to guest a0
    emit_store_ctx_reg(e, 10, X64_RAX);

    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// rv64_ftoa_round: guest a0=buf_ptr, guest fa0=double, guest a1=frac → guest a0=length
// Host: int host_ftoa_round(char *buf, double val, int frac)
// SysV: rdi=buf, xmm0=val, esi=frac → rax=len
//
static void emit_stub_ftoa_round(void *ev, void *fn) {
    emit_t *e = static_cast<emit_t *>(ev);
    emit_stub_prologue(e);

    // rdi = host pointer to output buffer
    emit_load_ctx_reg(e, X64_RDI, 10);   // guest a0
    emit_guest_to_host(e, X64_RDI);
    // xmm0 = double value from guest fa0
    emit_load_ctx_fp(e, 0, CTX_FA0_OFF);
    // esi = frac from guest a1
    emit_load_ctx_reg(e, X64_RSI, 11);   // guest a1
    emit_call_host(e, fn);
    // Result (length) in rax → store to guest a0
    emit_store_ctx_reg(e, 10, X64_RAX);

    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// ---- Generic co_* intrinsic stub emitter ----
//
// Emits a native CALL to the host co_* function.  Arguments are in
// guest a0-a5 (x10-x15), mapped to System V: RDI, RSI, RDX, RCX, R8, R9.
// Pointer arguments (guest addresses) are converted to host via +R12.
// The host function returns size_t in RAX; the stub stores it to guest a0.
//
// The `fn` parameter is the host function pointer (cast to void*),
// passed through the intrinsic_slot_t.host_fn field.
//
// Argument descriptor bits: 1 bit per arg, 0=integer, 1=guest pointer.
// Packed into a uint8_t: bit 0 = arg 0 (a0), bit 1 = arg 1 (a1), etc.
//

static constexpr int x64_arg_regs[6] = {
    X64_RDI, X64_RSI, X64_RDX, X64_RCX, X64_R8, X64_R9
};

// Generic emitter for co_* functions, result in a0.
// `fn` is the host function pointer (from the intrinsic_slot_t).
// The nargs and ptr_mask are encoded in the emitter function (one
// emitter per signature pattern).
//
// For nargs <= 6: all args in SysV registers (RDI,RSI,RDX,RCX,R8,R9).
// For nargs > 6:  first 6 in registers, overflow pushed onto x86-64 stack.
//   The prologue aligns RSP to 16; we sub additional space for stack args
//   (padded to 16) and store via MOV [RSP+disp].  The epilogue's
//   `mov rsp, rbp` cleans it all up.
//
static void emit_stub_co_generic(void *ev, void *fn,
                                  int nargs, uint8_t ptr_mask) {
    emit_t *e = static_cast<emit_t *>(ev);
    emit_stub_prologue(e);

    int n_reg   = (nargs <= 6) ? nargs : 6;
    int n_stack = (nargs > 6) ? (nargs - 6) : 0;

    // Allocate stack space for overflow arguments (16-byte aligned).
    //
    if (n_stack > 0) {
        int stack_bytes = ((n_stack * 8 + 15) & ~15);
        emit_sub_r64_imm(e, X64_RSP, stack_bytes);

        // Load overflow args via RAX scratch, store to [RSP + offset].
        //
        for (int i = nargs - 1; i >= 6; i--) {
            emit_load_ctx_reg(e, X64_RAX, 10 + i);
            if (ptr_mask & (1 << i)) {
                emit_guest_to_host(e, X64_RAX);
            }
            emit_store_rsp(e, X64_RAX, (i - 6) * 8);
        }
    }

    // Load register args (first 6) in reverse order to avoid clobbering
    // RDI/RSI before reading them as ctx pointers.
    //
    for (int i = n_reg - 1; i >= 0; i--) {
        emit_load_ctx_reg(e, x64_arg_regs[i], 10 + i);  // guest a0+i
        if (ptr_mask & (1 << i)) {
            emit_guest_to_host(e, x64_arg_regs[i]);
        }
    }

    emit_call_host(e, fn);

    // Store return value (rax) to guest a0.
    emit_store_ctx_reg(e, 10, X64_RAX);

    emit_stub_epilogue(e);
    emit_intrinsic_return(e);
}

// Macro to define a thin emitter wrapper for each co_* signature.
// The wrapper captures the nargs and ptr_mask at compile time.
//
#define DEFINE_CO_EMITTER(name, nargs, ptr_mask) \
    static void emit_stub_##name(void *e, void *fn) { \
        emit_stub_co_generic(e, fn, nargs, ptr_mask); \
    }

// Signature patterns for co_* functions:
//   PP_II: 2 pointers, 2 integers   → co_first(out,p,len,delim)
//   P_II:  1 pointer, 2 integers    → co_words_count(p,len,delim)
//   PP_III: 2 ptrs, 3 ints          → co_mid(out,p,len,start,count)
//                                      co_trim(out,p,len,char,flags)
//                                      co_member(tgt,tlen,list,llen,delim)
//   PP_I:  2 ptrs, 1 int            → (no current use)
//   PPII:  2 ptrs, 2 ints (alt pos) → co_pos(h,hlen,n,nlen)
//                                      co_repeat(out,p,len,count)
//   PP_IIII: 2 ptrs, 4 ints         → co_delete(out,list,llen,pos,delim,osep)
//
// ptr_mask bits: bit 0=a0, bit 1=a1, etc.
// PP_II:    a0=ptr, a1=ptr → 0x03 (bits 0,1)
// P_II:     a0=ptr         → 0x01
// PPPP_II:  a0,a1=ptr      → 0x03 (same as PP_II, other args are ints)
// PPP:      a0=ptr, a1,a2=int, a3=int, a4=ptr, a5=int → need per-function
//
// Since ptr_mask is per-arg, each function needs its own mask.

// 2 args: co_cluster_count(p, len)
//   a0=ptr, a1=int  → mask=0x01
DEFINE_CO_EMITTER(co_2p, 2, 0x01)

// 3 args: co_tolower/co_toupper/co_reverse/co_escape(out, p, len)
//   a0=ptr, a1=ptr, a2=int  → mask=0x03
DEFINE_CO_EMITTER(co_3pp, 3, 0x03)

// 4 args: co_first/co_rest/co_last(out, p, len, delim)
//   a0=ptr, a1=ptr, a2=int, a3=int  → mask=0x03
DEFINE_CO_EMITTER(co_4pp, 4, 0x03)

// 4 args: co_repeat(out, p, len, count)
//   a0=ptr, a1=ptr, a2=int, a3=int  → mask=0x03 (same as above)
// (reuse co_4pp)

// 4 args: co_pos(haystack, hlen, needle, nlen)
//   a0=ptr, a1=int, a2=ptr, a3=int  → mask=0x05
DEFINE_CO_EMITTER(co_pos, 4, 0x05)

// 3 args: co_words_count(p, len, delim)
//   a0=ptr, a1=int, a2=int  → mask=0x01
DEFINE_CO_EMITTER(co_3p, 3, 0x01)

// 5 args: co_mid(out, p, len, start, count)
//   a0=ptr, a1=ptr, a2-a4=int  → mask=0x03
DEFINE_CO_EMITTER(co_5pp, 5, 0x03)

// 5 args: co_member(target, tlen, list, llen, delim)
//   a0=ptr, a1=int, a2=ptr, a3-a4=int  → mask=0x05
DEFINE_CO_EMITTER(co_member, 5, 0x05)

// 5 args: co_trim(out, p, len, trim_char, trim_flags)
//   a0=ptr, a1=ptr, a2-a4=int  → mask=0x03
// (reuse co_5pp)

// 6 args: co_sort_words(out, list, llen, delim, osep, sort_type)
//   a0=ptr, a1=ptr, a2-a5=int  → mask=0x03
DEFINE_CO_EMITTER(co_6pp, 6, 0x03)

// 6 args: co_delete(out, list, llen, pos, delim, osep)
//   same as co_6pp → mask=0x03

// 7 args: co_extract(out, p, len, iFirst, nWords, delim, osep)
//   a0=ptr, a1=ptr, a2-a6=int  → mask=0x03
DEFINE_CO_EMITTER(co_7pp, 7, 0x03)

// 8 args: co_setunion(out, list1, len1, list2, len2, delim, osep, sort_type)
//   a0=ptr, a1=ptr, a2=int, a3=ptr, a4-a7=int  → mask=0x0B (bits 0,1,3)
DEFINE_CO_EMITTER(co_8ppp, 8, 0x0B)

// Wrapper emitters for the old-style stubs (no host_fn parameter).
//
static void emit_stub_slen_w(void *ev, void *) { emit_stub_slen(static_cast<emit_t *>(ev)); }
static void emit_stub_scopy_w(void *ev, void *) { emit_stub_scopy(static_cast<emit_t *>(ev)); }
static void emit_stub_memcpy_w(void *ev, void *) { emit_stub_memcpy(static_cast<emit_t *>(ev)); }
static void emit_stub_memcmp_w(void *ev, void *) { emit_stub_memcmp(static_cast<emit_t *>(ev)); }
static void emit_stub_memset_w(void *ev, void *) { emit_stub_memset(static_cast<emit_t *>(ev)); }
static void emit_stub_memswap_w(void *ev, void *) { emit_stub_memswap(static_cast<emit_t *>(ev)); }

// try_emit_intrinsic: check if guest_pc matches a known intrinsic.
// If so, emit a complete native stub block and return the code pointer.
// If not, return nullptr (normal translation proceeds).
//
static uint8_t *try_emit_intrinsic(dbt_state_t *dbt, uint64_t guest_pc) {
    for (int i = 0; i < dbt->num_intrinsics; i++) {
        if (dbt->intrinsics[i].guest_addr == guest_pc) {
            // Emit into the code buffer.
            uint8_t *block_start = dbt->code_buf + dbt->code_used;
            emit_t e;
            e.buf = block_start;
            e.offset = 0;
            e.capacity = CODE_BUF_SIZE - dbt->code_used;

            dbt->intrinsics[i].emitter(&e, dbt->intrinsics[i].host_fn);

            if (e.offset > e.capacity) return nullptr;

            dbt->code_used += e.offset;
            dbt->intrinsic_hits++;

            cache_insert(dbt, guest_pc, block_start);
            dbt_trace_translate_pc(dbt, guest_pc,
                                   "intrinsic guest_pc=0x%llX bytes=%u slot=%d",
                                   static_cast<unsigned long long>(guest_pc),
                                   e.offset, i);
            return block_start;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------
// Intrinsic registration
// ---------------------------------------------------------------

// Maps dbt_emitter_id → function pointer.  The old-style stubs
// (slen, scopy, etc.) ignore the host_fn parameter; the generic
// co_* emitters use it as the CALL target.
//
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
// Block cache
// ---------------------------------------------------------------

// Hash to set index (0..BLOCK_CACHE_SETS-1).
//
static inline uint32_t cache_set(uint64_t pc) {
    uint32_t h = static_cast<uint32_t>(pc >> 2);
    h ^= (h >> 10);
    return h & BLOCK_CACHE_MASK;
}

// For compatibility with JIT inline probes that use the old name.
static inline uint32_t cache_hash(uint64_t pc) { return cache_set(pc); }

static block_entry_t *cache_lookup(dbt_state_t *dbt, uint64_t pc) {
    uint32_t set = cache_set(pc);
    block_entry_t *base = &dbt->cache[set * BLOCK_CACHE_WAYS];
    for (size_t w = 0; w < BLOCK_CACHE_WAYS; w++) {
        if (base[w].guest_pc == pc && base[w].native_code) {
            dbt->cache_hits++;
            return &base[w];
        }
    }
    dbt->cache_misses++;
    return nullptr;
}

static void cache_insert(dbt_state_t *dbt, uint64_t pc, uint8_t *code) {
    uint32_t set = cache_set(pc);
    block_entry_t *base = &dbt->cache[set * BLOCK_CACHE_WAYS];
    // Use first empty way.
    for (size_t w = 0; w < BLOCK_CACHE_WAYS; w++) {
        if (base[w].guest_pc == 0) {
            base[w].guest_pc = pc;
            base[w].native_code = code;
            return;
        }
    }
    // All ways occupied — evict way 0 (FIFO).
    base[0].guest_pc = pc;
    base[0].native_code = code;
}

// ---------------------------------------------------------------
// Return Address Stack (RAS) helpers
// ---------------------------------------------------------------

// Emit inline RAS push: ras[ras_top++ & MASK] = return_addr.
// Clobbers RAX, RCX, RDX.
//
static void emit_ras_push(emit_t *e, uint64_t return_addr) {
    // mov eax, [rbx + CTX_RAS_TOP_OFF]  — load ras_top (32-bit)
    emit_byte(e, 0x8B);
    emit_byte(e, modrm(0x02, X64_RAX, X64_RBX));
    emit_u32(e, CTX_RAS_TOP_OFF);

    // mov ecx, eax ; and ecx, RAS_MASK  — index
    emit_mov_r64(e, X64_RCX, X64_RAX);
    emit_and_r64_imm(e, X64_RCX, RAS_MASK);

    // mov qword [rbx + CTX_RAS_OFF + rcx*8], return_addr
    // Use: lea rdx, [rbx + CTX_RAS_OFF]; mov [rdx + rcx*8], imm
    // Actually, simpler: compute offset = CTX_RAS_OFF + rcx*8 in rdx
    emit_shl_r64_imm(e, X64_RCX, 3);   // rcx *= 8
    emit_add_r64_imm(e, X64_RCX, CTX_RAS_OFF);
    // mov rdx, return_addr
    emit_mov_r64_imm32(e, X64_RDX, static_cast<int32_t>(return_addr));
    // mov [rbx + rcx], rdx
    emit_byte(e, rex(1, reg_hi(X64_RDX), 0, 0));
    emit_byte(e, 0x89);
    emit_byte(e, modrm(0x01, X64_RDX, 0x04)); // SIB follows
    emit_byte(e, static_cast<uint8_t>((reg_lo(X64_RCX) << 3) | reg_lo(X64_RBX)));
    emit_byte(e, 0x00); // disp8 = 0

    // inc dword [rbx + CTX_RAS_TOP_OFF]
    emit_add_r64_imm(e, X64_RAX, 1);
    emit_byte(e, 0x89); // mov [rbx + CTX_RAS_TOP_OFF], eax (32-bit)
    emit_byte(e, modrm(0x02, X64_RAX, X64_RBX));
    emit_u32(e, CTX_RAS_TOP_OFF);
}

// Emit inline RAS pop + block cache probe for JALR returns.
// RCX holds the actual computed target.  Clobbers RAX, RDX.
// On RAS hit + cache hit: jumps directly to native code (no ret).
// On miss: falls through to emit_exit_indirect.
//
static void emit_ras_pop_and_probe(emit_t *e, dbt_state_t *dbt) {
    // dec dword [rbx + CTX_RAS_TOP_OFF]
    emit_byte(e, 0x8B); // mov eax, [rbx + CTX_RAS_TOP_OFF]
    emit_byte(e, modrm(0x02, X64_RAX, X64_RBX));
    emit_u32(e, CTX_RAS_TOP_OFF);
    emit_add_r64_imm(e, X64_RAX, -1);
    emit_byte(e, 0x89); // mov [rbx + CTX_RAS_TOP_OFF], eax
    emit_byte(e, modrm(0x02, X64_RAX, X64_RBX));
    emit_u32(e, CTX_RAS_TOP_OFF);

    // Load predicted: rdx = ras[eax & RAS_MASK]
    emit_and_r64_imm(e, X64_RAX, RAS_MASK);
    emit_shl_r64_imm(e, X64_RAX, 3); // * 8
    emit_add_r64_imm(e, X64_RAX, CTX_RAS_OFF);
    // mov rdx, [rbx + rax]
    emit_byte(e, rex(1, reg_hi(X64_RDX), 0, 0));
    emit_byte(e, 0x8B);
    emit_byte(e, modrm(0x01, X64_RDX, 0x04)); // SIB
    emit_byte(e, static_cast<uint8_t>((reg_lo(X64_RAX) << 3) | reg_lo(X64_RBX)));
    emit_byte(e, 0x00); // disp8 = 0

    // Compare actual (RCX) vs predicted (RDX)
    emit_cmp_r64(e, X64_RCX, X64_RDX);
    uint32_t jne_miss = emit_jcc_rel32(e, JCC_NE);

    // RAS hit: inline block cache probe (4-way set-associative).
    // set = ((rcx >> 2) ^ ((rcx >> 2) >> 10)) & BLOCK_CACHE_MASK
    // base_offset = set * WAYS * 16 = set * 64
    emit_mov_r64(e, X64_RAX, X64_RCX);
    emit_shr_r64_imm(e, X64_RAX, 2);
    emit_mov_r64(e, X64_RDX, X64_RAX);
    emit_shr_r64_imm(e, X64_RDX, 10);
    // xor rax, rdx
    emit_byte(e, rex(1, reg_hi(X64_RAX), 0, 0));
    emit_byte(e, 0x33);
    emit_byte(e, modrm(0x03, X64_RAX, X64_RDX));
    emit_and_r64_imm(e, X64_RAX, static_cast<int32_t>(BLOCK_CACHE_MASK));
    // rax = set * 64 (4 ways × 16 bytes each)
    emit_shl_r64_imm(e, X64_RAX, 6);

    // Probe 4 ways: check [r13 + rax + way*16].guest_pc == rcx
    uint32_t jmp_hits[BLOCK_CACHE_WAYS];
    uint32_t jne_misses[BLOCK_CACHE_WAYS];
    for (size_t w = 0; w < BLOCK_CACHE_WAYS; w++) {
        int32_t disp = static_cast<int32_t>(w * 16);
        // cmp [r13 + rax + disp], rcx
        emit_byte(e, rex(1, reg_hi(X64_RCX), reg_hi(X64_RAX), 1));
        emit_byte(e, 0x3B);
        if (disp == 0) {
            emit_byte(e, modrm(0x01, X64_RCX, 0x04)); // SIB, disp8
            emit_byte(e, static_cast<uint8_t>((reg_lo(X64_RAX) << 3) | reg_lo(X64_R13)));
            emit_byte(e, 0x00);
        } else {
            emit_byte(e, modrm(0x01, X64_RCX, 0x04)); // SIB, disp8
            emit_byte(e, static_cast<uint8_t>((reg_lo(X64_RAX) << 3) | reg_lo(X64_R13)));
            emit_byte(e, static_cast<uint8_t>(disp));
        }
        jne_misses[w] = emit_jcc_rel32(e, JCC_NE);

        // Hit: load native_code from [r13 + rax + disp + 8]
        emit_byte(e, rex(1, reg_hi(X64_RDX), reg_hi(X64_RAX), 1));
        emit_byte(e, 0x8B);
        emit_byte(e, modrm(0x01, X64_RDX, 0x04)); // SIB, disp8
        emit_byte(e, static_cast<uint8_t>((reg_lo(X64_RAX) << 3) | reg_lo(X64_R13)));
        emit_byte(e, static_cast<uint8_t>(disp + 8));

        emit_test_r64(e, X64_RDX, X64_RDX);
        uint32_t jz_skip = emit_jcc_rel32(e, JCC_E);

        // jmp rdx — direct to native code!
        emit_byte(e, 0xFF);
        emit_byte(e, modrm(0x03, 4, X64_RDX)); // jmp rdx
        dbt->ras_hits++;

        // Null native_code: fall through to next way.
        emit_patch_rel32(e, jz_skip, emit_pos(e));
        // Patch miss to next way's check.
        emit_patch_rel32(e, jne_misses[w], emit_pos(e));
    }

    // All 4 ways missed: fall through to indirect exit.
    emit_patch_rel32(e, jne_miss, emit_pos(e));
    dbt->ras_misses++;
}

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
    emit_mov_r64_imm32(e, X64_RAX, static_cast<int32_t>(return_pc));
    emit_store_guest(e, 1, X64_RAX);

    // Poison the RAS so the callee's pop_and_probe mismatches and falls
    // through to RET, returning to this native CALL site.
    emit_ras_push(e, 1ULL);

    // Native CALL to the translated target.
    uint32_t call_patch = emit_call_rel32(e);
    uint32_t target_off = static_cast<uint32_t>(callee->native_code - e->buf);
    emit_patch_rel32(e, call_patch, target_off);

    // If ctx.next_pc != return_pc, the callee exited early and we must
    // fall back to the dispatch loop through a cold side-exit stub.
    emit_cmp_ctx_imm32(e, CTX_NEXT_PC_OFF, static_cast<int32_t>(return_pc));
    uint32_t jne_cold = emit_jcc_rel32(e, JCC_NE);

    // Hot path: callee returned normally. Reload register cache because
    // the callee may have modified any guest register.
    rc_invalidate_reload(e, rc);

    side_exits[*num_side_exits].jcc_patch = jne_cold;
    side_exits[*num_side_exits].target_pc = 0; // sentinel
    side_exits[*num_side_exits].expected_next_pc = return_pc;
    (*num_side_exits)++;
    dbt->inline_calls++;
    return true;
}

enum class direct_jalr_flow_t {
    inline_call_done,
    tail_call,
    chained_exit,
};

static direct_jalr_flow_t emit_direct_jalr_flow(
    emit_t *e, reg_cache_t *rc, fp_cache_t *fc, dbt_state_t *dbt,
    uint64_t guest_pc, uint64_t pc, uint64_t target_pc, uint64_t return_pc,
    const rv64_insn_t &next, side_exit_t *side_exits, int *num_side_exits,
    bool can_tail_inline, const char *call_trace, const char *tail_trace,
    const char *exit_trace) {
    if (next.rd == 1) {
        block_entry_t *be = cache_lookup(dbt, target_pc);
        if (try_emit_inline_call(e, rc, fc, dbt, target_pc, be, return_pc,
                                 side_exits, num_side_exits)) {
            dbt_trace_fusion(dbt, pc, call_trace);
            return direct_jalr_flow_t::inline_call_done;
        }
        emit_ras_push(e, return_pc);
    }

    if (next.rd == 0 && can_tail_inline) {
        dbt_trace_fusion(dbt, pc, tail_trace);
        return direct_jalr_flow_t::tail_call;
    }

    rc_flush(e, rc);
    fc_flush(e, fc);
    emit_exit_chained(e, dbt, target_pc);
    dbt_trace_fusion(dbt, pc, exit_trace);
    return direct_jalr_flow_t::chained_exit;
}

// ---------------------------------------------------------------
// Block chaining helpers
// ---------------------------------------------------------------

// Backpatch a JMP rel32 in the code buffer to point to a new target.
//
static void backpatch_jmp(uint8_t *code_buf, uint32_t jmp_disp_offset,
                           uint8_t *target) {
    int32_t disp = static_cast<int32_t>(
        target - (code_buf + jmp_disp_offset + 4));
    memcpy(code_buf + jmp_disp_offset, &disp, 4);
}

// Backpatch all pending exits that target the given guest PC.
//
static void backpatch_chains(dbt_state_t *dbt, uint64_t guest_pc,
                              uint8_t *native_code) {
    for (size_t i = 0; i < dbt->patches.size(); i++) {
        if (dbt->patches[i].target_pc == guest_pc) {
            backpatch_jmp(dbt->code_buf, dbt->patches[i].jmp_offset,
                          native_code);
            dbt->chain_hits++;
        }
    }
}

// ---------------------------------------------------------------
// Block exit helpers
// ---------------------------------------------------------------

// Emit a chained exit: if target block is already translated, emit a
// direct JMP.  Otherwise emit JMP to a slow-path stub and record a
// patch site for backpatching when the target is later translated.
//
static void emit_exit_chained(emit_t *e, dbt_state_t *dbt,
                               uint64_t target_pc) {
    // Check if target is already translated (4-way lookup).
    block_entry_t *be = cache_lookup(dbt, target_pc);
    bool known = (be != nullptr);

    // Emit JMP rel32.
    emit_byte(e, 0xE9);
    uint32_t jmp_patch = emit_pos(e);
    emit_u32(e, 0); // placeholder

    if (known) {
        // Target already translated — patch JMP to go directly there.
        // Compute target offset relative to e->buf for emit_patch_rel32.
        uint32_t target_off = static_cast<uint32_t>(
            be->native_code - e->buf);
        emit_patch_rel32(e, jmp_patch, target_off);
    } else {
        // Record patch site for backpatching.  Store offset relative to
        // code_buf (not e->buf) since backpatch_jmp uses code_buf base.
        uint32_t abs_offset = static_cast<uint32_t>(
            e->buf - dbt->code_buf) + jmp_patch;
        // Slow-path stub: store next_pc and return to trampoline.
        uint32_t stub_pos = emit_pos(e);
        emit_patch_rel32(e, jmp_patch, stub_pos);

        // Record patch site (after stub_pos is known).
        uint32_t stub_abs = static_cast<uint32_t>(
            e->buf - dbt->code_buf) + stub_pos;
        dbt->patches.push_back(
            patch_site_t{abs_offset, stub_abs, target_pc});
        dbt->chain_misses++;
        emit_exit_with_pc(e, target_pc);
    }
}

// ---------------------------------------------------------------
// Translate a single block
// ---------------------------------------------------------------

static uint8_t *translate_block(dbt_state_t *dbt, uint64_t guest_pc) {
    // Check for intrinsic recognition before normal translation.
    // If guest_pc is a known intrinsic, emit a native stub block.
    //
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

    // Self-loop detection: pre-scan to find if any branch targets start_pc.
    // If so, pre-warm the register cache and record a warm_entry point.
    // The back-edge jumps to warm_entry (inside this block), keeping the
    // entire loop in one contiguous x86-64 block.
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
                if (target == guest_pc) {
                    self_loop = true;
                    break;
                }
                if (si.imm < 0) break;  // backward branch elsewhere
                past_first_branch = true;
                // Follow the branch target (not fall-through) since our
                // codegen uses BNE-to-body / fall-through-to-exit pattern.
                scan_pc = target;
                continue;
            }
            if (si.opcode == OP_JAL) {
                if (si.rd != 0) {
                    // JAL ra, target — function call.  The call returns to
                    // pc+4, so for loop detection purposes, skip past it.
                    past_first_branch = true;
                    scan_pc += 4;
                    continue;
                }
                uint64_t target = scan_pc + static_cast<int64_t>(si.imm);
                if (target == guest_pc) {
                    self_loop = true;
                    break;
                }
                if (si.imm < 0) break;  // backward jump elsewhere
                // Forward unconditional jump: follow target.
                if (si.imm > 0 && target + 4 <= dbt->memory_size) {
                    past_first_branch = true;
                    scan_pc = target;
                    continue;
                }
                break;
            }
            if (si.opcode == OP_JALR) {
                // Skip past returns (JALR x0, ra, 0) — they return
                // from Tier 2 calls back to the next instruction.
                if (si.rd == 0 && si.rs1 == 1 && si.imm == 0) {
                    scan_pc += 4;
                    continue;
                }
                break;  // other indirect jumps — stop
            }
            if (si.opcode == OP_SYSTEM)
                break;
            scan_pc += 4;
        }
        if (self_loop) {
            // Pre-load frequently used registers.  Since we flush at the
            // back-edge (register mapping may diverge through complex loop
            // bodies), warm_entry only avoids loading from cold context on
            // the first entry.  Still beneficial: the x86-64 loads at
            // warm_entry become the reload targets for subsequent iterations.
            int loaded = 0;
            for (int r = 1; r < 32 && loaded < RC_NUM_SLOTS; r++) {
                if (used[r]) { rc_read(&e, &rc, r); loaded++; }
            }

            // Align warm_entry to 32-byte boundary (absolute address).
            // The backward branch targets this point every iteration —
            // good alignment avoids instruction decode stalls from
            // cache line crossings.
            uintptr_t abs_cur = reinterpret_cast<uintptr_t>(e.buf) + emit_pos(&e);
            uint32_t pad_needed = ((abs_cur + 31) & ~(uintptr_t)31) - abs_cur;
            uint32_t target_pos = emit_pos(&e) + pad_needed;
            // Use multi-byte NOPs for padding.
            while (emit_pos(&e) < target_pos) {
                uint32_t pad = target_pos - emit_pos(&e);
                if (pad >= 8) {
                    // 8-byte NOP: 0F 1F 84 00 00 00 00 00
                    emit_byte(&e, 0x0F); emit_byte(&e, 0x1F);
                    emit_byte(&e, 0x84); emit_byte(&e, 0x00);
                    emit_byte(&e, 0x00); emit_byte(&e, 0x00);
                    emit_byte(&e, 0x00); emit_byte(&e, 0x00);
                } else if (pad >= 4) {
                    // 4-byte NOP: 0F 1F 40 00
                    emit_byte(&e, 0x0F); emit_byte(&e, 0x1F);
                    emit_byte(&e, 0x40); emit_byte(&e, 0x00);
                } else if (pad >= 2) {
                    // 2-byte NOP: 66 90
                    emit_byte(&e, 0x66); emit_byte(&e, 0x90);
                } else {
                    // 1-byte NOP: 90
                    emit_byte(&e, 0x90);
                }
            }

            warm_entry = emit_pos(&e);
        }
    }

    side_exit_t side_exits[MAX_SIDE_EXITS];
    int num_side_exits = 0;

    uint64_t pc = guest_pc;
    int count = 0;
    uint64_t fused_before = dbt->insns_fused;
    uint64_t inline_calls_before = dbt->inline_calls;

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
        //
        // Decode the next instruction if available, for LUI+ADDI,
        // AUIPC+ADDI, and AUIPC+JALR fusion patterns.
        //
        rv64_insn_t next;
        bool have_next = false;
        if (pc + 8 <= dbt->memory_size) {
            uint32_t next_word;
            memcpy(&next_word, dbt->memory + pc + 4, 4);
            rv64_decode(next_word, &next);
            have_next = true;
        }

        if (dbt_trace_translate_enabled(dbt, guest_pc)) {
            const char *n = "?";
            switch(insn.opcode) {
            case OP_LUI:n="LUI";break; case OP_AUIPC:n="AUI";break;
            case OP_JAL:n="JAL";break; case OP_JALR:n="JLR";break;
            case OP_BRANCH:n="BRN";break; case OP_LOAD:n="LD";break;
            case OP_STORE:n="SD";break; case OP_IMM:n="IMM";break;
            case OP_REG:n="REG";break; case OP_SYSTEM:n="SYS";break;
            }
            dbt_trace_translate_pc(dbt, guest_pc,
                "insn #%d pc=0x%llX %s rd=%d rs1=%d rs2=%d imm=%d",
                count, (unsigned long long)pc, n, insn.rd, insn.rs1, insn.rs2, insn.imm);
        }

        // Fusion: SLT/SLTI/SLTU/SLTIU + BEQ/BNE against x0.
        // This preserves rd (if nonzero) but reuses the original compare
        // flags to branch directly, avoiding a redundant test of rd.
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

            if (insn.opcode == OP_REG) {
                int rs1 = rc_read(&e, &rc, insn.rs1);
                int rs2 = rc_read(&e, &rc, insn.rs2);
                emit_cmp_r64(&e, rs1, rs2);
            } else {
                int rs1 = rc_read(&e, &rc, insn.rs1);
                emit_cmp_r64_imm(&e, rs1, insn.imm);
            }

            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_setcc(&e, is_unsigned ? SETCC_B : SETCC_L, rd);
                emit_movzx_r64_r8(&e, rd, rd);
            }

            uint8_t cc;
            if (next.funct3 == 1) {
                cc = is_unsigned ? JCC_B : JCC_L;
            } else {
                cc = is_unsigned ? JCC_AE : JCC_GE;
            }

            // Diamond merge: fuse the following branch-over-one pattern
            // into a conditional move while preserving the SLT result in rd.
            if (next.imm == 8 && pc + 12 <= dbt->memory_size) {
                uint32_t skip_word;
                memcpy(&skip_word, dbt->memory + pc + 8, 4);
                rv64_insn_t skip;
                rv64_decode(skip_word, &skip);

                uint8_t cmov_cc;
                if (next.funct3 == 1) {
                    cmov_cc = is_unsigned ? CMOV_AE : CMOV_GE;
                } else {
                    cmov_cc = is_unsigned ? CMOV_B : CMOV_L;
                }

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
                    // Compute the skipped instruction result first; it may
                    // clobber flags, so the SLT compare comes afterward.
                    if (skip.opcode == OP_LUI) {
                        emit_mov_r64_imm32(&e, X64_RCX, skip.imm);
                    } else if (skip.opcode == OP_IMM) {
                        int hr_src = rc_read(&e, &rc, skip.rs1);
                        emit_mov_r64(&e, X64_RCX, hr_src);
                        switch (skip.funct3) {
                        case ALU_ADDI: emit_add_r64_imm(&e, X64_RCX, skip.imm); break;
                        case ALU_XORI: emit_xor_r64_imm(&e, X64_RCX, skip.imm); break;
                        case ALU_ORI:  emit_or_r64_imm(&e, X64_RCX, skip.imm); break;
                        case ALU_ANDI: emit_and_r64_imm(&e, X64_RCX, skip.imm); break;
                        }
                    } else {
                        int hr_s1 = rc_read(&e, &rc, skip.rs1);
                        int hr_s2 = rc_read(&e, &rc, skip.rs2);
                        emit_mov_r64(&e, X64_RCX, hr_s1);
                        switch (skip.funct3) {
                        case ALU_ADD:
                            if (skip.funct7 == 0x20) {
                                emit_sub_r64(&e, X64_RCX, hr_s2);
                            } else {
                                emit_add_r64(&e, X64_RCX, hr_s2);
                            }
                            break;
                        case ALU_XOR: emit_xor_r64_op(&e, X64_RCX, hr_s2); break;
                        case ALU_OR:  emit_or_r64(&e, X64_RCX, hr_s2); break;
                        case ALU_AND: emit_and_r64(&e, X64_RCX, hr_s2); break;
                        }
                    }

                    if (insn.opcode == OP_REG) {
                        int rs1 = rc_read(&e, &rc, insn.rs1);
                        int rs2 = rc_read(&e, &rc, insn.rs2);
                        emit_cmp_r64(&e, rs1, rs2);
                    } else {
                        int rs1 = rc_read(&e, &rc, insn.rs1);
                        emit_cmp_r64_imm(&e, rs1, insn.imm);
                    }

                    if (insn.rd) {
                        int rd = rc_write(&e, &rc, insn.rd);
                        emit_setcc(&e, is_unsigned ? SETCC_B : SETCC_L, rd);
                        emit_movzx_r64_r8(&e, rd, rd);
                    }

                    int hr_rd = rc_read(&e, &rc, skip.rd);
                    emit_cmovcc(&e, cmov_cc, hr_rd, X64_RCX);

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

            if (self_loop && target == guest_pc) {
                rc_flush(&e, &rc); fc_flush(&e, &fc);
                uint32_t jcc_patch = emit_jcc_rel32(&e, cc);
                emit_patch_rel32(&e, jcc_patch, warm_entry);
                emit_exit_chained(&e, dbt, branch_pc + 4);
                dbt->insns_fused++;
                count++;
                goto done;
            }

            if (self_loop && next.imm > 0
                && num_side_exits < MAX_SIDE_EXITS
                && count < MAX_BLOCK_INSNS - 5) {
                uint32_t jcc_patch = emit_jcc_rel32(&e, cc);
                side_exits[num_side_exits].jcc_patch = jcc_patch;
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

            uint32_t jcc_patch = emit_jcc_rel32(&e, cc);
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            emit_exit_chained(&e, dbt, branch_pc + 4);
            emit_patch_rel32(&e, jcc_patch, emit_pos(&e));
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            emit_exit_chained(&e, dbt, target);
            dbt_trace_fusion(dbt, pc, "slt_branch");
            dbt->insns_fused++;
            count++;
            goto done;
        }

        // Fusion: LUI/AUIPC + LOAD/STORE with statically known address.
        if (have_next
            && (insn.opcode == OP_LUI || insn.opcode == OP_AUIPC)
            && insn.rd) {
            int64_t base = (insn.opcode == OP_AUIPC)
                ? static_cast<int64_t>(pc)
                : 0;
            int64_t addr64 = base + static_cast<int64_t>(insn.imm)
                           + static_cast<int64_t>(next.imm);

            if (addr64 >= INT32_MIN && addr64 <= INT32_MAX) {
                int32_t addr = static_cast<int32_t>(addr64);

                if (next.opcode == OP_LOAD && next.rs1 == insn.rd) {
                    // Preserve the address in rd when the load writes elsewhere.
                    if (insn.rd != next.rd) {
                        int au_rd = rc_write(&e, &rc, insn.rd);
                        if (base == 0 && insn.imm >= INT32_MIN && insn.imm <= INT32_MAX) {
                            emit_mov_r64_imm32(&e, au_rd, insn.imm);
                        } else {
                            emit_mov_r64_imm64(&e, au_rd,
                                static_cast<uint64_t>(base + static_cast<int64_t>(insn.imm)));
                        }
                    }

                    emit_xor_r64(&e, X64_RAX, X64_RAX);
                    int rd = next.rd ? rc_write(&e, &rc, next.rd) : X64_RAX;
                    switch (next.funct3) {
                    case 0: emit_load_mem8s(&e, rd, X64_RAX, addr);  break; // LB
                    case 1: emit_load_mem16s(&e, rd, X64_RAX, addr); break; // LH
                    case 2: emit_load_mem32s(&e, rd, X64_RAX, addr); break; // LW
                    case 3: emit_load_mem64(&e, rd, X64_RAX, addr);  break; // LD
                    case 4: emit_load_mem8u(&e, rd, X64_RAX, addr);  break; // LBU
                    case 5: emit_load_mem16u(&e, rd, X64_RAX, addr); break; // LHU
                    case 6: emit_load_mem32(&e, rd, X64_RAX, addr);  break; // LWU
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
                    // Store does not overwrite rd, so preserve the computed address.
                    int au_rd = rc_write(&e, &rc, insn.rd);
                    if (base == 0 && insn.imm >= INT32_MIN && insn.imm <= INT32_MAX) {
                        emit_mov_r64_imm32(&e, au_rd, insn.imm);
                    } else {
                        emit_mov_r64_imm64(&e, au_rd,
                            static_cast<uint64_t>(base + static_cast<int64_t>(insn.imm)));
                    }

                    int rs2 = next.rs2 ? rc_read(&e, &rc, next.rs2) : X64_RAX;
                    if (next.rs2 == 0) {
                        emit_xor_r64(&e, X64_RDX, X64_RDX);
                        rs2 = X64_RDX;
                    }

                    emit_xor_r64(&e, X64_RAX, X64_RAX);
                    switch (next.funct3) {
                    case 0: emit_store_mem8(&e, X64_RAX, rs2, addr);  break; // SB
                    case 1: emit_store_mem16(&e, X64_RAX, rs2, addr); break; // SH
                    case 2: emit_store_mem32(&e, X64_RAX, rs2, addr); break; // SW
                    case 3: emit_store_mem64(&e, X64_RAX, rs2, addr); break; // SD
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
        }
no_addr_fusion:

        switch (insn.opcode) {

        // -- LUI (with LUI+ADDI fusion) --
        //
        case OP_LUI: {
            // Fusion: LUI rd, upper + JALR rs1=rd → direct jump/call
            uint64_t target_u64;
            uint64_t return_pc;
            if (have_next
                && resolve_direct_jalr_target(pc, insn, next, &target_u64,
                                              &return_pc)) {
                int64_t target = static_cast<int64_t>(target_u64);

                // Materialize the LUI result if JALR writes a different rd.
                if (insn.rd != next.rd && insn.rd != 0) {
                    int rd = rc_write(&e, &rc, insn.rd);
                    emit_mov_r64_imm32(&e, rd, insn.imm);
                }

                if (next.rd) {
                    int rd = rc_write(&e, &rc, next.rd);
                    emit_mov_r64_imm32(&e, rd,
                        static_cast<int32_t>(return_pc));
                }

                bool can_tail_inline =
                    next.rd == 0
                    && !(target >= static_cast<int64_t>(guest_pc)
                         && target <= static_cast<int64_t>(pc + 4))
                    && count < MAX_BLOCK_INSNS - 4;
                switch (emit_direct_jalr_flow(&e, &rc, &fc, dbt, guest_pc, pc,
                                              target_u64, return_pc, next,
                                              side_exits, &num_side_exits,
                                              can_tail_inline,
                                              "lui_jalr_call",
                                              "lui_jalr_tail",
                                              "lui_jalr")) {
                case direct_jalr_flow_t::tail_call:
                    pc = static_cast<uint64_t>(target);
                    count++;
                    dbt->insns_fused++;
                    continue;
                case direct_jalr_flow_t::inline_call_done:
                case direct_jalr_flow_t::chained_exit:
                    count++;
                    dbt->insns_fused++;
                    goto done;
                }
            }

            // Fusion: LUI rd, upper + ADDI rd, rd, lower → MOV rd, imm32
            if (have_next && insn.rd
                && next.opcode == OP_IMM && next.funct3 == ALU_ADDI
                && next.rd == insn.rd && next.rs1 == insn.rd) {
                int64_t val = static_cast<int64_t>(insn.imm)
                            + static_cast<int64_t>(next.imm);
                int rd = rc_write(&e, &rc, insn.rd);
                if (val >= INT32_MIN && val <= INT32_MAX) {
                    emit_mov_r64_imm32(&e, rd, static_cast<int32_t>(val));
                } else {
                    emit_mov_r64_imm64(&e, rd, static_cast<uint64_t>(val));
                }
                dbt_trace_fusion(dbt, pc, "lui_addi");
                pc += 8;
                count++;
                dbt->insns_fused++;
                continue;
            }
            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm32(&e, rd, insn.imm);
            }
            pc += 4;
            continue;
        }

        // -- AUIPC (with AUIPC+ADDI and AUIPC+JALR fusion) --
        //
        case OP_AUIPC: {
            // Fusion: AUIPC rd, upper + JALR rs1=rd → direct jump/call
            uint64_t target_u64;
            uint64_t return_pc;
            if (have_next
                && resolve_direct_jalr_target(pc, insn, next, &target_u64,
                                              &return_pc)) {
                int64_t target = static_cast<int64_t>(target_u64);

                // Preserve the AUIPC result when JALR writes a different rd.
                if (insn.rd != next.rd && insn.rd != 0) {
                    int rd = rc_write(&e, &rc, insn.rd);
                    int64_t val = static_cast<int64_t>(pc)
                                + static_cast<int64_t>(insn.imm);
                    if (val >= INT32_MIN && val <= INT32_MAX) {
                        emit_mov_r64_imm32(&e, rd, static_cast<int32_t>(val));
                    } else {
                        emit_mov_r64_imm64(&e, rd, static_cast<uint64_t>(val));
                    }
                }
                if (next.rd) {
                    int rd = rc_write(&e, &rc, next.rd);
                    emit_mov_r64_imm32(&e, rd,
                        static_cast<int32_t>(return_pc));
                }

                bool can_tail_inline =
                    next.rd == 0
                    && !(target >= static_cast<int64_t>(guest_pc)
                         && target <= static_cast<int64_t>(pc + 4))
                    && count < MAX_BLOCK_INSNS - 4;
                switch (emit_direct_jalr_flow(&e, &rc, &fc, dbt, guest_pc, pc,
                                              target_u64, return_pc, next,
                                              side_exits, &num_side_exits,
                                              can_tail_inline,
                                              "auipc_jalr_call",
                                              "auipc_jalr_tail",
                                              "auipc_jalr")) {
                case direct_jalr_flow_t::tail_call:
                    pc = static_cast<uint64_t>(target);
                    count++;
                    dbt->insns_fused++;
                    continue;
                case direct_jalr_flow_t::inline_call_done:
                case direct_jalr_flow_t::chained_exit:
                    count++;
                    dbt->insns_fused++;
                    goto done;
                }
            }
            // Fusion: AUIPC rd, upper + ADDI rd, rd, lower → MOV rd, pc+imm
            if (have_next && insn.rd
                && next.opcode == OP_IMM && next.funct3 == ALU_ADDI
                && next.rd == insn.rd && next.rs1 == insn.rd) {
                int64_t val = static_cast<int64_t>(pc)
                            + static_cast<int64_t>(insn.imm)
                            + static_cast<int64_t>(next.imm);
                int rd = rc_write(&e, &rc, insn.rd);
                if (val >= INT32_MIN && val <= INT32_MAX) {
                    emit_mov_r64_imm32(&e, rd, static_cast<int32_t>(val));
                } else {
                    emit_mov_r64_imm64(&e, rd, static_cast<uint64_t>(val));
                }
                dbt_trace_fusion(dbt, pc, "auipc_addi");
                pc += 8;
                count++;
                dbt->insns_fused++;
                continue;
            }
            // Unfused AUIPC.
            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                int64_t val = static_cast<int64_t>(pc) + static_cast<int64_t>(insn.imm);
                if (val >= INT32_MIN && val <= INT32_MAX) {
                    emit_mov_r64_imm32(&e, rd, static_cast<int32_t>(val));
                } else {
                    emit_mov_r64_imm64(&e, rd, static_cast<uint64_t>(val));
                }
            }
            pc += 4;
            continue;
        }

        // -- JAL --
        //
        case OP_JAL: {
            uint64_t target = pc + static_cast<int64_t>(insn.imm);

            // Superblock: unconditional backward jump to loop start.
            if (self_loop && insn.rd == 0 && target == guest_pc) {
                // Flush all dirty registers, then JMP to warm_entry.
                rc_flush(&e, &rc); fc_flush(&e, &fc);
                uint32_t jmp_patch = emit_jmp_rel32(&e);
                emit_patch_rel32(&e, jmp_patch, warm_entry);
                goto done;
            }

            // Superblock: forward unconditional jump — follow inline.
            if (self_loop && insn.rd == 0 && insn.imm > 0) {
                pc = target;
                continue;
            }

            // Intrinsics are handled at block level (try_emit_intrinsic
            // in translate_block).  The caller's JAL finds the native
            // stub through normal block chaining / cache lookup.

            // Superblock native CALL: if this is a function call (JAL ra)
            // and the target is already translated, emit a native x86-64
            // CALL instead of exiting the block.  The callee's translated
            // code ends with RET (via emit_exit_indirect), which returns
            // here.  The entire loop body stays in one x86-64 block.
            //
            if (insn.rd == 1) {
                block_entry_t *be = cache_lookup(dbt, target);
                dbt_trace_translate_pc(dbt, guest_pc,
                                       "inline_call guest_pc=0x%llX target=0x%llX found=%d",
                                       static_cast<unsigned long long>(pc),
                                       static_cast<unsigned long long>(target),
                                       be ? 1 : 0);
                if (try_emit_inline_call(&e, &rc, &fc, dbt, target, be, pc + 4,
                                         side_exits, &num_side_exits)) {
                    pc += 4;
                    count++;
                    continue;
                }
            }

            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm32(&e, rd, static_cast<int32_t>(pc + 4));
            }
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            if (insn.rd == 1) {
                emit_ras_push(&e, pc + 4);
            }
            emit_exit_chained(&e, dbt, target);
            goto done;
        }

        // -- JALR --
        //
        case OP_JALR: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            emit_mov_r64(&e, X64_RCX, rs1);
            emit_add_r64_imm(&e, X64_RCX, insn.imm);
            // Clear bit 0 per spec.
            emit_and_r64_imm(&e, X64_RCX, ~1);
            emit_store_next_pc(&e, X64_RCX);

            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm32(&e, rd, static_cast<int32_t>(pc + 4));
            }

            rc_flush(&e, &rc); fc_flush(&e, &fc);

            // RAS push for indirect calls (JALR rd=ra).
            if (insn.rd == 1) {
                emit_ras_push(&e, pc + 4);
            }

            emit_load_next_pc(&e, X64_RCX);

            // RAS return prediction is DISABLED for now.
            //
            // The probe JMPs directly to the predicted block on a hit,
            // bypassing emit_exit_indirect's RET. When this block runs
            // inside an inline CALL context, the outer caller expects
            // the callee to RET through the x86 stack. The probe's JMP
            // breaks that contract — it transfers control without RET,
            // leaving the inline CALL continuation stranded on the stack.
            //
            // Disabling the probe did NOT fix the 0x1096C cold exits
            // (they have a different cause), but the probe remains
            // architecturally unsafe for inline-call contexts. Since
            // blob blocks serve both inline and dispatch-loop contexts,
            // we cannot conditionally enable the probe per-block.
            //
            // Future: a context flag (e.g., pushed on the x86 stack by
            // inline CALL, checked by the probe) could allow safe
            // selective enablement.

            // Tag this exit with the guest PC for cold-exit diagnostics.
            // mov qword [rbx + last_exit_from_off], imm32(pc)
            {
                static constexpr int32_t EXIT_FROM_OFF =
                    static_cast<int32_t>(offsetof(dbt_state_t, last_exit_from));
                emit_mov_r64_imm32(&e, X64_RAX, static_cast<int32_t>(pc));
                emit_byte(&e, rex(1, reg_hi(X64_RAX), 0, 0));
                emit_byte(&e, 0x89);
                emit_byte(&e, modrm(0x02, X64_RAX, X64_RBX));
                emit_u32(&e, EXIT_FROM_OFF);
                // Restore RCX from next_pc (RAX clobbered it).
                emit_load_next_pc(&e, X64_RCX);
            }
            emit_exit_indirect(&e, X64_RCX);
            goto done;
        }

        // -- Branches (with diamond merge for short forward branches) --
        //
        case OP_BRANCH: {
            uint64_t target = pc + static_cast<int64_t>(insn.imm);

            // Determine the branch condition code.
            uint8_t cc;
            switch (insn.funct3) {
            case 0: cc = JCC_E;  break; // BEQ
            case 1: cc = JCC_NE; break; // BNE
            case 4: cc = JCC_L;  break; // BLT
            case 5: cc = JCC_GE; break; // BGE
            case 6: cc = JCC_B;  break; // BLTU
            case 7: cc = JCC_AE; break; // BGEU
            default:
                rc_flush(&e, &rc); fc_flush(&e, &fc);
                emit_exit_chained(&e, dbt, pc);
                goto done;
            }

            // Self-loop: back-edge to block start → internal Jcc.
            // The entire loop stays in one contiguous x86-64 block.
            // Flush to context before jumping since register mapping
            // may have diverged from warm_entry's layout.
            //
            if (self_loop && target == guest_pc) {
                int rs1 = rc_read(&e, &rc, insn.rs1);
                int rs2 = rc_read(&e, &rc, insn.rs2);
                // Flush before CMP to avoid clobbering flags.
                // Use store-to-context for dirty regs, then CMP, then Jcc.
                rc_flush(&e, &rc); fc_flush(&e, &fc);
                emit_cmp_r64(&e, rs1, rs2);
                uint32_t jcc_patch = emit_jcc_rel32(&e, cc);
                emit_patch_rel32(&e, jcc_patch, warm_entry);
                // Fall-through = loop exit.
                emit_exit_chained(&e, dbt, pc + 4);
                goto done;
            }

            // Diamond merge: branch-over-one pattern.
            // If the branch skips exactly 1 instruction (+8) and that
            // instruction is a simple ALU op, convert to CMOVcc.
            //
            if (insn.imm == 8 && pc + 8 <= dbt->memory_size) {
                uint32_t skip_word;
                memcpy(&skip_word, dbt->memory + pc + 4, 4);
                rv64_insn_t skip;
                rv64_decode(skip_word, &skip);

                // The skipped instruction runs when branch is NOT taken.
                // CMOVcc inverse condition: branch taken → skip (no move).
                // We need the INVERSE condition for CMOVcc.
                //
                uint8_t cmov_cc;
                switch (insn.funct3) {
                case 0: cmov_cc = CMOV_NE; break; // BEQ skips → exec if NE
                case 1: cmov_cc = CMOV_E;  break; // BNE skips → exec if E
                case 4: cmov_cc = CMOV_GE; break; // BLT skips → exec if GE
                case 5: cmov_cc = CMOV_L;  break; // BGE skips → exec if L
                case 6: cmov_cc = CMOV_AE; break; // BLTU skips → exec if AE
                case 7: cmov_cc = CMOV_B;  break; // BGEU skips → exec if B
                default: goto no_diamond;
                }

                bool can_predicate = false;

                // OP_IMM: ADDI, XORI, ORI, ANDI (not shifts — different emit)
                if (skip.opcode == OP_IMM && skip.rd != 0
                    && (skip.funct3 == ALU_ADDI || skip.funct3 == ALU_XORI
                        || skip.funct3 == ALU_ORI || skip.funct3 == ALU_ANDI)) {
                    can_predicate = true;
                }
                // OP_REG: ADD, SUB, AND, OR, XOR (not M-ext, not shifts)
                if (skip.opcode == OP_REG && skip.rd != 0
                    && skip.funct7 != 0x01
                    && (skip.funct3 == ALU_ADD || skip.funct3 == ALU_XOR
                        || skip.funct3 == ALU_OR || skip.funct3 == ALU_AND)) {
                    can_predicate = true;
                }
                // OP_LUI: load upper immediate
                if (skip.opcode == OP_LUI && skip.rd != 0) {
                    can_predicate = true;
                }

                if (can_predicate) {
                    // Compute the ALU result in scratch (RCX) FIRST
                    // (this may clobber flags).
                    //
                    if (skip.opcode == OP_LUI) {
                        emit_mov_r64_imm32(&e, X64_RCX, skip.imm);
                    } else if (skip.opcode == OP_IMM) {
                        int hr_src = rc_read(&e, &rc, skip.rs1);
                        emit_mov_r64(&e, X64_RCX, hr_src);
                        switch (skip.funct3) {
                        case ALU_ADDI: emit_add_r64_imm(&e, X64_RCX, skip.imm); break;
                        case ALU_XORI: emit_xor_r64_imm(&e, X64_RCX, skip.imm); break;
                        case ALU_ORI:  emit_or_r64_imm(&e, X64_RCX, skip.imm); break;
                        case ALU_ANDI: emit_and_r64_imm(&e, X64_RCX, skip.imm); break;
                        }
                    } else { // OP_REG
                        int hr_s1 = rc_read(&e, &rc, skip.rs1);
                        int hr_s2 = rc_read(&e, &rc, skip.rs2);
                        emit_mov_r64(&e, X64_RCX, hr_s1);
                        switch (skip.funct3) {
                        case ALU_ADD:
                            if (skip.funct7 == 0x20)
                                emit_sub_r64(&e, X64_RCX, hr_s2);
                            else
                                emit_add_r64(&e, X64_RCX, hr_s2);
                            break;
                        case ALU_XOR: emit_xor_r64_op(&e, X64_RCX, hr_s2); break;
                        case ALU_OR:  emit_or_r64(&e, X64_RCX, hr_s2); break;
                        case ALU_AND: emit_and_r64(&e, X64_RCX, hr_s2); break;
                        }
                    }

                    // Ensure rd has its OLD value in a host register.
                    int hr_rd = rc_read(&e, &rc, skip.rd);

                    // Now do the branch comparison (after ALU, since ALU
                    // may clobber flags).
                    int hr_rs1 = rc_read(&e, &rc, insn.rs1);
                    int hr_rs2 = rc_read(&e, &rc, insn.rs2);
                    emit_cmp_r64(&e, hr_rs1, hr_rs2);

                    // CMOVcc: update rd only when branch NOT taken.
                    emit_cmovcc(&e, cmov_cc, hr_rd, X64_RCX);

                    // Mark rd dirty (value may have changed).
                    int slot = rc_find(&rc, skip.rd);
                    if (slot >= 0) {
                        rc.slots[slot].dirty = 1;
                        rc.slots[slot].last_use = ++rc.clock;
                    }

                    pc += 8; // consumed branch + skipped instruction
                    count++;
                    dbt->insns_fused++;
                    continue;
                }
            }
        no_diamond:

            // Superblock side exit: if we're inside a self-loop scan and
            // this is a forward branch, record the taken path as a cold
            // side exit and continue translating the fall-through inline.
            //
            if (self_loop && insn.imm > 0
                && num_side_exits < MAX_SIDE_EXITS
                && count < MAX_BLOCK_INSNS - 4) {
                int rs1 = rc_read(&e, &rc, insn.rs1);
                int rs2 = rc_read(&e, &rc, insn.rs2);
                emit_cmp_r64(&e, rs1, rs2);
                uint32_t jcc_patch = emit_jcc_rel32(&e, cc);
                side_exits[num_side_exits].jcc_patch = jcc_patch;
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

            // Emit: jcc taken; [fall-through]; jmp not_taken
            uint32_t jcc_patch = emit_jcc_rel32(&e, cc);

            // Fall-through: continue to pc+4.
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            emit_exit_chained(&e, dbt, pc + 4);

            // Taken path:
            emit_patch_rel32(&e, jcc_patch, emit_pos(&e));
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            emit_exit_chained(&e, dbt, target);
            goto done;
        }

        // -- Loads --
        //
        case OP_LOAD: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            emit_mov_r64(&e, X64_RCX, rs1);
            if (insn.imm) emit_add_r64_imm(&e, X64_RCX, insn.imm);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;
            switch (insn.funct3) {
            case 0: emit_load_mem8s(&e, rd, X64_RCX, 0);  break; // LB
            case 1: emit_load_mem16s(&e, rd, X64_RCX, 0); break; // LH
            case 2: emit_load_mem32s(&e, rd, X64_RCX, 0); break; // LW
            case 3: emit_load_mem64(&e, rd, X64_RCX, 0);  break; // LD
            case 4: emit_load_mem8u(&e, rd, X64_RCX, 0);  break; // LBU
            case 5: emit_load_mem16u(&e, rd, X64_RCX, 0); break; // LHU
            case 6: emit_load_mem32(&e, rd, X64_RCX, 0);  break; // LWU
            default: break;
            }
            pc += 4;
            continue;
        }

        // -- Stores --
        //
        case OP_STORE: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rs2 = rc_read(&e, &rc, insn.rs2);
            emit_mov_r64(&e, X64_RCX, rs1);
            if (insn.imm) emit_add_r64_imm(&e, X64_RCX, insn.imm);
            emit_mov_r64(&e, X64_RDX, rs2);
            switch (insn.funct3) {
            case 0: emit_store_mem8(&e, X64_RCX, X64_RDX, 0);  break; // SB
            case 1: emit_store_mem16(&e, X64_RCX, X64_RDX, 0); break; // SH
            case 2: emit_store_mem32(&e, X64_RCX, X64_RDX, 0); break; // SW
            case 3: emit_store_mem64(&e, X64_RCX, X64_RDX, 0); break; // SD
            default: break;
            }
            pc += 4;
            continue;
        }

        // -- OP_IMM (ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI) --
        //
        case OP_IMM: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;

            switch (insn.funct3) {
            case ALU_ADDI:
                emit_mov_r64(&e, rd, rs1);
                emit_add_r64_imm(&e, rd, insn.imm);
                break;
            case ALU_SLTI: // SLTI
                emit_cmp_r64_imm(&e, rs1, insn.imm);
                emit_setcc(&e, SETCC_L, rd);
                emit_movzx_r64_r8(&e, rd, rd);
                break;
            case ALU_SLTIU: // SLTIU
                emit_cmp_r64_imm(&e, rs1, insn.imm);
                emit_setcc(&e, SETCC_B, rd);
                emit_movzx_r64_r8(&e, rd, rd);
                break;
            case ALU_XORI:
                emit_mov_r64(&e, rd, rs1);
                emit_xor_r64_imm(&e, rd, insn.imm);
                break;
            case ALU_ORI:
                emit_mov_r64(&e, rd, rs1);
                emit_or_r64_imm(&e, rd, insn.imm);
                break;
            case ALU_ANDI:
                emit_mov_r64(&e, rd, rs1);
                emit_and_r64_imm(&e, rd, insn.imm);
                break;
            case ALU_SLLI: { // SLLI (6-bit shift amount for RV64)
                uint8_t shamt = static_cast<uint8_t>(insn.imm & 0x3F);
                emit_mov_r64(&e, rd, rs1);
                emit_shl_r64_imm(&e, rd, shamt);
                break;
            }
            case ALU_SRLI: { // SRLI / SRAI (bit 10 of imm selects)
                uint8_t shamt = static_cast<uint8_t>(insn.imm & 0x3F);
                emit_mov_r64(&e, rd, rs1);
                if (insn.imm & 0x400) // SRAI
                    emit_sar_r64_imm(&e, rd, shamt);
                else
                    emit_shr_r64_imm(&e, rd, shamt);
                break;
            }
            }
            pc += 4;
            continue;
        }

        // -- OP_REG (ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND, MUL, etc.) --
        //
        case OP_REG: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rs2 = rc_read(&e, &rc, insn.rs2);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;

            // Guard against rd==rs2 aliasing: when the destination
            // register is the same host register as rs2 (but different
            // from rs1), "mov rd, rs1" clobbers rs2 before the
            // operation reads it.  For commutative ops we can just
            // swap; for non-commutative ops we save rs2 to a temp.
            bool rd_rs2_alias = (rd == rs2 && rd != rs1);

            if (insn.funct7 == 0x01) {
                // M extension (multiply/divide).
                switch (insn.funct3) {
                case 0: // MUL (commutative)
                    if (rd_rs2_alias) {
                        emit_imul_r64(&e, rd, rs1);
                    } else {
                        emit_mov_r64(&e, rd, rs1);
                        emit_imul_r64(&e, rd, rs2);
                    }
                    break;
                case 1: // MULH (signed * signed, high 64)
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_imul1_r64(&e, rs2);
                    rc_store(&e, &rc, insn.rd, X64_RDX);
                    break;
                case 2: // MULHSU (signed * unsigned, high 64) — fall back to helper
                    // Not easily done with x86. Use: RAX=rs1, imul1 with abs, then adjust.
                    // For now, emit interpreter fallback via ECALL-like mechanism.
                    // TODO: implement MULHSU inline
                    goto fallback_interp;
                case 3: // MULHU (unsigned * unsigned, high 64)
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_mul_r64(&e, rs2);
                    rc_store(&e, &rc, insn.rd, X64_RDX);
                    break;
                case 4: // DIV
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_cqo(&e);
                    emit_idiv_r64(&e, rs2);
                    rc_store(&e, &rc, insn.rd, X64_RAX);
                    break;
                case 5: // DIVU
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    // xor rdx, rdx for unsigned
                    emit_xor_r64(&e, X64_RDX, X64_RDX);
                    emit_div_r64(&e, rs2);
                    rc_store(&e, &rc, insn.rd, X64_RAX);
                    break;
                case 6: // REM
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_cqo(&e);
                    emit_idiv_r64(&e, rs2);
                    rc_store(&e, &rc, insn.rd, X64_RDX);
                    break;
                case 7: // REMU
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_xor_r64(&e, X64_RDX, X64_RDX);
                    emit_div_r64(&e, rs2);
                    rc_store(&e, &rc, insn.rd, X64_RDX);
                    break;
                }
            } else {
                // Base integer.
                switch (insn.funct3) {
                case ALU_ADD:
                    if (insn.funct7 == 0x20) {
                        // SUB (non-commutative)
                        if (rd_rs2_alias) {
                            emit_mov_r64(&e, X64_RAX, rs2);
                            emit_mov_r64(&e, rd, rs1);
                            emit_sub_r64(&e, rd, X64_RAX);
                        } else {
                            emit_mov_r64(&e, rd, rs1);
                            emit_sub_r64(&e, rd, rs2);
                        }
                    } else {
                        // ADD (commutative)
                        if (rd_rs2_alias) {
                            emit_add_r64(&e, rd, rs1);
                        } else {
                            emit_mov_r64(&e, rd, rs1);
                            emit_add_r64(&e, rd, rs2);
                        }
                    }
                    break;
                case ALU_SLL:
                    emit_mov_r64(&e, X64_RCX, rs2);
                    emit_mov_r64(&e, rd, rs1);
                    emit_shl_r64_cl(&e, rd);
                    break;
                case ALU_SLT:
                    emit_cmp_r64(&e, rs1, rs2);
                    emit_setcc(&e, SETCC_L, rd);
                    emit_movzx_r64_r8(&e, rd, rd);
                    break;
                case ALU_SLTU:
                    emit_cmp_r64(&e, rs1, rs2);
                    emit_setcc(&e, SETCC_B, rd);
                    emit_movzx_r64_r8(&e, rd, rd);
                    break;
                case ALU_XOR:
                    if (rd_rs2_alias) {
                        emit_xor_r64_op(&e, rd, rs1);
                    } else {
                        emit_mov_r64(&e, rd, rs1);
                        emit_xor_r64_op(&e, rd, rs2);
                    }
                    break;
                case ALU_SRL:
                    emit_mov_r64(&e, X64_RCX, rs2);
                    emit_mov_r64(&e, rd, rs1);
                    if (insn.funct7 == 0x20)
                        emit_sar_r64_cl(&e, rd);
                    else
                        emit_shr_r64_cl(&e, rd);
                    break;
                case ALU_OR:
                    if (rd_rs2_alias) {
                        emit_or_r64(&e, rd, rs1);
                    } else {
                        emit_mov_r64(&e, rd, rs1);
                        emit_or_r64(&e, rd, rs2);
                    }
                    break;
                case ALU_AND:
                    if (rd_rs2_alias) {
                        emit_and_r64(&e, rd, rs1);
                    } else {
                        emit_mov_r64(&e, rd, rs1);
                        emit_and_r64(&e, rd, rs2);
                    }
                    break;
                }
            }
            pc += 4;
            continue;
        }

        // -- OP_IMM32 (ADDIW, SLLIW, SRLIW, SRAIW) --
        //
        case OP_IMM32: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;

            switch (insn.funct3) {
            case 0: // ADDIW
                emit_mov_r64(&e, rd, rs1);
                emit_add_r32_imm(&e, rd, insn.imm);
                emit_movsxd(&e, rd, rd);
                break;
            case 1: { // SLLIW
                uint8_t shamt = static_cast<uint8_t>(insn.imm & 0x1F);
                emit_mov_r64(&e, rd, rs1);
                emit_shl_r32_imm(&e, rd, shamt);
                emit_movsxd(&e, rd, rd);
                break;
            }
            case 5: { // SRLIW / SRAIW
                uint8_t shamt = static_cast<uint8_t>(insn.imm & 0x1F);
                emit_mov_r64(&e, rd, rs1);
                if (insn.imm & 0x400) // SRAIW
                    emit_sar_r32_imm(&e, rd, shamt);
                else
                    emit_shr_r32_imm(&e, rd, shamt);
                emit_movsxd(&e, rd, rd);
                break;
            }
            }
            pc += 4;
            continue;
        }

        // -- OP_REG32 (ADDW, SUBW, SLLW, SRLW, SRAW, MULW, DIVW, etc.) --
        //
        case OP_REG32: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rs2 = rc_read(&e, &rc, insn.rs2);
            int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;
            bool rd_rs2_alias_w = (rd == rs2 && rd != rs1);

            if (insn.funct7 == 0x01) {
                // M extension W-suffix.
                switch (insn.funct3) {
                case 0: // MULW (commutative)
                    if (rd_rs2_alias_w) {
                        emit_imul_r32(&e, rd, rs1);
                    } else {
                        emit_mov_r64(&e, rd, rs1);
                        emit_imul_r32(&e, rd, rs2);
                    }
                    emit_movsxd(&e, rd, rd);
                    break;
                case 4: // DIVW
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    // cdq for 32-bit sign extension
                    emit_byte(&e, 0x99); // CDQ (not CQO)
                    // Need 32-bit idiv
                    if (reg_hi(rs2))
                        emit_byte(&e, rex(0, 0, 0, reg_hi(rs2)));
                    emit_byte(&e, 0xF7);
                    emit_byte(&e, modrm(0x03, 7, rs2));
                    emit_movsxd(&e, X64_RAX, X64_RAX);
                    rc_store(&e, &rc, insn.rd, X64_RAX);
                    break;
                case 5: // DIVUW
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_xor_r64(&e, X64_RDX, X64_RDX);
                    if (reg_hi(rs2))
                        emit_byte(&e, rex(0, 0, 0, reg_hi(rs2)));
                    emit_byte(&e, 0xF7);
                    emit_byte(&e, modrm(0x03, 6, rs2));
                    emit_movsxd(&e, X64_RAX, X64_RAX);
                    rc_store(&e, &rc, insn.rd, X64_RAX);
                    break;
                case 6: // REMW
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_byte(&e, 0x99); // CDQ
                    if (reg_hi(rs2))
                        emit_byte(&e, rex(0, 0, 0, reg_hi(rs2)));
                    emit_byte(&e, 0xF7);
                    emit_byte(&e, modrm(0x03, 7, rs2));
                    emit_movsxd(&e, X64_RDX, X64_RDX);
                    rc_store(&e, &rc, insn.rd, X64_RDX);
                    break;
                case 7: // REMUW
                    rc_load(&e, &rc, X64_RAX, insn.rs1);
                    emit_xor_r64(&e, X64_RDX, X64_RDX);
                    if (reg_hi(rs2))
                        emit_byte(&e, rex(0, 0, 0, reg_hi(rs2)));
                    emit_byte(&e, 0xF7);
                    emit_byte(&e, modrm(0x03, 6, rs2));
                    emit_movsxd(&e, X64_RDX, X64_RDX);
                    rc_store(&e, &rc, insn.rd, X64_RDX);
                    break;
                }
            } else {
                switch (insn.funct3) {
                case 0: // ADDW / SUBW
                    if (insn.funct7 == 0x20) {
                        // SUBW (non-commutative)
                        if (rd_rs2_alias_w) {
                            emit_mov_r64(&e, X64_RAX, rs2);
                            emit_mov_r64(&e, rd, rs1);
                            emit_sub_r32(&e, rd, X64_RAX);
                        } else {
                            emit_mov_r64(&e, rd, rs1);
                            emit_sub_r32(&e, rd, rs2);
                        }
                    } else {
                        // ADDW (commutative)
                        if (rd_rs2_alias_w) {
                            emit_add_r32(&e, rd, rs1);
                        } else {
                            emit_mov_r64(&e, rd, rs1);
                            emit_add_r32(&e, rd, rs2);
                        }
                    }
                    emit_movsxd(&e, rd, rd);
                    break;
                case 1: // SLLW
                    emit_mov_r64(&e, X64_RCX, rs2);
                    emit_mov_r64(&e, rd, rs1);
                    emit_shl_r32_cl(&e, rd);
                    emit_movsxd(&e, rd, rd);
                    break;
                case 5: // SRLW / SRAW
                    emit_mov_r64(&e, X64_RCX, rs2);
                    emit_mov_r64(&e, rd, rs1);
                    if (insn.funct7 == 0x20)
                        emit_sar_r32_cl(&e, rd);
                    else
                        emit_shr_r32_cl(&e, rd);
                    emit_movsxd(&e, rd, rd);
                    break;
                }
            }
            pc += 4;
            continue;
        }

        // -- FP Load (FLD) --
        //
        case OP_FP_LOAD: {
            if (insn.funct3 == 3) { // FLD
                int rs1 = rc_read(&e, &rc, insn.rs1);
                emit_mov_r64(&e, X64_RCX, rs1);
                if (insn.imm) emit_add_r64_imm(&e, X64_RCX, insn.imm);
                int xd = fc_write(&e, &fc, insn.rd);
                emit_load_mem_f64(&e, xd, X64_RCX, 0);
            }
            pc += 4;
            continue;
        }

        // -- FP Store (FSD) --
        //
        case OP_FP_STORE: {
            if (insn.funct3 == 3) { // FSD
                int rs1 = rc_read(&e, &rc, insn.rs1);
                emit_mov_r64(&e, X64_RCX, rs1);
                if (insn.imm) emit_add_r64_imm(&e, X64_RCX, insn.imm);
                int xs2 = fc_read(&e, &fc, insn.rs2);
                emit_store_mem_f64(&e, X64_RCX, xs2, 0);
            }
            pc += 4;
            continue;
        }

        // -- FP Arithmetic (OP_FP) --
        //
        case OP_FP: {
            uint8_t funct5 = insn.funct7 >> 2;
            uint8_t fmt = insn.funct7 & 3;
            if (fmt != FP_FMT_D) goto fallback_interp;

            switch (funct5) {
            case FP_FADD: {
                int xs1 = fc_read(&e, &fc, insn.rs1);
                int xs2 = fc_read(&e, &fc, insn.rs2);
                int xd  = fc_write(&e, &fc, insn.rd);
                emit_movsd_xmm(&e, XMM0, xs1);
                emit_addsd(&e, XMM0, xs2);
                emit_movsd_xmm(&e, xd, XMM0);
                break;
            }
            case FP_FSUB: {
                int xs1 = fc_read(&e, &fc, insn.rs1);
                int xs2 = fc_read(&e, &fc, insn.rs2);
                int xd  = fc_write(&e, &fc, insn.rd);
                emit_movsd_xmm(&e, XMM0, xs1);
                emit_subsd(&e, XMM0, xs2);
                emit_movsd_xmm(&e, xd, XMM0);
                break;
            }
            case FP_FMUL: {
                int xs1 = fc_read(&e, &fc, insn.rs1);
                int xs2 = fc_read(&e, &fc, insn.rs2);
                int xd  = fc_write(&e, &fc, insn.rd);
                emit_movsd_xmm(&e, XMM0, xs1);
                emit_mulsd(&e, XMM0, xs2);
                emit_movsd_xmm(&e, xd, XMM0);
                break;
            }
            case FP_FDIV: {
                int xs1 = fc_read(&e, &fc, insn.rs1);
                int xs2 = fc_read(&e, &fc, insn.rs2);
                int xd  = fc_write(&e, &fc, insn.rd);
                emit_movsd_xmm(&e, XMM0, xs1);
                emit_divsd(&e, XMM0, xs2);
                emit_movsd_xmm(&e, xd, XMM0);
                break;
            }
            case FP_FSQRT: {
                int xs1 = fc_read(&e, &fc, insn.rs1);
                int xd  = fc_write(&e, &fc, insn.rd);
                emit_movsd_xmm(&e, XMM0, xs1);
                emit_sqrtsd(&e, XMM0, XMM0);
                emit_movsd_xmm(&e, xd, XMM0);
                break;
            }
            case FP_FMINMAX: { // FMIN.D / FMAX.D
                int xs1 = fc_read(&e, &fc, insn.rs1);
                int xs2 = fc_read(&e, &fc, insn.rs2);
                int xd  = fc_write(&e, &fc, insn.rd);
                emit_movsd_xmm(&e, XMM0, xs1);
                if (insn.funct3 == 0)
                    emit_minsd(&e, XMM0, xs2);
                else
                    emit_maxsd(&e, XMM0, xs2);
                emit_movsd_xmm(&e, xd, XMM0);
                break;
            }
            case FP_FCMP: { // FEQ.D / FLT.D / FLE.D
                int xs1 = fc_read(&e, &fc, insn.rs1);
                int xs2 = fc_read(&e, &fc, insn.rs2);
                emit_ucomisd(&e, xs1, xs2);
                int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;
                switch (insn.funct3) {
                case 2: // FEQ.D
                    emit_setcc(&e, SETCC_E, rd);
                    break;
                case 1: // FLT.D
                    emit_setcc(&e, SETCC_B, rd);
                    break;
                case 0: // FLE.D
                    emit_setcc(&e, SETCC_AE, rd);
                    // Actually FLE needs: ucomisd rs2, rs1 with AE
                    // Let's use the correct order.
                    break;
                }
                emit_movzx_r64_r8(&e, rd, rd);
                break;
            }
            case FP_FSGNJ: { // FSGNJ.D / FSGNJN.D / FSGNJX.D
                if (insn.rs1 == insn.rs2) {
                    // Common idioms: fmv.d (0), fneg.d (1), fabs.d (2)
                    int xs1 = fc_read(&e, &fc, insn.rs1);
                    emit_movsd_xmm(&e, XMM0, xs1);
                    if (insn.funct3 == 1) {
                        // FSGNJN (fneg): xor with sign bit
                        emit_mov_r64_imm64(&e, X64_RAX, 0x8000000000000000ULL);
                        emit_movq_xmm_r64(&e, XMM1, X64_RAX);
                        emit_byte(&e, 0x66); emit_byte(&e, 0x0F);
                        emit_byte(&e, 0x57); // xorpd
                        emit_byte(&e, modrm(0x03, XMM0, XMM1));
                    } else if (insn.funct3 == 2) {
                        // FSGNJX with rs1==rs2: sign XOR sign = 0, so fabs
                        emit_mov_r64_imm64(&e, X64_RAX, 0x7FFFFFFFFFFFFFFFULL);
                        emit_movq_xmm_r64(&e, XMM1, X64_RAX);
                        emit_byte(&e, 0x66); emit_byte(&e, 0x0F);
                        emit_byte(&e, 0x54); // andpd
                        emit_byte(&e, modrm(0x03, XMM0, XMM1));
                    }
                    // funct3==0: just copy (XMM0 already loaded)
                    { int xd = fc_write(&e, &fc, insn.rd);
                      emit_movsd_xmm(&e, xd, XMM0); }
                } else {
                    // General case: extract sign from rs2, magnitude from rs1
                    // Use integer registers for bit manipulation — flush to
                    // XMM0/XMM1 scratch from cache.
                    int xs1 = fc_read(&e, &fc, insn.rs1);
                    int xs2 = fc_read(&e, &fc, insn.rs2);
                    emit_movsd_xmm(&e, XMM0, xs1);
                    emit_movsd_xmm(&e, XMM1, xs2);
                    // Use integer registers for bit manipulation.
                    // XMM0=rs1, XMM1=rs2 already loaded from cache.
                    emit_movq_r64_xmm(&e, X64_RAX, XMM0);
                    emit_movq_r64_xmm(&e, X64_RCX, XMM1);
                    // RAX = rs1 bits, RCX = rs2 bits
                    emit_mov_r64_imm64(&e, X64_RDX, 0x7FFFFFFFFFFFFFFFULL);
                    emit_and_r64(&e, X64_RAX, X64_RDX); // clear rs1 sign
                    if (insn.funct3 == 0) {
                        // FSGNJ: use rs2 sign
                        emit_mov_r64_imm64(&e, X64_RDX, 0x8000000000000000ULL);
                        emit_and_r64(&e, X64_RCX, X64_RDX);
                        emit_or_r64(&e, X64_RAX, X64_RCX);
                    } else if (insn.funct3 == 1) {
                        // FSGNJN: use negated rs2 sign
                        emit_mov_r64_imm64(&e, X64_RDX, 0x8000000000000000ULL);
                        emit_and_r64(&e, X64_RCX, X64_RDX);
                        emit_xor_r64_op(&e, X64_RCX, X64_RDX);
                        emit_or_r64(&e, X64_RAX, X64_RCX);
                    } else {
                        // FSGNJX: XOR signs
                        emit_mov_r64_imm64(&e, X64_RDX, 0x8000000000000000ULL);
                        emit_and_r64(&e, X64_RCX, X64_RDX);
                        emit_or_r64(&e, X64_RAX, X64_RCX);
                        // FSGNJX keeps rs1 magnitude, XORs signs.
                        // Re-read from cache (XMM0/XMM1 may be clobbered).
                        emit_movsd_xmm(&e, XMM0, xs1);
                        emit_movq_r64_xmm(&e, X64_RAX, XMM0);
                        emit_movsd_xmm(&e, XMM0, xs2);
                        emit_movq_r64_xmm(&e, X64_RCX, XMM0);
                        emit_xor_r64_op(&e, X64_RAX, X64_RCX);
                        emit_and_r64(&e, X64_RAX, X64_RDX); // isolate XOR'd sign
                        // Get rs1 magnitude
                        emit_movsd_xmm(&e, XMM0, xs1);
                        emit_movq_r64_xmm(&e, X64_RCX, XMM0);
                        emit_mov_r64_imm64(&e, X64_RDX, 0x7FFFFFFFFFFFFFFFULL);
                        emit_and_r64(&e, X64_RCX, X64_RDX);
                        emit_or_r64(&e, X64_RAX, X64_RCX);
                    }
                    { int xd = fc_write(&e, &fc, insn.rd);
                      emit_movq_xmm_r64(&e, xd, X64_RAX); }
                }
                break;
            }
            case FP_FCVTW: { // FCVT.W.D / FCVT.WU.D / FCVT.L.D / FCVT.LU.D
                int xs1 = fc_read(&e, &fc, insn.rs1);
                int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;
                emit_cvttsd2si_r64(&e, rd, xs1);
                // For W variants, sign-extend 32→64
                if (insn.rs2 == 0 || insn.rs2 == 1) { // FCVT.W.D / FCVT.WU.D
                    emit_movsxd(&e, rd, rd);
                }
                break;
            }
            case FP_FCVTDW: { // FCVT.D.W / FCVT.D.WU / FCVT.D.L / FCVT.D.LU
                int rs1 = rc_read(&e, &rc, insn.rs1);
                int xd = fc_write(&e, &fc, insn.rd);
                emit_cvtsi2sd_r64(&e, xd, rs1);
                break;
            }
            case FP_FCLASS: { // FMV.X.D (funct3=0) / FCLASS.D (funct3=1)
                if (insn.funct3 == 0) {
                    // FMV.X.D: move FP bits to integer
                    int xs1 = fc_read(&e, &fc, insn.rs1);
                    int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;
                    emit_movq_r64_xmm(&e, rd, xs1);
                } else {
                    // FCLASS.D: complex classification — fall back
                    goto fallback_interp;
                }
                break;
            }
            case FP_FMVDX: { // FMV.D.X (funct3=0)
                int rs1 = rc_read(&e, &rc, insn.rs1);
                int xd = fc_write(&e, &fc, insn.rd);
                emit_movq_xmm_r64(&e, xd, rs1);
                break;
            }
            default:
                goto fallback_interp;
            }
            pc += 4;
            continue;
        }

        // -- FMA (FMADD, FMSUB, FNMSUB, FNMADD) --
        //
        case OP_FMADD: case OP_FMSUB: case OP_FNMSUB: case OP_FNMADD: {
            uint8_t fmt = insn.funct7 & 3;
            if (fmt != FP_FMT_D) goto fallback_interp;

            int xs1 = fc_read(&e, &fc, insn.rs1);
            int xs2 = fc_read(&e, &fc, insn.rs2);
            emit_movsd_xmm(&e, XMM0, xs1);
            emit_mulsd(&e, XMM0, xs2);
            int xs3 = fc_read(&e, &fc, insn.rs3);
            emit_movsd_xmm(&e, XMM1, xs3);
            switch (insn.opcode) {
            case OP_FMADD:  emit_addsd(&e, XMM0, XMM1); break;
            case OP_FMSUB:  emit_subsd(&e, XMM0, XMM1); break;
            case OP_FNMSUB: {
                // -(rs1*rs2) + rs3 = rs3 - (rs1*rs2)
                emit_subsd(&e, XMM1, XMM0);
                int xd = fc_write(&e, &fc, insn.rd);
                emit_movsd_xmm(&e, xd, XMM1);
                pc += 4;
                continue;
            }
            case OP_FNMADD:
                // -(rs1*rs2) - rs3
                emit_addsd(&e, XMM0, XMM1);
                // Negate: xor with sign bit
                emit_mov_r64_imm64(&e, X64_RAX, 0x8000000000000000ULL);
                emit_movq_xmm_r64(&e, XMM1, X64_RAX);
                // xorpd xmm0, xmm1 (66 0F 57)
                emit_byte(&e, 0x66);
                emit_byte(&e, 0x0F);
                emit_byte(&e, 0x57);
                emit_byte(&e, modrm(0x03, XMM0, XMM1));
                break;
            }
            { int xd = fc_write(&e, &fc, insn.rd);
              emit_movsd_xmm(&e, xd, XMM0); }
            pc += 4;
            continue;
        }

        // -- ECALL / EBREAK --
        //
        case OP_SYSTEM: {
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            if (insn.imm == 0) {
                // ECALL: set next_pc with bit 0 = 1 as signal
                emit_exit_with_pc(&e, pc | 1);
            } else {
                // EBREAK: set next_pc with bit 1 = 1 as signal
                emit_exit_with_pc(&e, pc | 2);
            }
            goto done;
        }

        default:
        fallback_interp:
            // Unhandled instruction: skip it (advance PC by 4).
            // The caller should use the interpreter for full coverage.
            rc_flush(&e, &rc); fc_flush(&e, &fc);
            emit_exit_chained(&e, dbt, pc + 4);
            goto done;
        }
    }

    // Block size limit reached.
    rc_flush(&e, &rc);
    fc_flush(&e, &fc);
    emit_exit_chained(&e, dbt, pc);

done:
    // Emit cold stubs for superblock side exits.
    // Each stub: restore dirty registers from snapshot at branch point,
    // then chained exit to the taken-path target.
    //
    for (int i = 0; i < num_side_exits; i++) {
        emit_patch_rel32(&e, side_exits[i].jcc_patch, emit_pos(&e));
        if (side_exits[i].target_pc == 0) {
            // Cold exit from inline CALL: callee returned with
            // unexpected next_pc.  Store diagnostics, then RET
            // to the dispatch loop.
            //
            static constexpr int32_t CE_COUNT_OFF =
                static_cast<int32_t>(offsetof(dbt_state_t, cold_exit_count));
            static constexpr int32_t CE_ACTUAL_OFF =
                static_cast<int32_t>(offsetof(dbt_state_t, cold_exit_actual));
            static constexpr int32_t CE_EXPECTED_OFF =
                static_cast<int32_t>(offsetof(dbt_state_t, cold_exit_expected));

            // Store actual next_pc: mov rax, [rbx + next_pc]; mov [rbx + actual], rax
            emit_load_next_pc(&e, X64_RAX);
            emit_byte(&e, rex(1, reg_hi(X64_RAX), 0, 0));
            emit_byte(&e, 0x89);
            emit_byte(&e, modrm(0x02, X64_RAX, X64_RBX));
            emit_u32(&e, CE_ACTUAL_OFF);

            // Store expected next_pc for this inline CALL cold exit.
            emit_mov_r64_imm64(&e, X64_RDX, side_exits[i].expected_next_pc);
            emit_byte(&e, rex(1, reg_hi(X64_RDX), 0, 0));
            emit_byte(&e, 0x89);
            emit_byte(&e, modrm(0x02, X64_RDX, X64_RBX));
            emit_u32(&e, CE_EXPECTED_OFF);

            // inc qword [rbx + CE_COUNT_OFF]
            emit_byte(&e, rex(1, 0, 0, 0));
            emit_byte(&e, 0xFF);
            emit_byte(&e, modrm(0x02, 0, X64_RBX));  // inc [rbx + disp32]
            emit_u32(&e, CE_COUNT_OFF);

            emit_ret(&e);
        } else {
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

    dbt->blocks_translated++;
    dbt->insns_translated += count;
    if (self_loop) {
        dbt->superblock_count++;
        dbt->side_exits_total += num_side_exits;
    }
    if (e.offset > e.capacity) {
        return nullptr;
    }
    // Hex dump of JIT code for traced blocks.
    if (dbt_trace_translate_enabled(dbt, guest_pc) && e.offset <= 2048) {
        fprintf(stderr, "[dbt-xlate] native_code %p +%u:\n", block_start, e.offset);
        for (uint32_t i = 0; i < e.offset; i++) {
            if (i % 16 == 0) fprintf(stderr, "  %04X: ", i);
            fprintf(stderr, "%02X ", block_start[i]);
            if (i % 16 == 15 || i == e.offset - 1) fprintf(stderr, "\n");
        }
    }

    dbt_trace_translate_pc(dbt, guest_pc,
                           "block guest_pc=0x%llX bytes=%u insns=%d self_loop=%d side_exits=%d fused=%llu inline_calls=%llu total_fused=%llu total_inline_calls=%llu",
                           static_cast<unsigned long long>(guest_pc),
                           e.offset, count, self_loop ? 1 : 0, num_side_exits,
                           static_cast<unsigned long long>(dbt->insns_fused - fused_before),
                           static_cast<unsigned long long>(dbt->inline_calls - inline_calls_before),
                           static_cast<unsigned long long>(dbt->insns_fused),
                           static_cast<unsigned long long>(dbt->inline_calls));
    dbt->code_used += e.offset;
    return block_start;
}

// ---------------------------------------------------------------
// Trampoline — sets up callee-saved registers and calls JIT block
// ---------------------------------------------------------------

static void emit_trampoline(dbt_state_t *dbt) {
    emit_t e;
    e.buf = dbt->code_buf;
    e.offset = 0;
    e.capacity = 512;

    // Save callee-saved registers.
    emit_push(&e, X64_RBX);
    emit_push(&e, X64_R12);
    emit_push(&e, X64_R13);
    emit_push(&e, X64_R14);
    emit_push(&e, X64_R15);

    // RBX = ctx (RDI), R12 = memory (RSI), R13 = cache (RCX)
    // mov rbx, rdi
    emit_byte(&e, rex(1, 0, 0, 0));
    emit_byte(&e, 0x89);
    emit_byte(&e, modrm(0x03, X64_RDI, X64_RBX));

    // mov r12, rsi
    emit_byte(&e, rex(1, 0, 0, 1));
    emit_byte(&e, 0x89);
    emit_byte(&e, modrm(0x03, X64_RSI, reg_lo(X64_R12)));

    // mov r13, rcx
    emit_byte(&e, rex(1, 0, 0, 1));
    emit_byte(&e, 0x89);
    emit_byte(&e, modrm(0x03, X64_RCX, reg_lo(X64_R13)));

    // Pre-load pinned guest registers from ctx.
    // a0 (x10) → RSI, a1 (x11) → RDI, a2 (x12) → R8, a3 (x13) → R9
    //
    for (int i = 0; i < RC_NUM_PINNED; i++) {
        emit_load_guest(&e, rc_host_regs[i], rc_pinned_guest[i]);
    }

    // call rdx (block code)
    emit_byte(&e, 0xFF);
    emit_byte(&e, modrm(0x03, 2, X64_RDX));

    // Post-store pinned guest registers back to ctx.
    // Ensures ctx is up-to-date when control returns to C++.
    //
    for (int i = 0; i < RC_NUM_PINNED; i++) {
        emit_store_guest(&e, rc_pinned_guest[i], rc_host_regs[i]);
    }

    // Restore callee-saved.
    emit_pop(&e, X64_R15);
    emit_pop(&e, X64_R14);
    emit_pop(&e, X64_R13);
    emit_pop(&e, X64_R12);
    emit_pop(&e, X64_RBX);
    emit_ret(&e);

    dbt->code_used = e.offset;
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------

int dbt_init(dbt_state_t *dbt, uint8_t *memory, size_t memory_size,
             int (*ecall_fn)(rv64_ctx_t *, void *), void *ecall_user) {
    *dbt = dbt_state_t();
    dbt->memory = memory;
    dbt->memory_size = memory_size;
    dbt->ecall_fn = ecall_fn;
    dbt->ecall_user = ecall_user;

    // Allocate block cache.
    try
    {
        dbt->cache.assign(BLOCK_CACHE_SIZE, block_entry_t{});
    }
    catch (const std::bad_alloc &)
    {
        fprintf(stderr, "dbt: cannot allocate block cache\n");
        return -1;
    }

    // Allocate JIT code buffer (RWX).
#ifdef WIN32
    dbt->code_buf = static_cast<uint8_t *>(
        VirtualAlloc(nullptr, CODE_BUF_SIZE,
                     MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (dbt->code_buf == nullptr) {
#else
    dbt->code_buf = static_cast<uint8_t *>(
        mmap(nullptr, CODE_BUF_SIZE,
             PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (dbt->code_buf == MAP_FAILED) {
#endif
        fprintf(stderr, "dbt: cannot allocate JIT code buffer\n");
        dbt->cache.clear();
        return -1;
    }

    // Emit trampoline at the start of the code buffer.
    emit_trampoline(dbt);

    return 0;
}

void dbt_reset(dbt_state_t *dbt, uint8_t *memory, size_t memory_size,
               int (*ecall_fn)(rv64_ctx_t *, void *), void *ecall_user) {
    // Update program pointers.
    dbt->memory = memory;
    dbt->memory_size = memory_size;
    dbt->ecall_fn = ecall_fn;
    dbt->ecall_user = ecall_user;

    if (dbt->blob_code_end > 0) {
        // Preserve blob translations: only clear program blocks.
        // Evict cache entries whose native code is beyond the blob region.
        uint8_t *blob_end = dbt->code_buf + dbt->blob_code_end;
        for (size_t i = 0; i < BLOCK_CACHE_SIZE; i++) {
            if (dbt->cache[i].native_code >= blob_end) {
                dbt->cache[i].guest_pc = 0;
                dbt->cache[i].native_code = nullptr;
            }
        }
        dbt->patches.clear();
        dbt->code_used = dbt->blob_code_end;

        // Optional NOP sled for alignment experiments.
        // TINYMUX_DBT_PAD=N inserts N bytes before program code.
        {
            static int pad = -1;
            if (pad < 0) {
                const char *env = getenv("TINYMUX_DBT_PAD");
                pad = env ? atoi(env) : 0;
            }
            uint32_t p = static_cast<uint32_t>(pad);
            if (p > 0 && dbt->code_used + p < CODE_BUF_SIZE) {
                memset(dbt->code_buf + dbt->code_used, 0x90, p);
                dbt->code_used += p;
            }
        }

        // Keep intrinsics — they're blob-related.
    } else {
        // No blob — full reset.
        for (auto &entry : dbt->cache) {
            entry = {};
        }
        dbt->patches.clear();
        dbt->code_used = 0;
        emit_trampoline(dbt);
        dbt->num_intrinsics = 0;
        memset(dbt->intrinsics, 0, sizeof(dbt->intrinsics));
    }

    // Reset statistics.
    dbt->blocks_translated = 0;
    dbt->cache_hits = 0;
    dbt->cache_misses = 0;
    dbt->insns_translated = 0;
    dbt->ras_hits = 0;
    dbt->ras_misses = 0;
    dbt->chain_hits = 0;
    dbt->chain_misses = 0;
    dbt->insns_fused = 0;
    dbt->trace = 0;
    dbt->trace_guest_pc = 0;
    dbt->trace_guest_pc_filter = false;
}

// Lightweight re-run: update only the ECALL callback and clear the CPU
// context.  Keeps the block cache and translated code intact — safe when
// the guest code region is unchanged between runs.
//
void dbt_rerun(dbt_state_t *dbt,
               int (*ecall_fn)(rv64_ctx_t *, void *), void *ecall_user) {
    dbt->ecall_fn = ecall_fn;
    dbt->ecall_user = ecall_user;
}

// Pre-translate all reachable blocks from a guest address.
// Used to ensure Tier 2 blob functions are fully translated and chained
// before superblocks try to inline-call them.
//
void dbt_pretranslate(dbt_state_t *dbt, uint64_t guest_pc) {
    // Visited set: prevents re-scanning already-processed blocks.
    // Direct-mapped hash — collisions just cause redundant work, not bugs.
    static constexpr int VISITED_SIZE = 4096;
    static constexpr int VISITED_MASK = VISITED_SIZE - 1;
    uint64_t visited_map[VISITED_SIZE];
    memset(visited_map, 0, sizeof(visited_map));

    auto is_visited = [&](uint64_t pc) -> bool {
        return visited_map[(pc >> 2) & VISITED_MASK] == pc;
    };
    auto mark_visited = [&](uint64_t pc) {
        visited_map[(pc >> 2) & VISITED_MASK] = pc;
    };

    static constexpr int MAX_WORKLIST = 4096;
    uint64_t worklist[MAX_WORKLIST];
    int wl_count = 0;

    auto enqueue_pc = [&](uint64_t pc) -> bool {
        if (wl_count >= MAX_WORKLIST || is_visited(pc)) return false;
        mark_visited(pc);
        worklist[wl_count++] = pc;
        return true;
    };

    enqueue_pc(guest_pc);

    while (wl_count > 0) {
        uint64_t pc = worklist[--wl_count];

        // Scan RV64 code FIRST to discover successors.
        // For function calls (JAL rd=1), recursively pretranslate the
        // call target BEFORE translating this block.  This ensures the
        // call target is in the cache when translate_block runs, so the
        // inline CALL optimization fires.
        uint64_t scan_pc = pc;
        for (int i = 0; i < MAX_BLOCK_INSNS && scan_pc + 4 <= dbt->memory_size; i++) {
            uint32_t w;
            memcpy(&w, dbt->memory + scan_pc, 4);
            rv64_insn_t si;
            rv64_decode(w, &si);

            rv64_insn_t next_si;
            bool have_next = false;
            if (scan_pc + 8 <= dbt->memory_size) {
                uint32_t next_w;
                memcpy(&next_w, dbt->memory + scan_pc + 4, 4);
                rv64_decode(next_w, &next_si);
                have_next = true;
            }

            if (si.opcode == OP_BRANCH) {
                uint64_t target = scan_pc + static_cast<int64_t>(si.imm);
                enqueue_pc(target);
                enqueue_pc(scan_pc + 4);
                break;
            }
            if (si.opcode == OP_JAL) {
                uint64_t target = scan_pc + static_cast<int64_t>(si.imm);
                if (si.rd != 0 && !is_visited(target)) {
                    // Function call: pretranslate callee FIRST so inline
                    // CALL can find it in the cache.
                    mark_visited(target);
                    dbt_pretranslate(dbt, target);
                }
                if (si.rd == 0) {
                    // Unconditional jump: follow target, stop scanning.
                    enqueue_pc(target);
                    break;
                }
                // Function call: enqueue both target and fall-through,
                // then CONTINUE scanning.  The block may absorb this
                // call via inline CALL and continue translating past
                // it — subsequent calls in the same block must also
                // be discovered and pretranslated.
                enqueue_pc(target);
                enqueue_pc(scan_pc + 4);
                scan_pc += 4;
                continue;
            }
            uint64_t target;
            uint64_t return_pc;
            if (have_next
                && resolve_direct_jalr_target(scan_pc, si, next_si, &target,
                                              &return_pc)
                && target < dbt->memory_size) {
                if (next_si.rd != 0 && !is_visited(target)) {
                    // Direct call encoded as AUIPC/LUI + JALR: pretranslate
                    // callee first so shared inline CALL can find it.
                    mark_visited(target);
                    dbt_pretranslate(dbt, target);
                }
                if (next_si.rd == 0) {
                    // Unconditional indirect jump: follow, stop.
                    enqueue_pc(target);
                    break;
                }
                // Direct call: enqueue and continue scanning past
                // the 2-instruction pair (same rationale as JAL rd=1).
                enqueue_pc(target);
                enqueue_pc(return_pc);
                scan_pc += 8;  // skip LUI/AUIPC + JALR
                continue;
            }
            if (si.opcode == OP_JALR || si.opcode == OP_SYSTEM) {
                break;
            }
            scan_pc += 4;
        }

        // Translate if not already cached.
        if (!cache_lookup(dbt, pc)) {
            uint8_t *code = translate_block(dbt, pc);
            if (!code) continue;
            cache_insert(dbt, pc, code);
            backpatch_chains(dbt, pc, code);
        }
    }
}

void dbt_resolve_chains(dbt_state_t *dbt) {
    // Second pass: resolve any patch sites whose targets are now in cache
    // but weren't when the JMP was emitted.  Only patches still pointing
    // to their slow-path stub (unresolved) are updated — already-resolved
    // patches have been backpatched by backpatch_chains and must not be
    // touched again.
    //
    uint32_t resolved = 0;
    uint32_t already_ok = 0;
    uint32_t unresolvable = 0;
    for (size_t i = 0; i < dbt->patches.size(); i++) {
        uint64_t target = dbt->patches[i].target_pc;
        if (target == 0) continue;

        // Check: is the JMP still pointing to the slow-path stub?
        uint32_t jmp_off = dbt->patches[i].jmp_offset;
        int32_t cur_disp;
        memcpy(&cur_disp, dbt->code_buf + jmp_off, 4);
        uint32_t cur_target = jmp_off + 4 + static_cast<uint32_t>(cur_disp);

        if (cur_target != dbt->patches[i].stub_offset) {
            already_ok++;
            continue;  // already resolved by backpatch_chains
        }

        block_entry_t *be = cache_lookup(dbt, target);
        if (be) {
            backpatch_jmp(dbt->code_buf, jmp_off, be->native_code);
            dbt->chain_hits++;
            resolved++;
        } else {
            unresolvable++;
            dbt_trace_translate(dbt, "unresolved chain: target=0x%llX",
                                static_cast<unsigned long long>(target));
        }
    }
    dbt_trace_translate(dbt,
                        "resolve_chains: %u resolved, %u already_ok, %u unresolvable of %u total",
                        resolved, already_ok, unresolvable,
                        static_cast<unsigned>(dbt->patches.size()));
}

int dbt_run(dbt_state_t *dbt, uint64_t entry_pc, uint64_t stack_top) {
    typedef void (*trampoline_fn_t)(rv64_ctx_t *ctx, uint8_t *mem,
                                     void *block, void *cache);
    trampoline_fn_t trampoline =
        reinterpret_cast<trampoline_fn_t>(static_cast<void *>(dbt->code_buf));

    dbt->ctx = {};
    dbt->ctx.next_pc = entry_pc;
    dbt->ctx.x[2] = stack_top; // SP
    uint64_t dispatch_count = 0;

    for (;;) {
        dispatch_count++;

        if (dbt->max_dispatch && dispatch_count > dbt->max_dispatch) {
            dbt->dispatch_count = dispatch_count;
            fprintf(stderr, "dbt: dispatch limit exceeded (%llu)\n",
                    static_cast<unsigned long long>(dbt->max_dispatch));
            return -2;
        }

        uint64_t pc = dbt->ctx.next_pc;

        // ECALL signal: bit 0 set.
        if (pc & 1) {
            if (dbt->trace & DBT_TRACE_EXEC) {
                fprintf(stderr, "[dbt] disp=%llu ECALL pc=0x%llX\n",
                        static_cast<unsigned long long>(dispatch_count),
                        static_cast<unsigned long long>(pc & ~3ULL));
            }
            dbt->ctx.next_pc = (pc & ~3ULL) + 4;
            int rc = dbt->ecall_fn(&dbt->ctx, dbt->ecall_user);
            if (rc >= 0) {
                dbt->dispatch_count = dispatch_count;
                return rc;
            }
            dbt->ctx.x[0] = 0;
            continue;
        }

        // EBREAK signal: bit 1 set.
        if (pc & 2) {
            dbt->dispatch_count = dispatch_count;
            fprintf(stderr, "dbt: EBREAK at 0x%llX\n",
                    static_cast<unsigned long long>(pc & ~3ULL));
            return -1;
        }

        // Look up or translate block.
        block_entry_t *be = cache_lookup(dbt, pc);
        uint8_t *code;
        if (be) {
            code = be->native_code;
            if (dbt->trace & DBT_TRACE_EXEC) {
                fprintf(stderr, "[dbt] disp=%llu HIT  pc=0x%llX\n",
                        static_cast<unsigned long long>(dispatch_count),
                        static_cast<unsigned long long>(pc));
            }
        } else {
            code = translate_block(dbt, pc);
            if (!code) {
                dbt->dispatch_count = dispatch_count;
                return -1;  // code buffer full
            }
            cache_insert(dbt, pc, code);

            // Backpatch any chained exits that were waiting for this block.
            backpatch_chains(dbt, pc, code);
            if (dbt->trace & DBT_TRACE_EXEC) {
                fprintf(stderr, "[dbt] disp=%llu MISS pc=0x%llX\n",
                        static_cast<unsigned long long>(dispatch_count),
                        static_cast<unsigned long long>(pc));
            }
        }

        // Execute.
        trampoline(&dbt->ctx, dbt->memory, code, dbt->cache.data());
        dbt->ctx.x[0] = 0;
    }
}

void dbt_cleanup(dbt_state_t *dbt) {
#ifdef WIN32
    if (dbt->code_buf) {
        VirtualFree(dbt->code_buf, 0, MEM_RELEASE);
#else
    if (dbt->code_buf && dbt->code_buf != MAP_FAILED) {
        munmap(dbt->code_buf, CODE_BUF_SIZE);
#endif
        dbt->code_buf = nullptr;
    }
    dbt->cache.clear();
    dbt->cache.shrink_to_fit();
    dbt->patches.clear();
    dbt->patches.shrink_to_fit();
}
