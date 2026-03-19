# WorldBuilder v2 — Design & Roadmap

Supersedes the original design-worldbuilder.md with lessons learned
from the working prototype.

## What We Built (v1 Prototype)

Working Python tool chain at `tools/worldbuilder/`:

| Tool | Purpose |
|------|---------|
| `worldbuilder.py check` | 10 DRC validation rules |
| `worldbuilder.py plan` | Human-readable execution plan |
| `worldbuilder.py compile` | MUX commands (full or incremental) |
| `worldbuilder.py diff` | Changes vs saved state |
| `executor.py apply` | Telnet bot, executes against live MUX |
| `executor.py verify` | Check live game matches spec |
| `executor.py rollback` | Destroy objects, clear state |
| `importer.py` | Walk live game → generate spec + state |

Spec features: rooms, exits (bidirectional), things, attributes,
flags, parents, zones, components with parameters and ports,
procedural grid generation.

4 components: apartment_unit, tavern, forest_area, street_grid.
4 test specs: park, apartments, forest, town.

~2,800 lines Python + YAML. All tests pass.

---

## Known Issues to Fix

### 1. Description Escaping

**Problem**: `.replace('\n', '%r')` is naive. Real MUX text has `%r`,
`%t`, `%b`, and literal `%` that needs escaping as `%%`.

**Fix**: Write a `mux_escape(text)` function that properly handles:
- `\n` → `%r`
- `\t` → `%t`
- Literal `%` in descriptions → `%%` (avoid substitution)
- Strip trailing whitespace per line

### 2. Exit Model Too Simple

**Problem**: Real exits have locks, messages, flags, costs.

**Fix**: Expand the exit spec:
```yaml
exits:
  - from: castle_gate
    to: courtyard
    name: "Gate;gate;north;n"
    lock: "flag(player, APPROVED)"
    desc: "A massive iron-bound oak gate."
    succ: "You push open the heavy gate."
    osucc: "pushes open the heavy gate."
    fail: "The gate is locked tight."
    ofail: "tries the gate, but it won't budge."
    drop: "The gate swings shut behind you."
    odrop: "arrives through the gate."
    flags: [DARK]
```

Compiler emits: `@lock`, `@desc`, `@succ`, `@osucc`, `@fail`,
`@ofail`, `@drop`, `@odrop`, `@set` for each.

### 3. Brittle Dbref Capture

**Problem**: Parsing `@dig` output varies across MUX versions.

**Fix**: After every `@dig/teleport`, always do `think %L` and parse
that response. Simpler, more reliable, works on all servers.

### 4. State Doesn't Track Content

**Problem**: `diff` knows if rooms exist but can't detect
description/attribute changes.

**Fix**: Store a content hash in the state file:
```yaml
objects:
  park_entrance:
    dbref: "#1234"
    type: room
    name: Park Entrance
    content_hash: "a1b2c3d4..."  # hash of desc+attrs+flags
```

Diff compares hashes. If hash differs, emit update commands.
Full content storage is too verbose — hashes are sufficient
for change detection.

### 5. Component Port Alias Conflicts

**Problem**: Multiple component instances connecting to the same
room can create exit alias conflicts that the DRC correctly catches
but the component author can't easily prevent.

**Fix**: Ports should declare whether back exits are auto-uniquified:
```yaml
ports:
  door:
    room: living_room
    back_unique: true  # auto-append instance ID to back exit aliases
```

Or: the component can use `${_instance_id}` in exit names for
explicit control.

---

## Roadmap: Phase 2

### A. @decomp / reformat / unformat Integration

**This is the highest-priority expansion.** It enables offline
development and round-trip game-as-code.

#### @decomp Import (online, one object at a time)

Replace the importer's per-attribute queries with:
```
@decomp/tf #1234
```

This outputs every attribute as `&ATTR #1234=value`, one per line.
Parse the output into the spec's attribute map. One command instead
of N queries per object — faster, complete, reliable.

#### reformat Import (offline, from flatfile)

TinyMUX ships `reformat` (`mux/src/tools/reformat.cpp`) which
converts a binary flatfile to human-readable text:

```
Object: #1234
Name: Park Entrance
Description: A wrought-iron gate...
ACONNECT: @pemit %#=Welcome.
Flags: FLOATING
Parent: #100
Zone: #50
Exits: #1235 #1236
```

WorldBuilder could parse reformat output into a spec.
**No running server needed.** Import from a backup.

#### unformat Output (offline, to flatfile)

TinyMUX ships `unformat` which converts human-readable text back
to flatfile entries. If the WorldBuilder compiler can emit
unformat-compatible format:

```
worldbuilder.py compile --format=unformat park.yaml > park.unformat
cat park.unformat | unformat >> game.flat
```

**Build an entire game world without a running server.**

#### Round-Trip

```
flatfile → reformat → worldbuilder import → spec.yaml (edit) →
worldbuilder compile --format=unformat → unformat → flatfile
```

Git-diffable, human-editable, LLM-assistable game content.

### B. Softcode Linting

Validate MUX softcode in attribute values:

| Check | Description |
|-------|-------------|
| Balanced brackets | `[function()]` — no unclosed `[` |
| Function existence | `pemit()` exists, `pmeir()` doesn't |
| Substitution syntax | `%#`, `%0`-`%9`, `%q0`-`%qz` are valid |
| Dangerous functions | `@force`, `@destroy` in attribute values |
| Common mistakes | `[pemit(%#,text)]` missing `=` in @-command context |

