# Attribute Metadata Contract

This document defines the per-attribute metadata contract for TinyMUX's
storage backend interface (`IStorageBackend`).  Any backend implementation
must preserve these semantics.

## Key Space

Attributes are keyed by `(object, attrnum)`:

- `object` — unsigned 32-bit dbref identifying the owning object.
- `attrnum` — unsigned 32-bit attribute number.  Values 0-255 are
  reserved for builtin attributes (DESC, SUCC, FAIL, etc.).  Values
  256+ are user-defined virtual attributes.

The pair `(object, attrnum)` is globally unique.

## Value Fields

Each attribute stores four fields:

| Field | Type | Description |
|-------|------|-------------|
| `value` | opaque bytes | Attribute content.  Typically UTF-8 text, up to LBUF_SIZE bytes.  The backend must store and return the exact byte sequence without modification. |
| `owner` | signed 32-bit int | dbref of the attribute's owner.  Default: -1 (NOTHING). |
| `flags` | signed 32-bit int | Attribute flag bitfield (AF_WIZARD, AF_LOCK, etc.).  Default: 0. |
| `mod_count` | unsigned 32-bit int | Monotonically increasing modification counter.  Default: 0.  Incremented on every Put.  Never decremented. |

## Interface Methods

### Single-Attribute Operations

- **Get(object, attrnum)** — Returns value, owner, flags.  Returns false
  if the attribute does not exist.
- **Put(object, attrnum, value, len, owner, flags)** — Inserts or
  replaces the attribute.  Must atomically increment mod_count.
- **Del(object, attrnum)** — Removes the attribute.  Returns true even
  if it did not exist.

### Bulk Operations

- **DelAll(object)** — Removes all attributes on an object.
- **GetAll(object, callback)** — Iterates all attributes on an object.
  The callback receives `(attrnum, value, len, owner, flags)` for each.
  Iteration order is unspecified.
- **GetBuiltin(object, callback)** — Same as GetAll but only iterates
  attributes with `attrnum < 256`.

### Count and Mod-Count

- **Count(object)** — Returns the number of attributes stored on an
  object.
- **GetModCount(object, attrnum)** — Returns the current mod_count for
  a single attribute.  Returns 0 if the attribute does not exist.
- **GetAllModCounts(object, callback)** — Iterates `(attrnum, mod_count)`
  pairs for an object.  Used by the JIT compiler to seed the in-memory
  staleness tracking map.

### Maintenance

- **Sync()** — Ensure all data is durable.  For SQLite, this is a WAL
  checkpoint.  For a KV backend, this would be an explicit flush or
  sync operation.
- **Tick()** — Periodic light maintenance.  For SQLite, runs
  `PRAGMA optimize`.  A KV backend may use this for compaction hints
  or statistics collection.

## mod_count Semantics

The `mod_count` field supports the JIT compiler's cache invalidation:

1. Each `(object, attrnum)` pair has a mod_count that starts at 0.
2. Every Put increments mod_count atomically (even if the value is
   unchanged).
3. The JIT compiler records mod_count when compiling an attribute.
   On subsequent access, it compares the stored mod_count against the
   current value to detect staleness.
4. mod_count is never decremented.  Deletion removes the attribute
   entirely; if the same key is re-created, mod_count restarts from 0
   (or the backend's default).

The backend must ensure that mod_count increments are visible to
subsequent GetModCount calls within the same process.  Cross-process
visibility is not required (TinyMUX is single-process).

## @search Interaction

The `@search` command does not query the attribute table directly.

- SQL fast-path predicates (owner, type, zone, parent, flags) query
  the object metadata table only.
- `eval=` and `eeval=` predicates evaluate softcode expressions via
  `mux_exec()`, which triggers attribute lookups through the standard
  `cache_get()` path.
- No changes to `@search` are needed when the attribute backend changes.

## Encoding Requirements

A backend implementation must:

1. Store attribute values as opaque byte sequences.  No character set
   conversion or normalization.
2. Preserve exact byte length (values may contain embedded NUL bytes,
   though this is rare in practice).
3. Handle values up to LBUF_SIZE (currently 65536) bytes.
4. Use little-endian fixed-width integers for any on-disk key/metadata
   encoding (per the design doc's portability requirement).
