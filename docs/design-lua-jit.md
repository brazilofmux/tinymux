# Lua Bytecode → JIT: Phase 2 Specification

## 1. Scope

This document specifies the precise lowering of Lua 5.4 bytecode into the
existing HIR/RV64/DBT pipeline. It addresses the design constraints raised
in the Phase 2 review: register state ownership, upvalue/closure handling,
table mutation visibility, generic calls, and coroutine interaction.

## 2. Compilation Unit

The compilation unit is a single **Lua Proto** (function prototype). Each
Proto contains a flat array of bytecode instructions, a constant pool, an
upvalue descriptor list, and nested Proto references.

Compilation is **method-at-a-time**, not trace-based. The entire Proto is
lowered to HIR in one pass. This matches the existing softcode JIT model
(one expression → one compiled_program).

### 2.1 Eligibility

A Proto is eligible for JIT compilation when:

- `numparams` ≤ 8 (register pressure bound)
- `maxstacksize` ≤ 64 (fits in guest memory LUA_STACK region)
- `is_vararg` == 0 (varargs require dynamic frame sizing)
- No `OP_CLOSURE` (nested closures require GC integration)
- No `OP_VARARG`, `OP_VARARGPREP`
- No `OP_TBC` (to-be-closed requires finalizer integration)
- No `OP_TAILCALL` (requires frame manipulation)

Ineligible Protos run in the stock Lua VM. This is not a fallback — the VM
is the baseline, and JIT is an optimization for qualifying functions.

## 3. Type Model

### 3.1 The Problem

Lua is dynamically typed. Every Lua register holds a `TValue` — a tagged
union of nil, boolean, integer, float, string, table, function, userdata.
The HIR has two types: `TY_INT` (64-bit integer in RV64 register) and
`TY_STRING` (guest memory address).

The JIT must specialize on types to produce useful code.

### 3.2 Strategy: Integer-Specialized Compilation

**Assumption:** For the Lua code worth JIT-compiling (numeric loops, array
processing, arithmetic), the hot registers are integers. This is the same
assumption LuaJIT makes, and it matches the MUX use case (dice rolls, stat
calculations, combat math, iteration counters).

**Approach:** Compile the entire Proto assuming integer types for all
arithmetic operands. Emit **type guards** at function entry for parameters
and at ECALL return sites. If a guard fails at runtime, abandon JIT
execution and restart the call in the Lua VM.

### 3.3 Type Guard Mechanism

A type guard checks that a Lua register holds an integer:

```
; Guest memory layout: LUA_REGS[r] = { value: i64, tag: u8 }
; Tag check: LUA_TAG_INT = 3  (LUA_VNUMINT & 0x0F)
LOAD_BYTE  tag, LUA_REGS_BASE + r * 9 + 8
BRC        tag == LUA_TAG_INT, continue_block, bailout_block
```

The bailout block sets a flag in the eval_ctx and returns ECALL_EXIT with
a "type guard failed" code. The caller (lua_jit_eval) then falls back to
`lua_pcall()` on the same Proto with the original arguments.

### 3.4 String Handling

Lua strings are GC-managed objects. The JIT cannot store raw string
pointers in guest memory (the GC may relocate them). Two options:

**Option A (Phase 2):** Strings always go through ECALL. Any operation
that produces or consumes a Lua string (OP_CONCAT, OP_GETFIELD, string
library calls) is an ECALL into the Lua VM. The JIT only optimizes
integer paths.

**Option B (Phase 3+):** Copy string bytes into guest memory string pool
(STR_BASE). Pin GC objects during JIT execution via `lua_gc(L,
LUA_GCSTOP)`. Copy results back on exit. This is complex and deferred.

Phase 2 uses Option A.

## 4. Register State Ownership

This is the core architectural question. The answer must be unambiguous.

### 4.1 Two Register Files

During JIT execution, there are **two** register files:

| Register file | Location | Owner | Contents |
|---------------|----------|-------|----------|
| Lua stack | `L->stack` (host heap) | Lua VM | TValues (tagged) |
| Guest regs | `LUA_REGS_BASE` (guest memory) | JIT | Unboxed int64 values |

The JIT operates exclusively on guest regs. The Lua stack is the
authoritative state.

### 4.2 Entry Protocol

When entering JIT execution for a Proto with N integer parameters:

```
1. For each parameter r in [0, numparams):
   a. Type-check L->stack[r].tt_ == LUA_VNUMINT
   b. If any fails → abandon, run in Lua VM
   c. guest_regs[r] = L->stack[r].value_.i

2. For each upvalue descriptor that references _ENV:
   → Mark function as ineligible (table access needed)

3. Execute JIT'd code (RV64 → x86-64 via DBT)
```

### 4.3 ECALL Protocol (JIT → Lua VM)

When the JIT hits an operation it can't handle natively:

```
ECALL_LUA_OP:
  1. Flush dirty guest regs back to Lua stack:
     For each modified register r:
       L->stack[r].value_.i = guest_regs[r]
       L->stack[r].tt_ = LUA_VNUMINT

  2. Execute the Lua operation in the VM:
     luaV_execute() for one instruction, or
     call the appropriate luaV_* / luaO_* helper

  3. Reload guest regs from Lua stack:
     For each integer-typed register r:
       if L->stack[r].tt_ != LUA_VNUMINT → bailout
       guest_regs[r] = L->stack[r].value_.i

  4. Return to JIT execution
```

### 4.4 Exit Protocol

When JIT execution completes (OP_RETURN):

```
1. Flush all live guest regs back to Lua stack
2. Set nresults in the Lua call frame
3. Return control to lua_jit_eval()
4. lua_jit_eval() returns normally, Lua sees the results on the stack
```

### 4.5 Invariant

**At all times, either the JIT or the Lua VM owns the registers, never
both.** Ownership transfers are explicit (flush/reload). There is no
concurrent mutation.

## 5. Guest Memory Layout

Extend the existing 1MB guest memory space:

```
0x84000             LUA_REGS_BASE
                    64 registers × 9 bytes (8 value + 1 tag) = 576 bytes
0x84240             LUA_CONSTS_BASE
                    Constant pool (integer constants copied from Proto->k)
                    Up to 256 × 8 bytes = 2KB
0x84A40             LUA_LOCALS_BASE
                    Additional local storage for JIT temporaries
                    4KB
0x85A40             (end of Lua region)
```

This fits within the existing 1MB address space with room to spare.

**Note:** Only integer constants are copied to guest memory. String and
table constants remain in the Lua heap and are accessed via ECALL.

## 6. ECALL Extensions

New syscall numbers for Lua operations:

```cpp
// Lua ECALL range: 0x300 - 0x3FF

// Arithmetic fallback (float or metamethod)
#define ECALL_LUA_ARITH      0x300  // a0=op, a1=ra, a2=rb, a3=rc

// Comparison with non-integer operands
#define ECALL_LUA_CMP        0x301  // a0=op, a1=ra, a2=rb, a3=k(expected)

// Table operations
#define ECALL_LUA_GETTABLE   0x302  // a0=table_reg, a1=key_reg, a2=dest_reg
#define ECALL_LUA_SETTABLE   0x303  // a0=table_reg, a1=key_reg, a2=val_reg
#define ECALL_LUA_GETFIELD   0x304  // a0=table_reg, a1=field_kidx, a2=dest_reg
#define ECALL_LUA_SETFIELD   0x305  // a0=table_reg, a1=field_kidx, a2=val_reg
#define ECALL_LUA_GETI       0x306  // a0=table_reg, a1=int_key, a2=dest_reg
#define ECALL_LUA_SETI       0x307  // a0=table_reg, a1=int_key, a2=val_reg
#define ECALL_LUA_NEWTABLE   0x308  // a0=dest_reg, a1=array_size, a2=hash_size

// String operations
#define ECALL_LUA_CONCAT     0x310  // a0=first_reg, a1=last_reg
#define ECALL_LUA_LEN        0x311  // a0=src_reg, a1=dest_reg

// Function calls
#define ECALL_LUA_CALL       0x320  // a0=func_reg, a1=nargs, a2=nresults
#define ECALL_LUA_SELF       0x321  // a0=dest_reg, a1=table_reg, a2=key_kidx

// Upvalue operations
#define ECALL_LUA_GETUPVAL   0x330  // a0=dest_reg, a1=upval_idx
#define ECALL_LUA_SETUPVAL   0x331  // a0=src_reg, a1=upval_idx
#define ECALL_LUA_GETTABUP   0x332  // a0=dest_reg, a1=upval_idx, a2=key

// Loop helpers
#define ECALL_LUA_FORPREP    0x340  // a0=ra (initializes loop vars)
#define ECALL_LUA_TFORCALL   0x341  // a0=ra, a1=nresults

// MUX bridge calls (mux.* API)
#define ECALL_MUX_NOTIFY     0x380  // a0=dbref, a1=msg_addr
#define ECALL_MUX_GET        0x381  // a0=dbref, a1=attr_addr, a2=out_addr
#define ECALL_MUX_SET        0x382  // a0=dbref, a1=attr_addr, a2=val_addr
#define ECALL_MUX_EVAL       0x383  // a0=expr_addr, a1=out_addr
#define ECALL_MUX_NAME       0x384  // a0=dbref, a1=out_addr
#define ECALL_MUX_OWNER      0x385  // a0=dbref, a1=out_dest_reg

// Bailout — abandon JIT, restart in VM
#define ECALL_LUA_BAILOUT    0x3FF  // a0=reason_code
```

