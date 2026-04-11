#!/usr/bin/env python3
"""Live adapter helpers for WorldBuilder.

This module owns:
- normalized managed-state persistence
- MUX command emission from semantic operations
- a few transport-adjacent helpers shared by executor/importer

Server-format contract
----------------------
The live executor and verifier interact with a running MUX over telnet.
Rather than parsing the free-form output of mutating commands like @dig,
@open, and @create — which is the server-version-specific text that the
tracker was warning about — we always round-trip queries through the
`think` softcode command wrapped in a pair of ASCII sentinels:

    think WB1[<expression>]WB2

`think` emits its evaluated argument verbatim on the executor's output
stream, so the response buffer will contain the literal bytes
`WB1<value>WB2`. `SENTINEL_PATTERN` extracts `<value>` regardless of
echoed commands, prompts, telnet IAC sequences, ANSI colour, or stale
lines already in the buffer. `lastcreate(me, <kind>)` gives a
deterministic handle on the most recently created object without any
reliance on @dig / @open / @create response text.

The only non-function softcode we still depend on is `%L` (current
location, via `@dig/teleport`), which is a MUX core invariant.
"""

from pathlib import Path
import re
import time
import yaml
import hashlib


DBREF_PATTERN = re.compile(r'#(-?\d+)')

# Sentinel-bracketed think response. WB1/WB2 chosen to be letters-only
# (no regex metacharacters, no ambiguity with softcode brackets) and
# extremely unlikely to appear in legitimate game output. The captured
# group tolerates negative dbrefs so callers can distinguish #-1
# (NOTHING) from a real result.
SENTINEL_PATTERN = re.compile(r'WB1(.*?)WB2', re.DOTALL)


def think_expr(expression):
    """Wrap a softcode expression in the sentinel-protected `think` form.

    The caller sends the returned string as a MUX command; the response
    can then be fed to `parse_think_response()` to recover the value.
    """
    return f'think WB1[{expression}]WB2'


def parse_think_response(text):
    """Extract the value emitted by a sentinel-wrapped `think` command.

    Returns the matched string (possibly empty) or None if no sentinel
    pair was found. Matches across newlines so the value survives
    wrapped output or telnet framing.

    Response buffers frequently contain both the server's echo of the
    command (which carries the literal `WB1[expr]WB2` bytes) and the
    evaluated `think` output (which carries `WB1<value>WB2` with the
    softcode brackets already stripped). We take the *last* match so
    we always land on the evaluated output rather than the echoed
    command.
    """
    if not text:
        return None
    matches = SENTINEL_PATTERN.findall(text)
    return matches[-1] if matches else None


def query_think(conn, expression, wait=0.5):
    """Send a sentinel-wrapped `think` query and return the value.

    `conn` must expose `send(text)` and `read_response(wait)` matching
    the executor's MuxConnection. Returns None if the sentinels were
    not seen (timeout, connection error, disabled softcode, etc.).
    """
    conn.send(think_expr(expression))
    time.sleep(0.3)
    resp = conn.read_response(wait)
    return parse_think_response(resp)


def query_last_created(conn, kind):
    """Return the dbref of the most recent object of `kind` created by us.

    `kind` is one of 'R' (room), 'E' (exit), 'T' (thing), 'P' (player).
    Returns a dbref string like '#42' on success, or None if nothing
    of that kind has been created this session or the query failed.
    """
    if kind not in ('R', 'E', 'T', 'P'):
        raise ValueError(f'invalid kind: {kind!r}')
    value = query_think(conn, f'lastcreate(me,{kind})')
    if not value:
        return None
    value = value.strip()
    match = DBREF_PATTERN.fullmatch(value)
    if not match:
        return None
    # Reject NOTHING / sentinel negatives.
    if int(match.group(1)) < 0:
        return None
    return value


def mux_escape(text):
    """Escape a multi-line string for MUX attribute/description setting.

    Handles:
    - Literal % in text → %% (prevent substitution)
    - \\n → %r (MUX linebreak)
    - \\t → %t (MUX tab)
    - Trailing whitespace stripped per line
    - Leading/trailing blank lines stripped
    """
    lines = text.rstrip().split('\n')
    while lines and not lines[0].strip():
        lines.pop(0)

    processed = []
    for line in lines:
        processed.append(line.rstrip().replace('%', '%%').replace('\t', '%t'))
    return '%r'.join(processed)


def mux_unescape(text):
    """Reverse MUX escaping back to plain text (for import)."""
    text = text.replace('%r', '\n')
    text = text.replace('%t', '\t')
    text = text.replace('%b', ' ')
    text = text.replace('%%', '%')
    return text


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


