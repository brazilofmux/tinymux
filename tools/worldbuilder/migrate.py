#!/usr/bin/env python3
"""WorldBuilder Migration System — numbered, ordered spec changes.

Migrations are YAML files in a directory, applied in filename order.
Each migration is a full or partial spec that gets compiled and applied.
The system tracks which migrations have been applied.

Usage:
    migrate.py status <migration_dir>              — show applied/pending
    migrate.py up <migration_dir> [--host ...]     — apply next pending
    migrate.py up-all <migration_dir> [--host ...] — apply all pending
    migrate.py down <migration_dir> [--host ...]   — rollback last applied
    migrate.py create <migration_dir> <name>       — create new migration file
"""

import sys
import os
import time
import yaml
import argparse
from pathlib import Path
from worldbuilder import parse_spec, check_drc, compile_spec, format_commands


# ---------------------------------------------------------------------------
# Migration State
# ---------------------------------------------------------------------------

class MigrationState:
    """Tracks which migrations have been applied."""

    def __init__(self, state_path):
        self.path = Path(state_path)
        self.applied = []  # list of {name, applied_at}
        if self.path.exists():
            with open(self.path, 'r') as f:
                data = yaml.safe_load(f)
            if data and 'applied' in data:
                self.applied = data['applied']

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.path, 'w') as f:
            yaml.dump({'applied': self.applied}, f,
                      default_flow_style=False, sort_keys=False)

    def is_applied(self, name):
        return any(m['name'] == name for m in self.applied)

    def mark_applied(self, name):
        self.applied.append({
            'name': name,
            'applied_at': time.strftime('%Y-%m-%dT%H:%M:%SZ'),
        })
        self.save()

    def mark_rolled_back(self, name):
        self.applied = [m for m in self.applied if m['name'] != name]
        self.save()

    def last_applied(self):
        return self.applied[-1]['name'] if self.applied else None


# ---------------------------------------------------------------------------
# Migration Discovery
# ---------------------------------------------------------------------------

def discover_migrations(migration_dir):
    """Find all migration YAML files in order.

    Files are sorted by name, so naming convention matters:
    001_initial.yaml, 002_add_park.yaml, 003_add_tavern.yaml
    """
    migration_dir = Path(migration_dir)
    if not migration_dir.is_dir():
        return []

    files = sorted(migration_dir.glob('*.yaml'))
    # Filter out state files and non-migration files
    migrations = []
    for f in files:
        if f.name.startswith('.') or f.name.startswith('_'):
            continue
        if 'state' in f.name.lower():
            continue
        migrations.append(f)

    return migrations


# ---------------------------------------------------------------------------
# Migration Application
# ---------------------------------------------------------------------------

def apply_migration(migration_path, dry_run=False, conn=None):
    """Parse, validate, and compile a migration spec.

    Returns (commands, spec) on success, or (None, error_string) on failure.
    """
    try:
        spec = parse_spec(migration_path)
    except Exception as e:
        return None, f"Parse error: {e}"

    drc = check_drc(spec)
    if not drc.ok:
        errors = '; '.join(drc.errors)
        return None, f"DRC errors: {errors}"

    commands = compile_spec(spec)
    return commands, spec


