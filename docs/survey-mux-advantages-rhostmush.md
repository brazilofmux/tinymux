# What TinyMUX Has That RhostMUSH Could Use

Reverse survey: TinyMUX advantages relevant to RhostMUSH. Companion to
docs/survey-rhostmush.md.

---

Rhost has the most features of any MU* server by raw count. But features are
not architecture. MUX's advantages are structural — the kind of changes that
require rethinking foundations, not adding functions.

## Architecture

### 1. SQLite Write-Through vs QDBM + Cache + Flatfile Dumps

Rhost uses QDBM (not GDBM) — a reimplementation of the DBM interface with
better space efficiency and performance. On top of QDBM, Rhost has a
two-level cache architecture:

- **QDBM index layer:** Maps object ID to (file-offset, size) in a separate
  `.db` data file. QDBM stores the index, not the objects themselves.
- **LRU object cache:** 1024-bucket hash table with four chains per bucket
  (active, modified-active, old, modified-old). Objects are promoted on access
  and dirty-tracked separately.
- **LRU attribute cache:** Similar structure, 4096-wide hash table, caches
  individual attributes independently of their parent objects.
- **A_LIST management:** In GDBM compatibility mode, Rhost keeps A_LIST
  (the attribute name directory) under 4000 characters. QDBM mode supports
  up to 10,000 attributes per object, hard-limited and configurable via
  VLIMIT.
- **Attributes packed in object blobs:** Each object is serialized with all
  its attributes as one unit. Adding an attribute means rewriting the blob.

This is real engineering — the cache hit rates are good and the
active/modified split means dirty objects get priority treatment. But the
fundamental constraints remain:

- **Crash vulnerability** — changes since last fork+dump are lost. No WAL,
  no journal, no transaction log.
- **Blob relocation** — adding an attribute to an object rewrites the entire
  serialized blob. If the new blob is larger, it relocates to the end of the
  data file. Under concurrent mutations, abandoned holes get claimed by other
  objects and the file grows monotonically until a flatfile dump/reload
  compacts it.
- **No indexed queries** — @search scans every object linearly.
- **No standard tooling** — the `.db`/`.dir`/`.pag` files are opaque binary.

MUX's SQLite write-through:

- Every mutation (s_Location, s_Owner, atr_add) writes through immediately
- Individual attributes are separate rows — no blob relocation
- @dump is a WAL checkpoint only — sub-millisecond, no fork
- @search routes to indexed SQL queries
- Database inspectable with `sqlite3` CLI, standard backup tools
- Comsys and mail in the same database — unified backup

**Impact:** MUX eliminates both the crash-durability gap and the
blob-relocation pathology. On a 100k-object game, indexed @search is orders
of magnitude faster. But credit where due — Rhost's cache layer means most
operations never touch QDBM at all during normal play.

### 2. GANL Networking vs BSD select()

Rhost uses raw BSD sockets with `select()`. MUX's GANL provides:

- epoll (Linux) — O(1) event delivery
- kqueue (BSD/macOS) — O(1) event delivery
- select fallback — portability
- Factory pattern — runtime engine selection

Rhost has added WebSocket support on top of raw BSD sockets, which works but
doesn't address the fundamental scalability issue.

**Impact:** Better performance under load. select() scans all fds every
iteration.

### 3. @restart / @reboot — Connection Preservation

Both Rhost and MUX preserve player connections across reboot. The mechanism is
the same: serialize descriptor state to a restart file (`restart.db` /
`reboot.db`), `exec()` the new binary, reload descriptors on startup. MUX has
an extra step — GANL deregisters fds from epoll/kqueue before the exec —
but this is an implementation detail, not a different approach.

**Impact:** Parity — both servers handle this well.

---

## Unicode

### 4. Full UTF-8 with NFC Normalization

Rhost has partial UTF-8 support (`TOG_UTF8` toggle, `isunicode()`, `isutf8()`,
`codepoint()` functions) but lacks:

- **NFC normalization** — é (precomposed) and e+◌́ (decomposed) are
  stored as different byte sequences, causing matching failures
- **DFA-based classification** — MUX uses optimized state machines for
  Unicode properties (314 states for printable, 132 for CCC, etc.)
- **Canonical Combining Class tracking** — needed for correct
  normalization of combining character sequences
- **CJK width-aware operations** — ljust/rjust/center respect
  double-width characters
- **Generated pipeline** — MUX's tables are built from Unicode data
  files via reproducible tools, not hand-coded

MUX normalizes at storage time (`atr_add_raw_LEN()`). Every attribute value is
in NFC. Rhost stores whatever bytes arrive.

**Impact:** International games with Korean, Japanese, Chinese, or accented
European text will encounter character equivalence bugs in Rhost. MUX
eliminates the class.

