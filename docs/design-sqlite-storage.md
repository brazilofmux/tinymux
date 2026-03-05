# SQLite Storage Engine Design

## Overview

Replace both CHashFile (`.dir`/`.pag` attribute store) and the `db[]` array
flatfile dump with a single SQLite database as the unified, authoritative
store for all game state. Layer a read-only LRU cache over it for hot-path
performance. Develop as a side project (like GANL) and slot in when ready.

## Problem Statement

TinyMUX currently has **two separate persistence systems** with different
failure modes:

### CHashFile (attributes)

The biggest bottleneck is **writing dirty pages back to disk**. The `Tick()`
method dribbles pages via `FlushCache()` on a timer, but under heavy write
load the cache fills with dirty pages faster than they can be flushed. When
`AllocateEmptyPage()` needs a free slot, it must synchronously flush the
oldest page — blocking the game loop. The `Sync()` call during `@dump` flushes
*all* dirty pages at once, causing a visible pause.

Additional CHashFile limitations:
- No indexing beyond hash buckets — `@search` is a full scan
- No crash recovery — interrupted writes can corrupt `.dir`/`.pag`
- Page splitting and directory doubling are complex and fragile

### db[] array (object metadata)

The `db[]` in-memory array holds location, contents, exits, next, link, owner,
parent, zone, flags, powers, name, and throttle data. This is serialized to a
flatfile periodically by `db_write()`. Between dumps, it exists only in
memory. A crash loses everything since the last dump.

### The split creates problems

- Two different write strategies, two different failure windows
- `@dump` must serialize both systems — flatfile write + `cache_sync()`
- `@restart` must handle both — reload flatfile + reopen CHashFile
- No single source of truth — object metadata and attributes are decoupled

## Design Principles

### Write-Through, Not Write-Back

**SQLite is always authoritative.** Every mutation — whether to an attribute
or to object metadata — is written through to SQLite immediately. The LRU
cache is a **read-only acceleration layer** that is never dirty.

This eliminates:
- Dirty tracking
- Flush dribbling (`Tick()`)
- Cache coherency problems
- Data loss windows between cache and durable store

**Why this works:** CHashFile's `WritePage()` was slow because it seeked to a
specific offset and wrote a full page of mixed data. SQLite WAL writes are
sequential appends — fundamentally different I/O. A single prepared
`INSERT OR REPLACE` with WAL mode and `PRAGMA synchronous=NORMAL` is typically
under 50 microseconds.

### One Database for Everything

Every piece of game state has its rightful place in SQLite:
- Object metadata (what `db[]` holds today)
- Attribute values (what CHashFile holds today)
- The `db[]` array remains in memory as a read cache for object metadata,
  but it is populated FROM SQLite, not the other way around.

### Cache Is Never Authoritative

The LRU cache and `db[]` array are populated from SQLite on startup and on
cache miss. They are invalidated on writes. They can be dropped and rebuilt
at any time without data loss. A crash loses nothing — SQLite has it all.

### @search Hits SQLite Directly

Because writes are write-through, SQLite is always up to date. Bulk
operations like `@search` query SQLite directly without worrying about
stale data or dirty cache entries. No coherency problem exists because
there is no dirty cache.

## Architecture

```
+--------------------------------------------------+
|                  Game Code                        |
|  atr_get / s_Location / Flags / @search / etc.   |
+--------------------------------------------------+
        |  reads             |  writes
        v                    v
+----------------+    +---------------------------+
| Read Cache     |    | Write-Through Path        |
| - db[] array   |    | - SQLite write            |
| - Attribute    |    | - Invalidate/update cache |
|   LRU map      |    |                           |
+----------------+    +---------------------------+
        |  miss              |
        v                    v
+--------------------------------------------------+
|                    SQLite                         |
|  WAL mode, synchronous=NORMAL, WITHOUT ROWID     |
|  Single .db file — always authoritative           |
+--------------------------------------------------+
```

