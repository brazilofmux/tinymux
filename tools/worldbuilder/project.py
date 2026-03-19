#!/usr/bin/env python3
"""WorldBuilder Project — multi-zone project management.

A project file ties multiple zone specs together, resolves cross-zone
references ($external_id), and validates the entire game world as a unit.

Usage:
    project.py check <project.yaml>    — validate all zones + cross-refs
    project.py plan <project.yaml>     — combined plan across all zones
    project.py lint <project.yaml>     — softcode lint all zones
    project.py compile <project.yaml>  — compile all zones in order
"""

import sys
import yaml
import argparse
from pathlib import Path
from worldbuilder import parse_spec, check_drc, compile_spec, format_commands, format_plan
from softcode_lint import lint_spec


# ---------------------------------------------------------------------------
# Project Model
# ---------------------------------------------------------------------------

class Project:
    def __init__(self, name, zones=None, cross_refs=None, build_order=None):
        self.name = name
        self.zones = zones or []              # list of zone file paths
        self.cross_refs = cross_refs or {}    # alias -> zone_id/room_id
        self.build_order = build_order or []  # explicit ordering (optional)


class ProjectSpec:
    """Loaded project with parsed zone specs."""
    def __init__(self, project, base_dir):
        self.project = project
        self.base_dir = base_dir
        self.zone_specs = {}    # filename -> WorldSpec
        self.zone_drcs = {}     # filename -> DRCResult
        self.all_room_ids = {}  # "zone_file:room_id" -> room_id


# ---------------------------------------------------------------------------
# Loader
# ---------------------------------------------------------------------------

def load_project(path):
    """Load a project YAML file."""
    path = Path(path)
    with open(path, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f)

    proj_data = data.get('project', data)

    project = Project(
        name=proj_data.get('name', 'Unnamed Project'),
        zones=proj_data.get('zones', []),
        cross_refs=proj_data.get('cross_references', {}),
        build_order=proj_data.get('build_order', []),
    )

    return project, path.parent


def load_all_zones(project, base_dir):
    """Load and parse all zone specs in a project."""
    ps = ProjectSpec(project, base_dir)

    for zone_path_str in project.zones:
        zone_path = base_dir / zone_path_str
        if not zone_path.exists():
            print(f"ERROR: Zone file not found: {zone_path}", file=sys.stderr)
            continue

        spec = parse_spec(zone_path)
        drc = check_drc(spec)
        ps.zone_specs[zone_path_str] = spec
        ps.zone_drcs[zone_path_str] = drc

        # Register all room IDs
        for room_id in spec.rooms:
            qualified = f"{zone_path_str}:{room_id}"
            ps.all_room_ids[qualified] = room_id

    return ps


# ---------------------------------------------------------------------------
# Cross-Reference Validation
# ---------------------------------------------------------------------------

def validate_cross_refs(ps):
    """Validate that all $references in all zones resolve.

    Returns list of error strings.
    """
    errors = []

    # Build a lookup of all available targets
    available = set()
    for zone_file, spec in ps.zone_specs.items():
        for room_id in spec.rooms:
            available.add(f"{zone_file}:{room_id}")
            available.add(room_id)  # short form (ambiguous if duplicated)

    # Add cross-reference aliases
    ref_targets = {}
    for alias, target in ps.project.cross_refs.items():
        ref_targets[alias] = target
        # Verify the target exists
        if target not in available:
            # Try qualified form
            found = False
            for zone_file in ps.zone_specs:
                if f"{zone_file}:{target}" in available:
                    found = True
                    break
            if not found:
                errors.append(f"Cross-reference '${alias}' points to unknown room: {target}")

    # Check all exits in all zones for $references
    for zone_file, spec in ps.zone_specs.items():
        for ex in spec.exits:
            for ref_field in [ex.to_room, ex.from_room]:
                if ref_field.startswith('$'):
                    alias = ref_field[1:]
                    if alias not in ref_targets:
                        # Check if it's a room in another zone
                        found = False
                        for other_file, other_spec in ps.zone_specs.items():
                            if other_file != zone_file and alias in other_spec.rooms:
                                found = True
                                break
                        if not found:
                            errors.append(
                                f"Zone '{zone_file}': exit references unknown '${alias}' "
                                f"(not in cross_references and not found in other zones)")

    return errors


# ---------------------------------------------------------------------------
# Project-Level Statistics
# ---------------------------------------------------------------------------

