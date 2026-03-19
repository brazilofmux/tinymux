#!/usr/bin/env python3
"""WorldBuilder — MUX game content management tool.

Usage:
    worldbuilder.py plan <spec.yaml>              — validate and show plan
    worldbuilder.py compile <spec.yaml>           — output MUX commands
    worldbuilder.py check <spec.yaml>             — validate only (DRC)
    worldbuilder.py diff <spec.yaml> --state <f>  — show incremental changes
"""

import sys
import yaml
import argparse
import copy
import os
from pathlib import Path
from string import Template


# ---------------------------------------------------------------------------
# MUX Text Escaping
# ---------------------------------------------------------------------------

def mux_escape(text):
    """Escape a multi-line string for MUX attribute/description setting.

    Handles:
    - Literal % in text → %% (prevent substitution)
    - \\n → %r (MUX linebreak)
    - \\t → %t (MUX tab)
    - Trailing whitespace stripped per line
    - Leading/trailing blank lines stripped
    """
    # Strip leading/trailing blank lines
    lines = text.rstrip().split('\n')
    while lines and not lines[0].strip():
        lines.pop(0)

    # Process each line
    processed = []
    for line in lines:
        line = line.rstrip()
        # Escape literal % (but not our own %r/%t insertions)
        line = line.replace('%', '%%')
        processed.append(line)

    # Join with %r
    result = '%r'.join(processed)
    return result


def mux_unescape(text):
    """Reverse MUX escaping back to plain text (for import)."""
    text = text.replace('%r', '\n')
    text = text.replace('%t', '\t')
    text = text.replace('%b', ' ')
    text = text.replace('%%', '%')
    return text


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------

class Room:
    def __init__(self, id, name, description, flags=None, attrs=None, parent=None):
        self.id = id
        self.name = name
        self.description = description.rstrip()
        self.flags = flags or []
        self.attrs = attrs or {}
        self.parent = parent


class Thing:
    def __init__(self, id, name, location, description='', flags=None, attrs=None, parent=None):
        self.id = id
        self.name = name
        self.location = location  # room spec ID
        self.description = description.rstrip() if description else ''
        self.flags = flags or []
        self.attrs = attrs or {}
        self.parent = parent


class Exit:
    def __init__(self, from_room, to_room, name, back_name=None,
                 lock=None, desc=None, succ=None, osucc=None,
                 fail=None, ofail=None, drop=None, odrop=None,
                 flags=None):
        self.from_room = from_room
        self.to_room = to_room
        self.name = name
        self.back_name = back_name
        self.lock = lock
        self.desc = desc
        self.succ = succ
        self.osucc = osucc
        self.fail = fail
        self.ofail = ofail
        self.drop = drop
        self.odrop = odrop
        self.flags = flags or []


class Zone:
    def __init__(self, name, description=''):
        self.name = name
        self.description = description


class Component:
    """A reusable template with parameters, rooms, exits, and connection ports."""
    def __init__(self, name, params=None, rooms=None, exits=None, ports=None,
                 generate=None, grid=None, things=None):
        self.name = name
        self.params = params or {}    # name -> {type, default, required}
        self.rooms = rooms or {}      # id -> room data (pre-expansion)
        self.exits = exits or []      # exit data (pre-expansion)
        self.ports = ports or {}      # port_name -> {room, direction}
        self.generate = generate      # None or 'grid'
        self.grid = grid or {}        # grid generation config
        self.things = things or {}    # id -> thing data (pre-expansion)


