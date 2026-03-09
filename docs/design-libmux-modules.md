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

Built as `libmux.so` — linked into netmux and stubslave at build time, and
loaded by modules at runtime via dlopen.  (The former `libmux.a` static
library was removed; netmux now links directly against the shared object.)
libmux self-registers as a Module during init so its proxy/stub factory
classes coexist with netmux's classes (see g_AprioriModule).

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

#### 2.1 Proxy/stub pairs for remaining cross-process interfaces — COMPLETE

All three cross-process interfaces converted to standard marshaling:

- **mux_ISlaveControl** — proxy/stub/factory in libmux.cpp
- **mux_IQuerySink** — proxy/stub/factory in libmux.cpp
- **mux_IQueryControl** — proxy/stub/factory in libmux.cpp

Each conversion replaced ~400-600 lines of hand-coded proxy/stub with
~200-300 lines of structured proxy/stub/factory classes.  Net reduction of
~500 lines across all three.  The go/no-go was clearly "go" after the first
conversion.

#### 2.2 Marshaling helpers — COMPLETE

Six helpers declared in `libmux.h` and defined in `libmux.cpp`:

```cpp
void Marshal_PutString(QUEUE_INFO *pqi, const UTF8 *str);
bool Marshal_GetString(QUEUE_INFO *pqi, UTF8 *buf, size_t bufSize, const UTF8 **ppStr);
void Marshal_PutUInt32(QUEUE_INFO *pqi, uint32_t val);
bool Marshal_GetUInt32(QUEUE_INFO *pqi, uint32_t *pval);
void Marshal_PutInt(QUEUE_INFO *pqi, int val);
bool Marshal_GetInt(QUEUE_INFO *pqi, int *pval);
```

All proxy/stub code now uses these helpers consistently.  Raw
Pipe_AppendBytes/Pipe_GetBytes calls remain only in the helper
implementations, struct-return patterns, and binary blob patterns
(e.g., AddModule filenames).  Net reduction: ~49 lines across
log.cpp and libmux.cpp.

These six helpers cover all current marshaling needs.  MUX_RESULT
is typedef'd as `int`, so Marshal_PutInt/Marshal_GetInt handle it
directly — no additional helpers were needed.

#### 2.3 DEFINE_PROXYSTUB macro (maybe)

A macro analogous to DEFINE_FACTORY that generates the PSFactory boilerplate:

```cpp
DEFINE_PSFACTORY(CLogPSFactory, CLogProxy, CLogStub, IID_ILog)
```

This would eliminate ~80 lines of factory code per interface.  The proxy and
stub themselves are interface-specific and cannot be macro-generated.

**Open question:** Is the boilerplate reduction worth the macro complexity?
With only a handful of marshaled interfaces, probably not yet.

#### 2.4 Harden the pipe protocol — COMPLETE

- **Frame length validation:** Done (03467ecc).  The DFA-based framing was
  replaced with a simple Type+Channel+Length header.  MAX_FRAME_PAYLOAD
  (1 MB) is enforced in the decoder; oversized frames trigger a protocol
  error.
- **Channel exhaustion:** nNextChannel is a uint32_t that only increments.
  At one allocation per cross-process interface connection, 4 billion is
  not a practical concern.  Accepted as a documented assumption.
- **Error propagation:** Transport errors (MUX_E_INVALIDARG, MUX_E_UNEXPECTED)
  are returned from Invoke() while application errors come back as marshaled
  data in the return frame.  The MUX_RESULT code spaces are distinct enough
  in practice.  Accepted as-is.


## Phase 3: Grow libmux.so — SUBSTANTIALLY COMPLETE

### The Problem

Modules cannot call netmux functions because those symbols live in the netmux
binary, not in libmux.so.  To decompose the server into modules, those modules
need access to foundational services.

### Results (brazil branch, 2026-03-08)

Created `core.h` — a utility-layer subset of externs.h providing base types,
string utilities, time utilities, math, hash/random, buffer management, ANSI
constants, and SHA1.  No game-state dependency.  externs.h includes core.h as
its first action, so existing code is unaffected.

**18 of 19 LIBMUX_SRC files now compile with core.h** (no externs.h):

| Category | Files |
|----------|-------|
| Crypto/hash | sha1, svdrand, svdhash |
| Time | timeutil, timeabsolute, timedelta, timeparser, timezone |
| UTF-8 | utf8_collate, utf8_grapheme, utf8_normalize, utf8tables |
| String | stringutil |
| Math | mathutil, strtod |
| Other | alarm, ast_scan, libmux |

**1 file remains on externs.h:** alloc.cpp — its diagnostic logging uses
STARTLOG/Log/start_log which depend on mudstate.logging, mudconf.log_info,
and the CLogFile class.  Converting it would require also extracting the
logging subsystem, which is not worth the complexity.

**Build system:** Makefile.am splits sources into LIBMUX_SRC (19 files,
compiled as .lo with -fPIC) and NETMUX_SRC (game server files).  libmux.so
exports 513 symbols.  netmux links against libmux.so.

**Decoupling techniques used:**
- Global variables bridging core→server layer: `g_float_precision` (mathutil.h),
  `g_no_flash` / `g_space_compress` (stringutil.h).  Server syncs from mudconf
  after config load; `g_no_flash` written directly by config table.
- Declarations migrated from externs.h to appropriate utility headers:
  IEEE_MAKE_* and strtod functions → mathutil.h; parse_rgb/ColorTable →
  stringutil.h; ansi.h → core.h.
- cf_art_rule() config handler moved from stringutil.cpp to conf.cpp.

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

#### Approach C: Hybrid — CHOSEN DIRECTION

Move a carefully chosen subset into libmux.so (string utilities, type
definitions, buffer management).  Expose subsystem access through interfaces
for things modules genuinely call (logging, notification, attribute access).
Keep the evaluator and command processor in netmux — modules don't need to
call `mux_exec()` directly.

The interface-vs-direct decision is made per API, not globally.  We can
change our mind on any particular API as we go — start with an interface,
move to direct if the indirection hurts, or vice versa.

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

### Phase 3 Questions — Resolved

1. **Global state ownership:** Resolved with option (d): small globals in
   utility headers (g_float_precision, g_no_flash, g_space_compress) that the
   server syncs from mudconf after config load.  mudconf/mudstate stay in
   netmux; core-layer code uses the globals.

2. **ABI stability:** Accepted as non-issue for self-hosted servers where
   admin compiles modules alongside the server.

3. **Build system:** Resolved.  LIBMUX_SRC/NETMUX_SRC split in Makefile.am.
   Pattern rule compiles .cpp → .lo with -fPIC.  libmux.so links all .lo
   files.  CLEANFILES handles .lo cleanup.

4. **Testing:** Tested indirectly through smoke tests (409 pass, 0 fail).
   A unit test harness remains a future possibility.


## Phase 4: Modularize the Server

### Vision

The netmux binary becomes a thin shell: main loop, signal handling, and module
orchestration.  Subsystems (mail, comsys, commands, etc.) are modules loaded
at startup.