## Schema

```sql
-- Object metadata (replaces db[] flatfile serialization)
CREATE TABLE objects (
    dbref       INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    location    INTEGER NOT NULL DEFAULT -1,
    contents    INTEGER NOT NULL DEFAULT -1,
    exits       INTEGER NOT NULL DEFAULT -1,
    next        INTEGER NOT NULL DEFAULT -1,
    link        INTEGER NOT NULL DEFAULT -1,
    owner       INTEGER NOT NULL DEFAULT -1,
    parent      INTEGER NOT NULL DEFAULT -1,
    zone        INTEGER NOT NULL DEFAULT -1,
    pennies     INTEGER NOT NULL DEFAULT 0,
    flags1      INTEGER NOT NULL DEFAULT 0,
    flags2      INTEGER NOT NULL DEFAULT 0,
    flags3      INTEGER NOT NULL DEFAULT 0,
    powers1     INTEGER NOT NULL DEFAULT 0,
    powers2     INTEGER NOT NULL DEFAULT 0
);

-- Attribute storage (replaces CHashFile .dir/.pag)
CREATE TABLE attributes (
    object      INTEGER NOT NULL,
    attrnum     INTEGER NOT NULL,
    value       BLOB NOT NULL,
    PRIMARY KEY (object, attrnum)
) WITHOUT ROWID;
```

`WITHOUT ROWID` on attributes is critical — it makes the primary key a
clustered index, so all attributes for one object are physically adjacent on
disk. `GetAllForObject()` becomes a single sequential read.

The `objects` table uses a regular rowid table because the primary key is a
single integer — SQLite optimizes this case automatically.

### What stays in db[]

The `db[]` array remains as an in-memory read cache. It is loaded from the
`objects` table at startup. The `s_Location()`, `s_Flags()`, etc. macros are
replaced with functions that:
1. Update `db[]` in memory (for fast subsequent reads)
2. Write through to SQLite (for durability)

The `Location()`, `Flags()`, etc. read macros continue to read from `db[]`
unchanged — this is the hot path and it stays zero-cost.

### What stays in the LRU cache

The attribute LRU cache (`attribute_lru_cache_map`) remains as a read cache.
On `cache_put()`, the entry goes to SQLite and the cache is updated (or
invalidated). On `cache_get()`, the cache is checked first; on miss, SQLite
is queried and the result is cached.

### Transient state that does NOT go in SQLite

Some fields in `db[]` are runtime-only and don't belong in SQLite:
- `cpu_time_used` — accumulated CPU time, reset on restart
- `tThrottleExpired` / `throttled_*` — rate limiting state
- `purename` / `moniker` — derived from attributes, cached for speed

These stay in `db[]` only, initialized to defaults on load.

## SQLite Configuration

```cpp
sqlite3_exec(db, "PRAGMA journal_mode=WAL", ...);
sqlite3_exec(db, "PRAGMA synchronous=NORMAL", ...);
sqlite3_exec(db, "PRAGMA mmap_size=268435456", ...);   // 256MB
sqlite3_exec(db, "PRAGMA page_size=4096", ...);
sqlite3_exec(db, "PRAGMA cache_size=-65536", ...);     // 64MB SQLite page cache
```

## Write-Through Implementation

### Object Metadata Writes

Today's macros:
```cpp
#define s_Location(t,n)     db[t].location = (n)
#define s_Flags(t,f,n)      db[t].fs.word[f] = (n)
```

Become functions:
```cpp
void s_Location(dbref t, dbref n)
{
    db[t].location = n;
    // UPDATE objects SET location=? WHERE dbref=?
    sqlite3_bind_int(stmtUpdateLocation, 1, n);
    sqlite3_bind_int(stmtUpdateLocation, 2, t);
    sqlite3_step(stmtUpdateLocation);
    sqlite3_reset(stmtUpdateLocation);
}
```

