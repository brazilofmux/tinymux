# Routing Function Design

## Inspiration: FPS NPC Navigation (late 1990s)

In a late-1990s first-person shooter, level designers placed invisible navigational nodes
throughout each level. At level load, Dijkstra's algorithm pre-computed a
routing table for every node pair. The table stored only the **next hop** --
not the full path -- answering the question: "To get from node A to node B,
which neighbor of A should I visit next?"

Six tables were maintained: three hull sizes (different NPC collision
geometries) times two door-opening capabilities (can/cannot open doors).

Three compression techniques reduced a 1,000-node level from ~95MB
uncompressed to approximately 35KB:

1. **Diagonal elimination** -- source == destination requires no entry.
2. **Direct visibility** -- if two nodes have line-of-sight, no intermediate
   routing is needed; mark the entry as "direct."
3. **Row redundancy** -- bottleneck nodes where the next hop is the same
   regardless of destination collapse to a single "always(X)" annotation.

Runtime queries were O(1): look up `route[current][destination]`, get the
next node, walk toward it, repeat.

## Mapping to TinyMUX

| FPS engine             | TinyMUX                                |
|------------------------|----------------------------------------|
| Navigational nodes     | Rooms (or a marked subset)             |
| Edges between nodes    | Exits (TYPE_EXIT)                      |
| Hull sizes             | Player capability classes               |
| Door-opening           | Lock-passing equivalence classes        |
| Static level geometry  | Dynamic -- must handle invalidation     |
| Pre-computed at load   | Lazily computed, incrementally maintained|

### Navigational Nodes vs. All Rooms

The FPS engine did not make every point in space a node. Level designers chose
where to place them -- enough for coverage, few enough for efficiency.

TinyMUX can do the same. Not every room needs to participate in routing.
Options include:

- **Flag-based**: A `NAVIGABLE` flag or attribute on rooms that should
  participate. Builders mark the grid rooms; private rooms, OOC areas, and
  build spaces are excluded.
- **Zone-based**: All rooms in certain zones are navigable. Routing tables
  are computed per-zone with cross-zone bridge entries at zone boundaries.
- **Automatic**: Rooms reachable from a designated "root" room via unlocked
  exits are navigable.

The route does not need to be perfectly optimal. An approximate route through
navigational nodes is useful even if it misses shortcuts through unmarked
rooms.

## Routing Table Structure

```
route[source_room][dest_room] = exit_dbref
```

The table answers: "Standing in source_room, which exit should I take to
move toward dest_room?" Only the next hop is stored.

### Compression

MUSH topologies are extremely sparse. Most rooms have 2-6 exits. The same
three compression techniques from the FPS approach apply, and MUSH topology is
arguably more compressible:

1. **Diagonal** -- trivial; already there.
2. **Adjacent** -- exit destination == target room; store "direct."
3. **Row redundancy** -- hallways, dead-ends, and bottleneck rooms where
   every destination funnels through the same exit. MUSHes are full of these
   (corridors, lobbies, grid chokepoints).

Expected size: a 1,000-room zone should compress to well under 35KB. Most
MUSH grids are far more regular than FPS levels.

## Lock-Aware Routing Tiers

### Tier 1: Unconditional Routing

Ignore all locks. Assume every exit is traversable. This is the direct
analog of the FPS approach and covers the common case: public grid
navigation.

- Simple to compute (standard Dijkstra/BFS on the static graph).
- Simple to cache (one table per zone or per game).
- Useful for: admin tools, NPC movement, mapper clients, zone builders.

**Important**: Unconditional routing is an infrastructure primitive, not a
player-facing feature. It can return exits the executor cannot actually
use. Player-facing code (e.g., "how do I get to the tavern?") should use
Tier 2 or, at minimum, validate the returned exit before presenting it.

### Tier 2: Lock-Class Routing

For lock-aware routing, evaluate exit locks and group players into
equivalence classes based on which exits they can traverse.

In practice, most exits are unlocked, and locked exits use a small number
of patterns:

- Faction membership (`hasflag(me, FACTION_X)`)
- Builder/admin access (`hasflag(me, WIZARD)`)
- Key possession (`carries(#123)`)

**However**: unlike the FPS hull sizes, which are immutable properties
of an NPC class, every one of these MUX conditions is mutable actor state.
A player can pick up or drop a key, gain or lose a flag, change ownership --
all without touching the exit's lock definition. This means lock-based
equivalence classes are not stable over cache lifetime. The 3-5 class
estimate is plausible for a snapshot in time, but the classes shift as
world state changes.

