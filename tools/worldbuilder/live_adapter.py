#!/usr/bin/env python3
"""Live adapter helpers for WorldBuilder.

This module owns:
- normalized managed-state persistence
- MUX command emission from semantic operations
- a few transport-adjacent helpers shared by executor/importer
"""

from pathlib import Path
import re
import time
import yaml
import hashlib


DBREF_PATTERN = re.compile(r'#(\d+)')


def mux_escape(text):
    """Escape multiline text for MUX command emission."""
    lines = text.rstrip().split('\n')
    while lines and not lines[0].strip():
        lines.pop(0)

    processed = []
    for line in lines:
        processed.append(line.rstrip().replace('%', '%%'))
    return '%r'.join(processed)


def content_hash(obj):
    """Compute a stable content hash for a room-like object."""
    h = hashlib.sha256()
    h.update(getattr(obj, 'name', '').encode('utf-8'))
    h.update(getattr(obj, 'description', '').encode('utf-8'))
    for key in sorted(getattr(obj, 'attrs', {}).keys()):
        h.update(f'{key}={obj.attrs[key]}'.encode('utf-8'))
    for flag in sorted(getattr(obj, 'flags', [])):
        h.update(flag.encode('utf-8'))
    return h.hexdigest()[:16]


def extract_dbref(text):
    """Extract the first dbref (#NNN) from MUX output."""
    match = DBREF_PATTERN.search(text)
    return match.group(0) if match else None


class StateFile:
    """Tracks managed objects and the last normalized snapshot we knew about."""

    def __init__(self, path):
        self.path = Path(path)
        self.objects = {}
        self.zone = None
        self.last_applied = None
        self.version = 1
        if self.path.exists():
            self._load()

    def _load(self):
        try:
            with open(self.path, 'r', encoding='utf-8') as f:
                data = yaml.safe_load(f)
            if data:
                self.objects = data.get('objects', {})
                self.zone = data.get('zone')
                self.last_applied = data.get('last_applied') or data.get('last_synced')
                self.version = data.get('state_version', 1)
        except Exception as exc:
            print(f"Warning: Could not load state file {self.path}: {exc}")
            self.objects = {}

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        data = {
            'state_version': 1,
            'zone': self.zone,
            'last_applied': time.strftime('%Y-%m-%dT%H:%M:%SZ'),
            'objects': self.objects,
        }
        with open(self.path, 'w', encoding='utf-8') as f:
            yaml.dump(data, f, default_flow_style=False, sort_keys=False)

    def get_dbref(self, spec_id):
        obj = self.objects.get(spec_id)
        return obj.get('dbref') if obj else None

    def set_room(self, spec_id, dbref, room, objid=None, status='active'):
        self.objects[spec_id] = {
            'dbref': dbref,
            'objid': objid,
            'type': 'room',
            'name': room.name,
            'description': room.description,
            'flags': sorted(room.flags),
            'attrs': room.attrs,
            'content_hash': content_hash(room),
            'status': status,
        }

    def set_exit(self, spec_id, dbref, name, objid=None, status='active'):
        self.objects[spec_id] = {
            'dbref': dbref,
            'objid': objid,
            'type': 'exit',
            'name': name,
            'status': status,
        }

    def set_thing(self, spec_id, dbref, thing, objid=None, status='active'):
        self.objects[spec_id] = {
            'dbref': dbref,
            'objid': objid,
            'type': 'thing',
            'name': thing.name,
            'description': thing.description,
            'flags': sorted(thing.flags),
            'attrs': thing.attrs,
            'location': thing.location,
            'content_hash': content_hash(thing),
            'status': status,
        }

    def mark_pending_destroy(self, spec_id):
        if spec_id in self.objects:
            self.objects[spec_id]['status'] = 'pending_destroy'


