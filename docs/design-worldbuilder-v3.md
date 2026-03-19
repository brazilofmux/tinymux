# WorldBuilder v3 — Engineering Roadmap

Supersedes v2. All v2 phases (2A-2H) are complete. v3 focuses on
fundamental engineering problems, not features.

## What We Have (v2 Complete)

14 Python files, ~8,400 lines, 61 automated tests.

| Tool | Commands |
|------|----------|
| worldbuilder.py | check, lint, plan, compile, diff |
| executor.py | apply, verify, rollback |
| importer.py | walk + generate |
| project.py | check, plan, lint, compile (multi-zone) |
| mapgen.py | ascii, dot |
| softcode_lint.py | standalone lint |
| grammar.py | expand, generate |
| migrate.py | status, up, up-all, down, create |

4 components (apartment, tavern, forest, street_grid), 2 grammars
(nature, urban), 7 test specs, 1 project file, 3 test migrations.

---

## v3 Priorities (Engineering, Not Features)

### 1. Reconciliation — The Hand-Edit Problem

**Problem:** Builder creates zone via WorldBuilder. Another builder
hand-edits a description in-game. State file is now stale. Next
apply silently overwrites the hand-edit.

**Solution: `worldbuilder.py reconcile`**

1. Connect to game, read current state of all managed objects
2. Compare live state against spec AND against saved state
3. Three-way diff:
   - Spec says X, state says X, live says X → no conflict
   - Spec says X, state says X, live says Y → **live-modified**
     (someone hand-edited; prompt user)
   - Spec says Y, state says X, live says X → **spec-modified**
     (normal change; apply)
   - Spec says Y, state says X, live says Z → **conflict**
     (both changed; requires manual resolution)
4. Output a reconciliation report with choices per object
5. Update state file to match live reality

This is `terraform refresh` + `git merge` for game objects.

**Implementation:**
- Extend importer's query functions to read specific objects by dbref
- Add `content_hash` comparison: spec hash vs state hash vs live hash
- New CLI action: `worldbuilder.py reconcile <spec> --host ...`
- Reconciliation report format: per-object accept-local / accept-remote / conflict

### 2. Destructive Changes — Delete and Remove

**Problem:** Removing a room from the spec generates a diff showing
"DELETE room X" but the compiler emits nothing. No `@destroy`.
No attribute removal. The spec is additive-only.

**Solution: Explicit destroy and unset support**

Two approaches, both needed:

**A. Diff-based destruction:**
When `diff` detects objects in state but not in spec, generate
`@destroy` commands. Requires confirmation (dangerous).

```
worldbuilder.py compile --allow-destroy <spec>

# WARNING: The following objects will be destroyed:
#   @destroy #1234  (old_room — removed from spec)
#   @destroy #1235  (old_exit — orphaned by room removal)
# Type 'yes' to confirm:
```

**B. Attribute removal:**
When a room exists in both spec and state but an attribute was
removed from the spec, emit `&ATTR here=` (set to empty) or
`@wipe here/ATTR`.

Track attribute names in the state file's content record:
```yaml
park_entrance:
  dbref: "#1234"
  attrs_set: [SMELL, ACONNECT]  # what we last set
```

Diff against current spec attrs → removed attrs get `&ATTR here=`.

### 3. Grammar-Component Integration

**Problem:** Grammars and components are separate. A forest component
should declare its descriptions inline using grammar rules, but the
grid generator picks from a flat list instead.

**Solution:** Allow components to embed grammar rules and use
`${grammar_rule}` in description templates.

```yaml
component:
  name: haunted_forest
  grammar:
    adjective: [twisted, rotting, skeletal, blackened]
    tree: [oak, willow, ash, yew]
    detail:
      - "A crow watches from a bare branch."
      - "The ground squelches underfoot."

  generate: grid
  grid:
    room_name: "Haunted Wood [${x},${y}]"
    description_template: |
      ${adjective.cap} ${tree.s} loom in the fog. ${detail}
```

The grid generator calls `Grammar.expand()` per cell with the
component's embedded rules + the cell seed. No separate grammar
file needed (though imports still work for shared vocabularies).

