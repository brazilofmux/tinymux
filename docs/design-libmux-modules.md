# libmux Shared Library and Module Architecture Design

## Direction

Decompose netmux into a set of cooperating modules communicating through
well-defined COM-style interfaces.  The work proceeds in four phases:

1. **Standard Marshaler** — Complete.  CStandardMarshaler now handles both
   marshal and unmarshal.  CLogProxy/CLogStub/CLogPSFactory demonstrate the
   proxy/stub/factory pattern for mux_ILog.

2. **Improve libmux** — Harden the library, add proxy/stub pairs for all
   cross-process interfaces, and build helpers to reduce marshaling boilerplate.

3. **Grow libmux.so** — Move foundational code out of netmux into the shared
   library so that modules can use it directly.

4. **Modularize the server** — Factor the remaining server subsystems into
   modules behind interfaces.

Whether each phase is feasible, advisable, or worth doing is evaluated as part
of this document.  Each phase has open questions, experiments, and go/no-go
criteria.


## Current State

### What is libmux today?

A ~2,000-line library providing:

- Poor Man's COM: class/interface registration, QueryInterface, AddRef/Release
- Module loading: dlopen wrapper, mux_Register/mux_Unregister lifecycle
- Cross-process RPC: QUEUE_INFO serialization, channel management,
  framed pipe protocol (Call/Return/Msg/Disc)
- Marshaling: custom (mux_IMarshal) and standard (CStandardMarshaler)

Built as both `libmux.a` (statically linked into netmux and stubslave) and
`libmux.so` (dynamically loaded by modules at runtime).

### What can modules access today?

Modules link against `libmux.so` and include `libmux.h` + `modules.h`.
They can:

- Register COM classes and factories
- Hook server events (startup, shutdown, connect, disconnect, data lifecycle)
- Provide custom softcode functions (mux_IFunction)
- Use the logging service (mux_ILog)
- Issue SQL queries through the stubslave (mux_IQueryControl)

Modules **cannot** directly call any netmux internal function: no evaluator,
no database access, no attribute operations, no notification, no command
processing.  These are linked into the netmux binary and not exported through
libmux.so.

### What is the stubslave?

A separate process (`stubslave`) that loads modules out-of-proc.  It links
`libmux.a` and communicates with netmux over stdin/stdout pipes.  Channel 0
bootstraps new connections.  Custom marshaling handles CStubSlaveProxy
(mux_ISlaveControl) and CQueryClient (mux_IQuerySink).

### Scale of the server

| Subsystem | Files | LOC |
|---|---|---|
| Evaluation & functions | eval, ast, functions, funceval, funceval2, funmath | ~23,000 |
| Database & storage | db, sqlitedb, db_rw, sqlite_backend, attrcache | ~9,000 |
| Networking & I/O | netcommon, ganl_adapter, bsd, telnet, netaddr | ~11,000 |
| Commands | command, speech, look, set, create, move, wiz, rob | ~16,000 |
| Mail & comsys | mail, comsys | ~10,500 |
| Objects & predicates | object, player, flags, predicates, player_c | ~9,000 |
| Configuration & logging | conf, log | ~4,200 |
| Queue & timing | cque, game, timer, timeutil, timeparser | ~8,000 |
| String & utility | stringutil, mathutil, wild, match, help | ~14,000 |
| Module system | modules, libmux, stubslave | ~4,200 |
| Unicode tables | utf8tables (generated) | ~11,000 |
| **Total** | | **~120,000** |


## Phase 2: Improve libmux

### Goals

Make the standard marshaling infrastructure production-ready and reduce the
per-interface boilerplate cost.

### Work Items

#### 2.1 Proxy/stub pairs for remaining cross-process interfaces

Currently custom-marshaled:

- **mux_ISlaveControl** — CStubSlaveProxy in modules.cpp / CStubSlave_Call in
  stubslave.cpp (~350 lines of hand-coded proxy + ~250 lines of stub)
- **mux_IQuerySink** — CQueryClient in modules.cpp / CQuerySinkProxy in
  modules/sqlslave.cpp (~200 + ~150 lines)

These could be converted to standard marshaling (proxy/stub/factory like the
mux_ILog PoC), saving ~500 lines and proving the pattern at scale.

**Go/no-go:** Convert mux_ISlaveControl first.  If the proxy/stub code is
significantly shorter and equally correct, proceed with mux_IQuerySink.
If the reduction is marginal, leave them as custom marshals.

