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
- Validate identity via `objid()` before reading — detect recycled dbrefs
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

**Important: `@destroy` is deferred, not immediate.** The command
marks the object with a GOING flag; the object is not actually
recycled until the next DB cleanup pass (`@dump` or `@dbclean`).
Between `@destroy` and recycling:
- The object still exists with a valid dbref and objid
- Exits, parents, and zone references to it remain intact
- A builder (or script) can `@undestroy` it, cancelling the
  destruction entirely
- WorldBuilder queries (`objid()`, `name()`, `get()`) will still
  return valid data, making it look "alive"

This means:
- **State tracking:** After `@destroy`, the state entry should be
  marked `status: pending_destroy`, not deleted. The entry can only
  be cleared after confirming the object is truly gone (objid
  returns `#-1` or the GOING flag is observed).
- **Rollback is not atomic:** `@destroy` during rollback does not
  guarantee the object is removed. If `@undestroy` is issued before
  the next dump, the rollback is silently reverted.
- **Ordering matters:** Exits must be destroyed before rooms, but
  since destruction is deferred, the exit's destination room still
  exists at the time the exit is marked GOING. This is fine for
  `@destroy` ordering, but verify must account for GOING objects.
- **Re-creation race:** If WorldBuilder removes a room from spec
  (triggering `@destroy`) and the spec is later edited to re-add
  a room with the same spec ID, WorldBuilder must check whether
  the old object is still pending destruction before creating a
  new one. If it's still alive (GOING but not recycled), it should
  `@undestroy` and update rather than create a duplicate.

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

## Conceptual Challenges: The "Squishy" Problem

WorldBuilder aims for "Terraform for MUX," but MUX is an organic, live system. Unlike cloud infrastructure, builders "live" in their creations and make real-time, iterative tweaks. This creates a fundamental friction between the **Declarative Spec** and the **Living Game**.

### 1. Identity Stability (The Dbref Recycling Problem)
Once created, a dbref does not change — but it can be destroyed, and a
new object can claim the same dbref number. The state file stores dbrefs,
so if `#1234` is destroyed and recycled, WorldBuilder would silently
modify the wrong object.

**Solution: `objid()` validation.**
TinyMUX's `objid()` returns `#dbref:creation_timestamp` (e.g.,
`#1234:1678901234`). The creation timestamp is hardcoded at creation
time and cannot be changed by anyone. If the dbref is recycled, the
objid will differ because the new object has a different creation time.

The state file stores both dbref and objid:
```yaml
park_entrance:
  dbref: "#1234"
  objid: "#1234:1678901234"
```

On reconnect (reconcile, verify, apply-update), WorldBuilder queries
`objid(#1234)` and compares against the stored value:
- **Match** → same object, proceed normally
- **Mismatch** → original was destroyed and replaced; treat as missing
- **#-1** → object doesn't exist at all; treat as missing

This is superior to a `&WB_ID` attribute approach because:
- No extra attribute cluttering the object
- Cannot be accidentally wiped by `@wipe` or overwritten by a builder
- Already exists on every object — no setup, no migration
- Read-only from hardcode — builders cannot tamper with it

### 2. Pivot: From "Push-Only" to "Bi-directional Sync"
We currently assume the Spec is the sole source of truth. However, for many builders, the Game is the primary editor. If we only "Apply," we risk overwriting organic improvements.

**Refinement:** Elevate `reconcile` to a first-class citizen. 
WorldBuilder should behave more like `git` than `terraform`. The workflow should support "Pulling" changes from the game back into the YAML spec. This protects builder creativity and makes WorldBuilder a collaborative partner rather than a mechanical enforcer.

### 3. Softcode Bundles
YAML is excellent for data (rooms, exits) but "squishy" for logic (complex functions). Hardcoding large MUX functions inside YAML attributes is brittle and loses editor features (linting, syntax highlighting).

**Refinement:** Support **File-Linked Attributes**.
Allow the spec to point to external `.mux` scripts:
```yaml
attrs:
  ACONNECT: file://scripts/welcome_handler.mux
```
This separates **World Data** (YAML) from **Game Logic** (Softcode), allowing the use of proper MUX engineering tools for the latter.

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
state_version: 4
zone: Emerald Park
builder: BuildBot
last_applied: "2026-03-19T15:30:00Z"
objects:
  park_entrance:
    dbref: "#1234"
    objid: "#1234:1678901234"
    type: room
    name: Park Entrance
    desc_hash: "a1b2c3d4"
    attrs:
      SMELL: "Fresh flowers."
    flags: [FLOATING]
