# Audit Coverage Map — TinyMUX 2.14 hardening

**Living document.** Feature-complete 2.14 work is review-and-fix only.
This map tracks **where** we have looked, **when**, and **what to revisit**.
We expect to re-audit slices already marked “done” — that is intentional.

**Workflow (standing):**
explore/read → file High/Medium issues → fix PR → Claude cross-review
(+ Windows when the path is Win32-only) → merge.

**Prefs:** server-first; Linux/FreeBSD primary; Windows must not lag;
clients/Hydra after server depth is comfortable.

---

## How to use this map

1. Pick the next **slice ID** from the queue (or re-queue a prior slice).
2. Update that row’s **Last pass** / **Status** when you finish filing issues.
3. After a fix PR merges, note issue ranges in **Notes** (optional).
4. When revisiting, change Status to `revisit` and bump Last pass.

**Status values:**

| Status | Meaning |
|--------|---------|
| `deep` | Focused High/Medium pass completed this cycle |
| `partial` | Touched; known residual or thin coverage |
| `thin` | Spot-check only or only via related work |
| `deferred` | Out of current phase by design |
| `revisit` | Scheduled re-audit of a previously deep slice |
| `backlog` | Known work, not defect-driven |

---

## Slice catalog

Rough line counts are order-of-magnitude (`.c`/`.cpp`/`.h`); they change.

