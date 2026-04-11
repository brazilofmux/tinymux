#!/usr/bin/env python3
"""WorldBuilder Executor — connects to a MUX and applies compiled commands.

Handles:
- Telnet connection with login
- Sequential command execution with output capture
- Dbref capture via lastcreate() + sentinel-wrapped `think` queries,
  which avoids parsing the version-specific free-form @dig/@open/
  @create response text (see live_adapter.py's module docstring for
  the server-format contract this relies on)
- State file generation (spec ID → dbref mapping)
- Dry-run mode (log commands without executing)
"""

import socket
import ssl
import time
import argparse
from pathlib import Path
from worldbuilder import parse_spec, check_drc, plan_spec_operations, plan_incremental_operations
from live_adapter import (StateFile, content_hash,
                          query_live_snapshot, query_last_created,
                          query_think, mux_escape)


# ---------------------------------------------------------------------------
# Telnet Client (minimal, synchronous)
# ---------------------------------------------------------------------------

class MuxConnection:
    """Simple telnet connection to a MUX server."""

    IAC  = 255
    DONT = 254
    DO   = 253
    WONT = 252
    WILL = 251
    SB   = 250
    SE   = 240

    def __init__(self, host, port, use_ssl=False, timeout=10, verify_ssl=True):
        self.host = host
        self.port = int(port)
        self.use_ssl = use_ssl
        self.timeout = timeout
        self.verify_ssl = verify_ssl
        self.sock = None
        self.buffer = b''

    def connect(self):
        raw = socket.create_connection((self.host, self.port), self.timeout)
        if self.use_ssl:
            ctx = ssl.create_default_context()
            if not self.verify_ssl:
                ctx.check_hostname = False
                ctx.verify_mode = ssl.CERT_NONE
            self.sock = ctx.wrap_socket(raw, server_hostname=self.host)
        else:
            self.sock = raw
        self.sock.settimeout(self.timeout)
        # Drain initial welcome text
        self._read_until_quiet(2.0)

    def login(self, character, password):
        self.send(f'connect {character} {password}')
        time.sleep(1.0)
        return self._read_until_quiet(2.0)

    def send(self, text):
        data = (text + '\r\n').encode('utf-8')
        self.sock.sendall(data)

    def read_response(self, wait=0.5):
        """Read all available data, waiting up to `wait` seconds for quiet."""
        return self._read_until_quiet(wait)

    def _read_until_quiet(self, quiet_seconds=0.5):
        """Read until no data arrives for quiet_seconds."""
        result = b''
        self.sock.settimeout(quiet_seconds)
        while True:
            try:
                chunk = self.sock.recv(4096)
                if not chunk:
                    break
                result += chunk
            except socket.timeout:
                break
        self.sock.settimeout(self.timeout)
        # Strip telnet IAC sequences
        return self._strip_telnet(result).decode('utf-8', errors='replace')

    def _strip_telnet(self, data):
        """Remove telnet IAC sequences from raw data."""
        out = bytearray()
        i = 0
        while i < len(data):
            if data[i] == self.IAC and i + 1 < len(data):
                cmd = data[i + 1]
                if cmd in (self.WILL, self.WONT, self.DO, self.DONT):
                    # Respond DONT/WONT to everything
                    opt = data[i + 2] if i + 2 < len(data) else 0
                    if cmd == self.WILL:
                        self.sock.sendall(bytes([self.IAC, self.DONT, opt]))
                    elif cmd == self.DO:
                        self.sock.sendall(bytes([self.IAC, self.WONT, opt]))
                    i += 3
                elif cmd == self.SB:
                    # Skip until IAC SE
                    j = i + 2
                    while j < len(data) - 1:
                        if data[j] == self.IAC and data[j + 1] == self.SE:
                            j += 2
                            break
                        j += 1
                    i = j
                elif cmd == self.IAC:
                    out.append(self.IAC)
                    i += 2
                else:
                    i += 2  # skip 2-byte command
            else:
                out.append(data[i])
                i += 1
        return bytes(out)

    def disconnect(self):
        if self.sock:
            try:
                self.send('QUIT')
            except Exception:
                pass
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None