### 4. Attribute-Level Diffing

**Problem:** Content hash says "something changed" but not what.
Incremental compile re-sets all attributes, which works but is
noisy and doesn't show the human what actually changed.

**Solution:** Store full attribute snapshot in state (not just hash).

```yaml
park_entrance:
  dbref: "#1234"
  type: room
  name: Park Entrance
  desc_hash: "a1b2c3d4"
  attrs:
    SMELL: "Fresh flowers."
    ACONNECT: "@pemit %#=Welcome."
```

Diff becomes:
```
~ MODIFY room "Park Entrance" (#1234):
    ~ SMELL: "Fresh flowers." → "Roses and lavender."
    + ACONNECT: "@pemit %#=Welcome."  (new attribute)
    - SOUND: (removed)
```

Compile emits only changed attributes:
```
&SMELL #1234=Roses and lavender.
&ACONNECT #1234=@pemit %%#=Welcome.
&SOUND #1234=
```

### 5. Permission Modeling in DRC

**Problem:** DRC validates the spec but doesn't know if the builder
character has permission to execute the plan. `@chzone` requires
zone ownership. `@set` on certain flags requires WIZARD. Quota
limits are checked abstractly but not against the actual quota.

**Solution:** Add a builder profile to the spec or project:

```yaml
builder:
  character: BuildBot
  quota: 500
  powers: [builder]
  # NOT wizard — so can't @set INHERIT, can't @chzone arbitrary zones
```

DRC rules:
- Quota check: total objects vs builder quota
- Flag check: flags requiring WIZARD when builder isn't WIZARD
- Zone check: @chzone only to zones the builder controls
- Parent check: @parent only to objects the builder can examine

This doesn't require a live connection — it's static analysis
against the declared builder profile.

---

## Architectural Decisions

### Python Stays for Tooling

The Python investment is in the tool layer (offline validation,
compilation, diffing). This is correct. The tool doesn't run
inside the MUX — it generates commands that the MUX executes.
Same pattern as Terraform (Go) managing AWS (not Go).

### Function Dictionary Versioning

The softcode linter's function dictionary must track the MUX
codebase. When new functions are added to TinyMUX (like the 18
PennMUSH functions we added today), the linter dictionary needs
updating. This should be automated:

```
# Generate function list from MUX source
grep 'XFUNCTION\|static FUNCTION' mux/modules/engine/functions.cpp \
  | extract_names > tools/worldbuilder/mux_functions.txt
```

### State File as Source of Truth

The state file is the link between the spec (human intent) and
the game (live reality). v3's reconciliation and attribute-level
diffing both depend on a richer state file. The state format
should be versioned:

```yaml
state_version: 3
zone: Emerald Park
builder: BuildBot
last_applied: "2026-03-19T15:30:00Z"
objects:
  park_entrance:
    dbref: "#1234"
    type: room
    name: Park Entrance
    desc_hash: "a1b2c3d4"
    attrs:
      SMELL: "Fresh flowers."
    flags: [FLOATING]
```

---

## What We're NOT Doing in v3

- **GUI** — the tool is CLI-first. A web UI for spec editing could
  come later but isn't needed now.
- **Real-time sync** — WorldBuilder is batch-oriented (edit → plan →
  apply). Real-time two-way sync with a live game is a different
  tool with different tradeoffs.
- **MUX module integration** — embedding WorldBuilder inside the MUX
  as a C++ module would lose offline capability. Keep it external.
- **Language rewrite** — Python is correct for tooling. If parsing
  becomes a bottleneck (10K+ room specs), profile first, then
  consider Ragel for the hot path.

---

## Implementation Order

1. **State file v3 format** — richer state is prerequisite for 2-5
2. **Attribute-level diffing** — most immediately useful
3. **Destructive changes** — unblocks real production workflows
4. **Reconciliation** — needed when multiple builders are active
5. **Permission modeling** — safety for production deployment
6. **Grammar-component integration** — quality of life

Each item is independently valuable. No item depends on a later one.
