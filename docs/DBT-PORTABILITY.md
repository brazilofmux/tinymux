# DBT Portability: Multi-Platform Host Strategy

## Overview

The JIT compilation pipeline is:

```
Softcode/Lua --> AST --> HIR --> SSA --> optimize --> RV64 codegen --> host DBT
```

Everything through RV64 codegen is portable -- RV64 is the guest ISA, a
stable intermediate target.  The portability boundary is the **DBT layer**
that translates RV64 guest code to the host's native instruction set.

The DBT emits no syscalls.  All host interaction goes through ECALLs to
engine functions, which are already portable C++.  This means platforms
that share the same host ISA and calling convention are interchangeable
from the DBT's perspective -- the difference between Linux and FreeBSD
on x86-64 is invisible at this layer.

## Host Platform Taxonomy

Five logical targets, clustered by three dimensions: host ISA, calling
convention (ABI), and JIT memory model.

| Target | Host ISA | ABI | JIT Memory | Priority |
|--------|----------|-----|------------|----------|
| x86-64 SysV (Linux, FreeBSD) | x86-64 | System V AMD64 | mmap RWX | **Production** |
| x86-64 Win64 (Windows) | x86-64 | Win64 | VirtualAlloc | Planned |
| AArch64 SysV (Linux, FreeBSD, Crostini) | AArch64 | AAPCS64 | mmap RWX | Planned |
| AArch64 Apple (macOS) | AArch64 | AAPCS64 | MAP_JIT + W^X | Future |
| RV64 native (Linux, FreeBSD) | RV64GC | LP64D | mprotect | Trivial |

**Platform equivalences:**

- Linux and FreeBSD are identical on any given ISA (same ABI, and no
  syscalls emitted).
- AArch64 Apple uses the same AAPCS64 calling convention as AArch64
  Linux; only the JIT memory model differs.
- RV64 native needs no translation -- the guest code IS native code.

## Architecture: Compile-Time Backend Selection

Each backend is a separate `.cpp` file selected at configure time.
Shared code lives in `dbt.cpp` and includes the correct emitter header
via the dispatch header `dbt_host.h`.

```
include/
    dbt.h               Guest context, block cache, public API (shared)
    dbt_host.h          Compile-time platform dispatch (NEW)
    dbt_jit_mem.h       JIT memory alloc/free/W^X abstraction (NEW)
    dbt_emit_x64.h      x86-64 code emitter (existing)
    dbt_emit_a64.h      AArch64 code emitter (NEW, future)
    dbt_decoder.h        RV64 instruction decoder (shared)
    dbt_compile.h        JIT pipeline declarations (shared)

modules/engine/
    dbt.cpp              Shared: register cache, block cache, dispatch,
                         public API (~800 lines)
    dbt_x64_sysv.cpp    x86-64 SysV backend (extracted from dbt.cpp)
    dbt_x64_win64.cpp   x86-64 Win64 backend (NEW, future)
    dbt_a64_sysv.cpp    AArch64 Linux backend (NEW, future)
    dbt_a64_apple.cpp   AArch64 macOS backend (NEW, future)
    dbt_rv64_native.cpp RV64 native pass-through (NEW, future)
```

### Dispatch Header: `dbt_host.h`

```cpp
#if defined(__riscv) && __riscv_xlen == 64
  #define DBT_HOST_RV64_NATIVE 1
#elif defined(__aarch64__) || defined(_M_ARM64)
  #include "dbt_emit_a64.h"
  #define DBT_HOST_AARCH64 1
  #if defined(__APPLE__)
    #define DBT_HOST_AARCH64_APPLE 1
  #else
    #define DBT_HOST_AARCH64_SYSV 1
  #endif
#elif defined(__x86_64__) || defined(_M_X64)
  #include "dbt_emit_x64.h"
  #define DBT_HOST_X64 1
  #if defined(_WIN32)
    #define DBT_HOST_X64_WIN64 1
  #else
    #define DBT_HOST_X64_SYSV 1
  #endif
#else
  #error "Unsupported host platform for DBT"
#endif
```

### Shared vs Per-Backend Split

**Shared code** (in `dbt.cpp`, includes backend via `dbt_host.h`):

- Register cache algorithms: `rc_init`, `rc_find`, `rc_alloc`, `rc_read`,
  `rc_write`, `rc_flush`, `rc_invalidate_reload`, and all `fc_*` FP
  equivalents.  These call emitter functions (`emit_load_guest`,
  `emit_store_guest`) that are `static inline` in the per-ISA header,
  resolved at compile time.