# ---------------------------------------------------------------------------
# Executor
# ---------------------------------------------------------------------------


def execute(spec, conn, state, dry_run=False, log_file=None, operations=None):
    """Execute semantic operations against a live MUX connection.

    If operations is None, plans them from the spec and state.
    Returns True on success.
    """
    def log(msg):
        print(msg)
        if log_file:
            log_file.write(msg + '\n')

    def escape_attr_value(value):
        return mux_escape(str(value))

    def do_cmd(cmd):
        """Send a command and return response. In dry-run, just log."""
        if dry_run:
            log(f'  [dry-run] {cmd}')
            return ''
        log(f'  > {cmd}')
        conn.send(cmd)
        time.sleep(0.3)
        resp = conn.read_response(0.5)
        if resp.strip():
            for line in resp.strip().split('\n'):
                log(f'  < {line.rstrip()}')
        return resp

    def capture_objid(dbref):
        """Query objid for a dbref. Returns stripped string or None."""
        if dry_run:
            return None
        resp = do_cmd(f'think [objid({dbref})]')
        return resp.strip() if resp else None

    state.zone = spec.zone.name

    # Plan operations if not provided
    if operations is None:
        state_path = state.path
        if state_path.exists() and state.objects:
            operations = plan_incremental_operations(spec, str(state_path))
        else:
            operations = plan_spec_operations(spec)

    # Separate into phases: rooms first, then exits/things, then destroys
    room_creates = [op for op in operations if op.kind == 'create_room']
    room_updates = [op for op in operations if op.kind == 'update_room']
    exit_creates = [op for op in operations if op.kind == 'create_exit']
    thing_creates = [op for op in operations if op.kind == 'create_thing']
    destroy_exits = [op for op in operations if op.kind == 'destroy_exit']
    destroy_rooms = [op for op in operations if op.kind == 'destroy_room']

    # Phase 1: Create rooms and capture dbrefs
    if room_creates or room_updates:
        log(f'\n=== Phase 1: Rooms ({len(room_creates)} create, {len(room_updates)} update) ===')

    for op in room_creates:
        room = op.payload['room']
        log(f'\n--- Room: {room.name} ({op.obj_id}) — creating ---')
        do_cmd(f'@dig/teleport {room.name}')

        if not dry_run:
            time.sleep(0.2)
            # Prefer lastcreate(me,R) — deterministic, version-stable.
            # Fall back to %L (current location, set by @dig/teleport)
            # if lastcreate comes back empty for any reason.
            dbref = query_last_created(conn, 'R')
            if not dbref:
                dbref = query_think(conn, '%L')
                if dbref and not dbref.startswith('#'):
                    dbref = None
            if dbref:
                objid = capture_objid(dbref)
                state.set_room(op.obj_id, dbref, room, objid=objid)
                log(f'  [state] {op.obj_id} = {dbref} (objid: {objid})')
            else:
                log(f'  [WARNING] Could not capture dbref for {op.obj_id}')
        else:
            state.objects[op.obj_id] = {
                'dbref': f'#DRY_{op.obj_id}',
                'type': 'room',
                'name': room.name,
                'description': room.description,
                'flags': sorted(room.flags),
                'attrs': room.attrs,
                'content_hash': content_hash(room),
            }

        do_cmd(f'@desc here={mux_escape(room.description)}')
        for flag in room.flags:
            do_cmd(f'@set here={flag}')
        for attr_name, attr_value in room.attrs.items():
            do_cmd(f'&{attr_name} here={escape_attr_value(attr_value)}')
        if room.parent:
            do_cmd(f'@parent here={room.parent}')

    for op in room_updates:
        room = op.payload['room']
        dbref = op.payload['dbref']
        log(f'\n--- Room: {room.name} ({op.obj_id}) — updating at {dbref} ---')
        do_cmd(f'@teleport me={dbref}')
        time.sleep(0.2)

        if op.payload.get('name_changed'):
            do_cmd(f'@name here={room.name}')
        if op.payload.get('desc_changed'):
            do_cmd(f'@desc here={mux_escape(room.description)}')
        for flag in sorted(op.payload.get('flags_added', set())):
            do_cmd(f'@set here={flag}')
        for flag in sorted(op.payload.get('flags_removed', set())):
            do_cmd(f'@set here=!{flag}')
        for attr_name in sorted(op.payload.get('attrs_to_set', {})):
            do_cmd(f'&{attr_name} here={escape_attr_value(op.payload["attrs_to_set"][attr_name])}')
        for attr_name in sorted(op.payload.get('attrs_to_unset', set())):
            do_cmd(f'&{attr_name} here=')

        if not dry_run:
            objid = capture_objid(dbref)
            state.set_room(op.obj_id, dbref, room, objid=objid)

    # Phase 2: Create exits
    if exit_creates:
        log(f'\n=== Phase 2: Exits ({len(exit_creates)} create) ===')

    for op in exit_creates:
        ex = op.payload['exit']
        direction = op.payload['direction']

        if direction == 'forward':
            from_dbref = state.get_dbref(ex.from_room)
            to_dbref = state.get_dbref(ex.to_room)
            if not from_dbref:
                log(f'  [ERROR] No dbref for source room: {ex.from_room}')
                continue
            if not to_dbref:
                if ex.to_room.startswith('$'):
                    log(f'  [SKIP] External reference: {ex.to_room}')
                    continue
                log(f'  [ERROR] No dbref for destination room: {ex.to_room}')
                continue

            do_cmd(f'@teleport me={from_dbref}')
            time.sleep(0.2)
            log(f'\n--- Exit: {op.name} ({ex.from_room} -> {ex.to_room}) ---')
            do_cmd(f'@open {ex.name}={to_dbref}')

            if not dry_run:
                dbref = query_last_created(conn, 'E')
                if dbref:
                    objid = capture_objid(dbref)
                    state.set_exit(op.obj_id, dbref, op.name, objid=objid)
                else:
                    log(f'  [WARNING] Could not capture dbref for exit {op.obj_id}')
        else:
            # back exit
            from_dbref = state.get_dbref(ex.to_room)
            to_dbref = state.get_dbref(ex.from_room)
            if not from_dbref or not to_dbref:
                log(f'  [ERROR] Missing dbref for back exit: {ex.to_room} -> {ex.from_room}')
                continue

            do_cmd(f'@teleport me={from_dbref}')
            time.sleep(0.2)
            log(f'\n--- Back exit: {op.name} ({ex.to_room} -> {ex.from_room}) ---')
            do_cmd(f'@open {ex.back_name}={to_dbref}')

            if not dry_run:
                dbref = query_last_created(conn, 'E')
                if dbref:
                    objid = capture_objid(dbref)
                    state.set_exit(op.obj_id, dbref, op.name, objid=objid)
                else:
                    log(f'  [WARNING] Could not capture dbref for back exit {op.obj_id}')

    # Phase 3: Create things
    if thing_creates:
        log(f'\n=== Phase 3: Things ({len(thing_creates)} create) ===')

    for op in thing_creates:
        thing = op.payload['thing']
        loc_dbref = state.get_dbref(thing.location)
        if not loc_dbref:
            log(f'  [ERROR] No dbref for location: {thing.location}')
            continue

        do_cmd(f'@teleport me={loc_dbref}')
        time.sleep(0.2)
        log(f'\n--- Thing: {thing.name} ({op.obj_id}) in {thing.location} ---')
        do_cmd(f'@create {thing.name}')

        # Capture the dbref of the thing we just created. The
        # remaining @desc/@set/& commands still target the thing by
        # name because that matches MUX's normal authoring flow, but
        # the dbref + objid are what the state file needs for
        # subsequent reconciler passes.
        thing_dbref = None
        if not dry_run:
            thing_dbref = query_last_created(conn, 'T')

        if thing.description:
            do_cmd(f'@desc {thing.name}={mux_escape(thing.description)}')
        for flag in thing.flags:
            do_cmd(f'@set {thing.name}={flag}')
        for attr_name, attr_value in thing.attrs.items():
            do_cmd(f'&{attr_name} {thing.name}={escape_attr_value(attr_value)}')
        if thing.parent:
            do_cmd(f'@parent {thing.name}={thing.parent}')

        if thing_dbref:
            objid = capture_objid(thing_dbref)
            state.set_thing(op.obj_id, thing_dbref, thing, objid=objid)
            log(f'  [state] {op.obj_id} = {thing_dbref} (objid: {objid})')
        elif not dry_run:
            log(f'  [WARNING] Could not capture dbref for thing {op.obj_id}')

    # Phase 4: Destroy (exits before rooms)
    if destroy_exits or destroy_rooms:
        log(f'\n=== Phase 4: Destroy ({len(destroy_exits)} exits, {len(destroy_rooms)} rooms) ===')

    for op in destroy_exits:
        dbref = op.payload['dbref']
        log(f'\n--- Destroy exit: {op.name} ({op.obj_id}) at {dbref} ---')
        do_cmd(f'@destroy {dbref}')
        state.mark_pending_destroy(op.obj_id)

    for op in destroy_rooms:
        dbref = op.payload['dbref']
        log(f'\n--- Destroy room: {op.name} ({op.obj_id}) at {dbref} ---')
        do_cmd(f'@destroy/override {dbref}')
        state.mark_pending_destroy(op.obj_id)

    # Save state
    state.save()
    log(f'\n=== Done. State saved to {state.path} ===')
    return True


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Verify — check live game matches spec
# ---------------------------------------------------------------------------

