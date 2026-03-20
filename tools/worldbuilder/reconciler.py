#!/usr/bin/env python3
"""Three-way reconciliation for WorldBuilder."""

from pathlib import Path
import yaml


class ReconcileEntry:
    def __init__(self, obj_id, obj_type, name, status, details=None, notes=None):
        self.obj_id = obj_id
        self.obj_type = obj_type
        self.name = name
        self.status = status
        self.details = details or []
        self.notes = notes or []


class ReconcileResult:
    def __init__(self, entries=None):
        self.entries = entries or []

    @property
    def summary(self):
        counts = {}
        for entry in self.entries:
            counts[entry.status] = counts.get(entry.status, 0) + 1
        return counts


def load_snapshot(path):
    """Load a normalized snapshot/state YAML file."""
    snapshot_path = Path(path)
    if not snapshot_path.exists():
        return {}
    with open(snapshot_path, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f) or {}
    return data.get('objects', data)


def build_spec_snapshot(spec):
    """Normalize a spec into a snapshot-shaped mapping."""
    objects = {}
    for room_id, room in spec.rooms.items():
        objects[room_id] = {
            'type': 'room',
            'name': room.name,
            'description': room.description.strip(),
            'flags': sorted(room.flags),
            'attrs': dict(room.attrs),
            'status': 'active',
        }
    for thing_id, thing in spec.things.items():
        objects[thing_id] = {
            'type': 'thing',
            'name': thing.name,
            'description': thing.description.strip(),
            'flags': sorted(thing.flags),
            'attrs': dict(thing.attrs),
            'location': thing.location,
            'status': 'active',
        }
    for ex in spec.exits:
        objects[f'exit_{ex.from_room}_{ex.to_room}'] = {
            'type': 'exit',
            'name': ex.name.split(';')[0],
            'from_room': ex.from_room,
            'to_room': ex.to_room,
            'status': 'active',
        }
        if ex.back_name:
            objects[f'exit_{ex.to_room}_{ex.from_room}'] = {
                'type': 'exit',
                'name': ex.back_name.split(';')[0],
                'from_room': ex.to_room,
                'to_room': ex.from_room,
                'status': 'active',
            }
    return objects


def _canon(value, field):
    if field == 'flags':
        return sorted(value or [])
    if field == 'attrs':
        return value or {}
    if isinstance(value, str):
        return value.strip()
    return value


def _collect_fields(spec_obj, state_obj, live_obj):
    fields = set()
    for obj in (spec_obj, state_obj, live_obj):
        if not obj:
            continue
        fields |= (set(obj.keys()) - {'dbref', 'objid', 'type', 'status'})
    return sorted(fields)


def reconcile_snapshots(spec_objects, state_objects, live_objects):
    entries = []
    all_ids = set(spec_objects) | set(state_objects) | set(live_objects)

    for obj_id in sorted(all_ids):
        spec_obj = spec_objects.get(obj_id)
        state_obj = state_objects.get(obj_id)
        live_obj = live_objects.get(obj_id)
        obj_type = (spec_obj or state_obj or live_obj or {}).get('type', 'object')
        name = (spec_obj or live_obj or state_obj or {}).get('name', obj_id)
        notes = []
        details = []

        if state_obj and state_obj.get('status') == 'pending_destroy':
            entries.append(ReconcileEntry(obj_id, obj_type, name, 'pending_destroy'))
            continue

        if state_obj and not live_obj:
            entries.append(ReconcileEntry(obj_id, obj_type, name, 'missing_live'))
            continue

        if state_obj and live_obj:
            stored_objid = state_obj.get('objid')
            live_objid = live_obj.get('objid')
            if stored_objid and live_objid and stored_objid != live_objid:
                entries.append(ReconcileEntry(obj_id, obj_type, name, 'recycled_live'))
                continue

        field_statuses = []
        for field in _collect_fields(spec_obj, state_obj, live_obj):
            spec_val = _canon((spec_obj or {}).get(field), field)
            state_val = _canon((state_obj or {}).get(field), field)
            live_val = _canon((live_obj or {}).get(field), field)

            if spec_val == state_val == live_val:
                status = 'unchanged'
            elif spec_val != state_val and live_val == state_val:
                status = 'spec_modified'
            elif live_val != state_val and spec_val == state_val:
                status = 'live_modified'
            elif spec_val == live_val and spec_val != state_val:
                status = 'spec_modified'
                notes.append(f'{field} already converged between spec and live')
            elif state_obj is None and spec_obj is not None and live_obj is None:
                status = 'spec_modified'
            elif state_obj is None and live_obj is not None and spec_obj is None:
                status = 'live_modified'
            else:
                status = 'conflict'

            field_statuses.append(status)
            if status != 'unchanged':
                details.append((field, status, state_val, spec_val, live_val))

        if 'conflict' in field_statuses:
            overall = 'conflict'
        elif 'live_modified' in field_statuses:
            overall = 'live_modified'
        elif 'spec_modified' in field_statuses:
            overall = 'spec_modified'
        else:
            overall = 'unchanged'

        entries.append(ReconcileEntry(obj_id, obj_type, name, overall, details, notes))

    return ReconcileResult(entries)


def format_reconcile_report(result):
    lines = ["WorldBuilder Reconcile", "======================", ""]
    if not result.entries:
        lines.append("No managed objects found.")
        return '\n'.join(lines)

    for entry in result.entries:
        lines.append(f"{entry.status.upper():<15} {entry.obj_type} \"{entry.name}\" ({entry.obj_id})")
        for field, status, state_val, spec_val, live_val in entry.details:
            lines.append(f"  {field}: {status}")
            lines.append(f"    state={state_val!r}")
            lines.append(f"    spec ={spec_val!r}")
            lines.append(f"    live ={live_val!r}")
        for note in entry.notes:
            lines.append(f"  note: {note}")
        lines.append("")

    summary = ", ".join(
        f"{count} {status}" for status, count in sorted(result.summary.items())
    )
    lines.append(f"Summary: {summary}")
    return '\n'.join(lines)
