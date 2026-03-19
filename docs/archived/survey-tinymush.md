# TinyMUSH 4.0 Survey

Surveyed: /tmp/tinymush (2026-03-16, updated from 2026-03-06)
Purpose: Identify features, patterns, or ideas worth borrowing for TinyMUX.

## Verdict

Slim pickings. TinyMUSH 4 is a modernization of TinyMUSH 3 (CMake, C11, LMDB
backend, module system) but the feature set is largely a subset of what MUX
already has, plus some questionable additions (structures, grid system). A few
items are worth noting.

No new commits since the 2026-03-06 survey. The codebase is unchanged.

---

## Potentially Interesting

### 1. CIEDE2000 Perceptual Color Distance (ansi.c)

TinyMUSH implements CIEDE2000 color distance for downgrading 24-bit color to
256-color or 16-color palettes. This uses CIELAB color space with sRGB gamma
correction and pre-computed Lab coordinates for each palette entry.

Their ansi.c is 3818 lines — substantial. The color pipeline works like this:
every `ColorEntry` in the 272-entry `colorDefinitions[]` table has pre-computed
CIELAB coordinates. When converting from RGB to a lower palette, they call
`ansi_rgb_to_cielab()` (sRGB inverse gamma, D65 reference white, CIE 1976
formulas) then `ansi_find_closest_color_with_lab()` which does a linear scan
of all entries of the target `ColorType`, computing CIEDE2000 distance for
each. The CIEDE2000 implementation is textbook — luminance/chroma/hue
corrections with the standard 25^7 constant (6103515625).

**Worth borrowing?** ~~Maybe.~~ **Done in 2.14** — MUX now uses CIELAB
(Euclidean) for nearest-neighbor palette search. CIELAB Euclidean is cheaper
than CIEDE2000 and perceptually adequate for the palette sizes involved
(16/256). CIEDE2000 adds chroma/hue corrections that matter for fine color
discrimination but not for mapping to a sparse palette. MUX's approach is
the right trade-off: correct color space, simpler distance metric.

### 2. LMDB as Storage Backend (db_backend_lmdb.c)

TinyMUSH supports LMDB (Lightning Memory-Mapped Database) alongside GDBM. LMDB
offers memory-mapped zero-copy reads, MVCC concurrency, and automatic page
coalescing. Starts at 1 GB map, grows to 16 GB.

**Worth borrowing?** No. MUX already moved to SQLite which provides the same
benefits plus SQL query capability, WAL mode, and broader ecosystem. LMDB
would be a lateral move.

### 3. @cron / @crontab / @crondel

Unix-style cron scheduling for timed attribute triggers. Full cron syntax
(minute, hour, DOM, month, DOW, ranges, steps). Tasks stored as attributes on
objects, checked every server pulse. Bitmap-based storage.

**Worth borrowing?** ~~Interesting for game builders.~~ **Done in 2.14** — @cron/@crondel/@crontab implemented with Vixie-style computed next-fire-time scheduling (no polling loop). Supports ranges, lists, steps, month/day-of-week names, DOM/DOW OR-semantics.

### 4. benchmark() Function

`benchmark(<expression>, <count>)` — evaluates an expression N times and
returns execution time. Useful for softcode performance testing.

**Worth borrowing?** Possibly. Simple to implement, useful for coders
optimizing complex softcode.

### 5. Module System (api.c, src/modules/)

Dynamic module loading via dlsym() with function pointers for command
interception, object lifecycle hooks, connection events, and database export.
Comsys and mail are implemented as modules.

Their module API (conf_modules.c) resolves ~16 well-known symbol names per
module via `dlsym_format()`: `process_command`, `process_no_match`, `did_it`,
`create_obj`, `destroy_obj`, `create_player`, `destroy_player`,
`announce_connect`, `announce_disconnect`, `examine`, `dump_database`,
`db_grow`, `db_write`, `db_write_flatfile`, `do_second`, `cache_put_notify`,
`cache_del_notify`. Modules also call `register_commands()`,
`register_functions()`, and `register_hashtables()` during init. The skeleton
module template (src/modules/skeleton/) is well-documented with clear patterns
for commands, functions, config directives, and hash tables.

**Worth borrowing?** No. MUX's COM-based module system (typed interfaces with
QueryInterface, AddRef, Release) is architecturally superior — it provides
binary-compatible versioned interfaces rather than relying on dlsym name
conventions. MUX 2.14 now has comsys and mail as COM modules too, plus the
engine itself is a COM module in engine.so. The dlsym approach is simpler but
fragile (no versioning, no interface discovery, silent NULL on typos).

