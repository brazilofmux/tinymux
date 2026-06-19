# Survey: memory-safety pass over unsurveyed engine subsystems (June 2026)

A memory-safety / undefined-behavior audit of the 30 engine and networking
subsystems that had no sharp safety survey yet, continuing the methodology of
the parser / JIT / wild / comsys / mail campaign (deserializers and
player-facing / network-facing paths, with an eye for OOB read/write, integer
overflow feeding an index or allocation, UTF-8 boundary off-by-ones,
use-after-free, and non-literal format strings).

**Methodology.** One finder per subsystem read the file in full (plus the
relevant callers, structs, and macros) and reported only defensible issues with
a concrete path from untrusted input — a player command/function argument,
network bytes, an on-disk DB/flatfile, or a Lua value — to the unsafe
operation. Every candidate finding was then put through two adversarial
verifiers with opposing lenses: one tried to **refute** it by locating the
guard/clamp/invariant that makes the code safe, the other tried to **confirm**
it by constructing a concrete hostile-input trigger. A finding is "confirmed"
only where both verifiers independently agreed it is real and reachable.

**Result: 6 confirmed bugs fixed (2 player-reachable HIGH stack overflows,
2 heap OOB reads including a heap info-leak, 1 twin, 1 overflow-UB), plus the
one contested low-severity read. The remaining 24 subsystems — including the
untrusted-network parsers (telnet, websocket, netaddr), the 16k-line softcode
function library, and the config/flag deserializers — were found memory-safe
with documented methodology.** Fixes and regression tests are described below;
CHANGES.md tracks them as #845–#849.

## Confirmed findings (fixed)

### #845 — `scramble()` / `shuffle()` stack buffer overflow (HIGH, player-reachable)

`funceval2.cpp` `fun_scramble` (line ~116) and `fun_shuffle`'s single-char
delimiter path (line ~184) declared their Fisher-Yates index arrays as
`LBUF_OFFSET indices[LBUF_SIZE/2]` (16384 entries), but the count that
populates them — `co_cluster_count()` for scramble (one cluster per ASCII byte)
and `co_words_count()` for shuffle (1 + delimiter count for a non-space
single-char delimiter) — can reach ~`LBUF_SIZE`. A single function argument is
a full `LBUF_SIZE` (32768-byte) lbuf, so any player calling
`scramble(<~32 KB string>)` or `shuffle(<~32 KB list>,<char>)` overflowed the
stack array by up to ~16384 `uint16_t` entries. Both functions are registered
`CA_PUBLIC`. The multi-char path of `fun_shuffle` already correctly used
`indices[LBUF_SIZE]`. **Fix:** size both arrays to `LBUF_SIZE`. Regression:
`overflow_inject_fn.mux` TC012/TC013.

### #846 — `decode_attr_flags()` undersized buffer (HIGH)

`look.cpp` `view_atr` (line ~885) and `functions.cpp` `fun_flags` (line ~512)
passed an `UTF8 xbuf[11]` to `decode_attr_flags()`, whose prototype contract is
`UTF8 buff[NUM_ATTRIBUTE_CODES+1]` with `NUM_ATTRIBUTE_CODES == 13`. The
function emits one letter per set attribute-flag bit (13 distinct, all
user/wiz-settable via `@set obj/attr=…` and `@lock`) plus a NUL terminator, so
an object with enough attribute flags set drove a 3-byte out-of-bounds stack
write when examined or passed to `flags()`. **Fix:** size both buffers to
`NUM_ATTRIBUTE_CODES+1`.

### #847 — `snprintf`-return-as-length OOB read in named references (HIGH / MEDIUM)

`predicates.cpp` `do_reference` (line ~3000) and `match.cpp`
`absolute_named_reference` (line ~199) formatted `"%s.%ld"` into an
`LBUF_SIZE` buffer and used `snprintf`'s **return value** directly as a copy /
compare length. `snprintf` returns the length it *would* have written; when a
long reference name truncates, that return exceeds the buffer, so the
subsequent `std::vector<UTF8>(p, p + n)` range and `StringCloneLen(p, n)` read
past the end of the lbuf. In `do_reference` the over-read heap bytes are stored
into the persisted `reference_entry->name` and can be echoed back to a player
via `@reference/list` — a heap info-leak in addition to the OOB read. Both
sites are reachable with a single ~32 KB `@reference <name>=obj` argument or a
`look #_<name>` match. **Fix:** clamp the length to `min(n, LBUF_SIZE-1)`.

### #848 — `@cron` signed-integer overflow (LOW, UB)