---

## Functions (106 that RhostMUSH lacks)

Despite Rhost having 592 functions to MUX's 388, there are 106 functions MUX
has that Rhost doesn't. Several are genuinely useful.

### SQL Result Set API

Rhost has `sqlite_query()` which returns a flat string. MUX has a cursor-based
API:

| Function | Description |
|----------|-------------|
| sql() | Execute query |
| rserror() | Last error |
| rsrec() | Current row |
| rsnext() / rsprev() | Navigate cursor |
| rsrecnext() / rsrecprev() | Fetch + navigate |
| rsrows() | Row count |
| rsrelease() | Free result set |

**Impact:** Iterate large result sets without buffering everything. Rhost's
sqlite_query() is limited by LBUF size.

### Comsys Functions

Rhost has a comsys system but limited softcode access. MUX has:

- `channels()` — list all channels
- `cemit()` — emit to channel from softcode
- `cwho()` — list channel members
- `comalias()` — get channel aliases
- `comtitle()` — get/set titles
- `chanobj()` — get channel object

**Impact:** Enables softcode-driven channel management systems.

### Cryptographic Functions

| Function | Description |
|----------|-------------|
| sha1() | SHA-1 hash |
| digest() | Multiple hash algorithms |
| crc32() | Fast checksum |

Rhost has `crc32()` and `crc32obj()` but not sha1() or digest().

### Integer Math

| Function | Description |
|----------|-------------|
| iadd() | 64-bit integer addition (multi-arg) |
| isub() | 64-bit integer subtraction |
| imul() | 64-bit integer multiplication |
| idiv() | 64-bit integer division |
| iabs() | 64-bit absolute value |
| isign() | Sign of integer |
| israt() | Is rational number? |

Rhost has `div()` but not the full 64-bit integer family. MUX's integer
functions avoid floating-point conversion, preserving precision to ±9.2
quintillion.

### Regex Functions

MUX has a full regex function suite that Rhost lacks:

- `regedit()` / `regeditall()` / `regediti()` / `regeditalli()`
- `regmatch()` / `regmatchi()`
- `regrab()` / `regraball()` / `regrabi()` / `regraballi()`

Rhost has `grep` but not the regex editing/grabbing family. These are
essential for sophisticated text processing.

### Other Notable Functions

| Function | Description |
|----------|-------------|
| colordepth() | Per-connection color capability detection |
| moniker() | ANSI-decorated object name |
| matchall() | All matching positions in list |
| filterbool() | Filter list by boolean expression |
| grepi() | Case-insensitive grep |
| lmath() | Apply math op to entire list |
| pickrand() | Random list element |
| distribute() | Distribute value across bins |
| if() | Simple conditional (Rhost uses ifelse only) |
| itemize() | Oxford comma list formatting |
| height() / width() | Terminal dimensions |
| terminfo() | Terminal capability info |
| host() | Connection hostname |
| ports() / lports() | Port information |
| mail() / mailfrom() | Mail query functions |

### Zone Content Functions

- `zchildren()`, `zexits()`, `zrooms()`, `zthings()` — enumerate zone
  contents by type
- `zone()` — get zone of object

Rhost has `zlcon()`, `zsearch()`, `lzone()` but lacks the type-specific zone
enumeration.

---

## Omega Converter

MUX's Omega converter handles T5X ↔ T6H ↔ P6H ↔ R7H conversions. The
recent direct T5X — R7H path preserves full 24-bit color by translating
MUX's private-use Unicode color code points into Rhost's `%c<#RRGGBB>`
softcode.

Rhost has no equivalent conversion tool. A game wanting to migrate to or from
Rhost depends on MUX's Omega.

---

## Build and Test Infrastructure

### Autoconf + Reproducible Build

MUX uses standard autoconf with well-defined configure options. Rhost uses a
menu-driven `make confsource` system that's less scriptable and harder to
integrate into CI/CD.

### Smoke Tests

MUX has 348 automated test cases covering functions, Unicode, sort behavior,
and regressions. Tests run via `./tools/Makesmoke && ./tools/Smoke` with
deterministic pass/fail.

Rhost has no comparable automated test suite.

---

## Summary

Rhost's advantages over MUX are in feature breadth: more functions, more
flags, more toggles, more config options, Lua scripting, MySQL support. These
are additive features.

MUX's advantages over Rhost are in engineering depth: SQLite write-through
(crash durability), GANL networking (scalability), full
UTF-8/NFC (correctness), indexed @search (performance), cursor-based SQL API
(capability), regex functions (text processing), and Omega converter
(portability).

Breadth is easy to add. Depth is hard to retrofit.