For fields that change together (e.g., during object creation or movement),
batch into a single transaction:
```cpp
void move_object(dbref thing, dbref dest)
{
    sqlite3_exec(db, "BEGIN", ...);
    // ... unlink from old location, link into new ...
    // Multiple s_Location / s_Contents / s_Next calls
    // Each one writes through individually within the transaction
    sqlite3_exec(db, "COMMIT", ...);
}
```

The transaction groups the I/O into a single WAL append. Individual
`s_Location()` calls outside a transaction auto-commit, which is fine for
isolated updates.

### Attribute Writes

```cpp
bool cache_put(Aname *nam, const UTF8 *value, size_t len)
{
    // 1. Write to SQLite (authoritative)
    sqlite3_bind_int(stmtAttrPut, 1, nam->object);
    sqlite3_bind_int(stmtAttrPut, 2, nam->attrnum);
    sqlite3_bind_blob(stmtAttrPut, 3, value, len, SQLITE_STATIC);
    sqlite3_step(stmtAttrPut);
    sqlite3_reset(stmtAttrPut);

    // 2. Update read cache
    // ... (existing LRU cache update logic, unchanged)
}
```

### Attribute Deletes

```cpp
void cache_del(Aname *nam)
{
    // 1. Delete from SQLite
    sqlite3_bind_int(stmtAttrDel, 1, nam->object);
    sqlite3_bind_int(stmtAttrDel, 2, nam->attrnum);
    sqlite3_step(stmtAttrDel);
    sqlite3_reset(stmtAttrDel);

    // 2. Remove from read cache
    // ... (existing LRU cache removal logic, unchanged)
}
```

## Prepared Statements

```cpp
// Object metadata
sqlite3_stmt *stmtObjInsert;       // INSERT INTO objects VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
sqlite3_stmt *stmtObjDelete;       // DELETE FROM objects WHERE dbref=?
sqlite3_stmt *stmtObjLoadAll;      // SELECT * FROM objects ORDER BY dbref
sqlite3_stmt *stmtUpdateLocation;  // UPDATE objects SET location=? WHERE dbref=?
sqlite3_stmt *stmtUpdateContents;  // UPDATE objects SET contents=? WHERE dbref=?
sqlite3_stmt *stmtUpdateExits;     // UPDATE objects SET exits=? WHERE dbref=?
sqlite3_stmt *stmtUpdateNext;      // UPDATE objects SET next=? WHERE dbref=?
sqlite3_stmt *stmtUpdateLink;      // UPDATE objects SET link=? WHERE dbref=?
sqlite3_stmt *stmtUpdateOwner;     // UPDATE objects SET owner=? WHERE dbref=?
sqlite3_stmt *stmtUpdateParent;    // UPDATE objects SET parent=? WHERE dbref=?
sqlite3_stmt *stmtUpdateZone;      // UPDATE objects SET zone=? WHERE dbref=?
sqlite3_stmt *stmtUpdateFlags;     // UPDATE objects SET flags1=?, flags2=?, flags3=? WHERE dbref=?
sqlite3_stmt *stmtUpdatePowers;    // UPDATE objects SET powers1=?, powers2=? WHERE dbref=?
sqlite3_stmt *stmtUpdatePennies;   // UPDATE objects SET pennies=? WHERE dbref=?
sqlite3_stmt *stmtUpdateName;      // UPDATE objects SET name=? WHERE dbref=?

// Attribute operations
sqlite3_stmt *stmtAttrGet;         // SELECT value FROM attributes WHERE object=? AND attrnum=?
sqlite3_stmt *stmtAttrPut;         // INSERT OR REPLACE INTO attributes VALUES (?,?,?)
sqlite3_stmt *stmtAttrDel;         // DELETE FROM attributes WHERE object=? AND attrnum=?
sqlite3_stmt *stmtAttrDelObj;      // DELETE FROM attributes WHERE object=?
sqlite3_stmt *stmtAttrGetObj;      // SELECT attrnum, value FROM attributes WHERE object=?
```

