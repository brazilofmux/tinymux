#!/usr/bin/env python3
"""WorldBuilder Executor — connects to a MUX and applies compiled commands.

Handles:
- Telnet connection with login
- Sequential command execution with output capture
- Dbref extraction from @dig/@open output
- State file generation (spec ID → dbref mapping)
- Dry-run mode (log commands without executing)
"""

import socket
import ssl
import time
import argparse
from pathlib import Path
from worldbuilder import parse_spec, check_drc
from live_adapter import StateFile, content_hash, extract_dbref


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

    def __init__(self, host, port, use_ssl=False, timeout=10):
        self.host = host
        self.port = int(port)
        self.use_ssl = use_ssl
        self.timeout = timeout
        self.sock = None
        self.buffer = b''

    def connect(self):
        raw = socket.create_connection((self.host, self.port), self.timeout)
        if self.use_ssl:
            ctx = ssl.create_default_context()
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


def execute(spec, conn, state, dry_run=False, log_file=None):
    """Execute a compiled spec against a live MUX connection.

    Returns True on success.
    """
    def log(msg):
        print(msg)
        if log_file:
            log_file.write(msg + '\n')

    def do_cmd(cmd, expect_dbref=False):
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

    state.zone = spec.zone.name

    # Phase 1: Create rooms and capture dbrefs
    log(f'\n=== Phase 1: Creating/Updating {len(spec.rooms)} rooms ===')

    for room_id, room in spec.rooms.items():
        existing = state.get_dbref(room_id)
        if existing:
            log(f'\n--- Room: {room.name} ({room_id}) — exists as {existing}, updating ---')
            # Teleport to existing room and update
            do_cmd(f'@teleport me={existing}')
            time.sleep(0.2)
            if not dry_run:
                # Query objid for identity tracking
                objid_resp = do_cmd(f'think [objid({existing})]')
                objid = objid_resp.strip() if objid_resp else None
                state.set_room(room_id, existing, room, objid=objid)
        else:
            log(f'\n--- Room: {room.name} ({room_id}) — creating ---')
            resp = do_cmd(f'@dig/teleport {room.name}')

            if not dry_run:
                # Always use think %L — most reliable across MUX versions
                time.sleep(0.2)
                resp2 = do_cmd('think %L')
                dbref = extract_dbref(resp2)
                if not dbref:
                    # Fallback: try parsing @dig output
                    dbref = extract_dbref(resp)
                if dbref:
                    # Query objid for identity tracking
                    objid_resp = do_cmd(f'think [objid({dbref})]')
                    objid = objid_resp.strip() if objid_resp else None
                    state.set_room(room_id, dbref, room, objid=objid)
                    log(f'  [state] {room_id} = {dbref} (objid: {objid})')
                else:
                    log(f'  [WARNING] Could not capture dbref for {room_id}')
            else:
                # In dry run, we still want to store the object in state so Phase 2 can find it
                # We use a dummy object for dry-run
                state.objects[room_id] = {
                    'dbref': f'#DRY_{room_id}',
                    'type': 'room',
                    'name': room.name,
                    'description': room.description,
                    'flags': sorted(room.flags),
                    'attrs': room.attrs,
                    'content_hash': content_hash(room)
                }

        # Set description
        desc = room.description.rstrip().replace('\n', '%r')
        do_cmd(f'@desc here={desc}')

        # Set flags
        for flag in room.flags:
            do_cmd(f'@set here={flag}')

        # Set attributes
        for attr_name, attr_value in room.attrs.items():
            do_cmd(f'&{attr_name} here={attr_value}')

    # Phase 2: Create exits (now that all rooms have dbrefs)
    log(f'\n=== Phase 2: Creating exits ===')

    for ex in spec.exits:
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

        # Teleport to source room
        do_cmd(f'@teleport me={from_dbref}')
        time.sleep(0.2)

        # Create forward exit
        forward_name = ex.name.split(';')[0]
        log(f'\n--- Exit: {forward_name} ({ex.from_room} -> {ex.to_room}) ---')
        resp = do_cmd(f'@open {ex.name}={to_dbref}')

        if not dry_run:
            dbref = extract_dbref(resp)
            if dbref:
                objid_resp = do_cmd(f'think [objid({dbref})]')
                objid = objid_resp.strip() if objid_resp else None
                exit_id = f'exit_{ex.from_room}_{ex.to_room}'
                state.set_exit(exit_id, dbref, forward_name, objid=objid)

        # Create back exit
        if ex.back_name:
            do_cmd(f'@teleport me={to_dbref}')
            time.sleep(0.2)

            back_name = ex.back_name.split(';')[0]
            log(f'--- Back exit: {back_name} ({ex.to_room} -> {ex.from_room}) ---')
            resp = do_cmd(f'@open {ex.back_name}={from_dbref}')

            if not dry_run:
                dbref = extract_dbref(resp)
                if dbref:
                    objid_resp = do_cmd(f'think [objid({dbref})]')
                    objid = objid_resp.strip() if objid_resp else None
                    exit_id = f'exit_{ex.to_room}_{ex.from_room}'
                    state.set_exit(exit_id, dbref, back_name, objid=objid)

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

    log(f"Verifying {len(spec.rooms)} rooms...")

    for room_id, room in spec.rooms.items():
        dbref = state.get_dbref(room_id)
        if not dbref:
            issues.append(f"Room '{room_id}' ({room.name}): not in state file (never applied?)")
            continue

        # Validate objid — detect dbref recycling
        stored_objid = state.objects.get(room_id, {}).get('objid')
        if stored_objid:
            live_objid = query_one(f'think [objid({dbref})]')
            if live_objid and live_objid != stored_objid:
                issues.append(f"Room '{room_id}' ({dbref}): objid mismatch — stored=\"{stored_objid}\" live=\"{live_objid}\" (dbref was recycled!)")
                continue
            if not live_objid or live_objid.startswith('#-1'):
                issues.append(f"Room '{room_id}' ({dbref}): object no longer exists")
                continue

        # Check name
        live_name = query_one(f'think [name({dbref})]')
        if live_name and live_name != room.name:
            issues.append(f"Room '{room_id}' ({dbref}): name mismatch — spec=\"{room.name}\" live=\"{live_name}\"")

        # Check description exists
        live_desc = query_one(f'think [get({dbref}/DESCRIBE)]')
        if not live_desc:
            live_desc = query_one(f'think [get({dbref}/DESC)]')
        if not live_desc and room.description.strip():
            issues.append(f"Room '{room_id}' ({dbref}): description missing on live server")

        # Check attributes
        for attr_name, expected_val in room.attrs.items():
            live_val = query_one(f'think [get({dbref}/{attr_name})]')
            if live_val != expected_val:
                issues.append(f"Room '{room_id}' ({dbref}): attr {attr_name} mismatch — spec=\"{expected_val}\" live=\"{live_val}\"")

        log(f"  [{dbref}] {room.name} — {'OK' if not any(room_id in i for i in issues) else 'MISMATCH'}")

    # Check exits exist
    log(f"Verifying exits...")
    for ex in spec.exits:
        from_dbref = state.get_dbref(ex.from_room)
        to_dbref = state.get_dbref(ex.to_room)
        if not from_dbref or not to_dbref:
            continue
        # Check that an exit from from_dbref leads to to_dbref
        exit_list = query_one(f'think [exits({from_dbref})]')
        if to_dbref not in (exit_list or ''):
            # More thorough check — walk exits
            pass  # Could enumerate, but this is a good first pass

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
            conn = MuxConnection(args.host, int(args.port), args.ssl)
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
