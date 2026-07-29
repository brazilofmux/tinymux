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
| A2 | GANL adapter / DESC | `mux/src/ganl_adapter.cpp`, `interface.h` | large | Pass 14 2026-07-29 | deep | #1074 DS_NEED_PROTO grace+poll cap; #1135 fail-closed noaddr; #1126 gmcp zero; #790 SOCKET range; InvalidSessionId closes; free_desc2 before list insert; #794 send_data; no new High/Medium |
| A3 | Net / output / idle | `mux/src/net.cpp`, `sitemon.cpp`, `bsd.cpp` | large | Pass 14 hygiene 2026-07-29 | deep | #1133–#1135/#1139; **#1141** getpeername fail-closed on @restart (map residual cleared) |
| A4 | Telnet NVT | `mux/src/telnet.cpp` | med | Pass 14 2026-07-29 | deep | #1126/#1128/#1131/#1132 held: sbOverflow stay-in-SB; enable_us uses us_state; TELNET_OPTION_SIZE 4096; no new High/Medium |
| A5 | WebSocket (netmux) | `mux/src/websocket.cpp`, `websocket_test.*` | med | Pass 14 2026-07-29 | deep | #1081 mask; #1083 output_limit; control FIN/len; WS_MAX_PAYLOAD + frag cap; handshake 4k; zero-payload dispatch (#822); no new High/Medium |
| A6 | Signals / restart helpers | `mux/src/signals.cpp`, restart bits in adapter/net | med | Pass 14 2026-07-29 | deep | #1127 g_bCanRestart armed from adapter path; panic-restart + dump reaps held; no new High/Medium |
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
| C1 | Classic eval / AST | `eval.cpp`, `ast.cpp`, `ast_scan.rl` | large | Pass 14 2026-07-29 | deep | Survey `docs/survey-eval-parser.md` held: parse_to stack[32] bounded; #840 AST_PARSE_MAX_DEPTH; AST_EVAL_MAX_DEPTH=400; func_nest/invk lim; nest-- on all exits; no new High/Medium |
| C2 | Function builtins | `mux/modules/engine/functions.cpp`, `funceval*.cpp`, `funmath.cpp`, `funcweb.cpp` | huge | Pass 14 2026-07-29 | deep | #1106–#1124 closed; Pass 14: no raw sprintf/strcpy residual (mux_sprintf + BAN_LEGACY); family re-rotate still useful for logic |
| C3 | Commands / hooks | `command.cpp`, `predicates.cpp`, `set.cpp` (@include) | large | Pass 12 2026-07-26 | deep | #1279 @include as includer; #1280 NOEVAL footgun |
| C4 | Match / wild | `match.cpp`, `wild.cpp` | med | Pass 14 2026-07-29 | deep | Survey + #835–#837 held; invk limit; scenario wild_capture |
| C5 | Speech / look / move | `speech.cpp`, `look.cpp`, `move.cpp`, `create.cpp`, … | large | Pass 8 | deep | #1181, #1186–#1188 **closed** (map stale fixed Pass 14) |
| C6 | Boolexp / locks | `boolexp.cpp` | med | residual C6/G2/F4 | deep | #839 parse depth; oversize-key NUL (#1294); empty=open by design |

### D — JIT / DBT / Lua

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| D1 | JIT compiler / ECALL | `jit_compiler.cpp` | huge | Pass 1–3 | deep | Guest bounds, setq, watermarks, PIN_ARRAY, fargs |
| D2 | HIR lower / codegen | `hir_*.cpp` | large | Pass 7 + re-scout 2026-07-26 | deep | Highs #1143–#1146 → #1156; #1258–#1260 closed |
| D3 | DBT backends | `dbt*.cpp`, `dbt_rt/` | large | Pass 7 + re-scout 2026-07-26 | deep | Highs closed; #1292 closed |
| D4 | Lua module / bytecode | `lua_mod.cpp`, `lua_bytecode.*`, `hir_lower_lua.*` | med | #1751 campaign 2026-07-29 | deep | Post-entry contract shipped; residual typed fidelity + mixed softcode/Lua corpus |
| D5 | JIT oracles / fuzzer | `testcases/tools/jit_diff/`, q-reg oracle | — | standing | deep tooling | Re-run soak regularly, not just on changes |