def project_stats(ps):
    """Compute aggregate statistics across all zones."""
    total_rooms = 0
    total_exits = 0
    total_things = 0
    total_attrs = 0
    zone_summaries = []

    for zone_file, spec in ps.zone_specs.items():
        n_rooms = len(spec.rooms)
        n_exits = sum(1 + (1 if ex.back_name else 0) for ex in spec.exits)
        n_things = len(spec.things)
        n_attrs = sum(len(r.attrs) for r in spec.rooms.values())

        total_rooms += n_rooms
        total_exits += n_exits
        total_things += n_things
        total_attrs += n_attrs

        zone_summaries.append({
            'file': zone_file,
            'name': spec.zone.name,
            'rooms': n_rooms,
            'exits': n_exits,
            'things': n_things,
            'attrs': n_attrs,
        })

    return {
        'total_rooms': total_rooms,
        'total_exits': total_exits,
        'total_things': total_things,
        'total_attrs': total_attrs,
        'zones': zone_summaries,
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='WorldBuilder Project — multi-zone management')
    parser.add_argument('action', choices=['check', 'plan', 'lint', 'compile'],
                        help='Action to perform')
    parser.add_argument('project', help='Path to project YAML file')
    parser.add_argument('--format', choices=['default', 'upload', 'raw'],
                        default='default', help='Output format for compile')

    args = parser.parse_args()
    project_path = Path(args.project)

    if not project_path.exists():
        print(f"Error: File not found: {project_path}", file=sys.stderr)
        return 1

    # Load project
    project, base_dir = load_project(project_path)
    ps = load_all_zones(project, base_dir)

    if not ps.zone_specs:
        print("Error: No zone specs loaded.", file=sys.stderr)
        return 1

    # ── check ──
    if args.action == 'check':
        print(f"Project: {project.name}")
        print(f"Zones: {len(ps.zone_specs)}")
        print()

        all_ok = True

        # Per-zone DRC
        for zone_file, drc in ps.zone_drcs.items():
            spec = ps.zone_specs[zone_file]
            status = "PASS" if drc.ok else "FAIL"
            print(f"  [{status}] {spec.zone.name} ({zone_file})")
            if not drc.ok:
                all_ok = False
                for err in drc.errors:
                    print(f"        ERROR: {err}")
            for warn in drc.warnings:
                print(f"        WARN:  {warn}")

        # Cross-reference validation
        print()
        xref_errors = validate_cross_refs(ps)
        if xref_errors:
            all_ok = False
            print("Cross-reference errors:")
            for err in xref_errors:
                print(f"  ERROR: {err}")
        else:
            print("Cross-references: OK")

        # Duplicate room ID detection
        print()
        room_id_zones = {}  # room_id -> [zone_files]
        for zone_file, spec in ps.zone_specs.items():
            for room_id in spec.rooms:
                room_id_zones.setdefault(room_id, []).append(zone_file)

        dupes = {rid: zones for rid, zones in room_id_zones.items() if len(zones) > 1}
        if dupes:
            print("Duplicate room IDs across zones:")
            for rid, zones in dupes.items():
                print(f"  WARN: '{rid}' in: {', '.join(zones)}")
        else:
            print("Room IDs: unique across all zones")

        # Stats
        print()
        stats = project_stats(ps)
        print(f"Totals: {stats['total_rooms']} rooms, {stats['total_exits']} exits, "
              f"{stats['total_things']} things, {stats['total_attrs']} attributes")

        return 0 if all_ok else 1

    # ── plan ──
    elif args.action == 'plan':
        print(f"WorldBuilder Project Plan — {project.name}")
        print("=" * (28 + len(project.name)))
        print()

        for zone_file, spec in ps.zone_specs.items():
            drc = ps.zone_drcs[zone_file]
            print(format_plan(spec, drc))
            print()

        stats = project_stats(ps)
        print(f"Project totals: {stats['total_rooms']} rooms, "
              f"{stats['total_exits']} exits, {stats['total_things']} things")

    # ── lint ──
    elif args.action == 'lint':
        all_ok = True
        for zone_file, spec in ps.zone_specs.items():
            lint_result = lint_spec(spec)
            if lint_result.ok and not lint_result.warnings:
                print(f"  [PASS] {spec.zone.name} ({zone_file})")
            else:
                status = "FAIL" if not lint_result.ok else "WARN"
                print(f"  [{status}] {spec.zone.name} ({zone_file})")
                if not lint_result.ok:
                    all_ok = False
                for loc, msg in lint_result.errors:
                    print(f"        ERROR [{loc}]: {msg}")
                for loc, msg in lint_result.warnings:
                    print(f"        WARN  [{loc}]: {msg}")

        if all_ok:
            print("\nSoftcode lint: All zones passed.")
        return 0 if all_ok else 1

    # ── compile ──
    elif args.action == 'compile':
        # Check all DRCs first
        for zone_file, drc in ps.zone_drcs.items():
            if not drc.ok:
                print(f"DRC errors in {zone_file}:", file=sys.stderr)
                for err in drc.errors:
                    print(f"  ERROR: {err}", file=sys.stderr)
                return 1

        # Compile in order
        order = project.build_order or list(ps.zone_specs.keys())
        for zone_file in order:
            spec = ps.zone_specs.get(zone_file)
            if not spec:
                continue
            print(f"# ============================================")
            print(f"# Zone: {spec.zone.name} ({zone_file})")
            print(f"# ============================================")
            commands = compile_spec(spec)
            print(format_commands(commands, args.format))
            print()

    return 0


if __name__ == '__main__':
    exit(main())
