# Design: LBUF Architecture for the JIT

## Status: Draft (2026-03-15)

## Problem Statement

LBUFs are the fundamental unit of data in TinyMUX.  Every function call,
every attribute evaluation, every queue entry, every register value — all
flow through 8000-byte LBUFs.  The current JIT maps them to fixed guest
memory slots (10 x 8KB), which caps expression complexity and wastes
space.  This document designs an LBUF architecture for the JIT that
eliminates fixed slot limits and respects three key realities:

1. **Memory bandwidth is the wall.**  Copying 8KB buffers is expensive.
   The fastest LBUF operation is the one that never copies.

2. **LBUFs have three distinct lifetimes:**
   - *Ephemeral* — allocated, touched at [0] with '\0', filled, consumed,
     freed.  Stack-like.  Millions per second.
   - *Packed* — finalized, packed together with other small values,
     reference-counted, never copied again.  The `reg_ref` pattern.
   - *Queued* — attached to a command queue entry, may not execute for
     seconds or hours.  Heap-like.

3. **The goal is to eliminate fixed limits.**  No LBUF_SIZE.  No
   invocation depth limit.  No recursion limit.  The only constraints
   are memory and time budgets.  If a user's expression can complete
   within budget, it should work.

## Current State

### Host Side (alloc.cpp, eval.cpp)

**Pool allocator:** Free-list backed by heap.  `alloc_lbuf()` pops from
free list (O(1)), `free_lbuf()` pushes back.  No fixed pool size.

**reg_ref / lbuf_ref (the packing pattern):**
- `lbuf_ref`: refcount + pointer to one 8KB LBUF.
- `reg_ref`: refcount + pointer into an lbuf_ref's data + length.
- `RegAssign()` batches: multiple small register values are packed
  sequentially into a single LBUF.  One LBUF, many reg_refs pointing
  into it at different offsets.
- When the last reg_ref releases, the lbuf_ref's refcount hits zero,
  and the LBUF returns to the pool.

**Queue persistence:** `BQUE` entries hold `reg_ref*` pointers and call
`RegAddRef()`.  The reg_ref (and its backing LBUF) stays alive until the
queue entry executes and calls `RegRelease()`.

### Guest Side (JIT, dbt_compile.cpp)

**Fixed layout:**
```
0x0000 - 0x0FFF   Code (4 KB)
0x1000 - 0x3FFF   String pool (12 KB)
0x4000 - 0x7FFF   fargs arrays (16 KB)
0x8000 - 0xFFFF   Output slots 0-3 (32 KB, 4 x 8 KB)
0x10000 - 0x2FFFF  Tier 2 blob (128 KB)
0x30000 - 0x3BFFF  Output slots 4-9 (48 KB, 6 x 8 KB)
```

10 output slots.  Each ECALL or Tier 2 function call consumes one slot
for its result buffer.  Nested expressions deeper than 10 exhaust the
slots and bail to the AST evaluator.

**Problem:** A real-world expression like `cat(first(X),mid(Y,0,3),add(1,2))`
uses 3 slots for the inner calls plus 1 for the cat.  Complex softcode
routinely nests 15-20 function calls.  The 10-slot limit forces frequent
AST fallback.

## Design

### Principle: Zero-Copy Pipeline

An LBUF's lifecycle in the JIT should mirror the host's fast path:

1. **Allocate** — touch [0] with '\0'.  As fast as bumping a pointer.
2. **Fill** — function writes into it.  The only copy.
3. **Consume** — another function reads it.  No copy.
4. **Finalize** — if the value must persist (register, queue), pack it
   into a shared arena.  Refcount the arena.  Never copy the data again.
5. **Release** — refcount drops to zero, arena space is reclaimed.

### Three Tiers of Guest Buffer Management

#### Tier A: The Scratch Ring (Ephemeral)

For the hot path — function call results that are consumed immediately
and discarded.

- A **ring buffer** in guest memory, sized generously (e.g., 256 KB).
- Each function call bumps a `scratch_head` pointer to claim space.
- When the caller consumes the result, the space is logically freed.
- No per-buffer metadata.  No refcount.  No free list.  Just a pointer.

The ring works because ephemeral buffers follow strict stack discipline:
inner calls produce results that outer calls consume before producing
their own results.  The ring pointer advances monotonically during
compilation; at runtime, the same addresses are reused across calls.

If a compiled expression would exceed the ring, compilation fails and
falls back to AST — same as today, but the threshold is 256 KB instead
of 80 KB, allowing ~30 concurrent buffers instead of 10.

This is the **minimum viable change** that unblocks deeper expressions.

