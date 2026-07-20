# Survey: GANL networking subsystem

GANL ("Generic Async Network Layer", `mux/ganl/`) is TinyMUX's mandatory
event-driven network implementation, replacing the legacy select loop. This
document is a standing findings inventory from a code survey begun 2026-06-10.
It is organized by sub-part so the audit can be picked up incrementally.

**Verification legend:** ✅ verified (read the code path and/or reproduced) ·
🔶 surveyed, reasoning sound, not independently reproduced · 📝 captured from a
sub-survey, pending a verification pass.

## Architecture (so the findings make sense)

- **Single-threaded.** `GanlAdapter::run_main_loop()` (`ganl_adapter.cpp`) *is*
  the MUX main loop: each iteration calls `networkEngine_->processEvents()`,
  synchronously dispatches the resulting `IoEvent`s into the SessionManager
  callbacks (`onConnectionOpen`/`onDataReceived`/`onConnectionClose`), then runs
  `process_tinyMUX_tasks()`. Engine callbacks and MUX task code never run
  concurrently. The dominant hazard class is therefore **re-entrancy and stale
  pointers, not data races** — the adapter even removed its old mutex because
  shared_ptr destructors re-entered adapter methods.
- **Layers:** native pollers (`epoll`/`kqueue`/`select` on POSIX, `iocp`/
  `wselect` on Windows) → `ConnectionBase` + `io_buffer` + protocol handler →
  `secure_transport` (OpenSSL / Schannel) → `GanlTinyMuxSessionManager` (in
  `mux/src/ganl_adapter.cpp`) → MUX core (`net.cpp`, `bsd.cpp`, `DESC`).
- **`ConnectionHandle` is the fd** on POSIX engines (`network_types.h:20`,
  `= uintptr_t`; engines return `static_cast<ConnectionHandle>(fd)`). There is
  **no GANL-handle field inside `DESC`** — the association lives only in the
  adapter's three maps (`handle_to_desc_`, `desc_to_handle_`, `handle_to_conn_`)
  and `d->socket` doubles as the key.
- **History:** socket-object lifetime (destruction vs. pending async callbacks)
  is the single most-patched area — `e876ffe9f` (login-timeout UAF from stale
  handle mappings), `e0c27fa52` (pure-virtual on teardown), `62c3aa573` (QUIT
  double-free), `46aa0a2c3` (SSL session UAF). Treat new lifetime/teardown
  findings here as plausibly real, not auto-duplicate.

## Sub-part 1 — `ganl_adapter.cpp` bridge (surveyed 2026-06-10)