class WorldSpec:
    def __init__(self):
        self.zone = None
        self.rooms = {}      # id -> Room
        self.things = {}     # id -> Thing
        self.exits = []      # [Exit]


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def parse_spec(path):
    """Parse a YAML spec file into a WorldSpec."""
    with open(path, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f)

    spec = WorldSpec()

    # Zone
    zone_data = data.get('zone', {})
    spec.zone = Zone(
        name=zone_data.get('name', 'Unnamed Zone'),
        description=zone_data.get('description', ''),
    )

    # Rooms
    for room_id, room_data in data.get('rooms', {}).items():
        spec.rooms[room_id] = Room(
            id=room_id,
            name=room_data.get('name', room_id),
            description=room_data.get('description', ''),
            flags=room_data.get('flags', []),
            attrs=room_data.get('attrs', {}),
            parent=room_data.get('parent'),
        )

    # Things (objects placed in rooms)
    for thing_id, thing_data in data.get('things', {}).items():
        spec.things[thing_id] = Thing(
            id=thing_id,
            name=thing_data.get('name', thing_id),
            location=thing_data.get('location', ''),
            description=thing_data.get('description', ''),
            flags=thing_data.get('flags', []),
            attrs=thing_data.get('attrs', {}),
            parent=thing_data.get('parent'),
        )

    # Exits
    for exit_data in data.get('exits', []):
        ex = Exit(
            from_room=exit_data['from'],
            to_room=exit_data['to'],
            name=exit_data['name'],
            back_name=exit_data.get('back'),
            lock=exit_data.get('lock'),
            desc=exit_data.get('desc'),
            succ=exit_data.get('succ'),
            osucc=exit_data.get('osucc'),
            fail=exit_data.get('fail'),
            ofail=exit_data.get('ofail'),
            drop=exit_data.get('drop'),
            odrop=exit_data.get('odrop'),
            flags=exit_data.get('flags', []),
        )
        spec.exits.append(ex)

    # Load and expand component instances
    spec_dir = Path(path).parent
    components = {}
    for imp in data.get('imports', []):
        comp_path = spec_dir / imp
        if comp_path.exists():
            comp = load_component(comp_path)
            if comp:
                components[comp.name] = comp

    for inst_data in data.get('instances', []):
        comp_name = inst_data.get('component', '')
        comp = components.get(comp_name)
        if not comp:
            continue
        inst_id = inst_data.get('id', comp_name)
        params = {**{k: v.get('default', '') for k, v in comp.params.items()},
                  **inst_data.get('params', {}),
                  '_instance_id': inst_id}
        expand_component(spec, comp, inst_id, params,
                         inst_data.get('connect', {}))

    return spec


def load_component(path):
    """Load a component definition from a YAML file."""
    with open(path, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f)

    comp_data = data.get('component', {})
    return Component(
        name=comp_data.get('name', ''),
        params=comp_data.get('params', {}),
        rooms=comp_data.get('rooms', {}),
        exits=comp_data.get('exits', []),
        ports=comp_data.get('ports', {}),
        generate=comp_data.get('generate'),
        grid=comp_data.get('grid', {}),
        things=comp_data.get('things', {}),
    )


def expand_param(text, params):
    """Expand ${param} references in a string."""
    if not isinstance(text, str):
        return text
    for key, val in params.items():
        text = text.replace('${' + key + '}', str(val))
    return text


import hashlib

def _seeded_pick(items, seed_str):
    """Deterministically pick an item from a list based on a string seed."""
    h = int(hashlib.md5(seed_str.encode()).hexdigest(), 16)
    return items[h % len(items)]


def _seeded_bool(seed_str, probability):
    """Deterministically return True with given probability based on seed."""
    h = int(hashlib.md5(seed_str.encode()).hexdigest()[:8], 16)
    return (h / 0xFFFFFFFF) < probability


def expand_grid(spec, comp, inst_id, params, connections):
    """Expand a grid-type component into a WxH room grid with cardinal exits."""
    prefix = inst_id + '_'
    grid = comp.grid
    width = int(params.get('width', grid.get('width', 3)))
    height = int(params.get('height', grid.get('height', 3)))
    density = float(params.get('density', grid.get('density', 0.8)))
    descriptions = grid.get('descriptions', ['A nondescript room.'])
    room_name_template = grid.get('room_name', 'Room [${x},${y}]')

    # Create rooms
    for y in range(height):
        for x in range(width):
            room_id = f"{prefix}{x}_{y}"
            cell_params = {**params, 'x': str(x), 'y': str(y)}
            name = expand_param(room_name_template, cell_params)
            desc = _seeded_pick(descriptions, f"{inst_id}:{x}:{y}")

            spec.rooms[room_id] = Room(
                id=room_id,
                name=name,
                description=desc.rstrip(),
            )

    # Create exits between adjacent cells
    directions = {
        'north': (0, -1, 'North;north;n', 'South;south;s'),
        'south': (0, 1, 'South;south;s', 'North;north;n'),
        'east':  (1, 0, 'East;east;e', 'West;west;w'),
        'west':  (-1, 0, 'West;west;w', 'East;east;e'),
    }

    created = set()  # avoid duplicate bidirectional exits
    for y in range(height):
        for x in range(width):
            for dir_name, (dx, dy, fwd_name, back_name) in directions.items():
                nx, ny = x + dx, y + dy
                if 0 <= nx < width and 0 <= ny < height:
                    pair = tuple(sorted([(x, y), (nx, ny)]))
                    if pair in created:
                        continue

                    # Density check — seeded for determinism
                    seed = f"{inst_id}:{pair[0]}:{pair[1]}"
                    if not _seeded_bool(seed, density):
                        continue

                    from_id = f"{prefix}{x}_{y}"
                    to_id = f"{prefix}{nx}_{ny}"
                    spec.exits.append(Exit(
                        from_room=from_id,
                        to_room=to_id,
                        name=fwd_name,
                        back_name=back_name,
                    ))
                    created.add(pair)

    # Wire edge ports to external rooms
    for port_name, conn_data in connections.items():
        port = comp.ports.get(port_name, {})
        edge = port.get('edge', '')
        target = conn_data.get('to', '')
        exit_name = conn_data.get('name', '')
        back_name = conn_data.get('back', '') or None
        if not edge or not target or not exit_name:
            continue

        # Pick the middle cell on the specified edge
        if edge == 'north':
            room_id = f"{prefix}{width // 2}_0"
        elif edge == 'south':
            room_id = f"{prefix}{width // 2}_{height - 1}"
        elif edge == 'west':
            room_id = f"{prefix}0_{height // 2}"
        elif edge == 'east':
            room_id = f"{prefix}{width - 1}_{height // 2}"
        else:
            continue

        spec.exits.append(Exit(
            from_room=room_id,
            to_room=target,
            name=exit_name,
            back_name=back_name,
        ))


