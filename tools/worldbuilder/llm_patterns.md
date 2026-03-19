# WorldBuilder LLM Integration Patterns

How to safely use language models to generate game content through
WorldBuilder's validation pipeline.

## Safety Model

```
Human intent → LLM generates YAML → DRC validates → Human reviews → Apply
```

The LLM never touches the game directly. All output goes through:
1. **YAML spec** — structured, parseable, diffable
2. **DRC validation** — catches errors before anything executes
3. **Softcode lint** — validates any code in attributes
4. **Human review** — plan output shows exactly what will happen
5. **Apply** — mechanical execution of reviewed plan

## Prompt Patterns

### 1. Zone Generation

**Prompt:**
```
Generate a WorldBuilder YAML spec for a medieval castle with these rooms:
- Courtyard (entrance)
- Great Hall
- Kitchen
- Dungeon (below courtyard)
- Tower (above great hall)
- Armory (off the great hall)

Use the WorldBuilder YAML format with zone, rooms, and exits sections.
Each room needs a name, a 3-4 sentence description, and appropriate
exits with aliases. Connect all rooms logically.
```

**Expected output:** A complete YAML spec that passes `worldbuilder.py check`.

### 2. Description Writing

**Prompt:**
```
Write room descriptions for a WorldBuilder spec. The zone is an
underwater coral reef. For each room below, write a 3-4 sentence
description rich in sensory detail (sight, sound, touch). Use
present tense, second person.

Rooms:
- reef_shallows: The Shallows
- coral_garden: Coral Garden
- deep_trench: The Trench
- sea_cave: Sea Cave
- kelp_forest: Kelp Forest

Output as YAML room entries with name and description fields.
```

### 3. Component Design

**Prompt:**
```
Design a WorldBuilder component for a general store. Use the
component YAML format with:

params: store_name, shopkeeper_name, specialty (what they mainly sell)
rooms: shop_floor (main room), storeroom (back room)
things: shopkeeper (NPC with INVENTORY attribute)
exits: between rooms, plus a port called 'entrance' for external connection
descriptions: 3-4 sentences each, use ${params} for customization
```

### 4. Grammar Authoring

**Prompt:**
```
Create a WorldBuilder grammar file (YAML) for generating descriptions
of a haunted mansion. Include these grammar rules:

- adjective: 8+ creepy adjectives
- furniture: 6+ old furniture items
- sound: 6+ eerie sounds
- smell: 4+ unpleasant smells
- detail: 6+ atmospheric details (can reference other rules)

Plus templates for: ballroom, bedroom, library, cellar

Use the format: grammar:, templates:, with ${rule} and ${rule.modifier}
syntax.
```

### 5. Spec Review

**Prompt:**
```
Review this WorldBuilder spec for quality and consistency. Check for:
- Descriptions that are too short or use placeholder text
- Missing sensory details (only sight, no sound/smell/touch)
- Inconsistent tone or style between rooms
- Exits that don't make spatial sense
- Missing logical connections

[paste spec here]
```

### 6. Bulk Description Enhancement

**Prompt:**
```
These room descriptions are stubs. Expand each to 3-4 sentences with
sensory detail, maintaining the same tone. Keep the room name and ID
unchanged. Output as YAML.

rooms:
  market:
    name: Market Square
    description: "A busy market."
  alley:
    name: Back Alley
    description: "A dark alley."
  docks:
    name: The Docks
    description: "Wooden docks."
```

## Validation Workflow

After getting LLM output, always run:

```bash
# 1. Save output
cat > new_zone.yaml

# 2. Validate structure + rules
worldbuilder.py check new_zone.yaml

# 3. Validate any softcode
worldbuilder.py lint new_zone.yaml

# 4. Review the plan
worldbuilder.py plan new_zone.yaml

# 5. Generate a map to visualize topology
mapgen.py new_zone.yaml

# 6. Only then consider applying
executor.py apply new_zone.yaml --host ... --dry-run
```

## What LLMs Are Good At

- **Descriptions** — vivid, varied, consistent tone
- **Grammar rules** — large vocabulary lists
- **Bulk expansion** — turning stubs into full descriptions
- **Layout suggestions** — "how should a castle be connected?"
- **NPC dialogue** — SPEECH attributes, DESC text
- **Consistency review** — spotting tone mismatches

## What LLMs Are Bad At

- **Exact MUX softcode** — gets function names wrong, misses escaping
- **Dbref references** — doesn't know your game's object numbers
- **Game-specific systems** — economy balance, combat formulas
- **Spatial consistency** — may connect rooms in impossible ways
- **Exit alias conventions** — varies by game culture

Always DRC-validate and human-review before applying.
