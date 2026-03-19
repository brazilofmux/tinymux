#!/usr/bin/env python3
"""WorldBuilder Map Generator — visual maps from spec topology.

Usage:
    mapgen.py <spec.yaml> [--format ascii|dot]
    mapgen.py <project.yaml> --project [--format ascii|dot]

Generates:
    ascii — text-art map with box-drawing characters
    dot   — Graphviz DOT format for rendering with `dot -Tpng`
"""

import sys
import yaml
import argparse
from pathlib import Path
from worldbuilder import parse_spec


# ---------------------------------------------------------------------------
# Graph Extraction
# ---------------------------------------------------------------------------

def extract_graph(spec):
    """Extract a room graph from a spec.

    Returns:
        nodes: dict of room_id -> short_name
        edges: list of (from_id, to_id, direction_hint)
    """
    nodes = {}
    for room_id, room in spec.rooms.items():
        # Shorten name for display
        name = room.name
        if len(name) > 20:
            name = name[:18] + '..'
        nodes[room_id] = name

    edges = []
    seen = set()
    for ex in spec.exits:
        if ex.from_room in nodes and ex.to_room in nodes:
            pair = tuple(sorted([ex.from_room, ex.to_room]))
            if pair not in seen:
                # Determine direction hint from exit name
                direction = guess_direction(ex.name)
                edges.append((ex.from_room, ex.to_room, direction))
                seen.add(pair)

    return nodes, edges


def guess_direction(exit_name):
    """Guess cardinal direction from exit name/aliases."""
    lower = exit_name.lower()
    aliases = [a.strip() for a in lower.split(';')]
    for a in aliases:
        if a in ('n', 'north'): return 'n'
        if a in ('s', 'south'): return 's'
        if a in ('e', 'east'): return 'e'
        if a in ('w', 'west'): return 'w'
        if a in ('u', 'up', 'upstairs', 'stairs'): return 'u'
        if a in ('d', 'down', 'downstairs'): return 'd'
        if a in ('ne', 'northeast'): return 'ne'
        if a in ('nw', 'northwest'): return 'nw'
        if a in ('se', 'southeast'): return 'se'
        if a in ('sw', 'southwest'): return 'sw'
    return '?'


# ---------------------------------------------------------------------------
# ASCII Map
# ---------------------------------------------------------------------------

def layout_grid(nodes, edges):
    """Assign grid positions to nodes using a simple BFS-based layout.

    Tries to respect cardinal directions from edge hints.
    """
    if not nodes:
        return {}

    positions = {}  # room_id -> (x, y)
    occupied = set()  # (x, y) positions in use

    # Build adjacency with directions
    adj = {}
    for from_id, to_id, direction in edges:
        adj.setdefault(from_id, []).append((to_id, direction))
        # Reverse direction
        rev = {'n': 's', 's': 'n', 'e': 'w', 'w': 'e',
               'u': 'd', 'd': 'u', 'ne': 'sw', 'sw': 'ne',
               'nw': 'se', 'se': 'nw', '?': '?'}
        adj.setdefault(to_id, []).append((from_id, rev.get(direction, '?')))

    # Direction to delta
    deltas = {
        'n': (0, -1), 's': (0, 1), 'e': (1, 0), 'w': (-1, 0),
        'u': (1, -1), 'd': (-1, 1),
        'ne': (1, -1), 'nw': (-1, -1), 'se': (1, 1), 'sw': (-1, 1),
        '?': (1, 0),
    }

    # BFS from first node
    start = list(nodes.keys())[0]
    positions[start] = (0, 0)
    occupied.add((0, 0))
    queue = [start]
    visited = {start}

    while queue:
        current = queue.pop(0)
        cx, cy = positions[current]

        for neighbor, direction in adj.get(current, []):
            if neighbor in visited or neighbor not in nodes:
                continue

            dx, dy = deltas.get(direction, (1, 0))
            nx, ny = cx + dx, cy + dy

            # Find a free position near the desired spot
            attempts = 0
            while (nx, ny) in occupied and attempts < 20:
                # Spiral out
                attempts += 1
                if attempts % 4 == 1: nx += 1
                elif attempts % 4 == 2: ny += 1
                elif attempts % 4 == 3: nx -= 1
                else: ny -= 1

            positions[neighbor] = (nx, ny)
            occupied.add((nx, ny))
            visited.add(neighbor)
            queue.append(neighbor)

    # Place any unvisited nodes
    max_x = max(x for x, y in occupied) + 2 if occupied else 0
    for room_id in nodes:
        if room_id not in positions:
            while (max_x, 0) in occupied:
                max_x += 1
            positions[room_id] = (max_x, 0)
            occupied.add((max_x, 0))
            max_x += 1

    return positions


