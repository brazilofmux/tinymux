# Tiered Storage: RAM Cache + libmdbx + SQLite

## Status

Study document progressing toward an implementation decision.

This document is not a commitment to add libmdbx. Its purpose is to
decide whether TinyMUX should:

- make the RAM attribute cache much larger and more configurable
- treat attribute persistence as a KV problem separate from SQLite's
  relational role
- keep SQLite for the workloads it already fits well

The document distinguishes:

- what exists today
- what change is being proposed
- what must be proved before rollout
- where it is reasonable to stop

## Why Study This

The current unified SQLite design is coherent and maintainable, but it
serves two different classes of workload:

1. The hot path for individual attribute reads and writes keyed by
   `(dbref, attrnum)`.
2. Relational and structured workloads such as `@search`, mail,
   comsys, connlog, metadata, and code cache.

Those workloads want different things.

The attribute path is latency-sensitive and mostly KV-shaped. SQLite can
serve it, but it brings pager, row-format, and general transactional
machinery designed for a broader problem set. SQLite is still an
excellent fit for indexed search, joins, reporting, and durable
structured tables.

Three observations motivate the study:

1. **Tier 1 is likely worth improving regardless.** A configurable cache
   can cover much of what old `MEMORY_BASED` builds were trying to
   achieve, without a separate compile-time mode.
2. **A KV backend may be a better fit for live attributes.** libmdbx is
   a plausible candidate, but the gain needs to be measured in TinyMUX,
   not assumed from generic database benchmarks.
3. **SQLite remains valuable even if attributes move.** Most structured
   subsystems and much of the existing operational tooling still belong
   there.

The core question is not whether libmdbx can be faster in isolation.
The core question is whether the performance gain is large enough to
justify the extra code, migration, testing, and operational complexity.

## Current Architecture

The codebase already has the right abstraction boundary:

- `attrcache.cpp` is the hot in-process attribute cache.
- [`mux/include/storage_backend.h`](/home/sdennis/tinymux/mux/include/storage_backend.h)
  defines the durable attribute backend contract.
- SQLite is currently the authoritative durable store for both object
  metadata and attributes.
- `db[]` remains the in-memory object table used by the game loop.

Current properties that matter to this study:

- The attribute cache sits above the durable backend.
- Attribute writes are currently write-through.
- Object metadata updates such as `s_Location()` and `s_Flags()` also
  write through to SQLite.
- SQLite also owns mail, comsys, connlog, metadata, attr names, and
  code cache.
- `cache_preload()` already bulk-loads builtin attributes on connect and
  movement.
- `@dump` currently means SQLite checkpointing, not flatfile export.

The important implication is this: any redesign should extend the
existing backend seam. It should not add a second attribute access path
with subtly different semantics.

## Proposed Architecture

```
┌─────────────────────────────────────────────────┐
│              Tier 1: Hot Cache (RAM)            │
│  db[] array (object metadata, always resident)  │
│  Attribute cache (configurable, preload-aware)  │
└──────────────┬──────────────────────────────────┘
               │ reads miss / writes pass through
┌──────────────▼──────────────────────────────────┐
│      Tier 2: Durable Attribute Store            │
│  SQLite today; libmdbx candidate                │
│  One authoritative store for live attributes    │
└──────────────┬──────────────────────────────────┘
               │ search/tooling integration
┌──────────────▼──────────────────────────────────┐
│          Tier 3: SQLite (always present)        │
│  Objects, mail, comsys, connlog, metadata,      │
│  attr names, code cache, import/export          │
└─────────────────────────────────────────────────┘
```

### Tier 1: Hot Cache

This is the least risky part of the design and the most likely to pay
for itself even if no further stages ship.

- `db[]` remains the resident object table.
- The attribute cache remains the first lookup point.
- Cache size becomes an explicit operational knob.
- Preload policy becomes more aggressive and measurable.

With an unlimited cache setting, this becomes the runtime analogue of
the old `MEMORY_BASED` behavior.

### Tier 2: Durable Attribute Store

The study case for libmdbx is narrow:

- attributes only
- no object metadata in v1
- no mail/comsys/connlog
- no attempt to replace SQLite wholesale

If libmdbx is adopted, it should become the authoritative durable store
for live attribute values and per-attribute metadata such as owner,
flags, and `mod_count`.

The case for libmdbx is not "new technology." It is that a local,
transactional KV store with mmap-based reads may fit TinyMUX's dominant
attribute access pattern better than SQLite does.

### Tier 3: SQLite

SQLite remains first-class:

