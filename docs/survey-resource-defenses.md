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
| **Per-source-IP connection cap** | **Yes (2026-07-22)** | `max_preauth_per_site`=2 caps *unauthenticated* connections per source address; authenticated sessions are never counted — see the update below |
| **Failed-login throttle per-IP** | **Yes (2026-07-22)** | `login_fail_limit`=10 per `login_fail_period`=60s, token bucket per source (v6 keyed by /64) — see the update below |
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

## Update (2026-07-22): per-source pre-authenticated connection cap

**The naive form of this defense is harmful.** Multi-connection is *normal* in
MUSH: a dorm or NAT puts many unrelated players behind one address, households
share one, and a single player commonly sits on five or more alts at once. A
per-IP cap on *total* connections would break real play on every one of those,
which is why 36 years of TinyMUX shipped only allow/deny site ACLs and never a
per-IP count.

What is *not* normal is many connections from one address sitting at the login
prompt. Legitimate multi-play authenticates promptly; a slowloris does not
authenticate at all. So the cap counts only connections **without**
`DS_CONNECTED`:

- `max_preauth_per_site` (default **2**, `0`=unlimited, `CA_GOD`/`CA_WIZARD`)
  — max simultaneous not-yet-authenticated connections from one source address.
- Checked in the GANL accept path (`ganl_adapter.cpp`) just before the new
  `DESC` joins `g_descriptors_list`, so a refused connection never occupies a
  descriptor slot.
- Refusal writes a short "try again in a moment" line **raw**
  (`SOCKET_WRITE`, the same route `fcache_rawdump` uses) because the DESC is
  torn down immediately and the normal output queue would never flush; it logs
  under `LOG_NET|LOG_SECURITY`.
- Comparison is by address only. `mux_sockaddr::operator==` includes the source
  port — which differs for every connection from one peer — so a new
  `mux_sockaddr::same_address()` does family + address-bytes equality
  (`netaddr.cpp`), covered by 6 new unit tests in `tests/netaddr`.

**Self-healing by construction:** a slot frees the instant a peer authenticates
*or* `conn_timeout` (120s) reaps it, so the worst case for a legitimate player
who collides is one retry, never a lockout. Cross-family (v4 vs v4-mapped-v6)
is deliberately not unified — it can at most double a hostile client's
allowance, and both forms of one peer cannot arrive on the same listener.

**Verified against the threat model and the domain:** with the default of 2, a
third pre-auth socket from 127.0.0.1 is refused *with* the explanatory message;
authenticating a pending connection immediately frees a slot for a new one; and
12 authenticated sessions from that same single address were all accepted — the
dorm / household / five-alts case is untouched. Regression: smoke 1319/1319,
stress 8/8 (its 16 simultaneous logins from one address still pass), netaddr
39/39, ganl 14/14.

## Update (2026-07-22): per-source failed-login throttle

`retry_limit` (3) is **per-socket**: after three bad passwords the connection
closes and the attacker reconnects for three more. Nothing remembers anything
across connections, so brute-force-by-reconnect was unbounded. This adds that
memory as a token bucket per source address, consumed only by failed
*connect* attempts:

- `login_fail_limit` (default **10**, `0`=off) — burst of failed logins allowed
  from one source.
- `login_fail_period` (default **60** seconds) — the interval over which that
  budget fully refills. Sustained rate = limit/period, so the default is
  10/minute against a previously unlimited rate.

Checked in `check_connect` **before** `ConnectPlayer`, so a throttled source
also stops costing us a password hash per guess. Guests are exempt (fixed
password, separately bounded by the guest pool). On refusal the socket is left
open and `retries_left` untouched — the attempt never reached a password check,
so it is not a failed login — and `conn_timeout` still reaps an idle one.

### Two shapes of this defense that are actively harmful, and were rejected

- **Per-account lockout.** Player names are public (WHO, in-game, the
  directory). A per-account lockout lets anyone lock any player — including a
  wizard — out of their own game by spamming failures at their name. That
  trades a brute-force risk for a guaranteed griefing tool.
- **A delay before answering a failed login.** The classic anti-brute-force
  move, and impossible here: this server is single-threaded, so sleeping to
  slow one attacker stops the world for every other player. The throttle must
  be non-blocking, so it refuses rather than stalls.

### Keying: IPv6 by /64, not by address

A single IPv6 customer normally holds a whole /64, so one host can source 2⁶⁴
distinct addresses. Keying a per-source table on the full v6 address would let
one attacker *both* evade the throttle entirely *and* flood the table with
single-use entries. `mux_sockaddr::source_key()` therefore returns the 4-byte
v4 address or the 8-byte v6 **/64 prefix** — the allocation unit is the unit
that must be throttled. Lengths differ per family, so keys never collide
across them. Covered by 7 unit tests in `tests/netaddr`.

### The table must not become the resource it protects

Fixed array of 512 slots, scanned linearly — no allocation, no growth, nothing
to flood. It is consulted only on login attempts, which are themselves already
bounded by `max_preauth_per_site`. When full, eviction takes the **least**
suspicious entry (the fullest bucket, oldest as tie-break), so table pressure
never costs us the record of an active attacker. Fully-refilled buckets carry
no information and are the first to go.

### The dorm cost is real, bounded, and deliberate

Many unrelated players share one address, so an exhausted bucket does briefly
refuse legitimate players from that address — including ones typing the correct
password. It is bounded (the bucket refills continuously; the wait is seconds,
not a lockout), the default budget is generous relative to how often real
players mistype, and an admin can widen or disable it. There is deliberately
**no** "this source already has an authenticated session" exemption: it would
read as dorm-friendly while handing a full bypass to exactly the attacker who
worries us most — an existing player going after someone else's account.

**Verified** on a live netmux at `login_fail_limit 3` / `login_fail_period 600`
(refill negligible during the run): guesses 1–3 rejected normally, 4–6 refused
with the wait message, **each from a fresh connection** — i.e. reconnecting no
longer buys a fresh batch, which is the whole point. A correct password is also
refused while over budget (the documented cost). At limit 5 / period 20s the
budget demonstrably refills and the correct password succeeds again. Both the
`CON/THR` log line and the earlier `NET/SITE` pre-auth line were confirmed to
emit (note: `netmux` needs `-e -` to send the game log to stderr; without it
the log goes to a file and looks absent). Regression: smoke 1319/1319, stress
8/8, netaddr 46/46, ganl 14/14.
