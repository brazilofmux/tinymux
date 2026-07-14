---
title: TinyMUX 2.14 CHANGES
date: April 2026
author:
 - Brazil
---

Changes in TinyMUX 2.14 (relative to the 2.13 branch point).

# Changes in 2.14.0.9 (2026-JUL-14):

This release is a stability and hardening follow-up to 2.14.0.8: a
file-descriptor leak in the GANL networking stack and a connect-time crash
loop are fixed, the database load paths get a second memory-safety pass
under the corrupt-database threat model, the @mail and mail-alias
subsystems are migrated to RAII/STL storage, and the Omega flatfile
converter gains full cross-server (PennMUSH, RhostMUSH, TinyMUSH) color
interoperability.  It also carries a round of JIT/interpreter parity and
performance work and a large expansion of the smoke-test suite.

## Networking

 - A descriptor leak in the `epoll` engine is fixed.  The abnormal-
   disconnect path (`EPOLLERR`/`EPOLLHUP` — a client `RST` or hangup)
   emitted a close event but, unlike the graceful and outbound-failure
   paths, never closed the socket or removed it from the engine, so every
   abrupt disconnect leaked one descriptor, left registered in `epoll`
   forever.  A busy game accumulated tens of thousands of orphaned sockets
   over its uptime.  `handleClose()` now closes the descriptor on the
   abnormal path as well; verified live (40 `RST` disconnects leaked 40
   descriptors before the fix, 0 after).
 - When `ConnectionBase::initialize()` fails to associate its context, the
   accepted socket is now closed rather than leaked, matching the sibling
   failure branches.
 - A rate-limited `NET/STAT` socket-accounting log line (logical DESCs vs.
   the adapter's connection maps vs. the OS descriptor count, plus
   accept/close churn) was added to localize where sockets accumulate.
 - GANL console shutdown now handles a Windows logoff cleanly.

## Reliability and Restart

 - A connect-time crash loop is fixed.  A player whose `A_LOGINDATA` was
   truncated or malformed made `decrypt_logindata()` dereference a null
   field pointer and `SIGSEGV` on connect; and, separately,
   `unset_signals()` spun forever on a missing loop increment instead of
   restoring the default handlers, so a caught crash pinned the process at
   100% CPU in the signal handler and defeated crash-driven `@restart`
   recovery.  Field reads now return an empty string rather than null, and
   the signal-reset loop is fixed, so a bad `A_LOGINDATA` fails safe and a
   crash restarts as intended.  `record_login` also warns on a malformed
   `A_LOGINDATA` read.
 - Connection, idle, and server-start times now survive `@restart` past
   2038.  They are 64-bit Unix-second counts but were round-tripped through
   the 32-bit `dbref` channel in `restart.db`, truncating after
   2038-01-19 and corrupting the WHO "On For" column for any session
   spanning a restart.  They are now serialized as 64-bit and `restart.db`
   is bumped to version 5 (older versions still read).  A missing
   `EPOCH_OFFSET` in the `time()` fallback of `GetUTCLinearTime` was also
   fixed.

## Database Load Hardening