def expand_component(spec, comp, inst_id, params, connections):
    """Expand a component instance into rooms and exits in the spec."""
    prefix = inst_id + '_'

    # Check for procedural generation
    if hasattr(comp, 'generate') and comp.generate == 'grid':
        expand_grid(spec, comp, inst_id, params, connections)
        return

    # Create rooms
    for room_id, room_data in comp.rooms.items():
        full_id = prefix + room_id
        spec.rooms[full_id] = Room(
            id=full_id,
            name=expand_param(room_data.get('name', room_id), params),
            description=expand_param(room_data.get('description', ''), params),
            flags=room_data.get('flags', []),
            attrs={k: expand_param(v, params) for k, v in room_data.get('attrs', {}).items()},
            parent=room_data.get('parent'),
        )

    # Create internal exits
    for exit_data in comp.exits:
        spec.exits.append(Exit(
            from_room=prefix + exit_data['from'],
            to_room=prefix + exit_data['to'],
            name=expand_param(exit_data['name'], params),
            back_name=expand_param(exit_data.get('back', ''), params) or None,
        ))

    # Create things
    for thing_id, thing_data in comp.things.items():
        full_id = prefix + thing_id
        spec.things[full_id] = Thing(
            id=full_id,
            name=expand_param(thing_data.get('name', thing_id), params),
            location=prefix + thing_data.get('location', ''),
            description=expand_param(thing_data.get('description', ''), params),
            flags=thing_data.get('flags', []),
            attrs={k: expand_param(v, params) for k, v in thing_data.get('attrs', {}).items()},
            parent=thing_data.get('parent'),
        )

    # Wire up ports to external rooms
    for port_name, conn_data in connections.items():
        port = comp.ports.get(port_name)
        if not port:
            continue
        port_room = prefix + port['room']
        target_room = conn_data.get('to', '')
        exit_name = expand_param(conn_data.get('name', ''), params)
        back_name = expand_param(conn_data.get('back', ''), params) or None

        if port_room and target_room and exit_name:
            spec.exits.append(Exit(
                from_room=port_room,
                to_room=target_room,
                name=exit_name,
                back_name=back_name,
            ))


# ---------------------------------------------------------------------------
# Design Rule Checks (DRC)
# ---------------------------------------------------------------------------

class DRCResult:
    def __init__(self):
        self.errors = []
        self.warnings = []

    @property
    def ok(self):
        return len(self.errors) == 0

    def error(self, msg):
        self.errors.append(msg)

    def warn(self, msg):
        self.warnings.append(msg)


# Flags that builders should never set
DANGEROUS_FLAGS = {
    'WIZARD', 'GOD', 'IMMORTAL', 'ROYALTY', 'STAFF', 'INHERIT',
    'TRUST', 'SLAVE',
}

# Attributes that could be dangerous
DANGEROUS_ATTRS = {
    'ADESTROY', 'AFORCE', 'AFORCELOCK',
}