#### Tier B: Arenas (Packed / Register)

For values that outlive a single expression — register assignments,
function return values passed across queue boundaries.

- An **arena** is a contiguous block of host memory (e.g., 64 KB).
- The guest writes finalized values into the arena via an ECALL.
- Multiple values pack sequentially, exactly like today's reg_ref
  batching into an lbuf_ref.
- The arena has a **refcount** (number of live references into it).
- When all references release, the arena returns to a free list.

**ECALL interface:**
```
ECALL_ARENA_ALLOC(size) → arena_id, offset
    Allocate space in the current arena.  If the arena is full,
    seal it and start a new one.

ECALL_ARENA_REF(arena_id) → void
    Increment refcount.

ECALL_ARENA_RELEASE(arena_id) → void
    Decrement refcount.  If zero, reclaim.
```

The guest never manages arena memory directly.  It requests space,
writes data, and the host handles lifetime.  This is the reg_ref
pattern, but the guest participates in the protocol.

**Garbage collection:** Sealed arenas with refcount zero are reclaimed.
No compaction needed — arenas are append-only until sealed, then
immutable.  Fragmentation is bounded by arena size.

#### Tier C: DMA Windows (Large / Queue)

For large values (future: beyond 8 KB) and for transferring data between
guest and host without copying.

Inspired by the slow-32 MMIO architecture: a shared memory region that
both guest and host can access directly.

- **DMA windows** are host-allocated buffers mapped into the guest's
  address space at known offsets.
- The guest writes into the window; the host reads from it (or vice
  versa) without any copy.
