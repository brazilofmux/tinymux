#!/usr/bin/env python3
"""WorldBuilder Executor — connects to a MUX and applies compiled commands.

Handles:
- Telnet connection with login
- Sequential command execution with output capture
- Dbref extraction from @dig/@open output
- State file generation (spec ID → dbref mapping)
- Dry-run mode (log commands without executing)
"""

import re
import socket
import ssl
import time
import json
import yaml
import argparse
from pathlib import Path
from worldbuilder import parse_spec, check_drc, compile_spec


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
# State File
# ---------------------------------------------------------------------------

class StateFile:
    """Tracks spec ID → dbref mappings."""

    def __init__(self, path):
        self.path = Path(path)
        self.objects = {}    # spec_id -> {dbref, type, name}
        self.zone = None
        self.last_applied = None
        if self.path.exists():
            self._load()

    def _load(self):
        with open(self.path, 'r') as f:
            data = yaml.safe_load(f)
        if data:
            self.objects = data.get('objects', {})
            self.zone = data.get('zone')
            self.last_applied = data.get('last_applied')

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        data = {
            'zone': self.zone,
            'last_applied': time.strftime('%Y-%m-%dT%H:%M:%SZ'),
            'objects': self.objects,
        }
        with open(self.path, 'w') as f:
            yaml.dump(data, f, default_flow_style=False, sort_keys=False)

    def set_room(self, spec_id, dbref, name):
        self.objects[spec_id] = {'dbref': dbref, 'type': 'room', 'name': name}

    def set_exit(self, spec_id, dbref, name):
        self.objects[spec_id] = {'dbref': dbref, 'type': 'exit', 'name': name}

    def get_dbref(self, spec_id):
        obj = self.objects.get(spec_id)
        return obj['dbref'] if obj else None

    def resolve(self, placeholder):
        """Resolve %{room:spec_id} to a dbref."""
        m = re.match(r'%\{room:(\w+)\}', placeholder)
        if m:
            return self.get_dbref(m.group(1))
        return None


# ---------------------------------------------------------------------------
# Executor
# ---------------------------------------------------------------------------

DBREF_PATTERN = re.compile(r'#(\d+)')


def extract_dbref(text):
    """Extract the first dbref (#NNN) from MUX output."""
    m = DBREF_PATTERN.search(text)
    return m.group(0) if m else None


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
    log(f'\n=== Phase 1: Creating {len(spec.rooms)} rooms ===')

    for room_id, room in spec.rooms.items():
        existing = state.get_dbref(room_id)
        if existing:
            log(f'\n--- Room: {room.name} ({room_id}) — exists as {existing}, updating ---')
            # Teleport to existing room and update
            do_cmd(f'@teleport me={existing}')
            time.sleep(0.2)
        else:
            log(f'\n--- Room: {room.name} ({room_id}) — creating ---')
            resp = do_cmd(f'@dig/teleport {room.name}')

            if not dry_run:
                # Extract dbref from @dig output
                dbref = extract_dbref(resp)
                if dbref:
                    state.set_room(room_id, dbref, room.name)
                    log(f'  [state] {room_id} = {dbref}')
                else:
                    # Try to get current location
                    resp2 = do_cmd('think %L')
                    dbref = extract_dbref(resp2)
                    if dbref:
                        state.set_room(room_id, dbref, room.name)
                        log(f'  [state] {room_id} = {dbref} (from %L)')
                    else:
                        log(f'  [WARNING] Could not capture dbref for {room_id}')
            else:
                state.set_room(room_id, f'#DRY_{room_id}', room.name)

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
                exit_id = f'exit_{ex.from_room}_{ex.to_room}'
                state.set_exit(exit_id, dbref, forward_name)

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
                    exit_id = f'exit_{ex.to_room}_{ex.from_room}'
                    state.set_exit(exit_id, dbref, back_name)

    # Save state
    state.save()
    log(f'\n=== Done. State saved to {state.path} ===')
    return True


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='WorldBuilder Executor — apply specs to a live MUX')
    parser.add_argument('spec', help='Path to YAML spec file')
    parser.add_argument('--host', required=True, help='MUX hostname')
    parser.add_argument('--port', default='4201', help='MUX port')
    parser.add_argument('--ssl', action='store_true', help='Use SSL/TLS')
    parser.add_argument('--character', required=True, help='Builder character name')
    parser.add_argument('--password', required=True, help='Builder password')
    parser.add_argument('--state', help='State file path (default: .worldbuilder/<zone>.state.yaml)')
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

    try:
        if args.dry_run:
            print(f"Dry run — no commands will be sent to {args.host}:{args.port}")
            execute(spec, None, state, dry_run=True, log_file=log_file)
        else:
            # Connect
            print(f"Connecting to {args.host}:{args.port}...")
            conn = MuxConnection(args.host, args.port, args.ssl)
            conn.connect()
            print("Connected. Logging in...")

            resp = conn.login(args.character, args.password)
            if 'Invalid' in resp or 'incorrect' in resp.lower():
                print(f"Login failed: {resp.strip()}")
                conn.disconnect()
                return 1
            print("Logged in.")

            try:
                execute(spec, conn, state, log_file=log_file)
            finally:
                conn.disconnect()
                print("Disconnected.")
    finally:
        if log_file:
            log_file.close()

    return 0


if __name__ == '__main__':
    exit(main())
