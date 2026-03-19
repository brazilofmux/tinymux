# WorldBuilder — Game Content Management System

## Design Document

A three-layer system for building and maintaining MUX game worlds,
inspired by Terraform (infrastructure as code) and EDA schematic
capture (reusable components, design rule checking).

## The Problem

Building a MUX game today:
- Tedious: `@dig`, `@open`, `@desc`, `@set` one at a time, by hand
- Error-prone: typos in exit destinations, dangling exits, missing descs
- Not version-controlled: the flatfile is the only truth, and it's binary
- Not reusable: every apartment building is hand-built from scratch
- Not auditable: who changed what, when, why?
- Not LLM-assistable: no safe way to use AI for content generation

## The Three Layers

### Layer 1: Specification Language ("the schematic")

A declarative YAML-based language describing the game world. Human-readable,
diffable, version-controllable, LLM-assistable.

```yaml
# worldspec.yaml — a small park area

zone:
  name: Emerald Park
  description: A peaceful urban park in the downtown district.
  parent: $zone_master

rooms:
  park_entrance:
    name: Park Entrance
    description: |
      A wrought-iron gate opens onto a gravel path that winds between
      ancient oak trees. Benches line the walkway, and the sound of a
      fountain carries from deeper within the park.
    flags: [FLOATING]
    attrs:
      ACONNECT: "@pemit %#=You feel a cool breeze as you enter the park."

  fountain_plaza:
    name: Fountain Plaza
    description: |
      A marble fountain dominates the center of a circular plaza. Water
      arcs from the mouths of four stone fish, catching the light as it
      falls into the basin below.
    attrs:
      SMELL: "The mist from the fountain carries a fresh, clean scent."

  rose_garden:
    name: The Rose Garden
    description: |
      Rows of carefully tended rose bushes form a maze of color and
      fragrance. A small wooden sign reads 'Maintained by the Garden
      Society.'
    parent: $garden_parent

exits:
  - from: park_entrance
    to: fountain_plaza
    name: North Path;np;north;n
    back: South Path;sp;south;s

  - from: fountain_plaza
    to: rose_garden
    name: Garden;garden;east;e
    back: Plaza;plaza;west;w

  - from: park_entrance
    to: $downtown_square    # external reference
    name: Out;out;south;s
    # no back — managed by the downtown zone spec
```

### Layer 2: Compiler ("terraform plan" + DRC)

Deterministic software that:

1. **Parses** the spec into an internal representation
2. **Validates** against design rules (the DRC)
3. **Resolves** references ($variables, cross-zone links)
4. **Plans** the changeset (what to create, modify, delete)
5. **Outputs** a human-reviewable execution plan

#### Design Rule Checks (DRC)

| Rule | Category | Description |
|------|----------|-------------|
| Exit symmetry | Topology | Every `back:` creates a return exit |
| No dangling exits | Topology | Every exit destination exists or is a valid $ref |
| Reachability | Topology | All rooms reachable from at least one entry point |
| Description required | Content | Every room has a non-empty description |
| Description quality | Content | Min length, no placeholder text ("TBD", "TODO") |
| Name uniqueness | Naming | No duplicate room IDs within a zone |
| Attribute validation | Safety | No dangerous attrs (@destroy, @force, etc.) |
| Flag validation | Safety | Only allowed flags (no WIZARD, GOD, etc.) |
| Quota check | Resources | Total objects within specified quota |
| Zone membership | Structure | All objects belong to the declared zone |
| Parent validity | Structure | Referenced parents exist |
| Exit alias conflicts | Naming | No two exits from same room share an alias |

#### Plan Output

```
$ worldbuilder plan emerald_park.yaml

WorldBuilder Plan — Emerald Park
================================

Zone: Emerald Park
  + CREATE zone object "Emerald Park"

Rooms (3 new, 0 modified, 0 deleted):
  + CREATE room "Park Entrance" (park_entrance)
  + CREATE room "Fountain Plaza" (fountain_plaza)
  + CREATE room "The Rose Garden" (rose_garden)

Exits (5 new):
  + CREATE exit "North Path" from park_entrance → fountain_plaza
  + CREATE exit "South Path" from fountain_plaza → park_entrance
  + CREATE exit "Garden" from fountain_plaza → rose_garden
  + CREATE exit "Plaza" from rose_garden → fountain_plaza
  + CREATE exit "Out" from park_entrance → $downtown_square

Attributes (3):
  + SET park_entrance/ACONNECT
  + SET fountain_plaza/SMELL
  + SET park_entrance/FLOATING flag

DRC: 12 checks passed, 0 warnings, 0 errors.
Total: 9 objects (1 zone + 3 rooms + 5 exits)
```

