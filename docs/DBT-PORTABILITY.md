# DBT Portability: Host Platform Strategy

## Architecture

The JIT compilation pipeline is:

```
Softcode → AST → HIR → SSA → optimize → RV64 codegen → host DBT
```

Everything through RV64 codegen is portable — RV64 is the guest ISA, a
stable intermediate target.  The portability boundary is the **DBT layer**
that translates RV64 guest code to the host's native instruction set.

## Host Platform Assessment

### x86-64 Linux (DONE)

The current production target.  System V ABI.  Ubuntu/Debian are the
primary test platforms.  This is where all development and testing happens.

### x86-64 FreeBSD (TRIVIAL)

Same System V ABI as Linux.  Same register convention, same calling
convention.  The DBT emits no syscalls — all host interaction goes through
ECALLs to engine functions, which are already portable C++.  FreeBSD
x86-64 should work with the existing DBT backend unchanged.

**Status:** Needs testing.  Probably just works.

### x86-64 Windows (NOT WORTH IT)

Windows uses the Win64 ABI: different argument registers (rcx/rdx/r8/r9
vs rdi/rsi/rdx/rcx), 32-byte shadow space, different callee-saved set.
A separate DBT backend would be required.

However, Windows hosting is not a realistic deployment scenario.  MUSHes
are hosted on Unix.  The typical Windows use case is bringing up a game
locally to check backups or do initial development before deploying to
Unix.  The AST evaluator handles that case without JIT.

**Decision:** AST-only on Windows.  No DBT backend planned.  The
`--enable-jit` configure flag is already Unix-only.

### AArch64 Linux (FUTURE — INTERESTING)

ARM64 with AAPCS64 calling convention.  Clean register-rich ISA that
maps well from RV64.  Relevant for:

- Raspberry Pi 4/5 (hobbyist hosting)
- AWS Graviton / cloud ARM instances
- General industry direction

There will be two sub-variants to consider:

1. **AArch64 Linux** — standard mmap/mprotect for JIT memory.
2. **AArch64 macOS** — Apple requires MAP_JIT and
   pthread_jit_write_protect_np() to toggle W^X per-thread.  Different
   JIT memory model.

**Status:** No implementation yet.  Worth doing eventually as ARM hosting
becomes more common.

### Native RISC-V (FUTURE — SIMPLE BUT NO DEMAND)

If the host is RV64GC, the guest code IS native code.  No dynamic binary
translation needed — just mprotect the guest memory region as executable
and call into it.  The Tier 2 blob is already native.

This is the simplest possible "backend" — it's the absence of one.

**Status:** No implementation needed until RISC-V hosting exists.  The
guest codegen already produces valid RV64 machine code, so this is
trivially achievable when demand appears.

## Priority Order

1. **x86-64 Linux** — done, production.
2. **x86-64 FreeBSD** — test, probably works already.
3. **AArch64 Linux** — future, when ARM hosting demand justifies it.
4. **AArch64 macOS** — future, slightly harder (W^X model).
5. **Native RISC-V** — trivial but no demand.
6. **x86-64 Windows** — not planned.  AST-only is sufficient.

## Design Implications

The current architecture makes adding new host backends straightforward:

- **dbt.cpp** contains the x86-64 translator.  A new backend would be a
  parallel file (e.g., `dbt_aarch64.cpp`) selected at configure time.
- The **rv_compiler** output (guest memory buffer with RV64 code) is the
  interface between codegen and DBT.  New backends consume the same input.
- **ECALLs** are the only host interaction.  The ECALL dispatcher is in
  C++ and is already portable.  New backends just need to emit the
  host-native calling convention to reach the dispatcher.
- **Tier 2 blobs** (pre-compiled RV64 library functions) are guest-ISA
  code and work on any host.  No per-platform blob variants needed.

The main work for a new backend is:
1. RV64 instruction decoder (shared, already exists in `dbt_decoder.cpp`)
2. Host code emitter (per-platform: register mapping, instruction encoding)
3. JIT memory allocation (mmap+mprotect on Unix, VirtualAlloc on Windows,
   MAP_JIT on macOS)
4. Calling convention bridge for ECALLs