- Block cache: `cache_lookup`, `cache_insert`.
- Public API: `dbt_init`, `dbt_reset`, `dbt_rerun`, `dbt_pretranslate`,
  `dbt_resolve_chains`, `dbt_run`, `dbt_cleanup`.
- Trace/debug infrastructure.
- `dbt_state_t` management.

**Per-backend code** (one `.cpp` per target, compiled conditionally):

- Host register role tables: `rc_host_regs[]`, `rc_pinned_guest[]`,
  `host_arg_regs[]`.
- `emit_trampoline()` -- saves callee-saved regs, sets up pinned
  registers, calls block code, restores.
- `translate_block()` -- per-instruction RV64-to-host translation
  (~1600 lines on x86-64).
- All intrinsic stubs (`emit_stub_slen`, `emit_stub_scopy`, ...,
  `emit_stub_co_generic`, and all `DEFINE_CO_EMITTER` instances).
- Block chaining: `emit_exit_chained`, `backpatch_jmp`.
- ABI bridge: `emit_stub_prologue`, `emit_stub_epilogue`,
  `emit_call_host`.

**Per-ISA emitter header** (included via `dbt_host.h`):

- `emit_t` struct (buf, offset, capacity) -- same layout on all ISAs.
- Register constants and encoding helpers.
- `static inline` instruction emission functions.
- Guest register load/store: `emit_load_guest`, `emit_store_guest`.

**Interface between shared and backend:** Backend-specific arrays
(`rc_host_regs[]`, etc.) are declared `extern` in shared code and
defined in the backend `.cpp`.  No virtual dispatch or function
pointers -- everything is resolved at compile time.

### JIT Memory Abstraction: `dbt_jit_mem.h`

Four functions, with platform-specific implementations:

| Function | x86-64 Linux/FreeBSD | Windows | AArch64 Linux | AArch64 Apple |
|----------|---------------------|---------|---------------|---------------|
| `jit_alloc(size)` | mmap RWX | VirtualAlloc RWX | mmap RWX | mmap MAP_JIT |
| `jit_free(ptr, size)` | munmap | VirtualFree | munmap | munmap |
| `jit_write_begin()` | no-op | no-op | no-op | `pthread_jit_write_protect_np(false)` |
| `jit_write_end(addr, len)` | no-op | no-op | `__builtin___clear_cache` | W^X toggle + icache invalidate |

On x86-64, the instruction cache is coherent with data writes, so
`jit_write_end` is a no-op.  On AArch64, the icache must be explicitly
invalidated after writing code.  On AArch64 Apple, the W^X model adds
write-protect toggling around every code emission.

`jit_write_begin()`/`jit_write_end()` are called in:
- `emit_trampoline()` (once at init)
- `translate_block()` (each translation)
- `backpatch_jmp()` (each chain resolution)

### Build System Integration

**`configure.ac`** detects the host platform when `--enable-jit`:

```
case "$host_cpu" in
  x86_64|amd64)
    DBT_BACKEND=dbt_x64_sysv
    case "$host_os" in
      mingw*|cygwin*) DBT_BACKEND=dbt_x64_win64 ;;
    esac ;;
  aarch64|arm64)
    DBT_BACKEND=dbt_a64_sysv
    case "$host_os" in
      darwin*) DBT_BACKEND=dbt_a64_apple ;;
    esac ;;
  riscv64)
    DBT_BACKEND=dbt_rv64_native ;;
  *)
    AC_MSG_ERROR([JIT not supported on $host_cpu]) ;;
esac
AC_SUBST(DBT_BACKEND)
```

**`Makefile.am`** conditionally compiles the selected backend:
`dbt.cpp $(DBT_BACKEND).cpp`.

## Register Mapping Tables

### x86-64 SysV (Production)

| Role | Register | Notes |
|------|----------|-------|
| Guest context pointer | RBX | callee-saved, points to `rv64_ctx_t` |
| Guest memory base | R12 | callee-saved |
| Block cache base | R13 | callee-saved |
| Scratch | RAX, RCX, RDX | never cached |
| Register cache (8 slots) | RSI, RDI, R8-R11, R14, R15 | 8-slot LRU |
| Pinned guest: x10(a0) | RSI | pre-loaded by trampoline |
| Pinned guest: x11(a1) | RDI | pre-loaded by trampoline |
| Pinned guest: x12(a2) | R8 | pre-loaded by trampoline |
| Pinned guest: x13(a3) | R9 | pre-loaded by trampoline |
| FP cache (6 slots) | XMM2-XMM7 | XMM0-XMM1 = scratch |
| SysV arg regs | RDI, RSI, RDX, RCX, R8, R9 | 6 integer args |
| Callee-saved | RBX, RBP, R12-R15 | saved by trampoline |

