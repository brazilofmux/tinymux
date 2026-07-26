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
| A1 | Driver bootstrap / COM | `mux/src/driver.cpp`, `driver_bridge.cpp`, `modules.cpp`, `muxcli.cpp` | med | — | thin | Config basket pull; module load order |
| A2 | GANL adapter / DESC | `mux/src/ganl_adapter.cpp`, `interface.h` | large | Pass 1–2, #1074 | deep | Restart DESC, `DS_NEED_PROTO`, IOCP cap, accept path |
| A3 | Net / output / idle | `mux/src/net.cpp`, `sitemon.cpp`, `bsd.cpp` | large | Pass 6 | deep | #1133–#1135 closed (#1139); residual: `@restart` getpeername fail-open |
| A4 | Telnet NVT | `mux/src/telnet.cpp` | med | Pass 6 | deep | #1126/#1128/#1131/#1132 closed (#1139; SB overflow via sbOverflow) |
| A5 | WebSocket (netmux) | `mux/src/websocket.cpp`, `websocket_test.*` | med | Pass 3 | deep | Mask, handshake close, backpressure (#1081–#1083) |
| A6 | Signals / restart helpers | `mux/src/signals.cpp`, restart bits in adapter/net | med | Pass 6 | deep | #1127/#1129 → #1138; #1130/#1136 → #1139 (cores + dump reaps) |
| A7 | Netaddr / site keys | `mux/src/netaddr.cpp`, `tests/netaddr/` | small | unit green | partial | Covered by units; logic re-read optional |
| A8 | Slave / stubslave | `mux/src/slave.cpp`, `stubslave.cpp`, DNS channel in `ganl_adapter` | small | #1220 + residual 2026-07-26 | deep | Framing #1220; residual stubslave write remainder + Win32 DNS queue caps |

### B — GANL library

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| B1 | Epoll / select engines | `mux/ganl/src/*_network_engine.cpp` (unix) | med | Pass 1–2 context | partial | FD_SETSIZE, hup, writable cap — harness helps |
| B2 | IOCP / wselect | `iocp_*`, `wselect_*` | med | Pass 2 Windows | deep | FD_SETSIZE accept leak, IOCP desc cap |
| B3 | OpenSSL transport | `openssl_transport.cpp` | med | earlier TLS work | partial | Renegotiation disabled; re-read chunking |
| B4 | Schannel transport | `schannel_transport.cpp` | med | Pass 2 | deep | EXTRA loop, handle init (#1067–#1068); nits remain |
| B5 | Connection / buffers | `connection.cpp`, `io_buffer`, types | med | Pass 2 context | partial | Backlog, write posts |
| B6 | GANL tests | `mux/ganl/tests/` | small | ongoing | partial | Expand wselect/IOCP cases over time |

### C — Engine softcode & evaluation

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| C1 | Classic eval / AST | `eval.cpp`, `ast.cpp`, `ast_scan.rl` | large | Pass 2 softcode | partial | elock/lastcreate fixed; full matrix not exhausted |
| C2 | Function builtins | `mux/modules/engine/functions.cpp`, `funceval*.cpp`, `funmath.cpp`, `funcweb.cpp` | huge | Pass 5 | deep | #1106–#1124 closed (#1121 Highs, #1123 Mediums, #1125 JIT perms); re-rotate by family later |
| C3 | Commands / hooks | `command.cpp`, `predicates.cpp` | large | thin | thin | @-command side effects |
| C4 | Match / wild | `match.cpp`, `wild.cpp` | med | scenario tests | partial | Live `$` capture has scenario; re-read matching |
| C5 | Speech / look / move | `speech.cpp`, `look.cpp`, `move.cpp`, `create.cpp`, … | large | Pass 8 | deep | #1181, #1186–#1188 open (clone/preserve, moniker HTML, pagecost order, @open HOME) |
| C6 | Boolexp / locks | `boolexp.cpp` | med | elock-related | partial | Lock evaluation side channels |

### D — JIT / DBT / Lua

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| D1 | JIT compiler / ECALL | `jit_compiler.cpp` | huge | Pass 1–3 | deep | Guest bounds, setq, watermarks, PIN_ARRAY, fargs |
| D2 | HIR lower / codegen | `hir_*.cpp` | large | Pass 7 | deep | Highs #1143–#1146 → #1156; residual Mediums #1149–#1150 |
| D3 | DBT backends | `dbt*.cpp`, `dbt_rt/` | large | Pass 7 | deep | Highs #1147–#1148 → #1156; residual Mediums #1151–#1153 |
| D4 | Lua module / bytecode | `lua_mod.cpp`, `lua_bytecode.*`, `hir_lower_lua.*` | med | Pass 3 ECALL | partial | Softlib bridges hardened; module surface remains |
| D5 | JIT oracles / fuzzer | `testcases/tools/jit_diff/`, q-reg oracle | — | standing | deep tooling | Re-run soak regularly, not just on changes |

### E — Persistence & queue

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| E1 | SQLite / sqlitedb | `sqlitedb.cpp`, `sqlite_backend.cpp` | large | Pass 1, #1073 | deep | Schema gate, prepare, load paths |
| E2 | Attr cache / write queue | `attrcache.cpp` | med | Pass 1, residual notes | partial | Flush errors; code-cache coalesce residual |
| E3 | Flatfile R/W | `db.cpp`, `db_rw.cpp` | large | Pass 1 import flush | partial | Import/export edges |
| E4 | Command queue | `cque.cpp`, `timer.cpp`, `cron.cpp` | large | Pass 1, #1080 | deep | OOM refund, depth, runaway money |
| E5 | Object / player / flags | `object.cpp`, `player*.cpp`, `flags.cpp`, `powers.cpp` | large | Pass 8 | deep | #1179–#1180 High; #1182–#1185 Medium open |

### F — Modules (loadable)

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| F1 | comsys_mod | `mux/modules/comsys/` | small | Pass 3, 9 | deep | #1084 locks; Pass 9 dual-path Highs/Mediums with F3 |
| F2 | mail_mod | `mux/modules/mail/` | med | Pass 2, 9 | deep | MAIL_DB_LIMIT; Pass 9 body WT #1192 + dual-store #1191 |
| F3 | Engine-in-tree comsys/mail | `engine/comsys.cpp`, `engine/mail.cpp` | large | Pass 9 | deep | Dual path vs modules; Highs #1189–#1193, Mediums #1194–#1199 open |
| F4 | Module ABI / COM | `engine_com.cpp`, `modules.h` | large | Pass 3 CouldDoit | partial | Vtable adds need full rebuild |

### G — Convert / offline tools

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| G1 | Omega / format convert | `mux/convert/*` | huge | Pass 1, 3, #1087 | deep | sprintf class largely closed; re-scan new paths |
| G2 | muxescape / script | `mux/muxescape/`, `mux/script/` | med | — | thin | |
| G3 | announce | `mux/announce/` | small | — | thin | |

### H — libmux / platform / unicode

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| H1 | Alloc / string / time | `mux/lib/alloc.*`, `stringutil.*`, `timeutil.*`, `alarm.*` | med | alarm fixes earlier | partial | Pool accounting |
| H2 | Color / Ragel | `color_ops.rl`, color path | med | generated-file discipline | thin | Edit `.rl` only |
| H3 | UTF-8 / collation | `utf/`, `utf8tables.*`, `unicode_*` | large | — | deferred | Generated tables; deep Unicode later |
| H4 | Platform abstraction | `platform.cpp`, design-platform-interface | small | — | thin | |

### I — Hydra proxy

| ID | Slice | Paths | ~Size | Last pass | Status | Notes |
|----|--------|-------|------:|-----------|--------|-------|
| I1 | Session / process manager | `session_manager.*`, `process_manager.*` | med | Pass 4 | deep | #1091–#1102 closed (#1103 Highs, #1105 Mediums) |
| I2 | gRPC / gRPC-web | `grpc_server.*`, `grpc_web.*` | med | Pass 4 | partial | Auth lockout + ListGames/GetGameStatus gate (#1105); deeper RPC surface remains |
| I3 | Telnet bridge / stream | `telnet_bridge.*`, `telnet_stream.*` | med | Pass 4 | deep | SB reassembly cap + disconnect (#1101 via #1105) |
| I4 | Proxy WebSocket | `mux/proxy/websocket.*` | small | Pass 4 | deep | #1093–#1095 closed (#1103 mask/CLOSE; #1105 RSV/control limits) |
| I5 | Accounts / crypto / scrollback | `account_manager.*`, `crypto.*`, `scrollback.*` | med | Pass 4 | deep | #1091–#1092, #1098, #1100 closed (#1103/#1105) |
| I6 | Proxy regression | `proxy_regression.cpp` | small | Pass 4 | partial | 16/64-bit mask, CLOSE, RSV, large PING, SB cap; more cases welcome |

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
| K1 | Smoke suite | `testcases/` | large | every fix PR | deep | 1325+ TCs; grow when behavior changes |
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

## Pass history (post feature-complete)

| Pass | When (approx) | Focus | Issue / PR trail |
|------|---------------|--------|------------------|
| Pass 1 | 2026-07 | Restart DESC, SQLite durability, queue, JIT host edge, conf ints, convert encode | #1039–#1061 → PR #1062 |
| Pass 2 | 2026-07 | Softcode, mail_mod, Windows IOCP/wselect, Schannel, JIT residual | #1063–#1071 → PR #1072; #1073/#1075 schema; #1074/#1076 banner |
| Pass 3 | 2026-07 | Convert residual, JIT bounds, queue money, WS, comsys_mod, conf live sync | #1077–#1080 → #1086; #1087/#1088; #1081–#1085 → #1089 |
| Pass 4 | 2026-07 | Hydra proxy I1–I5 (session, scrollback, WS, auth, process) | #1091–#1102 filed; Highs → #1103; Mediums → #1105 (complete) |
| Pass 5 | 2026-07 | Softcode C2 function builtins + JIT ECALL perms | Highs #1106–#1110 → #1121; Mediums #1111–#1119/#1122 → #1123; #1124 JIT `check_access` → #1125 |
| Pass 6 | 2026-07 | A3 net/output + A4 telnet NVT + A6 signals/restart | #1126–#1136 filed; #1127/#1129 → #1138; rest → #1139 (complete) |
| Pass 7 | 2026-07 | D2 HIR + D3 DBT | Highs #1143–#1148 → #1156; Mediums #1149–#1153 residual; also JIT float/ifelse Highs #1157/#1159 closed separately |
| Pass 8 | 2026-07 | E5 object/player/flags/powers + C5 speech/look/move/create | Highs #1179–#1181; Mediums #1182–#1188 open (not yet fixed) |
| Pass 9 | 2026-07 | F3 engine comsys/mail vs F1/F2 modules | Highs #1189–#1193; Mediums #1194–#1199 open (not yet fixed) |

Also useful historical surveys (pre-hardening-month):  
`docs/survey-*-pass-2026-06.md`, `docs/survey-ganl-networking.md`, `docs/survey-queue.md`, etc.

---

## Recommended rotation (next ~N passes)

Revisit is expected. Suggested order balances **new surface** with **re-sweeps**:

| Next | Slice(s) | Why |
|------|----------|-----|
| **Now** | **Fix Pass 8 Highs** #1179–#1181 and/or **Pass 9 Highs** #1189–#1193 | Standing Highs-first; @cdestroy inverted is default-path |
| **Pass 10** | **I2 deep + I6** remaining gRPC surface + regression expansion | Pass 4 closed; residual depth |
| **Pass 11** | **B3 + B4 nits + B6** OpenSSL re-read + Schannel residual + harness | Windows/Linux TLS depth |
| **Pass 12** | **C3** commands/hooks deep | Still thin; @-command side effects |
| **Pass 13+** | **J\*** clients by platform | After server/proxy confidence |
| **Residual** | Pass 7 Mediums #1149–#1153; Pass 6 twin #1141; Pass 8/9 Mediums | Fix when rotating back |
| **Anytime** | **D5** jit_diff soak + corpus gaps (#1160) | Continuous; other agents on float/fuzz |

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
- `make test` (smoke 1319, ganl, netaddr, JIT oracle, iOS units if Darwin)
- Opt-in: `make test-scenario`, `testcases/tools/jit_diff/soak.sh`
- Windows: Schannel/IOCP/wselect when the slice is Win32-heavy

---

## Changelog (map document)

| Date | Change |
|------|--------|
| 2026-07-24 | Initial map after Passes 1–3; open defects cleared except #1027 |
| 2026-07-24 | Pass 4 Hydra filed #1091–#1102; I* slice status updated |
| 2026-07-25 | Pass 4 Highs #1091–#1094 merged (#1103); I4/I5/I6 + rotation updated |
| 2026-07-25 | Pass 4 Mediums #1095–#1102 merged (#1105); Pass 4 complete; rotation → Pass 5 C2 |
| 2026-07-25 | Pass 5 complete (#1121/#1123/#1125); C2 deep; rotation → Pass 6 A3+A4+A6 |
| 2026-07-25 | Pass 6 A3+A4+A6 filed #1126–#1136; rotation → Fix Pass 6 |
| 2026-07-25 | Pass 6 complete (#1138 crash/core, #1139 Highs+Mediums); rotation → Pass 7 D2+D3 |
| 2026-07-25 | Pass 7 Highs closed (#1156); Mediums #1149–#1153 residual; D2/D3 deep |
| 2026-07-25 | Pass 8 E5+C5 filed #1179–#1188; E5/C5 deep; rotation → Pass 9 F3 (or Fix Pass 8 Highs) |
| 2026-07-25 | Pass 9 F3/F1/F2 filed #1189–#1199; dual-path deep; rotation → Fix Highs (Pass 8/9) |

| 2026-07-26 | Pass A8 residual: stubslave parent write remainder + Win32 DNS queue caps; A8 → deep |

Update this table when the map structure changes.