def verify(spec, conn, state, log_file=None):
    """Verify that a live game matches the spec. Returns list of discrepancies."""
    issues = []

    def log(msg):
        print(msg)
        if log_file:
            log_file.write(msg + '\n')

    def query_one(cmd):
        conn.send(cmd)
        time.sleep(0.3)
        resp = conn.read_response(0.3)
        lines = [l.strip() for l in resp.strip().split('\n') if l.strip()]
        return lines[0] if lines else ''

    def check_object(spec_id, obj_name, dbref, expected_desc, expected_attrs,
                     expected_flags=None, expected_location=None):
        stored_objid = state.objects.get(spec_id, {}).get('objid')
        if stored_objid:
            live_objid = query_one(f'think [objid({dbref})]')
            if live_objid and live_objid != stored_objid:
                issues.append(f"{spec_id!r} ({dbref}): objid mismatch — stored=\"{stored_objid}\" live=\"{live_objid}\" (dbref was recycled!)")
                return False
            if not live_objid or live_objid.startswith('#-1'):
                issues.append(f"{spec_id!r} ({dbref}): object no longer exists")
                return False

        live_name = query_one(f'think [name({dbref})]')
        if live_name and live_name != obj_name:
            issues.append(f"{spec_id!r} ({dbref}): name mismatch — spec=\"{obj_name}\" live=\"{live_name}\"")

        live_desc = query_one(f'think [get({dbref}/DESCRIBE)]')
        if not live_desc:
            live_desc = query_one(f'think [get({dbref}/DESC)]')
        if not live_desc and expected_desc.strip():
            issues.append(f"{spec_id!r} ({dbref}): description missing on live server")
        elif live_desc != expected_desc:
            issues.append(f"{spec_id!r} ({dbref}): description mismatch")

        for attr_name, expected_val in expected_attrs.items():
            live_val = query_one(f'think [get({dbref}/{attr_name})]')
            if live_val != expected_val:
                issues.append(f"{spec_id!r} ({dbref}): attr {attr_name} mismatch — spec=\"{expected_val}\" live=\"{live_val}\"")

        if expected_flags is not None:
            live_flags = query_one(f'think [flags({dbref})]')
            live_flag_set = set(live_flags.split()) if live_flags else set()
            expected_flag_set = set(expected_flags)
            if live_flag_set != expected_flag_set:
                issues.append(f"{spec_id!r} ({dbref}): flags mismatch — spec=\"{' '.join(sorted(expected_flag_set))}\" live=\"{' '.join(sorted(live_flag_set))}\"")

        if expected_location is not None:
            live_location = query_one(f'think [loc({dbref})]')
            if live_location != expected_location:
                issues.append(f"{spec_id!r} ({dbref}): location mismatch — spec=\"{expected_location}\" live=\"{live_location}\"")

        return True

    log(f"Verifying {len(spec.rooms)} rooms...")

    for room_id, room in spec.rooms.items():
        dbref = state.get_dbref(room_id)
        if not dbref:
            issues.append(f"Room '{room_id}' ({room.name}): not in state file (never applied?)")
            continue

        check_object(
            room_id,
            room.name,
            dbref,
            room.description,
            room.attrs,
            expected_flags=room.flags,
        )

        log(f"  [{dbref}] {room.name} — {'OK' if not any(room_id in i for i in issues) else 'MISMATCH'}")

    # Check exits exist
    log(f"Verifying exits...")
    for ex in spec.exits:
        to_dbref = state.get_dbref(ex.to_room)
        forward_id = f'exit_{ex.from_room}_{ex.to_room}'
        forward_state = state.objects.get(forward_id)
        if not forward_state:
            issues.append(f"Exit '{forward_id}' ({ex.name}): not in state file (never applied?)")
        elif not to_dbref:
            continue
        else:
            exit_dbref = forward_state.get('dbref')
            live_objid = query_one(f'think [objid({exit_dbref})]')
            stored_objid = forward_state.get('objid')
            if stored_objid and live_objid and live_objid != stored_objid:
                issues.append(f"Exit '{forward_id}' ({exit_dbref}): objid mismatch — stored=\"{stored_objid}\" live=\"{live_objid}\" (dbref was recycled!)")
            elif not live_objid or live_objid.startswith('#-1'):
                issues.append(f"Exit '{forward_id}' ({exit_dbref}): object no longer exists")
            else:
                live_name = query_one(f'think [name({exit_dbref})]')
                expected_name = ex.name.split(';')[0]
                if live_name and live_name != expected_name:
                    issues.append(f"Exit '{forward_id}' ({exit_dbref}): name mismatch — spec=\"{expected_name}\" live=\"{live_name}\"")
                live_dest = query_one(f'think [home({exit_dbref})]')
                if live_dest != to_dbref:
                    issues.append(f"Exit '{forward_id}' ({exit_dbref}): destination mismatch — spec=\"{to_dbref}\" live=\"{live_dest}\"")

        if ex.back_name:
            back_id = f'exit_{ex.to_room}_{ex.from_room}'
            back_state = state.objects.get(back_id)
            from_dbref = state.get_dbref(ex.from_room)
            if not back_state:
                issues.append(f"Exit '{back_id}' ({ex.back_name}): not in state file (never applied?)")
            elif not from_dbref:
                continue
            else:
                exit_dbref = back_state.get('dbref')
                live_objid = query_one(f'think [objid({exit_dbref})]')
                stored_objid = back_state.get('objid')
                if stored_objid and live_objid and live_objid != stored_objid:
                    issues.append(f"Exit '{back_id}' ({exit_dbref}): objid mismatch — stored=\"{stored_objid}\" live=\"{live_objid}\" (dbref was recycled!)")
                elif not live_objid or live_objid.startswith('#-1'):
                    issues.append(f"Exit '{back_id}' ({exit_dbref}): object no longer exists")
                else:
                    live_name = query_one(f'think [name({exit_dbref})]')
                    expected_name = ex.back_name.split(';')[0]
                    if live_name and live_name != expected_name:
                        issues.append(f"Exit '{back_id}' ({exit_dbref}): name mismatch — spec=\"{expected_name}\" live=\"{live_name}\"")
                    live_dest = query_one(f'think [home({exit_dbref})]')
                    if live_dest != from_dbref:
                        issues.append(f"Exit '{back_id}' ({exit_dbref}): destination mismatch — spec=\"{from_dbref}\" live=\"{live_dest}\"")

    log(f"Verifying things...")
    for thing_id, thing in spec.things.items():
        dbref = state.get_dbref(thing_id)
        if not dbref:
            issues.append(f"Thing '{thing_id}' ({thing.name}): not in state file (never applied?)")
            continue

        expected_location = state.get_dbref(thing.location)
        check_object(
            thing_id,
            thing.name,
            dbref,
            thing.description,
            thing.attrs,
            expected_flags=thing.flags,
            expected_location=expected_location,
        )

    if not issues:
        log("\nVerification passed — all rooms match spec.")
    else:
        log(f"\nVerification found {len(issues)} issue(s):")
        for issue in issues:
            log(f"  - {issue}")

    return issues