- A small **descriptor ring** (like slow-32's request/response rings)
  coordinates ownership:

```
Guest:  "I've written 4200 bytes into window 3."
         → descriptor: { window: 3, length: 4200, op: FINALIZE }
         → ECALL_DMA_SUBMIT

Host:   Reads data directly from window 3's mapped address.
        Packs into an arena or attaches to a queue entry.
        Marks window 3 as free.
         → descriptor: { window: 3, op: ACK }
```

- Window count is small (4-8) but each window can be large (64-128 KB).
- The guest reuses windows in a ring pattern.

This tier is the path to eliminating LBUF_SIZE.  A DMA window can hold
128 KB.  If the guest needs more, it streams: fill window, submit,
fill again.  The host reassembles.

### Guest Memory Map (Proposed)

```
0x00000 - 0x00FFF   Code (4 KB)
0x01000 - 0x03FFF   String pool (12 KB)
0x04000 - 0x07FFF   fargs (16 KB)
0x08000 - 0x0FFFF   SUBST/CARGS slots (32 KB)
0x10000 - 0x2FFFF   Tier 2 blob (128 KB)
0x30000 - 0x6FFFF   Scratch ring (256 KB) ← Tier A
0x70000 - 0x7FFFF   DMA windows (64 KB, 4 x 16 KB) ← Tier C
0x80000 - 0x8FFFF   Descriptor rings + control (4 KB) ← Tier C
0xF0000 - 0xFFFFF   Stack (64 KB)
```

Total: ~1 MB guest memory.  Arenas (Tier B) live on the host side and
are accessed only through ECALLs.

### ECALL Convention (Extended)

Existing ECALLs (a7 values):
```
0x093   ECALL_EXIT
0x100   ECALL_CALL_FUNC (by name)
0x101   ECALL_CALL_INDEX (by index)
```

New ECALLs:
```
0x110   ECALL_ARENA_ALLOC    a0=size → a0=arena_id, a1=offset
0x111   ECALL_ARENA_REF      a0=arena_id
0x112   ECALL_ARENA_RELEASE  a0=arena_id
0x120   ECALL_DMA_SUBMIT     a0=window, a1=length, a2=op
0x121   ECALL_DMA_ACK        → a0=window (next free)
0x130   ECALL_SETQ_PACK      a0=reg_num, a1=guest_addr, a2=len
        Pack a guest buffer into the current arena and bind to %q<N>.
        Combines arena_alloc + memcpy + register assignment in one call.
```

`ECALL_SETQ_PACK` is the critical fast path.  Today, `HIR_SETQ_SYNC`
writes a value to both the guest SUBST slot and the host's mudstate
register.  With arenas, it would pack the value into an arena (one
host-side memcpy), create a reg_ref pointing into the arena, and assign
the register — all in a single ECALL crossing.

### Interaction with Existing reg_ref

The host side of arenas IS the reg_ref/lbuf_ref system, repackaged:

| Current              | Proposed              |
|----------------------|-----------------------|
| `lbuf_ref`           | Arena                 |
| `lbuf_ref.refcount`  | Arena refcount        |
| `reg_ref`            | Arena slot reference  |
| `RegAssign` batching | Arena sequential pack |
| `BufRelease`         | `ECALL_ARENA_RELEASE` |

An arena is simply an lbuf_ref with a larger backing buffer (64 KB
instead of 8 KB) and explicit guest-side protocol.  The host allocator
doesn't change — arenas come from the same heap pool.

### Maximum Buffer Size

With arenas and DMA windows, the hard 8000-byte LBUF_SIZE limit can
grow.  Candidates:

- **64 KB** — fits in one DMA window, reasonable for any single attribute
  or function result.
- **128 KB** — matches arena size, allows a single value to fill an arena.
- **Configurable** — SQLite stores BLOBs; the practical limit is what
  moves between the game and the database.

For compiled code, the source text size matters less — the compiled
form is compact.  The limit that matters is the **output** size of a
single function evaluation.

### Garbage Collection

Arenas are append-only and immutable once sealed.  No compaction.
Reclamation is a simple free-list return when refcount hits zero.

**When does refcount drop?**

- Ephemeral values (Tier A scratch ring): no refcount, no GC.
- Register values (Tier B arenas): released when register is overwritten
  or when the enclosing localize/letq scope exits.
- Queued values: released when the queue entry executes or is canceled.

**Compaction scenario:** If many small values are packed into one arena,
and most are released but one holds the arena alive, memory is wasted.
Mitigation: keep arenas small enough (64 KB) that the waste is bounded,
and prefer packing values with similar lifetimes into the same arena
(e.g., all registers from one evaluation frame go into one arena).

## Migration Path

### Phase 0: Expand Scratch Ring (Immediate)

Replace the current 10-slot output layout with a 256 KB scratch ring.
No new ECALLs.  No arenas.  Just more room.

- Move output region to 0x30000-0x6FFFF (256 KB).
- `alloc_output()` bumps a pointer instead of indexing fixed slots.
- Same bail-to-AST on overflow, but threshold is ~30 buffers not 10.
- Tier 2 blob stays at 0x10000-0x2FFFF.
- All existing tests should pass unchanged.

### Phase 1: Arena ECALLs (Register Fast Path)

Add `ECALL_SETQ_PACK` to replace the current SETQ_SYNC write-through.

- Guest computes a value in the scratch ring.
- `ECALL_SETQ_PACK` copies it into a host-side arena and binds the
  register — one ECALL, one memcpy.
- `restore_global_regs` works as before (refcount-based).
- This makes `localize()` and `letq()` feasible as JIT natives.

### Phase 2: DMA Windows (Large Values)

Add DMA windows for values larger than the scratch ring can hold.

- Guest writes into a DMA window.
- `ECALL_DMA_SUBMIT` transfers ownership to the host.
- Host packs into an arena or consumes directly.
- This eliminates the 8 KB per-buffer ceiling.

### Phase 3: Variable Buffer Size

Remove LBUF_SIZE as a compile-time constant.  Buffer size becomes a
runtime property of each allocation.  The pool allocator gains size
classes (8 KB, 64 KB, etc.) or switches to a slab allocator.

### Phase 4: Budget-Based Limits

Replace fixed invocation/recursion limits with memory + time budgets.
Each evaluation context has:

- `max_memory`: total arena bytes allocated.
- `max_time`: wall-clock microseconds.

When either budget is exceeded, evaluation halts with an error.
No arbitrary depth limits.  No fixed buffer counts.

## Open Questions

1. **Scratch ring vs. stack allocator?**  The ring is simpler, but a
   true stack allocator (with explicit push/pop) might reclaim space
   more aggressively in deeply nested expressions.  The ring relies on
   the assumption that buffers are consumed in roughly LIFO order.

2. **Arena size?**  64 KB balances waste vs. syscall frequency.  Needs
   benchmarking.  Smaller arenas (16 KB) reduce waste from long-lived
   stragglers.  Larger arenas (128 KB) reduce allocation overhead.

3. **DMA window count?**  4 windows at 16 KB each is conservative.
   Could grow to 8 windows or use variable-sized windows.

4. **Should the Tier 2 blob move?**  It currently occupies 0x10000-
   0x2FFFF (128 KB) in the middle of guest memory, creating a gap that
   complicates layout.  Moving it to high memory (0xC0000+) would give
   the scratch ring a contiguous region.

5. **Thread safety?**  Not a concern today (single-threaded execution),
   but if the engine ever runs evaluation on multiple threads, arenas
   need per-thread or lock-free allocation.