- object persistence
- relational search support
- mail, comsys, connlog
- metadata and attr-name registry
- code cache
- import/export and offline tooling

SQLite should not be described as a backup format. Even in the libmdbx
variant, it remains a primary subsystem store.

## Scope Recommendation

The earlier broad version of this idea tried to do too much at once:

- move attributes into libmdbx
- duplicate object metadata into libmdbx
- keep SQLite synchronized asynchronously
- preserve SQLite-oriented search and tooling unchanged

That is too much surface area for a first implementation.

The stronger v1 scope is:

1. Improve Tier 1 first.
2. Keep `db[]` + SQLite object persistence unchanged.
3. If needed, add libmdbx as the durable backend for attributes only.
4. Keep SQLite authoritative for every non-attribute subsystem.
5. Resolve `@search` semantics explicitly rather than hiding them behind
   a mirror.

This version reduces risk because it avoids inventing a second durable
home for object metadata, and it makes rollback conceptually possible.

## Data Placement

| Data | RAM | libmdbx candidate | SQLite |
|---|:---:|:---:|:---:|
| `db[]` object metadata | Yes | No in v1 | Yes |
| Attribute cache entries | Yes | No | No |
| Durable attribute values | No | Yes if enabled | Yes otherwise |
| Attribute names registry | In-memory copy | No in v1 | Yes |
| Mail/comsys/connlog | No | No | Yes |
| Metadata (`db_top`, `attr_next`, etc.) | In-memory copy | No in v1 | Yes |
| Code cache | Optional in-memory cache | No | Yes |

The key deliberate choice is not to duplicate the object table into
libmdbx. That duplication adds correctness and migration burden without
improving the current hot path, because object metadata is already
resident in `db[]`.

## Data Flow

### Attribute Read

```
cache_get(dbref, attrnum)
  │
  ├─ Tier 1 cache hit
  │    → return cached value
  │
  └─ Tier 1 cache miss
       │
       ├─ Read from active durable backend
       ├─ Promote into Tier 1
       └─ Return value
```

The cache layer should not materially care whether the active durable
backend is SQLite or libmdbx.

### Attribute Write

```
cache_put(dbref, attrnum, value, owner, flags)
  │
  ├─ Update Tier 1 cache
  └─ Write active durable backend
```

The important rule is semantic, not mechanical: a successful write must
have a clear durability meaning. If libmdbx uses batching internally,
that is acceptable only if the behavior change is explicitly documented
and considered acceptable.

### `@dump`

`@dump` becomes a design checkpoint question, not just an implementation
detail:

- If only one authoritative attribute store exists, `@dump` should make
  that store durable and checkpoint SQLite.
- If a mirror exists, `@dump` also needs to define whether it forces
  logical convergence between stores.

This behavior must be explicit. It cannot be left implicit in backend
internals.

### Object Create / Destroy

Object lifecycle stays in `db[]` + SQLite. It is outside the v1 libmdbx
experiment.

## On-Disk Format

If a new KV format is introduced, it should be explicit and portable:

- fixed-width integer fields
- defined byte order, preferably little-endian
- explicit format versioning
- no dependence on host ABI or struct packing

Proposed minimal KV shape:

```text
Database: attrs
Key:      uint32_le dbref + uint32_le attrnum
Value:    uint32_le owner
          uint32_le flags
          uint32_le mod_count
          uint8_t[] value
```

The exact physical layout may change, but "native-endian struct dump" is
not an acceptable format.

## The `@search` Problem

This is the hardest design question.

A KV backend is a good fit for point lookups. It is not automatically a
good fit for relational or content-search workloads. Before libmdbx is
considered production-ready, the project needs a position on the
following:

1. Must `@search` see exact current attribute state?
2. May `@search` use a lagging derived index or mirror?
3. Which `@search` predicates actually need attribute values, and which
   can remain object-table queries?

This choice drives the whole design more than the storage API does.

### Option A: One Authoritative Attribute Store

When libmdbx is enabled, it is the only live attribute store. SQLite no
longer stores the active attribute table.

Consequences:

- one source of truth for attributes
- no mirror lag
- no reconciliation step after crash
- `@search` paths that need attributes must read from the active
  backend or from an index derived from it
- SQLite-oriented tools must be taught to read attribute data from the
  active backend

This is the cleaner design.

### Option B: libmdbx Authoritative, SQLite Mirrored

libmdbx stores live attributes; SQLite stores a periodically refreshed
mirror or derived searchable representation.

Consequences:

- easier transition for existing SQL-based search and tooling
- observable staleness unless sync is forced
- startup and crash recovery require reconciliation logic
- debugging becomes harder because two durable stores are involved

This is best treated as a transition mechanism, not the preferred
steady-state design.

### Recommendation

Treat Option A as the default target. Add Option B only if measurements
show that exact-current search over the active backend is not viable and
the project is willing to accept documented staleness semantics.

## Crash Recovery and Durability

This area needs policy decisions, not just implementation detail.

Questions that must be answered explicitly:

- What does a successful attribute write guarantee?
- If batching is used, how much acknowledged data may be lost on crash?
- What does `@dump` guarantee: durability, cross-store convergence, or
  both?
- If a mirror exists and stores disagree on startup, which store is
  authoritative and why?

libmdbx's copy-on-write model is attractive, but the real issue is not
which database sounds more robust in the abstract. The real issue is
whether TinyMUX's durability contract remains easy to reason about after
adding batching, migration, and possibly mirroring.

### Crash Recovery by Option

**Today (SQLite only):**

1. Start the server.
2. `sqlite3_open()` replays the WAL automatically.
3. No manual steps.

**Option A (libmdbx authoritative, no SQLite attribute table):**

1. Start the server.
2. libmdbx opens and self-recovers (copy-on-write B+tree — last
   committed transaction is always intact, no replay needed).
3. SQLite opens and WAL-recovers.
4. No mirror reconciliation needed (but startup orphan scan runs;
   see below).

This avoids mirror reconciliation, but it does not eliminate cross-store
consistency concerns. Object identity spans both stores: SQLite owns
object existence and metadata, libmdbx owns attributes keyed by those
object ids. After a crash, the following states are possible:

- **Orphaned attributes**: An object was destroyed in SQLite (committed)
  but its attributes survive in libmdbx (the delete was still in the
  write batch). Startup must scan for attributes whose dbref no longer
  exists in the object table and clean them up.
- **Missing initial attributes**: An object was created in SQLite but
  the initial attribute writes had not yet been committed to libmdbx.
  The object exists but has no attributes. This is the same state as a
  crash between `@create` and the first `&attr` — recoverable by the
  game operator, not a data corruption issue.

A startup consistency check (scan libmdbx for dbrefs not in `db[]`,
delete orphans) is required. This is lightweight compared to Option B's
full mirror reconciliation, but it is not zero.

**Option B (libmdbx authoritative, SQLite mirror):**

1. Start the server.
2. libmdbx self-recovers.
3. SQLite WAL-recovers.
4. The mirrored attribute table in SQLite may be behind libmdbx (last
   sync point). Startup must re-sync libmdbx → SQLite before `@search`
   is reliable.

This is materially more complex. The reconciliation step must handle
interrupted syncs, partial writes, and the possibility that the mirror
is arbitrarily stale if the previous shutdown was unclean.

Option A avoids this entire problem class.

## Backup

### Current Design

`./Backup` uses SQLite's `.backup` API (`sqlite3 db ".backup dest"`),
which produces a consistent point-in-time snapshot of the database while
the server is live. No fork(), no @dump, no server pause. The script
then bundles the snapshot with config and text files into a tar.gz.

This is clean and correct.

### Impact of libmdbx

With tiered storage, `./Backup` must snapshot two files:

- `netmux.sqlite` (objects, mail, comsys, connlog, metadata)
- `netmux.mdbx` (attribute data)

The concern is consistency: if the two snapshots are taken at different
times, the backup could contain attributes for objects that have been
destroyed (or objects missing attributes that were set between
snapshots).

**Option A reduces the overlap but does not eliminate the problem.**
SQLite and libmdbx own different data, but object identity spans both
stores. Two independent live snapshots do not produce a single atomic
cut across both stores — they can capture a combination of states that
never existed simultaneously. This is worse than a crash, which gives
one ordered failure point; staggered snapshots can splice together pre-
and post-mutation states from different stores.

Examples of backup inconsistency:

- SQLite snapshot includes a newly created object; libmdbx snapshot was
  taken moments earlier and does not include its attributes.
- libmdbx snapshot includes attributes for an object that was destroyed
  in the SQLite snapshot taken moments later.
- Pending libmdbx write batches (not yet committed) are invisible to
  `mdbx_env_copy2()`, while the corresponding object-table updates may
  already be committed in SQLite.

**Recommended approach:**

1. **Server-assisted backup (recommended).** Add `@backup` or extend
   `@dump` with a backup mode. The server: flushes the pending libmdbx
   write batch, calls `mdbx_env_copy2()`, then `sqlite3_backup_*()`,
   all while the game loop is quiesced between ticks. This produces a
   logically consistent pair of snapshots. This is the only approach
   that provides strong restore semantics.