### A — Driver / network (server process: `netmux`)

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| A1 | Driver bootstrap / COM | `mux/src/driver.cpp`, `driver_bridge.cpp`, `modules.cpp`, `muxcli.cpp` | med | Pass 14 scout 2026-07-29 | thin | engine.so via mux_AddModule; IPlatform CreateInstance; no new High this hop |
| A2 | GANL adapter / DESC | `mux/src/ganl_adapter.cpp`, `interface.h` | large | Pass 1–2, #1074 | deep | Restart DESC, `DS_NEED_PROTO`, IOCP cap, accept path |
| A3 | Net / output / idle | `mux/src/net.cpp`, `sitemon.cpp`, `bsd.cpp` | large | Pass 6 | deep | #1133–#1135 closed (#1139); residual: `@restart` getpeername fail-open |
| A4 | Telnet NVT | `mux/src/telnet.cpp` | med | Pass 6 | deep | #1126/#1128/#1131/#1132 closed (#1139; SB overflow via sbOverflow) |
| A5 | WebSocket (netmux) | `mux/src/websocket.cpp`, `websocket_test.*` | med | Pass 3 | deep | Mask, handshake close, backpressure (#1081–#1083) |
| A6 | Signals / restart helpers | `mux/src/signals.cpp`, restart bits in adapter/net | med | Pass 6 | deep | #1127/#1129 → #1138; #1130/#1136 → #1139 (cores + dump reaps) |
| A7 | Netaddr / site keys | `mux/src/netaddr.cpp`, `tests/netaddr/` | small | Pass 14 2026-07-29 | deep | #799/#800 held; **#1774** CIDR prefix int truncation (PR #1776) |
| A8 | Slave / stubslave | `mux/src/slave.cpp`, `stubslave.cpp`, DNS channel in `ganl_adapter` | small | Pass 14 2026-07-29 | deep | #1220/#801/#1274/#1275 held; re-read no new High/Medium |

### B — GANL library

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| B1 | Epoll / select / kqueue / wselect / IOCP | `mux/ganl/src/*_network_engine.cpp` | med | Pass 14 2026-07-29 | deep | #942–#947/#946/#943/#1290 V6ONLY held; maxEvents write-disarm guards held |
| B2 | IOCP / wselect | `iocp_*`, `wselect_*` | med | Pass 14 2026-07-29 | deep | #1070 accept+FD_SETSIZE close; re-read with B1 |
| B3 | OpenSSL transport | `openssl_transport.cpp` | med | Pass 14 2026-07-29 | deep | #948/#949/#1282 read-BIO cap + cipher pin held; TLS 1.2+; renegotiation disabled; no new High/Medium |
| B4 | Schannel transport | `schannel_transport.cpp` | med | Pass 14 2026-07-29 | deep | #1067/#1068/#950–#952/#1282 handshake+incomplete caps held; no new High/Medium |
| B5 | Connection / buffers | `connection.cpp`, `io_buffer`, types | med | Pass 14 2026-07-29 | deep | #794 high-water + try/catch in send_data; #953 kMaxBufferSize |
| B6 | GANL tests | `mux/ganl/tests/` | small | Pass 14 2026-07-29 | partial | Locks #942–#947/#953/EMFILE + Win accept path; expand kqueue-only / soak |

### C — Engine softcode & evaluation

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| C1 | Classic eval / AST | `eval.cpp`, `ast.cpp`, `ast_scan.rl` | large | Pass 14 scout 2026-07-29 | partial | Spot-check LBUF/mux_sprintf; full matrix still open |
| C2 | Function builtins | `mux/modules/engine/functions.cpp`, `funceval*.cpp`, `funmath.cpp`, `funcweb.cpp` | huge | Pass 5 | deep | #1106–#1124 closed; re-rotate by family later |
| C3 | Commands / hooks | `command.cpp`, `predicates.cpp`, `set.cpp` (@include) | large | Pass 12 2026-07-26 | deep | #1279 @include as includer; #1280 NOEVAL footgun |
| C4 | Match / wild | `match.cpp`, `wild.cpp` | med | Pass 14 2026-07-29 | deep | Survey + #835–#837 held; invk limit; scenario wild_capture |
| C5 | Speech / look / move | `speech.cpp`, `look.cpp`, `move.cpp`, `create.cpp`, … | large | Pass 8 | deep | #1181, #1186–#1188 **closed** (map stale fixed Pass 14) |
| C6 | Boolexp / locks | `boolexp.cpp` | med | residual C6/G2/F4 | deep | #839 parse depth; oversize-key NUL (#1294); empty=open by design |

### D — JIT / DBT / Lua

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| D1 | JIT compiler / ECALL | `jit_compiler.cpp` | huge | Pass 1–3 | deep | Guest bounds, setq, watermarks, PIN_ARRAY, fargs |
| D2 | HIR lower / codegen | `hir_*.cpp` | large | Pass 7 + re-scout 2026-07-26 | deep | Highs #1143–#1146 → #1156; #1258/#1259 closed; check #1260 state before re-queue |
| D3 | DBT backends | `dbt*.cpp`, `dbt_rt/` | large | Pass 7 + re-scout 2026-07-26 | deep | Highs closed; residual #1292 family — verify before re-queue |
| D4 | Lua module / bytecode | `lua_mod.cpp`, `lua_bytecode.*`, `hir_lower_lua.*` | med | #1751 campaign 2026-07-29 | deep | Post-entry contract shipped; residual typed fidelity + mixed softcode/Lua corpus |
| D5 | JIT oracles / fuzzer | `testcases/tools/jit_diff/`, q-reg oracle | — | standing | deep tooling | Re-run soak regularly, not just on changes |

### E — Persistence & queue

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| E1 | SQLite / sqlitedb | `sqlitedb.cpp`, `sqlite_backend.cpp` | large | Pass 1, #1073 | deep | Schema gate, prepare, load paths |
| E2 | Attr cache / write queue | `attrcache.cpp` | med | Pass 1 + residual 2026-07-26 | deep | Flush-on-fail leaves queue; #1284 code-cache coalesce |
| E3 | Flatfile R/W | `db.cpp`, `db_rw.cpp` | large | Pass 1 + Pass 13 residual 2026-07-26 | deep | #806 dbref/lock gates held; color migrate fail-closed |
| E4 | Command queue | `cque.cpp`, `timer.cpp`, `cron.cpp` | large | Pass 1, #1080 | deep | OOM refund, depth, runaway money |
| E5 | Object / player / flags | `object.cpp`, `player*.cpp`, `flags.cpp`, `powers.cpp` | large | Pass 8 | deep | #1179–#1185 closed (Pass 14 hygiene) |

### F — Modules (loadable)

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| F1 | comsys_mod | `mux/modules/comsys/` | small | Pass 3, 9 | deep | #1084 locks; Pass 9 dual-path with F3 |
| F2 | mail_mod | `mux/modules/mail/` | med | Pass 2, 9 | deep | MAIL_DB_LIMIT; Pass 9 dual-store |
| F3 | Engine-in-tree comsys/mail | `engine/comsys.cpp`, `engine/mail.cpp` | large | Pass 9 | deep | Dual path vs modules; #1189–#1191 closed (Pass 14 hygiene) |
| F4 | Module ABI / COM | `engine_com.cpp`, `modules.h` | large | residual C6/G2/F4 | deep | IAttributeAccess/IPermissions gated; #1295 |

### G — Convert / offline tools

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| G1 | Omega / format convert | `mux/convert/*` | huge | Pass 1, 3, #1087 | deep | sprintf class largely closed; re-scan new paths |
| G2 | muxescape / script | `mux/muxescape/`, `mux/script/` | med | residual C6/G2/F4 | deep | #1296 muxescape 16MiB cap; muxscript -p MarkConnected |
| G3 | announce | `mux/announce/` | small | Pass 14 2026-07-29 | deep | **#1778** accept fd leak on getnameinfo fail; accept errors no longer `_exit` |

### H — libmux / platform / unicode

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| H1 | Alloc / string / time | `mux/lib/alloc.*`, `stringutil.*`, `timeutil.*`, `alarm.*` | med | residual 2026-07-26 | deep | #1290 freelist drop-one; alarm int64 ms clamp |
| H2 | Color / Ragel | `color_ops.rl`, color path | med | generated-file discipline | thin | Edit `.rl` only |
| H3 | UTF-8 / collation | `utf/`, `utf8tables.*`, `unicode_*` | large | — | deferred | Generated tables; deep Unicode later |
| H4 | Platform abstraction | `platform.cpp`, design-platform-interface | small | Pass 14 scout 2026-07-29 | partial | BootHelper/Reap/Maximize used; RegisterSignalHandler incomplete (stored, never invoked; Win SetConsoleCtrl TODO); not product-breaking today — signals still in signals.cpp |

### I — Hydra proxy

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| I1 | Session / process manager | `session_manager.*`, `process_manager.*` | med | Pass 4 | deep | #1091–#1102 closed |
| I2 | gRPC / gRPC-web | `grpc_server.*`, `grpc_web.*` | med | Pass 4 + Pass 10 | deep | #1265–#1269 closed |
| I3 | Telnet bridge / stream | `telnet_bridge.*`, `telnet_stream.*` | med | Pass 4 | deep | SB reassembly cap + disconnect |
| I4 | Proxy WebSocket | `mux/proxy/websocket.*` | small | Pass 4 | deep | #1093–#1095 closed |
| I5 | Accounts / crypto / scrollback | `account_manager.*`, `crypto.*`, `scrollback.*` | med | Pass 4 | deep | #1091–#1092, #1098, #1100 closed |
| I6 | Proxy regression | `proxy_regression.cpp` | small | Pass 14 2026-07-29 | deep | #1270 residual closed in-tree: #1265/#1266 subscriber+work-queue caps; convertInput helper for #1267-class |

### J — Clients (Hydra family)

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| J1 | Console client | `client/console/` | med | — | deferred | |
| J2 | Win32 GUI | `client/win32gui/` | med | — | deferred | |
| J3 | Web client | `client/web/` | med | — | deferred | |
| J4 | Android Titan | `client/android/` | large | build-only notes | deferred | Resume path untested (status-2.14) |
| J5 | iOS Titan | `client/ios/` | large | unit tests in make test | partial | Parser units only |
| J6 | TinyFugue port | `client/tf/` | med | — | deferred | |
| J7 | Shared client bits | `client/shared/` | small | — | deferred | |

### K — Tests, packaging, docs tooling

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| K1 | Smoke suite | `testcases/` | large | every fix PR | deep | Grow when behavior changes; Lua seam corpus in flight |
| K2 | Scenario / stress | `tests/scenario/`, `tests/stress/` | med | opt-in | partial | wild-capture, site-threshold, jit-perms; make more routine |
| K3 | Unit islands | `tests/alarm`, `netaddr`, `db`, `libmux` | small | on change | partial | |
| K4 | Packaging | `debian/`, `docker/`, `dounix.sh`, `win32/` | med | — | thin | |
| K5 | Parser research | `parser/` | small | — | deferred | Not production runtime |
| K6 | Worldbuilder / tools | `tools/worldbuilder/` | — | — | deferred | |
| K7 | Generated-file discipline | see `docs/generated-files.md` | — | standing | process | Never hand-edit outputs |

### L — Product backlog (not audit defects)

| ID | Item | Status |
|----|------|--------|
| L1 | #1027 in-game archive/restore via SQLite | backlog |

---

## Pass log (summary)

| Pass | When | Focus | Outcome |
|------|------|-------|---------|
| Pass 1–13 | 2026-07 | See historical changelog | Many Highs/Mediums closed |
| **Pass 14** | **2026-07-29** | Dice loop from A7 | #1774 netaddr CIDR; #1778 announce fd leak; B1–B5/B3/B4/A8/C4/I6 residual re-deep; map hygiene |

Also useful historical surveys:  
`docs/survey-*-pass-2026-06.md`, `docs/survey-ganl-networking.md`, `docs/survey-queue.md`, etc.

---

## Recommended rotation (next ~N passes)

**Dice loop (2026-07-29):** start **A7**.  
Done this pass: A7 → A8 → B5 → C4 → B1/B2 → G3 → B3/B4 → I6 → A1/H4 scout.  
**Next: C1 deep** (or K2, H2, K4 thin) — then wrap toward A2…A6 if still hot.

| Next | Slice(s) | Why |
|------|----------|-----|
| **Now** | Dual-review open Pass 14 PRs (#1776, #1779, map PRs) | Land Mediums |
| **Then** | **C1** full softcode matrix / **K2** scenario defaults | Partial slices with product surface |
| **Anytime** | Mixed softcode/Lua corpus + residual D* fidelity | Product soak |
| **Later** | **J\*** clients by platform | After server/proxy confidence |
| **Anytime** | **D5** jit_diff soak | Continuous |

When a pass is “empty” (no High/Medium), still **record the pass** and Status=`deep` with date — that prevents false “never looked” later.

---

## Review lenses (apply every slice)

Use the same questions so re-audits stay consistent:

1. **Bounds** — lengths, indexes, guest/host, stack buffers, `sprintf` vs `snprintf`
2. **Lifetime cycle** — alloc/free, COM refs, DESC/ws, queue entries, SQLite pins
3. **Permissions** — executor vs thing, locks, unfindable, module vs engine
4. **Failure paths** — OOM, partial init, reject/close, refund/quota
5. **Config sync** — mudconf vs `g_dc`, boot-only knobs
6. **Platform** — Win32 vs POSIX, FD limits, TLS stack differences
7. **Generated files** — source + regen present (`docs/generated-files.md`)

---

## Standing verification (every fix PR)

From `docs/status-2.14.md` and practice:

- `make install` (or Windows `netmux.sln` when needed)
- `make test` (smoke, ganl, netaddr, JIT oracle, iOS units if Darwin)
- Opt-in: `make test-scenario`, `testcases/tools/jit_diff/soak.sh`
- Windows: Schannel/IOCP/wselect when the slice is Win32-heavy

---

## Changelog (map document)

| Date | Change |
|------|--------|
| 2026-07-24 | Initial map after Passes 1–3 |
| 2026-07-24–26 | Passes 4–13; residual scouts (see git history) |
| 2026-07-26 | **Pass 13 residual** (anti-cool): E3; #1411; #1408 family |
| 2026-07-29 | **Pass 14 dice loop:** A7 #1774; G3 #1778; B1–B5/B3/B4/A8/C4 residual re-deep; I6 deep (#1270 residual covered); H4 partial (signal wiring incomplete); stale “open” notes cleaned on C5/I2/I6; next C1/K2 |

Update this table when the map structure changes.
