# From Custom CPUs to Lua JIT: Building a Compiler Stack in 16 Days

*How a MUSH server got a JIT compiler, and what that has to do with
building your own CPU.*

---

## The Problem Nobody Asked About

TinyMUX is a text-based virtual world server. People use it to build
online communities—think collaborative fiction, tabletop RPG
campaigns, social spaces. The software has been around since the 1990s.
Its scripting language ("softcode") is evaluated as a stream of
characters. No AST. No compilation. Just `mux_exec()` walking through
text, character by character.

For thirty years, that was fine.

Then someone decided to add Lua.

## The 821-Commit Sprint

Between March 2 and March 18, 2026, the brazil branch of TinyMUX
received 821 commits and 820,134 lines of new code. Here's what
happened, roughly in order:

1. **Fixed the build system.** The starting point (commit `a3cf309d0`)
   was "Fix autoconf/automake build system issues." Not glamorous, but
   you can't compile a JIT if you can't compile anything.

2. **Wrote 500+ smoke tests.** From 32 test cases to 547. Every
   function, every edge case, every regression. The test infrastructure
   uses `expect` to upload test objects to a running MUX, execute them,
   and compare SHA1 hashes of output.

3. **Integrated GANL networking.** Replaced the 1990s-era `select()`
   loop with an epoll-based asynchronous networking library. Added TLS
   (OpenSSL + Schannel), MSSP, GMCP, WebSocket support. The server can
   now talk to modern MU* clients on modern protocols.

4. **Built the AST evaluator.** Replaced the classic character-by-
   character parser with a Ragel-generated scanner that builds AST
   nodes, evaluated by a tree walker. LRU parse cache (1024 entries).
   Native NOEVAL handlers for `if()`, `switch()`, `iter()`, `cand()`,
   `cor()`. The old parser was deleted.

5. **Moved the database to SQLite.** Write-through for all object
   mutations. WAL checkpoint instead of flatfile dumps. Indexed
   `@search`. Schema version 7. The old Berkeley DB hash file code was
   removed.

6. **Split the server into three layers.** `libmux.so` (core
   utilities), `engine.so` (game logic), `netmux` (driver/networking).
   COM-based interfaces between layers, `-fvisibility=hidden` on
   engine.so, only 6 exported symbols. The architecture is ready for
   process isolation (engine in a child process, driver survives engine
   crashes).

7. **Built a JIT compiler.** Not for Lua—for softcode. AST nodes
   lower to HIR (High-level Intermediate Representation), then to RV64
   machine code, then to x86-64 via dynamic binary translation (DBT).
   The DBT has block chaining, superblock formation, native intrinsics,
   and a register cache. Tier 2 blob of cross-compiled Ragel functions
   provides Unicode-aware string operations at native speed.

8. **Embedded Lua 5.4.** Sandboxed interpreter with instruction and
   memory limits, bytecode cache, `mux.*` bridge functions (15 C
   functions providing object queries, attribute access, notification,
   and softcode evaluation).

9. **Built a Lua JIT compiler.** The JIT deserializes Lua bytecode
   (without Lua headers—our own standalone deserializer), lowers 79
   of 83 opcodes to HIR, compiles through the same SSA/RV64/DBT
   pipeline as softcode. Float arithmetic via RV64D—SSE2. Table
   operations via ECALL back to the Lua VM. Generic function calls
   (`math.floor()`, `string.upper()`, `tostring()`) via `lua_pcall`.

10. **Built a Windows client (Titan)** and an **Android client** (Titan
    for Android, Kotlin/Compose). Both connect via TLS, support GMCP,
    and render ANSI/xterm-256/truecolor.

## The Compiler Stack

The Lua JIT didn't appear from nowhere. It sits on top of a stack that
took years to build:

**SLOW-32** (`~/slow-32`): A custom 32-bit RISC CPU with a complete
LLVM backend. Custom assembler, linker, simulator, debugger. The
selfhosting SSA compiler (14,261 lines, 7 phases) was the architectural
reference for the TinyMUX JIT—parallel-array HIR, linear-scan
register allocation, iterative optimization convergence.

**RV32IMFD DBT** (`~/riscv`): A dynamic binary translator for RISC-V
32-bit. Block chaining, superblock formation, ECALL service model. The
techniques developed here—register caching, instruction fusion,
diamond-merge optimization, native intrinsic stubs—were ported
directly to the TinyMUX JIT's RV64 DBT.

**TinyMUX JIT** (`~/tinymux`): The production system. RV64IMD
(integer + multiply + double-precision float). 8-slot LRU register
cache. Tier 2 blob with cross-compiled Ragel functions. SQLite code
cache (schema v7). 547 smoke tests, all passing.

