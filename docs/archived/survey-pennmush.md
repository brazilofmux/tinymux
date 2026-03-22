# PennMUSH Survey

Surveyed: /tmp/pennmush (2026-03-16, updated from 2026-03-06)
Source: PennMUSH 1.8.8p0 (Apr 20 2020, last released version)
Purpose: Identify features, patterns, or ideas worth borrowing for TinyMUX.

## Verdict

Significantly more interesting than TinyMUSH. Penn took a different
architectural path and has several mature features MUX previously lacked: JSON
support, WebSockets, connection logging, an HTTP server, and base64 encoding.

**Update (2.14):** MUX has now implemented all Tier 1 items from this survey:
JSON, WebSockets, base64, HMAC, connection logging, plus Penn-inspired letq(),
sortkey(), and regex attribute matching. The remaining gaps are Penn's built-in
HTTP server and GMCP/MSSP telnet protocol support.

**Update (2026-03-16):** No new PennMUSH releases since the 2026-03-06 survey
(1.8.8p1 remains unreleased). Added newly identified items: GMCP support,
MSSP, oob(), spellnum()/ordinal(), benchmark(), OpenBSD pledge() sandboxing,
SSL slave process architecture, and color handling comparison. Updated
architecture table for MUX's PUA 24-bit color redesign and Unicode 16.0/DUCET
advances.

---

## High Priority—Worth Implementing

### 1. JSON Support (funjson.c, jsontypes.c, cJSON)

Penn has comprehensive JSON manipulation:

- `json(<type>, <value>)` -- construct JSON values
- `json_query(<json>, <path>)` -- query with JSONPath-like syntax
- `json_mod(<json>, <op>, <path>, <value>)` -- set/insert/replace
- `json_map(<json>, <ufun>)` -- iterate with user function
- `isjson(<string>)` -- validate JSON

Implementation uses cJSON library for construction and SQLite JSON1 extension
for queries. Handles UTF-8 escaping, all JSON types.

**Worth borrowing?** Yes. JSON is the lingua franca of web APIs. Games with
web portals, Discord bots, or external integrations need this. MUX already has
SQLite with JSON1—could leverage that for queries and add softcode
functions on top.

**Status:** Implemented in MUX 2.14. isjson() is a native RFC 8259 parser;
json(), json_query(), json_mod() use SQLite JSON1.

### 2. WebSocket Support (websock.c)

RFC 6455 compliant WebSocket implementation:

- HTTP upgrade handshake with SHA-1 verification
- Text and binary frame support
- Multiple output channels (text, JSON, HTML, prompt)
- `wsjson()` and `wshtml()` softcode functions

**Worth borrowing?** Yes. WebSocket support is essential for modern web
clients. Every serious MU* web client (Evennia, Ares) uses WebSockets. Without
this, MUX games are limited to telnet clients or proxy servers.

**Status:** Implemented in MUX 2.14. RFC 6455 with same-port auto-detection
and wss:// (WebSocket over TLS) via deferred protocol detection.

### 3. Connection Logging (connlog.c)

SQLite-based connection audit trail:

- Separate database (`connlog_db`)
- Tracks: player, IP, hostname, SSL, WebSocket, connect/disconnect times
- R-Tree index for efficient time-range queries
- `connlog()` and `addrlog()` softcode functions for querying
- WAL mode, automatic checkpointing

**Worth borrowing?** Yes. Security/audit logging that's queryable from
softcode. MUX logs connections to text files which are hard to query. With
SQLite already in the stack, this is straightforward.

**Status:** Implemented in MUX 2.14. SQLite connlog table, connlog()/addrlog()
softcode functions, INSERT on connect, UPDATE on disconnect.

### 4. Base64 Functions (encode64/decode64)

`encode64(<string>)` and `decode64(<string>)` -- standard base64
encoding/decoding.

**Worth borrowing?** Yes. Small, useful, easy to implement. Needed for any
HTTP/API integration work.

**Status:** Implemented in MUX 2.14.

### 5. HMAC Function

`hmac(<message>, <key>, <algorithm>)` -- keyed-hash message authentication
code.

**Worth borrowing?** Yes, if doing webhook verification or API auth. Goes with
the JSON/HTTP theme.

**Status:** Implemented in MUX 2.14. hmac() via OpenSSL, default sha256, any
digest() algorithm.

---

## Medium Priority—Interesting Ideas

### 6. Built-in HTTP Server

Penn 1.8.8 added an HTTP server. Commands:

- `@http` -- handle HTTP requests
- `@respond` -- set response code/headers
- `formdecode()` -- decode form/path data

This allows MU* objects to serve web pages and API endpoints directly.
Implementation is substantial: ~300 lines in bsd.c handling HTTP request
parsing, method dispatch, and response generation. Connections with
`CONN_HTTP_REQUEST` flag get routed through `do_http_command()`.

**Worth borrowing?** Maybe. Ambitious but powerful. Combined with JSON and
WebSockets, this makes the MU* a self-contained web application server. Risk:
attack surface, complexity.

### 7. GMCP Support (bsd.c)

Generic MUD Communication Protocol (telnet option 201):

- Telnet negotiation via `TELNET_HANDLER(telnet_gmcp)`
- Registered handler system: `register_gmcp_handler(package, func)`
- Built-in handlers: `Core.Hello` (client identification), `Core.Ping`
- `oob(<player>, <package>[, <json>])` softcode function to send GMCP data
- cJSON used for parsing/generating GMCP payloads
- Example softcode handler pattern that triggers attributes on #0

GMCP is the modern standard for out-of-band MU* data (room info, character
stats, map data). Supported by Mudlet, Mushclient, and web clients.

**Worth borrowing?** Yes. GMCP is increasingly expected by modern MU* clients.
The handler registration pattern is clean and extensible. MUX currently has no
GMCP or OOB data support.

### 8. MSSP Support (bsd.c)

MUD Server Status Protocol (telnet option 70):

- Responds to MSSP-REQUEST with server metadata
- Configurable key/value pairs via `struct mssp` linked list
- Used by MU* crawlers/directories for automatic server discovery

**Worth borrowing?** Low effort, high visibility. MSSP lets MU* directories
auto-discover your game's name, player count, codebase, etc. Simple telnet
subnegotiation, maybe 50 lines of code.

### 9. letq()—Scoped Named Registers

`letq(<name>, <value>, ..., <expression>)` -- bind named registers for the
duration of an expression, then automatically unset them.

MUX has `setq(name, val)` but no scoping—registers persist until
overwritten. `letq()` prevents register pollution in nested calls.

**Worth borrowing?** Good idea. Clean, safe, reduces debugging headaches for
softcoders working with nested u() calls.

**Status:** Implemented in MUX 2.14.

### 10. sortkey()—Sort by Computed Key

`sortkey(<ufun>, <list>)` -- sort a list where the sort key is computed by a
user function applied to each element. Avoids the Schwartzian transform
pattern that MUSH coders currently do manually.

**Worth borrowing?** Nice quality-of-life for softcoders. Simple to implement
on top of existing sort infrastructure.

**Status:** Implemented in MUX 2.14.

### 11. Nospoof Emit Variants (nsoemit, nspemit, nsremit, nszemit, etc.)

Full set of nospoof-aware emit functions that prepend source identification to
prevent spoofing.

MUX has some nospoof support but Penn's coverage is more complete.

**Worth borrowing?** Low effort to fill gaps if any exist in MUX's set.

### 12. PCG Random Number Generator (pcg_basic.c)

Penn uses PCG (Permuted Congruential Generator):

- 64-bit state, 32-bit output
- Better statistical properties than LCG
- Multiple independent streams
- Faster than Mersenne Twister, smaller state

**Worth borrowing?** MUX uses its own RandomINT32. If the current RNG has
known statistical weaknesses, PCG is a good modern replacement. Low priority
unless someone finds bias in the current generator.

**Status:** Implemented in MUX 2.14—PCG-XSL-RR-128/64 (pcg64), replacing
Mersenne Twister.

### 13. Spell Suggestion (spellfix.c, suggest())

`suggest(<word>, <dictionary>)` -- phonetic spell correction using SQLite's
spellfix1 virtual table.

**Worth borrowing?** Cute but niche. Could be useful for "did you mean X?" on
typo'd commands. Low priority.

### 14. Regex Attribute Matching (reglattr, regnattr, regxattr, etc.)

Full suite of regex-based attribute name matching:

- `reglattr(<obj>, <pattern>)` -- list attrs matching regex
- `regnattr(<obj>, <pattern>)` -- count attrs matching regex
- `regxattr(<obj>, <pattern>)` -- list attrs+values matching regex

**Worth borrowing?** Useful for code introspection. MUX has `lattr()` with
wildcards but no regex variant.

**Status:** Implemented in MUX 2.14—reglattr()/reglattrp() via PCRE2.

### 15. Extended Lock Types

