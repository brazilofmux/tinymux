# Inline CALL Cold-Exit Problem

## Status
Active investigation. 593/593 smoke tests pass. Performance is good
for non-iter expressions (10-19x faster than native for constant-folded,
parity for Tier 2 string ops). Iter has a per-element dispatch overhead
from inline CALL cold-exit failures.

## Architecture

The DBT translates RV64 guest code to x86-64 native code. Functions
in the "blob" (pre-compiled softlib.rv64) provide Tier 2 string
operations (WORDS, SPLIT_TOKEN, STRCAT, etc.).

**Inline CALL mechanism**: When a program block has a JAL ra (function
call) to a blob function, and the blob entry is in the block cache,
the translator emits an x86 `CALL` to the blob's native code instead
of exiting to the dispatch loop. After the callee RETs, the hot-path
check verifies `ctx.next_pc == expected_return_addr`. If match, the
block continues. If mismatch, the cold exit fires (RETs to dispatch).

## The Problem

For `iter(a b c d e, X)`, each element produces 2 dispatches:

```
disp=4 HIT pc=0x1096C    (blob: SPLIT_TOKEN return block)
disp=5 HIT pc=0x140      (program: after SPLIT_TOKEN)
```

The cold-exit diagnostic shows `ce_actual=0x1096C` on every failure.
The expected value is `0x140` (program's return address).

## What We Know

### Block Structure

The program calls SPLIT_TOKEN (blob entry 0x10820) via inline CALL.
The blob entry block (0x10820, 6 instructions) branches to internal
blocks. Eventually reaches block 0x1094C (21 instructions):

```
#1-#7:   Argument setup (MV registers)
#8:      JAL ra, 0x15C7C (inner call to co_split_token intrinsic)
         → INLINE CALL fires (target in cache)
         → Intrinsic runs, sets next_pc=0x1096C, RETs
         → Hot-path check: next_pc==0x1096C → MATCH → continues
#10-#12: Process result, store output
#13:     LD ra, 56(sp)   ← RESTORES original ra (0x140) from stack
#14-#20: Restore callee-saved regs, pop stack frame
#21:     JALR x0, ra, 0  ← Function return (should set next_pc=0x140)
```

The JALR at #21 terminates the block via:
1. `rc_read(rs1=ra)` → reads restored ra (0x140) from register cache
2. `emit_store_next_pc(RCX)` → stores 0x140 to ctx.next_pc
3. `rc_flush` → writes all cached registers to ctx
4. `emit_exit_indirect(RCX)` → stores next_pc=RCX, RETs

After the RET, the program's inline CALL continuation checks:
`cmp ctx.next_pc, 0x140` → should match → hot path.

**BUT the cold exit fires with actual=0x1096C.** The JALR's
`emit_store_next_pc` should have overwritten 0x1096C with 0x140.

### What We've Ruled Out

1. **Cache collisions**: 4-way set-associative cache (1024 sets × 4 ways).
   Verified no collisions between blob and program PCs.

2. **Unchained blob blocks**: All 3043/3043 chains resolved. No
   slow-path stubs executing within the blob.

3. **Pretranslation gaps**: Deep pretranslation with callee-first
   ordering and visited set. All blob blocks pretranslated.

4. **RAS prediction corruption**: Disabled RAS pop_and_probe entirely.
   Cold exit pattern unchanged.

5. **Register state corruption**: JALR handler stores next_pc BEFORE
   flush. Flush doesn't touch ctx.next_pc (offset 256, flush writes
   to x[0..31] at offsets 0-248).

6. **Wrong block executing**: Block 0x1094C's instruction dump
   confirms JALR x0, ra, 0 at insn #21. LD ra, 56(sp) at insn #13
   restores correct ra. Prologue (0x10820) saves ra at 56(sp).

### RAS Probe: Not the Cause of This Symptom

Disabling emit_ras_pop_and_probe globally did not change the
0x1096C cold-exit pattern — the probe is not the cause of THIS
symptom. However, the probe remains architecturally unsafe for
inline-call contexts: on a hit, it JMPs directly to the predicted
block, bypassing emit_exit_indirect's RET. This breaks the x86
CALL/RET contract that inline CALL callers depend on. The probe
is currently disabled pending a context-aware mechanism.