A second memory-safety pass over the database load paths (flatfile reader,
@mail/mail-alias loader, SQLite bulk attribute load) under the
corrupt-or-crafted-database threat model, covering code the June pass
(#806/#808/#841/#843) did not reach:

 - `getstring_noalloc` no longer overflows its static buffer.  Both the
   escaped-string refill path (a mis-accumulated byte count) and the legacy
   continued-line path (no capacity guard) could march past the buffer on a
   crafted >64 KB quoted string; both now track the space remaining.
 - The mail-alias loader (`malias_read`) now clamps the recipient count it
   reads from the file, and `make_numlist` bounds both its alias-expansion
   and direct-recipient copies, so a crafted `mail.db` alias can no longer
   overflow the fixed recipient stack array.  The mail-alias table also
   resets its count and capacity when an allocation fails, so an oversized
   count can no longer leave it pointing at freed or undersized storage.
 - The SQLite bulk-load paths now clamp attribute value lengths to
   `LBUF_SIZE`, matching the write and single-read paths, closing a heap
   overflow on the first read of an over-long value written directly into
   the database file.
 - `get_list` now stops at end-of-file instead of spinning forever (and
   flooding the log) on a flatfile truncated mid-attribute-list, and the v2
   lock parser (`getboolexp1`) frees its partial subtree on each
   malformed-input error path.

## @mail and Mail-Alias Storage

The @mail and mail-alias subsystems were migrated from manual C-style
arrays and hand-managed string lifetimes to RAII/STL storage, eliminating
the class of leaks, allocation-failure state corruption, and out-of-bounds
accesses that motivated the hardening above:

 - The mail-alias table is now a `std::vector<std::unique_ptr<malias_t>>`
   with `std::string` names; the manual `new[]`/`delete[]`, optimistic
   size updates, and defensive null guards are gone.
 - Mail bodies use a `std::vector` of `std::string`, and mail headers own
   `std::string` fields with per-player `std::list` storage, removing the
   manual `MAILBODY` array and all `StringClone`/`MEMFREE` lifetime
   management.
 - Two regressions introduced by the migration were caught and fixed: the
   SQLite write-through insert for newly sent mail was restored (it had
   become dead code, risking a lost message on a crash before the next full
   sync), and a mailbox-full off-by-one that cut the effective per-player
   limit by one and inflated the "mailbox is full" count was corrected.

## Omega Flatfile Converter

The Omega cross-server flatfile converter gained full color
interoperability across PennMUSH, RhostMUSH, and TinyMUSH, plus
current-format support and a memory-safety pass:

 - Current (v5, 2.14) flatfiles are now supported, and the per-channel
   delta PUA color codec was restored so 2.13 (v3/v4) flatfiles round-trip
   byte-exact.
 - Color is now carried across every conversion direction: import from and
   export to PennMUSH inline markup and RhostMUSH `%c` codes, and 256- and
   24-bit color is preserved across TinyMUSH (raw-ANSI) conversion and
   across the v4→v3 TinyMUX downgrade (previously reduced to 16 colors).
   Latin-1 text is also preserved on the TinyMUX→TinyMUSH path (accented
   characters had been turned into `?`).
 - The `@decomp` extraction helpers no longer silently truncate large
   attributes (buffers doubled to `2*LBUF` with a clean-boundary warning on
   overflow), and a latent PennMUSH markup buffer overflow on color-heavy
   values was fixed.
 - AddressSanitizer over the full conversion matrix found and fixed five
   latent memory-safety bugs (allocator mismatches and a stack
   use-after-scope on the cross-family paths); an ASan harness, a
   color-stress fixture, and a converter test pool were added.
 - The `-v` version selector was replaced with a self-describing per-family
   registry (canonical ids, aliases, `latest`/`oldest`/`same`, and an
   `omega --list`), and the detected input and produced output versions are
   now reported.

## JIT / DBT Engine

 - The JIT now declines cleanly when its Tier-2 blob (`softlib.rv64`) is
   missing.  The blob-presence check sat inside a once-only init block, so
   only the first call declined and every later call ran the JIT without
   the blob, producing silently wrong results for some compiled shapes.
   The check is hoisted so a blob-less build falls back to the interpreter
   on every call, and the missing blob is logged once. (#875)
 - `member()` now compares color-stripped words, matching the compiled
   `co_member` and the rest of the word-comparison family; a colored list
   or target previously never matched in the interpreter.  (Found by the
   JIT differential fuzzer.)
 - The Tier-2 `left`/`right` wrappers are now grapheme-cluster-aware and
   range-check negative counts, matching the host functions, and `left()`
   is now a first-class function (with `strtrunc()` as its documented
   synonym) rather than a config-file alias, so it is visible to the
   function table, the JIT allowlist, and tooling.
 - `#$` no longer leaks into `switch()`/`switchall()` pattern evaluation on
   the AST fast path, which had diverged from the interpreted path. (#857)
 - A `cand()`/`cor()` chain that bails to the interpreter mid-chain (a
   runtime-integer argument followed by a runtime-string one) no longer
   crashes or emits garbage; the partial fast-path lowering is now rolled
   back cleanly before the fallback. (#858)
 - `ljust()`/`rjust()`/`center()` now range-check the requested width
   before narrowing it, so a large width (e.g. `ljust(x,999999)`) reports
   an out-of-range error instead of silently wrapping to a bogus column
   count; the interpreter and Tier-2 paths now agree.
 - `fmod()` on non-IEEE-SNaN portability builds now guards the divisor
   rather than the dividend, so `fmod(0,3)` is `0` and `fmod(x,0)` is
   indeterminate (they were inverted).  Dead code on the primary build.
 - A muxscript death mid-run is now reported as a crash by the smoke
   harness instead of being silently swallowed, and the HIR compiler
   poisons and abandons a compile that overflows its instruction/block
   capacity, falling back to the AST evaluator. (#859)

## Performance

 - `NearestPretty` gained an integer fast-path that skips the 9× shortest-
   decimal search for whole-number results — the common case for integer
   list math — measured at roughly −35% on `iter(lnum(100),mul(%i0,%i0))`.
 - `run_cached_program` now populates only the substitution and argument
   slots a compiled program actually reads instead of all ~45 every call,
   dropping the per-call floor of a cached eval from ~0.40 to ~0.15 µs.
 - `co_find_delim` — the shared delimiter scanner under `first`/`rest`/
   `extract`/`words`/`iter` — gained a `memchr` fast path for ASCII
   delimiters (safe because internal color and multi-byte UTF-8 bytes are
   all ≥ 0x80), measured at roughly −24% on list-walk benchmarks.

## Console and Memory Safety

 - The console input line and cursor are now clipped to the window width in
   `redraw_input()`, so long input no longer wraps onto the status bar and
   the cursor stays on-screen on narrow terminals.
 - The `atr_get`/`alloc_lbuf` RAII migration continued: `create_player`'s
   trimmed-password buffer is now an owning `LBuf`, so its several
   early-return paths free automatically.

## Build, Tests, and Documentation

 - A top-level `LICENSE` file (Artistic-1.0 plus the revised TinyMUD
   notice) and a `CONTRIBUTING.md` were added, and `AGENTS.md` was
   corrected to the 2.14 build workflow.
 - The smoke-test suite was substantially expanded with error-path and
   boundary coverage across many softcode functions (argument-count and
   width boundaries, divide-by-zero, no-match `switch`/`case`, Unicode and
   RTL handling, permission and malformed-syntax paths, and more).

# Changes in 2.14.0.8 (2026-JUN-15):

This release pairs a broad security and correctness pass with several
larger features: a unified date/time parser, a tiered storage cache with
speculative prefetch, a substantial hardening and expansion of the GANL
networking stack, and continued JIT/interpreter parity work.

The security and correctness pass covers the softcode evaluator, the
JIT/DBT pipeline, the @lock and command parsers, the database loaders, and
the player-authentication, comsys, and @mail subsystems.  Several of the
fixes close player-reachable crashes and denial-of-service conditions;
others close out-of-bounds reads/writes that could be triggered by a
corrupt, migrated, or maliciously crafted database file.  Sites are
tracked in docs/survey-*.md.

## New Softcode Functions

 - `vwidth()` — the visible display width of a string, accounting for
   color codes and wide/zero-width characters (the column count `wrap()`
   and the alignment functions actually use).
 - `cachestats()` — report attribute-cache statistics (size, hit/miss
   counts, queue depth) for tuning the tiered storage cache.

## Date and Time Parsing

 - `ParseDate` has been replaced by a unified Ragel `-G2` scanner feeding
   a recursive-descent parser, giving consistent handling of the many
   accepted date/time formats across the time-parsing surface
   (`convtime()`, `@convtime`, and friends).
 - Out-of-range years are now rejected rather than silently wrapped, and
   `do_convtime` no longer overflows on extreme year values. (#715)
 - A `mux_min(int, int)` size_t truncation that could make `ParseDate`
   fail outright was fixed, and a native fuzz/boundary harness was added.

## Tiered Storage and Caching

 - Stage 1 of the tiered storage cache: a configurable in-RAM attribute
   cache with a tunable size and depth-preload, in front of the SQLite
   backend.
 - Object-affinity speculative prefetch on a cache miss warms related
   attributes before they are asked for.
 - A write-behind cache with pinning and tombstones, plus batched,
   demand-driven SQLite writes, removes per-operation write latency from
   the hot path; `cachestats()` exposes the counters.

## Security and Robustness Fixes

 - Integer division/remainder of the most-negative 64-bit value by -1
   (`idiv`/`mod`/`remainder`/`floordiv`), and division by zero, no longer
   raise SIGFPE and crash the server.  Both the interpreter and the JIT-
   compiled fast paths are guarded.  This was a player-reachable
   denial-of-service. (#805, #811)
 - A deeply nested `@lock` key (for example, several hundred `!` operators)
   no longer overflows the C stack and crashes the server.  The lock parser
   now bounds its recursion depth, and its scratch buffer is no longer
   stack-allocated.  Any player able to `@lock` an object they own could
   trigger this. (#839)
 - The flatfile and SQLite database loaders now validate the dbrefs,
   attribute numbers, message numbers, and lock expressions they read.  A
   corrupt or crafted database can no longer drive an out-of-bounds write,
   an out-of-bounds read, an unbounded recursion, or a runaway allocation
   while loading objects, attributes, locks, @mail, or comsys channels.
   (#806, #807, #808, #810, #841, #843)
 - Logging in as a player whose stored password uses a hash method the
   local C library's `crypt()` cannot compute (for example, a `$6$`
   SHA-512 password database moved to a platform without it) no longer
   crashes the server; authentication now fails closed. (#842)
 - The just-in-time translator's instruction-emit, code-cache
   reconstruction, and Lua-bytecode paths now bound every buffer write and
   index, closing several out-of-bounds writes/reads reachable under code-
   buffer pressure or from a malformed compiled-code cache entry. (#830,
   #831, #832, #833)
 - The softcode expression (`[...]`) parser now caps its recursion depth,
   matching the evaluator, so pathologically nested input cannot overflow
   the stack on platforms with a small default stack size. (#840)
 - `justify()` no longer reserves a ~1.25 MB stack frame. (#818)
 - `scramble()` and `shuffle()` no longer overflow a stack buffer on large
   input.  Their Fisher-Yates index arrays were sized `LBUF_SIZE/2` while the
   grapheme-cluster / word count can reach `LBUF_SIZE`, so any player could
   smash the stack with a single ~32 KB argument.  Both arrays are now sized
   to `LBUF_SIZE`. (#845)
 - `examine` and the `flags()` softcode function no longer overflow a stack
   buffer when decoding attribute flags.  `decode_attr_flags()` can emit up
   to `NUM_ATTRIBUTE_CODES` letters plus a NUL, but two callers gave it an
   11-byte buffer; both now match the documented `NUM_ATTRIBUTE_CODES+1`
   contract.  Reachable by setting enough attribute flags on a readable
   object. (#846)
 - `@reference` and absolute named-reference lookups no longer read past the
   end of a buffer.  Both used `snprintf`'s return value (the length it would
   have written) directly as a copy/compare length; on truncation of a long
   reference name that exceeded the buffer, yielding an out-of-bounds read —
   and for `@reference`, the adjacent heap bytes could be stored and echoed
   back via `@reference/list`.  The length is now clamped to what was
   actually written. (#847)
 - `@cron` field and step parsing no longer accumulates digits into a signed
   `int` without bound; an over-long numeric field could overflow `int`
   (undefined behavior).  Parsing now bails once the value exceeds the field
   range. (#848)
 - `index()` no longer reads the byte preceding its argument buffer when an
   item begins at the delimiter (e.g. `index(|,|,1,1)`); the trailing-space
   trim now guards before decrementing the scan pointer. (#849)

## Softcode Correctness Fixes

A correctness sweep of the softcode function library, evaluator, and lock
and wildcard semantics (tracked in docs/survey-correctness-pass-2026-06.md)
found the following behavioral bugs:

 - `xor()` now tests the truthiness of each argument on its full 64-bit
   value, matching `and()`/`or()`/`lxor()`.  It had narrowed each argument
   to a 32-bit `int` first, so a non-zero value whose low 32 bits were zero
   (e.g. `xor(4294967296)`) was wrongly treated as false — and disagreed
   with its own list form `lxor()` on identical input. (#850)
 - `wrapcolumns()` no longer drops a character on every hard (no-space)
   line break.  The character at the break column was overwritten with a
   NUL and skipped; e.g. `wrapcolumns(abcdefghij,4,1)` lost the `e` and `j`.
   Each wrapped segment is now copied out with its own terminator so no
   input character is consumed by the break. (#851)
 - `step()` now evaluates the attribute as the object that owns it (like
   `map()`/`mix()`/`foreach()`), not as the calling executor, so `%!`/`me`
   and permission checks inside the stepped attribute resolve to the
   attribute's object.  This is a behavior change for `step()` callers that
   relied on the previous (inconsistent) identity. (#852)
 - `tr()` now requires its documented three arguments.  It was registered
   with a one-argument minimum, so `tr(abc)` reached the body and
   dereferenced the unallocated `<find>`/`<replace>` arguments; it now
   returns an argument-count error like its siblings. (#853)
 - `unique()` now honors its documented `<sorttype>` argument, which had
   been ignored (every comparison was a literal `strcmp`).  It uses the same
   type codes as `sort()` — `a` (default), `i` case-insensitive, `n`
   integer, `f` floating-point, `d` dbref, `u`/`c` Unicode — so e.g.
   `unique(1 2 2.0 3, f)` is `1 2 3` and `unique(a A b a, i)` is `a b`.
   The bogus `w=word` code (which exists in no related server) was removed
   from the help; an absent or unrecognized type remains a literal compare,
   so existing calls are unchanged. (#854)

## Command Correctness Fixes

A correctness sweep of the command-side verb handlers (tracked in
docs/survey-cmd-correctness-pass-2026-06.md):

 - `@clone/cost` on an exit now enforces the same "you must control your
   current location" rule as a plain `@clone` of an exit.  The locality
   check lived only on the non-`/cost` path, so `@clone/cost <exit>=<n>`
   let a builder splice a cloned exit into a room they do not control. (#855)
 - The `@mark`/`@mark_all`/`@apply_marked` "DB cleaning is enabled" refusal
   now points at the real command `@mark_all/clear` instead of the
   non-existent `@unmark_all` (which produced a "Huh?"). (#856)
 - `@ps <object>` now lists the queue of a controlled object owned by
   another player.  A residual owner filter (absent from the sibling
   `@halt`) made it report nothing for, e.g., a wizard inspecting an object
   they had `@chown`ed away. (#857)
 - `whisper "<quoted name>"=...` now applies the same too-far-away / not-
   connected filter as the unquoted form, so the sender no longer gets a
   success confirmation paired with a delivery error (and `A_LASTWHISPER`
   is not polluted with an unreachable target).  A related quoted-name
   path that could loop without advancing the parser was also fixed. (#858)
 - `@flag/remove` with an unknown or empty flag name now reports an error
   instead of silently doing nothing, matching the other flag-name failure
   paths. (#859)
 - The `report` help now says 4-hour segments, matching the code (the
   bucket size was changed to four hours but the help still said eight).
   (#860)

## JIT / DBT Engine

 - Numerous JIT-vs-interpreter result divergences were corrected so that
   JIT-compiled softcode produces byte-identical results to the
   interpreter: the compile-time folds of `t()`/`not()` on fractional-zero
   and whitespace inputs (#824); folds of `bound()` and `comp()` (#824);
   float function results fed into integer operations no longer truncate
   (#826); `trunc()` rounds toward zero (#827); `mod()` of negative
   operands is floor-mod (#828); and float `add()`/`sub()` use the same
   error-compensated summation as the interpreter (#829).
 - Tier-2 "blob" math now matches the interpreter for list reductions,
   `lnum()`/`space()` edge cases, and `ladd()`; `isdbref()` is handled by
   the engine; and `ladd()` is re-enabled with full parity. (#812, #813,
   #814, #815)
 - The RV64 Tier-2 environment gained a real `.bss`, static `.data`, and a
   guest heap, enabling more functions to run in compiled code.
 - The DBT now reclaims its x86-64 translation buffer when it nears full,
   so a long-running server no longer silently degrades JIT-compiled
   attributes back to the interpreter. (#834)
 - AArch64 DBT code generation fixes: `LOAD`/`STORE` with a zero base
   register and a nonzero immediate computed the wrong address; scratch-
   register clobbers with `x0` operands and an inverted conditional-select
   produced wrong results; and division by zero returned the wrong value.
   (#804, #809) (AArch64 is a non-default backend.)
 - More functions now run in JIT-compiled code with verified interpreter
   parity: `ldelete()` and `wordpos()` are re-enabled in Tier 2 with
   correct word-list / character-position semantics (#768); `ulambda()`
   bodies are routed through the evaluator so they compile (#718); and
   runtime-argument floating-point arithmetic is handled via the Tier-2
   blob (#778).
 - Further JIT result divergences were closed: `pos()` returns `#-1` (not
   `0`) when the substring is not found (#770); `ljust()`/`rjust()`/
   `center()` truncate when the width is smaller than the content (#772);
   multi-character `delim`/`osep` arguments fall back to the interpreter
   in the functions that mishandled them (#768, #782); empty list elements
   now survive `split_token` (#789); and COLOR-encoded output is
   byte-exact against the interpreter (#785, #787).
 - The JIT now reclaims its per-VM code arena on recompile instead of
   leaking it, keeps its arena registry and cursor `thread_local`, and
   tightens ECALL context isolation.
 - DBT JIT is now enabled on Apple Silicon (arm64 macOS).
 - The persistent (SQLite) compiled-code cache no longer serves stale
   entries.  Its staleness key folded in the hand-maintained tier-1
   compiler version and the Tier-2 blob, but not the tier-1 codegen
   itself, so a codegen change without a manual version bump could keep an
   old compiled program — including one that evaluated to the wrong (e.g.
   empty) result — matching and being served across an upgrade.  The cache
   key now also includes the engine build stamp, so a rebuild always
   invalidates previously persisted entries; and plain literal command
   text (with no function calls) is no longer compiled or cached, since
   the JIT adds nothing there.  (#844)

## Wildcard and String Matching

 - `?` in a wildcard pattern now matches one whole UTF-8 character rather
   than one byte, so `strmatch(café,????)` matches as expected. (#835)
 - Case-insensitive wildcard matching now folds non-ASCII letters (É/é,
   Ñ/ñ, …) from either side, in both the non-capturing matcher (`strmatch`,
   name/attribute/channel matching) and the capturing matcher used by
   `$`-commands and `^`-listens, while preserving original-case captures.
   (#836, #837)
 - A `?` immediately following a `*` no longer splits a multi-byte
   character across capture registers. (#838)
 - `accent()` with the `E`/`e` plus macron form now returns the correct
   character. (#816)

## Networking

This release brings a large hardening pass over the GANL networking stack
and the telnet / WebSocket / DNS protocol surface.

 - Site-ban bypass fixes: IPv4-mapped IPv6 source addresses are now
   canonicalized before site-rule matching, and subnet containment with a
   shared base/end address is computed correctly, so an IPv6-presented
   client can no longer slip past an IPv4 site ban. (#799, #800)
 - The `epoll` engine now handles the `epoll_wait()` error path (EINTR
   retry; real errors surfaced) instead of spinning, and completes the
   sibling error placeholders in event processing. (#791)
 - Reverse-DNS hostnames from the resolver slave are sanitized, the slave
   protocol buffers are bounded, and a purely numeric address can no
   longer pose as a resolved hostname. (#801)
 - The connection-handle-to-descriptor narrowing is guarded against
   truncation (#790), the descriptor re-entrancy guard is now state-based
   rather than reason-based (#802), and DESC teardown paths are hardened
   against stale handle mappings.
 - WebSocket: RFC 6455 conformance hardening (CLOSE / fragment-state / RSV
   / UTF-8 / 64-bit length / control-frame rules); a zero-payload frame
   ending exactly at a read boundary is now dispatched; the handshake flag
   is cleared before processing; and a Windows-IOCP double-free of per-I/O
   data on an immediate `WSASend`/`WSARecv` failure is fixed. (#792, #796)
 - Telnet: an IAC-encoding buffer overflow is fixed, the subnegotiation
   buffer is decoupled from `SBUF_SIZE`, parser limits are bounded, and
   descriptor field copies are clamped.  A one-byte stack overflow in the
   `slave.cpp` query buffer, and decimal-overflow / undefined-shift /
   digit-offset bugs in the `DecodeN` IPv4 parser, are fixed.
 - New negotiation support: configurable STARTTLS offers, NEW-ENVIRON
   parsing, CHARSET REQUEST list parsing, ANSI/MXP capability handling,
   and an OpenSSL key-password callback; an SSL session use-after-free and
   an unhandled `read()` EOF are fixed.  Config-file site rules are loaded
   through the driver bridge on demand. (#793, #797, #803)
 - The `sqlslave` helper now surfaces connection/query failures instead of
   dropping them, and ref-count races and buffer-ownership bugs are fixed.

## Engine and Database

 - `@dolist/now` runs multi-command bodies and honors `@break`, and inline
   command lists support `;|` piping. (#765, #788)
 - Comsys channels and @mail created in-game now survive a warm boot, and
   are cleared correctly on a forced game load. (#783)
 - Flatfile export no longer silently drops per-attribute owner and flags,
   and the SQLite import path is mistake-proofed against importing into a
   live database. (#766)
 - `wrap()` truecolor width is fixed; grapheme clusters are no longer split
   on interior color codes; and width / `strdistance` are cluster-aware for
   ZWJ emoji. (#716, #787)
 - SQLite backend: column reads are null-guarded, code-cache blobs are
   validated and the statement reset on failure, and the
   reset/rollback/checkpoint paths are corrected.

## Memory Safety and Reliability

 - A large RAII migration: ~75 `atr_get`/`atr_pget` sites and ~150
   `alloc_lbuf`/`free_lbuf` sites were converted to owning `LBuf` handles
   (`LBuf::adopt`, `LBufPtr`), removing manual free paths that could leak
   on an error return. (#717)
 - All static scratch buffers are now `thread_local`, and the reference
   counts across the comsys, @mail, lua, and exp3 modules — and the
   platform layer — are atomic.
 - The Lua undumper caps its `read_size()` varint length to prevent a
   `size_t` overflow, and validates compiled-code-cache blobs before use.

## Platform and Build

 - macOS arm64 build and test-rig support; the UTF-8 DFA tables were
   regenerated and the table-generator toolchain fixed on macOS.
 - Restart and file-descriptor handling hardened: `close_range`/`closefrom`
   in the boot-helper fast path, the real `rlim_cur` reported, and
   `PanicRestart` bounds its argv by argc and uses `execv`.
 - Generated-file safeguards: a pre-commit hook plus read-only Ragel output
   guard against hand-editing generated sources.
 - The long-dead `muxsvc/` Windows service stub was removed.

## Clients

 - TitanFugue gained interactive keyboard input for `read()`/`tfread()`
   (#758, #759, #760), a tokenized status-format-var cache (#761), an
   `nlog()` counter, and better Hydra reconnect / error diagnostics.
 - Credential storage is hardened across the console and Win32 GUI clients;
   web-client world passwords were moved out of localStorage; and the
   WorldBuilder gained TLS verification, attribute-escaping fixes, and
   expanded softcode danger checks.

## Other Fixes

 - `version()` no longer returns an empty string. (#817)

# Changes in 2.14.0.7 (2026-APR-03):

## New Softcode Functions

 - `graphemes()` — explode a string into its individual grapheme
   clusters as a space-delimited list.
 - `tr()` upgraded to grapheme-cluster-to-grapheme-cluster mapping.
   Previously operated on individual bytes; now correctly handles
   multi-byte grapheme clusters in both the source and replacement
   lists.
 - `butlast()` — return all but the last element of a list. Symmetric
   complement to `rest()`.
 - `zip()` — interleave two lists element-by-element without applying
   a function. `zip(a b c, 1 2 3)` produces `a 1 b 2 c 3`.
 - `posn()` — find the Nth occurrence of a substring within a string.
   Generalizes `pos()` which always returns the first match.
 - `wordstart()` and `wordend()` — return the grapheme offset of the
   start or end of the Nth word in a string.
 - `lxor()` — boolean parity (XOR) reduction across a list. Returns 1
   if an odd number of list elements are true.
 - `lband()`, `lbor()`, `lbxor()` — bitwise AND, OR, and XOR
   reductions across a list of integers. 64-bit arithmetic.
 - `limath()` — 64-bit integer list reduction with a user-specified
   operator (+, -, *, /, %, min, max, band, bor, bxor). Generalizes
   `lmath()` to the integer domain.
 - `strsort()` and `strunique()` — sort and deduplicate at the
   grapheme-cluster level. Operate on individual grapheme clusters
   within a string, not on list words.
 - `strunion()`, `strdiff()`, `strinter()` — grapheme-cluster-level
   set union, difference, and intersection. Treat each grapheme
   cluster as a set element.
 - `land()`, `lor()`, `lxor()` now document empty-list identity
   semantics: `land()` returns 1, `lor()` returns 0, `lxor()` returns
   0 on an empty list.
 - `cwho()`, `channels()`, `chanusers()`, `maillist()` now support
   offset/limit pagination for large result sets.

## Routing System

 - New room-to-room routing engine with four phases:
   - Phase 1: Static unconditional next-hop tables built by
     `@route/set`. `route()` and `routepath()` query functions.
   - Phase 2: Per-zone routing tables with cross-zone gateway
     selection via a meta-table.
   - Phase 3: Lock-validated routing — exit locks are checked at
     query time so locked paths are excluded from results.
   - Phase 4: `@walk` and `@patrol` NPC movement primitives. `@walk`
     moves an object along a computed route; `@patrol` cycles an
     object through a list of waypoints indefinitely.

## DBT Multi-Platform Backends

 - The dynamic binary translator has been refactored into pluggable
   platform backends with a common host-abstraction layer
   (`dbt_host.h`, `dbt_internal.h`, `dbt_jit_mem.h`).
 - x86-64 SysV backend (`dbt_x64_sysv.cpp`) extracted from the
   monolithic `dbt.cpp` — the existing Linux/FreeBSD/macOS path.
 - x86-64 Win64 backend (`dbt_x64_win64.cpp`) — Windows calling
   convention (RCX/RDX/R8/R9, shadow space, non-volatile XMM6-15).
   JIT is now enabled in the Windows build.
 - AArch64 AAPCS64 backend (`dbt_a64_sysv.cpp`) — ARM64 code
   generation with instruction fusion (Stage 7), superblock formation
   (Stage 3), and inline CALL (Stage 5). I-cache flush via
   `__builtin___clear_cache`.
 - `JIT_COMPILER_VERSION` added to the SQLite code cache key so that
   backend changes automatically invalidate stale cached blobs.
 - Platform dispatch is compile-time; `--disable-jit` builds cleanly
   stub all backend entry points.

## JIT Compiler Improvements

 - Peephole optimization pass: redundant load-after-store elimination,
   identity arithmetic removal, and dead-code cleanup.
 - Superblock optimization pass: merge straight-line basic blocks
   across unconditional edges to reduce branch overhead. Runs before
   SSA construction to avoid PHI complications.
 - Native HIR lowering for `log(value, base)` when the base is a
   compile-time constant — avoids ECALL overhead for common cases
   like `log(x, 2)` and `log(x, 10)`.
 - Re-enabled JIT fast paths for 12 string/list functions with full
   interpreter parity: `rest()`, `last()`, `squish()`, `elements()`,
   `remove()`, `edit()`, `replace()`, `insert()`, `splice()`,
   `log()`, and the trigonometric family (`sin`, `cos`, `tan`, `asin`,
   `acos`, `atan`, `atan2`) with angle-unit (degree/radian/grad)
   support.
 - ITOA/FTOA coercion for non-string ECALL arguments in `hir_lower`.
   Functions receiving numeric HIR values that expect string fargs now
   get an automatic coercion node instead of garbage output.

## JIT Bug Fixes

 - Fix `cand()` result handling — JIT was returning the branch
   condition instead of the final evaluated value when stored as an
   attribute and triggered.
 - Fix `cat()` dropping arguments when lowered as ECALL instead of
   STRCAT.
 - Fix `MEMBER` and `ROUND` constant folding producing incorrect
   results.
 - Fix arithmetic lowering to match interpreter semantics for
   floating-point boundary cases.
 - Disable JIT float EQ/NE lowering — IEEE 754 equality semantics
   differ between the DBT fast path and the interpreter's string
   comparison. Falls through to ECALL.
 - Disable Tier 2 JIT for `remove()` and `round()` pending full
   parity audit.
 - Fix `splice()` to use color-stripped comparison, matching the
   interpreter's behavior when ANSI color codes are present.

## Hardening and Reliability

 - All signal handlers are now async-signal-safe. Fatal signal
   handlers (`SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL`) use only
   `write()` and `_exit()`. `SIGHUP`, `SIGUSR1`, `SIGUSR2` set
   atomic flags that are checked in the main event loop.
 - Restart-file loading hardened: path traversal rejected, file size
   limited, descriptor range validated before `dup2`.
 - `ISOUTOFMEMORY` macro removed; each allocation site now has its
   own recovery path instead of a single global flag.
 - `g_dump_child_pid` fixed to prevent stale PID reuse after a failed
   `fork()` during `@dump`.
 - `PlayerNuke` guard added to prevent destruction of connected
   players.
 - Comsys `dbck()` false alarms fixed — channel consistency checks
   no longer flag valid channel state as corrupt.
 - Mail module false alarms fixed — mailbox validation no longer
   reports phantom inconsistencies on empty mailboxes.
 - Lua JIT: back-edge iteration budget added to prevent infinite loops
   from consuming unbounded CPU. Loops that exceed the budget bail to
   the Lua VM interpreter.

## Softcode Bug Fixes

 - Fix `split_words()` to handle non-space delimiters and empty input
   correctly. Previously returned a spurious empty element.

## Build System and Internals

 - `CPlatform` extracted from `modules.cpp` into `platform.cpp` for
   cleaner driver/engine separation.
 - `libmux.so` now builds with `-fvisibility=hidden`, reducing
   exported symbols from 665 to 351. Only the documented API surface
   is visible to engine.so and netmux.
 - `co_replace_at()` and `co_insert_at()` added to color_ops Ragel
   machine for ANSI-aware positional string replacement and insertion.
 - Duplicate help entries and stale dynhelp aliases removed.
 - Function alias table synced into JIT lookup after config load so
   that aliases defined in `mux.config` are JIT-eligible.
 - 917 smoke tests (up from 733 in 2.14.0.6). Systematic migration
   from SHA1-hash assertions to semantic assertions across 10 waves,
   exposing and fixing latent JIT parity bugs that opaque hashes were
   hiding.

# Changes in 2.14.0.6 (2026-MAR-26):

## JIT Compiler Improvements

 - Re-entrant JIT execution via shared heap DBT. Inner expressions
   reached through ECALL (e.g., `u()` bodies) now compile and execute
   in a persistent shared code heap with an independent DBT context.
 - Compile-time constant folding for 35+ functions: arithmetic (ADD,
   SUB, MUL, FDIV, IDIV, MOD, INC, DEC, ABS, SIGN, FLOOR, CEIL,
   TRUNC, ROUND, MAX, MIN, BOUND), comparison (EQ, NEQ, GT, GTE, LT,
   LTE, COMP), boolean (NOT, T), string (CAT, STRCAT, STRLEN, LCSTR,
   UCSTR, CAPSTR, REVERSE, ESCAPE, STRIPANSI, SQUISH, MID, POS,
   MEMBER, FIRST, REST, LAST, WORDS, EXTRACT, REPEAT, TRIM, EDIT,
   DELETE, RIGHT, ISNUM, ISINT, LPOS), and constants (PI, E, MUDNAME,
   VERSION). All use the same co_* Ragel or libmux implementations as
   the runtime evaluator — semantics-matched by construction.
 - STRLEN constant fold uses grapheme cluster counting (co_cluster_count)
   to match the runtime fun_strlen behavior. MID and DELETE use
   co_mid_cluster and co_delete_cluster respectively.
 - Code quality metrics in jitstats(): code_bytes (total/max),
   hir_insns (total/max), spills (register allocator spill count).
 - 50 JIT/AST parity tests comparing try_fold results against
   asteval() for all foldable functions.
 - JIT coverage measurement tests validating eval_attempts,
   eval_handled, compile_ok, and folded counters.

## JIT Bug Fixes

 - Fix silent memory corruption on string pool, fargs pool, and code
   size overflow. Pool overflow now sets a `pool_exhausted` flag and
   bails to the AST evaluator instead of using address 0.
 - Fix shared heap cache staleness: cached entries now track inline
   dependencies and validate via attr_mod_count on lookup. Stale
   entries from modified attribute bodies are evicted and recompiled.
 - Fix TRIM constant fold for multi-character patterns. Single-char
   patterns use co_trim; multi-char patterns fall through to the
   runtime co_trim_pattern path.
 - Fix DUMP_HIR crash from uninitialized block_first/block_last
   arrays. Phase 1 dump called hir_dump before hir_build_cfg computed
   block ranges.
 - Skip tier2_install for constant-folded programs (needs_jit=false).
   64% of compiled programs never touch the DBT — the 132KB blob copy
   was wasted for them.

## Performance

 - Eliminate 132KB tier2_install memcpy from compile_expression().
   The compilation pipeline never reads the blob region; callers that
   need it for runtime install it afterward.
 - Use tier2_reset_writable() in persistent VM prepare_run() instead
   of full tier2_install. Code and rodata are immutable after
   ensure_dbt() installs them once.
 - Disable WAL auto-checkpoint for SQLite. The WAL accumulates writes
   without fsync during normal operation; explicit checkpoint at @dump
   and shutdown handles persistence. Eliminates the 48% fsync
   bottleneck from code cache writes.
 - Add ASCII fast path to string_prefix(), string_compare(), and
   mux_stricmp(). Object name matching and command dispatch skip the
   full Unicode mux_tolower state machine for ASCII characters (<0x80),
   using a direct table lookup instead.
 - Remove rvbench from smoke suite — benchmarking tool was consuming
   86% of test runtime with no correctness assertions.

## Other

 - Native HIR handlers for default(), edefault(), localize(), letq().
 - mux_IPlatform COM interface replaces platform #ifdefs in signal
   handler and driver initialization.
 - WebSocket GameSession transport design and scaffolding.
 - Unicode collation fixes ported from libutf: pairs DFA, NFC
   tiebreaker, root collation.
 - Mail expiration, @dbclean, vlimit config parameter.
 - LBUF_SIZE increased from 8000 to 32768 with pool-backed LBuf RAII.

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

# Changes in 2.14.0.5 (2026-MAR-23):

 - Lua JIT Phase 3 shipped: runtime type guards with string-to-numeric
   promotion at all arithmetic and comparison sites; XMM register cache
   for floating-point (6-slot LRU in XMM2–XMM7); native `HIR_STRCMP`
   for string comparisons; integer fast-path for Lua table access
   (`HIR_LUA_GETI` bypasses string marshalling); pinned array
   optimization for native memory access in numeric for-loops;
   persistent SQLite cache for compiled Lua programs.
 - v2 blob format: flat image layout with BSS support and extended blob
   region, replacing the ELF-based format.
 - Comsys softcode accessors: `chaninfo()`, `chanusers()`,
   `chanuser()`, `chanfind()` — query channel configuration, membership,
   per-user status, and reverse-lookup by partial name. `chaninfo()`
   includes access-check fields.
 - Mail softcode accessors: `mailcount()`, `mailstats()`, `maillist()`,
   `mailinfo()`, `mailflags()` — query folder counts, read/unread/clear
   statistics, message lists, per-message metadata, and flag strings.
 - Help text for all new comsys and mail accessor functions.
 - Fixed `@clist/full` display and added access-check fields to
   `chaninfo()`.
 - Fixed `iter()` body evaluation: was incorrectly using the noeval
   branch path instead of direct eval.
 - Added 2.13-compatible noeval branch evaluation mode for backward
   compatibility with existing softcode.
 - Converted walkdb object list, DBT cache, and DBT patches from
   custom containers to STL.
 - Fixed `size_t` loop variable warnings in DBT patch iteration.

# Changes in 2.14.0.4 (2026-MAR-21):

 - JIT Tier 3: cross-attribute `u()` inlining. The JIT compiler can now
   inline the body of helper attributes called via `u()` directly into
   the caller's compiled code, eliminating ECALL overhead for hot paths.
   Per-attribute `mod_count` column (schema v8) tracks mutations so
   compiled code is automatically invalidated when any inlined dependency
   changes. Dependency vectors are stored alongside compiled programs in
   the SQLite code cache.
 - JIT re-entrancy guard: nested `u()` calls that would recursively
   invoke the JIT compiler now fall back to the AST evaluator.
   Previously, each nested call spawned (and destroyed) a new DBT
   environment — correct but unnecessarily slow. This allows the
   compiler to do more work per compilation unit without risk of
   recursive re-entry.
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

# Changes in 2.14.0.2 (2026-MAR-20):

 - Fixed incomplete distribution TOC files: many source files were
   missing from the unix and win32 distribution archives in 2.14.0.1.
 - Switched Windows build from /MT (static CRT) to /MD (dynamic CRT)
   and added `mux_fclose()` wrapper for cross-module FILE* handling.
 - Added `Startmux.bat` to the win32 distribution.
 - Added `softlib.rv64` to the win32 binary distribution.
 - Removed stale `buildnum.sh` from unix distribution.