Penn has 30+ lock types vs MUX's ~12. Notable additions:

- `Follow` -- who can follow this player
- `Receive` -- who can give objects to you
- `Examine` -- who can examine this object
- `Control` -- who can control this object (beyond ownership)
- `Destroy` -- who can destroy this object
- `Interact` -- who can interact with this object
- `MailForward` -- mail forwarding lock
- `Chown` -- who can @chown this object
- `Filter`/`InFilter` -- message filtering locks

**Worth borrowing?** Selectively. `Control` and `Examine` locks are genuinely
useful for delegation. Others are niche.

---

## Lower Priority

### 16. Timezone Support (tz.c)

IANA timezone database support. Reads system tzdata files, validates timezone
names. Players can set TZ attribute for localized times.

**Worth borrowing?** Nice for international games. MUX has basic UTC offset
support. Full tz database is more correct for DST handling.

### 17. spellnum() / ordinal()—Number to Words

`spellnum(<number>)` converts integers and decimals to English words (e.g.,
`spellnum(42)` returns "forty-two"). `ordinal(<number>)` returns ordinal form
("forty-second"). Handles negatives, decimals to hundred-trillionths, numbers
up to 999,999,999,999,999.

**Worth borrowing?** Niche but charming for RP-oriented games. ~120 lines of C.
MUX has nothing equivalent.

### 18. benchmark()—Expression Timing

`benchmark(<expression>, <count>[, <sendto>])` -- evaluates an expression N
times and reports min/max/total timing using TSC (timestamp counter). Useful
for softcode performance tuning.

**Worth borrowing?** Useful for power users and developers. MUX has no
equivalent softcode profiling tool.

### 19. Chunk Memory Allocator (chunk.c)

Custom allocator: 64KB regions, 3 size classes, deref-based migration for
cache optimization. Reduces fragmentation for many small allocations.

**Worth borrowing?** No. Modern malloc (jemalloc, tcmalloc) handles this
better. This was designed when system allocators were worse.

### 20. Database Tools (dbtools/)

C++ offline tools: `dbupgrade`, `grepdb`, `pwutil`, `db2dot` (Graphviz
visualization of room topology).

**Worth borrowing?** The `db2dot` map visualization is clever. The rest is
covered by MUX's dbconvert. Low priority.

### 21. Object Warning System (warnings.c)

Automated checking for topology problems, invalid locks, orphaned objects,
circular parent chains. Warnings displayed to object owners.

**Worth borrowing?** Moderately useful for game maintenance. MUX has `@dbck`
but no per-object warning system.

### 22. Test Framework

Penn has both hardcode tests (C, run at startup via `--tests`/`--only-tests`
flags) and softcode tests (Perl harness with `runtest.pl`, 35 test files,
Valgrind integration). The hardcode framework uses `TEST_GROUP()` and `TEST()`
macros embedded directly in source files, with dependency tracking between test
groups. Softcode tests connect to the game via Perl `MUSHConnection` module and
match output against regexes.

**Worth borrowing?** MUX's smoke test infrastructure (593 test cases across 185
suites as of 2026-03-12) is comparable in scale. The Perl harness approach with
regex matching is more flexible for output validation than MUX's expect-based
approach, but MUX's system is well-established.

### 23. SSL Slave Process (ssl_slave.c, ssl_master.c)

Penn uses a separate ssl_slave process (built on libevent) to handle SSL/TLS
connections. The slave accepts SSL connections, does the TLS handshake and
hostname lookup, then proxies the plaintext over a unix socket to the main
process. Uses kqueue on BSDs to detect parent crash.

**Worth borrowing?** No. MUX's integrated OpenSSL approach is simpler. The
slave pattern was designed to isolate SSL complexity from the main event loop,
but MUX's GANL networking already handles this cleanly. Penn's ssl_slave
requires libevent as an additional dependency.

### 24. OpenBSD pledge() Sandboxing

Penn uses `pledge(2)` on OpenBSD to restrict process privileges:

- netmush: limited to stdio, rpath, wpath, cpath, inet, unix, dns, proc,
  exec, flock, fattr
- info_slave: limited to stdio, proc, flock, inet, dns
- ssl_slave: similar restrictions

**Worth borrowing?** Interesting security hardening, but OpenBSD-only.
Equivalent on Linux would be seccomp-bpf or landlock, which are more complex.
Low priority unless targeting OpenBSD specifically.

---

## Not Worth Borrowing

### Pueblo HTML Support

Dead protocol. No modern clients support it.

