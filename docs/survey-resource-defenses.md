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
| **Per-source-IP connection cap** | **Yes (2026-07-22)** | `max_preauth_sitecons`=2 caps *unauthenticated* connections per source address; authenticated sessions are never counted — see the update below |
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

- `max_preauth_sitecons` (default **2**, `0`=unlimited, `CA_GOD`/`CA_WIZARD`)
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
bounded by `max_preauth_sitecons`. When full, eviction takes the **least**
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

## Update (2026-07-22): RhostMUSH prior art, and what we took from it

Surveyed RhostMUSH at `ce5226ff` on a tip from Ambrosia ("Rhost's filled to
the brink" with DoS/connection-spam protections). Findings that changed this
work:

**They validate two of our rejections.** Rhost has no per-account lockout
anywhere — a repo-wide grep finds nothing — and no per-IP failed-login
throttle either; their failed-login defense is a per-*descriptor* retry
counter (`retry_limit`, default 3), exactly the gap we just closed. Detection
is pushed to humans via wizard-monitor broadcasts, and the per-account defense
in lieu of lockout is an opt-in per-character site ACL (`A_SITEGOOD` /
`A_SITEBAD`). So "don't build a per-account lockout" is not just our reasoning
— it is what a codebase with far more of this machinery also concluded.

**They found a threat we had missed: refusal logging is itself a DoS.**
Refusing a connection still costs a log write, so a flood we successfully
refuse fills a disk instead. Their `nospam_connect` exists solely for this,
and their help text names it outright ("Real twinkish players may try multiple
connects to overload a log file"). Both of our new defenses logged every
refusal — we had converted a connection flood into a disk flood. Adopted their
parameter by name and semantics (0=log all, 1=collapse a consecutive run from
one address into first-line+summary, 2=log none), with two changes:

- **Default 1, not their 0.** The collapse loses no signal — first line plus
  an exact count — so there is no reason to ship the hole open.
- **Flushed on the periodic idle sweep, not only when a run ends.** Rhost's
  refusals happen at accept, so an accepted connection ends the run. Ours also
  refuse at *login*, and every login attempt arrives on a freshly accepted
  socket — flushing on accept would end the run before every single refusal
  and defeat the damping entirely. Flushing on the sweep additionally bounds
  refusal logging by **time** rather than by the attacker's rate, which is the
  property we actually wanted; a sustained attacker otherwise produces neither
  a different address nor a successful login, so the count would sit
  unreported for the whole attack.

**They confirm the /64 decision by getting it wrong.** Every Rhost
auto-generated ACL entry keys on the full address — `/128` for IPv6
(`bsd.c:1594,1622,1639`; `netcommon.c:5261`) — appended to a list with no cap
and no expiry. A v6 attacker holding a /64 emits a fresh address per
connection, which both evades the penalty and grows the list per attempt. Our
`source_key()` keys v6 on the /64 for exactly this reason.

**Naming.** Per maintainer direction we match Rhost's configuration vocabulary
where the knob is the same thing. `nospam_connect` is adopted verbatim.
`max_preauth_per_site` was renamed to **`max_preauth_sitecons`** to sit in
their `max_sitecons` family — but deliberately *not* named `max_sitecons`,
because theirs caps ALL connections from a site and ours caps only
unauthenticated ones; an identical name would be a false friend that reads as
configured while behaving differently. Our `login_fail_limit` /
`login_fail_period` have no Rhost counterpart to match.

### Rhost ideas worth taking later (not in this change)

1. **Graduated ACL entries.** Every Rhost site entry carries an optional
   connection threshold (`forbid_site <addr> <mask> [<maxconns>]`, and
   `forbid_host *.aol.com|3`), so a restriction only engages *above* N
   simultaneous connections from that site. This is the single best answer to
   the dorm/NAT problem in their design: policy becomes "the dorm is fine
   until it isn't" instead of a binary block.
2. **Latency-DoS self-healing on the accept path.** They auto-learn hosts that
   stall ident (>3s → `H_NOAUTH`) or fail rDNS (→ `H_NODNS`) so the same
   timeout is never paid twice. Worth auditing whether our DNS/ident path can
   be stalled by an attacker connecting from addresses with blackholed PTR,
   and whether we remember.
3. **Keep a large blocklist out of the ACL.** Their `@blacklist` is a separate
   capped structure (default 100k) precisely so a big list doesn't turn the
   linear ACL walk into the bottleneck.
4. **Degrade rather than reject.** Their proxy heuristic answers a suspicious
   connection by forcing registration-required rather than refusing it. The
   *response* is the transferable idea, not the heuristic.

### Calibration on "filled to the brink"

Worth recording honestly: Rhost's aggressive machinery is almost entirely
**off by default** (`lastsite_paranoia 0`, `pcreate_paranoia 0`,
`max_pcreate_lim -1`, `proxy_checker 0`, `nospam_connect 0`). What ships live
is `max_sitecons`, `retry_limit 3`, `regtry_limit 1`, `conn_timeout 120`, and
a descriptor reserve. The transferable posture is not any single counter — it
is *opt-in, whitelist-aware, graduated, and hand-reversible*.

## Update (2026-07-22): graduated site rules (per-entry connection threshold)

Ported from RhostMUSH (`forbid_site <addr> <mask> [<maxconns>]`), noted as the
best idea in that survey. Binary site policy is what makes shared addresses
painful: a dorm, a NAT gateway or a household is *one address* carrying many
unrelated players, so `register_site` on the campus punishes everyone for one
abuser while doing nothing leaves the site undefended. A threshold makes the
middle expressible — **"the dorm is fine until it isn't"**:

```
forbid_site   192.0.2.0/24 8            # CIDR + threshold
forbid_site   192.0.2.0 255.255.255.0 8 # address + mask + threshold
permit_site   127.0.0.1/32 3            # exemption WITH a ceiling
```

Every site directive accepts the optional trailing count: `forbid_site`,
`register_site`, `noguest_site`, `suspect_site`, `nositemon_site`,
`permit_site`, `guest_site`, `trust_site`, `sitemon_site`. Omitted or `0`
means "always", the historical behaviour. `reset_site` rejects one (it removes
rules rather than applying one) rather than ignoring it silently.

**Direction depends on what the rule does.** Rules that SET an `HI_` flag
(forbid, register, noguest, suspect, nositemon) are restrictions and engage at
or above the threshold. Rules that CLEAR one (permit, guest, trust, sitemon)
are exemptions and engage only *below* it — an exemption that survived past its
own ceiling would not be a threshold at all. This is the same inversion Rhost
applies to its permit-side entries, and it is what makes "permitted up to N
connections" sayable.

**Counting is per connecting ADDRESS, not per subnet.** The subnet selects
which rule you land under; the address is what gets counted. That is the right
unit here — a NATted dorm is one address, so its own population is what the
threshold should measure. The count excludes the incoming connection (it is
not yet in `g_descriptors_list`), so threshold N means "engages once N
connections from that address already exist", matching Rhost.

**Cost is zero for existing configurations.** The count is computed lazily,
at most once per lookup, and only when a thresholded rule is actually matched.
An ACL with no thresholds — every configuration that exists today — never
walks the descriptor list.

**Self-healing:** dropping below the threshold re-admits immediately; verified
live.

### A parsing bug the exemption case caught

The first implementation stripped the trailing threshold with
`strstr(buf, token)`, which finds the **first** copy of the token's text.
`permit_site 127.0.0.1/32 3` therefore truncated at the `3` of `/32`, leaving
`127.0.0.1/` — `parse_subnet` failed and the rule was **silently dropped**.
`forbid_site 127.0.0.0/8 4` worked only by luck (no earlier `4`), which is
exactly why testing the *exemption* direction mattered: the restriction
direction happened to pass. Fixed by cutting the last token via a backward
scan, and the logic was factored into `parse_site_threshold()` in
`netaddr.cpp` so this bug class is unit-testable — 11 new cases in
`tests/netaddr`, including three digit collisions.

**Verified live** (`. `=accepted, `X`=refused, six sequential connections):

| Configuration | 1 2 3 4 5 6 |
|---|---|
| `forbid_site 127.0.0.0/8 4` | `. . . . X X` |
| `forbid_site 127.0.0.0/8 3` (digit collision) | `. . . X X X` |
| `forbid_site 127.0.0.0/8` + `permit_site 127.0.0.1/32 3` | `. . . X X X` |
| `forbid_site 127.0.0.0/8` (regression) | `X X X X X X` |
| no site rules (regression) | `. . . . . .` |

Thresholds also show in `@list site_information` (`Forbid (at 4+ conns)`) so an
admin can tell "forbidden" from "forbidden once 8 connections are up".
Regression: smoke 1319/1319, stress 8/8, netaddr 57/57, ganl 14/14.

> Testing note: probe a refusal by reading until EOF, not once. A refused
> connection receives its explanatory text *and then* the close, so a single
> `recv()` sees data and misreads the refusal as an acceptance.

## Update (2026-07-22): Rhost's ident/rDNS latency self-healing — NOT applicable

Item 2 of the Rhost follow-up list, examined and **closed without a change.**
The mitigation exists to solve a problem our architecture does not have.

**Ident: gone, deliberately.** `slave.cpp` has no port-113 path at all — no
`connect()`, no ident protocol, only `getnameinfo`/`gethostbyaddr`. Per the
maintainer, TinyMUX did ident once and dropped it because probing back at a
connecting host was increasingly read as an aggressive act. So Rhost's
`DS_AUTH_IN_PROGRESS` >3s → `H_NOAUTH` auto-learn has nothing to attach to
here.

**rDNS: out of process and never waited on.** The difference that matters is
where the resolve happens. Rhost resolves **in the main process** — `addrout()`
calls `getnameinfo()` inline on the accept path — so a connection from an
address with a blackholed PTR stalls their single-threaded server, which is
precisely why they must auto-learn `H_NODNS` and never pay that timeout twice.
TinyMUX offloads to a separate slave process and never blocks on the answer:

- `queue_dns_lookup()` appends to `pendingWrites` and returns; the accept path
  goes straight on to `d->ss = SocketState::Accepted; welcome_user(d)`. The
  connection is **never gated** on the lookup.
- The write to the slave is non-blocking — `EAGAIN` registers write interest
  and returns rather than stalling the loop.
- `pendingWrites` is capped at 4096 and the read reassembly buffer at 64 KiB
  (both from #801); past the cap a lookup is simply dropped and the connection
  shows its numeric address.
- The answer arrives asynchronously and `apply_reverse_dns_result()` patches
  `d->addr` afterward.
- The slave forks per request with `MAX_CHILDREN 20` and a 300s per-child
  alarm, so a hostile PTR consumes a slave slot, never a driver cycle.

So there is no timeout for an attacker to make us pay, and nothing for an
auto-learned `H_NODNS` to save. Porting it would change the slave interface
for a benefit of zero.

**Residual risk, worth naming but not fixing.** An attacker connecting from
many addresses with deliberately blackholed PTR can occupy all 20 slave slots
for up to 5 minutes each, starving reverse lookups for legitimate connections.
The consequence is bounded and cosmetic — `WHO`, the logs, and `A_LASTSITE`
show numeric addresses instead of hostnames — with no effect on availability,
because nothing in the game waits on the result. Two cheap improvements exist
if that ever matters: a short negative cache so a known-bad address is not
re-queried per connection (there is no dedupe today — two call sites, no
cache), and a per-child alarm shorter than 300s. Neither is worth the churn
now; `max_preauth_sitecons` already bounds how many pre-auth connections one
address can hold, which is the cheapest lever on the same problem.

**Standing lesson:** a mitigation is only worth porting once you have located
the mechanism it mitigates in your own code. Rhost's is an in-process resolver
call; ours is a pipe to a child. Same feature, different architecture, and the
defense does not transfer.
