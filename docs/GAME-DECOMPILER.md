# Game Decompiler: Semantic Extraction from MUX Databases

## The Problem

Every MUX game is an opaque ball of softcode.  The original builder
knows what it does.  When they leave, the knowledge leaves with them.
New staff inherit a database with thousands of objects, tens of
thousands of attributes, and no documentation.

`@decompile` gives you the raw code.  `examine` gives you one object
at a time.  Neither tells you what the game *is*.

There is no tool that takes a MUX database and produces a human-readable
description of what was built, why it's structured that way, and how the
pieces relate to each other.

## The Idea

A **game decompiler** that reads a MUX database (SQLite or flatfile) and
produces a semantic map — a structured document that describes the game
world in terms of intent, not implementation.

The output answers questions like:

- What are the zones and how are they connected?
- What does this lock mean in plain English?
- Which objects form a "system" (combat, economy, BBS, jobs)?
- What events fire on startup, on connect, on cron?
- What's abandoned (unreachable rooms, unused code)?
- What are the global commands and what do they do?
- Where are the permission boundaries?

## What It Is Not

- Not a linter (though it could report problems it finds).
- Not an authoring tool (though its output could seed one).
- Not a code formatter or pretty-printer.
- Not opinionated about what a game should be.

It's a **reader**, not a writer.  It takes what exists and explains it.

## Output Layers

The decompiler produces output at multiple levels of abstraction:

### Layer 0: Inventory

Raw facts extracted directly from the database.  No interpretation.

- Object count by type (rooms, things, players, exits).
- Attribute count, total softcode volume.
- Master room contents (global commands and functions).
- Objects with @startup, @aconnect, @adisconnect, @daily, @cron.
- Zone assignments.  Parent chains.
- Flag distribution (WIZARD objects, INHERIT objects, DARK objects).

This is mechanical extraction.  Any tool could do it.

### Layer 1: Topology

The spatial structure of the game world.

- Room graph: nodes are rooms, edges are exits.
- Connected components (separate areas with no links between them).
- Unreachable rooms (no exit path from the starting room).
- Zone boundaries (clusters of rooms under the same zone object).
- Exit properties: one-way vs bidirectional, locked vs open.
- Obvious chokepoints (rooms where all paths converge).
- Floating objects (things not in any room).

Output format: a navigable map with annotations.  Could be text,
Graphviz, or structured data.

### Layer 2: Lock Semantics

Translate lock expressions into plain English.

MUX locks are boolean expressions over player properties:

```
@lock exit = hasflag(%#, WIZARD)        → "Wizard-only"
@lock exit = cor(hasflag(%#,APPROVED),   → "Approved players or staff"
                 hasflag(%#,STAFF))
@lock exit = match(get(%#/FACTION),Red)  → "Red faction members"
@lock exit = #42                         → "Only player #42 (Bob)"
```

The decompiler doesn't need to understand every possible lock.  It needs
a library of common patterns and a fallback that shows the raw expression
when it can't translate.  Even partial coverage is valuable — the common
cases (flag checks, attribute matches, dbref checks) cover most real
locks.

### Layer 3: System Detection

Identify clusters of objects that form coherent game systems.

Heuristics:

- **Command objects**: Things in the master room with `$command:` patterns
  in their attributes.  Group by naming convention (e.g., all `+job*`
  commands belong to the jobs system).
- **Function objects**: Objects providing `@function` or user-defined
  functions via `u()` chains.
- **Data objects**: Objects that are only referenced by other objects
  (never directly by players).  These are "backend" objects — config
  stores, counters, lookup tables.
- **Cross-reference analysis**: If objects A, B, and C all reference
  each other's attributes, they're likely part of the same system.
  Build a reference graph and find clusters.
- **Known signatures**: Myrddin's BBS has known attribute patterns
  (HDR_*, BDY_*, MESS_LST, MASTER_LST).  Anomaly Jobs has known
  patterns.  A signature library can identify common packages.