This is the fundamental difference from the FPS model: hull size was a static
property of the NPC type. Lock-passing is a dynamic property of the
actor's current state.

**Lock classification by cachability**:

- **Structurally static**: Locks that depend only on the identity of the
  actor (e.g., `=<dbref>`, `owner(<dbref>)`). These rarely change.
- **Volatilely static**: Locks that depend on actor state which changes
  infrequently but can change at any time (flags, attributes, carried
  objects). These are the common case. They look static but are not.
- **Dynamic**: `BOOLEXP_EVAL` (arbitrary softcode evaluation). These
  cannot be pre-classified at all. Exits with eval-locks are opaque.

The practical consequence: Tier 2 routing tables cannot be treated as
long-lived caches in the way Tier 1 tables can. See the Invalidation
section for how this is handled.

### Variable Exits (A_EXITVARDEST)

Exits with softcode-computed destinations cannot participate in
pre-computed routing. The destination is unknown until execution context
is provided.

**Handling**: Exclude variable exits from the routing table. They are opaque
edges. If game admins want them routable, they can set a hint attribute
(e.g., `ROUTE_HINT`) with the typical destination dbref.

## Correctness Contract

`route()` is an advisory function, not a movement primitive. It answers
"which exit *should* I take?" not "which exit *can* I take right now?"
Callers must understand what is and is not guaranteed.

### Tier 1 Guarantees

- The returned exit **existed and connected the indicated rooms** at the
  time the routing table was last computed.
- The route is **topologically valid** as of the last generation.
- **No lock checking is performed.** The exit may be locked against the
  executor.
- After a topology change (`@dig`, `@destroy`, `@link`, `@open`,
  `@unlink`), the table is stale until recomputation. During this window,
  the returned exit may no longer exist or may point to a different
  destination.

### Tier 2 Guarantees

- Same as Tier 1, plus: the returned exit **was passable by the executor's
  lock-equivalence class** at the time the table was computed.
- Lock-equivalence classes are snapshots. **World state changes that affect
  lock evaluation (flag changes, inventory changes, attribute changes,
  ownership changes) can silently invalidate a Tier 2 result without
  triggering table recomputation.**
- Tier 2 results are therefore **best-effort hints**, not access guarantees.

### Caller Obligations

- **Before moving through a returned exit**, the caller should verify the
  exit still exists and (for player-facing use) that the executor passes
  the lock. This is a cheap check (`could_doit()`) compared to
  recomputing the route.
- **Stale data is safe to use optimistically**: if the exit turns out to be
  impassable, the caller falls back (tries alternate exits, reports no
  route, etc.). The routing table is a hint that avoids BFS at query time;
  it is not a guarantee.
- **`route()` never has side effects.** It does not move objects, trigger
  attributes, or evaluate softcode on exits (except for Tier 2 lock
  evaluation on the immediate next-hop exit).

## Invalidation

FPS nodes were static. MUSH topology is not. The routing table is a
cache that must be invalidated when the underlying graph changes.

Invalidation is split into two categories: topology changes (which affect
Tier 1 and Tier 2 tables) and world-state changes (which affect only
Tier 2 tables).

### Topology Invalidation (Tier 1 + Tier 2)

These events change the structure of the routing graph itself:

- `@dig` -- new room or exit created
- `@destroy` -- room or exit destroyed
- `@link` / `@unlink` -- exit destination changed
- `@open` -- new exit created
- Setting/clearing `A_EXITVARDEST` -- static edge becomes dynamic or
  vice versa
- `@set` of the NAVIGABLE flag/attribute -- node joins or leaves the
  routing graph
- `@parent` changes -- inherited exits may shift

These invalidate the routing table and require recomputation.

**Destructive topology changes** (`@destroy`, `@unlink`, making an exit
variable) are qualitatively different from additive changes (`@dig`,
`@open`). After an additive change, the old route still works -- it just
might not be optimal. After a destructive change, the cached next hop may
point to a nonexistent or disconnected exit. The invalidation mechanism
must handle destructive changes promptly; lazy recomputation is acceptable
only if query-time fallback validates the returned exit before use.

### World-State Invalidation (Tier 2 only)

These events do not change graph topology but change which exits a given
player can traverse:

- `@lock` / `@unlock` on an exit -- lock definition changed
- `@set` / `@clear` of flags on a player -- flag-based locks affected
- `@set` of attributes on a player -- attribute-based locks affected
- Inventory changes (get/drop/give) -- possession-based locks affected
- Ownership changes (`@chown`) -- owner-based locks affected

These events are **far more frequent** than topology changes and affect
only Tier 2 tables. Full table recomputation on every flag change or
inventory pickup is not practical.

**Tier 2 invalidation strategy**: Rather than tracking every world-state
change, Tier 2 tables use a **short TTL** (time-to-live) combined with
query-time validation:

1. Tier 2 tables expire after a configurable period (e.g., 60 seconds).
   Expired tables are recomputed on next query.
2. At query time, the returned exit's lock is always re-evaluated against
   the executor via `could_doit()`. If the exit is now impassable,
   `#-1 EXIT IMPASSABLE` is returned immediately (no automatic retry;
   see Failure Behavior in the API section).
3. Lock-equivalence classes are recomputed when the table is rebuilt, not
   tracked incrementally.

This accepts that Tier 2 routes may be slightly stale but ensures they
are **validated before use**. The routing table saves the cost of a full
BFS; the per-hop lock check is cheap.

### Topology Invalidation Mechanism

**Generation counter**: Each routing table (per-zone or global) has a
generation number. Topology-changing operations bump the counter. Routing
queries compare the stored generation against the current generation and
trigger lazy recomputation on mismatch.

**Zone-scoped invalidation**: If routing tables are per-zone, only the
affected zone's table is invalidated. A `@dig` in the Tavern Zone does
not invalidate the Space Station Zone's routing table.

**Incremental recomputation**: For small changes (single exit added or
removed), incremental updates to the routing table may be cheaper than
full recomputation. This is an optimization for later; full recomputation
of a 250-room zone is fast enough to start with.

This pattern mirrors the existing JIT cache invalidation via `mod_count`
in SQLite.

## Softcode Interface

```
route(<source>, <destination>[, <options>])
```

**Returns**: The dbref of the exit to take from `<source>` to move one hop
toward `<destination>`.

**Options** (slash-separated or flag argument):

| Option        | Behavior                                      |
|---------------|-----------------------------------------------|
| (default)     | Unconditional routing (Tier 1)                |
| `locked`      | Lock-aware routing for the executor (Tier 2)  |
| `distance`    | Return hop count instead of exit dbref        |
| `path`        | Return full exit list (space-separated dbrefs) |
| `rebuild`     | Force immediate table recomputation for the zone|

**Option combinations and validation semantics**:

- `route(A, B)` -- Tier 1 next hop. No lock check. O(1) lookup.
- `route(A, B, distance)` -- Tier 1 hop count. No lock check.
- `route(A, B, path)` -- Tier 1 full exit list. No lock check.
- `route(A, B, locked)` -- Tier 2 next hop. Lock validated on returned
  exit via `could_doit()`. If the exit is impassable, see Failure
  Behavior below.
- `route(A, B, locked distance)` -- Tier 2 hop count. This is the
  **table's hop count for the executor's lock class**, not a validated
  count. It reflects the snapshot at table build time. If world state
  has changed since the table was built, the actual reachable distance
  may differ. Returned as a best-effort estimate.
- `route(A, B, locked path)` -- Tier 2 full exit list. Only the first
  hop is validated at query time. Subsequent hops reflect the table
  snapshot and may include exits the executor can no longer pass.
  Callers walking this path must validate each hop before use.
- `route(A, B, rebuild)` -- Forces the zone's Tier 1 table to be
  discarded and recomputed before answering. Useful after a known
  topology change if the caller cannot tolerate stale data.
  **Restricted to wizards** — this is a cache-busting operation that
  triggers a full BFS recomputation. Unprivileged callers receive
  `#-1 PERMISSION DENIED`. Rebuilds the zone containing `<source>`;
  if `<source>` and `<destination>` are in different zones, only the
  source zone is rebuilt (the destination zone rebuilds lazily when
  a query next enters it).
- `route(A, B, locked rebuild)` -- Same, but for the executor's Tier 2
  table. Use when a prior call returned `EXIT IMPASSABLE` and the
  caller wants a fresh attempt. Also wizard-only.

