/*! \file dbt.cpp
 * \brief RV64IMD dynamic binary translator — shared code.
 *
 * Platform-independent parts of the DBT: block cache, trace helpers,
 * dispatch loop, and public API.  The per-platform translation backend
 * (trampoline, instruction translation, intrinsic stubs) is in a
 * separate file selected at configure time (e.g. dbt_x64_sysv.cpp).
 *
 * See docs/DBT-PORTABILITY.md for the multi-platform design.
 */

#include "dbt.h"
#include "dbt_host.h"
#include "dbt_internal.h"
#include "dbt_jit_mem.h"
#include "dbt_decoder.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// ---------------------------------------------------------------
// Instruction cache coherency
// ---------------------------------------------------------------

// Flush the instruction cache for newly generated or modified code.
// On AArch64, the I-cache and D-cache are not coherent — writes to
// executable memory require an explicit cache maintenance operation
// before the CPU will fetch the new instructions.  On x86-64, this
// is a no-op (coherent I-cache).
//
static inline void dbt_flush_code(dbt_state_t *dbt, uint32_t from_offset) {
    jit_write_end(dbt->code_buf + from_offset,
                  dbt->code_used - from_offset);
}

// ---------------------------------------------------------------
// Trace helpers
// ---------------------------------------------------------------

bool dbt_trace_translate_enabled(const dbt_state_t *dbt,
                                  uint64_t guest_pc) {
    if ((dbt->trace & DBT_TRACE_TRANSLATE) == 0) return false;
    return !dbt->trace_guest_pc_filter || dbt->trace_guest_pc == guest_pc;
}