✅ **FIXED (dcf30cc4e): teardown-and-free paths left stale handle mappings.**
Two paths freed a `DESC` without dropping its handle→DESC mapping — the exact
shape of `e876ffe9f`, where a later GANL event on the same handle resolves
`get_desc()` to the freed pointer:
- `shutdownsock` (`bsd.cpp`) closed the fd and delisted the DESC but never
  called `remove_mapping`. Safe today only because its sole caller
  (`load_restart_db`'s post-restart cull, `net.cpp:3484`) runs with the maps
  already cleared. Now calls `remove_mapping` defensively.
- `GanlAdapter::close_connection`'s no-connection fallback freed the DESC but
  left it in `g_descriptors_list` / `g_descriptors_map` /
  `g_dbref_to_descriptors_map` and never unmapped it. Now mirrors
  `onConnectionClose`'s full delist+unmap. Reached only for an unmapped DESC;
  the common mapped path goes through `conn->close()`.
Both changes are no-ops on the common path. Verified live (clean QUIT, abrupt
RST, concurrent connections — no crash/pure-virtual/double-free).

✅ **ISSUE #790: `ConnectionHandle` (uintptr_t) truncated to `int` `d->socket`.**
`onConnectionOpen` (`d->socket = static_cast<int>(handle)`), the `@restart`
re-wire match (`d->socket == static_cast<int>(handle)`), and
`get_socket_descriptor` all narrow the handle to `int`. Safe while handles are
small POSIX fds; latent aliasing if any engine returns a handle above INT range
(pointer-valued or Windows `SOCKET`). In-code comment already flags it.

🔶 **NIT: `freeqs(d)` called twice in `onConnectionClose`** (lines ~803 and
~870). `freeqs` is idempotent (`net.cpp:324` — clears the deques, frees and
nulls `raw_input_buf`), so this is harmless redundancy, not a double-free.

🔶 **Note: `add_mapping` overwrites without cleanup.** If a handle is re-mapped
to a different DESC, the previous DESC's `desc_to_handle_` entry is orphaned.
Not reachable in the current flow (a handle maps to one DESC for its lifetime),
but the asymmetry is worth keeping in mind for outbound-connection reuse.

## Sub-part 2 — event engines (`mux/ganl/src/*_network_engine.cpp`) 📝

From a sub-survey; the epoll error-path item was independently verified.

✅ **ISSUE #791: epoll `processEvents` never handles `epoll_wait() == -1`.**
FIXED (a3032e249 + 9ac20a8f1). Literal placeholder (`// ... error handling for
nfds == -1 as before ...`, `epoll_network_engine.cpp:841`) returned 0 on error
→ caller re-polled immediately → CPU spin on a real error (EBADF/EINVAL); EINTR
mis-read as a timeout. Production Linux path. Now mirrors the select/kqueue
contract: EINTR → return 0 (no timeout sweep); other errno → log + return -1.
The two sibling placeholders in the same file were swept in the follow-up
commit: accept-error (~906) now logs a real failure (EMFILE etc.) before
breaking; listener EPOLLERR/EPOLLHUP (~912) now fully populates the Error event
(was leaking stale fields from the caller-reused `events[]` slot).

✅ **Candidates — verified against current source and filed 2026-07-20:**
- **ISSUE #942 — FIXED (PR #959, 2026-07-20):** epoll immediate-connect armed
  `EPOLLIN` but the map/handler expect `EPOLLOUT`, and no `epoll_ctl(MOD)` was
  issued → `ConnectSuccess` never fired. `initiateUnixConnect` had the same
  defect. Survey's "no callers" was **stale**:
  `mux/proxy/session_manager.cpp:1710-1714,2282-2286` call both, so every
  Unix-socket proxy back-door link hung on Linux (netmux doesn't use these
  paths). Fix: both connect paths register `OutboundConnecting`/`EPOLLOUT|EPOLLET`
  unconditionally — an already-connected socket is writable, so `EPOLL_CTL_ADD`
  reports `EPOLLOUT` on the next `epoll_wait` and the handler emits
  `ConnectSuccess` like the async path. Verified on Linux with a standalone
  libganl driver: AF_UNIX immediate connect emits `ConnectSuccess` post-fix and
  revert-verified to hang (timeout) pre-fix; full smoke 1306/1306 green.
- **ISSUE #943 — FIXED (PR #960, 2026-07-20):** lost write-wakeup at the
  `maxEvents` boundary — epoll/select/wselect disarmed `EPOLLOUT`/`wantWrite`
  **outside** the `eventCount < maxEvents` emit guard, stalling output. kqueue
  keeps disarm inside the guard (the pattern copied for select/wselect, which
  are level-triggered so an armed readiness simply re-reports). **epoll needed
  more than the guard move**: under `EPOLLET` a delivered edge is consumed, so
  "leave `EPOLLOUT` armed" never re-fires — and worse, the event loop's
  `eventCount < maxEvents` termination condition silently dropped whole
  fetched-but-unbudgeted kernel events (`epoll_wait` over-fetched up to 128 vs
  netmux's budget of 64). Linux verification caught both with a standalone
  libganl driver (deferred Write never arrived). Final epoll fix: (1) cap the
  `epoll_wait` fetch at `maxEvents` so unfetched ready-list entries stay queued
  in the kernel (ET edges included); (2) on budget exhaustion — loop-top for
  whole events, else-branch for same-fd Read+Write — re-arm via identical-mask
  `EPOLL_CTL_MOD`, which re-polls the fd and re-queues it if still ready.
  Driver-verified both deferral paths re-fire post-fix and revert-verified both
  stall pre-fix; full smoke 1306/1306 green.
- **ISSUE #944 — CLOSED, working-as-intended (2026-07-20).** `EPOLLHUP`/`EV_EOF`
  is checked before the readable payload, so a coalesced "data + FIN" is not read
  (kqueue emits Close for the `EV_EOF` kevent; epoll checks `EPOLLERR/EPOLLHUP`
  before `EPOLLIN`; select/wselect are read-first). But the net effect — the final
  command dropped on immediate client close — is **intended**: verified against
  TinyMUX 2.3 (`origin/release/2.3`), where `shutdownsock`→`freeqs(d)` discards
  `d->input_head` on disconnect and the command pipeline is the same deferred
  `Task_ProcessCommand` design. Commands are best-effort by the single-threaded
  "keep the server available to all players" principle. A prototyped read-before-
  close reorder was live-verified on kqueue/macOS (`Total read: 28, EOF=1` in one
  `handleRead`, vs 0 before) but changed nothing observable — the bytes are read,
  queued, then discarded by `freeqs` on close anyway — so it was reverted (PR #957
  closed). The half-close that *would* execute the final command (#956) was closed
  as by-design (it would exceed 2.3's guarantees). Residual minor nit: the
  cross-engine EOF-vs-read ordering divergence (kqueue/epoll EOF-first vs
  select/wselect read-first) — harmless, not worth a hot-path change.
- **ISSUE #946 — FIXED (2026-07-20):** select's `FD_SET` had no `FD_SETSIZE`
  bound → OOB write above the 1024th fd. **wselect refuted** — it already guards
  `fd_count < FD_SETSIZE`. Low current exposure (select engine near-unreachable
  via the factory; ulimit gate), but on glibc with `_FORTIFY_SOURCE` the miss is
  a hard `abort()` ("bit out of range 0 - FD_SETSIZE on fd_set"), i.e. a
  crash-DoS rather than silent corruption — driver-confirmed on Linux by burning
  fds past 1200 and adopting a socketpair end (pre-fix: abort; post-fix: clean
  `ENOBUFS`). Fix: reject `fd >= FD_SETSIZE` at all four registration sites —
  `createListener`/`acceptConnection` close the fd (engine owns it),
  `adoptListener`/`adoptConnection` reject without closing (ownership transfers
  only on success); `spawnSlave` is covered via `adoptConnection`. All three
  entry classes driver-verified rejecting with `ENOBUFS`.
- **ISSUE #947:** cross-engine `IoEvent`/close-ownership contract divergence
  (harmonization) plus two concrete point bugs: wselect never assigns `ev.buffer`
  on Read (stale-slot use-after-null, `:613-631`); select's `closeConnection`
  double-closes a running not-found fd (`:468-476`, missing the sibling
  idempotency).
- **ISSUE #945:** `checkNegotiationTimeouts` runs only on zero-event polls, so
  under sustained load telnet-negotiation timeouts are never enforced (half-open
  DoS assist). Confirmed in all five engines.
- No test coverage for `mux/ganl/` — a loopback harness driving each engine
  through the same scripted scenarios (EMFILE, HUP-with-data, maxEvents overflow,
  FD_SETSIZE) would catch most of these mechanically. Still a test-infra
  follow-up (see the `tests/netaddr/` pattern started in `bb2cb8f84`).

## Sub-part 3 — TLS transports (`openssl_transport.cpp`, `schannel_transport.cpp`) 📝

From a sub-survey; **verified against current source 2026-07-20** and filed.

✅ **OpenSSL (Linux/BSD):**
- **REFUTED — `SSL_read() == 0` truncation ambiguity.** The transport uses memory
  BIOs with `BIO_set_mem_eof_return(readBio, -1)` (`openssl_transport.cpp:232`),
  so a socket FIN never surfaces as `SSL_read()==0` (the mem BIO returns
  WANT_READ); truncation is caught independently at the engine layer as EOF →
  `Close`. `SSL_read()==0` here means a genuine decrypted close_notify, and the
  server takes no clean-vs-dirty-close-sensitive action. Not filed.
- **ISSUE #948:** renegotiation not disabled (`SSL_OP_NO_RENEGOTIATION` unset,
  `:76-83`) → TLS 1.2 renegotiation CPU-DoS; and no `SSL_CTX_set_mode` call, so a
  `WANT_READ` mid-`SSL_write` (via renegotiation) can trip `BAD_WRITE_RETRY` on
  the re-formatted buffer and drop the client.
- Peer verification hard-coded `SSL_VERIFY_NONE` — **benign today** (only caller
  is server-side, no outbound/cluster path exists; the verifyPeer-logging gap is
  already #743). Folded as a note into #948; would become real only if an
  outbound TLS client path is added.

✅ **Schannel (Windows) — verified by source reading (not compiled on the survey host):**
- **ISSUE #950:** on `SEC_I_RENEGOTIATE`, `context.established` flips false while
  the connection stays `Running`, so `sendData`'s `isEstablished()`-gated branch
  routes application replies to the **plaintext** else-path (`connection.cpp:
  320-325`) → peer-forced cleartext downgrade. HIGH (Windows).
- **ISSUE #951:** `encryptMessage` returns `Error` (drops the client) on messages
  larger than `cbMaximumMessage` (`:1249-1259`) instead of chunking — a long line
  of MUD output disconnects a Schannel client.
- **ISSUE #952:** TLS 1.2 only via the legacy `SCHANNEL_CRED` struct, no 1.3
  (OpenSSL build negotiates 1.3) — migrate to `SCH_CREDENTIALS`. Low.

## Sub-part 4 — protocol parsers (surveyed 2026-06-10)

**Routing fact (✅ verified, decides everything):** the live untrusted-input
telnet parser is `mux/src/telnet.cpp::process_input_helper`, invoked directly by
`ganl_adapter.cpp onDataReceived` (:750). The GANL-layer handler installed is
`RawPassthroughHandler` (ganl_adapter.cpp:1191/1325) — a no-op. WebSocket input
goes through `ws_process_input` in `mux/src/websocket.cpp`.

✅ **VERIFIED CLEAN — `mux/src/telnet.cpp::process_input_helper`.** The live
telnet parser is thoroughly hardened; the historical fixes are all present and
correct. SB accumulation is bounded by `qend = aOption + TELNET_OPTION_SIZE-1`
(case 17, :1047); the completed-SB dispatch length-gates every parser
(`5 == m` for NAWS :1064; `2 <= m` + `TELNETSB_IS` for TTYPE :1072; `envPtr <
&aOption[m]` walks for NEW-ENVIRON :1119; exact-length memcmp for CHARSET
:1254); outbound `send_sb` IAC-doubling math is exactly `6 + 2*nPayload`
(:121, no overflow); the UTF-8 / Latin1 / Latin2 emit paths bounds-check against
`pend` (`p < pend`, `p + nUTF <= pend`) with correct back-out; `nOption`
persists safely across reads (:1582). **No memory-safety bug.** Recorded so a
future audit doesn't re-plow it.

✅ **ISSUE #793: `telnet_protocol_handler.cpp` (1699 lines) is dead code.**
Compiled into libganl but never instantiated — written for an external "Hydra"
telnet *client*. Reasonably hardened internally (bounded SB cap 4096 + 30s
guard, correct NEW-ENVIRON ESC bounds) but a maintenance/audit hazard: looks
live, isn't. Latent gaps if activated (GMCP/MSSP/MXP SB unhandled →
default :1682; loose CHARSET `[TTABLE]` framing :66-77, bounds-guarded).

✅ **ISSUE #792: WebSocket RFC 6455 conformance (`websocket.cpp`).** Memory-safe
(length bounds, mask XOR, partial frames, `ws_state` lifetime all correct;
control-frame cap :449 and 64-bit bound :501 already hardened). Remaining gaps,
all bounded/low-severity:
- F1 CLOSE echoed but connection never terminated (:646 — keeps parsing frames).
- F2 fragmentation state unvalidated — 2nd non-FIN TEXT overwrites `frag_buf`
  (:609); CONTINUATION with no fragmentation in progress is delivered (:613).
- F3 RSV1/2/3 bits not rejected (:432).
- F4 no UTF-8 validation on TEXT frames (§8.1).
- F5 64-bit extended length truncates on the shipped Win32 (ILP32) build —
  `size_t` accumulation (:493-498) drops the high 4 bytes before the
  `> WS_MAX_PAYLOAD` check (:501) → protocol desync (not an over-read).

🔶 **NIT: protocol detection sniffs only the first packet.**
`ws_is_upgrade_request` (:132) checks for `"GET "` in the first 4 bytes; the
`DS_NEED_PROTO` decision in `onDataReceived` is locked on first data. Robust for
real HTTP (won't TCP-split inside the first 4 bytes), but a telnet user whose
first input is literally `"GET "` is misrouted to the WS handshake (then
dropped). UX edge, not security.

## Sub-part 5 — buffer + connection core (surveyed 2026-06-10)

`io_buffer.cpp` (341), `connection.cpp` (1386), `session_manager` (interface
only — no .cpp; the impl is `GanlTinyMuxSessionManager` in ganl_adapter.cpp).
Five issues filed; the headline is a live-path remote DoS.

✅ **ISSUE #794 (live, remote DoS — headline): no backpressure + throwing
buffer ops + no exception barrier → whole-server crash.** Three verified facts
combine: (1) `sendDataToClient` appends to `encryptedOutput_` with no high-water
mark (connection.cpp:299/308), so a never-reading client grows it without
bound; (2) `IoBuffer::ensureWritable`'s `buffer_.resize` throws `std::bad_alloc`
(io_buffer.cpp:138); (3) **zero try/catch** in connection.cpp / io_buffer.cpp /
the engines / `run_main_loop` — so the throw propagates to `std::terminate()`,
aborting the whole process. A condition that should drop one connection crashes
the server. Two independent fixes: a high-water disconnect (root) and an
exception barrier around per-connection dispatch (restores isolation for any
future accounting bug). Current `commitWrite`/`consumeRead` sites are provably
consistent, so `bad_alloc` is the reachable throw today.

✅ **ISSUE #795 (live, data-loss): plaintext `close()` drops queued output.**
Drain-before-close (`closeAfterWriteDrain_`) is armed only in the TLS branch
(connection.cpp:585-609); a plaintext `close()` goes straight to
`closeConnection(handle_)` at :614 with bytes still in `encryptedOutput_`. A
final line / boot message queued right before close on a write-blocked socket is
lost. `closeNetworkAfterDrain()` (:509) exists but isn't armed for plaintext.

✅ **ISSUE #796 (Windows/IOCP, memory-safety): WSASend reads `readPtr()` across
the async boundary while a concurrent append reallocates.** `postWrite` hands
`encryptedOutput_.readPtr()` to an overlapped WSASend (iocp_network_engine.cpp:
574); a concurrent `sendDataToClient` append (connection.cpp:299/308) can
`compact()`/resize the vector → UAF. The `lockForReuse` guard exists but is
never used on the write path. Readiness engines are safe (synchronous `::write`
from `readPtr()` in `handleWrite`). Windows-only; not on the Linux path.

✅ **ISSUE #797 (low/hygiene): session-manager grab-bag.** `session_errors_`
accumulates stale entries on failed `onConnectionOpen` (never cleared because
`onConnectionClose` is skipped for `InvalidSessionId`; connection.cpp:937) —
bounded by fd-table size (keyed by fd), not unbounded, and the whole error
channel is dead (`getLastSessionErrorString` has zero callers). Plus
`getConnectionHandle` returns a stale fabricated handle for closed sessions
(no `get_desc` check, asymmetric with siblings), and the `isAddress*` methods
are always-true/false stubs (latent bypass if ever wired).

✅ **ISSUE #798 (correctness/latent-UAF): inconsistent Closing-state guards.**
`handleNetworkEvent` (:189) and `postWrite` (:883) guard on `== Closed` while
`postRead` (:839) and `sendDataToClient` (:246) use `isClosingOrClosed()`. A
queued event in `Closing` state runs the pipeline on the TLS/protocol contexts
that `close()` already destroyed (:621). Includes the reason-specific
destructor re-entrancy workaround (ganl_adapter.cpp:799) and the missing
`return` after `close()` in IOCP `handleWrite` (:1351).

✅ **Candidates from the connection survey — verified 2026-07-20:**
- **ISSUE #949 (F2):** TLS-path partial write loses plaintext. Verified that
  OpenSSL does **not** retain the unconsumed plaintext (mem write-BIO holds only
  encrypted records); `processOutgoing` consumes input only on `bytesWritten>0`,
  and the next `sendDataToClient` runs `formattedOutput_.clear()` (now `:290`),
  discarding it. In the protocol/negotiation paths the source is a stack-local
  `IoBuffer`, so the loss is immediate.
- **ISSUE #953 (F8):** `continueTlsHandshake` return (`:777`) is dead **and**
  semantically inverted — both callers decide from `tlsProducedData`, so the
  return is a no-op today but a refactor trap. Filed with the two nits below.
- **REFUTED (F9):** epoll IS edge-triggered (`EPOLLIN|EPOLLET`), but the readiness
  read loop drains to EAGAIN before returning (`connection.cpp:1071-1109`), which
  is exactly the ET contract — no stall. The missing `postRead()` is harmless
  (epoll's `postRead` only stores the buffer pointer). Not filed.
- **ISSUE #953 (F5):** read-overflow guard closes via `handleError(0)` → "error 0"
  diagnostic. Trivial (near-unreachable; close reason still correct).
- **ISSUE #953 (io_buffer NIT):** `ensureWritable` growth has no overflow guard.
  Practically unreachable — but the survey's mitigation text was wrong: there is
  no websocket/telnet-SB buffering in this layer; the bound holds because the
  production handler is a raw passthrough and `required` is always small.

## Sub-part 6 — address / DNS (surveyed 2026-06-10)

`netaddr.cpp` (subnet matching = access control), the DNS slave (`slave.cpp` +
`slave_spawn_posix.cpp` + the adapter DNS channel). The two access-control
bypasses here are the strongest findings of the whole GANL survey.

✅ **ISSUE #799 (HIGH, access-control bypass): subnet containment inversion.**
`mux_subnet::compare_to` (netaddr.cpp:408-442) uses strict `<` on both bounds in
the kContains branch, so a wider subnet sharing a base/end address with a
narrower one (10.0.0.0/8 ⊃ 10.0.0.0/24 — the common nested-CIDR case) is
misclassified as kContainedBy. This inverts the access-control subnet tree: a
`forbid 10.0.0.0/8` silently stops applying after any rule on 10.0.0.0/24 is
added → a banned host is let in. Reachable through normal `@admin site` usage;
distinct from the historically-fixed subnet bugs. Verified by reading compare_to.

✅ **ISSUE #800 (HIGH, access-control bypass): IPv4-mapped IPv6 not
canonicalized.** Listeners are dual-stack (`IPV6_V6ONLY=0`, epoll:174/kqueue:178/
select:214), so an IPv4 client arrives as `::ffff:a.b.c.d`, and
`compare_to(MUX_SOCKADDR*)` (netaddr.cpp:459-466) builds a pure `mux_in6_addr`
with no v4-mapped → v4 normalization. A `forbid 1.2.3.4` rule (AF_INET) never
matches the mapped form → IPv4 ban bypassed by connecting over the v6 socket.
Verified.

✅ **ISSUE #801 (moderate, integrity): reverse-DNS hostname unsanitized +
slave protocol has no per-record validation.** The PTR hostname (client controls
their own record) is forwarded verbatim by the slave (slave.cpp:84,125) and
stored unfiltered into `A_LASTSITE`/`A_LASTIP` and `%s` log lines
(apply_reverse_dns_result, ganl_adapter.cpp:2339; net.cpp:1484). Control
chars/ANSI reach wizard-visible site displays. The newline-delimited slave→parent
protocol applies any well-formed line to an arbitrary numeric with no
query/response correlation — a cross-connection audit-spoof if the resolver ever
emits an unescaped newline (modern glibc escapes them; not guaranteed). Plus
uncapped `readBuffer`/`pendingWrites` (low DoS).

✅ **VERIFIED SAFE / FIXED (recorded so they aren't re-audited):**
- Access control uses the binary sockaddr (`isForbid(&d->address)`,
  ganl_adapter.cpp:615 / net.cpp:3011), NOT the reverse-DNS hostname — a spoofed
  PTR cannot bypass bans.
- The historically-fixed subnet bugs are all still correct: `mux_in6_addr`
  `operator==`/`<` compare contents via `memcmp(...,16)` (netaddr.cpp:1378/1368);
  `mux_sockaddr::operator==` IPv6 uses the 16-byte size (:1272); the subnet tree
  `reset` assigns the `remove()` return (net.cpp:2921), `remove` kContainedBy/
  kEqual detach siblings/children before delete (:2812-2847); `DecodeN` overflow/
  shift/hex handling correct.
- The getaddrinfo trailing-newline history is fixed (slave.cpp:220 strips
  `\n`/`\r`/space before the lookup); slave fork/exec fd hygiene (CLOEXEC, child
  cleanup on adopt failure) is correct.

🔶 **NITs (docs only):** `parse_subnet` leaks a partially-built mask on a
malformed-family mask (netaddr.cpp:559, error-path cosmetic leak); the
leading-zero strip loops (netaddr.cpp:91/139) read the separator/NUL one past the
token (harmless, in-bounds). DNS slave SIGCHLD counter is a racy non-atomic RMW
(slave.cpp, robustness only, MAX_CHILDREN=20 caps it); the slave child pid isn't
tracked by the parent (generic reaper still collects it — no zombie).

## Survey status: COMPLETE — fix pass underway

All six sub-parts surveyed (bridge, engines, protocol parsers, TLS, buffer+
connection core, address/DNS). Issues filed: **#790–#801**.

**Second filing pass (2026-07-20):** the previously-unfiled sub-part 2 (engines),
sub-part 3 (TLS), and sub-part 5 (connection core) candidates were independently
re-verified against current source and filed as **#942–#953**: #942 (epoll
immediate-connect), #943 (maxEvents write-stall), #944 (EOF-before-payload), #945
(negotiation-timeout-on-idle), #946 (select FD_SETSIZE), #947 (cross-engine
contract + wselect/select point bugs), #948 (OpenSSL renegotiation/write-mode),
#949 (TLS partial-write plaintext loss), #950/#951/#952 (Schannel downgrade /
chunking / TLS 1.3), #953 (connection-core hygiene). Two candidates were
**refuted** on verification and are recorded inline: OpenSSL `SSL_read()==0`
truncation (mem-BIO decouples EOF) and connection F9 (epoll ET stall — read loop
drains to EAGAIN).

**Fix pass:**
- **#794 — FIXED (df9f5de02):** per-connection output high-water mark in
  `GanlAdapter::send_data` (drop whole writes past 16× output_limit / ≥1 MB,
  charged to `output_lost`; no synchronous close — send_data runs inside
  process_output's drain loop) + exception barrier at the run_main_loop event
  dispatch and in send_data. New `ConnectionBase::pendingOutputBytes()`.
  No-op on the normal path; verified no regression + server survives a
  non-reading flood.
- **#799 — FIXED (b7eace169):** `mux_subnet::compare_to` containment now uses
  inclusive bounds (kEqual checked first, then `!(b<a)` on both ends), so
  same-base/same-end nested CIDRs classify correctly. Pure improvement, verified
  by case analysis + smoke. NOTE: no automated end-to-end test is reachable
  (siteinfo only checks live connection addresses; a compare_to unit test needs
  a netmux-side C++ harness that pulls in the driver) — the missing subnet-tree
  test infra is a worthwhile follow-up.
- **#800 — FIXED (d841f7a36):** `CanonicalizeMappedV4()` rewrites an inbound
  `::ffff:a.b.c.d` to native AF_INET in `onConnectionOpen` before access
  control/display/logging, so a single IPv4 ban covers both wire forms.
  No-op for native v4 / genuine v6. Verified by build + smoke + native-v4
  regression + code reasoning; NOTE the v4-mapped positive demo was
  environment-blocked (this sandbox binds the listener v4-only and
  `ip_address ::` didn't override it, so no mapped connection occurs locally
  — reproduce on a dual-stack-binding host).
- **#795 — FIXED (469c23fd4):** `close()` now drains queued plaintext output
  before closing the socket (else-if branch mirroring the TLS deferral via
  `closeAfterWriteDrain_`/`handleWrite`/`closeNetworkAfterDrain`). Inert on the
  empty-buffer common path. Verified by build + smoke + native close
  regression; queued-output positive demo needs a logged-in large-output
  session (no starter-DB credential here).
- **#798 — FIXED (19d5354f3):** `handleNetworkEvent` now ignores Read events
  when `isClosingOrClosed()` (don't reprocess input on torn-down TLS/protocol
  contexts), keeping Write (for the #795 drain) and Close/Error. `postWrite`
  intentionally stays `== Closed` (the drain needs Closing writes) — corrected
  its debug string. Also fixed the IOCP handleWrite post-close fall-through.
  Split the destructor re-entrancy remainder out to **#802** (reason-based vs
  state-based guard). Verified by build + smoke + live native-IPv4 regression
  incl. a data-then-QUIT drain.
- **#802 — FIXED (a808e9310):** the onConnectionClose skip-process_output guard
  was `reason != ServerShutdown`, inferring destructor teardown from the
  disconnect reason; `~ConnectionBase` can reach `cleanupResources` with any
  reason. Now state-based: `ConnectionBase::inTeardown_` (set in the destructor
  before `cleanupResources`, exposed as `isTearingDown()`), and onConnectionClose
  flushes only when the connection is still live (`get_connection(d)` non-null
  and `!isTearingDown()`). Key invariant: the map's shared_ptr is released
  *before* `~ConnectionBase` runs (that release drops the refcount to zero), so
  `get_connection()` returns null during teardown for any reason — the correct
  reason-independent signal. `send_data` also gained an `isTearingDown()`
  backstop at the `sendDataToClient()` chokepoint. Live-tested: logged-in QUIT
  flushes + clean NET/DISC, no pure-virtual abort; smoke 1115/1115.
- **#797 — FIXED (6f6b71dff):** session-manager hygiene — removed the
  write-only `session_errors_` channel (leak); `getConnectionHandle` gates on
  `get_desc`; `isAddress*` now route through `g_access_list` instead of
  always-true/false placeholders. Build + smoke.
- **#803 — FIXED (4f73b5463), found during this fix pass — config-file site
  rules never loaded.** `cf_read()` (in LoadGame) runs before
  `conn_bridge_init()` (in Startup) creates `g_pDriverCtl`, so every
  `forbid_site`/`register_site`/… was silently dropped — site bans via config
  did nothing. Fix: `site_update` brings the bridge up on demand. **First
  GANL-area fix with a full live bite-then-not-bite demo** (pre-fix: empty
  `@list`, loopback "Connection opened"; post-fix: rules in `@list`, loopback
  "Connection refused"). This was more fundamental than #799/#800 — those fixed
  the matcher; this is why rules never reached it.
- **Live-test gotcha (cost real time):** port 2860 on this host is held by
  another user's (`farm`) netmux; a same-port test server silently fails to bind
  and clients hit the wrong server. Always start test instances on a free port
  (e.g. 7860) and confirm ownership with `ss -ltnp` / `pgrep -u sdennis netmux`.
- **#790 — FIXED (7ede66556):** guard at onConnectionOpen refuses a
  connection handle outside SOCKET range (fail loud vs silent aliasing into
  `d->socket`); `d->socket` casts int→SOCKET for consistency. No-op for real
  fds (listener handle is 1); build + smoke + live.
- **#801 — FIXED (43edaf7e5):** reverse-DNS hostname (attacker-controlled PTR)
  flowed verbatim into A_LASTSITE/A_LASTIP, WHO/site display, and %s logs, and
  an embedded newline could split one slave response into multiple parsed
  records (forging another connection's audit entry). Now sanitized to the
  LDH-plus-dot/underscore charset in `slave.cpp query()` (at the source, kills
  the wire-injection vector) and defensively again in `apply_reverse_dns_result`
  (covers the Windows worker path). Slave protocol buffers bounded: readBuffer
  drops the slave past 64 KiB undelimited; pendingWrites caps at 4096. Filter
  unit-tested; live loopback still resolves 127.0.0.1 → localhost; smoke
  1115/1115. Not a ban bypass (access control uses the binary sockaddr).
- **#793 — FIXED (537b155a2):** removed the dead `telnet_protocol_handler.{cpp,h}`
  (1699-line Hydra telnet-*client* parser) from libganl — compiled but never
  instantiated (live handler is `RawPassthroughHandler` → `mux/src/telnet.cpp::
  process_input_helper`). Dropped from Makefile.am, regenerated Makefile.in,
  clean rebuild, smoke 1115/1115. User chose deletion over banner/build-gate.
- **#792 — FIXED (83615e880):** five RFC 6455 conformance gaps in the live WS
  parser (no memory-safety bug). New `ws_fail()` (Close + flush + close, safe
  from inside the parser via the dispatch shared_ptr). F1 received-CLOSE now
  terminates; F2 fragmentation state validated (orphan CONTINUATION / mid-frag
  TEXT → 1002); F3 RSV bits → 1002; F4 strict RFC 3629 UTF-8 check on TEXT →
  1007; F5 64-bit length accumulated into uint64_t before the narrowing bound
  check (was truncating on 32-bit Win32). Live-tested with a raw masked WS
  client (F1–F4 bitten; valid multibyte accepted); F5 correct-by-construction
  (32-bit-only). Smoke 1115/1115.
- **#796 — FIXED (f768ef58b):** Windows/IOCP-only UAF — `postWrite` aliased
  `encryptedOutput_.readPtr()` into the overlapped `WSASend`, so a concurrent
  `sendDataToClient` → `ensureWritable` compact/resize could relocate/free the
  bytes the async send was reading. Fix (copy option): the Write `PerIoData`
  now owns a `std::vector<char>` copy of the outbound bytes; `wsaBuf.buf` points
  into it, freed by `~PerIoData` after the completion. Fixed by construction —
  `_WIN32`-only, not in the POSIX build, so not compiled/live-tested here; Linux
  build clean, smoke 1115/1115.
- **GANL filed-issue backlog CLEARED this pass: #790, #791, #792, #793, #796,
  #801, #802.** Remaining follow-ups are not filed issues: the engine/TLS
  candidates listed above (epoll immediate-connect EPOLLOUT, lost write-wakeup
  at maxEvents, HUP-before-payload, FD_SETSIZE bound, cross-engine close
  divergences, negotiation-timeout-only-on-idle), and the socket-buffer-edge
  fixes (#794/#795/#798) which want the stress/unit harness follow-up (not
  live-testable here — kernel SNDBUF autotunes to 4MB).
- **Test infra — STARTED (bb2cb8f84):** `tests/netaddr/` now unit-tests
  `mux_subnet::compare_to(mux_subnet*)` by linking `netmux-netaddr.o` against
  libmux with three driver-global stubs (`g_bStandAlone`, `g_pILog`,
  `g_pINotify`) + `pool_init(POOL_LBUF, ...)`. 14 cases; **bug-catch verified**
  (reverting compare_to to the strict-`<` logic fails exactly the 4 shared-bound
  cases). This is the mechanical lock #799 was missing, and the pattern (a
  netmux-side object + libmux + a few stubs) generalizes to other netmux-only
  code that was previously untestable. Future extensions: address-vs-subnet
  (`compare_to(MUX_SOCKADDR*)`) and a v4-mapped case toward #800 (whose fix is
  adapter-side, so it needs the adapter or a refactor to reach).
- **#800 — now also locked (a4a00fa8b):** added v4-mapped canonicalization to
  `compare_to(MUX_SOCKADDR*)` itself (defense-in-depth at the access decision,
  complementing the adapter ingress fix) + 6 address-vs-subnet harness cases
  incl. the `::ffff:a.b.c.d` case. Bug-catch verified (removing the branch
  fails exactly that case). The harness is now 20 cases.
- Still reasoning-only (no mechanical lock yet): the connection/output fixes
  (#794/#795/#798) — they need either a known login credential for live
  large-output demos or a connection-level test harness (heavier: connection.cpp
  pulls in the engines).