**Default mode rationale**: The default is unconditional (Tier 1) because
it is the only mode available in Phase 1, it requires no lock evaluation
overhead, and it is the appropriate mode for the primary use cases:
admin tools, NPC scripting, and mapper data export. Player-facing code
that asks "how do I get there?" should use the `locked` option or
validate the returned exit before presenting it to the player. See the
Correctness Contract section.

**Error returns**:

- `#-1 NO ROUTE` -- no path exists between source and destination
- `#-1 NOT NAVIGABLE` -- source or destination is not in the routing graph
- `#-1 EXIT IMPASSABLE` -- Tier 2 only; see Failure Behavior below
- `#-1 PERMISSION DENIED` -- `rebuild` used by non-wizard caller
- `#-1 RATE LIMITED` -- uncached BFS throttled (one per executor per second)
- `#-2` -- invalid arguments

**Failure behavior for `locked` mode**:

When the query-time `could_doit()` check on the next-hop exit fails,
`route()` does **not** silently retry or recompute. It returns
`#-1 EXIT IMPASSABLE` immediately. Rationale:

1. **No hidden cost**: Automatic retry would make `route()` latency
   unpredictable. A single `route()` call should not trigger a full BFS
   recomputation behind the caller's back.
2. **Caller control**: The caller can decide whether to retry, fall back
   to Tier 1, or report failure. Different use cases want different
   behavior.
3. **Staleness signal**: `EXIT IMPASSABLE` tells the caller the table
   is stale for this executor. The caller can force a table rebuild
   (future: `route(..., locked rebuild)`) or accept the failure.

If the table itself has expired (TTL exceeded), it is rebuilt before
the query executes -- this is a cache miss, not a retry. The `could_doit()`
check runs against the freshly built table's result. If that also fails,
`EXIT IMPASSABLE` is returned.

**Examples**:

```
> think route(here, #100)
#347
> think route(here, #100, distance)
7
> think route(here, #100, path)
#347 #352 #360 #388 #401 #415 #99
> think route(here, #100, locked)
#347
```

## Storage

### SQLite (Tier 1 groundwork)

Phase 1 creates the Tier 1 SQLite schema and persists routing metadata
(`route_generation`, node count) so later phases can warm-start cleanly.
The full `route_table` contents are not persisted yet; Phase 1 still
rebuilds the in-memory table lazily on first query after startup. The
schema for later persistence is:

```sql
CREATE TABLE route_nodes (
    room_dbref   INTEGER PRIMARY KEY,
    zone_id      INTEGER,
    is_navigable INTEGER DEFAULT 1
);

CREATE TABLE route_table (
    zone_id      INTEGER,
    source       INTEGER,
    destination  INTEGER,
    next_exit    INTEGER,
    PRIMARY KEY (zone_id, source, destination)
);

CREATE TABLE route_meta (
    zone_id      INTEGER PRIMARY KEY,
    generation   INTEGER,
    node_count   INTEGER,
    compressed   BLOB     -- optional: compressed table for large zones
);
```

### In-Memory Cache (Tier 1 + Tier 2)

For runtime performance, the compressed Tier 1 routing table for active
zones lives in memory. SQLite is the persistence layer; the in-memory
representation uses the same compression as the FPS model (direct markers,
always-X rows, sparse entries).

**Tier 2 tables are in-memory only.** They are ephemeral by design:
short TTL, rebuilt on expiry, and dependent on a snapshot of world state
that changes frequently. Persisting them would mean persisting stale
data. The in-memory representation is:

```
struct RouteClassTable {
    int          zone_id;
    int          class_id;       // lock-equivalence class
    dynamic_bitset lock_bitmap;  // which exits this class can pass
                                 // (one bit per exit in the zone)
    time_t       created_at;     // for TTL expiry
    // compressed next-hop table (same format as Tier 1)
};
```

On restart, Tier 2 tables are simply absent and rebuilt on first query.
This is consistent with the correctness contract: Tier 2 results are
best-effort hints, not durable state.

## Implementation Phases

### Phase 1: Static Unconditional Routing

- Add `NAVIGABLE` flag or attribute.
- BFS/Dijkstra over navigable rooms with static exits.
- Build next-hop table with compression.
- Implement `route()` softcode function (Tier 1 only).
- Generation-counter invalidation on topology changes.
- SQLite schema groundwork plus routing metadata persistence.

### Phase 2: Hierarchical Zone Routing