# ---------------------------------------------------------------------------
# Rollback — destroy objects in reverse order
# ---------------------------------------------------------------------------

def rollback(spec, conn, state, dry_run=False, log_file=None):
    """Destroy all objects created by a prior apply, in reverse order."""
    issues = []

    def log(msg):
        print(msg)
        if log_file:
            log_file.write(msg + '\n')

    def do_cmd(cmd):
        if dry_run:
            log(f"  [dry-run] {cmd}")
            return ''
        log(f"  > {cmd}")
        conn.send(cmd)
        time.sleep(0.3)
        resp = conn.read_response(0.3)
        if resp.strip():
            for line in resp.strip().split('\n'):
                log(f"  < {line.rstrip()}")
        return resp

    # Collect all dbrefs from state, exits first then rooms
    exits_to_destroy = []
    rooms_to_destroy = []
    skipped = []
    for spec_id, obj in state.objects.items():
        dbref = obj.get('dbref', '')
        if not dbref or dbref.startswith('#DRY'):
            continue
        if obj.get('type') == 'exit':
            exits_to_destroy.append((spec_id, dbref, obj.get('name', ''), obj.get('objid')))
        elif obj.get('type') == 'room':
            rooms_to_destroy.append((spec_id, dbref, obj.get('name', ''), obj.get('objid')))

    if not exits_to_destroy and not rooms_to_destroy:
        log("Nothing to rollback — state is empty.")
        return

    log(f"Rolling back: {len(exits_to_destroy)} exits + {len(rooms_to_destroy)} rooms")

    def verify_objid(dbref, stored_objid, spec_id, name):
        """Verify objid before destroying. Returns True if safe to destroy."""
        if not stored_objid:
            return True  # No objid stored (legacy state), proceed on dbref alone
        if dry_run:
            return True
        live_objid = do_cmd(f'think [objid({dbref})]')
        live_objid = live_objid.strip() if live_objid else ''
        if live_objid and live_objid != stored_objid:
            log(f"  [SKIP] {name} ({dbref}): objid mismatch — stored={stored_objid} live={live_objid} (dbref recycled, not ours!)")
            skipped.append(spec_id)
            return False
        return True

    # Destroy exits first
    for spec_id, dbref, name, objid in exits_to_destroy:
        if not verify_objid(dbref, objid, spec_id, name):
            continue
        log(f"\n--- Destroy exit: {name} ({dbref}) ---")
        do_cmd(f'@destroy {dbref}')

    # Then rooms
    for spec_id, dbref, name, objid in rooms_to_destroy:
        if not verify_objid(dbref, objid, spec_id, name):
            continue
        log(f"\n--- Destroy room: {name} ({dbref}) ---")
        do_cmd(f'@destroy/override {dbref}')

    # Destruction is deferred in TinyMUX, so keep entries and mark them
    # pending_destroy until a later reconcile/verify confirms removal.
    if not dry_run:
        for spec_id, _, _, _ in exits_to_destroy:
            if spec_id not in skipped:
                state.mark_pending_destroy(spec_id)
        for spec_id, _, _, _ in rooms_to_destroy:
            if spec_id not in skipped:
                state.mark_pending_destroy(spec_id)
        state.save()
        log(f"\nState updated with pending_destroy markers. Saved to {state.path}")
    else:
        log(f"\n[dry-run] State would be updated with pending_destroy markers.")