2. **Live snapshot via library APIs (weaker guarantee).** libmdbx
   provides `mdbx_env_copy2()` for live snapshots; combined with
   `sqlite3 .backup`, both stores can be snapshotted from the
   `./Backup` script without server involvement. However, this only
   captures committed libmdbx state (not pending batches), and the two
   snapshots are not atomic. The resulting backup is crash-equivalent:
   it represents a state the server could have reached via unclean
   shutdown, not necessarily a state that ever existed during normal
   operation. Restore from such a backup requires the same startup
   consistency check described in the crash recovery section (orphan
   scan, missing-attr tolerance).

3. **No fork() reintroduced.** Both `mdbx_env_copy2()` and
   `sqlite3 .backup` are non-forking operations. The design preserves
   the current no-fork architecture.

### Updated Backup Script Shape

```sh
# Phase 1a: Snapshot libmdbx (live-safe)
mdbx_copy "$DATA/$GAMENAME.mdbx" "$BACKUP_MDBX"

# Phase 1b: Snapshot SQLite (live-safe, WAL-aware)
sqlite3 "$DATA/$GAMENAME.sqlite" ".backup '$BACKUP_SQLITE'"

# Phase 2: Bundle into tar.gz (same as today)
tar cf - "$BACKUP_SQLITE" "$BACKUP_MDBX" \
    "mux.config" "$GAMENAME.conf" $TEXT_FILES \
    | gzip -9 > "$BACKUP_TAR"
```

`mdbx_copy` is a utility that ships with libmdbx, analogous to
`sqlite3 .backup`. Both produce consistent, self-contained copies
suitable for cold-start restore.

### Restore Procedure

1. Stop the server.
2. Replace `netmux.sqlite` and `netmux.mdbx` from the backup.
3. Remove any stale `-wal`, `-shm`, or `-lck` files.
4. Start the server.
5. Startup consistency check runs automatically: scan libmdbx for
   attribute dbrefs not present in `db[]`, delete orphans.

If the backup was produced by a server-assisted snapshot, the
consistency check should find nothing. If it was produced by
independent live snapshots, it may clean up a small number of orphaned
attributes or log objects with missing initial attributes.

## Configuration

Candidate configuration parameters:

```text
cache_max_size 256M
  Tier 1 attribute cache size
  -1 = unlimited
  Accepts K/M/G suffixes

cache_preload_depth 2
  0 = current room only
  1 = current room + adjacent exits
  2 = two rooms deep

mdbx_map_size 4G
  Maximum libmdbx map size

mdbx_batch_size 500
  Writes per libmdbx transaction
  0 = commit every write
```

These values are examples, not final defaults. If batching remains in
scope, the exact durability semantics of `mdbx_batch_size` must be
documented alongside the parameter.

## Implementation Stages

Each stage should be independently useful or independently informative.
Stopping after an early stage is allowed.

### Stage 1: Better Tier 1 Cache

Improve the current cache without changing the durable backend.

Stories:

**1a. Configurable cache size.**

- `cache_max_size` in the conf file, parsed with K/M/G suffixes
- `-1` means unlimited (never evict)
- default changes from 1 MB to 256 MB
- `@list cache` shows current size, max size, hit rate, miss rate
- smoke tests pass at both `cache_max_size 1M` and `cache_max_size -1`

**1b. Better preload behavior.**

- `cache_preload_depth` parameter
- preload visible nearby attrs, not just builtin attrs
- queue preload work rather than doing it inline on movement

**1c. Baseline benchmarks.**

- measure hit/miss behavior before backend work
- log results so later stages have a comparison baseline

This stage is worth shipping even if all later stages are rejected.

### Stage 2: Backend Interface Audit (COMPLETE)

Validate that the current abstraction seam is sufficient.

Stories:

**2a. Audit `IStorageBackend`.** (COMPLETE)

- The abstract interface has no SQLite-specific assumptions.
- Two gaps were found and filled:
  - `Count(object)` — attribute count, was bypassing the interface.
  - `GetModCount(object, attrnum)` and `GetAllModCounts(object, cb)` —
    JIT mod_count access, was bypassing the interface via `GetDB()`.
- All attribute access now goes through `IStorageBackend`.  Remaining
  `GetDB()` calls are for non-attribute subsystems (object metadata,
  mail, comsys, connlog, vattr names, code cache, search indexes) that
  stay in SQLite per the design.