void dbt_trace_translate_pc(dbt_state_t *dbt, uint64_t guest_pc,
                             const char *fmt, ...) {
    if (!dbt_trace_translate_enabled(dbt, guest_pc)) return;

    va_list ap;
    va_start(ap, fmt);
    fputs("[dbt-xlate] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void dbt_trace_translate(dbt_state_t *dbt, const char *fmt, ...) {
    if ((dbt->trace & DBT_TRACE_TRANSLATE) == 0) return;

    va_list ap;
    va_start(ap, fmt);
    fputs("[dbt-xlate] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void dbt_trace_fusion(dbt_state_t *dbt, uint64_t pc, const char *kind) {
    dbt_trace_translate_pc(dbt, pc, "fusion guest_pc=0x%llX kind=%s",
                           static_cast<unsigned long long>(pc), kind);
}

// ---------------------------------------------------------------
// Direct JALR target resolution (pure computation)
// ---------------------------------------------------------------

bool dbt_resolve_direct_jalr_target(uint64_t pc,
                                     const rv64_insn_t &insn,
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

// ---------------------------------------------------------------
// Block cache
// ---------------------------------------------------------------

static inline uint32_t cache_set(uint64_t pc) {
    uint32_t h = static_cast<uint32_t>(pc >> 2);
    h ^= (h >> 10);
    return h & BLOCK_CACHE_MASK;
}

block_entry_t *dbt_cache_lookup(dbt_state_t *dbt, uint64_t pc) {
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

void dbt_cache_insert(dbt_state_t *dbt, uint64_t pc, uint8_t *code) {
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
// Block chaining
// ---------------------------------------------------------------

// Backpatch all pending exits that target the given guest PC.
//
void dbt_backpatch_chains(dbt_state_t *dbt, uint64_t guest_pc,
                           uint8_t *native_code) {
    const auto it = dbt->pending_patch_targets.find(guest_pc);
    if (it == dbt->pending_patch_targets.end()) {
        return;
    }

    for (size_t patch_index : it->second) {
        dbt_backend_backpatch_jmp(dbt->code_buf,
                                   dbt->patches[patch_index].jmp_offset,
                                   native_code);
        dbt->chain_hits++;
    }

    dbt->pending_patch_targets.erase(it);
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
    dbt->code_buf = jit_alloc(CODE_BUF_SIZE);
    if (!dbt->code_buf) {
        fprintf(stderr, "dbt: cannot allocate JIT code buffer\n");
        dbt->cache.clear();
        return -1;
    }

    // Emit trampoline at the start of the code buffer.
    dbt_backend_emit_trampoline(dbt);
    dbt_flush_code(dbt, 0);

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
        dbt->pending_patch_targets.clear();
        dbt->code_used = dbt->blob_code_end;

        // Optional NOP sled for alignment experiments.
        // TINYMUX_DBT_PAD=N inserts N bytes of NOP padding before
        // program code.  On x86-64, each NOP is 1 byte (0x90).
        // On AArch64, NOPs are 4 bytes so N is rounded up to a
        // multiple of 4.
        {
            static int pad = -1;
            if (pad < 0) {
                const char *env = getenv("TINYMUX_DBT_PAD");
                pad = env ? atoi(env) : 0;
            }
            uint32_t p = static_cast<uint32_t>(pad);
#if defined(__aarch64__)
            // AArch64 NOP: 0xD503201F (4 bytes each).
            uint32_t n_nops = (p + 3) / 4;
            uint32_t pad_bytes = n_nops * 4;
            if (n_nops > 0 && dbt->code_used + pad_bytes < CODE_BUF_SIZE) {
                for (uint32_t i = 0; i < n_nops; i++) {
                    uint32_t nop = 0xD503201F;
                    memcpy(dbt->code_buf + dbt->code_used + i * 4, &nop, 4);
                }
                dbt->code_used += pad_bytes;
            }
#else
            // x86-64 NOP: 0x90 (1 byte each).
            if (p > 0 && dbt->code_used + p < CODE_BUF_SIZE) {
                memset(dbt->code_buf + dbt->code_used, 0x90, p);
                dbt->code_used += p;
            }
#endif
        }

        // Keep intrinsics — they're blob-related.
        dbt_flush_code(dbt, dbt->blob_code_end);
    } else {
        // No blob — full reset.
        for (auto &entry : dbt->cache) {
            entry = {};
        }
        dbt->patches.clear();
        dbt->pending_patch_targets.clear();
        dbt->code_used = 0;
        dbt_backend_emit_trampoline(dbt);
        dbt->num_intrinsics = 0;
        memset(dbt->intrinsics, 0, sizeof(dbt->intrinsics));
        dbt_flush_code(dbt, 0);
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
                && dbt_resolve_direct_jalr_target(scan_pc, si, next_si,
                                                   &target, &return_pc)
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
        if (!dbt_cache_lookup(dbt, pc)) {
            uint8_t *code = dbt_backend_translate_block(dbt, pc);
            if (!code) continue;
            dbt_cache_insert(dbt, pc, code);
            dbt_backpatch_chains(dbt, pc, code);
        }
    }

    // Flush I-cache for all code generated during blob pretranslation.
    dbt_flush_code(dbt, 0);
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
            dbt->pending_patch_targets.erase(target);
            already_ok++;
            continue;  // already resolved by backpatch_chains
        }

        block_entry_t *be = dbt_cache_lookup(dbt, target);
        if (be) {
            dbt_backend_backpatch_jmp(dbt->code_buf, jmp_off,
                                       be->native_code);
            dbt->chain_hits++;
            resolved++;
            dbt->pending_patch_targets.erase(target);
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

    // Flush I-cache for any backpatched JMP targets.
    if (resolved > 0) {
        dbt_flush_code(dbt, 0);
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
        block_entry_t *be = dbt_cache_lookup(dbt, pc);
        uint8_t *code;
        if (be) {
            code = be->native_code;
            if (dbt->trace & DBT_TRACE_EXEC) {
                fprintf(stderr, "[dbt] disp=%llu HIT  pc=0x%llX\n",
                        static_cast<unsigned long long>(dispatch_count),
                        static_cast<unsigned long long>(pc));
            }
        } else {
            code = dbt_backend_translate_block(dbt, pc);
            if (!code) {
                dbt->dispatch_count = dispatch_count;
                return -1;  // code buffer full
            }
            dbt_cache_insert(dbt, pc, code);

            // Backpatch any chained exits that were waiting for this block.
            dbt_backpatch_chains(dbt, pc, code);

            // Flush I-cache for newly generated and backpatched code.
            dbt_flush_code(dbt, static_cast<uint32_t>(code - dbt->code_buf));

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

int dbt_resume(dbt_state_t *dbt, uint64_t entry_pc) {
    typedef void (*trampoline_fn_t)(rv64_ctx_t *ctx, uint8_t *mem,
                                     void *block, void *cache);
    trampoline_fn_t trampoline =
        reinterpret_cast<trampoline_fn_t>(static_cast<void *>(dbt->code_buf));

    dbt->ctx.next_pc = entry_pc;
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
        block_entry_t *be = dbt_cache_lookup(dbt, pc);
        uint8_t *code;
        if (be) {
            code = be->native_code;
            if (dbt->trace & DBT_TRACE_EXEC) {
                fprintf(stderr, "[dbt] disp=%llu HIT  pc=0x%llX\n",
                        static_cast<unsigned long long>(dispatch_count),
                        static_cast<unsigned long long>(pc));
            }
        } else {
            code = dbt_backend_translate_block(dbt, pc);
            if (!code) {
                dbt->dispatch_count = dispatch_count;
                return -1;  // code buffer full
            }
            dbt_cache_insert(dbt, pc, code);

            // Backpatch any chained exits that were waiting for this block.
            dbt_backpatch_chains(dbt, pc, code);

            // Flush I-cache for newly generated and backpatched code.
            dbt_flush_code(dbt, static_cast<uint32_t>(code - dbt->code_buf));

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
    if (dbt->code_buf) {
        jit_free(dbt->code_buf, CODE_BUF_SIZE);
        dbt->code_buf = nullptr;
    }
    dbt->cache.clear();
    dbt->cache.shrink_to_fit();
    dbt->patches.clear();
    dbt->patches.shrink_to_fit();
    dbt->pending_patch_targets.clear();
}