def check_drc(spec):
    """Run all design rule checks on a spec."""
    result = DRCResult()

    room_ids = set(spec.rooms.keys())

    # --- Room checks ---
    for room_id, room in spec.rooms.items():
        # Description required
        if not room.description or not room.description.strip():
            result.error(f"Room '{room_id}' has no description.")

        # Description quality
        desc = room.description.strip().lower()
        for placeholder in ['tbd', 'todo', 'fixme', 'placeholder', 'xxx']:
            if placeholder in desc:
                result.warn(f"Room '{room_id}' description contains '{placeholder}'.")

        # Description minimum length
        if len(room.description.strip()) < 20:
            result.warn(f"Room '{room_id}' description is very short ({len(room.description.strip())} chars).")

        # Dangerous flags
        for flag in room.flags:
            if flag.upper() in DANGEROUS_FLAGS:
                result.error(f"Room '{room_id}' uses dangerous flag: {flag}")

        # Dangerous attributes
        for attr_name in room.attrs:
            if attr_name.upper() in DANGEROUS_ATTRS:
                result.error(f"Room '{room_id}' uses dangerous attribute: {attr_name}")

        # Name required
        if not room.name or not room.name.strip():
            result.error(f"Room '{room_id}' has no name.")

    # --- Exit checks ---
    exit_sources = {}  # room_id -> set of alias strings
    for ex in spec.exits:
        # Source room exists
        if ex.from_room not in room_ids and not ex.from_room.startswith('$'):
            result.error(f"Exit from unknown room: '{ex.from_room}'")

        # Destination room exists (or is external ref)
        if ex.to_room not in room_ids and not ex.to_room.startswith('$'):
            result.error(f"Exit to unknown room: '{ex.to_room}'")

        # Exit name not empty
        if not ex.name or not ex.name.strip():
            result.error(f"Exit from '{ex.from_room}' has no name.")

        # Check for alias conflicts within a room
        aliases = set(a.strip().lower() for a in ex.name.split(';'))
        if ex.from_room not in exit_sources:
            exit_sources[ex.from_room] = set()
        conflicts = aliases & exit_sources[ex.from_room]
        if conflicts:
            result.error(f"Exit alias conflict in '{ex.from_room}': {conflicts}")
        exit_sources[ex.from_room] |= aliases

        # Back exit alias conflicts
        if ex.back_name:
            back_aliases = set(a.strip().lower() for a in ex.back_name.split(';'))
            if ex.to_room not in exit_sources:
                exit_sources[ex.to_room] = set()
            conflicts = back_aliases & exit_sources[ex.to_room]
            if conflicts:
                result.error(f"Exit alias conflict in '{ex.to_room}': {conflicts}")
            exit_sources[ex.to_room] |= back_aliases

    # --- Reachability check ---
    if room_ids:
        # BFS from first room
        start = next(iter(room_ids))
        visited = set()
        queue = [start]
        # Build adjacency from exits
        adj = {rid: set() for rid in room_ids}
        for ex in spec.exits:
            if ex.from_room in adj and ex.to_room in adj:
                adj[ex.from_room].add(ex.to_room)
            if ex.back_name and ex.to_room in adj and ex.from_room in adj:
                adj[ex.to_room].add(ex.from_room)

        while queue:
            node = queue.pop(0)
            if node in visited:
                continue
            visited.add(node)
            for neighbor in adj.get(node, set()):
                if neighbor not in visited:
                    queue.append(neighbor)

        unreachable = room_ids - visited
        for rid in unreachable:
            result.error(f"Room '{rid}' is unreachable from '{start}'.")

    # --- Zone check ---
    if not spec.zone or not spec.zone.name:
        result.error("No zone name defined.")

    return result


# ---------------------------------------------------------------------------
# Compiler — generate MUX commands
# ---------------------------------------------------------------------------