## @search and Bulk Operations

Because SQLite is always up to date (write-through), `@search` queries it
directly with no coherency concerns:

```sql
-- Find objects by flag
SELECT dbref FROM objects WHERE (flags1 & ?) != 0;

-- Find objects by attribute value
SELECT DISTINCT object FROM attributes
WHERE attrnum=? AND value LIKE ?;

-- Find objects by owner in a zone
SELECT dbref FROM objects WHERE owner=? AND zone=?;
```

Optional indexes for common search patterns:
```sql
CREATE INDEX idx_objects_owner ON objects(owner);
CREATE INDEX idx_objects_zone ON objects(zone);
CREATE INDEX idx_objects_location ON objects(location);
```

## @dump and Tick()

### @dump

`@dump` no longer serializes game state. SQLite already has everything. It
becomes a WAL checkpoint:

```cpp
void dump_database(void)
{
    // Force WAL checkpoint — compact the WAL into the main database file.
    sqlite3_wal_checkpoint_v2(m_db, nullptr, SQLITE_CHECKPOINT_TRUNCATE,
                              nullptr, nullptr);
}
```

This is fast (sub-second for typical games) and non-blocking for readers.

### Tick()

`Tick()` is no longer needed for dirty page flushing. It can be repurposed
for optional maintenance:
- LRU cache trimming
- SQLite `PRAGMA optimize` (periodic query planner tuning)
- Statistics gathering

### Flatfile export

The flatfile format (`db_write()`) remains as an **export/migration format**
only. It reads from SQLite (or from `db[]` + cache, same thing) and writes
the traditional flatfile. This is used for:
- Migration to/from other MU* servers
- Human-readable backups
- Compatibility with existing tools

## Startup and Loading

### Cold Start (new database or flatfile import)

1. Create SQLite database and tables
2. Read flatfile via existing `db_read()` path
3. `db_read()` populates `db[]` and calls `cache_put()` — both now write
   through to SQLite
4. Once loaded, the flatfile is no longer needed

### Warm Start (normal startup)

1. Open existing SQLite database
2. `SELECT * FROM objects ORDER BY dbref` — populate `db[]` array
3. Attribute LRU cache starts cold, warms on demand
4. Optional preloading (see below)

### @restart

1. In-process cache (`db[]` + LRU) is lost across `exec()`
2. SQLite WAL recovery handles any incomplete transactions automatically
3. On restart, warm start path runs — reload `db[]` from SQLite
4. No flatfile serialization/deserialization needed
5. Preloading warms the attribute cache for connected players

## Cache Preloading

### Startup Preload

Load all attributes for:
- `#0` (master room)
- Global config objects
- Connected player objects (from descriptor list surviving `@restart`)

### Player Connect Preload

Load all attributes for:
- The player object
- The player's location
- Objects in the player's inventory and location

### Player Move Preload

Load all attributes for:
- The destination room
- Objects in the destination room

### Preload Implementation

```cpp
void PreloadObject(dbref obj)
{
    // SELECT attrnum, value FROM attributes WHERE object=?
    // One indexed range scan, populates LRU cache
    sqlite3_bind_int(stmtAttrGetObj, 1, obj);
    while (sqlite3_step(stmtAttrGetObj) == SQLITE_ROW)
    {
        int attrnum = sqlite3_column_int(stmtAttrGetObj, 0);
        // ... populate LRU cache entry ...
    }
    sqlite3_reset(stmtAttrGetObj);
}
```

### Preload Budget

- `preload_max_objects` — cap on objects preloaded per event
- `preload_max_bytes` — cap on total bytes per event

## Crash Recovery and Backup

### SIGSEGV / Crash

