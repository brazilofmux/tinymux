# Scripting Strategy: TF Script + Lua

## Overview

TinyMUX needs a scripting strategy that spans the server and multiple client
platforms (ncurses, Win32 console, HTML5). The approach is a two-layer design:
TF script as the interactive shell language, Lua as the power/plugin layer
underneath.

## Why Two Languages

**TF script** is the MU-native language. It understands triggers, macros,
worlds, hilites — the domain vocabulary. Players who have used TinyFugue for
25 years think in it. It is terse and domain-appropriate for interactive use.
You do not want to type `lua.macro.create("foo", function() ... end)` to make
a trigger.

**Lua** is the embed-everywhere language. Small, fast, clean C API, embeds
into anything — C++, Swift, Kotlin, even WASM. If one scripting language must
work across all clients, Lua is the only realistic answer. JavaScript only
works natively on the web. Python is too heavy. Lua was literally designed for
embedding.

## Architecture

TF script stays as the **shell** — the interactive command language. `/def`,
`/trig`, `/bind`, the things typed at the input line.

Lua becomes the **power layer** underneath. When a TF script needs real
logic — string parsing, state machines, complex trigger behavior — it calls
into Lua. Think of it like how vim has ex commands for quick stuff and
Lua/Python for serious plugins.

### FFI Boundary

The key design question: does TF script call Lua, or does Lua define TF
triggers? Probably both, but one direction should be primary.

Likely surface:

- TF-to-Lua: `/lua myfunc(arg1, arg2)` or letting TF triggers delegate to
  Lua handlers.
- Lua-to-TF: Lua functions that register triggers, binds, and hilites using
  the TF vocabulary.

### API Contract

A common capability surface should be defined across the Lua and JavaScript
(HTML5 client) bindings:

- What events can scripts listen to (connect, disconnect, line received,
  prompt, GMCP, MSSP).
- What queries can they make (room state, channel list, mail count).
- What mutations are allowed (send command, set variable, modify trigger).

Defining this contract early — even informally — prevents the Lua and
JavaScript implementations from diverging into incompatible ecosystems.

### HTML5 Client

The HTML5 client gets JavaScript natively; fighting that is pointless. But it
should expose the same API contract that the Lua bindings expose. Same
capability surface, different host language.

## Implementation Sequence

The order matters. Adding Lua everywhere simultaneously before any single
client has proven the API design is the one bad answer.

### Phase 1: Lua on the Server

The RISC-V JIT compiler work is a natural on-ramp. Lua bytecode to RV64 to
x86-64 is a well-understood compilation target. LuaJIT already does this
natively, but rolling our own gives control over the sandbox boundary and lets
Lua integrate with the existing AST evaluator.

Server-side Lua comes first because:

1. Full control over the environment — iterate without breaking client UX.
2. Sandboxing, memory limits, and API surface questions get answered here.
3. The binding patterns learned directly inform client embedding.

### Phase 2: Win32 Console Client

No scripting beyond basic triggers. Proves the client architecture without
the complexity of an embedded scripting runtime.

### Phase 3: Lua in the ncurses Client

Retrofit Lua into the ncurses client, informed by server-side experience.
The API contract and sandbox patterns are already proven by this point.

### Phase 4: Future Clients

Every subsequent client gets Lua from day one, using the proven API contract
and embedding patterns.

## Rationale for Sequencing

- The ncurses client just shipped. Let it stabilize. Get real users hitting
  real bugs before adding a scripting layer.
- Adding Lua to a client now means designing the client-side Lua API before
  knowing what users actually need to script. The guesses will be wrong.
- Server-side Lua teaches sandboxing, API surface, and memory limit lessons
  that directly inform client embedding.