### x86-64 Win64 (Planned)

| Role | Register | Notes |
|------|----------|-------|
| Guest context pointer | RBX | callee-saved |
| Guest memory base | R12 | callee-saved |
| Block cache base | R13 | callee-saved |
| Scratch | RAX, RCX, RDX | never cached |
| Register cache (8 slots) | RSI, RDI, R8-R11, R14, R15 | same as SysV |
| Win64 arg regs | RCX, RDX, R8, R9 | 4 integer args |
| Win64 callee-saved | RBX, RBP, RDI, RSI, R12-R15 | RSI/RDI callee-saved! |
| Shadow space | 32 bytes below args | caller must allocate |

Key difference: RSI and RDI are callee-saved on Win64 (argument regs on
SysV).  The trampoline must save/restore them.  The register cache can
still use them (they're saved by the trampoline).  The 32-byte shadow
space must be allocated before every host CALL.  Overflow args (>4) go
to stack at `[RSP+32]`, `[RSP+40]`, etc.

### AArch64 AAPCS64 (Planned)

| Role | Register | Notes |
|------|----------|-------|
| Guest context pointer | X19 | callee-saved |
| Guest memory base | X20 | callee-saved |
| Block cache base | X21 | callee-saved |
| Scratch | X0, X1, X2 | also arg/result regs |
| Register cache (8 slots) | X9-X12, X22-X25 | 4 caller + 4 callee saved |
| Pinned guest: x10(a0) | X22 | callee-saved, survives CALLs |
| Pinned guest: x11(a1) | X23 | callee-saved |
| Pinned guest: x12(a2) | X24 | callee-saved |
| Pinned guest: x13(a3) | X25 | callee-saved |
| AAPCS64 arg regs | X0-X7 | 8 integer args (more than SysV x86-64) |
| Callee-saved | X19-X28, D8-D15 | saved by trampoline |
| FP cache (6 slots) | D16-D21 | caller-saved |
| FP scratch | D0, D1 | also arg/result regs |

AArch64 advantages: hardware SDIV/UDIV (single instruction vs x86-64
CQO+IDIV), 8 argument registers (fewer stack overflows for co_* stubs),
CBZ/CBNZ (fused compare-branch-zero), CSEL (conditional select, replaces
SETcc+MOVZX).

### RV64 Native (Trivial)

No register mapping needed.  Guest code runs natively.  The trampoline
sets up a0-a7 per LP64D convention and calls into guest code directly.
ECALLs use the RV64 ECALL instruction, dispatched by the existing ECALL
handler.

## Per-Backend Implementation Notes

### x86-64 SysV (Existing)

The current production backend.  No changes needed beyond extracting it
into `dbt_x64_sysv.cpp`.  Block chaining uses x86-64 `JMP rel32`
(4-byte displacement, +/-2GB range -- always sufficient for a 1MB code
buffer).

### x86-64 Win64

Fork of x86-64 SysV with ABI adjustments.  Same emitter header
(`dbt_emit_x64.h`), same instruction encoding.  Changes are confined to:

1. Trampoline register setup (incoming args in RCX/RDX/R8/R9).
2. Shadow space allocation (SUB RSP, 32 before every CALL).
3. Argument register mapping for intrinsic stubs.
4. Overflow argument layout.

### AArch64 SysV

New ISA backend.  Requires a new emitter header (`dbt_emit_a64.h`).
Reference implementation: `~/slow-32/tools/dbt/emit_a64.h` (targets a
32-bit guest but the emitter patterns are directly applicable).

Key architectural differences from x86-64:

- **Fixed-width instructions:** All AArch64 instructions are 32 bits.
  No variable-length encoding, no REX prefixes.
- **Hardware division:** SDIV/UDIV are single instructions.  The x86-64
  multi-instruction sequence (MOV to RAX, CQO, IDIV, handle remainder
  in RDX) becomes one instruction.
- **Fused compare-branch:** CBZ/CBNZ eliminate the CMP+Jcc pair for
  zero-tests.  TBNZ/TBZ test individual bits.
- **Conditional select:** CSEL replaces the SETcc+MOVZX pattern.
- **PC-relative branches:** B has 26-bit signed offset (128MB range).
  B.cond has 19-bit (1MB range).  For a 1MB code buffer, B.cond
  can reach any target.  If larger buffers are needed, side-exit stubs
  use B to a nearby trampoline that does BR via register.
- **Block chaining:** Backpatch a B instruction by rewriting its 26-bit
  offset field (bits [25:0] of a 32-bit instruction word).
- **Icache invalidation:** Required after writing code.  Call
  `__builtin___clear_cache(start, end)` after each translation.

### AArch64 Apple

Same ISA and ABI as AArch64 SysV.  Only the JIT memory model differs.
Apple Silicon enforces W^X: memory pages cannot be simultaneously
writable and executable.

Implementation via `jit_write_begin()`/`jit_write_end()` in
`dbt_jit_mem.h`:

```cpp
// Before writing JIT code:
pthread_jit_write_protect_np(false);   // enable write, disable execute

// ... emit code ...

// After writing JIT code:
pthread_jit_write_protect_np(true);    // enable execute, disable write
sys_icache_invalidate(addr, len);      // flush icache
```

The backend `.cpp` can likely be shared with AArch64 SysV (identical
translation logic).  The JIT memory functions are the only difference.

### RV64 Native

The simplest possible "backend" -- it's the absence of one.  Guest code
is already native RV64 machine code.

- `translate_block()`: No translation.  The block cache entry points
  directly into guest memory.
- `dbt_init()`: `mprotect` the guest memory region as
  `PROT_READ | PROT_EXEC`.
- Intrinsic stubs: Emit native RV64 code to call host functions via the
  LP64D calling convention (a0-a7 for args, ra for return address).
- Block chaining: Native RV64 JAL with 20-bit offset, or skip chaining
  entirely and always return to dispatch (simplest).

## Implementation Phases

### Phase 1: Design Document (This Document)

Document the multi-platform strategy, abstraction boundaries, register
mapping tables, and per-backend implementation notes.

### Phase 2: Extract Platform Abstraction

Refactor the existing code to separate shared logic from the x86-64 SysV
backend.  No new backends, no functional change.

1. Create `include/dbt_host.h` (platform dispatch).
2. Create `include/dbt_jit_mem.h` (JIT memory abstraction).
3. Split `dbt.cpp` into shared code + `dbt_x64_sysv.cpp`.
4. Update `configure.ac` and `Makefile.am` for backend selection.
5. Verify: `make clean && make install && make test` -- identical results.

### Phase 3a: x86-64 Win64 Backend

Same ISA, different ABI.  Validates the abstraction without requiring a
new emitter.

1. Create `dbt_x64_win64.cpp` (fork of `dbt_x64_sysv.cpp`).
2. Adjust trampoline, arg registers, shadow space, overflow args.
3. Test on Windows 11 / VS 2026.

### Phase 3b: AArch64 Linux Backend

New ISA.  The larger effort.

1. Create `include/dbt_emit_a64.h` (AArch64 emitter).
2. Create `dbt_a64_sysv.cpp` (AAPCS64 backend).
3. Add icache invalidation to `jit_write_end()`.
4. Test on ARM Chromebook (Crostini).

### Phase 3c: AArch64 Apple Backend

Variant of Phase 3b.

1. Create `dbt_a64_apple.cpp` (or share with `dbt_a64_sysv.cpp`).
2. Add MAP_JIT and W^X toggling to `dbt_jit_mem.h`.
3. Test on Mac hardware.

### Phase 4: RV64 Native Pass-Through

Trivial backend.

1. Create `dbt_rv64_native.cpp` (~100 lines).
2. Test on RV64 hardware (when available).

## Related Work

The following sibling projects informed this design:

- **`~/slow-32/tools/dbt/`** -- Proven dual-emitter pattern with
  `emit_x64.h` + `emit_a64.h`, `#ifdef __aarch64__` platform dispatch,
  `host_reg_t` typedef, shared `emit_ctx_t`.  The AArch64 emitter is
  directly reusable as a reference for `dbt_emit_a64.h`.

- **`~/riscv/dbt/`** -- RV32IMFD DBT with identical register cache
  patterns, intrinsic interception, block chaining.  Same architectural
  lineage.

- **`~/qemu/`** -- SLOW-32 TCG target in a QEMU fork.  QEMU's TCG
  demonstrates the full generality of multi-target code generation but
  is architecturally distant from this DBT's lightweight design.
