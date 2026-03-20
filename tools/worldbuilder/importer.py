#!/usr/bin/env python3
"""WorldBuilder Importer — walk a live MUX zone and generate a spec.

Usage:
    importer.py --host <host> --port <port> --character <char> --password <pw>
                --start <dbref> [--depth <N>] [--output <file>] [--zone <name>]

Connects to the game, examines the starting room, follows exits to
discover connected rooms, reads properties, and outputs a YAML spec
plus a state file mapping spec IDs to dbrefs.
"""

import re
import time
import yaml
import argparse
from pathlib import Path
from executor import MuxConnection


# ---------------------------------------------------------------------------
# MUX Query Helpers
# ---------------------------------------------------------------------------

def query(conn, cmd):
    """Send a think command and capture the output."""
    conn.send(cmd)
    time.sleep(0.4)
    resp = conn.read_response(0.5)
    # Strip blank lines and prompts
    lines = [l.strip() for l in resp.strip().split('\n') if l.strip()]
    return lines


def query_one(conn, cmd):
    """Send a think command and return the first non-empty line."""
    lines = query(conn, cmd)
    return lines[0] if lines else ''


def get_name(conn, dbref):
    return query_one(conn, f'think [name({dbref})]')


def get_desc(conn, dbref):
    # Description can be multiline — use get()
    lines = query(conn, f'think [get({dbref}/DESCRIBE)]')
    if not lines or lines == ['']:
        # Try @desc style
        lines = query(conn, f'think [get({dbref}/DESC)]')
    return '\n'.join(lines) if lines else ''


def get_flags(conn, dbref):
    raw = query_one(conn, f'think [flags({dbref})]')
    return raw


def get_exits(conn, dbref):
    """Get list of exit dbrefs from a room."""
    raw = query_one(conn, f'think [lcon({dbref}, EXIT)]')
    if not raw or raw == '#-1':
        # Try exits()
        raw = query_one(conn, f'think [exits({dbref})]')
    if not raw or raw.startswith('#-1'):
        return []
    return [x.strip() for x in raw.split() if x.strip().startswith('#')]


def get_exit_dest(conn, exit_dbref):
    """Get the destination dbref of an exit."""
    return query_one(conn, f'think [home({exit_dbref})]')


def get_exit_name(conn, exit_dbref):
    """Get the full name (with aliases) of an exit."""
    return query_one(conn, f'think [name({exit_dbref})]')


def get_objid(conn, dbref):
    """Get the objid (dbref:creation_timestamp) of an object."""
    return query_one(conn, f'think [objid({dbref})]')


def get_attrs(conn, dbref, skip=None, use_decomp=True):
    """Get user-settable attributes on an object.

    If use_decomp is True, tries @decomp/tf first (one command, all attrs).
    Falls back to lattr+get if @decomp is not available.
    """
    skip = skip or {'DESCRIBE', 'DESC', 'IDESC', 'LAST', 'LASTSITE',
                    'LASTIP', 'LASTPAGE', 'MAILCURF', 'MAILFLAGS',
                    'STARTUP', 'CREATED', 'MODIFIED'}
    attrs = {}

    if use_decomp:
        # @decomp/tf outputs: &ATTR #dbref=value (one per line)
        lines = query(conn, f'@decomp/tf {dbref}')
        for line in lines:
            # Parse &ATTR #NNN=value
            if line.startswith('&') and '=' in line:
                # &ATTRNAME #1234=value
                eq_pos = line.index('=')
                left = line[1:eq_pos]  # ATTRNAME #1234
                value = line[eq_pos + 1:]
                # Split off the dbref from the attr name
                parts = left.rsplit(' ', 1)
                if len(parts) == 2:
                    attr_name = parts[0]
                    if attr_name.upper() not in skip:
                        attrs[attr_name] = value
            # Also catch @desc, @succ, etc.
            elif line.startswith('@desc ') and '=' in line:
                pass  # Description handled separately
        if attrs:
            return attrs
        # If @decomp returned nothing useful, fall through to lattr

    # Fallback: lattr + get (N queries)
    raw = query_one(conn, f'think [lattr({dbref})]')
    if not raw or raw.startswith('#-1'):
        return {}
    attr_names = [a.strip() for a in raw.split() if a.strip()]
    for attr in attr_names:
        if attr.upper() in skip:
            continue
        val = query_one(conn, f'think [get({dbref}/{attr})]')
        if val:
            attrs[attr] = val
    return attrs


