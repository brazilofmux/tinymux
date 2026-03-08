---
title: TinyMUX 2.14 CHANGES
date: March 2026
author:
 - Brazil
---

# Major changes that may affect performance and require softcode tweaks:

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

# Feature Additions:

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
 - ALONE flag for rooms (#642).
 - TALKMODE flag for talk mode / dotty behaviour (#684).
 - `link_anywhere` power (#529).
 - `player_channels` config parameter to limit per-player channel
   membership (#517).
 - Support `#$` token substitution in `if()`/`ifelse()` (#560).
 - Optional delimiter argument to `channels()` (#678).
 - `@object/lockname` indirect lock syntax (#702).
 - Support `::` escape for literal `:` in `$-command` and `^-listen`
   patterns (#662).
 - Store full recipient list in sender's `@mail/bcc` copy.

# Bug Fixes:

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

# Performance Enhancements:

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

 - Removed FiranMUX build.
 - Updated SAL annotations.
 - Fix ReSharper warnings and accept recommendations.
 - Updated to C++14 standard.
 - Updated to C++17 standard.
 - Replaced acache_htab, attr_name_htab, channel_htab, desc_htab, and
   descriptor_list with STL equivalent.
 - Replaced flags_htab, func_htab, and fwdlist_htab with STL equivalents
   (unordered_map).
 - Replaced 8 additional CHashTable instances with std::unordered_map.
 - Re-work the general case mux_string::tranform case to use STL map
   instead of scratch_htab.
 - Update ax_cxx_compile_stdcxx.m4.
 - Update configure.in and configure.
 - Concentrate #include files into externs.h.
 - Updated to autoconf v2.71 and improved build dependencies to prevent
   race conditions.
 - Applied const-correctness improvements to functions handling name
   formatting in the network user display.
 - Updated +help index in plushelp.txt to include +selfboot and mp.
 - Re-enabled timezone caching in timezone.cpp (a change in 2008 had
   inadvertently disabled caching, affecting timezone-related
   performance).
 - Improved safety of XOR operations in utf/strings.cpp by adding a
   helper function that validates input lengths and buffer sizes. This
   change does not affect the output.
 - Improved the emission of COPY and RUN phrases in utf/smutil.cpp to
   support multiple consecutive phrases when necessary. This change is
   developer-only and is a necessary precursor to handling certain data
   sets correctly; output remains unchanged.
 - Name conflicts and deprecated openssl interfaces forced a
   reorganization in SHA-1 and Digest.
 - Removed SAL annotations that were interfering with code clarity.
 - Refreshed muxcli.cpp and its header to modernize code style and clean
   up legacy constructs.
 - Updated the mux_alarm class to improve clarity and maintainability.
 - Removed the unnecessary deleter for mux_alarm on Unix, simplifying
   memory management.
 - Reordered operations to avoid a race condition, enhancing stability
   in concurrent scenarios.
 - Resolved a naming conflict involving bind() to prevent build
   ambiguities.
 - Refactored the time parser by replacing macro constants with
   C++-style constants for better type safety and clarity.
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
 - Replaced hand-rolled MT19937 with std::mt19937.
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
 - Added 141 new smoke tests expanding coverage from 32 to 173
   functions (2.13.0.7).
 - Help entries for TALKMODE, citer(), player_channels, and
   talk_mode_default.
 - Document Master Room in COMMAND EVALUATION help topic.
 - Document multi-character separator support in squish() and trim()
   help.
 - Added help alias so 'help left()' displays strtrunc() topic.
 - Added prerequisites section and fixed step numbering in INSTALL.md.
 - SQLite 3.49.1 amalgamation bundled for both Unix and Windows builds.
 - SQLite schema versioning with automatic migration (currently v5).
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
 - Replaced IntArray with std::vector<int>.
 - Removed dead MEMORY_BASED, SQLITE_STORAGE, USE_GANL preprocessor
   conditionals.
 - Removed deprecated stack subsystem.
 - Removed dead cache statistics counters.
 - Removed database compression feature.
 - Added 175 new smoke test cases expanding coverage to 348 total.
 - `dbconvert` supports `-C` and `-m` flags for comsys and mail
   flatfile import/export.