**2b. Define per-attribute metadata contract.** (COMPLETE)

- See [`docs/attribute-metadata.md`](attribute-metadata.md).
- Schema: `(object, attrnum) → (value, owner, flags, mod_count)`.
- mod_count is monotonically increasing, used by JIT for staleness.

**2c. Audit `@search` attribute usage.** (COMPLETE)

- `@search` SQL fast-paths query only object metadata (owner, type,
  zone, parent, flags).  No direct queries on the attributes table.
- `eval=` predicates go through `mux_exec()` → `cache_get()` →
  `IStorageBackend`.  No changes needed when backend changes.

### Stage 3: Experimental libmdbx Attribute Backend

Implement libmdbx behind `IStorageBackend`, for attributes only.

Stories:

**3a. Backend implementation.**

- new backend files
- full `IStorageBackend` coverage
- current preload and iteration semantics preserved

Acceptance criteria:

- all attribute get/put/del go through libmdbx when enabled
- `GetAll()` and `GetBuiltin()` preserve current cache and search needs
- crash recovery is understood and tested
- smoke tests pass with libmdbx as the active attribute backend
- `dbconvert` can still import/export (reads active backend for attrs)

**3b. Migration path.**

- bulk-load from SQLite attributes into libmdbx on first enable
- idempotent and restartable
- verify counts and random samples
- preserve rollback options until migration is trusted

**3c. Rollback path.**

- define how the server returns to SQLite-backed attributes
- avoid making rollback depend on undocumented manual repair

### Stage 4: Resolve the Search Story

Decide whether Option A is sufficient or whether a mirror/index is
required.

Stories:

**4a. Benchmark current-state search over the active backend.**

- especially `eval` / `eeval` and any other attribute-heavy predicates

**4b. If needed, add a mirror or searchable derived index.**

- only after Option A is measured
- explicitly document any staleness semantics

### Stage 5: Stress Testing and Validation

Prove the operational behavior.

Stories:

**5a. Write stress test.**

- large object/attribute population
- random read/write mix
- throughput and latency metrics

**5b. Crash recovery test.**

- forced termination during sustained writes
- verify recovery behavior and any bounded-loss claims

**5c. Memory pressure test.**

- small cache against large dataset
- verify bounded memory behavior and graceful degradation

**5d. Longevity test.**

- sustained simulated workload
- watch for leaks, growth, and behavioral drift

Acceptance criteria:

- results logged to file
- same harness can compare sqlite-only vs libmdbx-backed runs

### Stage 6: Rollout or Rejection

Promote libmdbx only if measured wins justify the added complexity.
Otherwise keep the Tier 1 improvements and stop there.

That is a successful outcome.

## Dependencies

- **libmdbx**: <https://gitflic.ru/project/erthink/libmdbx> (GitHub
  mirror available).  BSD-like license.  Single C source file
  (`mdbx.c` + `mdbx.h`), no external dependencies.  Statically linked.
- **No new runtime services**: no separate daemon or network dependency.
- **OS support**: POSIX mmap (Linux, FreeBSD, macOS).  Windows is not a
  first-stage goal.

## Risks

| Risk | Why it matters | Mitigation |
|---|---|---|
| Two sources of truth | Hardest failure mode to debug. | Prefer Option A; treat Option B as transitional only. |
| Search staleness | Players and admins may see surprising results. | Only allow if documented and operationally acceptable. |
| Durability ambiguity | Batching can weaken the meaning of "successful write". | Define the contract before rollout. |
| Migration complexity | Large games need restartable and verifiable migration. | Keep migration one-way, explicit, and checkable. |
| Maintenance burden | A second engine increases testing and tooling cost. | Share tests through `IStorageBackend` and reject if wins are too small. |
| mmap/map-size issues | Capacity or memory pressure can cause new failures. | Bound configuration, alert early, test under pressure. |

## Non-Goals

- replacing SQLite everywhere
- duplicating object metadata into libmdbx in v1
- rewriting mail/comsys/connlog around a KV store
- distributed storage
- making Windows support a gate for the study

## Bottom Line

The strongest version of this idea is not "replace SQLite with a
three-tier stack." It is:

- make Tier 1 materially better
- isolate the attribute-storage question from the rest of the database
- keep SQLite for the workloads it already fits
- add libmdbx only if benchmarks and semantics justify it
- accept that stopping after Stage 1 is a valid result

If this proceeds beyond Stage 1, the first design question is not the
libmdbx API. It is the source-of-truth rule for attributes and the
correctness story for `@search`.
