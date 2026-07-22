# Survey: per-resource defenses (anonymous input → single-threaded work)

Defensive reconnaissance (2026-07-21). The core threat: TinyMUX accepts
anonymous connections and does work under user direction on a single thread,
so any resource one user can exhaust harms *every* user. This maps the
EXISTING defenses per resource — because 36 years of accretion means most are
already present — and isolates the genuine gaps. Companion to
`survey-queue.md` and `survey-stress-queue.md`.

Method: three parallel code audits (connection/pre-auth, memory, CPU/alarm),
every claim file:line-anchored. The queue/rate axis is already covered by the
stress work.

## The through-line: two dead half-built defenses

The most telling finding is that **two intended defenses exist as scaffolding
but were never wired**:

- **`DESC.input_size`** (`interface.h`) — a per-connection input-backlog
  counter. It is reset to 0 and decremented, but **never incremented**
  anywhere (`net.cpp:330,2407,3468`). So `save_command` (`net.cpp:398-424`)
  enqueues every input line onto the unbounded `d->input_queue` deque with no
  cap. The field to bound it is present and half-plumbed.
- **`ECALL_CHECK_ALARM = 0x400`** (`engine_api.h:100-102`, documented
  "Alarm check for JIT back-edge budgeting … must exit") — **never emitted,
  never handled** (dead across the whole tree). The hook to make the JIT
  honor the wall-clock alarm is reserved but unbuilt.

Both read as "someone started the defense and it got left incomplete."
Finishing them is lower-risk and more faithful to the codebase than inventing
new mechanism.

## Resource map

### CPU / wall-clock time  (highest stakes — one command starves all)

| Path | Bound | Kind | Airtight? |
|---|---|---|---|
| Interpreter (AST) | `max_cmdsecs`=60s alarm, cooperatively polled at the AST loop, arg-eval, and every iterating builtin (`ast.cpp:2283`, and ~40 loop sites) | **wall-clock** | **Yes** |
| `func_invk_lim`=25000 / `func_nest_lim`=500 / `wild_invk_lim`=100000 / `nStackLimit`=10000 | interpreter counters | count | Yes (interp only) |
| **JIT / MUSHcode-DBT** | `max_dispatch`=10M dispatches (env-tunable, `0`=unlimited); **`alarm_clock.alarmed` never checked** in `dbt_run` (`dbt.cpp:482-501`) | **instructions, not time** | **NO — GAP** |
| JIT self-loop superblocks (ECALL-free) | escape even the dispatch counter (`dbt_x64_sysv.cpp:1293-1406`) | none per-iter | NO (narrow reach) |
| JIT / Lua | `lua_instruction_limit`=100K per-back-edge budget in guest code (`hir_lower_lua.cpp:54-83`) | instructions | bounded, not time |

**The gap:** the wall-clock abort is cooperative, and the JIT path polls
nothing, so a JIT-compiled program runs to `max_dispatch` (or unbounded)
before the server services anyone. `max_dispatch`=10M is a de-facto ~1s bound
for cheap ops, but it does not correlate to time — an ECALL-heavy program (each
ECALL a dispatch, up to 10M, each possibly microseconds-to-milliseconds) can
exceed the 60s wall-clock guarantee the interpreter upholds; `max_dispatch=0`
removes even that. **This regressed with the eval-bracket guard lift** — far
more softcode now takes the JIT path by default. The Lua path shows the fix
shape (guest-code back-edge budget that survives superblocking); the reserved
`ECALL_CHECK_ALARM` shows the intended shape (emit an alarm poll on back-edges).

### Memory  (OOM kills the process as fatally as anything)

| Resource | Exists? | Detail |
|---|---|---|
| Buffer pools (LBUF/MBUF/SBUF) total cap | **NO — GAP, CLIFF** | `pool_alloc*` (`alloc.cpp:255,342`) never caps total; exhaustion → fatal `OutOfMemory`, not graceful refusal |
| Per-owner attribute-BYTES budget | **NO — GAP** | object *count* quota only (`predicates.cpp:177`), **off by default** (`mudconf.quotas=false`); nothing sums attribute bytes/owner |
| Per-attribute size | Yes | silent clamp to `LBUF_SIZE-1` (`db.cpp:2146`) |
| Per-object attribute COUNT | **NO** | only the global 16.7M name space |
| Queue per-owner memory | depth only | `queuemax`=100 entries; each snapshots command + 10 env LBUFs + named-reg map with **no byte ceiling** (`cque.cpp:1171-1217`); wizards bypass the depth cap |
| Per-connection input backlog | **NO — GAP** | `input_queue` deque uncapped; `d->input_size` counter is dead (see above) |
| Per-connection output backlog | partial | DESC drop-oldest at `output_limit`=64KB (`net.cpp:145-172`), **but** GANL `IoBuffer` grows to **1 GiB** before *throwing* `length_error` (`io_buffer.cpp:133-136`) — a stalled reader can consume ~1GiB, failure is an exception not a drop |
| Registers | mostly bounded | q-regs share 1 LBUF; named-reg *count* uncapped |
| Mail | Yes | `mail_max_per_player`=500 |