def render_ascii(nodes, edges, positions):
    """Render an ASCII map from node positions."""
    if not positions:
        return "(empty map)"

    # Normalize positions to 0-based
    min_x = min(x for x, y in positions.values())
    min_y = min(y for x, y in positions.values())
    pos = {rid: (x - min_x, y - min_y) for rid, (x, y) in positions.items()}

    max_x = max(x for x, y in pos.values())
    max_y = max(y for x, y in pos.values())

    # Cell dimensions
    cell_w = 24  # chars per cell horizontally
    cell_h = 5   # chars per cell vertically

    # Canvas
    canvas_w = (max_x + 1) * cell_w + 1
    canvas_h = (max_y + 1) * cell_h + 1
    canvas = [[' '] * canvas_w for _ in range(canvas_h)]

    def put(r, c, ch):
        if 0 <= r < canvas_h and 0 <= c < canvas_w:
            canvas[r][c] = ch

    def puts(r, c, s):
        for i, ch in enumerate(s):
            put(r, c + i, ch)

    # Draw boxes for each room
    for room_id, (gx, gy) in pos.items():
        name = nodes.get(room_id, room_id)
        # Box top-left corner
        bx = gx * cell_w + 1
        by = gy * cell_h + 1

        # Draw box
        puts(by, bx, '+' + '-' * (cell_w - 4) + '+')
        for row in range(1, cell_h - 2):
            put(by + row, bx, '|')
            put(by + row, bx + cell_w - 3, '|')
        puts(by + cell_h - 3, bx, '+' + '-' * (cell_w - 4) + '+')

        # Room name centered in box
        display = name[:cell_w - 6]
        pad = (cell_w - 4 - len(display)) // 2
        puts(by + 1, bx + 1 + pad, display)

    # Draw connections between adjacent rooms
    edge_set = set()
    for from_id, to_id, direction in edges:
        if from_id in pos and to_id in pos:
            edge_set.add((from_id, to_id))

    for from_id, to_id, direction in edges:
        if from_id not in pos or to_id not in pos:
            continue
        fx, fy = pos[from_id]
        tx, ty = pos[to_id]

        # Only draw simple horizontal/vertical connections
        if fy == ty and abs(fx - tx) == 1:
            # Horizontal
            left = min(fx, tx)
            bx = left * cell_w + 1 + cell_w - 3
            by = fy * cell_h + 1 + 1
            for c in range(bx + 1, bx + 4):
                put(by, c, '-')
        elif fx == tx and abs(fy - ty) == 1:
            # Vertical
            top = min(fy, ty)
            bx = fx * cell_w + 1 + (cell_w - 3) // 2
            by = top * cell_h + 1 + cell_h - 3
            for r in range(by + 1, by + 3):
                put(r, bx, '|')

    return '\n'.join(''.join(row).rstrip() for row in canvas)


# ---------------------------------------------------------------------------
# DOT (Graphviz) Format
# ---------------------------------------------------------------------------

def render_dot(nodes, edges, spec_name='WorldBuilder'):
    """Render a Graphviz DOT graph."""
    lines = [f'digraph "{spec_name}" {{']
    lines.append('  rankdir=TB;')
    lines.append('  node [shape=box, style=filled, fillcolor="#2a2a3a", '
                 'fontcolor=white, fontname="Consolas"];')
    lines.append('  edge [color="#606060", arrowsize=0.7];')
    lines.append('')

    for room_id, name in nodes.items():
        safe_name = name.replace('"', '\\"')
        lines.append(f'  "{room_id}" [label="{safe_name}"];')

    lines.append('')

    for from_id, to_id, direction in edges:
        label = direction if direction != '?' else ''
        if label:
            lines.append(f'  "{from_id}" -> "{to_id}" [label="{label}", dir=both];')
        else:
            lines.append(f'  "{from_id}" -> "{to_id}" [dir=both];')

    lines.append('}')
    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='WorldBuilder Map Generator')
    parser.add_argument('spec', help='YAML spec or project file')
    parser.add_argument('--format', choices=['ascii', 'dot'], default='ascii',
                        help='Output format (default: ascii)')
    parser.add_argument('--project', action='store_true',
                        help='Treat input as a project file')

    args = parser.parse_args()
    path = Path(args.spec)

    if not path.exists():
        print(f"Error: {path} not found", file=sys.stderr)
        return 1

    if args.project:
        from project import load_project, load_all_zones
        project, base_dir = load_project(path)
        ps = load_all_zones(project, base_dir)
        # Merge all zones into one graph
        all_nodes = {}
        all_edges = []
        for zone_file, spec in ps.zone_specs.items():
            nodes, edges = extract_graph(spec)
            all_nodes.update(nodes)
            all_edges.extend(edges)
        nodes, edges = all_nodes, all_edges
        spec_name = project.name
    else:
        spec = parse_spec(path)
        nodes, edges = extract_graph(spec)
        spec_name = spec.zone.name

    if args.format == 'dot':
        print(render_dot(nodes, edges, spec_name))
    else:
        positions = layout_grid(nodes, edges)
        print(render_ascii(nodes, edges, positions))

    return 0


if __name__ == '__main__':
    exit(main())