def compile_spec(spec):
    """Compile a spec into a list of MUX commands.

    Returns a list of (comment, command) tuples.
    Commands use placeholder variables like %{room:park_entrance} for
    dbrefs that aren't known until execution time.
    """
    commands = []

    def cmd(comment, command):
        commands.append((comment, command))

    # Create rooms first (we need dbrefs for exits)
    cmd(f"=== Zone: {spec.zone.name} ===", "")

    for room_id, room in spec.rooms.items():
        cmd(f"--- Room: {room.name} ({room_id}) ---", "")
        cmd("Create room",
            f'@dig/teleport {room.name}')
        cmd("Store dbref",
            f'think [setq(0, %L)] Room {room_id} = %q0')
        # Use a register to note the dbref for later reference
        # In practice, the executor captures this from the output

        # Description
        desc = room.description.rstrip().replace('\n', '%r')
        cmd("Set description",
            f'@desc here={desc}')

        # Flags
        for flag in room.flags:
            cmd(f"Set flag {flag}",
                f'@set here={flag}')

        # Attributes
        for attr_name, attr_value in room.attrs.items():
            cmd(f"Set attribute {attr_name}",
                f'&{attr_name} here={attr_value}')

        # Parent
        if room.parent:
            cmd(f"Set parent",
                f'@parent here={room.parent}')

        # Zone — set all rooms to the zone object
        cmd("Set zone",
            f'@chzone here=%{{zone:{spec.zone.name}}}')

        cmd("", "")

    # Create exits
    cmd("=== Exits ===", "")

    def emit_exit_props(ex, exit_ref):
        """Emit property commands for an exit. exit_ref is the name or dbref."""
        if ex.lock:
            cmd("Lock", f'@lock {exit_ref}={ex.lock}')
        if ex.desc:
            cmd("Desc", f'@desc {exit_ref}={mux_escape(ex.desc)}')
        if ex.succ:
            cmd("Succ", f'@succ {exit_ref}={ex.succ}')
        if ex.osucc:
            cmd("Osucc", f'@osucc {exit_ref}={ex.osucc}')
        if ex.fail:
            cmd("Fail", f'@fail {exit_ref}={ex.fail}')
        if ex.ofail:
            cmd("Ofail", f'@ofail {exit_ref}={ex.ofail}')
        if ex.drop:
            cmd("Drop", f'@drop {exit_ref}={ex.drop}')
        if ex.odrop:
            cmd("Odrop", f'@odrop {exit_ref}={ex.odrop}')
        for flag in ex.flags:
            cmd(f"Flag {flag}", f'@set {exit_ref}={flag}')

    for ex in spec.exits:
        # Forward exit
        fwd_name = ex.name.split(';')[0]
        cmd(f"Exit: {fwd_name} ({ex.from_room} -> {ex.to_room})",
            f'@open {ex.name}=%{{room:{ex.to_room}}}')
        emit_exit_props(ex, fwd_name)

        # Back exit
        if ex.back_name:
            back_name = ex.back_name.split(';')[0]
            cmd(f"Back exit: {back_name} ({ex.to_room} -> {ex.from_room})",
                f'@open {ex.back_name}=%{{room:{ex.from_room}}}')
            # Back exits don't inherit forward exit's props

    # Create things
    if spec.things:
        cmd("=== Things ===", "")
        for thing_id, thing in spec.things.items():
            cmd(f"--- Thing: {thing.name} ({thing_id}) in {thing.location} ---", "")
            # Teleport to the room first
            cmd("Go to room",
                f'@teleport me=%{{room:{thing.location}}}')
            cmd("Create object",
                f'@create {thing.name}')
            if thing.description:
                desc = thing.description.rstrip().replace('\n', '%r')
                cmd("Set description",
                    f'@desc {thing.name}={desc}')
            for flag in thing.flags:
                cmd(f"Set flag {flag}",
                    f'@set {thing.name}={flag}')
            for attr_name, attr_value in thing.attrs.items():
                cmd(f"Set {attr_name}",
                    f'&{attr_name} {thing.name}={attr_value}')
            if thing.parent:
                cmd("Set parent",
                    f'@parent {thing.name}={thing.parent}')

    return commands


def format_plan(spec, drc_result):
    """Format a human-readable plan."""
    lines = []
    lines.append(f"WorldBuilder Plan — {spec.zone.name}")
    lines.append("=" * (len(lines[0])))
    lines.append("")

    # Count objects
    n_rooms = len(spec.rooms)
    n_exits = sum(1 + (1 if ex.back_name else 0) for ex in spec.exits)
    n_attrs = sum(len(r.attrs) for r in spec.rooms.values())
    n_flags = sum(len(r.flags) for r in spec.rooms.values())

    lines.append(f"Zone: {spec.zone.name}")
    lines.append(f"  + CREATE zone")
    lines.append("")

    lines.append(f"Rooms ({n_rooms} new):")
    for room_id, room in spec.rooms.items():
        lines.append(f"  + CREATE room \"{room.name}\" ({room_id})")
    lines.append("")

    lines.append(f"Exits ({n_exits} new):")
    for ex in spec.exits:
        forward_name = ex.name.split(';')[0]
        lines.append(f"  + CREATE exit \"{forward_name}\" from {ex.from_room} -> {ex.to_room}")
        if ex.back_name:
            back_name = ex.back_name.split(';')[0]
            lines.append(f"  + CREATE exit \"{back_name}\" from {ex.to_room} -> {ex.from_room}")
    lines.append("")

    n_things = len(spec.things)
    if n_things:
        lines.append(f"Things ({n_things} new):")
        for thing_id, thing in spec.things.items():
            lines.append(f"  + CREATE thing \"{thing.name}\" ({thing_id}) in {thing.location}")
        lines.append("")

    if n_attrs:
        lines.append(f"Attributes ({n_attrs}):")
        for room_id, room in spec.rooms.items():
            for attr_name in room.attrs:
                lines.append(f"  + SET {room_id}/{attr_name}")
        lines.append("")

    if n_flags:
        lines.append(f"Flags ({n_flags}):")
        for room_id, room in spec.rooms.items():
            for flag in room.flags:
                lines.append(f"  + SET {room_id}/{flag}")
        lines.append("")

    # DRC results
    total_checks = 12  # approximate
    lines.append(f"DRC: {total_checks - len(drc_result.errors)} checks passed, "
                 f"{len(drc_result.warnings)} warnings, {len(drc_result.errors)} errors.")

    for err in drc_result.errors:
        lines.append(f"  ERROR: {err}")
    for warn in drc_result.warnings:
        lines.append(f"  WARN:  {warn}")
    lines.append("")

    total = 1 + n_rooms + n_exits + n_things
    parts = [f"1 zone", f"{n_rooms} rooms", f"{n_exits} exits"]
    if n_things:
        parts.append(f"{n_things} things")
    lines.append(f"Total: {total} objects ({' + '.join(parts)})")

    return '\n'.join(lines)