- Associate navigable rooms with zones.
- Per-zone local tables with independent invalidation.
- **Meta-table** for inter-zone routing. The meta-table is a directed
  graph of **gateway edges**, not just zone adjacency. Each entry is:
  ```
  (source_zone, dest_zone, gate_room, gate_exit, target_room)
  ```
  where `gate_room` is the room in `source_zone` containing `gate_exit`,
  and `target_room` is the destination room in `dest_zone`. Multiple
  gateway edges may exist between the same pair of zones (different
  border crossings, one-way links, exits into different subregions).
  The meta-table runs Dijkstra over gateway edges to find the best
  border crossing for a given destination zone. Edge weight is a
  **heuristic estimate**: the intra-zone hop count from `gate_room`
  to the zone's most-connected room (or zone centroid). This is not
  exact — the true cost depends on the caller's current room within
  the zone, which varies per query. The meta-table selects a
  reasonable gateway; the local-table then routes precisely to it.
  For zones with multiple gateways to the same neighbor, this may
  not always pick the closest one to the caller, but it avoids the
  cost of per-room meta-table entries.
  Routing logic:
  - Same zone: use the local table directly.
  - Different zones: consult the meta-table to find the specific
    gateway edge (not just "next zone"), then use the local table
    to route from the current room to that gate room.
- Meta-table is cheap to compute and rarely invalidated (only when
  cross-zone exits are created or destroyed).

### Phase 3: Lock-Aware Routing

- Lock classification (structurally static / volatilely static / dynamic).
- Query-time lock validation on returned next-hop exit.
- TTL-based expiration for Tier 2 tables.
- Equivalence class detection (snapshot-based, rebuilt on table expiry).
- Multiple routing tables per zone (one per equivalence class).
- **Class limit**: cap at 8 active lock-equivalence classes per zone. If
  a player's capability set matches no existing class and the limit is
  reached, fall back to an **uncached lock-aware BFS** for that specific
  executor and query. This is a real BFS that respects the executor's
  locks (via `could_doit()` at each edge), not Tier 1 — it just isn't
  cached. Expensive but correct, and rare by design. Rate-limited to
  one uncached BFS per executor per second to prevent abuse; excess
  calls return `#-1 RATE LIMITED` rather than blocking.
- `route()` with `locked` option.
- `#-1 EXIT IMPASSABLE` return when validation fails.

### Phase 4: NPC Primitives

- Server-side `@walk <npc>=<destination>` -- moves an NPC one hop per
  tick along the routed path, consuming routing data without softcode
  overhead.
- Server-side `@patrol <npc>=<room1> <room2> ...` -- continuous loop
  movement between waypoints.
- These leverage the routing tables natively, avoiding the cost of
  repeated `route()` softcode calls per tick.

### Phase 5: Optimizations

- Incremental table updates for single-edge changes.
- ROUTE_HINT attribute for variable exits.
- Path caching for hot routes.
- Background recomputation: rebuild tables in a worker thread, serve
  stale data during recomputation, atomic-swap the new table in when
  ready. Prevents main-thread stalls on large zones.
- JIT integration -- `route()` calls in compiled softcode.

## Open Questions

1. **Granularity of NAVIGABLE**: Flag on each room, or zone-level opt-in?
   Both? Flag is more flexible; zone-level is less work for builders.

2. **Cross-zone routing**: Addressed by the hierarchical meta-table /
   local-table design in Phase 2. Remaining detail: how to handle rooms
   that belong to no zone (orphan rooms, zone 0)?

3. **Cost metric**: All exits equal weight (hop count), or support weighted
   edges? Weighted edges enable "prefer the main road" but add complexity.

4. **Stale routes**: Additive changes (`@dig`, `@open`) produce stale
   routes that still work -- they're just not optimal. Destructive changes
   (`@destroy`, `@unlink`) produce stale routes that may point to
   nonexistent exits. Query-time validation (check the exit exists and,
   for Tier 2, passes `could_doit()`) is required regardless of
   invalidation strategy. How expensive is this validation in practice?

5. **Maximum graph size**: What is the practical upper bound? 10,000 rooms?
   50,000? At what point does full recomputation become too expensive and
   incremental updates become mandatory?

6. **Asymmetric exits**: Exit from A to B does not imply an exit from B to
   A. The routing graph is directed. Dijkstra handles this naturally, but
   it means route(A, B) and route(B, A) can differ or one can be
   unreachable.