# ---------------------------------------------------------------------------
# Live Snapshot — query a running MUX to produce reconciler-compatible data
# ---------------------------------------------------------------------------

def query_live_object(conn, dbref, query_fn):
    """Query a single live object and return a normalized snapshot dict.

    conn: a MuxConnection (or any object with send/read_response)
    dbref: the dbref string, e.g. '#1234'
    query_fn: callable(conn, cmd) -> str  (send think command, return first line)

    Returns a dict suitable for reconcile_snapshots() live_objects, or None
    if the object doesn't exist.
    """
    objid = query_fn(conn, f'think [objid({dbref})]')
    if not objid or objid.startswith('#-1'):
        return None

    name = query_fn(conn, f'think [name({dbref})]')
    if not name:
        return None

    # Determine type
    type_flags = query_fn(conn, f'think [type({dbref})]')
    obj_type = (type_flags or '').strip().lower()
    if obj_type not in ('room', 'exit', 'thing', 'player'):
        obj_type = 'room'  # default fallback

    result = {
        'dbref': dbref,
        'objid': objid.strip(),
        'type': obj_type,
        'name': name,
        'status': 'active',
    }

    if obj_type in ('room', 'thing'):
        # Description
        desc = query_fn(conn, f'think [get({dbref}/DESCRIBE)]')
        if not desc:
            desc = query_fn(conn, f'think [get({dbref}/DESC)]')
        result['description'] = (desc or '').strip()

        # Flags — parse into sorted list
        raw_flags = query_fn(conn, f'think [flags({dbref})]')
        result['flags'] = sorted(raw_flags.split()) if raw_flags else []

        # Attributes via lattr + get
        raw_attrs = query_fn(conn, f'think [lattr({dbref})]')
        skip = {'DESCRIBE', 'DESC', 'IDESC', 'LAST', 'LASTSITE',
                'LASTIP', 'LASTPAGE', 'MAILCURF', 'MAILFLAGS',
                'STARTUP', 'CREATED', 'MODIFIED'}
        attrs = {}
        if raw_attrs and not raw_attrs.startswith('#-1'):
            for attr in raw_attrs.split():
                attr = attr.strip()
                if attr.upper() in skip:
                    continue
                val = query_fn(conn, f'think [get({dbref}/{attr})]')
                if val:
                    attrs[attr] = val
        result['attrs'] = attrs

    if obj_type == 'exit':
        dest = query_fn(conn, f'think [home({dbref})]')
        result['destination'] = dest or ''

    return result


def query_live_snapshot(conn, state, query_fn):
    """Query the live game for all objects tracked in state.

    Returns a dict of spec_id -> normalized object dict, suitable for
    passing as live_objects to reconcile_snapshots().

    conn: a MuxConnection
    state: a StateFile instance (provides .objects with dbrefs)
    query_fn: callable(conn, cmd) -> str
    """
    live_objects = {}
    for spec_id, obj in state.objects.items():
        dbref = obj.get('dbref')
        if not dbref or dbref.startswith('#DRY'):
            continue
        if obj.get('status') == 'pending_destroy':
            continue
        live_obj = query_live_object(conn, dbref, query_fn)
        if live_obj:
            live_objects[spec_id] = live_obj
    return live_objects


def normalize_imported_room(dbref, name, description, flags_str, attrs, objid):
    """Normalize raw importer data into a reconciler-compatible snapshot dict.

    flags_str: raw string from MUX flags() — split into sorted list.
    """
    flag_list = sorted(flags_str.split()) if isinstance(flags_str, str) else sorted(flags_str or [])
    return {
        'dbref': dbref,
        'objid': objid,
        'type': 'room',
        'name': name,
        'description': (description or '').strip(),
        'flags': flag_list,
        'attrs': attrs or {},
        'status': 'active',
    }


def normalize_imported_exit(dbref, name, from_dbref, to_dbref, objid=None):
    """Normalize raw importer exit data into a reconciler-compatible snapshot dict."""
    return {
        'dbref': dbref,
        'objid': objid,
        'type': 'exit',
        'name': name.split(';')[0] if name else '',
        'destination': to_dbref or '',
        'status': 'active',
    }


def emit_mux_commands(spec, operations):
    """Translate semantic operations into concrete MUX commands."""
    commands = []

    def escape_attr_value(value):
        return mux_escape(str(value))

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
                cmd(f"Set attribute {attr_name}", f'&{attr_name} here={escape_attr_value(attr_value)}')
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
                    f'&{attr_name} here={escape_attr_value(op.payload["attrs_to_set"][attr_name])}')
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
                cmd(f"Set {attr_name}", f'&{attr_name} {thing.name}={escape_attr_value(attr_value)}')
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