# ---------------------------------------------------------------------------
# Zone Walker
# ---------------------------------------------------------------------------

def walk_zone(conn, start_dbref, max_depth=20):
    """BFS from start_dbref, following exits. Returns discovered rooms and exits.

    Returns:
        rooms: dict of dbref -> {name, description, flags, attrs}
        exits: list of {from_dbref, to_dbref, name, exit_dbref}
    """
    rooms = {}
    exits = []
    visited = set()
    queue = [(start_dbref, 0)]

    print(f"Walking zone from {start_dbref} (max depth {max_depth})...")

    while queue:
        dbref, depth = queue.pop(0)
        if dbref in visited or depth > max_depth:
            continue
        visited.add(dbref)

        # Get room info
        name = get_name(conn, dbref)
        if not name:
            print(f"  [skip] {dbref} — could not read name")
            continue

        desc = get_desc(conn, dbref)
        flags = get_flags(conn, dbref)
        attrs = get_attrs(conn, dbref)
        objid = get_objid(conn, dbref)

        rooms[dbref] = {
            'name': name,
            'description': desc,
            'flags': flags,
            'attrs': attrs,
            'objid': objid,
        }
        print(f"  [{len(rooms)}] {dbref} {name}")

        # Teleport there and find exits
        conn.send(f'@teleport me={dbref}')
        time.sleep(0.3)
        conn.read_response(0.3)

        exit_dbrefs = get_exits(conn, dbref)
        for exit_dbref in exit_dbrefs:
            exit_name = get_exit_name(conn, exit_dbref)
            dest = get_exit_dest(conn, exit_dbref)
            if dest and dest != '#-1':
                exits.append({
                    'from_dbref': dbref,
                    'to_dbref': dest,
                    'name': exit_name,
                    'exit_dbref': exit_dbref,
                })
                # Queue the destination for exploration
                if dest not in visited:
                    queue.append((dest, depth + 1))

    return rooms, exits


# ---------------------------------------------------------------------------
# Spec Generator
# ---------------------------------------------------------------------------

def sanitize_id(name):
    """Convert a room name to a safe YAML key."""
    s = name.lower()
    s = re.sub(r'[^a-z0-9]+', '_', s)
    s = s.strip('_')
    return s or 'room'


def generate_spec(rooms, exits, zone_name):
    """Generate a YAML spec dict from discovered rooms and exits."""
    # Map dbrefs to spec IDs
    dbref_to_id = {}
    id_counts = {}
    for dbref, info in rooms.items():
        base_id = sanitize_id(info['name'])
        if base_id in id_counts:
            id_counts[base_id] += 1
            spec_id = f"{base_id}_{id_counts[base_id]}"
        else:
            id_counts[base_id] = 1
            spec_id = base_id
        dbref_to_id[dbref] = spec_id

    # Build spec
    spec = {
        'zone': {'name': zone_name},
        'rooms': {},
        'exits': [],
    }

    for dbref, info in rooms.items():
        spec_id = dbref_to_id[dbref]
        room_data = {
            'name': info['name'],
        }
        if info['description']:
            room_data['description'] = info['description'] + '\n'
        if info['attrs']:
            room_data['attrs'] = info['attrs']
        spec['rooms'][spec_id] = room_data

    # Deduplicate exits (find pairs for bidirectional)
    exit_pairs = {}  # (from, to) -> exit data
    for ex in exits:
        from_id = dbref_to_id.get(ex['from_dbref'])
        to_id = dbref_to_id.get(ex['to_dbref'])
        if not from_id or not to_id:
            continue
        key = (from_id, to_id)
        exit_pairs[key] = ex['name']

    # Match bidirectional pairs
    used = set()
    for (from_id, to_id), name in exit_pairs.items():
        if (from_id, to_id) in used:
            continue
        exit_data = {
            'from': from_id,
            'to': to_id,
            'name': name,
        }
        # Check for reverse exit
        reverse_key = (to_id, from_id)
        if reverse_key in exit_pairs and reverse_key not in used:
            exit_data['back'] = exit_pairs[reverse_key]
            used.add(reverse_key)
        used.add((from_id, to_id))
        spec['exits'].append(exit_data)

    return spec, dbref_to_id