### 6.1 ECALL Handler Contract

Every Lua ECALL follows this protocol:

1. **Read operand registers** from `a0..a4` — these are Lua register
   indices, not values. The handler reads actual values from the Lua
   stack (`L->stack[reg]`).

2. **Flush dirty guest regs** — before any VM operation, write back all
   modified integer registers from guest memory to the Lua stack.

3. **Execute the operation** — call the appropriate `luaV_*` helper.

4. **Reload integer results** — if the result register now holds an
   integer, write it to guest memory. If it holds a non-integer type,
   set a flag for the JIT to decide whether to bail out or continue
   with the register marked as "VM-only."

5. **Return** to JIT execution (return -1 for "continue").

### 6.2 Dirty Register Tracking

The RV64 codegen marks registers as dirty when it emits a store to
`LUA_REGS_BASE + r * 9`. The ECALL prologue flushes only dirty registers.
This is tracked as a bitmask in the eval_ctx:

```cpp
struct lua_eval_ctx : public eval_ctx {
    lua_State *L;           // Lua state
    Proto *proto;           // Current function prototype
    uint64_t dirty_regs;    // Bitmask of modified guest registers
    uint64_t int_regs;      // Bitmask of registers known to be integer
    int pc;                 // Current Lua PC (for bailout)
};
```

## 7. Opcode Lowering

### 7.1 Native (HIR) — Integer Arithmetic

These opcodes lower directly to existing HIR instructions when both
operands are known-integer:

| Lua opcode | HIR instruction | Notes |
|------------|-----------------|-------|
| OP_MOVE | HIR_COPY | Register copy |
| OP_LOADI | HIR_ICONST | Immediate integer |
| OP_LOADK | HIR_ICONST | If constant is integer |
| OP_LOADFALSE | HIR_ICONST(0) | Boolean as int |
| OP_LOADTRUE | HIR_ICONST(1) | Boolean as int |
| OP_ADD | HIR_ADD | Integer add |
| OP_SUB | HIR_SUB | Integer subtract |
| OP_MUL | HIR_MUL | Integer multiply |
| OP_IDIV | HIR_DIV | Integer division |
| OP_MOD | HIR_REM | Integer modulo |
| OP_UNM | HIR_NEG | Integer negate |
| OP_BNOT | emit NOT+1comp | Bitwise NOT (HIR_SUB from -1) |
| OP_ADDI | HIR_ADD+ICONST | Add immediate |
| OP_ADDK | HIR_ADD | Add constant |
| OP_SUBK | HIR_SUB | Subtract constant |
| OP_MULK | HIR_MUL | Multiply constant |
| OP_MODK | HIR_REM | Modulo constant |
| OP_IDIVK | HIR_DIV | Integer divide constant |
| OP_EQ | HIR_EQ | Equality (int) |
| OP_LT | HIR_LT | Less than (int) |
| OP_LE | HIR_LE | Less or equal (int) |
| OP_EQI | HIR_EQ+ICONST | Compare with immediate |
| OP_LTI | HIR_LT+ICONST | Less than immediate |
| OP_LEI | HIR_LE+ICONST | Less or equal immediate |
| OP_GTI | HIR_GT+ICONST | Greater than immediate |
| OP_GEI | HIR_GE+ICONST | Greater or equal immediate |
| OP_NOT | HIR_NOT | Logical NOT |
| OP_TEST | HIR_BRC | Branch on truthiness |
| OP_TESTSET | HIR_BRC+COPY | Branch and conditional copy |
| OP_JMP | HIR_BR | Unconditional branch |

