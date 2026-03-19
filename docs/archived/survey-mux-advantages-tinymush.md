# What TinyMUX Has That TinyMUSH Could Use

Reverse survey: TinyMUX advantages relevant to TinyMUSH 4. Companion to
docs/survey-tinymush.md.

---

## Architecture

### 1. SQLite Write-Through Storage

TinyMUSH has an abstract storage backend with two implementations:

- **GDBM (legacy):** Objects bundled with all attributes as single serialized
  blobs. Adding an attribute rewrites the entire blob, triggering GDBM record
  relocation. Uses GDBM's built-in cache (400 entries), file locking via
  fcntl, and `gdbm_sync()` for durability. A `dbrecover` tool can scan raw
  files for salvageable records after corruption.
- **LMDB (modern default):** Copy-on-write B+ tree with ACID transactions.
  No corruption from partial writes, automatic recovery on restart. Memory-
  mapped I/O means no separate cache layer needed. Map grows dynamically from
  1 GB to 16 GB. This is a genuine improvement over GDBM.

Both backends store objects as bundled blobs (object header + all attributes
in one record). Neither provides indexed queries — @search is always linear.

MUX's SQLite write-through stores each attribute as a separate row with typed
columns (owner INTEGER, flags INTEGER). @dump is a WAL checkpoint — no
re-serialization. @search routes to indexed SQL queries on owner, type, zone,
parent.

**Impact:** TinyMUSH's LMDB backend solves the crash-durability problem that
GDBM has. But MUX's per-attribute rows eliminate the blob-relocation
pathology, and SQL indexes make @search O(log n) instead of O(n). LMDB
narrows the gap significantly — the remaining difference is query capability
and storage granularity.

### 2. @search via Indexed SQL Queries

TinyMUSH's @search scans every object linearly. MUX routes simple @search
cases (owner, type, zone, parent, flags) to indexed SQL queries. O(log n + k)
vs O(n).

**Impact:** On a 100k-object game, MUX's @search is ~100x faster for common
cases.

### 3. GANL Networking (epoll/kqueue)

TinyMUSH uses `select()`. MUX's GANL layer provides:

- `epoll` on Linux (O(1) event delivery)
- `kqueue` on BSD/macOS
- `select` fallback
- Factory pattern — same binary, platform-specific engine

**Impact:** Scales to thousands of concurrent connections without performance
degradation. select() is O(n) in file descriptors.

### 3a. AST-Based Expression Evaluator

TinyMUSH has a traditional token-based parser with a `parse_to_cleanup()` pass.
MUX 2.14 replaced its parser with an AST-based evaluator:

- Ragel-generated goto-driven scanner for tokenization
- Expressions parsed into an AST and cached in an LRU cache (1024 entries)
- All %-substitutions, `##`/`#@`/`#$` tokens, and NOEVAL constructs handled
  natively
- Eliminates re-parsing of frequently evaluated expressions

**Impact:** Faster evaluation of complex softcode, especially in loops.
Architectural foundation for future optimization (JIT compilation).

### 3b. Three-Layer Architecture

MUX 2.14 splits into three shared objects:

- `libmux.so` — core types and utilities
- `engine.so` — game engine loaded as a COM module
- `netmux` — network driver

The engine communicates with the driver exclusively through 12 COM interfaces.
TinyMUSH has a module system via dlsym() but the core server is monolithic.

**Impact:** Foundation for process isolation (engine crashes don't kill the
driver). Clean separation of concerns.

### 3c. PCRE2

TinyMUSH uses PCRE. MUX 2.14 upgraded to PCRE2 with JIT compilation support,
better Unicode handling, and modern memory management.

### 4. @restart — Connection Preservation

Both TinyMUSH and MUX preserve player connections across restart. The
mechanism is the same: serialize descriptor state to a restart file
(`restart.db`), `exec()` the new binary, reload descriptors on startup. MUX
has an extra step — GANL deregisters fds from epoll/kqueue before the exec
— but this is an implementation detail, not a different approach.

**Impact:** Parity — both servers handle this well.

---

## Unicode

### 5. Full UTF-8 with NFC Normalization

TinyMUSH has no meaningful Unicode support. MUX has:

- DFA-based Unicode classification tables (314-state printable check)
- Automatic NFC normalization at attribute storage time
- Canonical Combining Class tracking (132 states, 934 code points)
- NFD decomposition (97 states, 2081 code points)
- NFC composition (129 states, 964 pairs)
- Hangul algorithmic composition
- CJK width-aware string truncation
- `chr()` and `ord()` for full Unicode range