def create_migration(migration_dir, name):
    """Create a new numbered migration file."""
    migration_dir = Path(migration_dir)
    migration_dir.mkdir(parents=True, exist_ok=True)

    # Find next number
    existing = discover_migrations(migration_dir)
    if existing:
        last_name = existing[-1].stem
        # Extract leading number
        num_str = ''
        for ch in last_name:
            if ch.isdigit():
                num_str += ch
            else:
                break
        next_num = int(num_str) + 1 if num_str else 1
    else:
        next_num = 1

    # Sanitize name
    safe_name = name.lower().replace(' ', '_').replace('-', '_')
    filename = f"{next_num:03d}_{safe_name}.yaml"
    filepath = migration_dir / filename

    # Write template
    with open(filepath, 'w') as f:
        f.write(f"# Migration: {name}\n")
        f.write(f"# Created: {time.strftime('%Y-%m-%d %H:%M')}\n")
        f.write(f"#\n")
        f.write(f"# Describe what this migration does:\n")
        f.write(f"#   ...\n\n")
        f.write(f"zone:\n")
        f.write(f"  name: {name}\n\n")
        f.write(f"rooms:\n")
        f.write(f"  # room_id:\n")
        f.write(f"  #   name: Room Name\n")
        f.write(f"  #   description: |\n")
        f.write(f"  #     Room description here.\n\n")
        f.write(f"exits: []\n")

    return filepath


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='WorldBuilder Migration System')
    parser.add_argument('action', choices=['status', 'up', 'up-all', 'down', 'create'],
                        help='Action to perform')
    parser.add_argument('migration_dir', help='Directory containing migration files')
    parser.add_argument('name', nargs='?', help='Migration name (for create)')
    parser.add_argument('--state', help='State file (default: <dir>/.migration_state.yaml)')
    parser.add_argument('--dry-run', action='store_true', help='Show commands without executing')

    args = parser.parse_args()
    migration_dir = Path(args.migration_dir)

    state_path = args.state or str(migration_dir / '.migration_state.yaml')
    state = MigrationState(state_path)

    # ── create ──
    if args.action == 'create':
        if not args.name:
            print("Error: migration name required", file=sys.stderr)
            return 1
        filepath = create_migration(migration_dir, args.name)
        print(f"Created: {filepath}")
        return 0

    # ── status ──
    all_migrations = discover_migrations(migration_dir)

    if args.action == 'status':
        if not all_migrations:
            print(f"No migrations found in {migration_dir}")
            return 0

        print(f"Migrations in {migration_dir}:")
        print()
        for mf in all_migrations:
            applied = state.is_applied(mf.name)
            marker = "[x]" if applied else "[ ]"
            applied_info = ""
            if applied:
                for m in state.applied:
                    if m['name'] == mf.name:
                        applied_info = f"  (applied {m['applied_at']})"
                        break
            print(f"  {marker} {mf.name}{applied_info}")

        n_applied = sum(1 for mf in all_migrations if state.is_applied(mf.name))
        n_pending = len(all_migrations) - n_applied
        print(f"\n{n_applied} applied, {n_pending} pending")
        return 0

    # ── up ──
    elif args.action == 'up':
        pending = [mf for mf in all_migrations if not state.is_applied(mf.name)]
        if not pending:
            print("All migrations applied. Nothing to do.")
            return 0

        mf = pending[0]  # apply next one
        print(f"Applying: {mf.name}")
        commands, result = apply_migration(mf)
        if commands is None:
            print(f"  FAILED: {result}", file=sys.stderr)
            return 1

        if args.dry_run:
            print(format_commands(commands))
            print(f"\n[dry-run] Would mark {mf.name} as applied.")
        else:
            print(format_commands(commands))
            state.mark_applied(mf.name)
            print(f"\nMarked {mf.name} as applied.")
        return 0

    # ── up-all ──
    elif args.action == 'up-all':
        pending = [mf for mf in all_migrations if not state.is_applied(mf.name)]
        if not pending:
            print("All migrations applied. Nothing to do.")
            return 0

        print(f"Applying {len(pending)} pending migration(s)...")
        for mf in pending:
            print(f"\n--- {mf.name} ---")
            commands, result = apply_migration(mf)
            if commands is None:
                print(f"  FAILED: {result}", file=sys.stderr)
                return 1

            if args.dry_run:
                print(format_commands(commands))
            else:
                print(format_commands(commands))
                state.mark_applied(mf.name)
                print(f"  Applied.")

        if args.dry_run:
            print(f"\n[dry-run] Would mark {len(pending)} migration(s) as applied.")
        else:
            print(f"\n{len(pending)} migration(s) applied.")
        return 0

    # ── down ──
    elif args.action == 'down':
        last = state.last_applied()
        if not last:
            print("No migrations to roll back.")
            return 0

        print(f"Rolling back: {last}")
        if args.dry_run:
            print(f"[dry-run] Would mark {last} as rolled back.")
        else:
            state.mark_rolled_back(last)
            print(f"Marked {last} as rolled back.")
            print("Note: objects created by this migration still exist in the game.")
            print("Use 'executor.py rollback' with the zone spec to destroy them.")
        return 0


if __name__ == '__main__':
    exit(main())
