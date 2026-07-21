# Survey: the command queue (cque.cpp / CScheduler)

Review of the queue design and, specifically, of the context shipped
with each queue entry — what is copied versus referenced, and what that
costs. Conducted 2026-07-21 as the first hardening pass of the
feature-complete 2.14 line (see `status-2.14.md`).

## Design confirmed sound

- **Two heaps** (`CTaskHeap` over `TASK_RECORD`): a when-ordered
  min-heap for timed tasks and a priority-ordered min-heap for
  immediate ones (`externs.h:1425-1443`).
- **FIFO within a cycle is a hard guarantee, not an accident**: both
  comparators tie-break on a monotone `m_Ticket` issued at scheduling
  time, giving a total, stable order for same-`when` / same-priority
  entries.
- **Priorities**: `PRIORITY_SYSTEM < PLAYER < OBJECT < SUSPEND`, with
  `CF_DEQUEUE` mapping onto a minimum-priority cutoff — disabling
  dequeue simply raises the floor above OBJECT/PLAYER.
- **Network integration by timeout**: the scheduler's next-deadline
  feeds the network engine's poll timeout, so an idle server blocks in
  the kernel and does nothing — no polling (verified during the GANL
  work; reconfirmed here).

## Context shipped per entry (BQUE) — copy vs reference

| Field | Mechanism | Cost |
|---|---|---|
| `comm` + `env[0..9]` | ONE exact-sized `MEMALLOC(tlen)`; strings memcpy'd into it | necessary snapshot (args must be frozen at enqueue); near-optimal — no LBUF-sized slack |
| `scr[36]` (%q0-z) | `RegAddRef` on each non-null `reg_ref*` | ~zero — refcount bump, value shared |
| `named_scr` | `NamedRegsCopy`: **null when empty**; else new map with **refcounted values** (nodes copied, strings shared) | proportional to named-register count; zero on the common path |
| `iter_token` / `switch_token` | was: `alloc_lbuf` (32KB) + strncpy per entry | **fixed** — see below |
| STUB_SLAVE `pResultsSet` | `AddRef` | ~zero |
| dispatch (`Task_RunQueueEntry`) | **move semantics** — registers, named map, and result set pointers are transferred into `mudstate` and nulled in the entry | zero copies, zero refcount churn |

Conclusion: the enqueue path was already reference-based everywhere the
semantics permit. The env/command snapshot is a single exact-sized
allocation, registers are refcounted, and dispatch is pure ownership
transfer. The design intuition ("copying could be a problem") had
exactly one confirmed instance:

## Finding (fixed): token LBUFs — 32KB held per entry for tiny strings

`wait_que` allocated a full pool LBUF (32,768 bytes) for each of
`iter_token` (the `##` element of a queued `@dolist`) and
`switch_token` (the `#$` match of a queued `@switch`), held from
enqueue until dispatch. Tokens are typically a few bytes.

Measured with `@list allocations` immediately after enqueue (before the
queue drains):

| | Lbufs in use | max RSS (20k-entry `@dolist`) |
|---|---|---|
| before | **5,002** (5,000 entries) | **244 MB** |
| after | **2** | **21 MB** |

Fix: `StringClone` (exact-size `MEMALLOC`) at the one allocation site;
the four `free_lbuf` pairs became `MEMFREE`. Content and semantics are
byte-identical (`##`/`#$` verified through queued dispatch; smoke
1319/1319 under both `jit_eval_brackets` states; oracle 9/9; jit_diff
400/0).

## Observations recorded, no action taken

- `mudconf.safer_iter` gates whether `@dolist` ships `iter_token` at
  all (`walkdb.cpp:26`) — with it on, queued entries carry no `##`
  context by design.
- **muxscript's EOF drain is paced and time-capped (~15s)**: a 20k
  fan-out executed only ~67 entries before exit, silently dropping the
  rest. Harmless for the smoke suite (its completeness gate counts
  dispatches), but queue *throughput* cannot be measured through
  muxscript — a live-netmux stress harness is the right vehicle, and a
  natural next hardening step.
- `setup_que` uses `static` locals (`nCommand`, `nLenEnv[]`) as scratch
  between the two passes — fine single-threaded (the engine is), worth
  remembering if threading assumptions ever change.
- Quota/pay checks run before any allocation, so a rejected enqueue
  allocates nothing.
