#!/bin/bash
#
#   run.sh — live NETWORK + QUEUE stress harness.
#
#   Spins a throwaway netmux from the starter DB on a free port, drives it
#   with stress_driver.py (concurrent connections, bulk queue fan-outs, an
#   overload burst), asserts the server survives and stays correct, then
#   tears it down.  Opt-in via `make test-stress`; NOT part of `make test`.
#
#   Scope note: this harness targets the accept/read/write path and the
#   command scheduler.  It deliberately does NOT stress the evaluator, JIT,
#   DBT, or the attribute store — workloads use the cheapest softcode that
#   still generates queue entries and socket traffic.  Because the queue is
#   best-effort (docs/survey-queue.md), the pass criteria are survival,
#   responsiveness, and no-loss ONLY when quotas are raised above the load;
#   overload shedding is reported, not failed.
#
#   Needs a built netmux (run `make install` first) and python3.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
STARTER_DB="$REPO_ROOT/mux/game/data/netmux.db"

if command -v timeout >/dev/null 2>&1; then
    TIMEOUT="timeout 180"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT="gtimeout 180"
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

PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()') || {
    echo "SKIP: could not allocate a free port."
    exit 0
}

WORK=$(mktemp -d)
NETMUX_PID=""

cleanup() {
    if [ -n "$NETMUX_PID" ]; then
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

# Quotas raised well above the load so the INTEGRITY phases see no shedding;
# the overload phase deliberately exceeds even these to exercise the
# best-effort survival path.
cat > "$WORK/netmux.conf" <<EOF
input_database  data/netmux.db
output_database data/netmux.db.new
crash_database  data/netmux.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port $PORT
mud_name StressMUX
master_room #2
command_quota_increment 1000000
command_quota_max       1000000
queue_active_chunk      100
player_queue_limit      60000
include alias.conf
include compat.conf
EOF

echo "==> Starting throwaway netmux on port $PORT"
( cd "$WORK" && LD_LIBRARY_PATH="$BIN" ./bin/netmux -c netmux.conf > netmux.log 2>&1 ) &
NETMUX_PID=$!

UP=0
for _ in $(seq 1 30); do
    if python3 -c "import socket,sys; s=socket.socket(); s.settimeout(0.5); sys.exit(0 if s.connect_ex(('127.0.0.1',$PORT))==0 else 1)" 2>/dev/null; then
        UP=1
        break
    fi
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

# --- Drive the stress workload ----------------------------------------------
$TIMEOUT python3 "$SCRIPT_DIR/stress_driver.py" 127.0.0.1 "$PORT"
RC=$?

# --- Crash check: the server must not have died under load ------------------
if ! kill -0 "$NETMUX_PID" 2>/dev/null; then
    echo "FAIL: netmux process died during the stress run. Log tail:"
    sed 's/^/    /' "$WORK/netmux.log" 2>/dev/null | tail -30
    RC=1
fi
if grep -aE 'SIGSEGV|SIGABRT|SIGBUS|panic|assert' "$WORK/netmux.log" >/dev/null 2>&1; then
    echo "FAIL: crash/abort signature in netmux.log:"
    grep -aE 'SIGSEGV|SIGABRT|SIGBUS|panic|assert' "$WORK/netmux.log" | sed 's/^/    /' | tail -10
    RC=1
fi

exit "$RC"