### Layer 3: Executor ("terraform apply")

A bot that connects to the MUX and issues commands:

```
> @dig/teleport Park Entrance
> @desc here=A wrought-iron gate...
> @set here=FLOATING
> &ACONNECT here=@pemit %#=You feel a cool breeze...
> @open North Path;np;north;n=<dbref of fountain_plaza>
> ...
```

The executor:
- Connects as a builder character
- Executes commands in order
- Captures dbrefs as they're created
- Maps spec IDs to dbrefs (the "state file")
- Logs all commands and results
- Can do dry-run (just log, don't execute)
- Idempotent: re-running updates only what changed

## Reusable Components ("cells" / "modules")

### Component Definition

```yaml
# components/apartment_unit.yaml
component:
  name: apartment_unit
  params:
    unit_number: { type: string, required: true }
    owner: { type: dbref, default: null }
    monthly_rent: { type: int, default: 100 }
    furnished: { type: bool, default: false }

  rooms:
    living_room:
      name: "Apartment ${unit_number} - Living Room"
      description: |
        A modest studio apartment with hardwood floors and tall windows.
        ${if furnished}The apartment is furnished with a couch, coffee
        table, and bookshelf.${endif}
      attrs:
        RENT_AMOUNT: "${monthly_rent}"
        RENT_OWNER: "${owner}"
        APARTMENT_UNIT: "${unit_number}"
        LOCK_USE: "owner:${owner}"

    bathroom:
      name: "Apartment ${unit_number} - Bathroom"
      description: |
        A small but clean bathroom with a shower stall, toilet, and
        pedestal sink. A mirror hangs above the sink.

  exits:
    - from: living_room
      to: bathroom
      name: Bathroom;bath
      back: Living Room;out

  # External connection point — the caller wires this up
  ports:
    door:
      room: living_room
      direction: out
```

### Component Instantiation

```yaml
# zones/riverside_apartments.yaml
zone:
  name: Riverside Apartments
  description: A three-story apartment building overlooking the river.

imports:
  - components/apartment_unit.yaml

rooms:
  lobby:
    name: Riverside Apartments - Lobby
    description: |
      A clean, well-lit lobby with a reception desk and a row of
      mailboxes on the wall. An elevator and stairwell provide access
      to the upper floors.

  hallway_2f:
    name: Second Floor Hallway
    description: |
      A carpeted hallway with numbered doors on each side. Soft
      overhead lighting and framed prints on the walls.

instances:
  - component: apartment_unit
    id: apt_201
    params:
      unit_number: "201"
      monthly_rent: 150
      furnished: true
    connect:
      door:
        to: hallway_2f
        name: "Apartment 201;201"
        back: "Hallway;out;hall"

  - component: apartment_unit
    id: apt_202
    params:
      unit_number: "202"
      monthly_rent: 120
    connect:
      door:
        to: hallway_2f
        name: "Apartment 202;202"
        back: "Hallway;out;hall"

  - component: apartment_unit
    id: apt_203
    params:
      unit_number: "203"
      monthly_rent: 120
    connect:
      door:
        to: hallway_2f
        name: "Apartment 203;203"
        back: "Hallway;out;hall"

exits:
  - from: lobby
    to: hallway_2f
    name: Stairs;stairs;up;u;2f
    back: Lobby;lobby;down;d;1f
  - from: lobby
    to: $outside_street
    name: Out;out;south;s
```

### Procedural Generation

```yaml
# components/forest_area.yaml
component:
  name: forest_area
  params:
    width: { type: int, default: 3 }
    height: { type: int, default: 3 }
    density: { type: float, default: 0.7 }

  generate: grid
  grid:
    width: ${width}
    height: ${height}
    room_template:
      name: "Deep Forest [${x},${y}]"
      description:
        pool:
          - "Tall pines tower overhead, their branches forming a dense canopy."
          - "Birch trees with peeling white bark cluster around a mossy boulder."
          - "A dense thicket of brambles makes passage difficult here."
          - "Ferns carpet the forest floor beneath ancient oaks."
          - "Shafts of sunlight pierce the canopy, illuminating a small clearing."
        selection: seeded_random  # deterministic from position
    exits: cardinal  # auto-generate N/S/E/W between adjacent cells
    connectivity: ${density}  # percentage of possible exits that exist

  ports:
    edge_north: { room: "[*,0]", direction: north }
    edge_south: { room: "[*,${height-1}]", direction: south }
    edge_west:  { room: "[0,*]", direction: west }
    edge_east:  { room: "[${width-1},*]", direction: east }
```

## State Management

### State File (terraform.tfstate equivalent)

```yaml
# .worldbuilder/state/emerald_park.state.yaml
spec_version: "1.0"
zone: emerald_park
last_applied: "2026-03-19T15:30:00Z"
builder_character: BuildBot

objects:
  park_entrance:
    dbref: "#1234"
    type: room
    created: "2026-03-19T15:30:01Z"
  fountain_plaza:
    dbref: "#1235"
    type: room
    created: "2026-03-19T15:30:02Z"
  rose_garden:
    dbref: "#1236"
    type: room
    created: "2026-03-19T15:30:03Z"
  exit_north_path:
    dbref: "#1237"
    type: exit
    created: "2026-03-19T15:30:04Z"
  # ...
```

### Import ("terraform import")

Read existing game state back into a spec:

```
$ worldbuilder import --zone "Emerald Park" --host game.example.com --port 4201
```

Connects to the game, walks the zone, reads dbrefs/attrs/exits, and
generates a YAML spec + state file. This bootstraps WorldBuilder for
existing game areas that were built by hand.

## Operations

### Workflow

```
# 1. Write or edit the spec (human + LLM)
$ vim emerald_park.yaml

# 2. Validate and plan (deterministic, safe)
$ worldbuilder plan emerald_park.yaml
# Review the plan output...

# 3. Apply (bot executes against the game)
$ worldbuilder apply emerald_park.yaml \
    --host game.example.com --port 4201 \
    --character BuildBot --password hunter2

# 4. Check state matches spec
$ worldbuilder verify emerald_park.yaml \
    --host game.example.com --port 4201

# 5. Diff two specs
$ worldbuilder diff old_park.yaml new_park.yaml
```

### Diff and Incremental Apply

When a spec changes, the planner diffs against the state file:

```
$ worldbuilder plan emerald_park.yaml

WorldBuilder Plan — Emerald Park (incremental)
===============================================

Rooms (0 new, 1 modified, 0 deleted):
  ~ MODIFY room "Fountain Plaza" (#1235):
    ~ description: changed (diff available with --verbose)
    + SET SOUND attribute

Exits (1 new):
  + CREATE exit "Hidden Path" from fountain_plaza → rose_garden

DRC: 12 checks passed, 0 warnings, 0 errors.
```

## Implementation Language

The compiler/planner/executor should be written in Python:
- YAML parsing (PyYAML/ruamel.yaml)
- Telnet client for executor (asyncio + telnetlib3)
- Template expansion (Jinja2-like)
- Graph analysis for topology checks (networkx or custom)
- Rich CLI output (click + rich)
- Cross-platform (builders use Windows, Linux, Mac)

## LLM Integration Point

The LLM helps at Layer 1 ONLY:
- "Generate a 5x5 forest area with a river running through it"
- "Write descriptions for these 10 rooms in a gothic style"
- "Add a shop to the town square with these inventory items"
- "Review this spec for consistency and suggest improvements"

The LLM's output is YAML that goes through the compiler. If the YAML
has errors, the compiler catches them. If the descriptions are bad,
a human reviews. At no point does the LLM execute commands against
the game.

## Component Library (future)

Pre-built, tested components for common patterns:

| Component | Description |
|-----------|-------------|
| apartment_unit | Single rentable apartment |
| apartment_building | Multi-floor building with lobby |
| shop | Retail store with inventory |
| tavern | Bar with seating, barkeeper NPC |
| street_grid | MxN city block layout |
| forest_area | Procedural wilderness |
| cave_system | Branching cavern network |
| castle | Multi-room fortification |
| ship | Vessel with cabins, deck, hold |
| arena | PvP combat area with staging |

## Safety Guarantees

1. **No LLM executes commands** — spec → compiler → plan → human review → apply
2. **All changes are planned first** — nothing happens without a plan
3. **State tracking** — always know what exists and what changed
4. **Rollback** — apply in reverse order to undo (within limits)
5. **Quota enforcement** — never exceed builder's object quota
6. **Attribute whitelist** — only safe attributes allowed in specs
7. **Flag whitelist** — only safe flags (no WIZARD, IMMORTAL, etc.)
8. **Exit validation** — no exits to rooms the builder can't access
9. **Idempotent apply** — re-running is safe, only applies changes
10. **Full audit log** — every command sent, every response received
