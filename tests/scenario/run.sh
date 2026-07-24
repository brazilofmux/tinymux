#!/bin/bash
#
#   run.sh — live scenario test for the wildcard capture path.
#
#   Spins a throwaway netmux from the starter DB on a free port, runs
#   wild_capture.py against it (login as Wizard, drive $-command captures),
#   then tears the server down.  Opt-in via `make test-scenario`; deliberately
#   NOT part of `make test` (a live-socket test is timing-sensitive).
#
#   Needs a built netmux (run `make install` first) and python3.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
STARTER_DB="$REPO_ROOT/mux/game/data/netmux.db"

# GNU timeout is `timeout` on Linux, `gtimeout` from Homebrew coreutils on macOS.
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT="timeout 60"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT="gtimeout 60"
else
    TIMEOUT=""
fi

# --- Preflight (skip, don't fail, when prerequisites are absent) ------------
if [ ! -x "$BIN/netmux" ]; then
    echo "SKIP: $BIN/netmux not found — run 'make install' first."
    exit 0
fi
if [ ! -r "$STARTER_DB" ]; then
    echo "SKIP: starter DB not found at $STARTER_DB."
    exit 0
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 not found."
    exit 0
fi

# Ask the OS for a free TCP port.
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()') || {
    echo "SKIP: could not allocate a free port."
    exit 0
}

WORK=$(mktemp -d)
NETMUX_PID=""

cleanup() {
    if [ -n "$NETMUX_PID" ]; then
        # Kill the slave (a child of netmux) and netmux itself.
        pkill -P "$NETMUX_PID" 2>/dev/null
        kill "$NETMUX_PID" 2>/dev/null
        wait "$NETMUX_PID" 2>/dev/null
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

# --- Build a throwaway game instance ----------------------------------------
mkdir -p "$WORK/data"
cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" "$WORK/"
ln -s "$BIN" "$WORK/bin"
ln -s "$REPO_ROOT/mux/game/text" "$WORK/text"
cp "$STARTER_DB" "$WORK/data/netmux.db"

cat > "$WORK/netmux.conf" <<EOF
input_database  data/netmux.db
output_database data/netmux.db.new
crash_database  data/netmux.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port $PORT
mud_name ScenarioMUX
master_room #2
include alias.conf
include compat.conf
EOF

echo "==> Starting throwaway netmux on port $PORT"
( cd "$WORK" && LD_LIBRARY_PATH="$BIN" ./bin/netmux -c netmux.conf > netmux.log 2>&1 ) &
NETMUX_PID=$!

# Wait for the listener to accept connections (up to ~15s).
UP=0
for _ in $(seq 1 30); do
    if python3 -c "import socket,sys; s=socket.socket(); s.settimeout(0.5); sys.exit(0 if s.connect_ex(('127.0.0.1',$PORT))==0 else 1)" 2>/dev/null; then
        UP=1
        break
    fi
    # Bail early if netmux died during startup.
    if ! kill -0 "$NETMUX_PID" 2>/dev/null; then
        echo "FAIL: netmux exited during startup. Log:"
        sed 's/^/    /' "$WORK/netmux.log" 2>/dev/null | tail -20
        exit 1
    fi
    sleep 0.5
done

if [ "$UP" -ne 1 ]; then
    echo "FAIL: netmux did not start listening on port $PORT."
    exit 1
fi

# --- Drive the scenarios ----------------------------------------------------
# Each driver runs against the same throwaway server; a failure in one does
# not skip the others, and any failure fails the run.
RC=0

for DRIVER in wild_capture.py site_threshold.py; do
    echo "==> $DRIVER"
    $TIMEOUT python3 "$SCRIPT_DIR/$DRIVER" 127.0.0.1 "$PORT" || RC=1
done

exit "$RC"