#### 2.2 Marshaling helpers

The mux_ILog proxy/stub revealed repetitive patterns:

- Marshal/unmarshal a UTF8 string (length-prefix + bytes)
- Marshal/unmarshal a scalar (fixed-size Pipe_AppendBytes/Pipe_GetBytes)
- Read method number, dispatch, encode MUX_RESULT return

Consider adding helpers to libmux:

```cpp
// In libmux.h or a new marshal_helpers.h
void Marshal_PutString(QUEUE_INFO *pqi, const UTF8 *str);
bool Marshal_GetString(QUEUE_INFO *pqi, UTF8 *buf, size_t bufSize, const UTF8 **ppStr);
void Marshal_PutUInt32(QUEUE_INFO *pqi, uint32_t val);
bool Marshal_GetUInt32(QUEUE_INFO *pqi, uint32_t *pval);
void Marshal_PutInt(QUEUE_INFO *pqi, int val);
bool Marshal_GetInt(QUEUE_INFO *pqi, int *pval);
```

**Experiment:** Measure the code reduction on mux_ILog proxy/stub.  The static
helpers already in log.cpp (MarshalString/UnmarshalString) show ~50% reduction
in the stub Invoke() method.  If moved to libmux, every interface benefits.

#### 2.3 DEFINE_PROXYSTUB macro (maybe)

A macro analogous to DEFINE_FACTORY that generates the PSFactory boilerplate:

```cpp
DEFINE_PSFACTORY(CLogPSFactory, CLogProxy, CLogStub, IID_ILog)
```

This would eliminate ~80 lines of factory code per interface.  The proxy and
stub themselves are interface-specific and cannot be macro-generated.

**Open question:** Is the boilerplate reduction worth the macro complexity?
With only a handful of marshaled interfaces, probably not yet.

#### 2.4 Harden the pipe protocol

- **Frame length validation:** Pipe_DecodeFrames does not enforce a maximum
  frame length.  A malformed or malicious stubslave could send an unbounded
  frame.  Add a configurable limit (e.g., 1 MB).
- **Channel exhaustion:** nNextChannel is a uint32_t that only increments.
  After 4 billion channel allocations it wraps.  Not a practical concern but
  document the assumption.
- **Error propagation:** When a stub Invoke() fails, the error is returned as
  the transport-level MUX_RESULT.  Distinguish transport errors from
  application errors.


## Phase 3: Grow libmux.so

### The Problem

Modules cannot call netmux functions because those symbols live in the netmux
binary, not in libmux.so.  To decompose the server into modules, those modules
need access to foundational services.

### Two Approaches

#### Approach A: Move source code into libmux.so

Physically move .cpp files from the netmux link into libmux.  The shared
library grows; the netmux binary shrinks.  Modules link against libmux.so and
call the functions directly.

**Advantages:**
- No indirection cost on hot paths (eval, attribute access)
- Straightforward — same code, different link target
- Modules get full C++ API access

**Disadvantages:**
- libmux.so becomes enormous (potentially 80K+ LOC)
- Tight coupling — modules depend on internal data structures
- ABI stability becomes critical (any struct layout change breaks modules)
- Global state (mudconf, mudstate) must live in the shared library
- Harder to run modules out-of-proc (they need the full library)

#### Approach B: Define interfaces for everything

Keep code in netmux.  Expose subsystem access through COM interfaces
(mux_IDatabase, mux_IEvaluator, mux_INotify, etc.).  Modules call through
vtable dispatch.

**Advantages:**
- Clean API boundaries — ABI is the vtable, not struct layouts
- Out-of-proc modules get automatic marshaling
- Can version interfaces independently

**Disadvantages:**
- Vtable indirection cost on hot paths (eval is called millions of times)
- Large interface surface — mux_IDatabase alone might have 30+ methods
- Every new function requires adding to the interface and proxy/stub
- Marshaling cost for cross-process modules on hot paths

#### Approach C: Hybrid (recommended for evaluation)

Move a carefully chosen subset into libmux.so (string utilities, type
definitions, buffer management).  Expose subsystem access through interfaces
for things modules genuinely call (logging, notification, attribute access).
Keep the evaluator and command processor in netmux — modules don't need to
call `mux_exec()` directly.

### What Modules Actually Need

Before moving anything, survey what a realistic module needs.  The existing
modules suggest:

| Need | Current solution | Frequency |
|---|---|---|
| Logging | mux_ILog | Low |
| Server events | mux_IServerEventsSink | Low |
| Custom functions | mux_IFunction | Medium (called per use) |
| SQL queries | mux_IQueryControl | Low |
| Attribute access | Not available | Would be high |
| Notify players | Not available | Would be medium |
| Object properties | Not available | Would be medium |
| Evaluator | Not available | Depends on module type |

**Key experiment:** Write a non-trivial module that needs attribute access and
player notification.  Determine whether interface-based access is acceptable or
whether direct function calls are required.  This informs the A-vs-B decision.

### Candidates for libmux.so Migration

If going with Approach C, these are the lowest-risk candidates:

1. **Type definitions and constants** — config.h typedefs (dbref, FLAG, UTF8),
   buffer size constants.  These are already in headers; just ensure they are
   available to module builds.

2. **String utilities** — stringutil.cpp functions that don't depend on game
   state (mux_stricmp, mux_strncpy, trim helpers, safe_str, etc.).
   ~9,000 LOC but many are pure functions.

3. **Buffer pool** — alloc.cpp (alloc_lbuf, free_lbuf).  Modules producing
   output need buffers.  Currently linked into netmux.

4. **Time utilities** — CLinearTimeAbsolute, CLinearTimeDelta.  Pure value
   types with no game-state dependency.

5. **Math utilities** — mathutil.cpp.  Pure functions.

**Do NOT move early:**
- Evaluator (ast.cpp, eval.cpp) — depends on mudstate deeply
- Database (db.cpp, sqlitedb.cpp) — owns global state
- Networking (bsd.cpp, netcommon.cpp) — owns descriptor state
- Commands — depend on everything

### Open Questions for Phase 3

1. **Global state ownership:** mudconf and mudstate are massive structs living
   in netmux's data segment.  If libmux.so needs to access them, they must
   either (a) move into the .so, (b) be passed as pointers during init, or
   (c) be accessed through an interface.  Option (a) is simplest but means
   the .so and the binary share mutable global state.

2. **ABI stability:** Modules compiled against one version of libmux.so may
   load into a different version.  COM interfaces are ABI-stable by design
   (vtable layout).  Direct function calls are not.  How much do we care?
   For a self-hosted server where admin compiles modules, probably not much.

3. **Build system:** Currently libmux.so is built from a single .cpp file via
   a custom rule in Makefile.am.  Growing it to multiple files requires proper
   automake shared library support (libtool or manual rules).

4. **Testing:** How do we test libmux.so independently?  Currently tested
   indirectly through the smoke tests.  A unit test harness for the shared
   library would be valuable.


## Phase 4: Modularize the Server

### Vision

The netmux binary becomes a thin shell: main loop, signal handling, and module
orchestration.  Subsystems (mail, comsys, commands, etc.) are modules loaded
at startup.

### Natural Module Boundaries

Based on the codebase survey, these subsystems have relatively clean boundaries:

| Module | Current files | LOC | Dependencies |
|---|---|---|---|
| Mail | mail.cpp | 6,000 | db, notify, player |
| Comsys | comsys.cpp | 4,500 | db, notify, player |
| Help | help.cpp | 800 | file I/O, notify |
| Guests | mguests.cpp | 350 | db, player, config |

These subsystems are deeply entangled and harder to extract:

| Subsystem | Why it's hard |
|---|---|
| Evaluator | Accesses mudstate, mudconf, db[] everywhere |
| Commands | 5,000 LOC switch table, calls into every subsystem |
| Database | Owns the db[] array, attribute cache, SQLite |
| Networking | Owns descriptors, interleaved with command processing |
| Objects | Tightly coupled to db, flags, powers |

### Feasibility Assessment

**Honest risks:**

1. **Performance:** COM interface dispatch adds vtable indirection.  For
   subsystems called once per command (mail, comsys), this is negligible.  For
   the evaluator (called per function invocation in softcode), even one extra
   indirection could be measurable.

2. **Complexity budget:** Each module boundary requires an interface definition,
   proxy/stub pair (if cross-process), factory, registration, and lifecycle
   management.  The mux_ILog PoC was ~830 lines for an 8-method interface.
   Applying this to 10 subsystems with 20+ methods each is 8,000-16,000 lines
   of scaffolding.

3. **Testing:** Module interactions are harder to test than monolithic code.
   Need integration tests beyond the current smoke suite.

