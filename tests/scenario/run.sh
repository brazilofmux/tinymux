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
# A second instance, started later with a non-default reality configuration
# (#2110).  Declared here so cleanup() can reference it under `set -u` before
# that point.
RL_WORK=""
RL_NETMUX_PID=""

cleanup() {
    if [ -n "$RL_NETMUX_PID" ]; then
        pkill -P "$RL_NETMUX_PID" 2>/dev/null
        kill "$RL_NETMUX_PID" 2>/dev/null
        wait "$RL_NETMUX_PID" 2>/dev/null
    fi
    if [ -n "$NETMUX_PID" ]; then
        # Kill the slave (a child of netmux) and netmux itself.
        pkill -P "$NETMUX_PID" 2>/dev/null
        kill "$NETMUX_PID" 2>/dev/null
        wait "$NETMUX_PID" 2>/dev/null
    fi
    rm -rf "$WORK"
    [ -n "$RL_WORK" ] && rm -rf "$RL_WORK"
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

for DRIVER in wild_capture.py site_threshold.py jit_perms.py jit_alternation.py telnet_negotiation.py page_cost.py driver_config_sync.py hook_noeval.py conn_sessions.py cpu_budget.py sidefx_fargs.py proto_detect.py; do
    echo "==> $DRIVER"
    $TIMEOUT python3 "$SCRIPT_DIR/$DRIVER" 127.0.0.1 "$PORT" || RC=1
done

# --- A second server, with reality levels that actually hide something ------
#
# reality_name_index.py needs def_thing_tx to NOT intersect def_player_rx, so
# that promote_match() takes its REALITY_LVLS early return.  At the shipped
# defaults every rx/tx is 1 and it never does -- which is exactly why #2110
# survived a 36/36 `make test`.  The setting is global, so it cannot be folded
# into the server above without changing what every other driver is testing.
RL_PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()') || {
    echo "SKIP: could not allocate a free port for the reality-levels server."
    exit "$RC"
}

RL_WORK=$(mktemp -d)
mkdir -p "$RL_WORK/data"
cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" "$RL_WORK/"
ln -s "$BIN" "$RL_WORK/bin"
ln -s "$REPO_ROOT/mux/game/text" "$RL_WORK/text"
cp "$STARTER_DB" "$RL_WORK/data/netmux.db"

cat > "$RL_WORK/netmux.conf" <<EOF
input_database  data/netmux.db
output_database data/netmux.db.new
crash_database  data/netmux.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port $RL_PORT
mud_name RealityMUX
master_room #2
def_player_rx 1
def_player_tx 1
def_room_rx 1
def_room_tx 1
def_exit_rx 1
def_exit_tx 1
def_thing_rx 1
def_thing_tx 2
include alias.conf
include compat.conf
EOF

echo "==> Starting reality-levels netmux on port $RL_PORT"
( cd "$RL_WORK" && LD_LIBRARY_PATH="$BIN" ./bin/netmux -c netmux.conf > netmux.log 2>&1 ) &
RL_NETMUX_PID=$!

RL_UP=0
for _ in $(seq 1 30); do
    if python3 -c "import socket,sys; s=socket.socket(); s.settimeout(0.5); sys.exit(0 if s.connect_ex(('127.0.0.1',$RL_PORT))==0 else 1)" 2>/dev/null; then
        RL_UP=1
        break
    fi
    if ! kill -0 "$RL_NETMUX_PID" 2>/dev/null; then
        echo "FAIL: reality-levels netmux exited during startup. Log:"
        sed 's/^/    /' "$RL_WORK/netmux.log" 2>/dev/null | tail -20
        exit 1
    fi
    sleep 0.5
done

if [ "$RL_UP" -ne 1 ]; then
    echo "FAIL: reality-levels netmux did not start listening on port $RL_PORT."
    exit 1
fi

echo "==> reality_name_index.py"
$TIMEOUT python3 "$SCRIPT_DIR/reality_name_index.py" 127.0.0.1 "$RL_PORT" || RC=1

exit "$RC"