- SQLite with WAL mode recovers automatically on next open
- All committed writes are preserved
- Since writes are write-through, the only possible loss is a write that
  was interrupted mid-`sqlite3_step()` — SQLite rolls this back cleanly
- No `.dir`/`.pag` corruption to worry about
- No flatfile staleness to worry about

### Incremental Backup

```cpp
// Non-blocking online backup via SQLite backup API
sqlite3_backup *backup = sqlite3_backup_init(destDb, "main", srcDb, "main");
while (sqlite3_backup_step(backup, nPagesPerStep) == SQLITE_OK)
{
    // Yield back to game loop between steps
}
sqlite3_backup_finish(backup);
```

## Migration

### Flatfile to SQLite (first-time migration)

1. Start with `--migrate-to-sqlite` flag
2. Runs existing `db_read()` which calls `cache_put()` etc.
3. Write-through populates SQLite
4. Done — flatfile no longer needed for normal operation

### CHashFile to SQLite (existing disk-based games)

Standalone tool or `@admin` command:
1. Open `.dir`/`.pag` via CHashFile
2. Open/create SQLite database
3. Iterate all CHashFile entries, insert into `attributes` table
4. Read existing flatfile, populate `objects` table
5. Verify row counts match

### Configuration

```
storage_backend sqlite       # or "hashfile" for legacy
sqlite_path     data/mux.db
cache_max_size  134217728    # 128MB attribute LRU cache
preload_on_connect yes
preload_on_move yes
preload_max_objects 200
```

## Implementation Stages

### Stage 1: Instrumentation and Baseline

Add counters to CHashFile and attrcache. Expose via `@list cache`.
Establish performance baseline. **Lands in mainline.**

### Stage 2: Storage Backend Interface

Abstract `cache_*` functions behind an `IStorageBackend` interface.
Wrap CHashFile as the first implementation. Validate interface
without changing behavior. **Lands in mainline.**

### Stage 3: SQLite Backend (side project)

Implement SQLite backend with write-through:
- Schema creation
- Prepared statements
- `cache_get` / `cache_put` / `cache_del` backed by SQLite
- WAL configuration

### Stage 4: Unified Object Store (side project)

Move `db[]` persistence from flatfile to SQLite:
- `objects` table
- Replace `s_Location()` etc. macros with write-through functions
- Startup loads `db[]` from SQLite instead of flatfile
- `@dump` becomes WAL checkpoint

### Stage 5: Preloading and Optimization

- Cache preloading on connect/move
- `@search` SQLite queries
- Optional indexes
- Performance validation against Stage 1 baseline

### Stage 6: Migration Tools

- Flatfile-to-SQLite importer
- CHashFile-to-SQLite converter
- Flatfile export retained for compatibility

| Phase | Work | Lands in |
|-------|------|----------|
| 1 | Counters + `@list cache` | Mainline |
| 2 | Backend interface + CHashFile wrapper | Mainline |
| 3 | SQLite attribute backend | Side project |
| 4 | SQLite object metadata backend | Side project |
| 5 | Preloading + `@search` optimization | Side project |
| 6 | Migration tools | Side project |

## Dependencies

- **SQLite3** — vendored amalgamation (`sqlite3.c` + `sqlite3.h`), single
  file, no external dependency.

## Success Criteria

1. **No game loop I/O stalls** — write-through to SQLite WAL is fast enough
   to never block the game loop perceptibly
2. **@dump completes in < 100ms** — WAL checkpoint vs. full flatfile write
3. **Attribute read latency unchanged** — LRU cache hit path is identical
4. **@search faster** — indexed SQLite queries vs. full object scan
5. **@restart recovery faster** — no flatfile parse, just load from SQLite
6. **Crash loses zero committed data** — SQLite WAL guarantees this
7. **All existing smoke tests pass** — behavioral compatibility
8. **Single source of truth** — no split between flatfile and CHashFile,
   no coherency questions between cache and store