### Open Questions

1. **Is the JALR at insn #21 actually executing?** Something might
   cause the block to exit BEFORE reaching insn #21. The block has
   21 instructions, no branches between #10 and #21, and
   `side_exits=1` (only the inner inline CALL's cold exit). But an
   implicit exit from a fusion or instruction handler could terminate
   the block early.

2. **Is `emit_store_next_pc` actually writing 0x140?** The LD at
   insn #13 loads from guest memory [sp+56]. If sp is wrong or the
   saved value was corrupted, ra could be wrong. But the prologue
   saves and epilogue restores using the same offset (56).

3. **Is the x86 CALL/RET pairing correct?** The inline CALL pushes
   a continuation on the x86 stack. The callee's JALR does
   exit_indirect → RET. The RET should pop the continuation. If
   something between the CALL and the final RET consumes the
   continuation (extra RET from an unchained exit), the cold exit
   fires. All 3043/3043 chains verified resolved, so no slow-path
   stubs should execute.

4. **Could there be a SECOND exit path in the block?** The block
   has `side_exits=1` (from the inner inline CALL). If the inner
   inline CALL's hot-path check fires correctly, the block continues.
   But what if the block has an IMPLICIT exit from one of the
   fusions or instruction handlers?

5. **ANSWERED: The cold exit is from the OUTER inline CALL.**
   `cold_exit_expected` reports `e=0x140` (program return address),
   confirming the OUTER program→blob inline CALL's cold exit fires.
   The inner blob→intrinsic inline CALL succeeds (hot path).

6. **ANSWERED: 0x1096C cold exit SOLVED.** The leaf function at
   0x11E78 (satoi/sitoa epilogue) was called via JMP (non-inline)
   from within the SPLIT_TOKEN wrapper. Its JALR RET stole the
   outer inline CALL's x86 continuation. Fixed by scanning past
   JAL rd=1 in the pretranslation worklist, which discovers all
   call targets in a block (not just the first). Result: disp
   dropped from 2N+4 to constant 4.

7. **Remaining: WORDS cold exit.** `ce=7(a=0x107C4,e=0x14,from=0x11E88)`.
   Same leaf function (0x11E88), but from the WORDS call chain.
   The sitoa intrinsic at 0x158A8 is translated AFTER its caller
   block (0x107B4) — a translation-order race where a parent block
   absorbs 0x107B4 via superblock extension before the worklist
   processes it. The inline CALL check sees found=0 on first
   translation, found=1 on subsequent. This costs 2 constant
   dispatches (not per-element).

## Current State

Dispatch count: constant 4 (was 3N+5, then 2N+4).
Remaining cold exits: 2 per iter call (WORDS chain).
Pattern: `disp=1 pc=0x0 | disp=2 pc=0x107C4 | disp=3 pc=0x14 | disp=4 ECALL`.

## Reproduction

```sh
cd testcases
./tools/BuildAndSmoke
grep '^BENCH035' smoke.log   # ce=N(a=0xACTUAL,e=0xEXPECTED,from=0xFROM)
```

## Key Files

- `mux/modules/engine/dbt.cpp` — translate_block, inline CALL, JALR handler
- `mux/modules/engine/dbt_compile.cpp` — pretranslate_tier2, rvbench
- `mux/include/dbt.h` — dbt_state_t, block_entry_t, cache constants
- `mux/include/dbt_emit_x64.h` — x86-64 emitter functions
- `mux/include/dbt_decoder.h` — RV64 instruction decoder

## What Would Help

- A GDB session with a breakpoint at the cold-exit x86 code, examining
  the x86 stack and ctx.next_pc at the moment of failure
- Disassembly of the JIT-generated x86 code for block 0x1094C to verify
  the instruction sequence matches expectations
- A counter in emit_exit_indirect that prints when it fires with
  next_pc=0x1096C, showing the x86 return address (to identify which
  exit_indirect is the one that fires)