def main():
    parser = argparse.ArgumentParser(
        description='WorldBuilder Executor — apply specs to a live MUX')
    parser.add_argument('action', nargs='?', default='apply',
                        choices=['apply', 'verify', 'rollback'],
                        help='Action (default: apply)')
    parser.add_argument('spec', help='Path to YAML spec file')
    parser.add_argument('--host', required=True, help='MUX hostname')
    parser.add_argument('--port', default='4201', help='MUX port')
    parser.add_argument('--ssl', action='store_true', help='Use SSL/TLS')
    parser.add_argument('--insecure', action='store_true',
                        help='Disable TLS certificate and hostname verification')
    parser.add_argument('--character', required=True, help='Builder character name')
    parser.add_argument('--password', required=True, help='Builder password')
    parser.add_argument('--state', help='State file path')
    parser.add_argument('--dry-run', action='store_true', help='Log commands without executing')
    parser.add_argument('--log', help='Log file path')

    args = parser.parse_args()
    spec_path = Path(args.spec)

    # Parse and validate
    spec = parse_spec(spec_path)
    drc = check_drc(spec)
    if not drc.ok:
        print("DRC errors — cannot apply:")
        for err in drc.errors:
            print(f"  ERROR: {err}")
        return 1

    # State file
    state_path = args.state or f'.worldbuilder/{spec.zone.name.lower().replace(" ", "_")}.state.yaml'
    state = StateFile(state_path)

    # Log file
    log_file = open(args.log, 'w') if args.log else None

    action = args.action

    try:
        if args.dry_run and action == 'apply':
            print(f"Dry run — no commands will be sent to {args.host}:{args.port}")
            execute(spec, None, state, dry_run=True, log_file=log_file)
        elif args.dry_run and action == 'rollback':
            print(f"Dry run rollback — no commands will be sent")
            rollback(spec, None, state, dry_run=True, log_file=log_file)
        else:
            # Connect
            print(f"Connecting to {args.host}:{args.port}...")
            conn = MuxConnection(args.host, int(args.port), args.ssl,
                                 verify_ssl=not args.insecure)
            conn.connect()
            print("Connected. Logging in...")

            resp = conn.login(args.character, args.password)
            if 'Invalid' in resp or 'incorrect' in resp.lower():
                print(f"Login failed: {resp.strip()}")
                conn.disconnect()
                return 1
            print("Logged in.")

            try:
                if action == 'apply':
                    execute(spec, conn, state, log_file=log_file)
                elif action == 'verify':
                    issues = verify(spec, conn, state, log_file=log_file)
                    return 0 if not issues else 1
                elif action == 'rollback':
                    rollback(spec, conn, state, log_file=log_file)
            finally:
                conn.disconnect()
                print("Disconnected.")
    finally:
        if log_file:
            log_file.close()

    return 0


if __name__ == '__main__':
    exit(main())
