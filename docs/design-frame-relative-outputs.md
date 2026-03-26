# Frame-Relative Output Addressing — Design & Status

## Problem Statement

The JIT compiler allocates output buffers on a downward-growing stack in
guest memory.  Each compiled function's prologue decrements SP by its
frame size, and output slots live within that frame.  For single-expression
evaluation this works: SP starts at STACK_TOP, the function runs, results
are read from the known absolute address.

For the persistent VM (PVM), multiple compiled functions share one guest
memory image.  Re-entrant calls (ECALL_CALL_COMPILED) nest: an outer
function calls an inner function which gets its own stack frame below the
outer's.  Output buffers must not collide between frames, and the C++
ECALL handler must know where each function's result actually landed.

## Visualizing the Memory Layout

### Single Expression (The Happy Path)
```
STACK_TOP (0x100000)   <--- Entry SP = s0
  |  - delta_1         <--- Output Slot 1
  |  - delta_2         <--- Output Slot 2
  v                    <--- Exit SP (after prologue sub sp, sp, frame_size)
```
Resolution: `s0 - delta` = `0x100000 - delta` (correct absolute address)

### Re-entrant Call (The Broken Path)
```
STACK_TOP (0x100000)   <--- Outer Entry SP = s0_outer
  |  - delta_out_1     <--- Outer Output Slot
  |  - ...
  |  - delta_inner     <--- Inner's Output (incorrectly resolved via s0_outer!)
  |  --- Frame 1 Boundary ---
  |                    <--- Outer Exit SP
  |  --- Frame 2 Entry ---
  v                    <--- Inner Entry SP = s0_inner
     |  - delta_inner  <--- Inner's ACTUAL Output Slot
     v                 <--- Inner Exit SP
```
The bug: The outer function resolves the inner's output address using its own `s0_outer`.
This places the result in the *outer's* frame, but the inner function writes into its
*own* frame below the outer's.

## What We Built (on master, working, switch OFF)

All infrastructure for frame-relative output addressing is committed and
passing 677 smoke tests.  The switch is off — `alloc_output()` returns
absolute addresses, and all the frame-relative machinery is a no-op.

### A1. Register reassignment
`RA_SCRATCH` moved from s0 (x8) to s11 (x27).  s0 is now `RA_FRAME_TOP`,
reserved for capturing entry SP.  Allocatable register count dropped from
11 to 10.

Commit: `2d828efc0`

### A2. Tagging helpers
`OUT_FRAME_TAG` (bit 31), `is_output_frame_ref()`, `output_frame_delta()`,
`make_output_frame_ref()`, `resolve_output_addr()` added to `rv_compiler`
in `dbt_compile.h`.

Commit: `067a1880e`

### A3. Prologue change
`mv s0, sp` emitted before the stack frame setup NOPs.  Captures entry SP
into s0 so frame-relative deltas can be resolved at runtime.

Commit: `c8101595a`

### A4. rv_load_guest_addr + ~30 call-site swaps
New codegen helper that checks `is_output_frame_ref()` and emits
`SUB rd, s0, delta` for tagged addresses, falling through to `rv_load_val`
for absolute ones.  All output/string/fargs address loads in
`hir_codegen.cpp` swapped to use it.

Commit: `74fea6929`

### A5. rv_patch_fargs
Runtime patching of fargs arrays before Tier 2 JAL calls.  For each
tagged fargs entry, emits code to resolve it via s0 and store the
absolute address back.

Commit: `ab3b967c0`

### A7. Resolve sites in jit_compiler.cpp
`resolve_runtime_out_addr()` wrapper added.  All C++ sites that read
`compiled_program.out_addr` or `compile_result.out_addr` now resolve
through it.  ECALL handlers updated: `ecall_invoke_fun()` resolves
frame-relative fargs pointers, both `eval_ecall` and `poc_ecall`
ECALL_CALL_COMPILED handlers resolve output refs.

Commit: `f7014e1b7`

## What Broke (A6 — the switch)

Commit `23897e9fe` flipped `alloc_output()` to return
`make_output_frame_ref(addr)` and added the companion PVM output reset
(each compilation starts output at STACK_TOP-8).  This was reverted by
`0ff7f43bc` after bisect confirmed it caused a busy-hang in pocvm2 tests.

## Root Cause Analysis

The bug is in how `ECALL_CALL_COMPILED` interacts with frame-relative
addressing.  The problem manifests only in re-entrant PVM calls, not in
single-expression evaluation.

### Why the addresses diverge

The tagged output address encodes `delta = STACK_TOP - abs_addr`.
`rv_load_guest_addr` resolves at runtime: `s0 - delta`.

- In the outer function: `s0 = STACK_TOP`, so result = `abs_addr`.
- In the inner function: `s0 < STACK_TOP`, so it writes elsewhere.

The caller resolves the callee's output address using the caller's `s0`, but
the callee will use its own `s0`.  The address is resolved too early — before
the callee establishes its own frame.

## The Solution: Option 1

The cleanest fix is to **not** resolve the output address in the codegen for
`ECALL_CALL_COMPILED`. Instead, pass the raw tagged value (the token) and let the
ECALL handler resolve it once the inner function's entry SP is known.

### Proposed Implementation Slice

1.  **Re-enable Tagging**: Flip `alloc_output()` to return `make_output_frame_ref(addr)`.
2.  **Add Raw Token Loader**: Create a codegen helper (e.g., `rv_load_raw_ref`) that always uses `rv_load_val` (ignoring the tag bit).
3.  **Targeted Emission**: Use this helper only at the `ECALL_CALL_COMPILED` emission site (currently in `fun_pocvm2` and eventually in Tier 3 `u()` inlining).
4.  **Confirm Handler Resolution**: Ensure `eval_ecall` and `poc_ecall` continue using `resolve_runtime_out_addr(out_ref, saved_ctx.x[2])`. `saved_ctx.x[2]` is exactly the entry SP for the callee.

That keeps the representation split clean:

- **Intra-function uses**: resolve via emitted code against the current frame's `s0`.
- **Cross-function compiled-call boundary**: transport the tagged token unchanged.
- **Runtime handoff after the inner returns**: resolve once against the callee's entry SP.

## Validation Plan

- **Single-expression JIT**: frame-relative outputs resolve correctly.
- **Normal Engine ECALLs**: `ECALL_CALL_FUNC` / `ECALL_CALL_INDEX` still receive absolute pointers.
- **Re-entrant Compiled Call**: inner output lands correctly below the outer frame.
- **Nested depth > 1**: No aliasing between buffers at each level.

## Reference

- Broken branch: `broken` — contains the original (failing) implementation
- Bisect result: `23897e9fe` is the first bad commit
- All A1-A5, A7 infrastructure is on master and passing
- Current tree paths:
  `mux/include/dbt_compile.h` for tagging helpers and `alloc_output()`
  `mux/modules/engine/hir_codegen.cpp` for `rv_load_guest_addr`,
  `rv_patch_fargs`, and the prologue
  `mux/modules/engine/jit_compiler.cpp` for resolve sites, ECALL handlers,
  and PVM compile/install