**Lua JIT** (Phase 2): Bytecode deserializer, 79/83 opcode lowering,
ECALL handlers for table/global/function operations using `lua_State *L`
callbacks. Float arithmetic, bitwise ops, generic for-loops, string
concatenation. `lua_settop()` save/restore for Lua stack management.

Each layer taught lessons that the next layer used. The SLOW-32 SSA
compiler taught HIR design. The RV32 DBT taught block translation. The
softcode JIT taught ECALL conventions. The Lua JIT taught type
promotion and VM interop.

## The MUSH Standard

Running parallel to all of this is the MUSH Standard (`~/mush`)—a
formal specification of how MUSH servers should behave. 66 markdown
files covering objects, attributes, flags, commands, expression
evaluation, substitution, functions, building, communication,
administration, and more.

The Standard exists because the MUSH ecosystem has been drifting for
decades. Three major codebases (TinyMUX, PennMUSH, RhostMUSH) have
diverged in subtle ways. The Standard defines the common ground:
conforming servers should produce identical output for the same input.

The JIT compiler is, in a sense, a test of the Standard. If the JIT
produces different output than the interpreter for any expression, the
JIT is wrong. 547 smoke tests enforce this. The Standard tells you what
"correct" means; the JIT tells you how fast you can get there.

## What 79/83 Means

Of the 83 Lua 5.4 opcodes:

- **79 are JIT-compiled.** Data movement, integer and float arithmetic,
  bitwise operations, comparisons, control flow, numeric and generic
  for-loops, string concatenation, table construction and access,
  global variable access, function calls (both `mux.*` bridge and
  general Lua functions), upvalue access, method calls (`t:method()`),
  and returns.

- **4 are permanently rejected.** `OP_CLOSURE` (nested function
  creation), `OP_VARARG` (variadic arguments), `OP_TBC` (to-be-closed
  variables), `OP_TAILCALL` (tail calls). These require deep Lua VM
  integration that the JIT architecture doesn't provide. Scripts using
  them run in the stock Lua VM—which is fine, because they're I/O-
  bound patterns that don't benefit from JIT compilation anyway.

When the JIT can't compile a script, it falls back transparently to the
Lua VM. The user never knows. The output is identical. The only
difference is speed.

## The Architecture That Worked

The design spec for the Lua JIT envisioned a dual register file with
type guards, dirty-register tracking, and explicit flush/reload between
the JIT and the Lua VM. We didn't build that. Instead:

- **ECALL everything that isn't native arithmetic.** Table access?
  ECALL to `lua_geti()`. Function call? ECALL to `lua_pcall()`. Global
  lookup? ECALL to `lua_getglobal()`.

- **Lua stack save/restore.** Before JIT execution, save
  `lua_gettop()`. After, restore with `lua_settop()`. ECALL handlers
  push freely; cleanup is automatic.

- **Compile-time type guards.** Check operand types when emitting HIR.
  Float where we expect int? Promote. String where we expect int?
  Reject the proto to the VM.

This turned out to be the right architecture. It got us to 79/83
opcodes with generic function calls in one session, without the
complexity of maintaining two register files in lockstep. The JIT does
native arithmetic at full speed; everything else goes through the Lua C
API at Lua speed. For MUX scripts—which are short glue between
attribute lookups, stat calculations, and player notifications—this
is the right tradeoff.

## What's Next

The JIT is done for now. The four rejected opcodes (closures, varargs,
to-be-closed, tail calls) are Everest—technically possible but
requiring GC integration, frame manipulation, and finalizer support that
would double the complexity for marginal benefit.

The more interesting next step is the MUSH Standard. Thirty years of
accumulated behavior, edge cases, and undocumented assumptions—all
being written down for the first time. The Standard is the foundation
that makes everything else possible: you can't JIT-compile softcode if
you don't know what softcode means.

And somewhere in the background, SLOW-32 is still running. The custom
CPU that started this whole journey—a 32-bit RISC architecture with
an LLVM backend, built from scratch to understand what compilers
actually do. Every time the TinyMUX JIT emits an RV64 instruction, it's
using techniques that were first tested on a CPU that doesn't exist in
silicon.

That's the thing about compiler work. You build tools that build tools
that build tools. And at the bottom of the stack, there's always someone
who decided to build a CPU from scratch, just to see how it works.

---

*Stephen Dennis is the maintainer of TinyMUX, author of the MUSH
Standard, and builder of compilers for CPUs that exist only in
software. The TinyMUX source code is at
github.com/brazilofmux/tinymux. The MUSH Standard is a work in
progress.*
