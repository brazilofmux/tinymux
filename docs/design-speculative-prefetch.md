# Speculative Attribute Prefetch

## Status

Not started.

| Stage | Status |
|-------|--------|
| 1. Object-Affinity Prefetch on Miss | Not started |
| 2. Measure and Instrument | Not started |
| 3. Command-Pattern Prefetch | Not started |
| 4. Parent-Chain Prefetch | Not started |
| 5. Static-Analysis Prefetch (JIT) | Not started |

## Why Study This

The current attribute cache has two loading modes:

1. **Reactive**: `cache_get()` loads one attribute at a time on miss.
2. **Spatial preload**: `cache_preload_nearby()` bulk-loads attributes
   for the player and nearby rooms on connect and movement.

The spatial preload is effective for its target: when a player moves
into a room, DESC, exits, and nearby room data are already warm.  But
a large class of attribute access is neither spatial nor predictable
from movement alone:

- `u(obj/attr)` calls evaluate arbitrary attributes on arbitrary
  objects.  The called attribute typically references other attributes
  on the same object via `get(me/...)` or `v(...)`.
- `@trigger obj/attr` reads one attribute, which then reads more.
- `search(eval=...)` iterates all objects and reads attributes on each.
- `look` reads DESC, DESCFORMAT, HTDESC, NAMEFORMAT, ADESC, ODESC on
  the target — up to 6 attributes on the same object.
- Parent-chain inheritance (`atr_pget_str_LEN`) walks parents one by
  one, calling `cache_get()` for each level.

Each cache miss is a separate SQLite round-trip (~3.6 us).  A single
`GetAll` call for an entire object costs ~5.5 us — less than two
individual misses.  If an operation will touch 3+ attributes on the
same object, prefetching the whole object on the first miss is faster
than loading attributes one at a time.

## Current Architecture

```
    softcode function (get, u, hasattr, etc.)
            │
            ▼
    atr_pget_str_LEN()        ← walks parent chain
            │
            ▼
    atr_get_raw_LEN()         ← single (object, attrnum) lookup
            │
            ▼
    cache_get()               ← LRU cache check
        │           │
        ▼           ▼
    cache hit     cache miss → g_pSQLiteBackend->Get()
                                (one attribute, ~3.6 us)
```

Current preload triggers:

- **Player connect** (`engine_com.cpp:4103`): `cache_preload_nearby()`
  loads the player, their location, and BFS neighbors.
- **Player movement** (`move.cpp:203-211`): Synchronous preload of
  destination, then deferred BFS for neighbors.

Nothing else triggers preloading.  Every other attribute access goes
through the reactive single-miss path.

## Proposed Stages

### Stage 1: Object-Affinity Prefetch on Miss

The simplest and highest-value change.

When `cache_get()` misses for `(object, attrnum)`, instead of loading
just that one attribute, call `GetAll(object, ...)` to load every
attribute on the object into the cache.  The rationale:

- If the code reads one attribute on an object, it almost always reads
  more.  `u(obj/attr)` evaluates code that typically references other
  attributes on the same object.
- `GetAll` (~5.5 us) is cheaper than 2 individual `Get` calls
  (~7.2 us).
- The worst case is loading attributes that are never read.  These sit
  in the LRU cache and get evicted normally.  The bounded-cache mode
  (`max_cache_size > 0`) already handles memory pressure.

Implementation:

- In `cache_get()`, when a miss occurs for `(obj, attr)`, call
  `cache_preload_obj(obj, true)` to bulk-load the object.
- Then retry the cache lookup.  It should hit.
- Guard against recursion: if `cache_preload_obj` is already running
  (re-entrant call), fall back to single-attribute load.

Cost: ~5.5 us per first-touch object instead of ~3.6 us per
individual miss.  Break-even at 2 attributes; net win at 3+.

### Stage 2: Measure and Instrument

Before adding more complex prefetch strategies, add instrumentation
to measure what the cache is actually doing:

- **Per-object miss counter**: Track how many distinct attributes are
  read per object per command cycle.  This tells us the average
  attribute fan-out and whether object-affinity prefetch is winning.
- **Prefetch hit rate**: Of attributes loaded by prefetch, how many
  were actually read before eviction?  High rate = good prediction.
  Low rate = wasted memory.
- **Miss-after-prefetch counter**: Attributes that missed even though
  the object was prefetched ��� these are parent-chain misses or
  attributes added after prefetch.

These counters feed into `@list cache` and `cachestats()` so they're
observable at runtime.

### Stage 3: Command-Pattern Prefetch

Certain commands have known attribute access patterns:

| Command | Attributes accessed | Object |
|---------|-------------------|--------|
| `look` | DESC, DESCFORMAT, HTDESC, NAMEFORMAT, ADESC, ODESC | target |
| `examine` | All attributes | target |
| `@trigger obj/attr` | The triggered attr + whatever it references | obj |
| `say`/`pose`/`emit` | SPEECHFORMAT, SPEECHMOD, FILTER | speaker, room |
| `u(obj/attr)` | The called attr + whatever it references | obj |

For `look`, the natural batch point is `show_a_desc()` (`look.cpp:1054`).
Before reading DESC, prefetch the target object.  For `examine`, the
object is already fully loaded since examine reads all attributes.

For `u(obj/attr)`, prefetch the object at `parse_and_get_attrib()`
(`funceval.cpp:117`) before evaluation begins.

This stage is only worth doing if Stage 2 measurements show that
object-affinity prefetch (Stage 1) doesn't already cover these
patterns.  If the first `cache_get()` miss triggers `GetAll`, then
subsequent attribute accesses on the same object are all hits.

### Stage 4: Parent-Chain Prefetch

`atr_pget_str_LEN()` (`db.cpp:2434-2470`) walks the parent chain via
`ITER_PARENTS()`, calling `atr_get_raw_LEN()` at each level.  Each
level is a separate cache lookup, potentially a separate miss.

Two options:

**Option A: Prefetch each parent on first miss.**  When `cache_get()`
misses for a parent object, load all of that parent's attributes.
This is just Stage 1 applied to parent objects — no new code if
Stage 1 is already implemented.

**Option B: Prefetch the entire parent chain upfront.**  At the start
of `atr_pget_str_LEN()`, walk the parent chain and preload each
ancestor.  This front-loads the cost but guarantees no misses during
the walk.

Option A is likely sufficient.  Option B is worth considering only if
Stage 2 data shows parent-chain misses are a significant fraction of
total misses.

### Stage 5: Static-Analysis Prefetch (JIT)

The AST and JIT compilers already know which `get()`, `u()`, and
`xget()` calls an expression makes.  At compile time, the attribute
references are visible in the AST.

Idea: when compiling an expression, emit a prefetch hint for each
object/attribute pair that the expression will reference.  On first
evaluation, the prefetch loads the objects; on subsequent evaluations,
they're already in cache.

This is the most complex stage and has diminishing returns if
Stages 1-4 already cover the common patterns.  Defer unless
measurements show a clear gap.

## What Not to Do

- **mmap pointer passing**: Returning pointers directly into mmap'd
  pages avoids copies but requires managing pointer lifetimes across
  transaction boundaries.  The complexity is not justified given that
  the cache already eliminates most backend reads.

- **Predictive machine learning**: The access patterns are
  deterministic from the softcode.  Heuristic prediction adds
  complexity without the determinism that static analysis provides.

- **Cross-object batching**: Loading attributes from multiple
  unrelated objects in one call requires either a new backend API or
  a compound SQL query.  Object-affinity prefetch (one `GetAll` per
  object) is simpler and handles the common case.

## Dependencies

- The write batching (`cache_flush_writes`) must be flushed before
  any prefetch that calls `GetAll`, to ensure the backend sees all
  queued writes.  This is already handled: `cache_flush_writes()` is
  called before `Sync()`, and `GetAll` reads through the backend
  which sees committed data.

  Actually, with the write queue, a queued-but-unflushed `Put` is
  visible in the LRU cache (the cache is updated immediately in
  `cache_put()`).  A `GetAll` from the backend would NOT include
  unflushed writes.  This means prefetch could load stale data for
  attributes that were just written but not yet flushed.

  **Mitigation**: Call `cache_flush_writes()` before `GetAll` in the
  prefetch path.  Or: skip prefetch for attributes already in the
  cache (the current `cache_preload_obj` already does this — it
  checks `if cache_map.find(nam) != end()` and skips cached entries).
  Since `cache_put()` always updates the cache, recently-written
  attributes are already in the cache and won't be overwritten by
  prefetch.

## Bottom Line

Stage 1 (object-affinity prefetch on miss) is the highest-value
change and the simplest to implement.  It turns every first-touch
cache miss into a bulk load that pre-warms the entire object.  The
cost is ~5.5 us per object instead of ~3.6 us per attribute, which
pays for itself when 2+ attributes are accessed.

Stage 2 (instrumentation) validates whether Stage 1 is sufficient or
whether command-pattern and parent-chain prefetch are needed.

Stages 3-5 are progressively more complex with diminishing returns.
They should only be pursued if measurements show gaps that Stages 1-2
don't cover.