**Impact:** International players can use native scripts. é and
e+combining-acute normalize to the same form. No mojibake.

---

## Functions (115 that TinyMUSH lacks)

### High Value

| Function | What It Does |
|----------|-------------|
| **SQL result sets** (rserror, rsnext, rsrec, rsrows, rsrecnext, rsrecprev, rsprev, rsrelease) | Cursor-based SQL query API — iterate results without loading all into memory |
| **sha1()** | SHA-1 hash — useful for data integrity, passwords, deduplication |
| **digest()** | Generalized hash function (SHA-256, SHA-512, etc.) |
| **crc32()** | Fast checksum |
| **chr() / ord()** | Unicode codepoint ↔ character conversion |
| **colordepth()** | Per-connection color capability detection |
| **moniker()** | ANSI-decorated display name |
| **pack() / unpack()** | Multi-radix (2-64) binary encoding |
| **JSON** (isjson, json, json_query, json_mod) | JSON construction, querying, and modification via SQLite JSON1 |
| **WebSocket** (wsjson, wshtml) | WebSocket output functions — TinyMUSH has no WebSocket support |
| **Connection logging** (connlog, addrlog) | SQLite-based connection audit trail |
| **encode64() / decode64()** | Base64 encoding/decoding |
| **hmac()** | Keyed-hash message authentication |
| **printf()** | ANSI-aware formatted output with field widths |
| **letq()** | Scoped named registers |
| **sortkey()** | Sort by computed key |
| **strdistance()** | Levenshtein edit distance |
| **lockencode() / lockdecode()** | Lock serialization |
| **dynhelp()** | Dynamic help from object attributes |
| **reglattr() / reglattrp()** | Regex attribute matching |
| **@cron / @crondel / @crontab** | Vixie-style cron scheduling |

### Medium Value

| Function | What It Does |
|----------|-------------|
| **iadd/isub/imul/idiv/iabs** | 64-bit integer math (no float conversion) |
| **if()** | Conditional (TinyMUSH uses ifelse) |
| **baseconv()** | Base conversion (2-36) |
| **lmath()** | Apply math operation to entire list |
| **pickrand()** | Pick random element from list |
| **distribute()** | Distribute value across bins |
| **wrap()** | Word-wrap text to width |
| **tr()** | Translate characters (like Unix tr) |
| **strip()** | Strip specific characters |
| **stripaccents()** | Remove diacritical marks |

### Comsys / Channel Functions

TinyMUSH's comsys is a module with minimal softcode access. MUX has:

- `channels()` — list channels
- `cemit()` — emit to channel
- `cwho()` — who's on channel
- `comalias()` — channel aliases
- `comtitle()` — channel titles
- `chanobj()` — channel object

### Zone Functions

- `zchildren()`, `zexits()`, `zrooms()`, `zthings()` — enumerate zone
  contents by type

### Reality Levels

- `hasrxlevel()`, `hastxlevel()`, `rxlevel()`, `txlevel()`,
  `listrlevels()` — full reality level API (TinyMUSH has none)

### Time Formatting

- `digittime()`, `singletime()`, `exptime()`, `writetime()` — multiple
  human-readable time formats
- `restartsecs()`, `startsecs()` — server uptime in seconds

---

## Commands

### @restart (connection-preserving)

Already discussed above. TinyMUSH's is disruptive; MUX's is seamless.

### Omega Converter

MUX ships Omega, a cross-format flatfile converter supporting T5X, T6H, P6H,
and R7H formats with full color fidelity. TinyMUSH has no equivalent offline
conversion tool.

---

## Summary

The gap between MUX and TinyMUSH has widened significantly with 2.14. Beyond
the existing architectural advantages — SQLite storage, GANL networking,
UTF-8/NFC, connection-preserving @restart — MUX now has web integration
features (JSON, WebSocket, base64) that TinyMUSH completely lacks, plus
architectural advances that have no TinyMUSH counterpart: an AST-based
expression evaluator with parse caching, a three-layer driver/engine/library
split communicating through COM interfaces, and PCRE2 with JIT support.
TinyMUSH's LMDB backend and module system are genuine improvements over its
older architecture, but the scope of MUX's 2.14 changes — 489 smoke tests
covering the expanded feature set — puts it on a fundamentally different
trajectory.