### 7.2 Native — Numeric For Loops

`OP_FORPREP` / `OP_FORLOOP` are the highest-value optimization target.
They lower to M2 multi-block HIR with PHI nodes:

```
Block 0 (entry):
  %init  = LUA_REG[A]       ; initial value
  %limit = LUA_REG[A+1]     ; limit
  %step  = LUA_REG[A+2]     ; step
  ; Type guard: all three must be integer
  BR → Block 1

Block 1 (loop header):
  %i = PHI [Block 0: %init, Block 2: %next]
  %cmp = LE %i, %limit      ; (or GE if step < 0)
  BRC %cmp → Block 2, Block 3

Block 2 (loop body):
  ; ... body opcodes ...
  %next = ADD %i, %step
  BR → Block 1

Block 3 (exit):
  ; continue after loop
```

This is identical to how the existing softcode JIT handles `iter()` loops
— the M2 multi-block infrastructure with PHI/SSA is already proven.

### 7.3 ECALL — Non-Integer Operations

These opcodes always emit an ECALL:

| Lua opcode | ECALL | Reason |
|------------|-------|--------|
| OP_ADD (float) | ECALL_LUA_ARITH | Float arithmetic |
| OP_POW | ECALL_LUA_ARITH | Always float (libm) |
| OP_DIV | ECALL_LUA_ARITH | Always float result |
| OP_CONCAT | ECALL_LUA_CONCAT | String / GC |
| OP_LEN | ECALL_LUA_LEN | String/table length |
| OP_GETTABLE | ECALL_LUA_GETTABLE | Table access |
| OP_SETTABLE | ECALL_LUA_SETTABLE | Table mutation |
| OP_GETFIELD | ECALL_LUA_GETFIELD | Table field access |
| OP_SETFIELD | ECALL_LUA_SETFIELD | Table field mutation |
| OP_GETI | ECALL_LUA_GETI | Table integer index |
| OP_SETI | ECALL_LUA_SETI | Table integer index set |
| OP_NEWTABLE | ECALL_LUA_NEWTABLE | Table creation / GC |
| OP_SELF | ECALL_LUA_SELF | Method lookup |
| OP_CALL | ECALL_LUA_CALL | Function call |
| OP_RETURN/0/1 | (exit sequence) | Frame pop |
| OP_GETUPVAL | ECALL_LUA_GETUPVAL | Upvalue read |
| OP_SETUPVAL | ECALL_LUA_SETUPVAL | Upvalue write |
| OP_GETTABUP | ECALL_LUA_GETTABUP | Upvalue table access |
| OP_SETTABUP | ECALL_LUA_SETTABLE | Upvalue table write |
| OP_SETLIST | ECALL_LUA_SETTABLE | Table init |
| OP_CLOSE | (flush + ECALL) | Close upvalues |
| OP_FORPREP (float) | ECALL_LUA_FORPREP | Float loop init |
| OP_TFORCALL | ECALL_LUA_TFORCALL | Generic for iterator |
| OP_TFORLOOP | ECALL + BRC | Generic for loop test |
| OP_MMBIN/I/K | ECALL_LUA_ARITH | Metamethod dispatch |

### 7.4 Ineligible (Reject Proto)

These opcodes cause the Proto to be rejected at compile time:

| Lua opcode | Reason |
|------------|--------|
| OP_CLOSURE | Requires GC allocation, nested Proto capture |
| OP_VARARG | Dynamic frame sizing |
| OP_VARARGPREP | Dynamic frame sizing |
| OP_TAILCALL | Frame manipulation |
| OP_TBC | To-be-closed / finalizer integration |

## 8. Upvalue and Closure Rules

### 8.1 Upvalue Access

Upvalues are **always** accessed via ECALL. The JIT never reads or writes
upvalue storage directly. This is because:

- Open upvalues point into another function's stack frame
- Closed upvalues hold a TValue in the UpVal object itself
- The GC manages UpVal lifetime

ECALL_LUA_GETUPVAL reads the upvalue into a Lua stack slot. If the result
is an integer, the ECALL handler writes the unboxed value to the
corresponding guest register. If non-integer, the register is marked as
VM-only.

### 8.2 Closures

Closures are **not compiled**. A Proto containing `OP_CLOSURE` is
ineligible for JIT. The created closure runs in the Lua VM.

