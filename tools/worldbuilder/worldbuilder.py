#!/usr/bin/env python3
"""WorldBuilder — MUX game content management tool.

Usage:
    worldbuilder.py plan <spec.yaml>        — validate and show execution plan
    worldbuilder.py compile <spec.yaml>     — output MUX commands to stdout
    worldbuilder.py check <spec.yaml>       — validate only (DRC checks)
"""

import sys
import yaml
import argparse
from pathlib import Path


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


class Exit:
    def __init__(self, from_room, to_room, name, back_name=None):
        self.from_room = from_room
        self.to_room = to_room
        self.name = name
        self.back_name = back_name


class Zone:
    def __init__(self, name, description=''):
        self.name = name
        self.description = description


class WorldSpec:
    def __init__(self):
        self.zone = None
        self.rooms = {}      # id -> Room
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

    # Exits
    for exit_data in data.get('exits', []):
        ex = Exit(
            from_room=exit_data['from'],
            to_room=exit_data['to'],
            name=exit_data['name'],
            back_name=exit_data.get('back'),
        )
        spec.exits.append(ex)

    return spec


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

        # Return to a staging room after each room creation
        # The executor will handle teleportation between rooms
        cmd("", "")

    # Create exits
    cmd("=== Exits ===", "")

    for ex in spec.exits:
        # Forward exit
        cmd(f"Exit: {ex.name.split(';')[0]} ({ex.from_room} -> {ex.to_room})",
            f'@open {ex.name}=%{{room:{ex.to_room}}}')
        # Note: %{room:xxx} is a placeholder the executor resolves to the actual dbref

        # Back exit
        if ex.back_name:
            cmd(f"Back exit: {ex.back_name.split(';')[0]} ({ex.to_room} -> {ex.from_room})",
                f'@open {ex.back_name}=%{{room:{ex.from_room}}}')

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

    total = 1 + n_rooms + n_exits
    lines.append(f"Total: {total} objects (1 zone + {n_rooms} rooms + {n_exits} exits)")

    return '\n'.join(lines)


def format_commands(commands):
    """Format compiled commands for output."""
    lines = []
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
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='WorldBuilder — MUX game content management tool')
    parser.add_argument('action', choices=['plan', 'compile', 'check'],
                        help='Action to perform')
    parser.add_argument('spec', help='Path to YAML spec file')

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

    if args.action == 'check':
        if drc.ok and not drc.warnings:
            print("DRC: All checks passed.")
        else:
            for err in drc.errors:
                print(f"ERROR: {err}")
            for warn in drc.warnings:
                print(f"WARN:  {warn}")
        sys.exit(0 if drc.ok else 1)

    elif args.action == 'plan':
        print(format_plan(spec, drc))
        sys.exit(0 if drc.ok else 1)

    elif args.action == 'compile':
        if not drc.ok:
            print("DRC errors — cannot compile:", file=sys.stderr)
            for err in drc.errors:
                print(f"  ERROR: {err}", file=sys.stderr)
            sys.exit(1)

        if drc.warnings:
            for warn in drc.warnings:
                print(f"# WARN: {warn}", file=sys.stderr)

        commands = compile_spec(spec)
        print(format_commands(commands))


if __name__ == '__main__':
    main()