### E — Persistence & queue

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| E1 | SQLite / sqlitedb | `sqlitedb.cpp`, `sqlite_backend.cpp` | large | Pass 14 scout 2026-07-29 | deep | Schema gate; blob OOM/oversize guards; ROLLBACK fail logged; re-deep residual held, no new High this hop |
| E2 | Attr cache / write queue | `attrcache.cpp` | med | Pass 14 2026-07-29 | deep | #1284 code-cache coalesce by source_hash; flush fail leaves queue; threshold 50; no new High/Medium |
| E3 | Flatfile R/W | `db.cpp`, `db_rw.cpp` | large | Pass 13 residual + Pass 14 stamp | deep | #806 gates; color migrate fail-closed; no new High this hop |
| E4 | Command queue | `cque.cpp`, `timer.cpp`, `cron.cpp` | large | Pass 14 2026-07-29 | deep | #1080/#1048/#1049/#1050 refund paths held; wait_que reverse-on-false; no new High/Medium |
| E5 | Object / player / flags | `object.cpp`, `player*.cpp`, `flags.cpp`, `powers.cpp` | large | Pass 14 2026-07-29 | deep | #1185 powers clear + IS_CLEAN powers; #1180 pcache_delete on destroy; decode_flags Hidden/CONNECTED; no new High/Medium |

### F — Modules (loadable)

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| F1 | comsys_mod | `mux/modules/comsys/` | small | Pass 14 2026-07-29 | deep | Channel locks via IPermissions (#1084 path); storage failures logged (#1630); dual-path with F3; no new High/Medium |
| F2 | mail_mod | `mux/modules/mail/` | med | Pass 14 2026-07-29 | deep | MAIL_DB_LIMIT; #1192 body persist; storage AddRef; no new High/Medium |
| F3 | Engine-in-tree comsys/mail | `engine/comsys.cpp`, `engine/mail.cpp` | large | Pass 14 scout 2026-07-29 | deep | #1189–#1191 closed; SQLite reload when module owns state |
| F4 | Module ABI / COM | `engine_com.cpp`, `modules.h` | large | Pass 14 2026-07-29 | deep | #1295 nValueMax/out-param guards; AtrAddRaw privileged; IAttributeAccess/IPermissions in-process |

### G — Convert / offline tools

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| G1 | Omega / format convert | `mux/convert/*` | huge | Pass 1, 3, #1087 | deep | sprintf class largely closed; re-scan new paths |
| G2 | muxescape / script | `mux/muxescape/`, `mux/script/` | med | Pass 14 stamp 2026-07-29 | deep | #1296 input size limit; muxscript -p MarkConnected; generated muxescape.cpp |
| G3 | announce | `mux/announce/` | small | Pass 14 2026-07-29 | deep | **#1778** accept fd leak on getnameinfo fail; accept errors no longer `_exit` |

### H — libmux / platform / unicode

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| H1 | Alloc / string / time | `mux/lib/alloc.*`, `stringutil.*`, `timeutil.*`, `alarm.*` | med | Pass 14 2026-07-29 | deep | #1290 freelist drop-one-not-wipe; alarm ms int64 clamp; i64FloorDivision INT64_MIN/-1 guard (#805); no new High/Medium |
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
| K2 | Scenario / stress | `tests/scenario/`, `tests/stress/` | med | Pass 14 2026-07-29 | partial | run.sh: 9 drivers + trap cleanup (not just wild_capture); still opt-in via `make test-scenario`; stress/ separate |
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
Done: A…E… → **F1/F2/F4** → **H1** → G2 stamp.  
**Server residual re-deep largely complete** for this pass (A–H core). **Next: H2/H4 thin polish**, **K3/K4**, or begin **J\*** clients (J5 partial first).

| Next | Slice(s) | Why |
|------|----------|-----|
| **Now** | Merge Pass 14 map PR stack if still open | Hygiene |
| **Then** | **J5** iOS / **J1** console — first client slices | Prefs: after server depth |
| **Anytime** | Mixed softcode/Lua corpus + residual D* fidelity | Product soak |
| **Anytime** | **D5** jit_diff soak; `make test-scenario` | Continuous |
| **Thin** | H2 color (edit .rl only), K4 packaging | Low urgency |

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
| 2026-07-29 | **Pass 14 dice loop:** A–E residual; **F1/F2/F4** modules + **H1** libmux re-deep (locks, MAIL_DB_LIMIT, #1295, freelist #1290 held, no new H/M); G2 stamp; server core residual pass largely complete; next J* or thin H2/K4 |

Update this table when the map structure changes.