Future work (Phase 4+) could JIT closures by:
- Compiling the inner Proto separately
- Storing upvalue indices in guest memory
- Handling upvalue capture as a specialized ECALL

### 8.3 _ENV Access

`OP_GETTABUP` with upvalue index 0 is the standard pattern for global
variable access (`_ENV[key]`). This always goes through ECALL. In the
context of the `mux.*` bridge, `mux.name(dbref)` compiles as:

```
GETTABUP  R(A), U(0), K("mux")     → ECALL_LUA_GETTABUP
GETFIELD  R(A), R(A), K("name")    → ECALL_LUA_GETFIELD
MOVE      R(A+1), R(arg)           → (native if integer)
CALL      R(A), 2, 2               → ECALL_LUA_CALL
```

The call chain is: JIT → ECALL → Lua VM → C bridge function → COM
interface → server. This is the expected path for `mux.*` calls — they
are I/O-bound, not compute-bound.

## 9. Table Identity and Mutation

### 9.1 Rule

**Tables never exist in guest memory.** All table operations are ECALLs.
Table identity, mutation, and GC are entirely managed by the Lua VM.

### 9.2 Consequence

There is no coherence problem for tables. The JIT never caches table
state. Every table read/write goes through the VM, which maintains the
authoritative table state.

This means table-heavy code gets no JIT benefit. That is acceptable:
- Table-heavy code is I/O-bound (attribute lookups, player queries)
- The value of Lua JIT is in numeric loops and arithmetic
- Table operations go through `mux.*` bridge calls anyway

### 9.3 Array Optimization (Phase 3+)

For integer-indexed array access patterns like:

```lua
for i = 1, #t do
    sum = sum + t[i]
end
```

A future optimization could:
1. At loop entry, pin the table and copy the array part to guest memory
2. JIT the loop body with native array access
3. At loop exit, write back if modified

This is LuaJIT's "AREF + HREF sinking" pattern. Deferred to Phase 3+.

## 10. Generic Function Calls and Varargs

### 10.1 Function Calls

All `OP_CALL` opcodes go through `ECALL_LUA_CALL`. The protocol:

1. JIT flushes all dirty guest registers to the Lua stack
2. ECALL handler calls `luaD_precall()` + `luaV_execute()` (or the
   C function directly for C calls)
3. Results are placed on the Lua stack per Lua calling convention
4. ECALL handler reloads integer results into guest registers
5. JIT resumes

### 10.2 Varargs

Protos with `is_vararg != 0` are ineligible for JIT. Varargs require
dynamic stack frame sizing that doesn't map to the fixed guest memory
layout.

### 10.3 Multiple Return Values

`OP_CALL` with `C != 1` (multiple returns) places results starting at
`R(A)`. The ECALL handler writes each integer result to the corresponding
guest register. Non-integer results are left in the Lua stack only; their
guest register slots are marked VM-only.

## 11. Coroutine Interaction

### 11.1 Rule

**JIT execution is non-resumable.** If a coroutine yield occurs during
JIT execution (via an ECALL), the JIT execution is abandoned. When the
coroutine is resumed, the function restarts in the Lua VM.

### 11.2 Rationale

Coroutine yield/resume requires saving and restoring the full Lua call
stack, including PC position. The JIT's RV64 execution state (block cache,
register cache, superblock state) is not designed to be serialized and
restored.

### 11.3 Consequence

Coroutine-heavy code runs in the Lua VM. This is acceptable because
coroutines in MUX Lua scripts are primarily used for state machines
(which are I/O-bound, not compute-bound).

## 12. Compilation Pipeline

### 12.1 lua_to_hir()

New file: `mux/modules/engine/lua_to_hir.cpp`

```cpp
// Compile a Lua Proto into an HIR program.
// Returns true on success, false if Proto is ineligible.
//
bool lua_to_hir(Proto *p, hir_program &h, lua_compile_ctx &ctx);
```

The function walks `p->code[0..p->sizecode-1]`, maintaining:

- `reg_type[64]` — type of each Lua register (INT, VM_ONLY, UNKNOWN)
- `block_map[sizecode]` — HIR block index for each Lua PC
- `hir_reg[64]` — current HIR value number for each Lua register

### 12.2 First Pass: Block Discovery

Scan bytecode for branch targets to identify basic block boundaries:

```
for each instruction at pc:
  if OP_JMP:       mark pc+1+offset as block start
  if OP_FORLOOP:   mark pc+1-offset as block start, pc+1 as block start
  if OP_FORPREP:   mark pc+1+offset as block start
  if comparison:   mark pc+2 as block start (skip + fallthrough)
  if OP_RETURN*:   mark pc+1 as block start (if not end)
```

### 12.3 Second Pass: Lowering

Walk bytecode in order, emitting HIR instructions. At block boundaries,
start a new HIR block. At back-edges (loop headers), emit PHI nodes for
all live integer registers.

### 12.4 Integration with compile cache

Cache key: `"lua:" + sha1(Proto->code, Proto->sizecode * 4) + ":" +
sha1(Proto->k, Proto->sizek * sizeof(TValue))`

The `blob_hash` field in `code_cache` includes the Lua version (5.4) and
the Tier 2 blob version, ensuring cache invalidation on upgrades.

## 13. What This Buys

### 13.1 Expected Performance Wins

| Code pattern | Stock VM | JIT | Speedup |
|---|---|---|---|
| Numeric for loop (1M iterations) | ~200ms | ~5ms | ~40× |
| Integer arithmetic chain | ~50ms | ~2ms | ~25× |
| Stat calculation (add/mul/cmp) | ~10ms | <1ms | ~15× |
| Table-heavy code | ~100ms | ~100ms | 1× (no benefit) |
| String processing | ~80ms | ~80ms | 1× (no benefit) |

### 13.2 What Stays in the VM

- All `mux.*` bridge calls (I/O-bound anyway)
- Table operations (hash lookups, GC)
- String operations (GC, buffer management)
- Closures and upvalue capture
- Coroutine yield/resume
- Metamethod dispatch

### 13.3 Typical MUX Lua Script Profile

Most MUX Lua scripts will be short glue: read attributes, do a calculation,
notify the player. The JIT wins are in the calculation step. The I/O steps
(`mux.get`, `mux.notify`, `mux.eval`) are ECALL-bound regardless.

The highest-value target is **combat systems** — tight numeric loops with
stat lookups, random rolls, and comparison chains. These are exactly the
functions that players complain are slow in softcode today.

## 14. Implementation Status (as of 2026-03-18)

| Step | Status | Notes |
|------|--------|-------|
| 1. eval_ctx + lua_State | ✅ Done | `void *lua_state` in eval_ctx, threaded through RunCompiled |
| 2. ECALL handlers | ✅ Done | __lua_newtable/geti/seti/getfield/setfield/getglobal/setglobal/call/pow |
| 3. Lua→HIR lowering | ✅ Done | 73/83 opcodes in hir_lower_lua.cpp |
| 4. Numeric for loop | ✅ Done | STORE_Q/LOAD_Q → PHI via hir_ssa_construct |
| 5. Type guards | ✅ Done | Compile-time: operand type checks, float constant validation |
| 6. Dirty register tracking | Skipped | Not needed — simplified architecture without dual register file |
| 7. Compile cache | ✅ Done | Sequential key in CJITCompile, LRU in CLuaMod |
| 8. Smoke tests | ✅ Done | 46 lua tests (TC001-TC046) |

**Additional completed work beyond original plan:**
- TY_FLOAT in HIR + RV64D codegen (16 float instructions)
- Lua folded into engine.so (not a separate module)
- Generic function calls: GETTABUP generalized, __lua_call via lua_pcall
- Lua stack save/restore around JIT execution
- Bitwise ops (6 HIR instructions)
- Table ops via Lua VM ECALL (NEWTABLE/GETTABI/SETTABI/etc.)
- Eligibility pre-filter with rejection diagnostics
- Bundled Lua 5.4.7 source (liblua54.a, no system package needed)

## 15. Remaining Work

**Phase 2 completion (incremental):**
- GETUPVAL/SETUPVAL — non-_ENV upvalue access (needs Lua call frame)
- TFORPREP/TFORCALL/TFORLOOP — generic for-loop via iterator ECALL
- CLOSE — close upvalues

**Phase 3 (future):**
- Runtime type guards (tag checks at function entry, bailout to VM)
- XMM register cache in DBT for FP values (currently spill-everywhere)
- String interning in guest memory (avoid ECALL for string comparisons)
- Array optimization for integer-indexed loops (pin table, native access)
- Cache key: sha1(bytecode) + Lua version for persistent cache