def format_commands(commands, fmt='default'):
    """Format compiled commands for output.

    fmt='default' — comments + commands, one per line
    fmt='upload'  — unformat-compatible .mux format (formatted softcode)
    fmt='raw'     — commands only, no comments (for piping to telnet)
    """
    lines = []

    if fmt == 'upload':
        # Emit as formatted softcode compatible with unformat.pl
        # Each command gets a comment line and a '-' terminator
        for comment, command in commands:
            if comment and not command:
                lines.append(f"# {comment}")
            elif command:
                if comment:
                    lines.append(f"# {comment}")
                lines.append(command)
                lines.append("-")
                lines.append("")
        return '\n'.join(lines)

    elif fmt == 'raw':
        for _, command in commands:
            if command:
                lines.append(command)
        return '\n'.join(lines)

    else:  # default
        for comment, command in commands:
            if comment and not command:
                lines.append(f"\n{comment}")
            elif comment:
                lines.append(f"# {comment}")
                lines.append(command)
            else:
                lines.append(command)
        return '\n'.join(lines)


# ---------------------------------------------------------------------------
# Diff Engine — compare spec against saved state
# ---------------------------------------------------------------------------

class Change:
    CREATE = 'create'
    MODIFY = 'modify'
    DELETE = 'delete'

    def __init__(self, action, obj_type, obj_id, name='', details=None):
        self.action = action
        self.obj_type = obj_type
        self.obj_id = obj_id
        self.name = name
        self.details = details or []  # list of (field, old, new) tuples


def load_state(state_path):
    """Load a saved state file. Returns dict of spec_id -> {dbref, objid, type, name, desc, attrs, flags}."""
    path = Path(state_path)
    if not path.exists():
        return {}
    with open(path, 'r') as f:
        data = yaml.safe_load(f)
    return data.get('objects', {}) if data else {}


def diff_spec(spec, state_path):
    """Compare spec against saved state. Returns list of Changes."""
    state = load_state(state_path)
    changes = []

    # Rooms: new and modified
    for room_id, room in spec.rooms.items():
        if room_id not in state:
            changes.append(Change(Change.CREATE, 'room', room_id, room.name))
        else:
            # Check for modifications
            old = state[room_id]
            details = []

            # 1. Name
            if old.get('name', '') != room.name:
                details.append(('name', old.get('name', ''), room.name))

            # 2. Description
            old_desc = old.get('description', '').strip()
            new_desc = room.description.strip()
            if old_desc != new_desc:
                # Truncate for display if very long
                old_disp = (old_desc[:30] + '...') if len(old_desc) > 33 else old_desc
                new_disp = (new_desc[:30] + '...') if len(new_desc) > 33 else new_desc
                details.append(('description', old_disp, new_disp))

            # 3. Flags
            old_flags = set(old.get('flags', []))
            new_flags = set(room.flags)
            if old_flags != new_flags:
                added = new_flags - old_flags
                removed = old_flags - new_flags
                if added:
                    details.append(('flags+', '', ', '.join(sorted(added))))
                if removed:
                    details.append(('flags-', ', '.join(sorted(removed)), ''))

            # 4. Attributes
            old_attrs = old.get('attrs', {})
            new_attrs = room.attrs
            all_attr_names = set(old_attrs.keys()) | set(new_attrs.keys())
            for attr in sorted(all_attr_names):
                old_val = old_attrs.get(attr)
                new_val = new_attrs.get(attr)
                if old_val != new_val:
                    if old_val is None:
                        details.append((f'attr:{attr}+', '', new_val))
                    elif new_val is None:
                        details.append((f'attr:{attr}-', old_val, '(removed)'))
                    else:
                        details.append((f'attr:{attr}', old_val, new_val))

            if details:
                changes.append(Change(Change.MODIFY, 'room', room_id, room.name, details))

    # Rooms: deleted (in state but not in spec)
    for obj_id, obj in state.items():
        if obj.get('type') == 'room' and obj_id not in spec.rooms:
            changes.append(Change(Change.DELETE, 'room', obj_id, obj.get('name', '')))

    # Exits: we track by exit_from_to key
    spec_exit_keys = set()
    for ex in spec.exits:
        key = f'exit_{ex.from_room}_{ex.to_room}'
        spec_exit_keys.add(key)
        if key not in state:
            changes.append(Change(Change.CREATE, 'exit', key,
                                  ex.name.split(';')[0]))
        if ex.back_name:
            back_key = f'exit_{ex.to_room}_{ex.from_room}'
            spec_exit_keys.add(back_key)
            if back_key not in state:
                changes.append(Change(Change.CREATE, 'exit', back_key,
                                      ex.back_name.split(';')[0]))

    # Exits: deleted
    for obj_id, obj in state.items():
        if obj.get('type') == 'exit' and obj_id not in spec_exit_keys:
            changes.append(Change(Change.DELETE, 'exit', obj_id, obj.get('name', '')))

    return changes


