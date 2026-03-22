# What TinyMUX Has That PennMUSH Could Use

Reverse survey: TinyMUX advantages relevant to PennMUSH. Companion to
docs/survey-pennmush.md.

---

Penn is the most architecturally sophisticated of the three competitors, so
the gap is narrower here. But MUX has genuine advantages in storage,
networking, Unicode, and several function areas.

## Architecture

### 1. SQLite Write-Through vs Chunk Allocator + Flatfile Dumps

Penn doesn't use GDBM at all. It has a custom chunk allocator that's
genuinely sophisticated:

- **64KB regions** with per-chunk dereference tracking (1-byte counter per
  chunk, 0-255).
- **LRU disk paging:** Regions page in/out of memory via a swap file.
  Configurable cache size. Hot regions stay resident; cold ones go to disk.
- **Locality-driven migration:** Frequently-accessed chunks migrate together
  within regions. Migration runs incrementally—slides, away-moves, and
  fill-moves to consolidate free space and improve cache locality.
- **Per-attribute granularity:** Each attribute value is its own chunk, not
  packed into an object blob. No blob-relocation pathology.
- **Fork-based dumps:** Child process serializes to flatfile while parent
  continues serving. Swap file is cloned for the child.

This is the most sophisticated memory management of any MU* server. The
dereference-weighted migration is clever—it's essentially a user-space
generational collector for attribute data.

What it doesn't provide:

- **Crash durability** — changes since last dump are lost. No WAL, no
  journal. Fork+dump is the only persistence mechanism.
- **Indexed queries** — @search is linear scan over the in-memory db[] array.
- **Standard tooling** — the swap file and in-memory format are proprietary.

MUX's SQLite write-through gives immediate durability, indexed @search,
WAL-mode checkpoints, and standard SQL tooling.

**Impact:** Penn's chunk allocator solves the blob-relocation problem that
plagues GDBM-based servers (each attribute is its own chunk). But it doesn't
solve crash durability or query performance. MUX's @search on indexed fields
is O(log n) vs Penn's O(n), and MUX never loses data on crash.

### 2. GANL Networking vs select()

Penn uses `select()` with configurable timeout. MUX's GANL provides epoll
(Linux) / kqueue (BSD) with O(1) event delivery.

Penn's choice of select() is a portability trade-off. MUX's factory pattern
achieves the same portability (select fallback exists) while using the optimal
engine on each platform.

**Impact:** Better scalability under high connection counts.

### 3. @restart / @shutdown/reboot—Connection Preservation

Both Penn and MUX preserve player connections across reboot. The mechanism is
the same: serialize descriptor state to a restart file (`restart.db` /
`reboot.db`), `exec()` the new binary, reload descriptors on startup. MUX has
an extra step—GANL deregisters fds from epoll/kqueue before the exec —
but this is an implementation detail, not a different approach.

**Impact:** Parity—both servers handle this well.

### 4. Comsys/Mail in SQLite

MUX stores comsys (channels) and mail in the same SQLite database with full
write-through. Penn stores these in separate flatfiles.

**Impact:** Unified backup, unified queries, no separate dump cycles.

### 4a. AST-Based Expression Evaluator

Penn has a traditional token-based parser (`eval.c`). MUX 2.14 replaced its
parser with an AST-based evaluator:

- Ragel-generated goto-driven scanner for tokenization
- Expressions parsed into an AST and cached in an LRU cache (1024 entries)
- All %-substitutions, `##`/`#@`/`#$` tokens, and NOEVAL constructs handled
  natively

**Impact:** Faster evaluation, especially for frequently-executed expressions.
Penn's parser re-parses every invocation.

### 4b. Three-Layer Architecture

MUX 2.14 splits into three shared objects: `libmux.so` (core types), `engine.so`
(game engine as COM module), `netmux` (network driver). 12 COM interfaces bridge
engine and driver. Penn is monolithic.

**Impact:** Foundation for process isolation. Clean separation enables engine
restarts without dropping connections.

### 4c. PCRE2 Parity

Both Penn and MUX now use PCRE2. This was previously a Penn advantage.

### 4d. PCG RNG Parity

Both Penn and MUX now use PCG random number generators. MUX uses PCG-XSL-RR-128/64
(128-bit state, 64-bit output) vs Penn's PCG basic (64-bit state, 32-bit output).
MUX's variant has larger state and wider output.

### 4e. JSON/WebSocket/Connection Logging Parity

MUX 2.14 now has:

- **JSON:** isjson() (native RFC 8259 parser), json()/json_query()/json_mod()
  (via SQLite JSON1)—comparable to Penn's cJSON + JSON1 approach
- **WebSocket:** RFC 6455 with same-port auto-detection and wss:// — comparable
  to Penn's implementation
- **Connection logging:** SQLite connlog table with connlog()/addrlog() —
  comparable to Penn's connlog.c

These were previously Penn-only features and the primary items from the PennMUSH
survey. The remaining Penn-only web feature is the built-in HTTP server.

### Architecture Comparison

| Area | PennMUSH | TinyMUX |
|------|----------|---------|
| Storage | Flatfile + chunk allocator | SQLite (write-through) |
| Networking | select() | GANL (epoll/kqueue) |
| Unicode | Latin-1 base, UTF-8 patches | Full UTF-8, NFC, DFA tables |
| Regex | PCRE2 | PCRE2 |
| RNG | PCG (64-bit state) | PCG-XSL-RR-128/64 |
| JSON | cJSON + SQLite JSON1 | isjson + SQLite JSON1 |
| WebSocket | RFC 6455 | RFC 6455 (same-port) |
| HTTP | Built-in server | None |
| Evaluator | Token-based | AST + Ragel + LRU cache |
| Architecture | Monolithic | Three-layer (libmux/engine/driver) |
| Tests | C framework + Perl harness | 489 smoke test cases |

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

MUX's UTF-8 pipeline: input—NFC normalize—store. Every attribute value
is canonical. The DFA tables are generated from Unicode data files via a
reproducible pipeline (utf/buildFiles + pairs + integers tools).

**Impact:** Penn games with international players risk character equivalence
bugs. MUX normalizes at storage, eliminating the class.

---

## Functions (114 that PennMUSH lacks)

### SQL Result Sets—MUX's Unique API

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
P6H (PennMUSH), and R7H (RhostMUSH) formats. The recent direct T5X—R7H
path preserves full 24-bit color.

Penn has `dbtools/` (dbupgrade, grepdb, pwutil, db2dot) but no cross-format
converter. A game migrating from Penn to another server has no tool support;
with Omega, converting to/from Penn's P6H format is a single command.

---

## Summary

Penn's web-facing features (JSON, WebSocket, connection logging) were its primary
advantages over MUX. As of 2.14, MUX has implemented all of these plus HMAC,
base64, letq(), and sortkey(). Penn retains its built-in HTTP server, object
warning system, and extended lock types.

MUX's architectural advantages remain: SQLite write-through (crash durability),
GANL networking (scalability), full UTF-8/NFC (correctness), cursor-based SQL
(capability), AST evaluator (performance), and three-layer architecture
(isolation). These are harder to retrofit than adding functions.

The remaining gap is Penn's HTTP server—the one Tier 1 web feature MUX has
not yet implemented. Penn's Perl-based test harness is also more sophisticated
than MUX's Expect-based smoke tests, though MUX's 489 test cases provide
broader function coverage.