def emit_mux_commands(spec, operations):
    """Translate semantic operations into concrete MUX commands."""
    commands = []

    def cmd(comment, command):
        commands.append((comment, command))

    def emit_exit_props(exit_obj, exit_ref):
        if exit_obj.lock:
            cmd("Lock", f'@lock {exit_ref}={exit_obj.lock}')
        if exit_obj.desc:
            cmd("Desc", f'@desc {exit_ref}={mux_escape(exit_obj.desc)}')
        if exit_obj.succ:
            cmd("Succ", f'@succ {exit_ref}={exit_obj.succ}')
        if exit_obj.osucc:
            cmd("Osucc", f'@osucc {exit_ref}={exit_obj.osucc}')
        if exit_obj.fail:
            cmd("Fail", f'@fail {exit_ref}={exit_obj.fail}')
        if exit_obj.ofail:
            cmd("Ofail", f'@ofail {exit_ref}={exit_obj.ofail}')
        if exit_obj.drop:
            cmd("Drop", f'@drop {exit_ref}={exit_obj.drop}')
        if exit_obj.odrop:
            cmd("Odrop", f'@odrop {exit_ref}={exit_obj.odrop}')
        for flag in exit_obj.flags:
            cmd(f"Flag {flag}", f'@set {exit_ref}={flag}')

    for op in operations:
        if op.kind == 'create_room':
            room = op.payload['room']
            cmd(f"--- Room: {room.name} ({op.obj_id}) ---", "")
            cmd("Create room", f'@dig/teleport {room.name}')
            cmd("Store dbref", f'think [setq(0, %L)] Room {op.obj_id} = %q0')
            cmd("Set description", f'@desc here={mux_escape(room.description)}')
            for flag in room.flags:
                cmd(f"Set flag {flag}", f'@set here={flag}')
            for attr_name, attr_value in room.attrs.items():
                cmd(f"Set attribute {attr_name}", f'&{attr_name} here={attr_value}')
            if room.parent:
                cmd("Set parent", f'@parent here={room.parent}')
            cmd("Set zone", f'@chzone here=%{{zone:{spec.zone.name}}}')
            cmd("", "")
        elif op.kind == 'update_room':
            room = op.payload['room']
            dbref = op.payload['dbref']
            cmd(f"--- UPDATE room: {room.name} ({op.obj_id}) at {dbref} ---", "")
            cmd("Teleport to room", f'@teleport me={dbref}')
            if op.payload['name_changed']:
                cmd("Update name", f'@name here={room.name}')
            if op.payload['desc_changed']:
                cmd("Update description", f'@desc here={mux_escape(room.description)}')
            for flag in sorted(op.payload['flags_added']):
                cmd(f"Set flag {flag}", f'@set here={flag}')
            for flag in sorted(op.payload['flags_removed']):
                cmd(f"Clear flag {flag}", f'@set here=!{flag}')
            for attr_name in sorted(op.payload['attrs_to_set']):
                cmd(f"Update {attr_name}",
                    f'&{attr_name} here={op.payload["attrs_to_set"][attr_name]}')
            for attr_name in sorted(op.payload['attrs_to_unset']):
                cmd(f"Remove {attr_name}", f'&{attr_name} here=')
        elif op.kind == 'create_exit':
            ex = op.payload['exit']
            if op.payload['direction'] == 'forward':
                cmd(f"Exit: {op.name} ({ex.from_room} -> {ex.to_room})",
                    f'@open {ex.name}=%{{room:{ex.to_room}}}')
                emit_exit_props(ex, op.name)
            else:
                cmd(f"Back exit: {op.name} ({ex.to_room} -> {ex.from_room})",
                    f'@open {ex.back_name}=%{{room:{ex.from_room}}}')
        elif op.kind == 'create_thing':
            thing = op.payload['thing']
            cmd(f"--- Thing: {thing.name} ({op.obj_id}) in {thing.location} ---", "")
            cmd("Go to room", f'@teleport me=%{{room:{thing.location}}}')
            cmd("Create object", f'@create {thing.name}')
            if thing.description:
                cmd("Set description", f'@desc {thing.name}={mux_escape(thing.description)}')
            for flag in thing.flags:
                cmd(f"Set flag {flag}", f'@set {thing.name}={flag}')
            for attr_name, attr_value in thing.attrs.items():
                cmd(f"Set {attr_name}", f'&{attr_name} {thing.name}={attr_value}')
            if thing.parent:
                cmd("Set parent", f'@parent {thing.name}={thing.parent}')
        elif op.kind == 'destroy_room':
            cmd("=== DESTROY removed rooms ===", "")
            cmd(f"Destroy room: {op.name} ({op.obj_id})",
                f'@destroy/override {op.payload["dbref"]}')
        elif op.kind == 'destroy_exit':
            cmd("=== DESTROY removed exits ===", "")
            cmd(f"Destroy exit: {op.name} ({op.obj_id})",
                f'@destroy {op.payload["dbref"]}')

    return commands