### Commerce System (BUY/COST/PAYMENT attributes)

Built-in economy hooks. Games that want economies build their own in softcode
with much more flexibility.

### Attribute Trees (backtick notation)

Hierarchical attributes via `ATTR\`FOO\`BAR` syntax. Adds complexity to the
attribute system for marginal benefit. Flat namespaces with dot-separated
conventions (SYSTEM.CONFIG.FOO) work fine.

### Dynamic @COMMAND/@FUNCTION Creation

Runtime creation of commands/functions from softcode. MUX's @addcommand and
@function already cover this.

### speak() Function

Formats speech output with configurable say/pose tokens. Trivially done in
softcode.

---

## Function Delta Summary

**PennMUSH has ~246 functions MUX doesn't.** Major categories:

| Category | Count | Examples | Interest |
|----------|-------|---------|----------|
| JSON | 5 | json, json_query, json_mod, json_map, isjson | Done |
| WebSocket | 2 | wsjson, wshtml | Done |
| Connection info | 3 | connlog, addrlog, ipaddr | Done |
| Base64/crypto | 4 | encode64, decode64, hmac, checkpass | Done |
| Nospoof emits | 8 | nspemit, nsoemit, nsremit, nszemit... | Medium |
| Lock queries | 6 | llocks, lockflags, lockowner, testlock... | Medium |
| Regex attr | 8 | reglattr, regnattr, regxattr... | Done |
| OOB/GMCP | 1 | oob | Medium |
| Object counting | 12 | nchildren, nexits, nplayers, nthings... | Low |
| Channel variants | 10 | cbuffer, cdesc, cflags, cmsgs... | Low |
| Mail variants | 8 | maillist, mailsend, mailstats... | Low |
| Formatting | 5 | lalign, render, strinsert, strreplace... | Low |
| Control flow | 8 | letq, cond, condall, firstof, allof... | Done (letq) |
| Math/conversion | 6 | spellnum, ordinal, baseconv, bound, mean, stddev | Low |
| Profiling | 1 | benchmark | Low |
| Misc | ~159 | Various | Low |

**MUX has ~114 functions Penn doesn't.** Notable MUX advantages:

- Color depth (colordepth, chr, ord, moniker)
- Reality levels (hasrxlevel, hastxlevel, rxlevel, txlevel, listrlevels)
- SQL result sets (rserror, rsnext, rsrec, rsrows...)
- Math (cos, sin, exp, ln, log, pi, e, floor, fmod)
- Comsys specifics (comalias, comtitle, chanobj)
- Pack/unpack (binary data)
- Time variants (digittime, singletime, exptime, writetime)

---

## Color Handling Comparison (New Section, 2026-03-16)

Penn and MUX have significantly different approaches to color:

### Penn's Approach (markup.c, ansi.h)

- Internal markup system using TAG_START/TAG_END (0x02/0x03) delimiters
- `ansi_data` struct: bits/offbits for styles, fg/bg as color name strings
- Color name database in SQLite (loaded from `game/txt/colors.json`)
  - Stores name, RGB hex, xterm index, 16-color ANSI code per entry
  - Nearest-neighbor 256-color downgrade via brute-force scan of xterm colors
  - `colorname_lookup()` and `rgb_lookup()` use prepared statements
- **Output modes:** None, Hilite-only, 16-color, Xterm-256, HTML
- **No 24-bit truecolor output:** Penn stores 24-bit internally (via `#RRGGBB`
  or `<R G B>` syntax in ansi_data) but always downgrades to 256-color on
  output. `ANSI_FORMAT_XTERM256` is the maximum output fidelity.
- `ansi_string` type: parsed representation with per-character markup indices,
  supports insert/delete/replace/scramble operations on styled text

### MUX's Approach

- 5-channel color model (V5): foreground, background, underline-color,
  strikethrough-color, plus style bits
- Per-channel encoding: 16-color, 256-color, or 24-bit RGB
- Unicode PUA encoding for 24-bit colors (redesigned 2026-03-15): fixed
  2-code-point encoding per channel, not per-channel deltas
- **Output modes:** None, 16-color, 256-color, 24-bit truecolor
- CIELAB nearest-neighbor for palette downgrades (perceptually uniform)
- DFA-based color state machine (generated tables)

### Assessment
MUX's color system is now substantially more advanced than Penn's:

- MUX outputs true 24-bit color; Penn caps at 256
- MUX uses perceptually uniform CIELAB for downgrades; Penn uses RGB distance
- MUX has 5 independent color channels; Penn has 2 (fg/bg) plus style bits
- Penn's SQLite color name database is elegant but slower than MUX's compiled
  tables

