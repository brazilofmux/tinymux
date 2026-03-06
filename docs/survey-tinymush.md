# TinyMUSH 4 Survey

Surveyed: /tmp/tinymush (2026-03-06)
Purpose: Identify features, patterns, or ideas worth borrowing for TinyMUX.

## Verdict

Slim pickings. TinyMUSH 4 is a modernization of TinyMUSH 3 (CMake, C11,
LMDB backend, module system) but the feature set is largely a subset of what
MUX already has, plus some questionable additions (structures, grid system).
A few items are worth noting.

---

## Potentially Interesting

### 1. CIEDE2000 Perceptual Color Distance (ansi.c)

TinyMUSH implements CIEDE2000 color distance for downgrading 24-bit color to
256-color or 16-color palettes. This uses CIELAB color space with sRGB gamma
correction and pre-computed Lab coordinates for each palette entry.

MUX currently does simple index mapping for color downgrade. CIEDE2000 would
produce better visual results when a game converts between color depths or
when a player's terminal doesn't support 24-bit.

**Worth borrowing?** Maybe. It's a nice-to-have for color approximation. Low
priority since MUX games generally author for their target depth.

### 2. LMDB as Storage Backend (db_backend_lmdb.c)

TinyMUSH supports LMDB (Lightning Memory-Mapped Database) alongside GDBM.
LMDB offers memory-mapped zero-copy reads, MVCC concurrency, and automatic
page coalescing. Starts at 1 GB map, grows to 16 GB.

**Worth borrowing?** No. MUX already moved to SQLite which provides the same
benefits plus SQL query capability, WAL mode, and broader ecosystem. LMDB
would be a lateral move.

### 3. @cron / @crontab / @crondel

Unix-style cron scheduling for timed attribute triggers. Full cron syntax
(minute, hour, DOM, month, DOW, ranges, steps). Tasks stored as attributes
on objects, checked every server pulse. Bitmap-based storage.

**Worth borrowing?** Interesting for game builders who want scheduled events
without softcode timer loops. MUX has @wait and @daily but no cron-style
granularity. Low effort to implement but niche demand.

### 4. benchmark() Function

`benchmark(<expression>, <count>)` — evaluates an expression N times and
returns execution time. Useful for softcode performance testing.

**Worth borrowing?** Possibly. Simple to implement, useful for coders
optimizing complex softcode.

### 5. Module System (api.c, src/modules/)

Dynamic module loading via dlsym() with function pointers for command
interception, object lifecycle hooks, connection events, and database export.
Comsys and mail are implemented as modules.

**Worth borrowing?** MUX already has a module system. TinyMUSH's approach of
making comsys and mail into modules is interesting architecturally but MUX
has already integrated these tightly. No action needed.

---

## Not Worth Borrowing

### Structures System (construct/destruct/structure/unstructure/etc.)

TinyMUSH has a "structures" feature — named record types with typed fields
that can be instantiated on objects. Functions: CONSTRUCT, DESTRUCT,
STRUCTURE, UNSTRUCTURE, LSTRUCTURES, LINSTANCES, MODIFY, LOAD, UNLOAD,
READ, WRITE.

This is the "records or some nonsense" — it's a complex system that adds
structured data types to softcode. In practice, MUSH builders use
delimited strings and lists for structured data. The structures system adds
complexity without enough payoff. Nobody asked for this.

### Grid System (grid/gridmake/gridset/gridsize)

2D grid manipulation functions. Niche use case (board games, maps).
Trivially implementable in softcode.

### Stack Functions (push/pop/peek/swap/dup/toss/empty/lstack)

Per-object LIFO stacks. MUX has q-registers and named registers which serve
the same purpose more flexibly. Stacks are a TinyMUSH 3 holdover.

### Named Variables (xvars/clearvars/lvars/store/x/z/qvars/qsub)

Extended variable system beyond basic q-registers. MUX already has setq/setr
with named registers (setq(0,val) or setq(name,val)), making most of this
redundant.

### Speak() Function

Formats speech output. Softcode can do this.

### Iter2/Itext2/List2

Parallel iteration variants. MUX's iter/parse with multiple lists via mix()
and munge() covers this.

### Boolean Variants (andbool/orbool/candbool/corbool/notbool/xorbool/etc.)

Boolean-specific versions of logical operators. MUX's existing and/or/not
with t() already handle this.

### Deprecated/Legacy Functions

IFZERO, IFTRUE, IFFALSE, ISFALSE, ISTRUE, NONZERO — all trivially
expressed with if/ifelse/t/not in MUX.

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
| URL_ESCAPE / URL_UNESCAPE | URL encoding | Could be useful for HTTP integration |
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
| @cron/@crontab/@crondel | Cron-style scheduling | Moderate |
| @addcommand/@delcommand | Dynamic command creation (GOD-only) | MUX has @addcommand |
| @hook | Pre/post command hooks | MUX has @hook |
| @redirect | Output redirection | Low |
| @colormap | ANSI color palette management | Low |
| @floaters | Find objects with invalid locations | Low (diagnostic) |
| @robot | Create robot/NPC | MUX uses @pcreate + ROBOT flag |

Most of these MUX already has or doesn't need.

---

## Architecture Notes

- **Build system**: CMake (vs MUX's autoconf). Neither is clearly better.
- **Networking**: select()-based event loop. MUX's GANL (epoll/kqueue) is
  more modern and performant.
- **DNS**: Background thread with SysV message queues. MUX uses a slave
  process. Both work; MUX's approach is more portable.
- **Eval**: Similar token-based parser. Has a separate `parse_to_cleanup()`
  pass. Uses configurable stack limit. No fundamental differences.
- **Queue**: Split across 7 files (cque*.c). Has explicit PID allocation
  per queue entry and economy-tied queue costs. MUX's queue is comparable.
- **Flags**: 3-word (96 flags max) vs MUX's 4-word design.

---

## Bottom Line

TinyMUSH 4 is a competent modernization of TinyMUSH 3 but hasn't
meaningfully expanded the feature set. The codebase is clean and
well-organized (good file splitting, modern C practices) but there's almost
nothing here that MUX doesn't already do as well or better. The CIEDE2000
color matching is the one genuinely clever implementation detail.

The structures/grid/stack features represent a design philosophy of adding
engine-level primitives for things that softcode handles adequately. MUX's
approach of keeping the engine lean and letting softcode handle application
logic is the better trade-off.

Next survey: PennMUSH (expected to have more interesting divergence).
