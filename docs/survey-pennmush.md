# PennMUSH Survey

Surveyed: /tmp/pennmush (2026-03-06)
Purpose: Identify features, patterns, or ideas worth borrowing for TinyMUX.

## Verdict

Significantly more interesting than TinyMUSH. Penn took a different
architectural path and has several mature features MUX lacks entirely:
JSON support, WebSockets, connection logging, an HTTP server, and
base64 encoding. The function library is ~30% larger (520 vs 388).
Several items are worth serious consideration.

---

## High Priority — Worth Implementing

### 1. JSON Support (funjson.c, jsontypes.c, cJSON)

Penn has comprehensive JSON manipulation:
- `json(<type>, <value>)` — construct JSON values
- `json_query(<json>, <path>)` — query with JSONPath-like syntax
- `json_mod(<json>, <op>, <path>, <value>)` — set/insert/replace
- `json_map(<json>, <ufun>)` — iterate with user function
- `isjson(<string>)` — validate JSON

Implementation uses cJSON library for construction and SQLite JSON1
extension for queries. Handles UTF-8 escaping, all JSON types.

**Worth borrowing?** Yes. JSON is the lingua franca of web APIs. Games
with web portals, Discord bots, or external integrations need this.
MUX already has SQLite with JSON1 — could leverage that for queries
and add softcode functions on top.

### 2. WebSocket Support (websock.c)

RFC 6455 compliant WebSocket implementation:
- HTTP upgrade handshake with SHA-1 verification
- Text and binary frame support
- Multiple output channels (text, JSON, HTML, prompt)
- `wsjson()` and `wshtml()` softcode functions

**Worth borrowing?** Yes. WebSocket support is essential for modern web
clients. Every serious MU* web client (Evennia, Ares) uses WebSockets.
Without this, MUX games are limited to telnet clients or proxy servers.

### 3. Connection Logging (connlog.c)

SQLite-based connection audit trail:
- Separate database (`connlog_db`)
- Tracks: player, IP, hostname, SSL, WebSocket, connect/disconnect times
- R-Tree index for efficient time-range queries
- `connlog()` and `addrlog()` softcode functions for querying
- WAL mode, automatic checkpointing

**Worth borrowing?** Yes. Security/audit logging that's queryable from
softcode. MUX logs connections to text files which are hard to query.
With SQLite already in the stack, this is straightforward.

### 4. Base64 Functions (encode64/decode64)

`encode64(<string>)` and `decode64(<string>)` — standard base64
encoding/decoding.

**Worth borrowing?** Yes. Small, useful, easy to implement. Needed for
any HTTP/API integration work.

### 5. HMAC Function

`hmac(<message>, <key>, <algorithm>)` — keyed-hash message
authentication code.

**Worth borrowing?** Yes, if doing webhook verification or API auth.
Goes with the JSON/HTTP theme.

---

## Medium Priority — Interesting Ideas

### 6. Built-in HTTP Server

Penn 1.8.8 added an HTTP server. Commands:
- `@http` — handle HTTP requests
- `@respond` — set response code/headers
- `formdecode()` — decode form/path data

This allows MU* objects to serve web pages and API endpoints directly.

**Worth borrowing?** Maybe. Ambitious but powerful. Combined with JSON
and WebSockets, this makes the MU* a self-contained web application
server. Risk: attack surface, complexity.

### 7. letq() — Scoped Named Registers

`letq(<name>, <value>, ..., <expression>)` — bind named registers
for the duration of an expression, then automatically unset them.

MUX has `setq(name, val)` but no scoping — registers persist until
overwritten. `letq()` prevents register pollution in nested calls.

**Worth borrowing?** Good idea. Clean, safe, reduces debugging headaches
for softcoders working with nested u() calls.

### 8. sortkey() — Sort by Computed Key

`sortkey(<ufun>, <list>)` — sort a list where the sort key is computed
by a user function applied to each element. Avoids the Schwartzian
transform pattern that MUSH coders currently do manually.