### Connection / descriptor / pre-auth  (the anonymous front door)

| Resource | Exists? | Detail |
|---|---|---|
| Unauth login idle timeout | Yes | `conn_timeout`=120s (`net.cpp:1228`) |
| Auth idle timeout | Yes | `idle_timeout`=3600s (`net.cpp:1222`) |
| Site ACL forbid/register/suspect/noguest | Yes | allow/deny by subnet — **no counting** (`mudconf.h:343-388`) |
| Global fd cap | Yes | `getdtablesize()-7` (`ganl_adapter.cpp:1919`) |
| Max connected players | Yes, off by default | `max_players`=-1 |
| Guest restrictions | Yes | pool cap 30, `CA_NO_GUEST` commands |
| **Per-source-IP connection cap** | **NO — GAP** | site ACL is allow/deny, never per-IP count → one source can fill the global fd table (slowloris); 120s unauth timeout is the only mitigant |
| **Failed-login throttle per-IP** | **NO — GAP** | `retry_limit`=3 is **per-socket** (`net.cpp:1829`); reconnect gives unlimited fresh batches, no lockout/delay/per-IP memory |
| **Input rate / backlog cap at read boundary** | **NO — GAP** | see `d->input_size` dead counter; only `d->quota` (command-execution token bucket, 100 +1/s) throttles at *dequeue*, not at the read |

## Gaps, ranked for defensive value

1. **JIT wall-clock alarm** (`ECALL_CHECK_ALARM`) — highest stakes: restores
   the 60s per-command availability guarantee on the path most softcode now
   takes; regressed with the guard lift; hook already reserved; Lua path is
   the working template. *Complexity: moderate (guest-code emission).*
2. **Per-connection input backlog cap** (`d->input_size`) — the anonymous
   front door; clean "finish the half-built defense"; graceful (throttle/drop
   a flooding connection); low risk. *Complexity: low.*
3. **Buffer-pool ceiling** — turns the OOM cliff into a graceful per-alloc
   refusal; global not per-owner, but it is the last backstop before process
   death. *Complexity: low-moderate (touch the hot alloc path — measure).*
4. **Per-source-IP connection + failed-login caps** — closes slowloris and
   brute-force-by-reconnect; needs a small per-IP table with expiry.
5. **Per-owner attribute-byte budget** and **queue byte ceiling** — memory
   accounting the depth/count caps don't provide; larger design (per-owner
   byte accounting).
6. **GANL output drop policy** — replace the 1GiB `length_error` with a
   per-connection drop-oldest matching the DESC-level policy.

## Update (2026-07-21): input backlog cap — measured, recalibrated

Implementing gap #2 first turned up two facts that reshaped it:

- **`input_size` is NOT dead — the memory audit was wrong.** It *is*
  incremented, in `telnet.cpp:1592` (the audit grepped only `net.cpp`). The
  real gap was narrower: the counter is maintained but nothing *caps* it. It
  is also incremented in a telnet *batch* (`nInputBytes`) while the dequeue
  decrements by `cmd.size()` — a latent drift — and the websocket input path
  doesn't touch it at all.
- **The app-level input queue does not grow unbounded in practice.** Measured
  under a sustained flood (reader thread holding TCP open), `input_size`
  stayed in the tens of bytes; even with the drain throttled to
  `command_quota 1` it peaked at ~44. The single-threaded read/drain cadence
  plus TCP flow control already bound it — a flood piles bytes into the
  kernel/GANL buffers, not the app command queue. So the unbounded-`input_queue`
  threat is largely mitigated by the existing architecture.

And a design constraint the maintainer flagged: **input is drop-sensitive**.
A dropped output line loses old game text (fine); a dropped input line
silently corrupts a user's paste — a code attribute is one legit ~32KB line,
`@edit`/multi-attribute uploads are legit bursts. So a drop-based cap is the
wrong primary mechanism for input; the correct form is **read-side
backpressure** (stop reading the descriptor when backlogged; TCP holds the
excess with zero loss), deferred for now.

**What shipped (option a):** the correctness half — `save_command` now owns
`input_size` per enqueued line, fixing the telnet-batch/dequeue drift and
covering the websocket path — plus a **high anti-runaway backstop**
(`input_limit`, default 16×LBUF ≈ 512KB, `<=0` disables) with hysteresis
logging. The backstop is set far above any interactive burst (verified: two
back-to-back 30KB attribute pastes store intact, zero drops) and only exists
to catch a pathological runaway (e.g. a future change that removes the
implicit bound). Read-side backpressure remains the proper form (option b),
deferred.

## Update (2026-07-21): JIT wall-clock alarm — FIXED