### Natural Module Boundaries

Based on the codebase survey, these subsystems have relatively clean boundaries:

| Module | Current files | LOC | Dependencies | Status |
|---|---|---|---|---|
| Comsys | comsys.cpp | 4,500 | db, notify, player | **COMPLETE** |
| Mail | mail.cpp | 6,000 | db, notify, player | **COMPLETE** |
| Help | help.cpp | 800 | file I/O, notify | **COMPLETE** (server-provided) |
| Guests | mguests.cpp | 350 | db, player, config | Future |

These subsystems are deeply entangled and harder to extract:

| Subsystem | Why it's hard |
|---|---|
| Evaluator | Accesses mudstate, mudconf, db[] everywhere |
| Commands | 5,000 LOC switch table, calls into every subsystem |
| Database | Owns the db[] array, attribute cache, SQLite |
| Networking | Owns descriptors, interleaved with command processing |
| Objects | Tightly coupled to db, flags, powers |

### Comsys Module — COMPLETE (brazil branch, 2026-03-08)

The channel communication system is fully extracted into `comsys_mod.so`.

**Files:** `mux/src/modules/comsys_mod.h` (~260 lines),
`mux/src/modules/comsys_mod.cpp` (~3,200 lines)

**Interface:** `mux_IComsysControl` (19 methods):

| Method | Server command |
|---|---|
| Initialize | Module startup (opens own SQLite connection) |
| PlayerConnect / PlayerDisconnect / PlayerNuke | Connection lifecycle |
| AddAlias / DelAlias / ClearAliases | addcom / delcom / clearcom |
| CreateChannel / DestroyChannel | @ccreate / @cdestroy |
| AllCom / ComList / ComTitle | allcom / comlist / comtitle |
| ChanList / ChanWho / CEmit | @clist / @cwho / @cemit |
| CSet | @cset (public/private/loud/quiet/spoof/object/header/log) |
| EditChannel | @ccharge / @cchown / @coflags / @cpflags |
| CBoot | @cboot |
| ProcessCommand | Alias-based say/pose/who/on/off/last dispatch |

**Architecture:**
- Module links against system `-lsqlite3` (no `-rdynamic`)
- Opens its own SQLite connection with WAL mode + busy_timeout
- Owns all channel data: `m_channels` map, `m_comsys_table[500]` hash
- Full write-through: every mutation persisted via prepared statements
- Receives connect/disconnect/shutdown events via `mux_IServerEventsSink`
- Uses 7 COM interfaces for server callbacks: ILog, IServerEventsControl,
  INotify, IObjectInfo, IAttributeAccess, IEvaluator, IPermissions
- Server fallback: all command handlers check `mudstate.pIComsysControl`
  and delegate if non-null; built-in code runs when module is not loaded

**What the comsys extraction proved:**
- The COM interface pattern scales to a 19-method interface without
  excessive boilerplate