This is a *parser*, not an executor. Static analysis only.
Could use the existing `mux_functions.h` as the function dictionary.

### C. Multi-Zone Projects

A project file that lists all zone specs:
```yaml
# millhaven.project.yaml
project: Millhaven
zones:
  - zones/town_square.yaml
  - zones/residential.yaml
  - zones/market_district.yaml
  - zones/sewers.yaml

cross_references:
  downtown_square: town_square/town_square
  sewer_entrance: sewers/entrance
```

`worldbuilder.py plan millhaven.project.yaml` validates all zones
together, resolves `$cross_references`, checks for dangling refs.

### D. Visual Map Generation

The spec has the full topology graph. Generate:

1. **ASCII map** — printable, embeddable in game help text
2. **SVG/HTML map** — zoomable, clickable, linkable to room details
3. **DOT graph** — for Graphviz rendering

```
worldbuilder.py map park.yaml --format=ascii
worldbuilder.py map park.yaml --format=svg > park.svg
worldbuilder.py map park.yaml --format=dot | dot -Tpng > park.png
```

Uses the room grid coordinates for positioned layout, or
force-directed layout for non-grid specs.

### E. Description Grammars

Richer procedural text generation using Tracery-style grammars:

```yaml
grammar:
  adjective: [massive, ancient, twisted, towering, gnarled]
  tree: [oak, pine, birch, willow, elm]
  ground: [carpet of fallen leaves, thick moss, tangled roots]
  detail:
    - "Birds sing in the branches overhead."
    - "A squirrel watches from a high branch."
    - "The smell of damp earth fills the air."

description: |
  A ${adjective} ${tree} grows here, surrounded by ${ground}.
  ${detail}
```

Seeded selection ensures determinism — same coordinates always
produce the same description. Grammar rules can be shared across
components via `grammar_imports`.

### F. Migration System

Numbered, ordered spec changes:

```
migrations/
  001_initial_park.yaml
  002_add_fountain.yaml
  003_rename_garden.yaml
  004_add_bench_objects.yaml
```

Each migration has the full spec state at that point (like a
database migration). `worldbuilder.py migrate` applies them in
order, tracking which have been applied.

Enables: "I want to add a room to the park" as a discrete,
reviewable, revertible change.

### G. Test Harness

Automated testing for WorldBuilder itself:

```
worldbuilder-test.py --host test.server --port 4201
  1. Apply park.yaml → verify 3 rooms, 4 exits
  2. Modify description → incremental apply → verify change
  3. Rollback → verify rooms destroyed
  4. Apply apartments.yaml → verify 8 rooms, 14 exits
  5. Apply town.yaml → verify things created
  6. Import → compare generated spec to original
```

Requires a clean test MUX instance. Could run in CI against
a Docker container.

### H. LLM Integration Patterns

The spec language is designed for LLM assistance at Layer 1.
Specific integration patterns:

| Pattern | Prompt | Output |
|---------|--------|--------|
| Zone generation | "A medieval castle with 8 rooms" | castle.yaml |
| Description writing | "Write descriptions for these rooms: [list]" | desc block |
| Component design | "Design a shop component with inventory" | shop.yaml |
| Spec review | "Check this spec for inconsistencies" | feedback |
| Grammar authoring | "Create a swamp description grammar" | grammar block |

**Safety boundary**: LLM output is always YAML that goes through
the DRC. Never executes against the game directly. The compiler
is the firewall.

---

## Architecture Decision: Python vs C++

The prototype is Python. This is correct for now:
- Rapid iteration on spec language and DRC rules
- YAML parsing (PyYAML) is mature
- Telnet client is trivial
- Cross-platform with no compilation

**When to consider C++/Ragel rewrite:**
- If parsing performance matters (specs with 10,000+ rooms)
- If we integrate directly into the MUX server as a module
- If the DRC needs to run on every `@dig` in real-time

**Likely path**: Python stays for the tool chain. If we need a
server-side DRC (validate `@dig` commands as they're issued),
that would be a C++ module using Ragel for parsing.

---

## File Layout (Current)

```
tools/worldbuilder/
├── worldbuilder.py          — parser, DRC, compiler, diff
├── executor.py              — telnet bot, apply/verify/rollback
├── importer.py              — walk live game, generate spec
├── components/
│   ├── apartment_unit.yaml  — rentable apartment
│   ├── tavern.yaml          — 3 rooms + NPC barkeeper
│   ├── forest_area.yaml     — procedural wilderness
│   └── street_grid.yaml     — procedural city block
├── tests/
│   ├── park.yaml            — 3-room park (basic)
│   ├── apartments.yaml      — component instances
│   ├── forest.yaml          — procedural 4x4 forest
│   ├── town.yaml            — multi-component town
│   └── bad_park.yaml        — intentionally broken (DRC test)
└── .worldbuilder/           — state files (gitignored)
```

## Relationship to Existing TinyMUX Tools

| Tool | Purpose | WorldBuilder Integration |
|------|---------|------------------------|
| `reformat` | Flatfile → human-readable | Import source (Phase 2A) |
| `unformat` | Human-readable → flatfile | Compile target (Phase 2A) |
| `@decomp` | Object → attribute list | Import accelerator (Phase 2A) |
| `dbconvert` | Flatfile format conversion | None needed |
| Smoke tests | Game functionality tests | Test harness (Phase 2G) |
