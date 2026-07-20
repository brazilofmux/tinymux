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
- **Terminal events do not transfer close duty.** After an engine emits
  `Close` or a connection-level `Error`, the fd is closed by whoever the
  engine's convention says — and that convention MUST be: the engine
  removes the fd from its own multiplexer/tracking, and the *application*
  (via `ConnectionBase::close()` → `closeConnection`) performs the actual
  close. An engine that closes the fd itself must still keep
  `closeConnection` a safe no-op afterwards (see idempotence above); it
  must never emit further events for that handle.
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

- **wselect:** Read events never assign `ev.buffer` — the saved
  `bufferRef` is dead and the slot retains a stale pointer (issue #947,
  needs the Windows box; fix is `ev.buffer = bufferRef`).
- **kqueue:** the accept-error `Error` event leaves `bytesTransferred` /
  `buffer` / `remoteAddress` unset (stale-slot pattern; macOS box).
- **epoll:** EMFILE during accept logs but emits no listener `Error`
  event; the LT listener re-reports so it also busy-spins — as do the
  other engines, for the same reason (tracked in the survey).
- **iocp:** completion-model differences (real `bytesTransferred`,
  buffer ownership during overlapped I/O) are documented in the engine
  itself; the field-population rule above still applies.