def generate_state(dbref_to_id, rooms):
    """Generate a state file dict."""
    objects = {}
    for dbref, spec_id in dbref_to_id.items():
        info = rooms.get(dbref, {})
        objects[spec_id] = {
            'dbref': dbref,
            'objid': info.get('objid'),
            'type': 'room',
            'name': info.get('name', ''),
            'description': info.get('description', ''),
            'flags': info.get('flags', '').split(),
            'attrs': info.get('attrs', {}),
            'status': 'active',
        }
    return {
        'state_version': 1,
        'zone': None,
        'last_applied': time.strftime('%Y-%m-%dT%H:%M:%SZ'),
        'objects': objects,
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='WorldBuilder Importer — walk a MUX zone and generate a spec')
    parser.add_argument('--host', required=True, help='MUX hostname')
    parser.add_argument('--port', default='4201', help='MUX port')
    parser.add_argument('--ssl', action='store_true', help='Use SSL/TLS')
    parser.add_argument('--character', required=True, help='Character name')
    parser.add_argument('--password', required=True, help='Password')
    parser.add_argument('--start', required=True, help='Starting room dbref (e.g. #123)')
    parser.add_argument('--depth', type=int, default=20, help='Max BFS depth (default 20)')
    parser.add_argument('--zone', default='Imported Zone', help='Zone name for the spec')
    parser.add_argument('--output', default='imported.yaml', help='Output spec file')
    parser.add_argument('--state', help='Output state file (default: .worldbuilder/<zone>.state.yaml)')

    args = parser.parse_args()

    # Connect
    print(f"Connecting to {args.host}:{args.port}...")
    conn = MuxConnection(args.host, int(args.port), args.ssl)
    conn.connect()
    print("Connected. Logging in...")
    resp = conn.login(args.character, args.password)
    print("Logged in.")

    try:
        # Walk
        rooms, exits = walk_zone(conn, args.start, args.depth)
        print(f"\nDiscovered {len(rooms)} rooms and {len(exits)} exits.")

        # Generate spec
        spec, dbref_to_id = generate_spec(rooms, exits, args.zone)

        # Write spec
        output_path = Path(args.output)
        with open(output_path, 'w', encoding='utf-8') as f:
            yaml.dump(spec, f, default_flow_style=False, sort_keys=False,
                      allow_unicode=True, width=120)
        print(f"Spec written to {output_path}")

        # Write state
        state_path = args.state or f'.worldbuilder/{args.zone.lower().replace(" ", "_")}.state.yaml'
        state_data = generate_state(dbref_to_id, rooms)
        state_data['zone'] = args.zone
        Path(state_path).parent.mkdir(parents=True, exist_ok=True)
        with open(state_path, 'w') as f:
            yaml.dump(state_data, f, default_flow_style=False, sort_keys=False)
        print(f"State written to {state_path}")

        # Validate the generated spec
        print("\nValidating generated spec...")
        from worldbuilder import parse_spec, check_drc
        gen_spec = parse_spec(output_path)
        drc = check_drc(gen_spec)
        if drc.ok:
            print("DRC: All checks passed.")
        else:
            for err in drc.errors:
                print(f"  ERROR: {err}")
        for warn in drc.warnings:
            print(f"  WARN:  {warn}")

    finally:
        conn.disconnect()
        print("Disconnected.")


if __name__ == '__main__':
    exit(main() or 0)