4. **Diminishing returns:** The server works.  Modularization enables future
   extension but adds complexity now.  The benefit is strongest for subsystems
   that game admins might want to swap (storage backend, networking) and weakest
   for subsystems that are inherently game-specific (commands, eval).

### Recommended Strategy

Start with the subsystems that have the clearest boundaries and lowest coupling:

**Tier 1 — Low risk, clear value:**
- Mail system behind mux_IMail interface
- Comsys behind mux_IComsys interface
- Help system behind mux_IHelp interface

**Tier 2 — Medium risk, moderate value:**
- Attribute access behind mux_IAttributeStore (already partially done
  with IStorageBackend)
- Player notification behind mux_INotify

**Tier 3 — High risk, evaluate carefully:**
- Command processor
- Evaluator
- Object system

**Tier 4 — Leave in netmux:**
- Main loop and signal handling
- Module orchestration
- Networking (too intertwined with the event loop)


## Experiments and Surveys

Before proceeding with Phase 3, run these experiments:

### Experiment 1: Convert mux_ISlaveControl to standard marshaling

**Purpose:** Validate that standard marshaling works for a real cross-process
interface, not just a PoC.

**Success criteria:** Code is shorter, functionality identical, smoke tests
pass.  The stubslave successfully loads and manages modules through the new
proxy/stub.

### Experiment 2: Measure interface dispatch overhead

**Purpose:** Determine whether vtable dispatch is acceptable for hot-path
interfaces.

**Method:** Write a micro-benchmark that calls a trivial method through:
(a) direct function call, (b) virtual method on in-process interface,
(c) cross-process marshal/unmarshal round-trip.

**Expected results:** (a) and (b) should be within noise.  (c) will be
orders of magnitude slower, confirming that cross-process marshaling is
only viable for low-frequency calls.

### Experiment 3: Build a non-trivial module

**Purpose:** Discover what a real module needs from netmux.

**Method:** Implement a module that provides a set of softcode functions
requiring attribute reads and player notification.  Record every place where
the module needs to call back into netmux.

**Outcome:** A concrete list of functions/interfaces that must be available
to modules, informing the Phase 3 migration scope.

### Survey 1: Global state audit

**Purpose:** Catalog all references to mudconf and mudstate from each .cpp file.

**Method:** grep for mudconf\. and mudstate\. across the source tree.  Count
references per file.  Identify which files could function without direct access
to these globals.

### Survey 2: Include dependency graph

**Purpose:** Understand which .cpp files can compile independently of the full
server.

**Method:** Attempt to compile individual .cpp files (stringutil, mathutil,
timeutil) with only libmux.h and config.h.  Record missing dependencies.


## Decision Points

After the experiments:

1. **Phase 2 complete → Phase 3?**  If Experiment 1 succeeds and marshaling
   helpers reduce boilerplate, proceed.  If the standard marshaler proves
   fragile or the code reduction is marginal, reconsider.

2. **Phase 3 scope?**  Survey 1 and 2 determine what can move into libmux.so
   without dragging in the world.  If stringutil and timeutil compile cleanly,
   start there.  If everything depends on mudstate, the hybrid approach (C) may
   not be practical without first refactoring the global state.

3. **Phase 4 at all?**  Experiment 3 determines whether the interface-based
   module model is practical for non-trivial subsystems.  If the interface
   surface is too large or the performance overhead is unacceptable, Phase 4
   may not be worth pursuing beyond Tier 1 (mail, comsys, help).


## Appendix: Interface Registry

Current registered interfaces after the standard marshaler PoC:

| IID | Interface | CID ProxyStub | Status |
|---|---|---|---|
| IID_ILog | mux_ILog | CID_LogPSFactory | Standard marshal PoC |

Current custom-marshaled interfaces:

| Interface | Proxy class | Stub handler | Location |
|---|---|---|---|
| mux_ISlaveControl | CStubSlaveProxy | CStubSlave_Call | modules.cpp / stubslave.cpp |
| mux_IQuerySink | CQueryClient | CQuerySinkProxy | modules.cpp / sqlslave.cpp |

Interfaces not currently marshaled (in-process only):

| Interface | Notes |
|---|---|
| mux_IServerEventsSink | Sink registration, not cross-process |
| mux_IServerEventsControl | Advise takes interface pointer arg |
| mux_IFunction | Called in-process by eval |
| mux_IFunctionsControl | Registration only |
| mux_IQueryControl | Used via custom-marshaled proxy |