**Worth borrowing?** Nice quality-of-life for softcoders. Simple to
implement on top of existing sort infrastructure.

### 9. Nospoof Emit Variants (nsoemit, nspemit, nsremit, nszemit, etc.)

Full set of nospoof-aware emit functions that prepend source
identification to prevent spoofing.

MUX has some nospoof support but Penn's coverage is more complete.

**Worth borrowing?** Low effort to fill gaps if any exist in MUX's set.

### 10. PCG Random Number Generator (pcg_basic.c)

Penn uses PCG (Permuted Congruential Generator):
- 64-bit state, 32-bit output
- Better statistical properties than LCG
- Multiple independent streams
- Faster than Mersenne Twister, smaller state

**Worth borrowing?** MUX uses its own RandomINT32. If the current RNG
has known statistical weaknesses, PCG is a good modern replacement.
Low priority unless someone finds bias in the current generator.

### 11. Spell Suggestion (spellfix.c, suggest())

`suggest(<word>, <dictionary>)` — phonetic spell correction using
SQLite's spellfix1 virtual table.

**Worth borrowing?** Cute but niche. Could be useful for "did you mean
X?" on typo'd commands. Low priority.

### 12. Regex Attribute Matching (reglattr, regnattr, regxattr, etc.)

Full suite of regex-based attribute name matching:
- `reglattr(<obj>, <pattern>)` — list attrs matching regex
- `regnattr(<obj>, <pattern>)` — count attrs matching regex
- `regxattr(<obj>, <pattern>)` — list attrs+values matching regex

**Worth borrowing?** Useful for code introspection. MUX has `lattr()`
with wildcards but no regex variant.

### 13. Extended Lock Types

Penn has 30+ lock types vs MUX's ~12. Notable additions:
- `Follow` — who can follow this player
- `Receive` — who can give objects to you
- `Examine` — who can examine this object
- `Control` — who can control this object (beyond ownership)
- `Destroy` — who can destroy this object
- `Interact` — who can interact with this object
- `MailForward` — mail forwarding lock
- `Chown` — who can @chown this object
- `Filter`/`InFilter` — message filtering locks

**Worth borrowing?** Selectively. `Control` and `Examine` locks are
genuinely useful for delegation. Others are niche.

---

## Lower Priority

### 14. Timezone Support (tz.c)

IANA timezone database support. Reads system tzdata files, validates
timezone names. Players can set TZ attribute for localized times.

**Worth borrowing?** Nice for international games. MUX has basic UTC
offset support. Full tz database is more correct for DST handling.

### 15. Chunk Memory Allocator (chunk.c)

Custom allocator: 64KB regions, 3 size classes, deref-based migration
for cache optimization. Reduces fragmentation for many small allocations.

**Worth borrowing?** No. Modern malloc (jemalloc, tcmalloc) handles
this better. This was designed when system allocators were worse.

### 16. Database Tools (dbtools/)

C++ offline tools: `dbupgrade`, `grepdb`, `pwutil`, `db2dot` (Graphviz
visualization of room topology).

**Worth borrowing?** The `db2dot` map visualization is clever. The rest
is covered by MUX's dbconvert. Low priority.

### 17. Object Warning System (warnings.c)

Automated checking for topology problems, invalid locks, orphaned
objects, circular parent chains. Warnings displayed to object owners.

**Worth borrowing?** Moderately useful for game maintenance. MUX has
`@dbck` but no per-object warning system.

### 18. Test Framework

Penn has both hardcode tests (C, run at startup) and softcode tests
(Perl harness, 28+ test files, Valgrind integration).

**Worth borrowing?** MUX's smoke test infrastructure is functional but
less sophisticated. The Perl harness approach with expected-output
matching is worth studying.

---

## Not Worth Borrowing

### Pueblo HTML Support

Dead protocol. No modern clients support it.

