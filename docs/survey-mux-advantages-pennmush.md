# What TinyMUX Has That PennMUSH Could Use

Reverse survey: TinyMUX advantages relevant to PennMUSH. Companion to
docs/survey-pennmush.md.

---

Penn is the most architecturally sophisticated of the three competitors, so
the gap is narrower here. But MUX has genuine advantages in storage,
networking, Unicode, and several function areas.

## Architecture

### 1. SQLite Write-Through vs Chunk Allocator + Flatfile Dumps

Penn uses its chunk allocator (64KB regions, deref-based migration) with
periodic flatfile dumps. This is clever engineering from an era of bad system
allocators, but it doesn't provide:

- **Crash durability** — Penn loses changes since last dump
- **Indexed queries** — @search is linear scan
- **Standard tooling** — SQLite is inspectable with standard tools

MUX's SQLite write-through gives immediate durability, indexed @search,
WAL-mode checkpoints, and standard SQL tooling. Every s_*() accessor writes
through to SQLite synchronously.

**Impact:** MUX never loses data on crash. @search on indexed fields (owner,
type, zone, parent) is O(log n) vs Penn's O(n).

### 2. GANL Networking vs select()

Penn uses `select()` with configurable timeout. MUX's GANL provides epoll
(Linux) / kqueue (BSD) with O(1) event delivery.

Penn's choice of select() is a portability trade-off. MUX's factory pattern
achieves the same portability (select fallback exists) while using the optimal
engine on each platform.

**Impact:** Better scalability under high connection counts.

### 3. @restart / @shutdown/reboot — Connection Preservation

Both Penn and MUX preserve player connections across reboot. The mechanism is
the same: serialize descriptor state to a restart file (`restart.db` /
`reboot.db`), `exec()` the new binary, reload descriptors on startup. MUX has
an extra step — GANL deregisters fds from epoll/kqueue before the exec —
but this is an implementation detail, not a different approach.

**Impact:** Parity — both servers handle this well.

### 4. Comsys/Mail in SQLite

MUX stores comsys (channels) and mail in the same SQLite database with full
write-through. Penn stores these in separate flatfiles.

**Impact:** Unified backup, unified queries, no separate dump cycles.

---

## Unicode

### 5. Full UTF-8 with NFC Normalization

Penn has partial UTF-8 support (charconv.c for Latin-1↔UTF-8, some
utf_impl.c work) but lacks:

- Automatic NFC normalization at storage time
- DFA-based Unicode property classification
- Canonical Combining Class tracking
- Hangul algorithmic composition
- Width-aware string operations for CJK

MUX's UTF-8 pipeline: input — NFC normalize — store. Every attribute value
is canonical. The DFA tables are generated from Unicode data files via a
reproducible pipeline (utf/buildFiles + pairs + integers tools).

**Impact:** Penn games with international players risk character equivalence
bugs. MUX normalizes at storage, eliminating the class.

---

## Functions (114 that PennMUSH lacks)

### SQL Result Sets — MUX's Unique API

Penn has `sql()` and `mapsql()` but returns everything in one string. MUX has
a full cursor-based API:

| Function | Description |
|----------|-------------|
| rserror() | Last SQL error message |
| rsrec() | Current row as delimited string |
| rsnext() | Advance cursor to next row |
| rsprev() | Move cursor to previous row |
| rsrecnext() | Fetch current row and advance |
| rsrecprev() | Fetch current row and retreat |
| rsrows() | Total row count |
| rsrelease() | Release result set |

**Impact:** Memory-efficient iteration over large result sets. Penn's sql()
must buffer everything into one LBUF (8KB typically).

### Integer Math

Penn does integer math through floating-point conversion. MUX has native
64-bit integer functions:

- `iadd()`, `isub()`, `imul()`, `idiv()`, `iabs()`, `isign()`
- Range: ±9.2 quintillion (vs Penn's double precision: 53-bit mantissa)

**Impact:** Banking systems, large counters, exact arithmetic without
floating-point rounding.

### Color and Display

| Function | Description |
|----------|-------------|
| colordepth() | Per-connection color capability (0/1/4/8/24) |
| moniker() | ANSI-decorated display name |
| strip() | Strip specific characters from string |

Penn has `colors()` and `render()` but no per-connection depth detection.

### Pack/Unpack

`pack(<int>, <radix 2-64>)` / `unpack(<str>, <radix>)` — multi-radix
encoding for compact data storage. 6 bits per character in base-64. Penn has
no equivalent.

### Reality Levels

Full compile-time reality level system:

- `hasrxlevel()`, `hastxlevel()`, `rxlevel()`, `txlevel()`
- `listrlevels()` — enumerate defined levels
- Bitfield-based visibility: objects only see each other if levels match

Penn has no reality level system.

### Zone Content Functions

- `zchildren()`, `zexits()`, `zrooms()`, `zthings()` — enumerate zone
  contents by type

Penn has `zmwho()` and `zwho()` for zone players but no zone content
enumeration by object type.

### Other Notable Functions

| Function | Description |
|----------|-------------|
| crc32() | Fast checksum |
| sha1() | SHA-1 hash (Penn has sha0, which is deprecated) |
| distribute() | Distribute value across bins |
| pickrand() | Random element from list |
| lrest() | All but first element of list |
| lattrcmds() | List attributes with $-commands |
| successes() | Count dice successes (WoD) |
| siteinfo() | Connection site information |
| terminfo() | Terminal capability query |
| verb() | Formatted verb output |

### Time Formatting

MUX has more time format variants:

- `digittime()` — HH:MM format
- `singletime()` — "3d 2h 15m" format
- `exptime()` — expanded time
- `writetime()` — human-readable timestamp
- `restartsecs()` — seconds since last restart

---

## Omega Converter

MUX ships Omega, supporting conversion between T5X (TinyMUX), T6H (TinyMUSH),
P6H (PennMUSH), and R7H (RhostMUSH) formats. The recent direct T5X — R7H
path preserves full 24-bit color.

Penn has `dbtools/` (dbupgrade, grepdb, pwutil, db2dot) but no cross-format
converter. A game migrating from Penn to another server has no tool support;
with Omega, converting to/from Penn's P6H format is a single command.

---

## Summary

Penn's advantages over MUX (JSON, WebSocket, HTTP server, connection logging)
are feature additions. MUX's advantages over Penn (SQLite write-through, GANL
networking, full UTF-8/NFC, cursor-based SQL) are architectural foundations
that are much harder to retrofit. Both servers handle connection-preserving
reboot.

The ideal MU* server would combine MUX's storage/networking/Unicode
architecture with Penn's web-facing features.
