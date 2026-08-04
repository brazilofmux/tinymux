# Survey: performance pass (2026-08)

Performance (not correctness) pass over the *whole server*, prompted by
"we did a lot of JIT/DBT work — did it buy anything?". Companion to
`docs/survey-jit-perf.md`, which covers the softcode eval path alone; this one
covers the paths that only a live netmux reaches, and it reaches a different
conclusion about where the time goes.

**Headline: for a live game, none of the top costs are in the JIT.** The JIT is
2.4–9.5× faster than the interpreter on the expressions it compiles, and
compilation amortises to nothing in a running game. What a live game actually
spends its time on, in order, is `$`-command dispatch, name resolution, and
(until #2054) debug logging.

## Method

Two workloads, deliberately kept apart. **A single blended percentage answers a
question nobody asked**, and the two disagree about almost everything.

- **The suite** (`testcases/tools/PerfSmoke`) — `muxscript` running the smoke
  battery under `perf record`. No network, no descriptors, no DB writes, no
  dump. It is a fine *compiler* benchmark and a misleading *game* benchmark:
  1605 expressions each compiled once and run once, which is the opposite of
  what a game does.
- **The live server** (`tests/profile/`, added this pass) — a throwaway netmux
  on a kernel-assigned free port, driven through phases that each do one kind
  of work, with `perf` attached and per-phase CPU read exactly from
  `/proc/<pid>/stat`.

Ad-hoc probes for specific questions used `rvbench(<expr>,<iters>)` and
`jitstats()`, and a `LD_PRELOAD` interposer for call attribution when perf's
unwinder could not reach the caller.

## ⚠️ Methodology gotchas (each cost real time)

1. **`perf record -p $!` attaches to the wrong process.** `( cd X && cmd ) &`
   leaves the subshell as `cmd`'s parent, and perf's inherit only follows
   children created *after* the attach. The result is a `perf.data` with a
   valid header and **zero samples**, which slices into "no samples in window"
   for every phase — indistinguishable from an idle server. Resolve the real
   pid, and fail loudly on an empty recording rather than reporting emptiness
   as a result.
2. **Kernel time cannot be sampled on most of the fleet.** `kptr_restrict`
   blocks `/proc/kallsyms`, so kernel samples land in one unsymbolised
   `[unknown]` bucket — 72–100% of every phase, which drowns everything
   actionable. Sample `task-clock:u` and count kernel time exactly from
   `/proc/<pid>/stat` instead.
3. **`@dump` forks.** `do_dump()` → `fork_and_dump()` returns as soon as the
   child exists, so a dump phase times a `fork` (0.016s regardless of database
   size). Set `fork_dump no` to measure the work.
4. **Bursts measure buffering, not command cost.** 20 000 commands sent without
   draining wedges where 1 000 worked: replies back up faster than an
   opportunistic drain clears them. Send a sentinel per chunk.
5. **Attribute writes need the object resolvable *by name*.** `&CMD0 obj=...`
   fails silently-ish if `obj` was already `@tel`'d to the master room and the
   writer is elsewhere. Two rounds of `$`-command measurement were invalid this
   way, and the failure looked like a *result* ("patterns are free!"). Write by
   dbref, verify by reading back, and **always run a positive control** that
   the mechanism under test actually fires.
6. **A positive control that does not read the socket is not a control.**
   `time.sleep()` then checking a buffer nothing filled reports "never fired"
   for a mechanism that fired fine.
7. **Wall-clock throughput saturates before the server does.** With the debug
   logging gone, every phase in `tests/profile/` lands at ~6000 cmd/s — the
   *same* rate — which means the driver is the limit. The `usr=`/`sys=` columns
   keep discriminating after the rates stop.
8. **A stale `libmux.so` on the library path silently replaces the build.**
   `-Wl,-rpath` emits RUNPATH, which the loader searches *after*
   `LD_LIBRARY_PATH` (unlike RPATH). See #2055.

## The live server: where a game's time goes

Per-command server CPU (user+sys), `tests/profile/`, 1500 objects,
Linux/x86-64, `--enable-jit --enable-stubslave`:

| phase | before #2054 | after #2054 |
|---|---:|---:|
| `dispatch` (`look`) | 256 µs | **30 µs** |
| `softcode_arith` | 244 µs | **25 µs** |
| `softcode_list` (`iter(lnum(50))`) | — | 82 µs |
| `attrwrite` | 311 µs | **142 µs** |
| `attrread` | 338 µs | **152 µs** |

The GANL debug logging (#2049) was a **flat tax that hid the shape**: before it
was removed every phase sat inside a 1.4× band and looked alike. Afterwards the
spread is 6×.

### 1. `$`-command dispatch — the largest cost in the server

Cost of a typed command that matches nothing (the common case, and the worst
case, since a miss tests every pattern), against objects in the master room —
which is in scope for every player:

| config | µs/cmd | over baseline | per object |
|---|---:|---:|---:|
| empty master room | 140 | — | — |
| 400 objects, **no** `$`-commands | 425 | +285 | 0.7 |
| 25 objects × 1 pattern | 390 | 250 | 10.0 |
| 50 × 1 | 660 | 520 | 10.4 |
| 100 × 1 | 1 380 | 1 240 | 12.4 |
| 200 × 1 | **3 380** | 3 240 | 16.2 |
| 400 × 1 | **9 440** | 9 305 | 23.3 |

It is the **patterns**, not the objects: 400 objects carrying no `$`-commands
cost +285 µs; the same 400 carrying one each cost +9 305 µs, 32× more. And it
is superlinear — per-object cost climbs 10.0 → 23.3 µs as the population grows,
roughly O(n^1.4).

Two hundred global commands make **every line every player types** cost 3.4 ms
of single-threaded server time. A `look` is 30 µs.

#### Root cause: the attribute cache is scanned in full, per object

`perf`, 15s of steady-state misses at 200 objects × 1 pattern:

| | share of user time |
|---|---:|
| `cache_collect_pending_attrnums` | **58.8%** |
| (`hashtable_policy.h` beneath it) | 43.3% |
| `sqlite3.c` | 16.3% |
| `pthread_mutex_lock`/`unlock` | 6.6% |

`cache_collect_pending_attrnums` (`attrcache.cpp:307`) does this:

```cpp
for (const auto &kv : mudstate.attribute_lru_cache_map)
{
    if (kv.first.object != static_cast<unsigned int>(thing)) continue;
    ...
}
```

It **iterates the entire attribute LRU cache** — every cached attribute of
every object in the game — and filters for the one object it was asked about.
Then it does the same over the whole write queue. Its caller,
`collect_attrnums_from_storage` (`db.cpp:1901`), runs once per object whose
attribute list is needed, which for `$`-matching is once per object in scope,
per typed command.

So the per-command cost is *(objects in scope)* × *O(total cached attributes)*,
which is exactly the observed superlinearity: touching more objects grows the
cache, so each scan costs more *and* there are more scans.

**This inverts the obvious fix.** Enlarging the attribute cache makes
`$`-dispatch **slower**, because the scan is over the whole cache. The fix
shape is an index — a secondary map from object → cached attrnums, or per-object
dirty lists — turning O(cache) into O(attrs on this object).

It also means a combined pattern automaton (a DFA over all `$`-patterns) would
optimise a term that is not the bottleneck: at 16–23 µs per pattern tested, the
wildcard match itself is a rounding error next to the cache scan.

### 2. Name resolution — ~66% of the attribute path

`attrread`, by symbol: `string_match` 23.4%, `string_compare` 20.8%,
`match_list` 18.5%, `PureName` 3.7% — **66.4%**. `sqlite3.c` + `db.cpp`: 8.0%.

`match_list` (`match.cpp:354`) walks a contents list comparing names one at a
time. Measured slope: **~59 µs fixed + ~62 ns per object in scope**, per lookup;
control phases that resolve no names stay flat across a 4× change in object
count. `#123` short-circuits at `CON_DBREF` and pays none of it. Tracked on
#2058.

### 3. Storage is not the big number, and the SQLite backend is why

`sqlite3.c` is 4–8% of user time even in the attribute phases, and `@dump`
measured *inline* is 0.027s against 18 000 attributes — writes are already
persisted by the time it runs. The historical "the database is the big number"
was about the periodic flatfile dump, which no longer exists in that form.

**But the persisted `code_cache` buys nothing** (#2061): it is written, looked
up, and hit — and a warm run costs the same wall time and produces the same
profile as a cold one. Mechanism not established; the two candidates are that
hits are a small share of total compilations, or that
`reconstruct_from_cache()` costs what compiling costs.

## The suite: a compiler benchmark

27s (was 33.6s before #2053). By DSO: `engine.so` 51.2%, `libc` 35.8%,
**JIT-generated code 5.5%**, `libmux` 4.8%.

| | share |
|---|---:|
| DBT (`dbt_x64_sysv`, `dbt.cpp`, `dbt_emit_x64.h`, `dbt_decoder.h`) | 21.5% |
| **`tier2_install` memcpy** — largest single line item | **13.4%** |
| HIR (`hir.h`, `hir_opt`, `hir_codegen`, `hir_ssa`, `hir_lower`) | 14.4% |
| `memset` from `hir_codegen` + `allocate_output_buffers` | 6.1% |
| **≈ compilation** | **≈ 57%** |
| **executing the compiled result** | **5.5%** |

`tier2_install` copies the whole 138 KB blob image into guest memory on *every*
`compile_expression()`. It is the biggest number here and worth **approximately
nothing to a live game**, which compiles each expression once — its value is
developer cycle time. Filing suite percentages next to live percentages without
that label is how a 13.4% outranks a 66%.

## Complexity defects found this pass

All three are the same shape — address element *i* by re-walking from the
start, in a loop over every element — and all three were found by attributing a
libc symbol, not by reading code.

| | status | worst case |
|---|---|---|
| `scramble()` (#2045) | fixed, #2053 | 19.3s → 0.145s |
| `shuffle()` (#2057) | open | ~3.4s at LBUF |
| `iter()` (#2052) | open | O(n²) in element count |

`iter()` is the interesting one: it is quadratic on **both** the interpreter and
the JIT, so the JIT's advantage decays from 2.6× at 10 elements to 1.006× at
100. A differential test cannot see it — `jit_diff` compares the two routes,
they agree, and they are both quadratic. Only an absolute scaling measurement
finds it. Same lesson as #1319/#1320 one layer up.

## What this pass did not measure

- **2.13 vs 2.14 end to end.** The release-justifying number — *does 2.14 beat
  2.13 on realistic softcode* — is still not measured. `tests/parity213` can
  stand both lines up side by side; nobody has pointed it at speed.
- **A real game's database.** Everything here uses the starter DB plus
  synthesised objects. The `$`-command and name-resolution constants are
  therefore harness-shaped; the *slopes* are not, which is why the slopes are
  what this survey reports.
- **Windows/IOCP and macOS/kqueue.** Linux/epoll only. #1920 tracks the Windows
  side.
