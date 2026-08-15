# Survey: performance pass (2026-08)

Performance (not correctness) pass over the *whole server*, prompted by
"we did a lot of JIT/DBT work — did it buy anything?". Companion to
`docs/survey-jit-perf.md`, which covers the softcode eval path alone; this one
covers the paths that only a live netmux reaches, and it reaches a different
conclusion about where the time goes.

**Headline: for a live game *as games are written today*, none of the top costs
are in the JIT.** Whatever the JIT is worth, it is worth it on microseconds
while `$`-command dispatch is spending milliseconds. What a live game actually
spends its time on, in order, is `$`-command dispatch, name resolution, and
(until #2054) debug logging.

**That qualifier is load-bearing, and this headline should not be quoted
without it.** The JIT/DBT is not aimed at the database, the network or command
dispatch — it is aimed at large softcode and Lua bodies, which do not exist in
any corpus measured here and largely do not exist in existing games, written as
they were against an interpreter. See "the target workload is not represented
here at all", below, before drawing a conclusion about whether the JIT earns
its keep.

**How much the JIT is worth is not settled here, and an earlier draft of this
survey overstated it.** The "2.4–9.5×" first written above came from
`rvbench`'s `native=` divided by its `cached=`, and `native=` is not the
interpreter — it calls `mux_exec`, which dispatches to the JIT (see the
`iter()` section). Against a real interpreter measurement the same expressions
give 6.1× (`add(1,2)`), 1.26× (`add(rand(100),1)`) and 1.42×
(`bound(rand(100),10,90)`).

Neither ratio answers the question a live game asks, which is `mux_exec` *with*
the JIT against `mux_exec` *without* it — dispatch overhead is paid either way,
and no instrument here reports that. It needs a build configured without
`--enable-jit`. Listed under "what this pass did not measure".

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
9. **To attribute a cost, remove it.** A profile plus a code read gives
   correlation. Bypassing the suspect code behind a throwaway env knob — even
   an incorrect bypass, since it is never going to ship — gives causation, and
   it distinguishes "large constant" from "this is the superlinear term",
   which a profile cannot. Revert the knob and rebuild before measuring
   anything else; an experimental artifact left installed poisons every later
   number.
10. **Two benchmark fields are mislabelled, and one of them reversed a
   conclusion in this very document.** `rvbench`'s `native=` calls `mux_exec`,
   which dispatches to the JIT for any JIT-eligible expression, so it is not
   the interpreter — it beat the interpreter outright on
   `iter(lnum(50),mul(##,2))` (18.9 µs against 31.9 µs), which is the tell.
   And `astbench`'s `jit=` times `jit_eval` even when it bails instantly for
   want of a lowering, so `citer()` reads a flat 2.7 µs at every N — absence,
   not speed — while its `result=` comes from a third interpreter call and
   never notices. Use `astbench`'s `ast=` for the interpreter and `rvbench`'s
   `cached=` for the JIT. `tests/growth/README.md` carries the long form.

## The live server: where a game's time goes

Per-command server CPU (user+sys), `tests/profile/`, 1500 objects,
Linux/x86-64, `--enable-jit --enable-stubslave`:

| phase | before #2054 | after #2054 | **current** |
|---|---:|---:|---:|
| `dispatch` (`look`) | 256 µs | 30 µs | **31 µs** |
| `softcode_arith` | 244 µs | 25 µs | **27 µs** |
| `softcode_list` (`iter(lnum(50))`) | — | 82 µs | **61 µs** |
| `attrwrite` | 311 µs | 142 µs | **143 µs** |
| `attrread` | 338 µs | 152 µs | **160 µs** |

The GANL debug logging (#2049) was a **flat tax that hid the shape**: before it
was removed every phase sat inside a 1.4× band and looked alike. Afterwards the
spread is 6×.

The **current** column is master after #2073, #2076, #2077/#2084 and #2078.
Only `softcode_list` moved (−26%, the `iter()` work); the rest are unchanged
within noise. That is not a disappointment — those merges targeted
`$`-dispatch and `iter`, and `$`-dispatch is not one of these phases. See
below for what it did to that path.

**Note what did *not* move.** #2084 caches the per-object attribute-number
list, and `attrread` did not improve at all, correctly: `get()` fetches one
attribute directly and never enumerates, so that cache serves `$`-matching and
`lattr()` rather than value fetches. The win shows up in the `$`-dispatch
numbers, not here.

**The ranking has therefore inverted.** With storage costs removed from the
attribute path, the two attribute phases are now the most expensive in the
harness by 5×, and both are dominated by name resolution — 66.8% of `attrread`
and 74.8% of `attrwrite` (§2). Storage no longer appears near the top of
either.

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

#### Causation, verified by removing it

A profile plus a code read establishes correlation. To establish causation the
scan was bypassed behind a throwaway env knob — deliberately **incorrect**, since
it drops dirty and tombstoned entries SQLite has not seen, and existing only to
attribute the cost. Same build, same probe, one variable:

| objects | scan active | scan bypassed | saved | µs/object active | µs/object bypassed |
|---:|---:|---:|---:|---:|---:|
| 25 | 380 µs | 290 µs | 24% | 10.0 | **6.8** |
| 50 | 680 µs | 470 µs | 31% | 11.0 | **7.0** |
| 100 | 1380 µs | 810 µs | 41% | 12.5 | **6.9** |
| 200 | **3430 µs** | **1510 µs** | **56%** | 16.5 | **6.9** |

**The result is the last column, not the 56%.** With the scan gone, per-object
cost is *flat* at 6.8–7.0 µs. With it, the cost per object climbs 10.0 → 16.5
and keeps climbing.

`$`-command dispatch is therefore **inherently linear** in objects-in-scope, and
`cache_collect_pending_attrnums` is the *entire* superlinear term — not a large
constant on top of it. That is why cost per object worsened as the master room
grew: the cache grew with it, so each of the n scans got more expensive. Two
compounding factors that are really one bug.

56% is an upper bound on what a correct fix recovers. It should recover nearly
all of it: the loop only ever acts on entries with `dirty` or `tombstone` set,
`dirty` means "pending write in the queue", and the queue flushes at
`WRITE_QUEUE_THRESHOLD = 50`. The set the loop cares about is small and bounded;
the structure it searches is the whole cache. That mismatch is the bug in one
sentence.

#### Two consequences

**This inverts the obvious fix.** Enlarging the attribute cache makes
`$`-dispatch **slower**, because the scan is over the whole cache. The fix
shape is an index — a secondary map from object → pending attrnums — turning
O(cache) into O(pending for this object).

**A combined pattern automaton would optimise the wrong term.** A DFA over all
`$`-patterns addresses the residual ~6.9 µs per object, which is the real
matching work (attribute fetch, permission check, wildcard match). That residual
is worth attacking — it is still 1.4 ms at 200 objects — but it is linear, and
it is not what made the curve bend.

#### The residual was profiled, and it was not the matching either (#2077)

Both halves of `collect_attrnums_from_storage` turned out to be the cost. #2073
fixed the pending-merge half; the storage half then called `GetAll()` on every
`atr_head`, i.e. one SQLite round trip per object in scope per typed command.
Profiling the residual put **`sqlite3.c` at 40.6%**, `fcntl` file locking at
10.5% and mutexes at 15.7% — against `wild()` + `wild_lit_eq()` at **0.48%**
with 200 patterns being tested per command.

So the automaton would optimise half a percent. Fixed by caching the
attribute-number list (#2084):

| objects | before #2073 | after #2073 | after #2084 |
|---:|---:|---:|---:|
| 25 | 380 µs | 270 µs | **90 µs** |
| 100 | 1380 µs | 750 µs | **170 µs** |
| 200 | **3430 µs** | 1580 µs | **290 µs** |
| per object | 16.5 µs | 7.3 µs | **1.1 µs** |

**The DFA idea is retired.** Twice in this pass the "obvious" fix for
`$`-dispatch was named before it was profiled, and twice the profile pointed
somewhere else — first at a whole-cache scan, then at a per-object database
query. Neither was the pattern matching, which has never risen above half a
percent of the path.

### 2. Name resolution — now the largest cost in the attribute path

**Current numbers** (master after #2073/#2076/#2084/#2078), self time by symbol:

| | `attrread` | `attrwrite` |
|---|---:|---:|
| `string_match` | 24.0% | 28.5% |
| `string_compare` | 21.2% | 23.4% |
| `match_list` | 18.5% | 18.9% |
| `PureName` | 3.0% | 4.0% |
| **name resolution** | **66.8%** | **74.8%** |

With storage out of the attribute path, these two phases are the most expensive
in the harness by 5×, and this is what they are made of.

`match_list` (`match.cpp:354`) walks a contents list comparing names one at a
time, and **returns early only on `md.absolute_form`** — a dbref. A name hit at
`CON_COMPLETE` does not stop the loop, so the full list is walked every time.
That is deliberate: ambiguity (two objects sharing a name) cannot be detected by
stopping at the first match.

Consequently position barely matters, which is itself the measurement worth
recording — 1000 objects, same set:

| lookup | µs/cmd | tax over dbref |
|---|---:|---:|
| by dbref (no walk) | 26.7 | — |
| name at head | 53.3 | 26.6 |
| name at middle | 60.0 | 33.3 |
| name at tail | 66.7 | 40.0 |

1.5× across the whole list. An earlier hypothesis here — that the first
measurement of this was best-case because it looked up a name sitting at the
head — was tested and **did not hold**.

There is no cheap early-exit to add; the walk is load-bearing. An index is the
only fix, and ambiguity does not block it (name → `vector<dbref>` is ambiguous
iff the list holds more than one), but the invalidation surface is
create/destroy/move/rename. Tracked on #2058.

### 2. Name resolution — ~66% of the attribute path

`attrread`, by symbol: `string_match` 23.4%, `string_compare` 20.8%,
`match_list` 18.5%, `PureName` 3.7% — **66.4%**. `sqlite3.c` + `db.cpp`: 8.0%.

`match_list` (`match.cpp:354`) walks a contents list comparing names one at a
time. Measured slope: **~59 µs fixed + ~62 ns per object in scope**, per lookup;
control phases that resolve no names stay flat across a 4× change in object
count. `#123` short-circuits at `CON_DBREF` and pays none of it. Tracked
on #2058.

### 3. Storage is not the big number, and the SQLite backend is why

`sqlite3.c` is 4–8% of user time even in the attribute phases, and `@dump`
measured *inline* is 0.027s against 18 000 attributes — writes are already
persisted by the time it runs. The historical "the database is the big number"
was about the periodic flatfile dump, which no longer exists in that form.

That claim is narrower than it was when first written. Storage *was* invisible
in the attribute path because the value cache absorbed it — but the attribute
**list** was going to SQLite on every enumeration, which is what made
`$`-dispatch 40.6% `sqlite3.c` until #2084. "Storage is cheap" held for
`cache_get`; it did not hold for `atr_head`.

**And the persisted `code_cache` buys nothing** (#2061): it is written, looked
up, and hit — and a warm run costs the same wall time and produces the same
profile as a cold one. The *correctness* half of that issue is fixed by #2079:
`blob_hash`'s tier 1 leg was a single `__DATE__`/`__TIME__` in
`jit_compiler.cpp`, so under incremental `make` a codegen change in any other
translation unit left the key unmoved and the cache served the previous build's
output. Verified by measurement — on master, touching `hir_lower.cpp` and
rebuilding left `blob_hash` identical. The *performance* half remains open:
mechanism not established, the candidates being that hits are a small share of
total compilations, or that `reconstruct_from_cache()` costs what compiling
costs.

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

`iter()` is the interesting one, and this survey originally got it wrong in a
way worth preserving, because the mistake is reusable.

**Only the JIT is quadratic.** Measured with the growth harness (#2059),
Darwin/arm64, fitted exponent over N=500..4000:

| route | instrument | b | per doubling |
|---|---|---:|---|
| interpreter | `astbench` `ast=` | **0.99** | 2.06× 1.96× 1.96× |
| JIT | `rvbench` `cached=` | **1.99** | 3.96× 3.95× 4.01× |

`fun_iter` advances a real cursor with `split_token` and is linear. The JIT's
lowering calls `EXTRACT(list, i, 1, delim)` per element — a fresh walk from the
head — because the cursor-based `SPLIT_TOKEN` fast path `hir_lower.cpp:2310`
tests for was never registered in `s_tier2_map` and has never executed. At
N=4000 the JIT is 134× *slower* than the interpreter it exists to beat.

**How "both routes" happened: `rvbench`'s `native=` is not the interpreter.**
It calls `mux_exec`, which dispatches to the JIT for any JIT-eligible
expression — and `iter()` is eligible. So `native=` and `cached=` were two
measurements of the JIT, differing by `mux_exec`'s per-call dispatch overhead
(`strlen`, AST-cache lookup, the `jit_can_handle` tree walk). They converge at
large N because they are the same route, and the "flat 26–35 µs saving" that
looked like a decaying JIT benefit is that dispatch overhead, which does not
depend on N.

The tell, once you look for it:

| expr | interpreter (`ast=`) | `native=` | `cached=` |
|---|---:|---:|---:|
| `add(1,2)` | 110 ns | 452 ns | 18 ns |
| `iter(lnum(50),mul(##,2))` | 31 880 ns | **18 885 ns** | 18 746 ns |

A leg labelled "native `mux_exec`" that runs *faster than the interpreter* is
running compiled code.

**So the differential-testing lesson is the opposite of what was written here.**
The routes do not agree — they differ by a whole complexity class, which a
differential *scaling* test would find immediately. What `jit_diff` cannot see
is anything about cost at all: it compares outputs, and both routes produce the
same list. The #1319/#1320 parallel does not apply; that was a fault genuinely
shared by two implementations. Use `astbench`'s `ast=` for the interpreter and
`rvbench`'s `cached=` for the JIT, and see `tests/growth/README.md` for the two
benchmark fields that lie and why.

## 2.13 vs 2.14, end to end

Both lines driven by `tests/profile/` — which works cross-version precisely
because it measures from *outside* the server: commands over a socket, CPU from
`/proc/<pid>/stat`, no dependency on `rvbench()`, `benchmark()` or `jitstats()`,
none of which exist in 2.13. `run.sh --bin <dir>` points it at any build.

Same box, same harness, 375 objects, two runs each, **both lines
logging-gated**:

| phase | 2.13 | 2.14 | 2.14 is |
|---|---|---|---:|
| `create` | 507 / 507 µs | 373 / 427 µs | 1.27× |
| `attrwrite` | 227 / 211 µs | 71 / 69 µs | **3.13×** |
| `attrread` | 142 / 138 µs | 80 / 84 µs | 1.71× |
| `dispatch` (`look`) | 100 / 92 µs | 34 / 42 µs | 2.53× |
| `softcode_arith` | 30 / 26 µs | 28 / 28 µs | **1.00×** |
| `softcode_list` | 90 / 95 µs | 65 / 55 µs | 1.54× |

**2.14 is faster on every phase, 1.3–3.1×**, and the gains are concentrated in
storage and the command path rather than in softcode evaluation.

### Equalising the logging first, which was not optional

2.13 has the identical ungated `#ifndef NDEBUG` gate in the same four GANL
files — 375 sites (#2089, backported as PR #2090). Before equalising, a
scale-0.25 run wrote **158,660** engine debug lines on 2.13 against **14** on
2.14. Comparing the lines as they ship would have reported 2.14 as 2–3× faster
for a reason having nothing to do with any 2.14 work. The table above is with
the gate applied to both.

### Do not read `softcode_arith` as a verdict on the JIT

That row is dead level, and the obvious reading is wrong. The phase evaluates
`add(mul(strlen(name(me)),7),3)` **once per command**, and 2.14's per-command
floor is 34–42 µs (`dispatch`). A few microseconds of arithmetic sit on top of
~28 µs of command overhead, so a 10× win on the expression would move the row
by single-digit percent — inside the run-to-run spread. **The phase cannot
resolve the JIT's contribution in either direction.**

`softcode_list` carries 50 evaluations per command and shows 1.54×, which is
the direction the instrument can actually see.

### And the target workload is not represented here at all

Worth stating plainly, because every JIT number in this document invites the
wrong conclusion without it. **The JIT/DBT is not intended to make the
database, the network or command dispatch faster** — those are what this
survey mostly measures, and they are where 2.14's wins came from.

It is intended to make *large chunks of Lua and softcode* fast to the point of
being nearly free. **Code of that shape does not exist yet** — not in the smoke
corpus, not in `tests/profile/`, not in the starter database, and largely not
in existing games, which were written against an interpreter and are shaped by
what was affordable under one. It is a build-it-and-they-will-come move, and
measuring it against workloads written for the old cost model measures the old
cost model.

So: #2068's "net loss" and the flat `softcode_arith` row are statements about
*today's* corpora, not verdicts on the design. If the JIT ever demonstrably
fails to earn its keep on the workload it was built for, that is worth an issue
and a removal plan — but nothing in this survey is that measurement, and the
optimisation work on it has barely started (#2066, #2074, #2080, #2086 are all
open and all found in the last day).

## What this pass did not measure

- **What the JIT is actually worth in a live game.** The honest comparison is
  `mux_exec` with the JIT against `mux_exec` without it, on the same
  expressions. Every instrument here reports something else: `rvbench`'s
  `cached=` skips `mux_exec` entirely, its `native=` dispatches *to* the JIT,
  and `astbench`'s `ast=` calls `ast_eval_node` directly. Answering it needs a
  tree configured without `--enable-jit`, which is a `make clean` and a rebuild,
  not a new harness.
- **What the JIT is worth on the workload it was built for.** See above: large
  softcode and Lua bodies do not exist in any corpus here, so nothing in this
  document measures the case the JIT exists to serve. Measuring that needs the
  workload to be written first.
- **A real game's database.** Everything here uses the starter DB plus
  synthesised objects. The `$`-command and name-resolution constants are
  therefore harness-shaped; the *slopes* are not, which is why the slopes are
  what this survey reports.
- **Windows/IOCP and macOS/kqueue.** Linux/epoll only. #1920 tracks the Windows
  side.
