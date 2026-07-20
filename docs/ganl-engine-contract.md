# GANL NetworkEngine contract

Rules every engine (`epoll`, `kqueue`, `select`, `wselect`, `iocp`) must
follow so `ConnectionBase` / the adapters can treat them interchangeably.
Written for issue #947 after the cross-engine survey found the contract
living only in scattered comments. Where an engine currently deviates, the
deviation is listed at the end — deviations are bugs to fix, not variants
to emulate.

## 1. Close ownership

- **`closeConnection(handle)` is idempotent.** A handle whose fd is not in
  the engine's socket map MUST no-op — regardless of whether the engine is
  running or shutting down. Rationale: `ConnectionBase::handleClose`
  re-closes defensively after the engine has already released the fd, and a
  raw `shutdown()`/`close()` on an unmapped fd number can tear down an
  unrelated descriptor that reused the number. (epoll, kqueue, wselect
  already no-op; select fixed for #947.)
- **The engine never closes a connection's fd from `processEvents`.** On a
  terminal condition (`Close` or connection-level `Error`) the engine emits
  the event, MAY deregister its own interest in the fd, but MUST keep the
  fd open and mapped; the *application* (via `handleClose`/`handleError` →
  `ConnectionBase::close()` → `closeConnection`) performs the actual close.
  Rationale: an engine-side close frees the fd *number* while the caller
  still holds a live handle to it, so a subsequent `accept()` can reuse the
  number and collide with the stale handle — the recurring GANL UAF class.
  Emit exactly ONE terminal event per teardown (select's old
  Error-then-synthesized-Close pair is gone). epoll and kqueue always
  worked this way; select was harmonized for #947 (its error branch now
  deregisters from the master fd sets — so level-triggered select does not
  re-report — and leaves the close to the application). Regression-guarded
  by the `conn-error-defer-close` harness scenario, which fails against the
  old self-closing select.
- **Listener errors do not tear down the listener.** The engine emits the
  listener-level `Error` and leaves the listener registered; the adapter
  logs it (NET/LERR). A transient accept failure (e.g. EMFILE) must not
  kill the listening socket.
- **Exception — outbound connect failure (epoll-only today):** on
  `ConnectFail` the engine cleans up the never-established socket itself
  (EPOLL_CTL_DEL + close + untrack). The application never had a working
  connection, and `closeConnection` afterwards is a safe no-op per the
  idempotence rule. No other engine currently implements outbound connect.
- **`detachConnection`/`detachListener`** deregister and untrack WITHOUT
  closing — used by `@restart` to let fds survive exec. They are also
  idempotent on a not-found fd.

## 2. IoEvent field population

The caller reuses one `IoEvent events[]` array across `processEvents`
calls; **a stale field from a previous event in the same slot is the
canonical GANL info-leak bug** (fixed once already in the epoll listener
error path). Therefore:

- Every emitted event MUST assign **all** fields consumed for its type, and
  null the rest: `type`, `connection`, `listener`, `context`, `error`,
  `bytesTransferred`, `buffer`, `remoteAddress` as applicable.
- Per type:
  - `Accept`: `connection` = new handle, `listener` set, `remoteAddress`
    set, `buffer = nullptr`, `bytesTransferred = 0`, `error = 0`.
  - `Read` (readiness engines): `bytesTransferred = 0`, `buffer` = the
    posted `activeReadBuffer` if the engine tracks one, else `nullptr`.
    The consumer performs the actual `read()`.
  - `Write`: `buffer = nullptr`, `bytesTransferred = 0`; `context` = the
    connection context (plus engine-specific write user-context if any).
  - `ConnectSuccess`/`ConnectFail`: `connection` set, `error` = 0 /
    `SO_ERROR` value.
  - `Close`/`Error`: `error` = the errno if known, else 0/`ECONNABORTED`
    respectively; remaining pointer fields explicitly nulled.
- When in doubt, zero the whole event before filling
  (`ev = IoEvent{}`), then assign.

## 3. Readiness delivery, budgets, and LT/ET

- **Consumers must drain to `EAGAIN`.** epoll registers connections
  edge-triggered (`EPOLLET`); kqueue/select/wselect are level-triggered.
  The read path in `ConnectionBase` drains until `EAGAIN`, which is what ET
  requires and is harmless under LT. Any new consumer of Read events MUST
  do the same; partial reads under ET stall silently.
- **The per-poll `maxEvents` budget must never eat readiness.** If an
  engine cannot emit an event this poll, the underlying readiness MUST
  still be deliverable on the next poll:
  - LT engines: simply leave the interest armed (select/wselect pattern).
  - ET engines: the consumed edge must be re-armed — epoll caps its
    `epoll_wait` fetch at `maxEvents` and re-arms overflowed fds with an
    identical-mask `EPOLL_CTL_MOD` (issue #943). Disarming interest
    without emitting the corresponding event is a contract violation.
- **Negotiation-timeout sweeps must not depend on idle polls** (issue
  #945): a busy server never returns zero events, so a sweep gated on
  `nfds == 0` never runs.

## Known deviations (open)

- **wselect:** still uses the legacy self-close convention on a
  connection error (close + synthesize inside `processEvents`) instead
  of the defer-to-application model of §1. Safe today because its
  `closeConnection` is idempotent and `handleClose` is state-guarded,
  but it carries the fd-number-reuse hazard §1 describes. Harmonizing it
  (mirror the select fix: emit `Error`, deregister interest, keep the fd
  mapped) needs the Windows box + harness run; `conn-error-defer-close`
  is POSIX-only until then.
- **iocp:** completion-model differences (real `bytesTransferred`,
  buffer ownership during overlapped I/O) are documented in the engine
  itself; the field-population rule above still applies.

## Resolved deviations

- ~~**wselect:** Read events never assign `ev.buffer`~~ — fixed in #962;
  regression-guarded by the Windows harness (`accept-read-ev-buffer`).
- ~~**kqueue:** accept-error `Error` event left `bytesTransferred` /
  `buffer` / `remoteAddress` unset~~ — fixed in #967 (full population).
- ~~**kqueue:** EV_ERROR after a budget-suppressed EV_EOF overwrote
  another connection's event~~ — fixed 2026-07-20: the overwrite index
  now keys on whether the Close was actually *emitted* (`closeEmitted`),
  not merely detected, so a budget-starved Close can no longer redirect
  the Error into a stranger's slot.
- ~~**epoll:** EMFILE during accept logs but emits no listener `Error`
  event~~ — fixed 2026-07-20: epoll now emits the fully-populated
  listener `Error` like select/kqueue (driver-verified with a clamped
  `RLIMIT_NOFILE`). The busy-spin on a pending-but-unacceptable
  connection remains cross-engine (LT listener re-reports; tracked in
  the survey).
- ~~**select:** self-closed errored connections (`fdsToClose` sweep +
  synthesized `Close`)~~ — harmonized 2026-07-20 to the §1 defer model;
  regression-guarded by `conn-error-defer-close` (fails against the old
  behavior via MSG_OOB → exceptfds).
- ~~**all POSIX engines:** per-site field population~~ — every emission
  site in epoll/kqueue/select now zeroes the slot (`ev = IoEvent{}`)
  before filling, mechanically enforcing §2.