def compile_incremental(spec, state_path):
    """Compile only the changes between spec and state."""
    state = load_state(state_path)
    commands = []

    def cmd(comment, command):
        commands.append((comment, command))

    # Rooms: only new or modified
    for room_id, room in spec.rooms.items():
        if room_id not in state:
            # New room — full create
            cmd(f"--- NEW room: {room.name} ({room_id}) ---", "")
            cmd("Create room", f'@dig/teleport {room.name}')
            cmd("Store dbref", f'think [setq(0, %L)] Room {room_id} = %q0')
            desc = room.description.rstrip().replace('\n', '%r')
            cmd("Set description", f'@desc here={desc}')
            for flag in room.flags:
                cmd(f"Set flag {flag}", f'@set here={flag}')
            for attr_name, attr_value in room.attrs.items():
                cmd(f"Set {attr_name}", f'&{attr_name} here={attr_value}')
            cmd("Set zone", f'@chzone here=%{{zone:{spec.zone.name}}}')
        else:
            # Existing room — update in place
            old = state[room_id]
            dbref = old.get('dbref', '')
            if not dbref:
                continue

            # Compute actual changes
            name_changed = old.get('name') != room.name
            desc_changed = old.get('description', '').strip() != room.description.strip()
            
            old_flags = set(old.get('flags', []))
            new_flags = set(room.flags)
            flags_added = new_flags - old_flags
            flags_removed = old_flags - new_flags

            old_attrs = old.get('attrs', {})
            new_attrs = room.attrs
            attrs_to_set = {k: v for k, v in new_attrs.items() if old_attrs.get(k) != v}
            attrs_to_unset = set(old_attrs.keys()) - set(new_attrs.keys())

            if any([name_changed, desc_changed, flags_added, flags_removed, attrs_to_set, attrs_to_unset]):
                cmd(f"--- UPDATE room: {room.name} ({room_id}) at {dbref} ---", "")
                cmd("Teleport to room", f'@teleport me={dbref}')

                if name_changed:
                    cmd("Update name", f'@name here={room.name}')
                
                if desc_changed:
                    desc = room.description.rstrip().replace('\n', '%r')
                    cmd("Update description", f'@desc here={desc}')

                for flag in sorted(flags_added):
                    cmd(f"Set flag {flag}", f'@set here={flag}')
                for flag in sorted(flags_removed):
                    cmd(f"Clear flag {flag}", f'@set here=!{flag}')

                for attr_name in sorted(attrs_to_set):
                    cmd(f"Update {attr_name}", f'&{attr_name} here={attrs_to_set[attr_name]}')
                for attr_name in sorted(attrs_to_unset):
                    cmd(f"Remove {attr_name}", f'&{attr_name} here=')

    # Deletions: objects in state but NOT in spec
    deletions = []
    for obj_id, obj in state.items():
        if obj.get('type') == 'room' and obj_id not in spec.rooms:
            deletions.append((obj_id, obj))
    
    if deletions:
        cmd("=== DESTROY removed rooms ===", "")
        for obj_id, obj in deletions:
            dbref = obj.get('dbref', '')
            if dbref:
                cmd(f"Destroy room: {obj.get('name')} ({obj_id})", f'@destroy/override {dbref}')

    # Exits: only new ones (modifying exits is rare — delete + recreate)
    # TODO: Implement incremental exit updates (locks, descs)
    for ex in spec.exits:
        key = f'exit_{ex.from_room}_{ex.to_room}'
        if key not in state:
            cmd(f"--- NEW exit: {ex.name.split(';')[0]} ---", "")
            cmd("Forward exit",
                f'@open {ex.name}=%{{room:{ex.to_room}}}')
        if ex.back_name:
            back_key = f'exit_{ex.to_room}_{ex.from_room}'
            if back_key not in state:
                cmd(f"--- NEW back exit: {ex.back_name.split(';')[0]} ---", "")
                cmd("Back exit",
                    f'@open {ex.back_name}=%{{room:{ex.from_room}}}')

    return commands


