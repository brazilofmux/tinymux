# WorldBuilder—Unified Design

## Status

This document is the authoritative design for WorldBuilder.

It replaces the older phased notes in
[design-worldbuilder-v2.md](design-worldbuilder-v2.md) and
[design-worldbuilder-v3.md](design-worldbuilder-v3.md). Those documents
remain only as historical pointers.

## Goal

WorldBuilder is a content engineering toolchain for TinyMUX worlds.
Its purpose is to make world content:

- declarative
- diffable
- reviewable
- importable from a live game or backup
- safe to apply incrementally
- resilient to hand-edits in the live world

The original inspiration was "Terraform for MUX." That remains useful
for the planning and state-comparison layer, but it is not the full
model. TinyMUX is not a clean RPC API. The live boundary is a telnet
conversation with server-specific output, latency, permissions, and
organic builder edits.

The architecture therefore narrows to three durable pieces:

1. Offline compiler/planner
2. Live adapter
3. Reconciler

Everything else is subordinate to those three pieces.

## Non-Goals

WorldBuilder is not trying to:

- replace interactive building entirely
- force YAML to be the only editing surface
- pretend telnet output is a stable machine API
- model every MUX subsystem before the core import/plan/reconcile/apply
  loop is reliable

## Core Model

WorldBuilder manages three views of the world:

- `spec`: the desired state, authored in YAML and related source files
- `state`: the last known managed state recorded by WorldBuilder
- `live`: the current state observed from the running game or from an
  imported backup

The essential operation is not a one-way push. It is comparison across
those three views.

### Sources of Truth

No single source is sufficient on its own:

- The spec captures intent.
- The state file preserves identity and the last applied snapshot.
- The live game reflects reality, including hand-edits and drift.

This leads to one central rule:

`apply` must never blindly assume that `state` still matches `live`.

## Architecture

### 1. Offline Compiler / Planner

The offline layer is deterministic. It does not talk to a running MUX.
Its job is to parse source files, validate them, normalize them into an
internal model, and plan changes.

#### Responsibilities

- Parse YAML specs, components, grammars, project files, and migrations.
- Expand reusable components and generated topology.
- Resolve internal references and declared external references.
- Run design-rule checks.
- Compute diffs between `spec` and `state`.
- Produce a human-reviewable plan.
- Compile intent into an execution program, without embedding telnet
  session behavior into the compiler itself.

#### Inputs

- zone spec files
- multi-zone project files
- component definitions
- optional grammar files
- optional softcode source files referenced by spec
- state files written by prior imports/applies/reconciliations

#### Outputs

- DRC results
- plan output
- diff output
- an execution program made of semantic operations such as:
  - create room
  - update description
  - set attribute
  - remove attribute
  - create exit
  - mark object for destroy

The preferred abstraction is an operation graph or ordered operation
list. Raw MUX command strings are a backend concern.

#### Required Properties

- deterministic for a given input set
- testable without a running game
- explicit about creates, updates, removals, and destructive actions
- able to diff at attribute level, not only at coarse object-hash level

### 2. Live Adapter

The live adapter is the ugly boundary layer. It knows how to talk to a
real TinyMUX server or an import/export format. It is allowed to be
messy because its job is to absorb the mess so the compiler and
reconciler stay clean.

#### Responsibilities

- Connect and authenticate to a live game.
- Send commands and read responses robustly.
- Query object identity and content from the server.
- Import live content into normalized records.
- Execute compiled operations against the server.
- Verify post-apply reality.
- Support offline import/export paths where available.

#### Backends

The live adapter may have multiple backends:

- telnet executor/query backend
- live import backend using `@decomp/tf` where possible
- flatfile import backend via `reformat`
- flatfile export backend via `unformat` or another stable offline
  format if that proves safer

The telnet backend is not treated as a clean CLI. It is a conversational
transport that must be normalized.

#### Identity

Stable identity is based on `objid()`, not dbref alone.

State records must store at least:

```yaml
objects:
  park_entrance:
    dbref: "#1234"
    objid: "#1234:1678901234"
```

Rules:

- dbref match and objid match: same object
- dbref match and objid mismatch: recycled object, treat as not ours
- objid missing because of legacy state: warn and operate cautiously

#### Observed Snapshot Shape

When the live adapter reads an object, it should normalize the result
into a structure rich enough for reconciliation:

```yaml
park_entrance:
  dbref: "#1234"
  objid: "#1234:1678901234"
  type: room
  name: Park Entrance
  description: |
    A wrought-iron gate opens onto a gravel path.
  flags: [FLOATING]
  attrs:
    ACONNECT: "@pemit %#=Welcome."
```

This is intentionally closer to the logical model than to raw telnet
lines.

#### Execution Semantics

Execution is an adapter concern, not a compiler concern. The adapter is
responsible for:

- object creation sequencing
- temporary dbref capture
- retry or fallback query strategies
- translating semantic operations into concrete MUX commands
- recording what actually happened

The adapter must preserve logs, because a conversational interface is
not self-describing after the fact.

### 3. Reconciler

The reconciler is the center of the workflow.

Its job is to compare `spec`, `state`, and `live`, then classify each
managed object and field.

#### Why This Is Central

TinyMUX worlds are routinely hand-edited in-game. Builders live in the
world and make small iterative changes. If WorldBuilder only supports
"apply desired state," it will overwrite valid work and lose trust.

The right model is therefore closer to `git merge` than to pure
Terraform.

#### Required Classifications

For each managed object or field:

- `unchanged`: spec, state, and live all agree
- `spec_modified`: spec differs from state, live matches state
- `live_modified`: live differs from state, spec matches state
- `conflict`: spec and live both differ from state in different ways
- `missing_live`: object recorded in state no longer exists live
- `recycled_live`: dbref exists but objid does not match
- `pending_destroy`: object was marked for destruction but may still
  exist live

#### Required Actions

The reconciler must support:

- refresh state from live when only live changed
- generate apply operations when only spec changed
- stop or require an explicit choice on conflicts
- record reconciliation decisions
- export a human-readable report

The minimum user-facing command set should eventually include:

- `import`
- `plan`
- `diff`
- `reconcile`
- `apply`
- `verify`

`reconcile` is not optional polish. It is the control point that keeps
the other commands safe.

## Desired Workflow

### Primary Workflow

1. Author or edit spec files offline.
2. Run `check` and `lint`.
3. Run `plan` or `diff`.
4. Query live state and run `reconcile`.
5. Review the reconciliation report.
6. Apply only the approved semantic operations.
7. Verify live results.
8. Update state from what actually happened.

### Import-First Workflow

For existing worlds or hand-built areas:

1. Import from live telnet queries or from a backup.
2. Generate a normalized spec plus state.
3. Clean up and modularize the spec offline.
4. Use reconcile/diff/apply for subsequent changes.

### Offline Round-Trip Workflow

Where TinyMUX tooling allows it:

1. `flatfile -> reformat -> import`
2. edit spec offline
3. `compile -> offline export format -> unformat`

This is valuable, but still secondary to the core three-piece
architecture.

## Data Model

### Spec

The spec should remain declarative and readable. It may describe:

- zone metadata
- rooms
- exits
- things
- flags
- attributes
- parents
- components and instances
- generated topology
- project-level cross-zone references

YAML remains appropriate for world data.

### Softcode

Large or logic-heavy softcode should not be forced inline when that
hurts tooling. File-linked attributes are preferred for substantial
logic:

```yaml
attrs:
  ACONNECT: file://scripts/welcome_handler.mux
```

This keeps world data declarative while allowing proper editing and
linting of softcode.

### State

The state file is the bridge between desired intent and live identity.
It is not merely a dbref map.

State must be versioned and store enough detail for field-level diffing
and reconciliation.

Minimum state content:

```yaml
state_version: 1
zone: Emerald Park
last_synced: 2026-03-19T00:00:00Z
objects:
  park_entrance:
    dbref: "#1234"
    objid: "#1234:1678901234"
    type: room
    name: Park Entrance
    description: "..."
    flags: [FLOATING]
    attrs:
      ACONNECT: "@pemit %#=Welcome."
    status: active
```

Useful object statuses include:

- `active`
- `pending_destroy`
- `missing`
- `conflict`

## Planning and Diffing

Planning must distinguish clearly between:

- create
- modify
- remove field
- destroy object

Human review output should be specific. "Something changed" is not
enough.

Example:

```text
~ MODIFY room "Park Entrance" (#1234)
    ~ description
    + attr:ACONNECT
    - attr:SOUND
```

This plan should be produced from normalized logical state, not from raw
telnet output.

## Destructive Changes

Destructive actions are real requirements, but TinyMUX destruction is
deferred and non-atomic.

Rules:

- destroy exits before rooms
- require explicit opt-in for destructive apply
- do not immediately delete destroyed objects from state
- mark them `pending_destroy`
- verify later whether they are actually gone or were undestroyed
- if a spec re-adds an object while the old one is still pending
  destruction, prefer recovery or reuse over creating duplicates

Attribute removal is also destructive in a smaller sense and must be
represented explicitly.

## Validation

The DRC layer remains important, but it should focus on stable checks:

- dangling references
- bidirectional exit integrity where declared
- alias conflicts
- description presence and quality
- flag safety
- attribute safety
- quota expectations
- parent and zone validity
- builder permission modeling where statically knowable

Softcode linting is part of validation, but static lint is separate from
the live adapter.

## Implementation Guidance

The current codebase may continue using Python for the tool layer. The
language is not the architectural issue. The important boundary is
between:

- pure planning logic
- live transport/query logic
- reconciliation logic

The code should move toward these separations:

### Compiler / Planner Module

- owns parsing, normalization, validation, diffing, and semantic
  operation generation
- contains no telnet sleeps, prompt parsing, or output scraping logic

### Live Adapter Module

- owns transport, query, import, apply, verify, and logging
- exposes normalized object reads and operation execution results

### Reconciler Module

- owns three-way comparison and conflict classification
- produces reports and approved operation sets

## Command Surface

The long-term command surface should map directly to the architecture:

- `check`
- `lint`
- `import`
- `plan`
- `diff`
- `reconcile`
- `apply`
- `verify`

Supporting commands such as `map`, `migrate`, and project helpers are
useful, but they should build on the same normalized model rather than
create parallel behavior.

## What To Do Next In Code

The next implementation pass should focus on compliance with this design
instead of feature expansion.

Priority order:

1. Separate semantic operation planning from raw MUX command emission.
2. Extract a cleaner live adapter interface from the current executor
   and importer.
3. Implement first-class reconciliation on top of `spec`, `state`, and
   normalized `live` snapshots.
4. Make destructive state transitions explicit with `pending_destroy`.
5. Ensure diff/apply/verify all operate on the same normalized object
   model.

If a feature does not strengthen one of those steps, it is not the
current priority.
