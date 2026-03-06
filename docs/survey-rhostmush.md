# RhostMUSH Survey

Surveyed: /tmp/rhostmush (2026-03-06)
Purpose: Identify features, patterns, or ideas worth borrowing for TinyMUX.

## Verdict

RhostMUSH is massive. 592 functions (vs MUX's 388), 141 @-commands, 4 flag
words, 8 toggle words, 3 power words, 3 depower words, a totem system, 549
config options, Lua scripting, MySQL, SQLite, WebSockets, doors, an account
system, and a cluster system. Built on a TinyMUSH 2.2.5 parser base that
has been expanded relentlessly for 20+ years.

Much of the bulk is combinatorial explosion (every function gets local/
default/eval variants, every system gets list/has/set functions). But
buried in the noise are genuinely useful features worth borrowing. The
challenge is separating signal from noise.

---

## High Priority — Worth Implementing

### 1. Base64 Functions (encode64/decode64)

Rhost has `encode64()` and `decode64()`. Penn has them too. MUX doesn't.
Required for any HTTP/webhook/API integration.

**Effort:** Small. Standard base64 is ~50 lines.
**Dependencies:** None.

### 2. printf() — Formatted Output

`printf(<format>, <arg1>, <arg2>, ...)` — C-style formatted output with
`%s`, `%d`, `%-20s`, etc. Powerful for aligned output without juggling
ljust()/rjust()/center().

**Worth borrowing?** Yes. Very useful for softcoders writing formatted
reports and tables. More readable than nested ljust/rjust calls.

### 3. Account System (account_*)

`account_login()`, `account_who()`, `account_boot()`, `account_su()`,
`account_owner()` — multi-character account management. One login, multiple
characters under one account.

**Worth borrowing?** The concept is very relevant for modern MU* games.
Multi-character games currently use softcode hacks. A proper account
system would be a significant feature addition. Medium-high effort.

### 4. execscript() / execscriptnr()

Execute external scripts/programs from softcode and capture output.

**Worth borrowing?** Carefully. This is powerful for integration (calling
Python scripts, web APIs via curl, etc.) but opens massive security
surface. MUX could implement this behind a power/permission gate.

### 5. Template System (template())

`template(<template>, <args>)` — apply a template with substitution
markers. Separates presentation from logic.

**Worth borrowing?** Nice for game builders doing consistent formatting.
Simple concept, moderate implementation.

### 6. dynhelp() — Dynamic Help from Attributes

`dynhelp(<object>, <topic>)` — read help text from object attributes
instead of static files. Allows softcode-driven help systems.

**Worth borrowing?** Yes. MUX has `textfile()` for reading static help
files, but dynamic help from objects is more flexible for game-specific
help systems.

### 7. Totem System

A flexible tagging system beyond flags. Totems are user-definable markers
on objects with arbitrary names, managed via `@totemdef`. Functions:
`hastotem()`, `andtotems()`, `ortotems()`, `listtotems()`, `totemset()`,
`totemvalid()`, `totems()`.

**Worth borrowing?** The concept of user-definable markers is good. MUX
has 10 MARKER flags (MARKER0-9) which serve a similar but limited purpose.
A proper tag system would be more flexible. Medium effort.

---

## Medium Priority — Interesting Ideas

### 8. Cluster System (cluster_*)

Object clusters — group objects that share attributes and operations.
28 cluster_* functions covering get/set/grep/u/wipe/stats/flags on
clustered objects.

**Worth borrowing?** The concept of object groups with shared operations
is useful for large builds. Could be done with zones + softcode, but
built-in support would be cleaner. Low-medium priority.

### 9. Extended String Functions

Rhost has several string functions MUX lacks:

| Function | Description | Interest |
|----------|-------------|----------|
| editansi() | Edit text preserving ANSI codes | Medium |
| garble() | Randomize text (drunk/foreign speech) | Low-Medium |
| printf() | Formatted output | High (listed above) |
| strlenvis() | Visible string length (excludes ANSI) | Medium |
| strlenraw() | Raw byte length | Low |
| strdistance() | Levenshtein edit distance | Medium |
| strmath() | Math on numeric strings | Low |
| streval() | Evaluate string as expression | Low |
| strfunc() | Apply function to string chars | Low |
| escapex() | Escape with custom char set | Medium |
| chomp() | Strip trailing newline | Trivial |
| parsestr() | Parse with custom parser | Medium |
| pedit() | Positional edit | Low |
| medit() | Multi-edit | Low |

### 10. Extended Math Functions

| Function | Description | Interest |
|----------|-------------|----------|
| cosh/sinh/tanh | Hyperbolic trig | Low |
| power10() | 10^n | Trivial |
| inf() | Infinity constant | Low |
| ee() | Scientific notation | Low |
| avg/lavg | Average / list average | Medium |
| tobin/todec/tohex/tooct | Base conversion | Medium (MUX has baseconv) |
| rotl/rotr | Bit rotation | Low |
| xnor | XNOR logic | Low |
| packmath() | Math on packed values | Low |
| ipv4math() | IPv4 address arithmetic | Niche |

### 11. Extended List Functions

| Function | Description | Interest |
|----------|-------------|----------|
| listdiff/listinter/listunion | Set operations with delimiter control | Medium |
| listmatch() | Match elements against pattern | Medium |
| sortlist() | Sort with multiple criteria | Medium |
| delextract() | Delete and return extracted elements | Medium |
| extractword() | Extract by word position | Low |
| elementpos() | Position of element | Low |
| lnum2() | lnum with step value | Medium |
| creplace() | Conditional replace | Low |

### 12. Character Classification (is* functions)

`isalnum()`, `isalpha()`, `isdigit()`, `islower()`, `isupper()`,
`ispunct()`, `isspace()`, `isxdigit()`, `isunicode()`, `isutf8()`

**Worth borrowing?** The Unicode-aware ones (`isunicode()`, `isutf8()`)
are relevant for MUX's UTF-8 work. The ASCII ones are trivial but
occasionally useful.

### 13. Lua Scripting Integration (lua.c)

Embedded Lua interpreter with MUD-specific bindings:
- Per-evaluation Lua context
- `rhost_get()` for reading attributes from Lua
- Permission checking on attribute access
- CPU time limits via alarm hooks

**Worth borrowing?** High ambition, high payoff. Lua would give MUX a
real programming language alongside softcode. But it's a major feature
with significant security implications. Long-term consideration for the
"new parser" track (#5 on the roadmap).

### 14. Shared-Memory Debugging (debug.c, debugmon.c)

Shared memory IPC for live debugging:
- External process monitors running MUD via shmem
- Call stack tracking with file/line numbers
- SIGUSR1 for triggering debug dumps

**Worth borrowing?** Interesting for diagnosing hangs and performance
issues without stopping the server. Medium effort, useful for operators.

### 15. Lock Encode/Decode (lockencode/lockdecode)

`lockencode()` and `lockdecode()` — convert locks to/from portable
string representation.

**Worth borrowing?** Useful for softcode that manipulates locks
programmatically (e.g., building systems that construct locks).

### 16. Mail Functions

Rhost has: `mailread()`, `mailsend()`, `mailquick()`, `mailquota()`,
`mailalias()`, `mailstatus()`, `foldercurrent()`, `folderlist()`.

MUX has: `mail()`, `mailfrom()`, `mailreview()`, `mailsubj()`,
`mailsize()`, `malias()`.

**Worth borrowing?** `mailsend()` (send mail from softcode) is the
most useful — enables automated mail systems without @mail command
parsing. MUX currently requires `@mail` via `@force` or similar hacks.

### 17. nslookup() — DNS Lookup from Softcode

Resolve hostnames/IPs from within softcode.

**Worth borrowing?** Niche but occasionally useful for softcode-driven
site checking. Must be async to avoid blocking.

### 18. subnetmatch() — IP Subnet Matching

`subnetmatch(<ip>, <cidr>)` — test if IP is in a subnet.

**Worth borrowing?** Useful for softcode site-based access control.
Simple to implement.

---

## Lower Priority

### 19. WebSocket Support (websock.c, websock2.c)

Rhost has two WebSocket implementations (old and new). RFC 6455
compliant with text/binary frames, ping/pong, proper handshake.

**Worth borrowing?** Yes, but Penn's implementation is probably cleaner
to reference. WebSocket support is listed as high priority in the Penn
survey. The implementation details don't need to come from Rhost.

### 20. Doors System (door.c, door_mail.c, door_mush.c)

Inter-MU* communication via "doors" — TCP connections to external
processes or other MUDs. Includes an Empire game protocol client.

**Worth borrowing?** The concept of external process integration is
interesting but `execscript()` covers the common case. Doors are a
niche feature from an era when MUDs interconnected more.

### 21. Moon Phase (moon())

Returns current moon phase.

**Worth borrowing?** Cute. Trivial to implement. Purely cosmetic.

### 22. Dice System (dice())

`dice(<count>, <sides>)` — roll dice. Rhost calls it `dice()`, MUX
has `die()`.

**Worth noting:** MUX already has `die()` which does the same thing.
Just a naming difference.

### 23. Governance System (govern.c)

Hierarchical government/delegation tree. 24-bit IDs + 8-bit levels.
Tree operations for parent-child relationships.

**Worth borrowing?** Overly complex for most games. Zone system + powers
cover the same territory more simply.

### 24. Tor Integration (bsd.c)

Built-in `check_tor()` for detecting Tor exit nodes.

**Worth borrowing?** Niche security feature. Most operators handle this
at the firewall level.

---

## Not Worth Borrowing

### Combinatorial Function Explosion

Rhost has many function families that are just variants:
- `u()`, `u2()`, `ulocal()`, `ueval()`, `udefault()`, `uldefault()`,
  `u2local()`, `u2default()`, `u2ldefault()` — 9 variants of u()
- Same pattern for `zfun*()`, `cluster_u*()` — another 20+ variants
- `setq()`, `setr()`, `setqm()`, `setrm()`, `setq_old()`, `setr_old()`,
  `setqmatch()`, `pushregs()` — 8 register variants

MUX handles this with fewer functions + optional arguments, which is
the better design. More functions != more power.

### Toggle System (8 words, 200+ toggles)

Toggles are flags by another name. Having both flags AND toggles AND
powers AND depowers AND totems is complexity for its own sake. MUX's
flags + powers model is cleaner.

### Depower System

Anti-powers that strip capabilities. Redundant with proper permission
design. If you need to remove a power, just... remove the power.

### Bang Notation (!$ !^ prefixes)

Optional boolean evaluation modifiers on pattern-match attributes.
Adds parser complexity for minimal benefit.

### Marker Flags (MARKER0-9)

Both Rhost and MUX have these. No delta.

### Various Niche Functions

- `garble()` — randomize text for drunk/foreign speech. Softcode.
- `race()`, `guild()` — game-specific attribute readers. Softcode.
- `moon()` — cosmetic. Softcode.
- `ruler()` — visual ruler line. Softcode.
- `sandbox()` — sandboxed evaluation. Complex for marginal benefit.
- `livewire()` — server monitoring. Operator tool.
- `sweep()` — dark object detection. MUX has equivalent.

---

## Function Delta Summary

### Rhost has 310 functions MUX doesn't. By category:

| Category | Count | Examples | Interest |
|----------|-------|---------|----------|
| Cluster system | 28 | cluster_get, cluster_u, cluster_set | Medium |
| Account system | 5 | account_login, account_who | High concept |
| Base64/crypto | 3 | encode64, decode64, checkpass | High |
| Formatting | 3 | printf, template, ruler | High (printf) |
| Totem/tag system | 9 | hastotem, listtotems, totemset | Medium |
| Toggle/depower | 8 | hastoggle, ltoggles, ldepowers | Low |
| String extensions | 20 | editansi, strdistance, garble | Medium |
| Math extensions | 12 | cosh, sinh, avg, power10 | Low |
| List extensions | 15 | listdiff, listinter, sortlist | Medium |
| Char classification | 12 | isalnum, isdigit, isunicode | Medium |
| U-function variants | 12 | u2, ueval, uldefault | Low |
| Zfun variants | 9 | zfun2, zfuneval, zfunlocal | Low |
| Lock functions | 4 | lockencode, lockdecode, lockcheck | Medium |
| Mail functions | 7 | mailsend, mailread, mailquota | Medium |
| Search variants | 4 | searchng, searchobjid, zsearch | Low |
| System/diagnostic | 15 | logtofile, freelist, atrcache | Low |
| Base conversion | 4 | tobin, todec, tohex, tooct | Medium |
| DNS/network | 3 | nslookup, subnetmatch, lookup_site | Low |
| Scripting | 2 | execscript, execscriptnr | Medium |
| Remaining ~140 | Various | Niche, variants, aliases | Low |

### MUX has 106 functions Rhost doesn't. Notable:

- Full SQL result set API (rserror, rsnext, rsrec, rsrows, etc.)
- Band/bor/bxor/bnand (bitwise — Rhost uses different names)
- Channel functions (cemit, channels, chanobj, comalias, comtitle, cwho)
- Integer math (iabs, iadd, idiv, imul, isub)
- Color depth support (colordepth, moniker)
- Various time formats (digittime, singletime, exptime, writetime)
- Zone functions (zchildren, zexits, zrooms, zthings)
- filterbool, grepi, matchall
- die (Rhost has dice instead)

---

## Architecture Comparison

| Area | RhostMUSH | TinyMUX |
|------|-----------|---------|
| Parser base | TinyMUSH 2.2.5 | TinyMUSH 2.2.5 |
| Functions | 592 | 388 |
| Commands | 141 @ + 200 std | ~100 @ + ~50 std |
| Flag words | 4 (128 flags) | 4 (128 flags) |
| Toggle words | 8 (~200 toggles) | 0 (no toggles) |
| Power words | 3 (48 powers) | 2 (32 powers) |
| Depower words | 3 | 0 |
| Config options | 549 | ~200 |
| Storage | GDBM/flat | SQLite |
| Networking | BSD sockets | GANL (epoll/kqueue) |
| Unicode | Partial UTF-8 | Full UTF-8, NFC, DFA |
| Color | 16/256/24-bit | 16/256/24-bit (PUA) |
| SQL | MySQL + SQLite | SQLite |
| Scripting | Lua embedded | None |
| WebSocket | Yes (two impls) | No |
| Build system | Make + menuconfig | Autoconf |

---

## Cross-Server Feature Matrix (all three surveys)

| Feature | TinyMUSH | PennMUSH | RhostMUSH | TinyMUX |
|---------|----------|----------|-----------|---------|
| JSON | No | **Yes** | No | No |
| WebSocket | No | **Yes** | **Yes** | No |
| HTTP server | No | **Yes** | No | No |
| Base64 | No | **Yes** | **Yes** | No |
| HMAC | No | **Yes** | No | No |
| SQLite | No | **Yes** | **Yes** | **Yes** |
| MySQL | No | Via module | **Yes** | No |
| Lua | No | No | **Yes** | No |
| Connection log | No | **Yes** (SQLite) | No | No |
| printf() | No | No | **Yes** | No |
| Account system | No | No | **Yes** | No |
| Exec scripts | No | No | **Yes** | No |
| Totem/tags | No | No | **Yes** | No |
| Template system | No | No | **Yes** | No |
| CIEDE2000 color | **Yes** | No | No | No |
| Full UTF-8/NFC | No | Partial | Partial | **Yes** |
| GANL networking | No | No | No | **Yes** |
| SQL result sets | No | No | No | **Yes** |
| Reality levels | No | No | Optional | **Yes** |
| Flatfile converter | No | dbtools | No | **Omega** |

---

## Consolidated Recommendations (All Three Surveys)

### Tier 1 — High Value, Implement

1. **JSON support** — Penn has it, web integration demands it, SQLite
   JSON1 already in the stack
2. **WebSocket support** — Both Penn and Rhost have it, essential for
   modern web clients
3. **Base64 encode/decode** — Both Penn and Rhost have it, needed for
   HTTP/API work
4. **Connection logging** — Penn's SQLite-based approach fits MUX
   perfectly
5. **printf()** — Rhost has it, genuinely useful for softcoders
6. **mailsend()** — Send mail from softcode without @force hacks

### Tier 2 — Medium Value, Consider

7. **letq()** — Penn's scoped registers, clean design
8. **sortkey()** — Penn's sort-by-computed-key
9. **Account system** — Rhost's multi-character accounts, modern games
   want this
10. **HMAC** — Penn has it, needed for webhook verification
11. **Template system** — Rhost's template(), presentation/logic separation
12. **strdistance()** — Levenshtein distance, useful for fuzzy matching
13. **lockencode/lockdecode** — Programmatic lock manipulation
14. **Regex attribute matching** — Penn's reglattr/regnattr family
15. **Character classification** — isunicode(), isutf8(), isalpha(), etc.
16. **dynhelp()** — Dynamic help from object attributes

### Tier 3 — Low Value or Long-Term

17. **HTTP server** — Penn has it, very ambitious
18. **Lua scripting** — Rhost has it, relates to parser roadmap item #5
19. **execscript()** — External process integration, security concerns
20. **@cron** — TinyMUSH has it, scheduled attribute triggers
21. **Totem/tag system** — Rhost's user-definable markers
22. **Shared-memory debugging** — Rhost's approach, useful for operators
23. **CIEDE2000 color** — TinyMUSH's perceptual color matching
24. **PCG RNG** — Penn's modern random number generator

---

## Bottom Line

Rhost's approach is "add everything, let config sort it out." This
produces an impressive feature count but also a maintenance burden and
a learning curve that can overwhelm new users. The 549 config options
tell the story.

MUX should cherry-pick the genuinely useful features (JSON, WebSocket,
base64, printf, connection logging) while maintaining its leaner design
philosophy. The consolidated Tier 1 list across all three surveys is
remarkably consistent: web integration primitives (JSON, WebSocket,
base64, HTTP) are the biggest gap in MUX's feature set.

The parser discussion (roadmap item #5) intersects with Rhost's Lua
integration — if MUX ever adds a real programming language, Lua is the
proven choice in this space.