---

## Unicode Comparison (New Section, 2026-03-16)

### Penn's Unicode Support

- Historical Latin-1 base with UTF-8 conversion layer (charconv.c)
- Optional ICU dependency for: NFC/NFD/NFKC/NFKD normalization, proper
  case folding (`lcstr2`/`ucstr2`), transliteration (`stripaccents`)
- Without ICU: bundled subset of ICU headers (punicode/) for basic UTF-8
  encode/decode macros only—no normalization, no case mapping, no width
- `ansi_strlen()` counts characters, not bytes, but no grapheme cluster
  awareness
- No Unicode collation (DUCET or otherwise)
- No East Asian width tables for console column counting

### MUX's Unicode Support

- Native UTF-8 throughout (no Latin-1 legacy layer)
- Built-in NFC normalization (DFA-based, 129-state table from Unicode 16.0)
- DUCET collation with proper multi-level sort keys
- Grapheme cluster segmentation for display width
- East Asian width tables (DFA-based) for accurate console column counting
- Full case mapping tables (generated)
- No external ICU dependency

### Assessment
MUX is significantly ahead. Penn's Unicode is bolted onto a Latin-1 core and
requires an external library (ICU) for features MUX handles natively. Penn has
no grapheme clusters, no DUCET collation, and no display width tables.

---

## Architecture Comparison

| Area | PennMUSH | TinyMUX |
|------|----------|---------|
| Storage | Flatfile + chunk allocator | SQLite (write-through) |
| Networking | select() | GANL (epoll/kqueue) |
| Color | 16/256 output, 24-bit internal | 16/256/24-bit output, PUA encoding |
| Color downgrade | RGB distance, SQLite lookup | CIELAB perceptual distance |
| Unicode | Latin-1 base, optional ICU | Full UTF-8, NFC, DUCET, graphemes |
| Unicode version | ICU-dependent (varies) | Unicode 16.0 (built-in tables) |
| SSL | OpenSSL via slave process | OpenSSL integrated |
| RNG | PCG (32-bit output) | PCG-XSL-RR-128/64 (64-bit output) |
| Regex | PCRE2 | PCRE2 |
| SQL | SQLite + MySQL + PostgreSQL | SQLite |
| JSON | cJSON + SQLite JSON1 | isjson + SQLite JSON1 |
| WebSocket | RFC 6455 | RFC 6455 (same-port) |
| HTTP | Built-in server | None |
| GMCP | Telnet 201, handler registry | None |
| MSSP | Telnet 70 | None |
| Evaluator | Token-based | AST + Ragel scanner + LRU cache |
| Architecture | Monolithic | Three-layer (libmux/engine/driver) |
| Sandboxing | OpenBSD pledge() | None |
| Build | Autoconf | Autoconf |
| Tests | C framework + Perl harness (35 files) | Expect smoke tests (185 suites, 593 cases) |

---

## Bottom Line

Penn is the most interesting of the three servers to survey. The JSON,
WebSocket, and HTTP features represent a genuine architectural vision: turning
the MU* into a web-native application server.

**Update (2.14):** MUX has now cherry-picked all the high-value items
identified here: JSON (isjson + SQLite JSON1), WebSockets (RFC 6455,
same-port), connection logging (SQLite connlog), base64, HMAC, letq(),
sortkey(), regex attribute matching (reglattr/reglattrp via PCRE2), and PCG
RNG. MUX also went beyond the survey with an AST evaluator (Ragel scanner +
LRU parse cache) and a three-layer architecture split (libmux/engine/driver).

**Update (2026-03-16):** MUX now surpasses Penn in two areas the original
survey rated as comparable: color handling (24-bit truecolor output with
CIELAB perceptual downgrades vs Penn's 256-color cap with RGB distance) and
Unicode support (built-in NFC, DUCET, grapheme clusters, Unicode 16.0 vs
Penn's optional ICU dependency with no collation or width tables).

The remaining actionable gaps are:

1. **GMCP/MSSP** (medium priority)—modern MU* client protocols, ~200 lines
2. **Built-in HTTP server** (lower priority)—ambitious, significant attack surface
3. **spellnum()/benchmark()** (low priority)—nice-to-have softcode functions

The function library differences are now mostly noise—Penn has more variants
of existing concepts (nspemit vs pemit) rather than fundamentally different
capabilities.

Next survey: RhostMUSH (expected to be the largest, most divergent).