Gap #1 (the availability regression) closed. The interpreter cooperatively
polls the per-command wall-clock alarm everywhere; the JIT/DBT dispatch loop
polled only `max_dispatch` (an instruction counter). Fix: `dbt_state_t` gained
a host-provided `const std::atomic<bool> *alarm_flag`; both DBT dispatch loops
(`dbt_run`, `dbt_resume`) check it each iteration and return `-3` when set. The
engine points it at `&alarm_clock.alarmed` at the two DBT setup sites; the
standalone `dbt_test` harness leaves it null (verified: 172/172, engine
layering preserved — dbt.cpp only reads a pointer it was handed, rather than
using the dead `ECALL_CHECK_ALARM` guest-code hook).

On abort the existing `rc != 0` path takes over: `run_cached_program` returns
false, jit_eval falls back to the AST, which short-circuits on the same
already-set flag, and the queue/net loop halts the object with "Expensive
activity abbreviated" — identical to how an over-long interpreted command
ends. New `bail_alarm` jitstats counter.

Verified against a live server with `max_dispatch` disabled (so the alarm is
the *only* bound) and `lag_limit 1`: a 25M-ECALL nested-`iter` loop aborts at
**1.01s** (not unbounded), `bail_alarm=1`, and — the point — a **concurrent
connection's ping is answered at 1.01s** instead of waiting out the whole
loop. Smoke 1319/1319 both toggles, oracle 9/9, jit_diff 400/0, stress 8/8,
no false-firing under the normal 60s limit.

**Residual (documented):** ECALL-free native self-loop superblocks
(`dbt_x64_sysv.cpp` warm-entry back-edges) don't return to the dispatch loop
and so still escape this check — but they form only for pure-arithmetic
blocks, which MUSHcode's loop bodies (all ECALL) don't produce. The Lua path
keeps its own instruction budget. Closing that residual needs the guest-code
`ECALL_CHECK_ALARM` emission (the Lua-budget pattern); deferred as not
user-reachable.

## Update (2026-07-21): pool memory budget — mechanism, and a recalibration

Implementing gap #3 (the `pool_alloc` → fatal `OutOfMemory` cliff) turned up
the healthiest possible finding: **the cliff is hard to reach, and that is the
36 years of defense-in-depth working as designed.** The whole MUSH-server
tradition is making dangerous things hard to reach; this is that.

What the measurement showed:
- `pool_alloc`/`pool_alloc_lbuf` only take memory from the system on the slow
  path (freelist empty); freed buffers return to the freelist, not the system.
  So the process's *pool footprint* equals the **peak concurrent** pool
  buffers, and grows only on genuine growth.
- For normal softcode that peak stays tiny: the interpreter allocs-and-frees
  through the freelist within a command, nesting is bounded (`func_nest_lim`,
  `nStackLimit`), and iterating builtins reuse buffers. A short session's pool
  footprint didn't cross even 256KB. Reaching the cliff needs a command
  holding *megabytes of concurrent pool buffers*, which the existing discipline
  makes hard.
- The crash observed while building the JIT alarm (a nested `iter` with
  `max_dispatch=0` OS-OOM-killed at 0.4s) is **not** this path — it is the
  JIT's own arena/guest-memory growth, and it is bounded by `max_dispatch`
  (default 10M) in production; it only OOMs when that guard is disabled. A
  separate, independently-guarded path.

**What shipped:** the pool-footprint budget as **defense-in-depth, off by
default.** `pool_memory_limit` (bytes, K/M/G suffixes; `0`=unlimited) caps the
process's pooled-buffer footprint; on the slow path, crossing it trips the
same cooperative per-command abort the wall-clock alarm uses
(`alarm_clock.alarmed`) so a runaway command unwinds and frees its buffers to
the freelist instead of the process reaching the fatal `OutOfMemory` — a
graceful degrade. It composes with the JIT wall-clock alarm (both trip the same
flag). Off by default because **no non-zero default is safe across deployments**
(a 256MB VPS and a 32GB host want very different ceilings) *and* because the
existing discipline already makes the cliff hard — this is the last backstop,
for the day a new code path, a pathological case, or a disabled guard makes the
pool reachable. Verified: default-off is a true no-op (smoke 1319/1319, oracle
9/9, jit_diff 400/0, stress 8/8); the trip path is wired to the abort flag.

## Recommended first increment

**#2 (input backlog cap) then #1 (JIT alarm).** #2 is a clean, low-risk
completion of an intended per-connection defense that guards the anonymous
front door — exactly the threat model — and its `d->input_size` scaffolding
proves the design was planned. #1 is the highest-stakes item but wants care
(it touches the JIT hot path and needs the self-loop-surviving guest-code
budget), so it earns a dedicated change once the pattern from Lua is adapted.
Both are "finish what was scaffolded," which is the right posture toward a
36-year-old defense layer.
