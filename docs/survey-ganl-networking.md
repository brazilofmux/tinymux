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
Literal placeholder (`// ... error handling for nfds == -1 as before ...`,
`epoll_network_engine.cpp:841`). Returns 0 on error → caller re-polls
immediately → CPU spin on a real error (EBADF/EINVAL); EINTR is mis-read as a
timeout. Production Linux path. Several sibling placeholders in the same file
(listener-error ~912, accept-error ~906) need the same sweep.

📝 **Candidates pending verification** (file individually after confirming):
- epoll: immediate-connect path arms for an `EPOLLOUT` it never registers
  (`~393-439`); `initiateUnixConnect` has the same defect (AF_UNIX connects
  succeed immediately, so it would actually trigger — currently no callers).
- epoll/select/wselect: lost write-wakeup at the `maxEvents` boundary — clears
  `wantWrite`/disables `EPOLLOUT` even when no Write event is emitted, stalling
  output. kqueue gets this right (`kqueue_network_engine.cpp:876-878`) — the
  pattern to copy.
- epoll/kqueue: `EPOLLHUP`/`EV_EOF` checked before the readable payload, so a
  combined "peer wrote then closed" (`QUIT\n`+FIN in one wakeup) drops the final
  data. select/wselect read-first ordering doesn't.
- select/wselect: no `FD_SETSIZE` bound before `FD_SET` → out-of-bounds write
  past the `fd_set` bitmap above the 1024th fd (`MAX_SOCKET_FDS` defined but
  never enforced).
- Cross-engine divergences: who closes an errored connection (select/wselect
  self-close + synthesize Close; epoll/kqueue/iocp leave it to the caller —
  double-close hazard); EMFILE-during-accept (some emit a listener Error, epoll
  spins); ET-vs-LT read contract; `ev.buffer`/stale-`IoEvent`-field validity
  varies by engine and is undocumented.
- Systemic: `checkNegotiationTimeouts` runs **only** when the poll returns zero
  events, so under sustained load telnet-negotiation timeouts are never enforced
  (immortal half-open connections = DoS angle).
- No test coverage for `mux/ganl/` — a loopback harness driving each engine
  through the same scripted scenarios (EMFILE, HUP-with-data, maxEvents overflow,
  FD_SETSIZE) would catch most of these mechanically.

## Sub-part 3 — TLS transports (`openssl_transport.cpp`, `schannel_transport.cpp`) 📝

From a sub-survey; **not yet independently verified** — confirm before filing.

📝 **OpenSSL (Linux/BSD):**
- `SSL_read() == 0` is mapped to a clean close *before* consulting
  `SSL_get_error()` (`~396`), so a truncation (`SSL_ERROR_SYSCALL` EOF without
  close_notify) is indistinguishable from a graceful disconnect. Branch on
  `SSL_get_error()` first; only `SSL_ERROR_ZERO_RETURN` is Closed.
- Renegotiation not disabled (`SSL_OP_NO_RENEGOTIATION` unset) → TLS 1.2 client
  can force renegotiation (CPU DoS); combined with no `SSL_MODE_*` write flags,
  a `WANT_READ` mid-`SSL_write` can trip `BAD_WRITE_RETRY` and drop the client.
- Peer verification hard-coded `SSL_VERIFY_NONE` (config only warns); fine for a
  public listener, unsafe for any outbound/cluster link.

📝 **Schannel (Windows) — flagged as a Windows release-blocker by the survey:**
- On `SEC_I_RENEGOTIATE`, `context.established` flips to false while the
  connection stays `Running`, so subsequent server replies egress as **plaintext
  on the TLS socket** — a peer-forced cleartext downgrade. Diverges sharply from
  the OpenSSL build.
- `encryptMessage` errors out (drops the client) on messages larger than the
  stream max instead of chunking — a long line of MUD output can disconnect a
  Schannel client.
- TLS 1.2 only, no 1.3 (OpenSSL build negotiates 1.3) — cross-platform
  divergence.

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

## Sub-parts not yet surveyed

- **Buffer + connection core** (`io_buffer.cpp`, `connection.cpp`,
  `session_manager`) — partial I/O, backpressure, drain-on-close, queue lifetime.
  Note `io_buffer.cpp ensureWritable` computes `writePos_ + required` with no
  overflow check (theoretical, multi-GB only) — flagged by the handler survey.
- **Address/DNS** (`netaddr.cpp`, `network_address.cpp`, `slave_spawn_posix.cpp`)
  — the `getaddrinfo` trailing-newline bug lived here; subnet `operator==`/`<`
  history.
