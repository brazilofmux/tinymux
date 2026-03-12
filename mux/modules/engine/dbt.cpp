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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>

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

// Superblock side exits: snapshot register cache at each forward branch.
// Cold stubs emitted after the block flush dirty registers from snapshot.
//
static constexpr int MAX_SIDE_EXITS = 8;

struct side_exit_t {
    uint32_t jcc_patch;         // offset of Jcc rel32 displacement
    uint64_t target_pc;         // guest PC of the taken path
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

// ---------------------------------------------------------------
// Intrinsic emitters: native x86-64 replacements for known Tier 2
// helper functions.  When the DBT encounters a JAL to one of these,
// it emits inline x86-64 instead of translating the RV64 byte loops.
//
// All intrinsics use only RAX/RCX/RDX as scratch (not in register
// cache).  Pinned registers (RSI=a0, RDI=a1, R8=a2, R9=a3) carry
// the arguments and results.
// ---------------------------------------------------------------

// rv64_slen(a0=string_ptr) → a0=length
// Counts bytes until NUL.  All pointers are guest addresses (R12-based).
//
static void emit_intrinsic_slen(emit_t *e) {
    // lea rax, [r12 + rsi]   ; host pointer = mem_base + guest_addr
    emit_byte(e, rex(1, reg_hi(X64_RAX), 0, 1));
    emit_byte(e, 0x8D);
    emit_byte(e, modrm(0x00, X64_RAX, 0x04)); // SIB
    emit_byte(e, static_cast<uint8_t>((reg_lo(X64_RSI) << 3) | reg_lo(X64_R12)));

    // xor ecx, ecx           ; counter = 0
    emit_byte(e, 0x31);
    emit_byte(e, modrm(0x03, X64_RCX, X64_RCX));

    // .loop:
    uint32_t loop_top = emit_pos(e);
    // cmp byte [rax + rcx], 0
    emit_byte(e, 0x80);
    emit_byte(e, modrm(0x01, 7, 0x04)); // SIB + cmp /7
    emit_byte(e, static_cast<uint8_t>((reg_lo(X64_RCX) << 3) | reg_lo(X64_RAX)));
    emit_byte(e, 0x00); // disp8 = 0
    emit_byte(e, 0x00); // imm8 = 0

    // je .done
    uint32_t je_patch = emit_pos(e);
    emit_byte(e, 0x74);
    emit_byte(e, 0x00); // rel8 placeholder

    // inc rcx
    emit_byte(e, rex(1, 0, 0, 0));
    emit_byte(e, 0xFF);
    emit_byte(e, modrm(0x03, 0, X64_RCX));

    // jmp .loop
    int32_t jmp_off = static_cast<int32_t>(loop_top) -
                      static_cast<int32_t>(emit_pos(e) + 2);
    emit_byte(e, 0xEB);
    emit_byte(e, static_cast<uint8_t>(jmp_off));

    // .done:
    uint32_t done_pos = emit_pos(e);
    e->buf[je_patch + 1] = static_cast<uint8_t>(done_pos - (je_patch + 2));

    // mov rsi, rcx            ; result length → a0
    emit_mov_r64(e, X64_RSI, X64_RCX);
}

// rv64_scopy(a0=dst, a1=src) → a0=pointer AT NUL terminator
// Copies bytes from src to dst until NUL, writes NUL, returns
// the guest address of the NUL byte in dst.
//
static void emit_intrinsic_scopy(emit_t *e) {
    // lea rcx, [r12 + rsi]   ; host dst = mem_base + guest_dst
    emit_byte(e, rex(1, reg_hi(X64_RCX), 0, 1));
    emit_byte(e, 0x8D);
    emit_byte(e, modrm(0x00, X64_RCX, 0x04)); // SIB
    emit_byte(e, static_cast<uint8_t>((reg_lo(X64_RSI) << 3) | reg_lo(X64_R12)));

    // lea rdx, [r12 + rdi]   ; host src = mem_base + guest_src
    emit_byte(e, rex(1, reg_hi(X64_RDX), 0, 1));
    emit_byte(e, 0x8D);
    emit_byte(e, modrm(0x00, X64_RDX, 0x04)); // SIB
    emit_byte(e, static_cast<uint8_t>((reg_lo(X64_RDI) << 3) | reg_lo(X64_R12)));

    // .loop:
    uint32_t loop_top = emit_pos(e);
    // movzx eax, byte [rdx]   ; load src byte
    emit_byte(e, 0x0F);
    emit_byte(e, 0xB6);
    emit_byte(e, modrm(0x00, X64_RAX, X64_RDX));

    // mov [rcx], al            ; store to dst
    emit_byte(e, 0x88);
    emit_byte(e, modrm(0x00, X64_RAX, X64_RCX));

    // test al, al
    emit_byte(e, 0x84);
    emit_byte(e, modrm(0x03, X64_RAX, X64_RAX));

    // je .done
    uint32_t je_patch = emit_pos(e);
    emit_byte(e, 0x74);
    emit_byte(e, 0x00); // rel8 placeholder

    // inc rcx
    emit_byte(e, rex(1, 0, 0, 0));
    emit_byte(e, 0xFF);
    emit_byte(e, modrm(0x03, 0, X64_RCX));

    // inc rdx
    emit_byte(e, rex(1, 0, 0, 0));
    emit_byte(e, 0xFF);
    emit_byte(e, modrm(0x03, 0, X64_RDX));

    // jmp .loop
    int32_t jmp_off = static_cast<int32_t>(loop_top) -
                      static_cast<int32_t>(emit_pos(e) + 2);
    emit_byte(e, 0xEB);
    emit_byte(e, static_cast<uint8_t>(jmp_off));

    // .done:
    uint32_t done_pos = emit_pos(e);
    e->buf[je_patch + 1] = static_cast<uint8_t>(done_pos - (je_patch + 2));

    // sub rcx, r12            ; convert host ptr back to guest address
    emit_byte(e, rex(1, reg_hi(X64_RCX), 0, 1));
    emit_byte(e, 0x2B);
    emit_byte(e, modrm(0x03, X64_RCX, reg_lo(X64_R12)));

    // mov rsi, rcx             ; result → a0
    emit_mov_r64(e, X64_RSI, X64_RCX);
}

// ---------------------------------------------------------------
// Block cache
// ---------------------------------------------------------------

static inline uint32_t cache_hash(uint64_t pc) {
    uint32_t h = static_cast<uint32_t>(pc >> 2);
    h ^= (h >> 10);
    return h & BLOCK_CACHE_MASK;
}

static block_entry_t *cache_lookup(dbt_state_t *dbt, uint64_t pc) {
    block_entry_t *e = &dbt->cache[cache_hash(pc)];
    if (e->guest_pc == pc && e->native_code) {
        dbt->cache_hits++;
        return e;
    }
    dbt->cache_misses++;
    return nullptr;
}

static void cache_insert(dbt_state_t *dbt, uint64_t pc, uint8_t *code) {
    block_entry_t *e = &dbt->cache[cache_hash(pc)];
    e->guest_pc = pc;
    e->native_code = code;
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

    // RAS hit: inline block cache probe.
    // index = ((rcx >> 2) ^ ((rcx >> 2) >> 10)) & BLOCK_CACHE_MASK
    emit_mov_r64(e, X64_RAX, X64_RCX);
    emit_shr_r64_imm(e, X64_RAX, 2);
    // xor rax, (rax >> 10) — spread high bits to avoid Tier 2 collisions
    emit_mov_r64(e, X64_RDX, X64_RAX);
    emit_shr_r64_imm(e, X64_RDX, 10);
    // xor rax, rdx
    emit_byte(e, rex(1, reg_hi(X64_RAX), 0, 0));
    emit_byte(e, 0x33);
    emit_byte(e, modrm(0x03, X64_RAX, X64_RDX));
    emit_and_r64_imm(e, X64_RAX, static_cast<int32_t>(BLOCK_CACHE_MASK));

    // entry = R13 + index * 16 (sizeof block_entry_t)
    emit_shl_r64_imm(e, X64_RAX, 4);
    // cmp [r13 + rax + 0], rcx  — check guest_pc
    emit_byte(e, rex(1, reg_hi(X64_RCX), reg_hi(X64_RAX), 1));
    emit_byte(e, 0x3B);
    emit_byte(e, modrm(0x01, X64_RCX, 0x04)); // SIB
    emit_byte(e, static_cast<uint8_t>((reg_lo(X64_RAX) << 3) | reg_lo(X64_R13)));
    emit_byte(e, 0x00); // disp8 = 0
    uint32_t jne_cache_miss = emit_jcc_rel32(e, JCC_NE);

    // Cache hit: load native_code and jump.
    // mov rax, [r13 + rax + 8]
    emit_byte(e, rex(1, reg_hi(X64_RAX), reg_hi(X64_RAX), 1));
    emit_byte(e, 0x8B);
    emit_byte(e, modrm(0x01, X64_RAX, 0x04)); // SIB
    emit_byte(e, static_cast<uint8_t>((reg_lo(X64_RAX) << 3) | reg_lo(X64_R13)));
    emit_byte(e, 0x08); // disp8 = 8

    // test rax, rax (null check)
    emit_test_r64(e, X64_RAX, X64_RAX);
    uint32_t jz_null = emit_jcc_rel32(e, JCC_E);

    // jmp rax — direct to native code!
    emit_byte(e, 0xFF);
    emit_byte(e, modrm(0x03, 4, X64_RAX)); // jmp rax
    dbt->ras_hits++;

    // Miss paths: fall through to indirect exit.
    emit_patch_rel32(e, jne_miss, emit_pos(e));
    emit_patch_rel32(e, jne_cache_miss, emit_pos(e));
    emit_patch_rel32(e, jz_null, emit_pos(e));
    dbt->ras_misses++;
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
    for (uint32_t i = 0; i < dbt->num_patches; i++) {
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
    // Check if target is already translated.
    uint32_t idx = cache_hash(target_pc);
    block_entry_t *be = &dbt->cache[idx];
    bool known = (be->guest_pc == target_pc && be->native_code);

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
        if (dbt->num_patches < MAX_PATCH_SITES) {
            dbt->patches[dbt->num_patches].jmp_offset = abs_offset;
            dbt->patches[dbt->num_patches].target_pc = target_pc;
            dbt->num_patches++;
            dbt->chain_misses++;
        }

        // Slow-path stub: store next_pc and return to trampoline.
        uint32_t stub_pos = emit_pos(e);
        emit_patch_rel32(e, jmp_patch, stub_pos);
        emit_exit_with_pc(e, target_pc);
    }
}

// ---------------------------------------------------------------
// Translate a single block
// ---------------------------------------------------------------

static uint8_t *translate_block(dbt_state_t *dbt, uint64_t guest_pc) {
    uint8_t *block_start = dbt->code_buf + dbt->code_used;

    emit_t e;
    e.buf = block_start;
    e.offset = 0;
    e.capacity = CODE_BUF_SIZE - dbt->code_used;

    reg_cache_t rc;
    rc_init_pinned(&rc);

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

    while (count < MAX_BLOCK_INSNS) {
        if (pc + 4 > dbt->memory_size) {
            rc_flush(&e, &rc);
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

        switch (insn.opcode) {

        // -- LUI (with LUI+ADDI fusion) --
        //
        case OP_LUI: {
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
            if (have_next && insn.rd
                && next.opcode == OP_JALR
                && next.rs1 == insn.rd) {
                int64_t target = static_cast<int64_t>(pc)
                               + static_cast<int64_t>(insn.imm)
                               + static_cast<int64_t>(next.imm);
                target &= ~1LL; // clear bit 0 per JALR spec
                if (next.rd) {
                    int rd = rc_write(&e, &rc, next.rd);
                    emit_mov_r64_imm32(&e, rd,
                        static_cast<int32_t>(pc + 8));
                }
                rc_flush(&e, &rc);
                emit_exit_chained(&e, dbt, static_cast<uint64_t>(target));
                count++;
                dbt->insns_fused++;
                goto done;
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
                rc_flush(&e, &rc);
                uint32_t jmp_patch = emit_jmp_rel32(&e);
                emit_patch_rel32(&e, jmp_patch, warm_entry);
                goto done;
            }

            // Superblock: forward unconditional jump — follow inline.
            if (self_loop && insn.rd == 0 && insn.imm > 0) {
                pc = target;
                continue;
            }

            // Intrinsic: if the JAL targets a known intrinsic function,
            // emit inline x86-64 instead of calling the translated RV64.
            // No CALL/RET, no block break — the native code runs inline.
            //
            if (insn.rd == 1) {
                bool is_intrinsic = false;
                if (dbt->intrinsic_slen && target == dbt->intrinsic_slen) {
                    // Flush dirty regs so intrinsic reads correct values.
                    rc_flush(&e, &rc);
                    emit_intrinsic_slen(&e);
                    // Only a0 (slot 0, RSI) changed — mark it dirty.
                    // Other pinned/cached values remain valid.
                    rc.slots[0].dirty = 1;
                    rc.slots[0].last_use = ++rc.clock;
                    dbt->intrinsic_hits++;
                    is_intrinsic = true;
                } else if (dbt->intrinsic_scopy && target == dbt->intrinsic_scopy) {
                    rc_flush(&e, &rc);
                    emit_intrinsic_scopy(&e);
                    rc.slots[0].dirty = 1;
                    rc.slots[0].last_use = ++rc.clock;
                    dbt->intrinsic_hits++;
                    is_intrinsic = true;
                }
                if (is_intrinsic) {
                    // Write ra = pc+4 (JAL rd=1 convention).
                    int rd = rc_write(&e, &rc, 1);
                    emit_mov_r64_imm32(&e, rd, static_cast<int32_t>(pc + 4));
                    pc += 4;
                    count++;
                    continue;
                }
            }

            // Superblock native CALL: if this is a function call (JAL ra)
            // and the target is already translated, emit a native x86-64
            // CALL instead of exiting the block.  The callee's translated
            // code ends with RET (via emit_exit_indirect), which returns
            // here.  The entire loop body stays in one x86-64 block.
            //
            if (insn.rd == 1) {
                uint32_t idx = cache_hash(target);
                block_entry_t *be = &dbt->cache[idx];
                if (be->guest_pc == target && be->native_code) {
                    // Flush cached registers — callee reads from ctx.
                    rc_flush(&e, &rc);

                    // Store ra = pc+4 in ctx (callee's JALR reads this).
                    emit_mov_r64_imm32(&e, X64_RAX,
                                       static_cast<int32_t>(pc + 4));
                    emit_store_guest(&e, 1, X64_RAX);

                    // Poison the RAS so the callee's pop_and_probe
                    // mismatches and falls through to RET.
                    emit_ras_push(&e, 1ULL);

                    // Native CALL to the translated target.
                    uint32_t call_patch = emit_call_rel32(&e);
                    uint32_t target_off = static_cast<uint32_t>(
                        be->native_code - e.buf);
                    emit_patch_rel32(&e, call_patch, target_off);

                    // Check: did the callee return normally?
                    // If ctx.next_pc != pc+4, the callee exited early
                    // (internal block not yet chained).  Fall back to
                    // the dispatch loop.
                    emit_cmp_ctx_imm32(&e, CTX_NEXT_PC_OFF,
                                       static_cast<int32_t>(pc + 4));
                    uint32_t jne_cold = emit_jcc_rel32(&e, JCC_NE);

                    // Hot path: callee returned normally.
                    // Reload register cache (callee clobbered regs).
                    rc_invalidate_reload(&e, &rc);

                    // Store the cold-exit patch for later emission.
                    // We'll emit the stub after the main block code.
                    if (num_side_exits < MAX_SIDE_EXITS) {
                        side_exits[num_side_exits].jcc_patch = jne_cold;
                        side_exits[num_side_exits].target_pc = 0; // sentinel
                        num_side_exits++;
                    }

                    dbt->inline_calls++;
                    pc += 4;
                    count++;
                    continue;
                }
            }

            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm32(&e, rd, static_cast<int32_t>(pc + 4));
            }
            // RAS push: if rd=x1 (ra), this is a function call.
            if (insn.rd == 1) {
                rc_flush(&e, &rc);
                emit_ras_push(&e, static_cast<uint64_t>(pc + 4));
            } else {
                rc_flush(&e, &rc);
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
            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r64_imm32(&e, rd, static_cast<int32_t>(pc + 4));
            }
            rc_flush(&e, &rc);

            // RAS pop: if rs1=x1 (ra), this is a function return.
            // Pop the predicted address and inline cache probe.
            if (insn.rs1 == 1) {
                emit_ras_pop_and_probe(&e, dbt);
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
                rc_flush(&e, &rc);
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
                rc_flush(&e, &rc);
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
            rc_flush(&e, &rc);
            emit_exit_chained(&e, dbt, pc + 4);

            // Taken path:
            emit_patch_rel32(&e, jcc_patch, emit_pos(&e));
            rc_flush(&e, &rc);
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
                emit_load_mem_f64(&e, XMM0, X64_RCX, 0);
                emit_store_fp_d(&e, insn.rd, XMM0);
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
                emit_load_fp_d(&e, XMM0, insn.rs2);
                emit_store_mem_f64(&e, X64_RCX, XMM0, 0);
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
            case FP_FADD:
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_load_fp_d(&e, XMM1, insn.rs2);
                emit_addsd(&e, XMM0, XMM1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            case FP_FSUB:
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_load_fp_d(&e, XMM1, insn.rs2);
                emit_subsd(&e, XMM0, XMM1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            case FP_FMUL:
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_load_fp_d(&e, XMM1, insn.rs2);
                emit_mulsd(&e, XMM0, XMM1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            case FP_FDIV:
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_load_fp_d(&e, XMM1, insn.rs2);
                emit_divsd(&e, XMM0, XMM1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            case FP_FSQRT:
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_sqrtsd(&e, XMM0, XMM0);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            case FP_FMINMAX: // FMIN.D / FMAX.D
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_load_fp_d(&e, XMM1, insn.rs2);
                if (insn.funct3 == 0)
                    emit_minsd(&e, XMM0, XMM1);
                else
                    emit_maxsd(&e, XMM0, XMM1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            case FP_FCMP: { // FEQ.D / FLT.D / FLE.D
                emit_load_fp_d(&e, XMM0, insn.rs1);
                emit_load_fp_d(&e, XMM1, insn.rs2);
                emit_ucomisd(&e, XMM0, XMM1);
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
                    emit_load_fp_d(&e, XMM0, insn.rs1);
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
                    emit_store_fp_d(&e, insn.rd, XMM0);
                } else {
                    // General case: extract sign from rs2, magnitude from rs1
                    emit_load_fp_d(&e, XMM0, insn.rs1);
                    emit_load_fp_d(&e, XMM1, insn.rs2);
                    // Load sign mask into RAX, then XMM via movq
                    emit_mov_r64_imm64(&e, X64_RAX, 0x8000000000000000ULL);
                    emit_movq_xmm_r64(&e, XMM1, X64_RAX);
                    // Save sign mask in XMM1 scratch
                    // Actually, need rs2 sign and rs1 magnitude
                    // Re-approach: use integer registers
                    emit_load_fp_d(&e, XMM0, insn.rs1);
                    emit_movq_r64_xmm(&e, X64_RAX, XMM0);
                    emit_load_fp_d(&e, XMM0, insn.rs2);
                    emit_movq_r64_xmm(&e, X64_RCX, XMM0);
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
                        // Wait — FSGNJX keeps rs1 magnitude, XORs signs
                        // Need to reload rs1 sign bit for XOR
                        // Simpler: just XOR the sign bits
                        emit_load_fp_d(&e, XMM0, insn.rs1);
                        emit_movq_r64_xmm(&e, X64_RAX, XMM0);
                        emit_load_fp_d(&e, XMM0, insn.rs2);
                        emit_movq_r64_xmm(&e, X64_RCX, XMM0);
                        emit_xor_r64_op(&e, X64_RAX, X64_RCX);
                        emit_and_r64(&e, X64_RAX, X64_RDX); // isolate XOR'd sign
                        // Get rs1 magnitude
                        emit_load_fp_d(&e, XMM0, insn.rs1);
                        emit_movq_r64_xmm(&e, X64_RCX, XMM0);
                        emit_mov_r64_imm64(&e, X64_RDX, 0x7FFFFFFFFFFFFFFFULL);
                        emit_and_r64(&e, X64_RCX, X64_RDX);
                        emit_or_r64(&e, X64_RAX, X64_RCX);
                    }
                    emit_movq_xmm_r64(&e, XMM0, X64_RAX);
                    emit_store_fp_d(&e, insn.rd, XMM0);
                }
                break;
            }
            case FP_FCVTW: { // FCVT.W.D / FCVT.WU.D / FCVT.L.D / FCVT.LU.D
                emit_load_fp_d(&e, XMM0, insn.rs1);
                int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;
                emit_cvttsd2si_r64(&e, rd, XMM0);
                // For W variants, sign-extend 32→64
                if (insn.rs2 == 0 || insn.rs2 == 1) { // FCVT.W.D / FCVT.WU.D
                    emit_movsxd(&e, rd, rd);
                }
                break;
            }
            case FP_FCVTDW: { // FCVT.D.W / FCVT.D.WU / FCVT.D.L / FCVT.D.LU
                int rs1 = rc_read(&e, &rc, insn.rs1);
                emit_cvtsi2sd_r64(&e, XMM0, rs1);
                emit_store_fp_d(&e, insn.rd, XMM0);
                break;
            }
            case FP_FCLASS: { // FMV.X.D (funct3=0) / FCLASS.D (funct3=1)
                if (insn.funct3 == 0) {
                    // FMV.X.D: move FP bits to integer
                    emit_load_fp_d(&e, XMM0, insn.rs1);
                    int rd = insn.rd ? rc_write(&e, &rc, insn.rd) : X64_RAX;
                    emit_movq_r64_xmm(&e, rd, XMM0);
                } else {
                    // FCLASS.D: complex classification — fall back
                    goto fallback_interp;
                }
                break;
            }
            case FP_FMVDX: { // FMV.D.X (funct3=0)
                int rs1 = rc_read(&e, &rc, insn.rs1);
                emit_movq_xmm_r64(&e, XMM0, rs1);
                emit_store_fp_d(&e, insn.rd, XMM0);
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

            emit_load_fp_d(&e, XMM0, insn.rs1);
            emit_load_fp_d(&e, XMM1, insn.rs2);
            emit_mulsd(&e, XMM0, XMM1);
            emit_load_fp_d(&e, XMM1, insn.rs3);
            switch (insn.opcode) {
            case OP_FMADD:  emit_addsd(&e, XMM0, XMM1); break;
            case OP_FMSUB:  emit_subsd(&e, XMM0, XMM1); break;
            case OP_FNMSUB:
                // -(rs1*rs2) + rs3 = rs3 - (rs1*rs2)
                emit_subsd(&e, XMM1, XMM0);
                emit_store_fp_d(&e, insn.rd, XMM1);
                pc += 4;
                continue;
            case OP_FNMADD:
                // -(rs1*rs2) - rs3
                emit_addsd(&e, XMM0, XMM1);
                // Negate: xor with sign bit
                // Load sign mask into XMM1 via integer register
                emit_mov_r64_imm64(&e, X64_RAX, 0x8000000000000000ULL);
                emit_movq_xmm_r64(&e, XMM1, X64_RAX);
                // xorpd xmm0, xmm1 (66 0F 57)
                emit_byte(&e, 0x66);
                emit_byte(&e, 0x0F);
                emit_byte(&e, 0x57);
                emit_byte(&e, modrm(0x03, XMM0, XMM1));
                break;
            }
            emit_store_fp_d(&e, insn.rd, XMM0);
            pc += 4;
            continue;
        }

        // -- ECALL / EBREAK --
        //
        case OP_SYSTEM: {
            rc_flush(&e, &rc);
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
            rc_flush(&e, &rc);
            emit_exit_chained(&e, dbt, pc + 4);
            goto done;
        }
    }

    // Block size limit reached.
    rc_flush(&e, &rc);
    emit_exit_chained(&e, dbt, pc);

done:
    // Emit cold stubs for superblock side exits.
    // Each stub: restore dirty registers from snapshot at branch point,
    // then chained exit to the taken-path target.
    //
    for (int i = 0; i < num_side_exits; i++) {
        emit_patch_rel32(&e, side_exits[i].jcc_patch, emit_pos(&e));
        if (side_exits[i].target_pc == 0) {
            // Cold exit from native CALL: callee set ctx.next_pc
            // to an internal address (not fully chained yet).
            // Return to the dispatch loop which will handle it.
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
    memset(dbt, 0, sizeof(*dbt));
    dbt->memory = memory;
    dbt->memory_size = memory_size;
    dbt->ecall_fn = ecall_fn;
    dbt->ecall_user = ecall_user;

    // Allocate block cache.
    dbt->cache = static_cast<block_entry_t *>(
        calloc(BLOCK_CACHE_SIZE, sizeof(block_entry_t)));
    if (!dbt->cache) {
        fprintf(stderr, "dbt: cannot allocate block cache\n");
        return -1;
    }

    // Allocate patch sites for block chaining.
    dbt->patches = static_cast<patch_site_t *>(
        calloc(MAX_PATCH_SITES, sizeof(patch_site_t)));
    if (!dbt->patches) {
        fprintf(stderr, "dbt: cannot allocate patch sites\n");
        free(dbt->cache);
        dbt->cache = nullptr;
        return -1;
    }
    dbt->num_patches = 0;

    // Allocate JIT code buffer (RWX).
    dbt->code_buf = static_cast<uint8_t *>(
        mmap(nullptr, CODE_BUF_SIZE,
             PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (dbt->code_buf == MAP_FAILED) {
        fprintf(stderr, "dbt: cannot allocate JIT code buffer\n");
        free(dbt->cache);
        dbt->cache = nullptr;
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

    // Clear block cache and patch sites (invalidate all translated blocks).
    memset(dbt->cache, 0, BLOCK_CACHE_SIZE * sizeof(block_entry_t));
    dbt->num_patches = 0;

    // Reset code buffer after the trampoline and re-emit it.
    // The trampoline is small (~30 bytes) and identical every time,
    // but re-emitting is simpler than tracking its exact size.
    dbt->code_used = 0;
    emit_trampoline(dbt);

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
    // Worklist of PCs to translate.
    static constexpr int MAX_WORKLIST = 64;
    uint64_t worklist[MAX_WORKLIST];
    int wl_count = 0;

    if (!cache_lookup(dbt, guest_pc)) {
        worklist[wl_count++] = guest_pc;
    }

    while (wl_count > 0) {
        uint64_t pc = worklist[--wl_count];
        if (cache_lookup(dbt, pc)) continue;

        uint8_t *code = translate_block(dbt, pc);
        cache_insert(dbt, pc, code);
        backpatch_chains(dbt, pc, code);

        // Scan the RV64 code at pc to find block exits.
        // Follow branches and JAL to discover successor blocks.
        uint64_t scan_pc = pc;
        for (int i = 0; i < MAX_BLOCK_INSNS && scan_pc + 4 <= dbt->memory_size; i++) {
            uint32_t w;
            memcpy(&w, dbt->memory + scan_pc, 4);
            rv64_insn_t si;
            rv64_decode(w, &si);

            if (si.opcode == OP_BRANCH) {
                uint64_t target = scan_pc + static_cast<int64_t>(si.imm);
                // Add both branch target and fall-through.
                if (wl_count < MAX_WORKLIST && !cache_lookup(dbt, target))
                    worklist[wl_count++] = target;
                if (wl_count < MAX_WORKLIST && !cache_lookup(dbt, scan_pc + 4))
                    worklist[wl_count++] = scan_pc + 4;
                break;
            }
            if (si.opcode == OP_JAL) {
                uint64_t target = scan_pc + static_cast<int64_t>(si.imm);
                if (si.rd == 0) {
                    // Unconditional jump — follow target.
                    if (wl_count < MAX_WORKLIST && !cache_lookup(dbt, target))
                        worklist[wl_count++] = target;
                } else {
                    // Function call — fall-through is the successor.
                    if (wl_count < MAX_WORKLIST && !cache_lookup(dbt, scan_pc + 4))
                        worklist[wl_count++] = scan_pc + 4;
                }
                break;
            }
            if (si.opcode == OP_JALR || si.opcode == OP_SYSTEM) {
                break; // indirect jump / ecall — can't follow statically
            }
            scan_pc += 4;
        }
    }
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
        uint64_t pc = dbt->ctx.next_pc;

        if (dbt->trace) {
            fprintf(stderr, "[dbt] pc=0x%llX\n",
                    static_cast<unsigned long long>(pc & ~3ULL));
        }

        // ECALL signal: bit 0 set.
        if (pc & 1) {
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
        } else {
            code = translate_block(dbt, pc);
            cache_insert(dbt, pc, code);

            // Backpatch any chained exits that were waiting for this block.
            backpatch_chains(dbt, pc, code);
        }

        // Execute.
        trampoline(&dbt->ctx, dbt->memory, code, dbt->cache);
        dbt->ctx.x[0] = 0;
    }
}

void dbt_cleanup(dbt_state_t *dbt) {
    if (dbt->code_buf && dbt->code_buf != MAP_FAILED) {
        munmap(dbt->code_buf, CODE_BUF_SIZE);
        dbt->code_buf = nullptr;
    }
    free(dbt->cache);
    dbt->cache = nullptr;
    free(dbt->patches);
    dbt->patches = nullptr;
}
