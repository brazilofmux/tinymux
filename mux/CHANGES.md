---
title: TinyMUX 2.14 CHANGES
date: March 2026
author:
 - Brazil
---

Changes since TinyMUX 2.12.0.12.

# Major changes that may affect performance and require softcode tweaks:

 - The server is now split into three shared objects: `libmux.so`
   (core types and utilities), `engine.so` (game engine loaded as a
   COM module), and `netmux` (network driver). The engine communicates
   with the driver exclusively through COM interfaces. This is the
   foundation for future process isolation.
 - The softcode expression evaluator has been replaced with an
   AST-based parser. Expressions are tokenized by a Ragel-generated
   scanner, parsed into an AST, and cached in an LRU cache. All
   %-substitutions, `##`/`#@`/`#$` tokens, and NOEVAL constructs
   (iter, switch, if/ifelse, cand/cor) are handled natively. The
   classic parser has been deleted.
 - Embedded Lua 5.4 scripting engine with sandboxed execution, bytecode
   cache, and `mux.*` bridge API. `@lua` command, `lua()` and
   `luacall()` softcode functions.
 - JIT compiler for softcode expressions: AST → HIR → SSA → optimize →
   RV64 codegen → x86-64 dynamic binary translation. Tier 2 blob with
   50+ pre-compiled native intrinsics. Optional (`--enable-jit`).
 - Lua JIT compiler: Lua bytecode compiled through the same HIR/SSA/DBT
   pipeline. 83 opcodes supported including table operations, bitwise
   ops, generic for-loops, upvalues, and ECALL back to Lua VM.
 - SQLite is now the always-on storage backend. All object metadata,
   attributes, comsys channels, and @mail are stored in a single SQLite
   database with write-through on every mutation. `@dump` performs a
   WAL checkpoint only — no flatfile serialization, no fork. Crash
   durability is immediate; no data is lost between dump cycles.
 - `@search` for simple cases (owner, type, zone, parent, flags) routes
   to indexed SQL queries — O(log n) instead of linear scan.
 - Unicode updated from 10.0 to 16.0. All attribute values are
   NFC-normalized at storage time. String functions use grapheme cluster
   indexing (UAX #29) instead of codepoint counting.
 - Unicode collation (UCA/DUCET) is the default sort order. `sort()`
   and set functions (`setunion`, `setinter`, `setdiff`) use
   locale-correct Unicode comparison. A case-insensitive collation sort
   type is also available.
 - CJK width-aware string operations: `center()`, `ljust()`, `rjust()`,
   `columns()` respect double-width characters.
 - The `have_comsys` and `have_mailer` config options have been removed;
   comsys and @mail are always present.
 - The MEMORY_BASED build option has been removed; SQLite is the only
   storage backend.
 - Reworked networking to support non-blocking SSL sockets.
 - Require PCRE to be installed instead of using a static, private
   version.
 - Updated regular expression engine from PCRE to PCRE2
   - Improved performance with Just-In-Time compilation
   - Better Unicode support
   - Modern API with improved memory management
 - GANL networking is now mandatory; replaces legacy select-based I/O
   with event-driven architecture using epoll (Linux), kqueue (BSD),
   and IOCP (Windows).
 - SSL support is always enabled; OpenSSL is required on Unix, Schannel
   on Windows. The --enable-ssl configure option has been removed.
 - The `safer_iter` config option allows disabling `##` itext
   substitution in iter()/list() for games that use `##` in attribute
   contents (#688).
 - `@listen` `%0` now retains Unicode typographic (fancy) quotes.
   Pattern matching uses normalized ASCII quotes, but capture is from
   the original text. Applies to both `@listen` and `^-listen`.
 - Replaced Mersenne Twister (std::mt19937) with PCG-XSL-RR-128/64
   (pcg64). 32 bytes of state replaces 2496 bytes. Seeded from
   /dev/urandom with unbiased rejection sampling for bounded output.

# Feature Additions:

## Protocols

 - MSSP (Mud Server Status Protocol, telnet option 70). Structured
   key-value data sent to MU* directory crawlers: NAME, PLAYERS,
   UPTIME, PORT, CODEBASE, FAMILY.
 - GMCP (Generic MUD Communication Protocol, telnet option 201). Full
   protocol negotiation. Inbound GMCP packets fire the A_GMCP attribute
   with %0=package, %1=json. `gmcp()` softcode function sends GMCP
   frames to players.
 - WebSocket support (RFC 6455) with same-port auto-detection and
   `wss://` (WebSocket over TLS) via deferred protocol detection.
 - Connection logging: SQLite `connlog` table records connect/
   disconnect events with timestamps, IPs, and player dbrefs.
   `connlog()` and `addrlog()` query functions.

## Commands

 - `@protect[/add]`, `@protect/del`, `@protect/list`, `@protect/alias`,
   `@protect/unalias`, `@protect/all` — player name reservation system.
   A_PROTECTNAME attribute, `max_name_protect` config parameter.
 - `@cron`/`@crondel`/`@crontab` — scheduled attribute triggers using
   5-field Unix cron syntax. Vixie-style computed next-fire-time
   scheduling integrates with the scheduler — no polling loop. Supports
   ranges, lists, steps, and month/day-of-week names. DOM/DOW
   OR-semantics per Vixie cron. In-memory, not persisted (use
   `@startup` to recreate entries across restarts).
 - `@include` with `/nobreak`, `/localize`, `/clearregs` switches.
   Include the contents of an attribute in the current command stream.
 - `@lua` command for server-side Lua 5.4 scripting.
 - `@chatformat` — per-player channel message formatting attribute.
   Evaluated with %0=channel name, %1=formatted message, %2=sender
   dbref. Zero overhead when not used.
 - `@object/lockname` indirect lock syntax (#702).
 - `@mail/unsafe`, `@mail/edit` switches.

## Functions — New

 - `benchmark(<expr>, <iterations>)` — benchmark expression evaluation
   with monotonic clock. 10000 iteration cap.
 - `lua()` and `luacall()` — evaluate Lua code from softcode.
 - `gmcp(<dbref>, <package>, <json>)` — send GMCP frame to a player.
 - `letq()` — scoped temporary registers.
 - `sortkey()` — custom sort-key evaluation.
 - `reglattr()` and `reglattrp()` — regex attribute matching.
 - `regrep()` and `regrepi()` — regex attribute value search (PCRE2).
 - `lockencode()` and `lockdecode()` — lock serialization.
 - `dynhelp()` — dynamic help from object attributes.
 - `mailsend()` — send mail from softcode.
 - `strdistance()` — Levenshtein edit distance.
 - `url_escape()` and `url_unescape()` — RFC 3986.
 - `printf()` — formatted output with ANSI-aware field widths.
 - `encode64()`, `decode64()`, `hmac()` — cryptographic functions.
 - `isjson()`, `json()`, `json_query()`, `json_mod()` — JSON.
 - `isalpha()`, `isdigit()`, `isupper()`, `islower()`, `ispunct()`,
   `isspace()`, `isword()` — Unicode character classification.
 - `nsemit()`, `nspemit()`, `nsoemit()`, `nsremit()` — nospoof emit
   family (always prepend `[Name(#dbref)]` header).
 - `prompt()` — send telnet GA-terminated prompt to a player.
 - `asteval()` — evaluate an expression using the AST evaluator.
 - `astbench()` — head-to-head AST vs JIT benchmarking.
 - `jitstats()` — JIT compiler statistics.
 - `sandbox(<function-list>, <expression>)` — evaluate with restricted
   function set.

## Functions — PennMUSH Feature Adoption

 - `mean()`, `median()`, `stddev()` — statistical functions.
 - `bound()` — clamp a value to a range.
 - `unique()` — remove duplicates from a list.
 - `linsert()`, `lreplace()` — list insert/replace by position.
 - `strdelete()`, `strinsert()`, `strreplace()` — string surgery.
 - `unsetq()` and `listq()` — register management.
 - `ncon()`, `nexits()`, `nplayers()`, `nthings()` — content counts.
 - `lplayers()`, `lthings()` — typed content lists.
 - `firstof()`, `strfirstof()` — short-circuit first non-empty.
 - `allof()`, `strallof()` — short-circuit collect all non-empty.
 - `#lambda` anonymous inline attributes for filter/map operations.
 - `cmogrifier()` — query channel mogrifier object.

## Functions — RhostMUSH Feature Adoption

 - `between()` — range testing.
 - `delextract()` — delete range of elements from a list.
 - `garble()` — garble text by percentage.
 - `caplist()` — title-case with smart article/conjunction handling.
 - `moon()` — moon phase / illumination percentage.
 - `soundex()`, `soundlike()` — phonetic fuzzy name matching.
 - `while()` — iterate while condition is true.
 - `crc32obj()` — CRC32 checksum across all object attributes.
 - `wrapcolumns()` — multi-column text wrapping.
 - `subnetmatch()` — IP subnet membership test.
 - `mapsql()` — map SQL result rows through softcode (wizard-only).

## Functions — Channel Query (PennMUSH-Compatible)

 - `cbuffer()` — channel buffer size.
 - `cdesc()` — channel description.
 - `cflags()` — channel flags or per-user flags.
 - `cmsgs()` — total message count.
 - `cowner()` — channel owner dbref.
 - `crecall()` — recall message history.
 - `cstatus()` — On/Off/Gag status for a player.
 - `cusers()` — subscriber count.

## Channel Mogrifiers

 - `MOGRIFY`BLOCK`, `MOGRIFY`MESSAGE`, `MOGRIFY`FORMAT`,
   `MOGRIFY`OVERRIDE`, `MOGRIFY`NOBUFFER` — per-channel message
   transformation hooks on the mogrifier object.

## Other Features

 - Update to Unicode 16.0.
 - `chr()` and `ord()` support the full Unicode range.
 - `zchildren()`, `zexits()`, `zrooms()`, `zthings()` zone listing
   functions (#624).
 - `zfun()` now accepts `obj/attr` syntax like `u()` (#624).
 - `citer()` character iterator function (#555).
 - `switchall()` and `caseall()` functions (#528).
 - `lrest()` function to return all but the last word (#580).
 - `lmath()` generic list-math function (#553).
 - `malias()` softcode function for querying mail aliases.
 - Removed 99-member mail alias limit; uses `std::vector<dbref>`.
 - ALONE flag for rooms (#642).
 - TALKMODE flag for talk mode / dotty behaviour (#684).
 - `link_anywhere` power (#529).
 - `player_channels` config parameter to limit per-player channel
   membership (#517).
 - Support `#$` token substitution in `if()`/`ifelse()` (#560).
 - Optional delimiter argument to `channels()` (#678).
 - Support `::` escape for literal `:` in `$-command` and `^-listen`
   patterns (#662).
 - Store full recipient list in sender's `@mail/bcc` copy.

# Bug Fixes:

 - Fix SIGSEGV/SIGABRT on login timeout — `shutdownsock()` bypassed
   GANL, leaving stale handle mappings that caused use-after-free when
   GANL detected the dead fd.
 - Fix reverse DNS slave: strip trailing newline from input before
   calling `getaddrinfo()`. Latent since 2012; activated when GANL
   switched to stream pipes.
 - Fix whisper bugs: "to far" typo, `A_LASTWHISPER` not saved for
   quoted names with spaces, bare `w` silently returning.
 - Fix `sqlite_sync_comsys` failing on orphaned channel aliases.
 - Fix Backup script excluding distribution help files.
 - Fix @restart hang and connection drop in the GANL adapter — the
   descriptor handoff across exec now correctly preserves all active
   connections.
 - Fix GANL pure virtual call on connection teardown during shutdown —
   the socket object could be destroyed while an async callback was
   still pending.
 - Fix GANL QUIT double-free — route disconnect through
   ganl_close_connection instead of destroying the socket directly.
 - @npemit now refers to @pemit/noeval.
 - pose now documents /noeval switch.
 - Don't notify permission denied with @edit when QUIET.
 - Fixed strncpy() corner case in @hook.
 - Fixed fun_cwho(,all) command to return all objects and players
   associated with the channel, ensuring it behaves as documented.
 - Modified home command behavior to disregard the command if the player
   is already at home, and to suppress public announcements in BLIND
   locations.
 - Fixed several network and SSL-related issues: added defensive null
   checks, corrected iterator handling, improved SSL state transitions,
   and fixed a descriptor management bug that could remove the wrong
   descriptor when a player has multiple connections.
 - Fixed a string ownership bug in updated muxcli.cpp (introduced in
   2.13.0.5). Neither side persisted the value. Serious.
 - Fix Reality Levels for exits, @descformat, and lcon() (#644, #645,
   #646).
 - Fix double-evaluation of reality level descriptions in @descformat
   (#610).
 - Always fire action attributes during quiet teleport (#674).
 - Suppress @enter and @leave for dark wizards (#525).
 - Fix HTML bright colors when CS_INTENSE is active (#659).
 - Suppress space-trimming in after() and before() when delimiter is
   explicit (#534).
 - Reject object names that exceed the MBUF_SIZE limit (#533).
 - Drain extra result sets after MySQL stored procedure queries (#693).
 - Return descriptive error from set() instead of bare #-1 (#543).
 - Fix objeval() leaking wizard privileges through locatable() (#687).
 - Report "Already set."/"Already cleared." for no-op @set/@power
   (#650).
 - Check VisibleLock (A_LVISIBLE) on exits (#573).
 - Fix @backup to return early when a dump is in progress (#612).
 - Ignore client CHARSET REQUEST when server REQUEST is pending
   (RFC 2066) (#664).
 - Strip outer braces from semicolon-separated queued commands (#620).
 - Fix splice() misalignment with leading/trailing spaces (#643).
 - Add CS_INTERP to @cpattr so arguments are evaluated (#665).
 - Fix @cset/header clearing to reset to default [ChannelName] (#670).
 - Fix squish() with multi-character separators.
 - Fix bugs in Poor Man's COM: infinite loop, delete[] misuse.
 - Short options were being parsed oddly.
 - Fix IPv6 subnet comparison: operator== compared array pointers
   instead of contents (always false); operator< didn't early-exit
   correctly. Also fix mux_sockaddr IPv6 memcmp using wrong size.
 - Fix subnet tree remove/reset: reset() discarded return value so
   @reset_site was a no-op; kContainedBy case deleted unrelated
   siblings; kEqual case didn't detach children before deletion.
 - Fix parse_to_lite reading past boundary when scanning for closing
   ')' inside bounded mux_exec calls.
 - Fix null handle crash in ModuleUnload when dlopen handle is NULL.

# Performance Enhancements:

 - JIT compiler for softcode: hot expressions are compiled to native
   x86-64 machine code via an RV64 intermediate representation. Tier 2
   blob provides 50+ pre-compiled native intrinsics for common string
   and math operations.
 - Lua JIT: Lua bytecode compiled through the same HIR/SSA/DBT
   pipeline as softcode. Eligible functions execute as native code.
 - AST parse cache (LRU, 1024 entries) eliminates re-parsing of
   frequently evaluated expressions.
 - Ragel-generated goto-driven scanner for expression tokenization.
 - Indexed @search via SQLite for owner, type, zone, parent, and flag
   queries.
 - Attribute cache preloading: built-in attributes (attrnum < 256) are
   bulk-loaded from SQLite on player connect and object move.
 - Replaced CHashTable with std::unordered_map throughout (vattr
   registry, function table, etc.).
 - Replaced qsort() with std::sort() in sort and set functions.
 - Replaced CTaskHeap with std::vector + STL heap algorithms.
 - Eliminated A_LIST attribute enumeration; replaced with direct SQLite
   queries and STL iteration context.

# Cosmetic Changes:

 - Increased trimmed name field length for WHO, DOING, and SESSION
   displays (from 16 to 31) and adjusted field widths to maintain proper
   alignment.
 - Show named empty folders in @mail/folder listing (#499).
 - Make channel name lookup case-insensitive (#681).
 - Allow @flag to rename over an alias for the same flag (#501).
 - Normalize smart quotes for @listen/^-listen/@filter pattern matching.
 - Remove UNINSPECTED from default stripped_flags (#626).

# Miscellaneous:

 - Three-layer build: libmux.so (core types/utilities), engine.so
   (game engine as COM module), netmux (network driver).
 - Source tree restructured: headers in `include/`, library sources in
   `lib/`, engine sources in `modules/engine/`, driver sources in
   `src/`, top-level autotools.
 - 12 server-provided COM interfaces bridge engine and driver:
   INotify, IObjectInfo, IAttributeAccess, IEvaluator, IPermissions,
   IMailDelivery, IHelpSystem, IGameEngine, IConnectionManager,
   IPlayerSession, ILog, IDriverControl.
 - engine.so uses `-fvisibility=hidden`; only COM front-door functions
   are exported.
 - Comsys module extraction: `modules/comsys/comsys_mod.cpp` provides
   channel management as a loadable COM module with its own SQLite
   connection.
 - Mail module extraction: `modules/mail/mail_mod.cpp` provides @mail
   read, flag, purge, folder, and malias operations as a loadable COM
   module.
 - game.cpp split into driver.cpp (network lifecycle) and engine.cpp
   (game logic). netcommon.cpp split into net.cpp (driver) and
   session.cpp (engine). interface.h restricted to driver files only.
 - Embedded Lua 5.4 with sandboxed execution environment and bytecode
   cache. `mux.*` bridge API provides access to server objects,
   attributes, and evaluation from Lua scripts.
 - Removed FiranMUX build.
 - Updated to C++17 standard.
 - Replaced acache_htab, attr_name_htab, channel_htab, desc_htab,
   descriptor_list, flags_htab, func_htab, fwdlist_htab, and 8
   additional CHashTable instances with std::unordered_map.
 - Concentrate #include files into externs.h.
 - Updated to autoconf v2.71 and improved build dependencies to prevent
   race conditions.
 - Implemented GANL @restart support: detach/adopt file descriptors
   across exec.
 - Ported @email SMTP client from raw sockets to GANL event-driven I/O.
 - Removed STARTTLS telnet negotiation code; GANL handles TLS natively.
 - Removed dead legacy networking code from bsd.cpp.
 - Decomposed bsd.cpp into focused translation units (netaddr.cpp,
   signals.cpp, sitemon.cpp, telnet.cpp).
 - Replaced command/text block queues with std::deque<std::string>.
 - Replaced CBitField internals with std::vector<bool>.
 - Replaced mux_alarm platform ifdefs with std::thread +
   std::condition_variable.
 - Replaced ReplaceFile/RemoveFile ifdefs with std::filesystem.
 - Replaced local-scope new[]/delete[] with std::vector.
 - Replaced std::mt19937 with PCG-XSL-RR-128/64 (pcg64).
 - Modernized mux_string class: std::vector, Rule of Five, API cleanup.
 - Replaced C-style casts with static_cast/reinterpret_cast across
   codebase.
 - Replaced ~317 flag/constant #defines with constexpr across headers.
 - Replaced custom integer types (INT8..INT64, UINT8..UINT64) with
   standard <cstdint> types.
 - Replaced getpagesize() ifdef cascade with sysconf(_SC_PAGESIZE).
 - Removed dead code: stale ifdefs, #if 0 blocks, unused declarations.
 - Returned to autoconf/automake build system.
 - Hardened Schannel TLS: EKU-aware cert selection, PEM support.
 - SQLite 3.49.1 amalgamation bundled for both Unix and Windows builds.
 - SQLite schema versioning with automatic migration (currently v7).
 - Comprehensive UTF-8 validation hardening across all string
   processing paths (decode, advance, truncate, collate, normalize).
 - NFC normalization pipeline: DFA-based Unicode property lookups,
   Canonical Combining Class tracking, Hangul algorithmic composition.
 - Grapheme cluster segmentation per UAX #29 for correct string
   indexing.
 - Unicode collation: DUCET tables generated via reproducible pipeline
   from allkeys.txt. DFA-based contraction lookup, sort key generation.
 - Omega converter freshened for all four server formats (T5X, T6H,
   P6H, R7H). Direct T5X-to-R7H path preserves full 24-bit color.
 - Removed CHashTable, CHashPage, CHashFile, IntArray, and legacy
   A_LIST machinery.
 - Removed dead MEMORY_BASED, SQLITE_STORAGE, USE_GANL preprocessor
   conditionals.
 - Removed deprecated stack subsystem.
 - Removed dead cache statistics counters.
 - Removed database compression feature.
 - Reproducible builds: removed build date/time/number injection.
   Version strings use only MUX_VERSION and MUX_RELEASE_DATE.
 - `dbconvert` supports `-C` and `-m` flags for comsys and mail
   flatfile import/export.
 - Remove SQLite calls from signal handlers. Write-through makes them
   unnecessary and they risk WAL corruption.
 - 537 smoke test cases across 184 test suites.
 - Help entries for all new commands and functions.
 - PennMUSH and RhostMUSH feature adoption documented in
   `docs/PENNMUSH-FEATURES.md` and `docs/RHOSTMUSH-FEATURES.md`.

# Changes in 2.14.0.2 (2026-MAR-20):

 - Fixed incomplete distribution TOC files: many source files were
   missing from the unix and win32 distribution archives in 2.14.0.1.
 - Switched Windows build from /MT (static CRT) to /MD (dynamic CRT)
   and added `mux_fclose()` wrapper for cross-module FILE* handling.
 - Added `Startmux.bat` to the win32 distribution.
 - Added `softlib.rv64` to the win32 binary distribution.
 - Removed stale `buildnum.sh` from unix distribution.

# Changes in 2.14.0.4 (2026-MAR-21):

 - JIT Tier 3: cross-attribute `u()` inlining. The JIT compiler can now
   inline the body of helper attributes called via `u()` directly into
   the caller's compiled code, eliminating ECALL overhead for hot paths.
   Per-attribute `mod_count` column (schema v8) tracks mutations so
   compiled code is automatically invalidated when any inlined dependency
   changes. Dependency vectors are stored alongside compiled programs in
   the SQLite code cache.
 - JIT re-entrancy guard: nested `u()` calls that would recursively
   invoke the JIT compiler now fall back to the AST evaluator instead of
   hanging. This is the foundation that allows the compiler to do more
   work per compilation unit without risk of recursive re-entry.
 - JIT/SSA compiler fixes: fixed CFG hang when allocating merge blocks
   after body lowering; fixed SSA hang when attempting to inline bodies
   containing control flow; restricted `u()` inlining to literal
   `#dbref/attr` patterns and clamped extra arguments.
 - Replaced configurable `article_rule` with a Ragel -G2 scanner for
   `art()`. The new scanner is faster and handles edge cases (silent
   vowels, acronyms) without configuration.
 - Added `muxscript`: a headless script driver that boots the engine
   via COM, loads the database, and executes softcode from the command
   line or a script file. Supports `-p` flag for player identity
   selection and poll-based event loop for queue drain. Integrated into
   the autotools build system.
 - Fixed `stubslave` write path for non-blocking sockets: added proper
   `POLLOUT` handling to avoid deadlock; replaced busy-wait with
   `poll()` to fix 100% CPU spin in the main loop.
 - Fixed `@switch` dropping `iter()` context (`##`, `#@`, itext) from
   an enclosing `@dolist`.
 - Fixed `sandbox()` not restoring alias permissions on exit.
 - Added JIT/DBT source files to the Windows build (`engine.vcxproj`)
   and fixed POSIX portability issues (`mmap`/`mprotect` guards,
   `uint8_t` qualification) so the JIT headers compile cleanly on both
   platforms.
 - Removed dead `has_control_flow` helper from the SSA optimizer.

# Changes in 2.14.0.3 (2026-MAR-20):

 - Added `muxescape/` directory to distribution TOCs (configure failed
   without `muxescape/Makefile.in`).
 - Added `rv64/rv64blob.h` and `rv64/softlib.rv64` to distribution TOCs
   (build with `--enable-jit` failed without the RV64 blob header).
