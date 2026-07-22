# Survey: live network + queue stress (defensive)

A live-netmux stress harness for the accept/read/write path and the command
scheduler — the queue "in its habitat" (a queue built for the network is a
different animal without one; see `survey-queue.md`). This is **defensive
work**: TinyMUX is deployed software, and the point is to push the network
and queue hard enough to find problems here, in a throwaway server, before a
live game hits them.

Harness: `tests/stress/{run.sh,stress_driver.py}`, opt-in via
`make test-stress` (live-socket, timing-sensitive, multi-second — not in
`make test`). It spins a throwaway netmux from the starter DB on a free port,
drives it, then asserts the server survived, stayed responsive, and — where
the load is within limits — lost nothing.

## The queue has reactive defenses; a stress driver must negotiate them

The instructive part of building this was discovering that a naive stress
driver defeats *itself* on the server's self-protection, then misreads the
defense as its own bug. The defenses found and characterised:

- **`QueueMax` — per-owner queue-depth cap.** Exceeding it does not just
  refuse the excess: it **halts the owner and flushes their entire queue**
  (`setup_que` → `halt_que` + `s_Halted`). A halted object then silently
  queues nothing (`setup_que`'s first line is `if (Halted(executor)) return`),
  so *every later command is a no-op until the halt is cleared*. A burst that
  trips the halt therefore poisons all subsequent work on that object — the
  symptom that sent the first driver iterations chasing a phantom "small
  fan-outs stopped working" bug.
  - The cap defaults to `player_queue_limit` (`mudconf.queuemax`), **bumped
    to `db_top + 1` for wizards** — so it scales with game size: a bigger
    game legitimately queues more, while the runaway threshold still tracks
    reality. A per-player `@QueueMax` attribute overrides it. On the ~12-object
    starter DB the wizard headroom is tiny, which is why unthrottled bursts
    trip it instantly.
  - Recovery is `@set <obj>=!halt` (the `Halted` flag is settable).
- **`lnum()` is LBUF-bounded** (~5000 elements ≈ 24KB < 32KB), so a single
  `@dolist` fan-out is capped there; more depth needs repeated fan-outs.
- **Per-cycle command quota** (`command_quota_max`) throttles *dequeue rate*,
  independent of the *depth* cap above — raising one does not raise the other
  (the first-iteration mistake: raised the quota, not the cap).
- **`function_invocation_limit`** (default 25000) bounds a single evaluation.

The driver negotiates rather than fights: clear any leftover halt at startup,
`@queuemax` up above the integrity load, and `@queuemax` *down* to make the
shed reproducible — then treat the halt-and-flush firing as the *result*.

## What the harness asserts

Best-effort semantics mean dropping under overload is CORRECT, so the
assertions are survival-shaped:

| Phase | Asserts |
|---|---|
| baseline | server responds |
| integrity | 5000-entry counted fan-out dispatches **all 5000**, in order (raised cap) |
| concurrency | 16 sockets each enqueue 500 at once → **8000/8000 exact**, uncorrupted |
| shed | an over-cap burst **halts+flushes** the owner (the defense fires) and the server does not crash |
| survival | server stays responsive *during* the shed |
| availability | a **brand-new connection is served** while the offender is halted — the whole game stays up |
| recovery | after clearing the halt, the queue works again |

Baseline throughput on this box: **~16,000 queue entries/sec** dispatched
(starter DB, loopback). Reported, not asserted — it is a regression
watermark, not a target.

## Result

No defect found: under every workload the server behaved correctly and the
defenses fired as designed. That is the intended outcome of a *baseline*
defensive harness — it now stands as a regression guard so a future change
that breaks shedding, corrupts concurrent enqueue, or wedges the server under
load fails here instead of in a deployed game.

## Harder hunt vectors (next, to actually break things)

The baseline exercises well-formed load. The adversarial pushes most likely
to surface a real defect — the "find it before someone else does" work — are
network-robustness, not queue-volume:

- **Connection churn**: rapid connect/disconnect storms (the disconnect
  `freeqs`/`shutdownsock` path; half-open sockets; connect-and-immediately-drop).
- **Malformed / fragmented input**: partial lines, no trailing newline,
  bytes dribbled one at a time, embedded NULs, over-long input lines at the
  read-buffer boundary.
- **Output backpressure**: a slow/stalled reader while the server is forced
  to emit a flood (the send-side buffering and drop policy).
- **Accept storms / fd pressure**: many simultaneous connects toward the
  descriptor limit.
- **TLS**: the same, against the OpenSSL/Schannel path, incl. mid-handshake
  drops.

These are the natural extensions of `stress_driver.py`; each wants the same
"survive + stay available" assertion shape.