- `mux_IObjectInfo::MatchThing` was added to bridge the name-matching
  gap (modules can't call init_match/match_everything directly)
- Modules can own their own SQLite connections alongside the server's
- The server delegation pattern (null-check + early return) is clean
  and preserves backward compatibility

### Mail Module — COMPLETE (brazil branch, 2026-03-08)

The @mail system is fully extracted into `mail_mod.so`.

**Files:** `mux/src/modules/mail_mod.h` (~400 lines),
`mux/src/modules/mail_mod.cpp` (~5,800 lines)

**Interfaces:**

`mux_IMailControl` (11 methods, implemented by mail_mod.so):

| Method | Server command |
|---|---|
| Initialize | Module startup (opens own SQLite connection, loads data) |
| PlayerConnect | Check/announce new mail on connect |
| PlayerNuke | Destroy all mail for a player |
| MailCommand | All @mail switches (31 cases: read/list/send/reply/forward/etc.) |
| MaliasCommand | All @malias switches (9 cases: create/delete/add/remove/etc.) |
| FolderCommand | All @folder switches (set/read/file/list) |
| CheckMail | Count and announce unread/urgent mail |
| ExpireMail | Expire old messages |
| CountMail | Return read/unread/cleared counts |
| DestroyPlayerMail | Remove all mail for a destroyed player |

`mux_IMailDelivery` (5 methods, implemented by netmux server):

| Method | Server function wrapped |
|---|---|
| MailCheck | could_doit(A_LMAIL) + mail_return (MFAIL evaluation) |
| NotifyDelivery | raw_notify with Moniker + did_it(A_MAIL, A_AMAIL) |
| IsComposing | Flags2(player) & PLAYER_MAILS |
| SetComposing | Set/clear PLAYER_MAILS in Flags2 |
| ThrottleCheck | ThrottleMail() |

**Architecture:**
- Module links against system `-lsqlite3` (no `-rdynamic`)
- Opens its own SQLite connection with WAL mode + busy_timeout
- Owns all mail data: `m_mail_htab` map, `m_mail_list` body storage,
  `m_malias` alias array
- Full write-through: every mutation persisted via prepared statements
- Receives connect/disconnect/shutdown events via `mux_IServerEventsSink`
- Uses 8 COM interfaces for server callbacks: ILog, IServerEventsControl,
  INotify, IObjectInfo, IAttributeAccess, IEvaluator, IPermissions,
  IMailDelivery
- Server fallback: all command handlers check `mudstate.pIMailControl`
  and delegate if non-null; built-in code runs when module is not loaded
- Signature evaluation uses IEvaluator (mux_exec via COM interface)
- String editing (@mail/edit) uses simple find-and-replace, no mux_exec

**What the mail extraction proved:**
- The mux_IMailDelivery pattern (server-provides-interface-to-module)
  cleanly separates data ownership from server-internal operations like
  lock evaluation, attribute triggers, and flag management
- Bidirectional interfaces work: module implements mux_IMailControl for
  the server to call, server implements mux_IMailDelivery for the module
  to call back
- The full @mail command surface (31 cases) fits comfortably behind a
  single MailCommand(executor, key, arg1, arg2) dispatch method
- Composition state (A_MAILTO/A_MAILSUB/A_MAILMSG/A_MAILFLAGS) stored
  on player attributes via IAttributeAccess works without server-internal
  attribute functions

### Help System — COMPLETE (brazil branch, 2026-03-08)

The help system is exposed as a server-provided interface.  Unlike comsys and
mail (which were extracted into modules), help stays in netmux but is
accessible to any module via `mux_IHelpSystem`.

**Interface:** `mux_IHelpSystem` (4 methods, implemented by netmux):

| Method | Server function wrapped |
|---|---|
| LookupTopic | help_helper() — topic lookup with alloc_lbuf buffer |
| FindHelpFile | mudstate.aHelpDesc[] iteration by CommandName |
| GetHelpFileCount | mudstate.nHelpDesc |
| ReloadIndexes | helpindex_load() |

**Implementation:** `CHelpSystem` class in modules.cpp + `CHelpSystemFactory`.
Registered as `CID_HelpSystem` in `netmux_classes[]`.

**Proof of concept:** The exp3 module's `mhelp()` softcode function demonstrates
cross-module help access.  `mhelp(topic)` looks up a topic in the default help
file; `mhelp(helpfile, topic)` looks up in a named help file.  Two smoke tests
verify topic lookup and nonexistent-topic error handling.

**What the help system proved:**
- The "server-provided interface" pattern (code stays in netmux, accessible
  via COM) is a viable alternative to full extraction for tightly coupled
  subsystems
- Help is deeply coupled to config (help file registration), command dispatch
  (help command names), and the evaluator (topic matching) — extraction would
  be high cost for little benefit
- A 4-method interface wrapping existing functions is minimal overhead

### Feasibility Assessment

**Honest risks:**

1. **Performance:** COM interface dispatch adds vtable indirection.  For
   subsystems called once per command (mail, comsys), this is negligible.  For
   the evaluator (called per function invocation in softcode), even one extra
   indirection could be measurable.

2. **Complexity budget:** Each module boundary requires an interface definition,
   factory, registration, and lifecycle management.  In practice, the comsys
   extraction showed that a 19-method interface + ~3,200 lines of module code
   is manageable.  The scaffolding overhead is modest relative to the logic.

3. **Testing:** 411 smoke tests pass with comsys and mail modules loaded.
   The delegation pattern (null-check + fallback) means unloading a module
   restores built-in behavior.

4. **Diminishing returns:** The comsys extraction proved the pattern works.
   Mail follows the same structure with more commands but the same architecture.

### Recommended Strategy

**Tier 1 — Low risk, clear value:**
- ~~Comsys behind mux_IComsysControl interface~~ — **COMPLETE**
- ~~Mail system behind mux_IMailControl interface~~ — **COMPLETE**
- Help system behind mux_IHelpSystem interface — **COMPLETE** (server-provided)

**Tier 2 — Medium risk, already done as infrastructure:**
- ~~Attribute access behind mux_IAttributeAccess~~ — **COMPLETE** (in-process)
- ~~Player notification behind mux_INotify~~ — **COMPLETE** (in-process)
- ~~Object info behind mux_IObjectInfo~~ — **COMPLETE** (in-process)
- ~~Evaluator behind mux_IEvaluator~~ — **COMPLETE** (in-process)
- ~~Permissions behind mux_IPermissions~~ — **COMPLETE** (in-process)

**Tier 3 — High risk, evaluate carefully:**
- Command processor
- Object system

**Tier 4 — Leave in netmux:**
- Main loop and signal handling
- Module orchestration
- Networking (too intertwined with the event loop)
- Evaluator (hot path, deeply coupled to mudstate)


## Phase 5: Driver/Engine Split (Mitosis)

### Motivation

Phases 1–4 decomposed netmux from the bottom up: extracting subsystems (comsys,
mail) into modules behind COM interfaces.  That approach has reached diminishing
returns — the remaining subsystems (commands, objects, evaluator, database) are
deeply entangled with each other and not worth extracting individually.

The next move is from the top down.  Inspired by the LPMud driver/game
architecture, split netmux into two layers:

```
netmux (driver)         — main(), networking, signals, module orchestration
  ↓ COM interface
engine.so (game engine) — database, commands, evaluator, scheduler, objects
  ↓ direct link
libmux.so (core)        — strings, time, math, hash, COM infrastructure
```

**Key constraints:**
- `engine.so` links against `libmux.so` only.  Exports `mux_Register` /
  `mux_Unregister` like any module — standard COM front door.
- `netmux` links against `libmux.so` only.  Loads `engine.so` via COM.
  **No `-rdynamic`.**  No symbol exports from netmux to engine.
- Engine calls back to driver through COM interfaces, same pattern as
  comsys_mod and mail_mod calling mux_INotify or mux_IObjectInfo.
- All existing modules (comsys_mod.so, mail_mod.so, exp3.so) continue to
  link against libmux.so and communicate with the engine through the same
  COM interfaces they use today.

### Guiding Principle

Dbrefs are quintessentially game.  Sockets, TLS, telnet, and charsets are
quintessentially driver.  The engine only wants UTF-8.  The driver only wants
"send this text to connection X" and "connection Y sent this command."

The biggest violation of this division today is the DESC struct, which mixes
network state (fd, TLS context, telnet negotiation, NAWS dimensions) with
game state (player dbref, command quota, doing string, output prefix/suffix).
The engine should see connections as opaque handles — COM pointers or integer
IDs — never touching socket-level details.

### Pre-Mitosis Stages

Before the physical split, three preparation stages bring the codebase to a
state where mitosis is mechanical.  At the end of Stage 3, every file is
classified Driver or Engine, the headers are clean, and the code compiles
exactly as before — still one binary.  Nothing has changed from the linker's
perspective.  The actual .so split becomes trivial.

---

### Stage 1: Classification (Documentation)

Establish a clear mental model of what belongs to Driver vs Engine.  Every
source file, every struct, every global — classify it.  Document the boundary
violations.  This is a state of mind, not a code change.

#### File Classification

**Driver (stays in netmux):**

| File | Role |
|------|------|
| driver.cpp | main(), startup orchestration, shutdown sequence |
| ganl_adapter.cpp | Event loop, I/O multiplexing, connection lifecycle |
| bsd.cpp | Descriptor lifecycle, socket I/O, output flushing |
| telnet.cpp | NVT state machine, option negotiation |
| netaddr.cpp | IP/IPv6 address parsing |
| signals.cpp | POSIX signal handlers |
| sitemon.cpp | IP-level site bans |
| slave.cpp | DNS resolver child process |
| modules.cpp | COM infrastructure, module loading, interface factories |
| _build.cpp | Version stamp |

**Engine (moves to engine.so):**

| File | Role |
|------|------|
| db.cpp, db_rw.cpp | Database core, object creation/destruction |
| sqlitedb.cpp, sqlite_backend.cpp | SQLite storage layer |
| attrcache.cpp | Attribute cache and LRU |
| ast.cpp, ast_scan.cpp, eval.cpp | Parser and evaluator |
| functions.cpp, funceval.cpp, funceval2.cpp, funmath.cpp | Softcode functions |
| command.cpp | Command dispatch table and processing |
| speech.cpp, look.cpp, set.cpp, create.cpp, move.cpp | Game commands |
| wiz.cpp, rob.cpp, quota.cpp | Admin/economy commands |
| object.cpp, player.cpp, player_c.cpp | Object/player lifecycle |
| flags.cpp, powers.cpp | Flag and power systems |
| cque.cpp | Command queue and scheduler |
| timer.cpp | Scheduled tasks, idle checks, @daily |
| conf.cpp | Configuration parsing (mudconf) |
| mail.cpp, comsys.cpp | Built-in subsystem fallbacks |
| help.cpp | Help file indexing and lookup |
| log.cpp | Logging subsystem |
| wild.cpp, match.cpp | Wildcard and name matching |
| boolexp.cpp | Lock evaluation |
| unparse.cpp, walkdb.cpp, vattr.cpp | DB utilities |
| predicates.cpp | Object predicates and permission checks |
| mguests.cpp | Guest system |
| file_c.cpp | File cache (connect.txt, motd.txt, etc.) |
| htab.cpp | Hash tables (NAMETAB-based) |
| plusemail.cpp | Email validation |
| local.cpp | Local extensions hook |
| levels.cpp | Reality levels (optional) |
| version.cpp | Version string |

**Hybrid (need splitting or reclassification):**

| File | Driver part | Engine part |
|------|------------|-------------|
| netcommon.cpp | — | update_quotas, raw_notify, announce_connect/disconnect, welcome_user, save_command |
| driver.cpp / engine.cpp | main(), ganl calls | load_game, dump_database, notify_check |

#### The DESC Boundary

The DESC struct (interface.h:142–188) is the primary boundary violation.
Current fields classified:

**Network-side (stays in Driver):**
- `socket` (SOCKET) — file descriptor
- `ss` (SocketState) — TLS state machine
- `nOption`, `aOption[]` — Telnet negotiation state
- `raw_input_buf`, `raw_input_at`, `raw_input_state`, `raw_codepoint_*` — NVT parser state
- `nvt_him_state[256]`, `nvt_us_state[256]` — Telnet option states
- `ttype` — Terminal type string
- `encoding`, `negotiated_encoding`, `charset_request_pending` — Charset state
- `width`, `height` — NAWS terminal dimensions
- `address` (mux_sockaddr), `addr[]`, `username[]` — Remote endpoint
- `connected_at` — Connection timestamp
- `output_queue`, `output_size`, `output_tot`, `output_lost` — Output buffer
- `input_queue`, `input_size`, `input_tot`, `input_lost` — Input buffer

**Game-side (belongs to Engine):**
- `player` (dbref) — Connected player object
- `flags` (DS_CONNECTED, DS_AUTODARK, DS_PUEBLOCLIENT)
- `quota` (int) — Command quota remaining
- `command_count` — Session command counter
- `timeout` — Idle timeout in seconds
- `retries_left` — Login retry counter
- `output_prefix`, `output_suffix` — Per-session output decoration
- `doing[]` — DOING string
- `program_data` — Interactive editing state (@edit)
- `last_time` — Last command timestamp (used by both sides)

**Resolution:** The engine sees connections as opaque handles.  A
driver-provided COM interface (e.g., `mux_IConnectionManager`) lets the
engine query connection state and send output without touching DESC fields
directly.  Alternatively, DESC splits into a driver-side struct and an
engine-side companion indexed by connection handle.

#### Descriptor Access from Engine Files — Detailed Audit

Every engine file that reaches into DESC structs or descriptor collections.
Severity reflects how deep the violation goes:

**CRITICAL — writes to DESC fields or driver collections:**

| Engine file | Function | What it does |
|-------------|----------|--------------|
| predicates.cpp | handle_prog() | Reads `d->player`, writes `d->program_data`, calls `queue_string`/`queue_write_LEN`, iterates `dbref_to_descriptors_map` |
| predicates.cpp | do_quitprog() | Iterates `dbref_to_descriptors_map`, writes `d->program_data = nullptr` |
| predicates.cpp | do_prog() | Iterates `dbref_to_descriptors_map`, writes `d->program_data`, calls `queue_string`/`queue_write_LEN` |
| db.cpp | load_restart_db() | Creates DESC structs, fills all fields from file, inserts into `descriptors_list`/`descriptors_map`, calls `shutdownsock()` |

**HIGH — calls driver output functions:**

| Engine file | Function | What it does |
|-------------|----------|--------------|
| db.cpp | dump_restart_db() | Iterates `descriptors_list`, reads 20+ DESC fields to persist across @restart |
| file_c.cpp | fcache_dump() | Takes DESC* param, calls `queue_write_LEN(d, ...)` |
| file_c.cpp | fcache_send() | Iterates `dbref_to_descriptors_map`, calls `fcache_dump(d, ...)` |
| mguests.cpp | CGuests::Create() | Takes DESC* param, calls `queue_string(d, ...)` for error messages |
| timer.cpp | heartbeat() | Iterates `descriptors_list`, reads `d->flags`/`d->player`, calls `queue_write_LEN` for keepalive NOPs |
| netcommon.cpp | raw_notify() | Iterates `dbref_to_descriptors_map`, calls `queue_string(d, msg)` |
| netcommon.cpp | shutdownsock() 5x, process_output() 2x | Calls driver functions for connection lifecycle |

**MEDIUM — reads DESC fields via collection iteration:**

| Engine file | Function | What it does |
|-------------|----------|--------------|
| functions.cpp | connection_func() (7 variants) | WHO, CONNCOUNT, DOING, WHERE, LASTSITE, INFO, LOCATIONS — iterates `descriptors_list`, reads `d->flags`, `d->player` |
| speech.cpp | raw_broadcast() | Iterates `descriptors_list`, reads `d->flags`/`d->player` for room broadcast |

**LOW — minimal descriptor collection access:**

| Engine file | Function | What it does |
|-------------|----------|--------------|
| flags.cpp | flag set/clear (2 sites) | `dbref_to_descriptors_map.equal_range()` to find connected sessions |
| flags.cpp | charset flag change (1 site) | Calls `send_charset_request(d)` — driver function |
| command.cpp | @list stats (1 site) | `dbref_to_descriptors_map.size()` for descriptor count |

**Key observations:**

1. `program_data` is game state (interactive @prog/@edit) stored on a driver
   struct.  It must move to engine-side storage indexed by connection handle.

2. `dump_restart_db` / `load_restart_db` are inherently boundary operations —
   they serialize the full DESC state across exec().  These will need
   cooperation between driver and engine.

3. `queue_string` / `queue_write_LEN` are the most common cross-boundary
   calls.  They appear in 6 engine files.  A driver-provided "send output to
   connection" interface eliminates all of them.

4. No engine files `#include "interface.h"` directly.  DESC visibility comes
   through `externs.h` → `mudconf.h` → forward declarations.

#### Global State Ownership — STATEDATA Audit

STATEDATA (mudstate) contains ~120 fields.  Classification:

**Driver fields (3) — must migrate to driver ownership:**

| Field | Type | Notes |
|-------|------|-------|
| descriptors_list | list\<DESC*\> | All active descriptors |
| descriptors_map | unordered_map\<DESC*, iterator\> | Reverse map for O(1) erase |
| dbref_to_descriptors_map | multimap\<dbref, DESC*\> | Player-to-connection lookup |

**Shared fields (12) — both sides need access:**

| Field | Type | Who sets | Who reads |
|-------|------|----------|-----------|
| shutdown_flag | bool | Engine (conf.cpp, @shutdown) | Driver (ganl_adapter main loop) |
| restarting | bool | Engine (db.cpp) | Driver (ganl_adapter), Engine (game.cpp) |
| dumping | bool | Engine (game.cpp) | Driver (signals.cpp), Engine (predicates) |
| bCanRestart | bool | Driver (signals.cpp) | Engine (predicates.cpp) |
| panicking | bool | Driver (signals.cpp) | Engine (conf.cpp) |
| dumper | dbref | Engine (game.cpp) | Driver (signals.cpp) |
| dumped | dbref | Engine (game.cpp) | Driver (signals.cpp) |
| pISlaveControl | mux_ISlaveControl* | Engine (conf.cpp) | Driver (modules.cpp) |
| pIQueryControl | mux_IQueryControl* | Engine (conf.cpp) | Shared |
| pIComsysControl | mux_IComsysControl* | Engine (conf.cpp) | Shared |
| pIMailControl | mux_IMailControl* | Engine (conf.cpp) | Shared |
| restart_count | uint | Driver | Engine |

**Engine fields (~105) — pure game state:**

All remaining fields: execution context (curr_executor, curr_enactor),
database state (db_top, db_size, freelist), evaluation limits (func_invk_ctr,
func_nest_lev, nStackNest), hash tables (command_htab, player_htab,
builtin_functions, etc.), attribute cache, timing counters, bit caches,
global registers, and string buffers.

**Driver-relevant CONFDATA fields (~13):**

| Field | Type | Notes |
|-------|------|-------|
| ports | vector\<int\> | Listening ports |
| sslPorts | vector\<int\> | SSL listening ports |
| ssl_certificate_file/key/password | UTF8[] | TLS configuration |
| ip_address | UTF8* | Bind address |
| pid_file | const UTF8* | PID file path |
| conn_timeout | int | Login timeout |
| retry_limit | int | Bad login retries |
| max_players | int | Connection limit |
| use_hostname | bool | DNS display preference |
| control_flags | int | CF_LOGIN etc. |
| site_file, conn_file, creg_file | UTF8* | Connection display files |

All other CONFDATA fields (~60) are pure engine (game costs, quotas, flags,
defaults, command limits, timing intervals, messages).

#### Engine→Driver Callback Surface

Functions defined in driver files that engine code calls.  This is the
interface surface that must become COM methods:

**From bsd.cpp — connection lifecycle:**

| Function | Called from (engine) | Frequency | What it does |
|----------|---------------------|-----------|--------------|
| shutdownsock(DESC*, reason) | netcommon.cpp (5x), db.cpp (1x) | Per disconnect | Close connection, log, trigger disconnect events |
| process_output(DESC*, flag) | netcommon.cpp (2x) | Per output flush | Write output queue to socket via GANL |

**From bsd.cpp — output queueing:**

| Function | Called from (engine) | Frequency | What it does |
|----------|---------------------|-----------|--------------|
| queue_string(DESC*, UTF8*) | predicates.cpp (6x), mguests.cpp (4x), netcommon.cpp | Per output line | Append UTF-8 string to DESC output queue |
| queue_write_LEN(DESC*, data, len) | predicates.cpp (3x), file_c.cpp (1x), timer.cpp (1x) | Per output chunk | Append raw bytes to DESC output queue |

**From telnet.cpp — protocol negotiation:**

| Function | Called from (engine) | Frequency | What it does |
|----------|---------------------|-----------|--------------|
| send_charset_request(DESC*, bool) | flags.cpp (1x) | Per charset flag change | Send CHARSET telnet negotiation |

**From ganl_adapter.cpp — lifecycle (called from game.cpp only):**

| Function | Called from | Frequency | What it does |
|----------|------------|-----------|--------------|
| ganl_initialize() | game.cpp | Once at startup | Initialize GANL networking |
| ganl_main_loop() | game.cpp | Once (blocks) | Main event loop |
| ganl_shutdown() | game.cpp | Once at exit | Tear down networking |

**From signals.cpp — setup (called from game.cpp only):**

| Function | Called from | Frequency | What it does |
|----------|------------|-----------|--------------|
| build_signal_names_table() | game.cpp | Once | Populate signal name table |
| set_signals() | game.cpp | Once | Install signal handlers |

**From modules.cpp — module lifecycle (called from game.cpp only):**

| Function | Called from | Frequency | What it does |
|----------|------------|-----------|--------------|
| init_modules() | game.cpp | Once | Initialize COM subsystem |
| final_modules() | game.cpp, predicates.cpp, signals.cpp | Once | Release all modules |

**Proposed driver-provided COM interface:**

The 4 high-frequency functions (shutdownsock, process_output, queue_string,
queue_write_LEN) plus the descriptor query operations map naturally to a
single interface:

```
mux_IConnectionManager:
    SendText(conn_handle, UTF8 *text)        — replaces queue_string
    SendRaw(conn_handle, data, len)          — replaces queue_write_LEN
    FlushOutput(conn_handle)                 — replaces process_output
    CloseConnection(conn_handle, reason)     — replaces shutdownsock
    GetConnectionCount(dbref player)         — replaces equal_range().count
    GetConnectionInfo(dbref player, index)   — replaces descriptor iteration
    GetTotalConnections()                    — replaces descriptors_list.size
    IsConnected(dbref player)                — replaces equal_range check
    SetCharset(conn_handle, charset)         — replaces send_charset_request
```

The game.cpp lifecycle calls (ganl_initialize, set_signals, init_modules)
stay as direct function calls — game.cpp is a driver file.

#### netcommon.cpp: The Key Hybrid

netcommon.cpp is misnamed — it is not "network-independent common code."  It
contains critical engine functions that happen to operate on descriptors:

| Function | Classification | Why |
|----------|---------------|-----|
| update_quotas() | Engine | Quota recharge is game policy, but iterates DESCs |
| raw_notify() | Engine | Message delivery by dbref, but queues on DESC |
| raw_notify_html() | Engine | Same, HTML variant |
| announce_connect() | Engine | Player login bookkeeping, A_TIMEOUT, cache_preload |
| announce_disconnect() | Engine | Triggers A_ADISCONNECT, broadcasts, zone notify |
| welcome_user() | Boundary | Sends connect.txt file to new connection |
| save_command() | Boundary | Queues raw input line for game processing |
| process_input_helper() | Boundary | NVT parsing → command queue entry |

The resolution is an interface.  announce_connect/disconnect are engine
functions that should call back to the driver through a COM interface for
descriptor-specific operations (count sessions, send raw output, flag a
descriptor as connected).

---

### Stage 2: In-Place Separation

With the classification established, go through the code and separate things
that combine Engine and Driver concerns.  No files move.  Everything still
compiles into netmux.  Smoke tests pass after every change.

**Work items:**

1. **Split DESC game-state from network-state.**  Either create a parallel
   game-state struct indexed by connection handle, or define accessor
   functions/interfaces that the engine calls instead of touching DESC
   fields directly.

2. **Move descriptor collections to driver ownership.**  The descriptor
   lists currently live in mudstate.  Move them out (or wrap them behind
   accessor functions) so engine code never iterates descriptors directly.

3. **Split netcommon.cpp.**  Game-side functions (announce_connect,
   announce_disconnect, raw_notify, update_quotas) move into an engine
   file.  Connection-level operations they need become interface calls.

4. **Split shutdownsock() in bsd.cpp.**  Lines doing game cleanup
   (announce_disconnect, attribute writes, accounting) become an engine
   function called from the driver's shutdown sequence.

5. **Wrap descriptor queries.**  Functions in functions.cpp (WHO, IDLE,
   CONN, PORTS) and predicates.cpp (connected test) currently iterate
   descriptors.  These should call through a connection-query interface.

6. **Create new headers as needed.**  Separate driver declarations from
   engine declarations.  This may mean splitting interface.h further or
   creating a connection_iface.h for the engine↔driver boundary.

Each item is a small, testable change.

---

### Stage 3A: File Splits

Split the three hybrid files so every source file is cleanly Driver or Engine.
All files remain in `mux/src/` — no subdirectory moves until the physical
`.so` split.

**netcommon.cpp → net.cpp (driver) + session.cpp (engine):**

net.cpp (driver) — owns descriptor collections, I/O queuing, connection
lifecycle:

| Function | Role |
|----------|------|
| `clearstrings`, `init_desc`, `destroy_desc`, `freeqs` | DESC memory |
| `add_to_output_queue`, `queue_write_LEN`, `queue_write`, `encode_iac` | I/O queuing |
| `queue_string` (×2) | Encoded output |
| `desc_addhash`, `desc_delhash` | Hash management |
| `save_command`, `set_userstring` | Input handling |
| `parse_connect`, `check_connect`, `failconn` | Login processing |
| `do_logged_out_internal`, `do_command`, `Task_ProcessCommand` | Command dispatch |
| `welcome_user` | Connect screens |
| `init_logout_cmdtab`, `logged_out0`, `logged_out1` | Logout command table |
| `check_idle` | Idle check (iterates descriptors) |
| `dump_users`, `dump_info` | WHO display |
| `boot_off`, `boot_by_port`, `desc_reload` | Connection management |
| `find_oldest`, `find_least_idle` | Descriptor queries |
| All 12 Stage 2 accessor functions | Boundary layer |
| `mux_subnets` class | Subnet matching |

session.cpp (engine) — game logic, notification, softcode:

| Function | Role |
|----------|------|
| `raw_notify` (×2), `raw_notify_html`, `raw_notify_newline` | Notification |
| `raw_broadcast` | Broadcast messages |
| `announce_connect`, `announce_disconnect` | Game-level connect/disconnect |
| `update_quotas` | Quota management |
| `make_portlist`, `make_port_ulist`, `make_ulist` | List builders |
| `fetch_session`, `fetch_idle`, `fetch_connect`, `fetch_height`, `fetch_width` | Softcode fetch |
| `fetch_cmds`, `fetch_ConnectionInfoFields`, `fetch_ConnectionInfoField` | Softcode fetch |
| `fetch_logouttime`, `ParseConnectionInfoString` | Softcode fetch |
| `find_connected_name` | Name matching |
| `MakeCanonicalDoing`, `do_doing` | @doing |
| `check_events` | @daily, etc. |
| `list_siteinfo` | @list |
| `trimmed_name`, `trimmed_site` | Display helpers |
| `fun_doing`, `fun_host`, `fun_poll`, `fun_motd`, `fun_siteinfo` | Softcode functions |

**game.cpp → driver.cpp (driver) + engine.cpp (engine): COMPLETE**

driver.cpp (driver, 1179 lines) — program entry, CLI, orchestration:

| Function | Role |
|----------|------|
| `main()` | Program entry point, startup/shutdown orchestration |
| `dbconvert()` | Standalone database conversion tool |
| `CLI_CallBack`, `OptionTable` | Command-line option parsing |
| `write_pidfile` | PID file management |
| `init_sql` | MySQL initialization (INLINESQL) |
| `info` | Database format display |
| `init_rlimit` | File descriptor limit setup |
| `mux_fopen`, `mux_open`, `mux_strerror` | I/O utilities |

engine.cpp (engine, 2215 lines) — game logic, notification, matching, dumps:

| Function | Role |
|----------|------|
| `do_dump`, `dump_database`, `dump_database_internal`, `fork_and_dump` | Database dumps |
| `load_game`, `clear_sqlite_after_sync_failure` | Database loading |
| `process_preload` | STARTUP processing |
| `notify_check` (×2), `notify_except`, `notify_except2`, `notify_except_N` | Notification dispatch |
| `check_filter`, `make_prefix`, `html_escape` | Notification helpers |
| `report` | Error reporting |
| `regexp_match`, `atr_match1`, `atr_match`, `list_check` | Attribute matching |
| `Hearer` | Listener detection |
| `do_shutdown` | @shutdown command |
| `do_timecheck`, `report_timecheck` | CPU time reporting |
| `do_readcache` | @readcache command |

**Note:** bsd.cpp should be audited for dead code at a stopping point.
All networking has migrated to GANL; any remaining select()-based code in
bsd.cpp is likely dead.

### Stage 3B: Boundary Hardening

Reduce cross-side visibility so Driver and Engine files don't leak
declarations into each other.

**Work items:**

1. **Header split.**  Split externs.h into engine.h and driver.h.
   externs.h is already mostly engine declarations.  Pull driver-only
   declarations (DESC, socket operations, telnet state) into driver.h.
   Engine files include engine.h only.  Driver files include both.

2. **Static declarations.**  Functions that are internal to one side
   become `static` or move to anonymous namespaces.  Eliminate extern
   declarations that cross the boundary unnecessarily.

3. **Verify the boundary.**  Every engine file compiles without including
   driver.h or interface.h.  Every driver file can call engine functions
   through the engine.h declarations (and eventually mux_IGameEngine).

4. **Define mux_IGameEngine.**  The interface the driver uses to call the
   engine:

   | Method | Wraps |
   |--------|-------|
   | LoadGame(configPath) | cf_read, load_game, module discovery |
   | Startup() | local_startup, module startup, init_timer |
   | RunTasks(ltaNow) | scheduler.RunTasks() |
   | UpdateQuotas(ltaLast, ltaNow) | update_quotas() |
   | WhenNext(pltaNext) | Scheduler query for next task time |
   | DumpDatabase() | dump_database() |
   | Shutdown() | local_shutdown, module shutdown, cleanup |
   | ShouldShutdown(pbFlag) | mudstate.shutdown_flag |

5. **Implement CGameEngine in-process.**  A COM class wrapping the above
   functions.  main() creates it via mux_CreateInstance and calls through
   the interface.  Still one binary — but the interface boundary is live
   and tested.

At the end of Stage 3, the codebase is ready for mitosis.  The physical
split into engine.so is a build-system change: move engine files to a new
Makefile target, link as shared library, done.

---

### Phase 5 Proper: The Split

With Stages 1–3 complete, the actual mitosis is mechanical:

1. **Build system:** Add engine.so target to Makefile.am.  Engine files
   compile as .lo with -fPIC.  engine.so links against libmux.so.
   netmux links against libmux.so only, loads engine.so via COM.

2. **Module front door:** engine.so exports mux_Register/mux_Unregister.
   Registers CID_GameEngine.  netmux calls mux_CreateInstance(CID_GameEngine)
   to get mux_IGameEngine.

3. **Driver-provided interfaces.**  netmux implements and registers COM
   interfaces that the engine acquires during Initialize:
   - mux_IConnectionManager — send output, query sessions, boot connections
   - mux_ILog — already exists
   - mux_IServerEventsControl — already exists

4. **Existing modules unchanged.**  comsys_mod.so, mail_mod.so, exp3.so
   continue to link against libmux.so and acquire engine-side interfaces
   (INotify, IObjectInfo, etc.) through COM.  The fact that those interfaces
   now live in engine.so instead of netmux is invisible to them.

5. **Smoke tests pass.**  The same 411 tests verify identical behavior.

### Future: JIT

Once the engine is a proper .so, it becomes the natural home for platform-
specific JIT backends (ARM, Intel, RISC-V).  The evaluator (ast.cpp, eval.cpp)
is already self-contained within the engine boundary.  JIT compilation replaces
the AST interpreter without touching the driver or libmux at all.


## Experiments and Surveys

Before proceeding with Phase 3, run these experiments:

### Experiment 1: Convert mux_ISlaveControl to standard marshaling — COMPLETE

**Purpose:** Validate that standard marshaling works for a real cross-process
interface, not just a PoC.

**Success criteria:** Code is shorter, functionality identical, smoke tests
pass.  The stubslave successfully loads and manages modules through the new
proxy/stub.

**Results (brazil branch):** mux_ISlaveControl, mux_IQuerySink, and
mux_IQueryControl were all converted from custom to standard marshaling.
Each conversion replaced ~400-600 lines of hand-coded proxy/stub with
~200-300 lines of structured proxy/stub/factory classes.  All smoke tests
pass.  This also surfaced the g_MainModule single-handler bug (fixed by
the libmux self-loading approach described in Experiment 3).

### Experiment 2: Measure interface dispatch overhead

**Purpose:** Determine whether vtable dispatch is acceptable for hot-path
interfaces.

**Method:** Write a micro-benchmark that calls a trivial method through:
(a) direct function call, (b) virtual method on in-process interface,
(c) cross-process marshal/unmarshal round-trip.

**Expected results:** (a) and (b) should be within noise.  (c) will be
orders of magnitude slower, confirming that cross-process marshaling is
only viable for low-frequency calls.

### Experiment 3: Build a non-trivial module — COMPLETE

**Purpose:** Discover what a real module needs from netmux.

**Method:** Implement a module that provides a set of softcode functions
requiring attribute reads and player notification.  Record every place where
the module needs to call back into netmux.

**Outcome:** A concrete list of functions/interfaces that must be available
to modules, informing the Phase 3 migration scope.

**Results (brazil branch, 2026-03-08):**

The exp3 module provides 9 softcode functions (mget, mset, mname, mowner,
mloc, mtype, meval, mtell, mhelp).  Each deliberately omits the interface it needs,
so it hits the boundary wall and returns an error.  12 smoke tests verify the
walls and interfaces are correct.

The boundary walls define the interface surface a real module requires:

| Function | Wall hit | Interface needed |
|---|---|---|
| mget() | attribute access | Attribute read |
| mset() | attribute write | Attribute write |
| mname() | object info | Object info |
| mowner() | object info | Object info |
| mloc() | object info | Object info |
| mtype() | object info | Object info |
| meval() | evaluator | Evaluator |
| mtell() | notify | Notify |

This distills to **5 distinct interfaces**: attribute read, attribute write,
object info, evaluator, and notify.  These map closely to the Tier 2 items
already identified in Phase 4.

**Unplanned discoveries:**

1. **libmux must be a Module.**  Once libmux registers its own classes
   (proxy/stub factories for standard marshaling), it cannot share netmux's
   single fpGetClassObject slot.  Fixed by renaming g_MainModule to
   g_AprioriModule (exclusively netmux) and having libmux self-register as a
   proper Module during mux_InitModuleLibrary().

2. **The full module lifecycle works end-to-end.**  dlopen, mux_Register,
   class factory lookup, fpGetClassObject dispatch, mux_Unregister — all
   functional with libmux and exp3 loaded simultaneously.

3. **The smoke infrastructure supports modules.**  The `module` config
   directive, bin symlink for library path, and the existing test harness
   work without modification.

**Implications for Phase 3:**

The COM boundary-wall pattern works cleanly.  The interface surface is
substantial but bounded — a realistic module needs ~5 interfaces, not dozens.
This favors Approach C (hybrid), with the additional insight that the
interface-vs-direct decision can be made independently per API.  Some APIs
may start as interfaces and move to direct calls if the indirection hurts;
others may start direct and get wrapped in interfaces if cross-process use
emerges.  The choice is not all-or-nothing.

### Survey 1: Global state audit — COMPLETE

**Purpose:** Catalog all references to mudconf and mudstate from each .cpp file.

**Method:** grep -co for `mudconf\.` and `mudstate\.` across mux/src/*.cpp
(excluding utf8tables.cpp).

**Results (2026-03-08):**

51 files have at least one reference.  26 files have zero.  Total: ~2,900
references (mudconf ~1,300, mudstate ~1,600).

**Heavily coupled (>100 refs) — stay in netmux:**

| File | mudconf | mudstate | Total | Subsystem |
|---|---|---|---|---|
| conf.cpp | 446 | 145 | 591 | Configuration |
| command.cpp | 143 | 117 | 260 | Command dispatch |
| netcommon.cpp | 64 | 121 | 185 | Network I/O |
| functions.cpp | 36 | 134 | 170 | Softcode functions |
| game.cpp | 47 | 113 | 160 | Main loop / startup |
| db.cpp | 20 | 122 | 142 | Database core |
| predicates.cpp | 19 | 85 | 104 | Object predicates |

**Moderately coupled (10-100 refs) — interface candidates:**

| File | mudconf | mudstate | Total |
|---|---|---|---|
| object.cpp | 49 | 42 | 91 |
| cque.cpp | 13 | 72 | 85 |
| ganl_adapter.cpp | 38 | 37 | 75 |
| walkdb.cpp | 17 | 54 | 71 |
| ast.cpp | 6 | 61 | 67 |
| timer.cpp | 20 | 46 | 66 |
| look.cpp | 35 | 19 | 54 |
| mail.cpp | 7 | 45 | 52 |
| comsys.cpp | 2 | 40 | 42 |
| mguests.cpp | 41 | 0 | 41 |
| attrcache.cpp | 2 | 35 | 37 |
| funceval2.cpp | 9 | 26 | 35 |

**Lightly coupled (1-9 refs) — possible to extract with minor refactoring:**

stringutil (3), mathutil (4), match (3), alloc (18), eval (22),
log (17), help (15), wild (8), modules (8), and others.

**Zero references — ready for libmux.so migration:**

| Category | Files |
|---|---|
| UTF-8 / Unicode | utf8_collate, utf8_grapheme, utf8_normalize |
| Time utilities | timeabsolute, timedelta, timeparser, timeutil, timezone |
| Crypto / hash | sha1, svdhash, svdrand |
| Math | funmath, strtod |
| Network helpers | netaddr, telnet, slave |
| Data structures | htab |
| Module infra | libmux, muxcli, stubslave |
| Storage | sqlite_backend, sqlitedb |
| Other | alarm, ast_scan, _build, local |

**Key insight:** The zero-reference files align closely with the Phase 3
migration candidates already identified (string utils, time utils, math,
buffer pool).  The lightly coupled files (stringutil at 3 refs, mathutil
at 4 refs) could migrate with minor refactoring — likely passing config
values as parameters instead of reading globals directly.

### Survey 2: Include dependency graph — COMPLETE

**Purpose:** Understand which .cpp files can compile independently of the full
server.

**Method:** For each of the 26 zero-mudconf/mudstate files, trace actual type
and function usage against the include tree.  All 26 files include `externs.h`
(the kitchen-sink header that pulls in the entire server), but most only use
a small subset of what `externs.h` provides.

**The blocker:** `externs.h` includes `mudconf.h`, `db.h`, `interface.h`, and
35+ other headers.  Every .cpp file includes it.  Extracting files into
libmux.so requires either (a) creating a lighter header or (b) refactoring
`externs.h` into layers.

**Actual dependency chains for GREEN files:**

| File group | Actually needs | From headers |
|---|---|---|
| sha1, svdhash, svdrand | UTF8, uint types | config.h |
| utf8_collate, utf8_grapheme | UTF8, UTF32, utf8_FirstByte, LBUF_SIZE, UNI_EOF | config.h, stringutil.h, alloc.h |
| utf8_normalize | above + string_desc | config.h, stringutil.h, externs.h (for string_desc) |
| timeutil | UTF8, FIELDEDTIME, mux_isdigit, mux_atol | config.h, timeutil.h, stringutil.h |
| timeabsolute, timedelta | above + mux_sprintf, mux_assert | config.h, timeutil.h, stringutil.h |
| timeparser, timezone | FIELDEDTIME, UTF8 | config.h, timeutil.h |
| strtod | basic types only | config.h |
| netaddr, telnet | UTF8, socket types | config.h, system headers |
| alarm | std threading | config.h, system headers |

**Classification of 26 zero-reference files:**

- **GREEN (18 files)** — Could compile with config.h + 1-2 utility headers:
  time utilities (5), UTF-8 (3), crypto/hash (3), strtod, netaddr, telnet,
  alarm, _build, muxcli, ast_scan, slave

- **YELLOW (6 files)** — Need minor refactoring:
  funmath (FUNCTION macro framework), libmux (modules.h), sqlite_backend
  and sqlitedb (dbref type), stubslave (module infra), utf8_normalize
  (string_desc in externs.h)

- **RED (2 files)** — Cannot extract:
  htab (dbref, NAMETAB, God(), check_access, notify, alloc_lbuf),
  local (server command/function registration)

**Practical path forward:** The string_desc typedef and LBUF_SIZE constant
could be moved from externs.h/alloc.h into a small `libmux_types.h` or
into config.h itself.  The GREEN files don't need db.h, mudconf.h, or
interface.h at all — their `#include "externs.h"` could be replaced with
targeted includes once those types are accessible.


## Decision Points

### Resolved

1. **Phase 2 complete → Phase 3?**  Yes.  Experiment 1 succeeded — all three
   cross-process interfaces converted to standard marshaling with meaningful
   code reduction and no regressions.

2. **Phase 4 at all?**  Yes, selectively.  Experiment 3 showed that the
   interface surface for a real module is ~5 interfaces, not dozens.  The
   boundary-wall pattern gates access cleanly.  Phase 4 is worth pursuing
   for Tier 1 and Tier 2 subsystems.

3. **Phase 3 scope?**  Resolved.  Surveys 1 and 2 completed.  18 of 19
   LIBMUX_SRC files decoupled to core.h.  alloc.cpp remains on externs.h
   (logging subsystem entanglement — not worth the complexity to extract).
   libmux.so exports 513 symbols covering strings, time, math, hash, crypto,
   UTF-8, ANSI color, and buffer management.

### Open

4. **Interface vs. direct, per API.**  Approach C (hybrid) is the direction,
   but the interface-vs-direct decision is made independently for each API.
   Some may start as interfaces and move to direct calls if indirection hurts
   on hot paths; others may start direct and get wrapped in interfaces if
   cross-process use emerges.  The choice is not all-or-nothing and can be
   revisited as we learn more.


## Appendix: Interface Registry

### Standard-marshaled interfaces (proxy/stub/factory in libmux.cpp)

| IID | Interface | CID ProxyStub | Status |
|---|---|---|---|
| IID_ILog | mux_ILog | CID_LogPSFactory | Complete |
| IID_ISlaveControl | mux_ISlaveControl | CID_SlaveControlPSFactory | Complete |
| IID_IQuerySink | mux_IQuerySink | CID_QuerySinkPSFactory | Complete |
| IID_IQueryControl | mux_IQueryControl | CID_QueryControlPSFactory | Complete |

### In-process interfaces (modules.h, implemented in modules.cpp)

| IID | Interface | Methods | Used by |
|---|---|---|---|
| IID_IServerEventsSink | mux_IServerEventsSink | 12 (lifecycle events) | exp3, comsys_mod |
| IID_IServerEventsControl | mux_IServerEventsControl | Advise/Unadvise | exp3, comsys_mod |
| IID_INotify | mux_INotify | Notify, RawNotify, NotifyCheck | comsys_mod, mail_mod |
| IID_IObjectInfo | mux_IObjectInfo | IsValid, GetName, GetOwner, GetLocation, GetType, IsConnected, IsPlayer, IsGoing, GetMoniker, MatchThing | comsys_mod, mail_mod |
| IID_IAttributeAccess | mux_IAttributeAccess | GetAttribute, SetAttribute | comsys_mod, mail_mod |
| IID_IEvaluator | mux_IEvaluator | Eval | comsys_mod, mail_mod |
| IID_IPermissions | mux_IPermissions | IsWizard, IsGod, HasControl, HasCommAll | comsys_mod, mail_mod |
| IID_IMailDelivery | mux_IMailDelivery | MailCheck, NotifyDelivery, IsComposing, SetComposing, ThrottleCheck | mail_mod |
| IID_IHelpSystem | mux_IHelpSystem | LookupTopic, FindHelpFile, GetHelpFileCount, ReloadIndexes | exp3 |
| IID_IFunction | mux_IFunction | Call (per softcode invocation) | exp3 |
| IID_IFunctionsControl | mux_IFunctionsControl | Add, Remove | exp3 |

### Module interfaces (modules.h, implemented in module .so files)

| IID | Interface | CID | Methods | Module |
|---|---|---|---|---|
| IID_IComsysControl | mux_IComsysControl | CID_Comsys | 19 | comsys_mod.so |
| IID_IMailControl | mux_IMailControl | CID_Mail | 11 | mail_mod.so |
