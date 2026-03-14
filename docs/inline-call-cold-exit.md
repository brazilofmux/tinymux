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

### Open Questions

1. **Is the JALR at insn #21 actually executing?** Something might
   cause the block to exit BEFORE reaching insn #21. But the block
   has 21 instructions, no branches between #10 and #21, and
   `side_exits=1` (only the inner inline CALL's cold exit).

2. **Is `emit_store_next_pc` actually writing 0x140?** The LD at
   insn #13 loads from guest memory [sp+56]. If sp is wrong or the
   saved value was corrupted, ra could be wrong. But the prologue
   saves and epilogue restores using the same offset (56).

3. **Is the x86 CALL/RET pairing correct?** The inline CALL pushes
   a continuation on the x86 stack. The callee's JALR does
   exit_indirect → RET. The RET should pop the continuation. If
   something between the CALL and the final RET consumes the
   continuation (extra RET from an unchained exit, RAS probe JMP,
   etc.), the cold exit fires.

4. **Is the intrinsic stub's `emit_exit_indirect` RETs to the right
   place?** The intrinsic at 0x15C7C is called via the INNER inline
   CALL at insn #8. Its `emit_intrinsic_return` loads ra from ctx
   and does exit_indirect → RET. This RET should pop the inner
   inline CALL's continuation (within block 0x1094C). The hot-path
   check passes (next_pc == 0x1096C). Then the block continues to
   insn #10.

5. **Could there be a SECOND exit path in the block?** The block
   has `side_exits=1` (from the inner inline CALL). If the inner
   inline CALL's hot-path check fires correctly, the block continues.
   But what if the block has an IMPLICIT exit from one of the
   fusions or instruction handlers?

## Reproduction

```sh
cd testcases
TINYMUX_DBT_TRACE=exec TINYMUX_DBT_MAX_DISPATCH=50 \
  ./tools/BuildAndSmoke
grep 'disp=.*pc=0x1096C' netmux.log | tail -5
```

The cold-exit count appears in benchmark output as `ce=N(0x1096C)`.

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