### Commerce System (BUY/COST/PAYMENT attributes)

Built-in economy hooks. Games that want economies build their own in
softcode with much more flexibility.

### Attribute Trees (backtick notation)

Hierarchical attributes via `ATTR\`FOO\`BAR` syntax. Adds complexity
to the attribute system for marginal benefit. Flat namespaces with
dot-separated conventions (SYSTEM.CONFIG.FOO) work fine.

### Dynamic @COMMAND/@FUNCTION Creation

Runtime creation of commands/functions from softcode. MUX's @addcommand
and @function already cover this.

### speak() Function

Formats speech output with configurable say/pose tokens. Trivially done
in softcode.

---

## Function Delta Summary

**PennMUSH has 246 functions MUX doesn't.** Major categories:

| Category | Count | Examples | Interest |
|----------|-------|---------|----------|
| JSON | 5 | json, json_query, json_mod, json_map, isjson | High |
| WebSocket | 2 | wsjson, wshtml | High |
| Connection info | 3 | connlog, addrlog, ipaddr | High |
| Base64/crypto | 4 | encode64, decode64, hmac, checkpass | High |
| Nospoof emits | 8 | nspemit, nsoemit, nsremit, nszemit... | Medium |
| Lock queries | 6 | llocks, lockflags, lockowner, testlock... | Medium |
| Regex attr | 8 | reglattr, regnattr, regxattr... | Medium |
| Object counting | 12 | nchildren, nexits, nplayers, nthings... | Low |
| Channel variants | 10 | cbuffer, cdesc, cflags, cmsgs... | Low |
| Mail variants | 8 | maillist, mailsend, mailstats... | Low |
| Formatting | 5 | lalign, render, strinsert, strreplace... | Low |
| Control flow | 8 | letq, cond, condall, firstof, allof... | Medium |
| Misc | ~167 | Various | Low |

**MUX has 114 functions Penn doesn't.** Notable MUX advantages:
- Color depth (colordepth, chr, ord, moniker)
- Reality levels (hasrxlevel, hastxlevel, rxlevel, txlevel, listrlevels)
- SQL result sets (rserror, rsnext, rsrec, rsrows...)
- Math (cos, sin, exp, ln, log, pi, e, floor, fmod)
- Comsys specifics (comalias, comtitle, chanobj)
- Pack/unpack (binary data)
- Time variants (digittime, singletime, exptime, writetime)

---

## Architecture Comparison

| Area | PennMUSH | TinyMUX |
|------|----------|---------|
| Storage | Flatfile + chunk allocator | SQLite (write-through) |
| Networking | select() | GANL (epoll/kqueue) |
| Color | 16/256/24-bit, JSON palette | 16/256/24-bit, Unicode PUA |
| Unicode | Latin-1 base, UTF-8 patches | Full UTF-8, NFC, DFA tables |
| SSL | OpenSSL with slave proxy | OpenSSL integrated |
| RNG | PCG | Custom (RandomINT32) |
| Regex | PCRE2 | PCRE |
| SQL | SQLite + MySQL + PostgreSQL | SQLite |
| JSON | cJSON + SQLite JSON1 | None |
| WebSocket | RFC 6455 | None |
| HTTP | Built-in server | None |
| Build | Autoconf | Autoconf |
| Tests | C framework + Perl harness | Expect-based smoke tests |

---

## Bottom Line

Penn is the most interesting of the three servers to survey. The JSON,
WebSocket, and HTTP features represent a genuine architectural vision:
turning the MU* into a web-native application server. MUX should
cherry-pick the high-value items (JSON, WebSockets, connection logging,
base64) while leveraging its existing SQLite infrastructure for
implementation.

The function library differences are mostly noise — Penn has more
variants of existing concepts (nspemit vs pemit, reglattr vs lattr)
rather than fundamentally different capabilities. The exceptions are
JSON, crypto, and web protocol support.

Next survey: RhostMUSH (expected to be the largest, most divergent).