`cron.cpp` `parse_cron_value` (line ~200) and the step loop in
`parse_cron_field` (line ~283) accumulated player-supplied digits into a signed
`int` (`val = val*10 + …`) with no digit cap and no overflow guard, so an
over-long numeric cron field overflowed `int` (undefined behavior). No memory
corruption results — the value is range-checked and only ever clamps a loop
bound — but the UB is closed by bailing once the accumulator exceeds the field
range. Reachable via `@cron <obj>/<attr> = 999999999999 * * * *`.

### #849 (contested → fixed) — `index()` 1-byte pre-buffer read (LOW)

`functions.cpp` `fun_index` (line ~3765): when a list item begins at the
delimiter (e.g. `index(|,|,1,1)`), the trailing-space trim `do { p--; } while
(…)` decremented before testing, reading `*(fargs[0]-1)`. Verifiers split:
the byte lies inside the lbuf's pool-header metadata (same allocation, so not a
true heap OOB), but it is UB-adjacent. **Fix:** guard-before-decrement
(`while (p > s && p[-1] == ' ') p--;`), preserving the original
trim-and-terminate semantics. Regression: `overflow_inject_fn.mux` TC014.

## Subsystems found memory-safe

Each was read in full with callers/structs cross-checked; the note records the
principal surfaces verified.

| Subsystem | Verdict | Notes |
|-----------|---------|-------|
| functions.cpp (1–8200) | clean¹ | output-buffer `nMax` clamp idiom before every memcpy; `co_*` color helpers bound to `out+LBUF_SIZE-1`; `db[]` indices gated by `Good_obj()`; env-var arrays `[NUM_ENV_VARS]` safe because dispatch clamps `nfargs` to `maxArgs`. ¹only the `index()` read above. |
| functions.cpp (8000–16002) | clean | edit/printf/strip/trim/tr/strreplace memcpy lengths capped; time buffers sized per worst-case; `chr` raw builder bounded with explicit end guard; `global_regs[]` clamped to `[0,MAX_GLOBAL_REGS)`; accent tables indexed within range; all printf formats are literals. |
| funceval.cpp | clean | every fixed buffer traced to its writer; `co_*` fills bounded; `xargs[MAX_ARG]` safe via evaluator clamp; UTF-8 boundary reads NUL-protected; allocations freed exactly once on all paths. |
| funmath.cpp | clean | shared `g_aDoubles[MAX_WORDS]` and `vals[]` bounded by the same `MAX_WORDS` list cap; no atoi-driven allocations or indices. |
| funcweb.cpp | clean | url/html/json string handling over untrusted args; output bounded by the standard clamp idiom. |
| engine_com.cpp | clean | engine↔comsys bridge; `CResultsSet` row access bounds-checked. |
| speech.cpp | clean | do_say SAY_PREFIX byte-peek at `message[0..2]` for the U+201C case is NUL-protected; shout/page/pemit message formatting bounded. |
| set.cpp | clean | all `@set`/`@name`/`@alias`/`@lock`/`@chown`/`@power`/`@edit` handlers traced from player input. |
| object.cpp | clean | create/destroy/ownership; player-writable attribute and on-disk DB paths bounded. |
| move.cpp | clean | movement / teleport paths. |
| create.cpp | clean | object-name / cost / link-target / password input bounded. |
| cque.cpp | clean | command queue; player-supplied queued strings; helper signatures cross-checked. |
| match.cpp | clean² | name matching from player input. ²only the named-reference read above. |
| routing.cpp | clean | `route()` player-controlled dbref/option path traced. |
| conf.cpp | clean | all `cf_*` CONFPARM handlers, conftable dispatch, `cf_set`/`do_admin`/`cf_read`, access control, display/list. |
| flags.cpp | clean | every fixed-array index / buffer write checked against flags.h/db.h definitions. |
| levels.cpp | clean | reality levels; backing structs/callers cross-checked. |
| wiz.cpp | clean | teleport/force/toad/newpassword/boot/poor/cut/motd/global. |
| walkdb.cpp | clean | `@search`/dump; every index/buffer/allocation from player input traced. |
| log.cpp | clean | every buffer-writing and format-string site; no player-controlled non-literal formats. |
| lua_mod.cpp | clean | Lua scripting bridge; untrusted script values crossing the boundary. |
| hir_lower_lua.cpp | clean | Lua HIR lowering; `HIR_MAX_INSNS` / bytecode field accessors bounded. |
| engine.cpp | clean | core dispatch; player-arg / attribute / network / flatfile paths. |
| telnet.cpp | clean | `process_input_helper` fed raw network bytes by the GANL adapter; option negotiation bounded. |
| websocket.cpp | clean | framing parser against `WS_MAX_PAYLOAD` (65536); `ws_state` handling bounded. |
| netaddr.cpp | clean | address parsing/formatting. |

## Provenance

Generated by the `memsafety-audit` workflow (30 finders + adversarial
double-verification, 44 agents). The full structured result — per-finding
verifier votes and per-subsystem methodology — is archived with the run.