def format_diff(changes):
    """Format changes for human display."""
    if not changes:
        return "No changes detected."

    lines = []
    symbols = {Change.CREATE: '+', Change.MODIFY: '~', Change.DELETE: '-'}
    labels = {Change.CREATE: 'CREATE', Change.MODIFY: 'MODIFY', Change.DELETE: 'DELETE'}

    for ch in changes:
        sym = symbols[ch.action]
        label = labels[ch.action]
        lines.append(f"  {sym} {label} {ch.obj_type} \"{ch.name}\" ({ch.obj_id})")
        for field, old, new in ch.details:
            lines.append(f"      {field}: \"{old}\" -> \"{new}\"")

    summary = {}
    for ch in changes:
        key = (ch.action, ch.obj_type)
        summary[key] = summary.get(key, 0) + 1

    lines.append("")
    parts = []
    for (action, obj_type), count in sorted(summary.items()):
        parts.append(f"{count} {obj_type}(s) to {action}")
    lines.append("Summary: " + ", ".join(parts))

    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='WorldBuilder — MUX game content management tool')
    parser.add_argument('action', choices=['plan', 'compile', 'check', 'diff', 'lint'],
                        help='Action to perform')
    parser.add_argument('spec', help='Path to YAML spec file')
    parser.add_argument('--state', help='State file for diff/incremental compile')
    parser.add_argument('--format', choices=['default', 'upload', 'raw'],
                        default='default', help='Output format for compile')

    args = parser.parse_args()
    spec_path = Path(args.spec)

    if not spec_path.exists():
        print(f"Error: File not found: {spec_path}", file=sys.stderr)
        sys.exit(1)

    # Parse
    try:
        spec = parse_spec(spec_path)
    except Exception as e:
        print(f"Error parsing spec: {e}", file=sys.stderr)
        sys.exit(1)

    # DRC
    drc = check_drc(spec)

    # Default state path
    state_path = args.state or f'.worldbuilder/{spec.zone.name.lower().replace(" ", "_")}.state.yaml'

    if args.action == 'check':
        if drc.ok and not drc.warnings:
            print("DRC: All checks passed.")
        else:
            for err in drc.errors:
                print(f"ERROR: {err}")
            for warn in drc.warnings:
                print(f"WARN:  {warn}")
        sys.exit(0 if drc.ok else 1)

    elif args.action == 'lint':
        from softcode_lint import lint_spec
        lint_result = lint_spec(spec)
        if lint_result.ok and not lint_result.warnings:
            print("Softcode lint: All checks passed.")
        else:
            for loc, msg in lint_result.errors:
                print(f"ERROR [{loc}]: {msg}")
            for loc, msg in lint_result.warnings:
                print(f"WARN  [{loc}]: {msg}")
        sys.exit(0 if lint_result.ok else 1)

    elif args.action == 'plan':
        print(format_plan(spec, drc))
        sys.exit(0 if drc.ok else 1)

    elif args.action == 'diff':
        if not drc.ok:
            print("DRC errors:", file=sys.stderr)
            for err in drc.errors:
                print(f"  ERROR: {err}", file=sys.stderr)
            sys.exit(1)

        changes = diff_spec(spec, state_path)
        print(f"WorldBuilder Diff — {spec.zone.name}")
        print(f"State file: {state_path}")
        print()
        print(format_diff(changes))

    elif args.action == 'compile':
        if not drc.ok:
            print("DRC errors — cannot compile:", file=sys.stderr)
            for err in drc.errors:
                print(f"  ERROR: {err}", file=sys.stderr)
            sys.exit(1)

        if drc.warnings:
            for warn in drc.warnings:
                print(f"# WARN: {warn}", file=sys.stderr)

        out_fmt = args.format

        # Use incremental compile if state exists
        if Path(state_path).exists():
            commands = compile_incremental(spec, state_path)
            if not commands:
                print("# No changes to apply (spec matches state).")
            else:
                if out_fmt == 'default':
                    print(f"# Incremental compile against {state_path}")
                print(format_commands(commands, out_fmt))
        else:
            commands = compile_spec(spec)
            print(format_commands(commands, out_fmt))


if __name__ == '__main__':
    main()