### 6. @colormap Command (netcommon.c)

Per-connection ANSI color remapping. Players can remap any of the 18 SGR
foreground/background colors to another. Stored as an `int[18]` array on the
descriptor. Applied during output postprocessing — a streaming filter that
intercepts SGR sequences and rewrites the color parameter.

The output postprocessing pipeline (PostprocessStreamContext) is a single-pass
streaming filter that handles both NoBleed (reset injection at line boundaries)
and colormap remapping. It parses ANSI escape sequences character by character,
tracks ColorState, and rewrites SGR parameters on the fly.

**Worth borrowing?** Low priority. The per-connection colormap is a nice
accessibility feature (e.g., remapping dark blue to bright blue for
readability) but it only works on the 16 basic ANSI colors, not on 256-color
or truecolor. MUX's color architecture already handles depth negotiation and
downgrade; per-player colormap would be a small addition if there's demand.

---

## Not Worth Borrowing

### Structures System (construct/destruct/structure/unstructure/etc.)

TinyMUSH has a "structures" feature — named record types with typed fields
that can be instantiated on objects. Functions: CONSTRUCT, DESTRUCT,
STRUCTURE, UNSTRUCTURE, LSTRUCTURES, LINSTANCES, MODIFY, LOAD, UNLOAD, READ,
WRITE.

This is the "records or some nonsense" — it's a complex system that adds
structured data types to softcode. In practice, MUSH builders use delimited
strings and lists for structured data. The structures system adds complexity
without enough payoff. Nobody asked for this.

### Grid System (grid/gridmake/gridset/gridsize)

2D grid manipulation functions. Niche use case (board games, maps). Trivially
implementable in softcode.

### Stack Functions (push/pop/peek/swap/dup/toss/empty/lstack)

Per-object LIFO stacks. MUX has q-registers and named registers which serve
the same purpose more flexibly. Stacks are a TinyMUSH 3 holdover.

### Named Variables (xvars/clearvars/lvars/store/x/z/qvars/qsub)

Extended variable system beyond basic q-registers. MUX already has setq/setr
with named registers (setq(0, val) or setq(name, val)), making most of this
redundant.

### Speak() Function

Formats speech output. Softcode can do this.

### Iter2/Itext2/List2

Parallel iteration variants. MUX's iter/parse with multiple lists via mix()
and munge() covers this.

### Boolean Variants (andbool/orbool/candbool/corbool/notbool/xorbool/etc.)

Boolean-specific versions of logical operators. MUX's existing and/or/not with
t() already handle this.

### Deprecated/Legacy Functions

IFZERO, IFTRUE, IFFALSE, ISFALSE, ISTRUE, NONZERO — all trivially expressed
with if/ifelse/t/not in MUX.

---

## Function Delta Summary

### TinyMUSH has, MUX doesn't (145 functions)

Most are in the "not worth borrowing" categories above. The few with any
merit:

| Function | Description | Interest |
|----------|-------------|----------|
| BENCHMARK | Time softcode execution | Low |
| CHOMP | Strip trailing newline | Trivial |
| DIFFPOS | Position of first difference between strings | Low |
| HELPTEXT | Read help file entries from softcode | Low |
| NATTR | Count of attributes on object | MUX has ATTRCNT |
| STREQ | Case-insensitive string equality | MUX has comp() and strmatch() |
| URL_ESCAPE / URL_UNESCAPE | URL encoding | **Done in 2.14** (RFC 3986) |
| WILDGREP / WILDMATCH / WILDPARSE | Wildcard variants of grep/match | Low |
| WHILE / UNTIL | Loop constructs | MUX has iter/loop patterns |

### MUX has, TinyMUSH doesn't (107 functions)

MUX is well ahead in: mail functions, comsys functions, channel functions,
color depth support (CHR, ORD, COLORDEPTH), cryptographic functions (SHA1,
DIGEST, CRC32), SQL integration, reality levels, time formatting variants,
telnet/terminal info, and Unicode support.

---

## Commands Delta

Notable TinyMUSH commands MUX doesn't have:

| Command | Description | Interest |
|---------|-------------|----------|
| @cron/@crontab/@crondel | Cron-style scheduling | **Done in 2.14** |
| @addcommand/@delcommand | Dynamic command creation (GOD-only) | MUX has @addcommand |
| @hook | Pre/post command hooks | MUX has @hook |
| @redirect | Output redirection | Low |
| @colormap | ANSI color palette remapping | Low (16-color only) |
| @floaters | Find objects with invalid locations | Low (diagnostic) |
| @robot | Create robot/NPC | MUX uses @pcreate + ROBOT flag |