```

---

## Known Defects (v2 Code Under v3 Scrutiny)

These are bugs and design gaps in the existing code that must be
resolved before any v3 feature (reconciliation, permission modeling)
can be built on top of them reliably.

### D1. Attribute Escaping — Data vs Code Ambiguity

**Problem:** The compiler emits `&ATTRNAME here={raw_value}` with
the value taken verbatim from YAML. If the attribute is data (a
smell description containing `100%`), the `%` is live and will be
evaluated as a substitution. If the attribute is softcode
(`@pemit %#=Welcome`), the `%#` *must* stay live.

`mux_escape()` exists and correctly escapes `%` → `%%`, `\n` → `%r`,
but it is only applied to room descriptions and exit descriptions.
Attribute values are never escaped. This means:
- Data attributes silently corrupt (`100%` → `1000`)
- There is no way to distinguish "this attribute is data, escape it"
  from "this attribute is code, leave it alone"

**Solution:** Introduce an explicit convention in the spec:

```yaml
attrs:
  SMELL: "Smells like 100% roses."     # data — auto-escape
  ACONNECT: !mux "@pemit %#=Welcome."  # code — literal, no escaping
```

The `!mux` YAML tag (or a simpler prefix convention) signals the
compiler to emit the value verbatim. All untagged string values get
`mux_escape()` applied. This also feeds into the softcode linter —
only `!mux`-tagged values need linting.

### D2. Things Not Tracked in State or Diff

**Problem:** The spec model supports `Thing` objects (items placed in
rooms). `compile_spec()` emits `@create` commands for them. But:
- `execute()` does not create Things
- `StateFile` has no `set_thing()` method
- `diff_spec()` ignores Things entirely
- `compile_incremental()` ignores Things entirely

Things are write-only: the full compiler generates them, but the
executor, differ, and state tracker don't know they exist. A second
`apply` will attempt to create duplicates.

**Solution:** Add `set_thing()` to `StateFile`. Extend `execute()`
with a Phase 3 that creates Things and captures their dbrefs and
objids. Extend `diff_spec()` and `compile_incremental()` to handle
Thing creation, modification, and deletion the same way rooms are
handled.

### D3. The `@teleport me=` / `here` Pattern

**Problem:** Both the compiler and executor teleport the builder
character to each room and use `here` as the command target. This
creates several failure modes:
- `@aenter`/`@oenter` triggers fire on the builder for every room
- If `@teleport` fails (HALT room, no permission), subsequent
  `@name here=`, `@desc here=` commands silently modify whatever
  room the builder is currently standing in
- Two concurrent WorldBuilder sessions on the same character will
  teleport each other around and corrupt both sessions
- The builder's location is observable by other players, causing
  spam and confusion

**Solution:** Use dbrefs directly in commands. The executor already
has the dbref for every existing object. Instead of:
```
@teleport me=#1234
@name here=New Name
@desc here=New description.
```
Emit:
```
@name #1234=New Name
@desc #1234=New description.
```

The `@teleport me=` + `here` pattern is only needed for `@dig`
(which creates a room and teleports you there). After creation,
all subsequent modifications should use the captured dbref. This
eliminates the teleport-storm, avoids trigger side effects, and
makes concurrent sessions safe.

Note: `@dig/teleport` is still needed for room *creation* because
we need `think %L` to capture the new dbref. But the attribute-
setting pass that follows should use the dbref, not `here`.

### D4. Exit Identity Is Lossy

**Problem:** Exits are keyed as `exit_{from_room}_{to_room}` in the
state file. This means:
- Two exits between the same room pair (e.g., a door and a window)
  collide on the same key — the second overwrites the first
- Exit alias changes are not detectable (the key doesn't encode
  the name)
- Exit property modifications (locks, descriptions, success/failure
  messages) are not tracked in state or handled by the differ
- `diff_spec()` can detect exit creation and deletion, but not
  modification

**Solution:** Key exits by spec-level ID (like rooms), not by the
from/to pair. The spec should assign each exit a unique identifier:

```yaml
exits:
  - id: main_door          # explicit, stable key
    from: lobby
    to: street
    name: "Door;out;o"
```

If no `id` is given, auto-generate from `{from}_{to}_{index}` to
handle multiple exits between the same pair. Store exit properties
in state (lock, desc, flags) and diff them like room attributes.

### D5. No Error Detection on Command Execution

**Problem:** `do_cmd()` sends a command, sleeps 0.3s, reads whatever
text came back, and logs it. There is no inspection of the response
for error conditions. A failed `@dig` (quota exceeded), a rejected
`@chzone` (not owner), or a `Permission denied` all pass silently.
The executor continues, setting attributes on the wrong room or
skipping objects without recording the failure.

**Solution:** Define an error detection function:

```python
MUX_ERRORS = [
    'Permission denied',
    'I don\'t see that here',
    'Huh?  (Type "help" for help.)',
    'That command is restricted',
    'You don\'t have enough quota',
]

def check_response(resp, context):
    for err in MUX_ERRORS:
        if err in resp:
            raise ExecutionError(f"{context}: {err}")
```

Call it after every `do_cmd()` that expects success. On error, log
the failure, skip the object, and mark it as failed in state. At
the end of execution, report all failures with their contexts.

Critical commands (`@dig`, `@destroy`) should abort the session on
failure. Property-setting commands (`@name`, `@desc`, `&ATTR`) can
continue with a warning.

The error checker should also detect the GOING flag on objects
before modifying them. An object marked GOING (pending destruction)
is still queryable but should not be treated as a live target.
Query `hasflag(#1234, GOING)` before update operations — if true,
either `@undestroy` it (if the spec still wants it) or skip it
(if the spec doesn't).

### D6. Zone Object Is Never Created

**Problem:** `compile_spec()` emits `@chzone here=%{zone:Name}` for
every room, but nothing creates the zone object itself. The plan
output displays `+ CREATE zone` but that is cosmetic — no command
is generated. If the zone object does not already exist on the game,
every `@chzone` silently fails and all rooms end up unzoned.

**Solution:** Add zone creation as Phase 0 of both the compiler and
executor:

```
@create Zone - Emerald Park
@set Zone - Emerald Park=ZONE
think [setq(0, num(Zone - Emerald Park))] Zone = %q0
```

The executor should capture the zone dbref/objid and store it in
state. All subsequent `@chzone` commands reference the zone by dbref.
DRC should also validate that the zone name does not collide with
existing objects (if a builder profile with connection info is
available).

### D7. Description Escaping Is Inconsistent

**Problem:** Two code paths generate description commands:
- `compile_spec()` line 574: `room.description.rstrip().replace('\n', '%r')`
  — does NOT escape `%` characters
- `mux_escape()`: escapes `%` → `%%`, then joins with `%r`
- `emit_exit_props()` calls `mux_escape()` for exit descriptions

Room descriptions in the compiler do not escape `%`. A room
description containing `100% chance of rain` will be evaluated
as `1000 chance of rain` on the game. Exit descriptions are
correctly escaped because they go through `mux_escape()`.

**Solution:** Route all description compilation through
`mux_escape()`. The inline `.replace('\n', '%r')` should be
deleted everywhere — `mux_escape()` already handles newlines.
This is also a prerequisite for D1 (attribute escaping) since the
fix is the same function.

### D8. Dry-Run Pollutes State

**Problem:** A dry-run apply writes `#DRY_roomid` entries into the
state file. If the user later runs a real `apply`, `execute()` finds
these entries via `state.get_dbref()`, treats the room as existing,
and tries `@teleport me=#DRY_park_entrance` — which fails. The
executor then falls into the update path instead of the create path,
and all commands for that room target the wrong location.

**Solution:** Dry-run should never write to the real state file.
Either:
- A. Use a separate in-memory state that is discarded after dry-run
  (simplest — just don't call `state.save()` in dry-run mode)
- B. Write to a `.dry-run.state.yaml` file for inspection, never
  read it back during real apply

The executor already has a `dry_run` flag. The fix is to skip
`state.save()` when `dry_run=True` and to avoid writing `#DRY_`
entries into `state.objects` at all.

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

### Phase A: Fix Foundations (defects that undermine everything above)

1. **D8 — Dry-run state isolation** — trivial fix, prevents data corruption
2. **D7 — Description escaping consistency** — route through `mux_escape()`
3. **D5 — Error detection on commands** — prerequisite for any reliable execution
4. **D3 — Use dbrefs instead of `here`** — prerequisite for concurrent use
5. **D1 — Attribute data/code distinction** — prerequisite for correct apply
6. **D6 — Zone creation** — prerequisite for complete first-time apply

### Phase B: Complete the Object Model

7. **D4 — Exit identity and state tracking** — exits need spec IDs and diffing
8. **D2 — Thing lifecycle** — create, track, diff, update, delete Things

### Phase C: v3 Features (built on solid foundations)

9. **State file v4 format** — richer state with objid tracking (**DONE**)
10. **Attribute-level diffing** — most immediately useful (**DONE**)
11. **Destructive changes** — unblocks real production workflows (**DONE** — basic)
12. **Reconciliation** — needed when multiple builders are active
13. **Permission modeling** — safety for production deployment
14. **Grammar-component integration** — quality of life

Phase A items are independent of each other and can be done in any
order. Phase B items depend on Phase A (especially D3 and D5).
Phase C features assume a reliable execution layer.