Output: "This game has: a BBS (objects #21-#23, Myrddin v4.0.6), a
jobs system (objects #26-#35), a chargen system (objects #50-#62),
and 15 unclassified global command objects."

### Layer 4: Event Model

What happens automatically, without player input.

- `@startup` handlers: what fires when the game boots.
- `@aconnect` / `@adisconnect`: what happens on player connect/disconnect.
- `@daily` / `@cron`: scheduled tasks.
- `@listen` / `^pattern`: passive listeners.
- `@forwardlist` chains: message propagation.
- Queue-based loops: `@wait` patterns that create recurring tasks.

Output: an event timeline / trigger map.  "On startup, #21 initializes
the BBS timeout system.  On connect, #30 checks for unread mail and
new job notifications.  Every 24 hours, #21 runs message expiry."

### Layer 5: Narrative Summary

A natural-language description of the game, suitable for a new staff
member or an AI that needs to understand the world before generating
content.

This layer is where an LLM could add value — take the structured data
from layers 0-4 and produce prose:

"Farm is a small game with 45 rooms across 3 zones: Town (15 rooms),
Wilderness (20 rooms), and the Underground (10 rooms).  The master room
contains a BBS, a jobs system, and 8 custom commands.  The economy is
managed by a single object (#40) that tracks currency in a player
attribute called GOLD.  Access to the Underground requires the MINER
flag.  The game has 3 active players and was last modified 6 days ago."

## Implementation Approach

### Phase 1: Offline Database Reader

A standalone tool (Python or C++) that reads a MUX SQLite database
directly.  No live game connection needed.

- Parse the objects table and attributes table.
- Build the room graph from exit objects and their locations/links.
- Extract attribute values and classify objects by role.
- Produce Layer 0 and Layer 1 output.

This is the foundation.  It works on any database, including backups
and archives of dead games.

### Phase 2: Lock and Attribute Interpreter

Pattern-matching on lock expressions and attribute values.

- Parse lock strings into an AST (reuse the MUX parser or write a
  simpler one — locks are a subset of the full eval language).
- Match against known patterns (flag checks, attribute matches,
  dbref comparisons, boolean combinations).
- Produce Layer 2 output.

### Phase 3: System Detection

Cross-reference analysis and signature matching.

- Build a reference graph: for each attribute value, find dbrefs and
  attribute references to other objects.
- Cluster objects by mutual reference density.
- Match known system signatures (BBS, jobs, mail, chargen patterns).
- Produce Layer 3 and Layer 4 output.

### Phase 4: LLM Integration

Feed layers 0-4 to an LLM to produce Layer 5 narrative.

- Structured prompt with the extracted data.
- LLM produces prose summary.
- Human reviews and edits.

This is optional and separable.  Layers 0-4 are valuable without it.

## Relationship to WorldBuilder

WorldBuilder tried to be the authoring tool.  The decompiler is the
reverse — it reads what exists and produces understanding.

The two could connect:

1. Decompiler extracts a game's structure.
2. Output feeds into a WorldBuilder-compatible spec (or a new format).
3. Builder (human or AI) modifies the spec.
4. Modified spec compiles back to MUX commands.

But the decompiler is valuable standalone, even if the round-trip
authoring story never materializes.  Understanding what you have is
the prerequisite for everything else.

## Relationship to Content Generation

If an AI is going to generate content for a MUX game, it needs to
understand the existing game first.  The decompiler's output is the
context window for content generation:

- "Here's the game map.  Add a new zone connected to the town square."
- "Here's the combat system.  Add a new weapon type."
- "Here's the faction structure.  Create a quest for the Red faction."

Without the decompiler, the AI is writing blind.  With it, the AI has
the same understanding a human builder would have after a week of
exploring the game.

## Open Questions

- **Scope**: Should this handle TinyMUX only, or also PennMUSH/RhostMUSH
  databases?  The flatfile formats differ but the concepts are the same.
- **Output format**: Markdown?  YAML?  HTML with navigation?  Interactive?
- **Incremental updates**: If the game changes, can the decompiler update
  its output incrementally, or does it re-scan everything?
- **Scale**: A game with 50 objects is trivial.  A game with 50,000
  objects needs indexing, search, and filtering.
- **Privacy**: Player attributes may contain sensitive data.  The
  decompiler should have a mode that skips player objects or redacts
  attribute values.

## Status

Design document only.  No implementation yet.