Most of these MUX already has or doesn't need.

---

## Architecture Notes

- **Build system:** CMake (vs MUX's autoconf). Their CMake setup is
  competent but straightforward: ccache auto-detection, LTO for Release
  builds, architecture-aware memory alignment, git-tag-based versioning
  (version.cmake extracts commit count since base tag as the tweak number),
  config-file preservation during install (backup + skip-if-exists pattern),
  and CPack integration. The `modules.cmake` file is a simple list of
  `add_subdirectory()` calls with comment-to-disable. Module CMakeLists are
  minimal (4 lines each). No clever patterns worth borrowing — MUX's
  autoconf setup handles the same concerns differently but equivalently.
- **Networking:** select()-based event loop. MUX's GANL (epoll/kqueue) is
  more modern and performant.
- **DNS:** Background thread with SysV message queues. MUX uses a slave
  process. Both work; MUX's approach is more portable.
- **Eval:** TinyMUSH has a similar token-based parser with a separate
  `parse_to_cleanup()` pass. MUX 2.14 replaced this with an AST-based
  evaluator — Ragel-generated scanner, parsed into AST, LRU cache (1024
  entries), and a DBT/JIT compiler (softcode -> AST -> RV64 -> x86-64). A
  massive architectural divergence.
- **Color handling:** TinyMUSH's ansi.c (3818 lines) is a clean
  implementation with well-separated concerns: ColorState/ColorInfo structs,
  CIELAB conversion, CIEDE2000 distance, a 272-entry pre-computed color
  table, streaming output postprocessing (NoBleed + colormap), and
  per-player color depth flags (ANSI/COLOR256/COLOR24BIT on FLAG_WORD2/3).
  They handle depth downgrade at output time via `resolve_color_type()`.
  They do NOT use PUA encoding for inline color — their %x codes generate
  ANSI escape sequences directly during eval, then the output postprocessor
  handles NoBleed and colormap. MUX's approach of encoding color state into
  PUA codepoints in the string (V5 two-code-point format) and deferring ANSI
  generation to the final output stage is more flexible — it preserves color
  information through string manipulation functions (mid, left, edit, etc.)
  where TinyMUSH would lose it. The PUA approach is architecturally superior
  for a system that does heavy string processing.
- **Queue:** Split across 7 files (cque*.c). Has explicit PID allocation
  per queue entry and economy-tied queue costs. MUX's queue is comparable.
- **Flags:** 3-word (96 flags max) vs MUX's 4-word (128 flags). Comparable.
- **Regex:** Both use PCRE2.
- **RNG:** TinyMUSH uses standard rand()/random(). MUX 2.14 uses PCG-XSL-RR-128/64.
- **Storage:** TinyMUSH offers GDBM or LMDB (mutually exclusive at build
  time). MUX uses SQLite with write-through.
- **Modules:** TinyMUSH uses dlsym() with naming conventions
  (`mod_<name>_<hook>`) to resolve ~16 function pointers per module. MUX
  2.14 uses COM interfaces (QueryInterface/AddRef/Release) with typed
  interface IDs and class factories. MUX's comsys and mail are now COM
  modules; the engine itself is a COM module in engine.so. The COM approach
  provides interface versioning, binary compatibility, and runtime
  discovery — none of which the dlsym approach offers.

---

## Bottom Line

TinyMUSH 4.0 is a competent modernization of TinyMUSH 3 but hasn't meaningfully
expanded the feature set. The codebase is clean and well-organized (good file
splitting, modern C practices, thorough Doxygen comments) but there's almost
nothing here that MUX doesn't already do as well or better.

MUX 2.14 has now addressed both the @cron item and the perceptual color
distance item (the two features with borrowing potential) and pulled further
ahead architecturally: AST-based evaluator with Ragel scanner, parse cache,
and JIT compilation; PCRE2; PCG-XSL-RR-128/64 RNG; CIELAB nearest-neighbor
palette search; PUA-based inline color encoding; SQLite write-through storage;
and a three-layer architecture (libmux.so/engine.so/netmux) with COM
interfaces. The gap between the two codebases has widened considerably.

The structures/grid/stack features represent a design philosophy of adding
engine-level primitives for things that softcode handles adequately. MUX's
approach of keeping the engine lean and letting softcode handle application
logic is the better trade-off.

Next survey: PennMUSH (expected to have more interesting divergence).
