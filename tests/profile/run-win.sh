#!/bin/bash
#
#   run-win.sh — Profile a LIVE netmux on Windows (#2081).
#
#   The Windows counterpart of run.sh.  Same throwaway instance, same
#   starter DB, same driver.py, same phases — so the numbers are
#   comparable to the Linux leg rather than a bespoke Windows benchmark.
#   Only the profiler differs: run.sh attaches `perf record`, and there is
#   no perf here.
#
#   What this measures that PerfSmokeWin cannot: PerfSmokeWin profiles
#   muxscript running the smoke corpus — no network, no descriptors, no
#   database writes, no dump.  On this box that profile was ~18% engine.dll
#   and essentially all of it the compile pipeline, which says more about
#   the harness than the server (#1926, #2081).
#
#   Per-phase CPU comes from driver.py's cpu_times(), which uses
#   GetProcessTimes on Windows.  Wall time is the driver's own monotonic
#   window.  ETW sampling is opt-in via --etw because a kernel session
#   needs an elevated shell and is only worth its cost once the A/B says
#   where to look.
#
#   Run from Git Bash.  Needs a built netmux (mux/bin_release), python3.
#
#   Usage: tests/profile/run-win.sh [--scale N] [--jit 0|1] [--etw] [--keep]
#            --scale N   multiply per-phase command counts (default 1)
#            --jit 0|1   jit_eval_brackets setting (default: leave at built-in)
#            --etw       also capture an ETW CPU profile of the whole run
#            --keep      keep the work directory
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/bin_release"
STARTER_DB="$REPO_ROOT/mux/game/data/netmux.db"

SCALE=1
JIT=""
ETW=0
KEEP=0
while [ $# -gt 0 ]; do
    case "$1" in
        --scale) SCALE=$2; shift 2 ;;
        --jit)   JIT=$2; shift 2 ;;
        --etw)   ETW=1; shift ;;
        --keep)  KEEP=1; shift ;;
        *) echo "unknown option: $1"; exit 2 ;;
    esac
done

if [ ! -x "$BIN/netmux.exe" ]; then
    echo "SKIP: $BIN/netmux.exe not found — build netmux.sln (Release|x64) first."
    exit 0
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 not found."
    exit 0
fi

# Never hardcode a port: 2860 is very likely somebody's live game on this
# box (the W3 soak runs there), and a fixed port would take it down.
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()') || {
    echo "SKIP: could not allocate a free port."; exit 0; }

WORK=$(mktemp -d -t profwin.XXXXXX)
SERVER_ERR="$WORK/server.err"
NETMUX_PID=""
REAL_PID=""

cleanup() {
    # $! is the launcher subshell, not the server: killing it leaves netmux
    # running, holding the port and its SQLite files (so the rmdir below
    # fails with "Device or resource busy" and the next run allocates a new
    # port while the old server lives on).  Kill the real pid.
    if [ -n "${REAL_PID:-}" ]; then
        powershell.exe -NoProfile -Command \
            "Stop-Process -Id $REAL_PID -Force -ErrorAction SilentlyContinue" \
            >/dev/null 2>&1
        sleep 1
    fi
    if [ -n "$NETMUX_PID" ] && kill -0 "$NETMUX_PID" 2>/dev/null; then
        kill "$NETMUX_PID" 2>/dev/null
        sleep 1
        kill -9 "$NETMUX_PID" 2>/dev/null
    fi
    [ "$ETW" -eq 1 ] && xperf -stop >/dev/null 2>&1
    if [ "$KEEP" -eq 1 ]; then
        echo "  work kept: $WORK"
    else
        rm -rf "$WORK"
    fi
}
trap cleanup EXIT INT TERM

# --- throwaway game instance (mirrors run.sh) -------------------------------
mkdir -p "$WORK/data"
cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" "$WORK/"
# Copies, not symlinks: MSYS turns `ln -s` into a copy anyway, and netmux
# resolves its module DLLs relative to the binary's directory.
cp -r "$BIN" "$WORK/bin"
cp "$REPO_ROOT/mux/rv64/softlib.rv64" "$WORK/bin/" 2>/dev/null || true
cp -r "$REPO_ROOT/mux/game/text" "$WORK/text"
cp "$STARTER_DB" "$WORK/data/netmux.db"

{
    cat <<EOF
input_database  data/netmux.db
output_database data/netmux.db.new
crash_database  data/netmux.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port $PORT
mud_name PerfMUXWin
master_room #2
command_quota_increment 10000000
command_quota_max 10000000
player_queue_limit 100000000
fork_dump no
EOF
    # The A/B this harness exists for.  Left unset by default so a plain run
    # measures the shipped configuration.
    [ -n "$JIT" ] && echo "jit_eval_brackets $JIT"
    echo "include alias.conf"
    echo "include compat.conf"
} > "$WORK/netmux.conf"

echo "==> netmux on port $PORT (work: $WORK)${JIT:+, jit_eval_brackets=$JIT}"

if [ "$ETW" -eq 1 ]; then
    if ! command -v xperf >/dev/null 2>&1; then
        echo "SKIP --etw: xperf not found (install Windows Performance Toolkit)."
        ETW=0
    elif ! net session >/dev/null 2>&1; then
        echo "SKIP --etw: not elevated; ETW kernel sessions need an admin shell."
        ETW=0
    else
        # Buffer file outside the churning work directory — a capture whose
        # -f file sat in the workload's own directory produced an ETL that
        # xperf itself could not process (#1926).
        xperf -on PROC_THREAD+LOADER+PROFILE -stackwalk Profile \
              -f "$(cygpath -m "${TEMP:-/tmp}")/profwin-kernel.etl" || ETW=0
    fi
fi

# --- boot -------------------------------------------------------------------
( cd "$WORK" && ./bin/netmux.exe -c netmux.conf -p netmux.pid -e . > "$SERVER_ERR" 2>&1 ) &
NETMUX_PID=$!

for _ in $(seq 1 60); do
    if python3 -c "import socket,sys; s=socket.socket(); s.settimeout(0.5); sys.exit(0 if s.connect_ex(('127.0.0.1',$PORT))==0 else 1)" 2>/dev/null; then
        break
    fi
    sleep 0.5
done
if ! python3 -c "import socket,sys; s=socket.socket(); s.settimeout(0.5); sys.exit(0 if s.connect_ex(('127.0.0.1',$PORT))==0 else 1)" 2>/dev/null; then
    echo "FAIL: netmux did not start listening on port $PORT."
    sed 's/^/    /' "$SERVER_ERR" | tail -20
    exit 1
fi

# The shell backgrounded a subshell, so $! is the launcher, not the server.
# driver.py needs the real pid for GetProcessTimes.
REAL_PID=$(powershell.exe -NoProfile -Command \
    "(Get-NetTCPConnection -LocalPort $PORT -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1).OwningProcess" \
    2>/dev/null | tr -d '\r' | tr -d ' ')
[ -z "$REAL_PID" ] && REAL_PID=""
echo "  netmux pid ${REAL_PID:-unknown}"

# --- drive ------------------------------------------------------------------
echo "==> phases (scale=$SCALE)"
if [ -n "$REAL_PID" ]; then
    python3 "$SCRIPT_DIR/driver.py" 127.0.0.1 "$PORT" "$WORK/phases.json" \
        --scale "$SCALE" --pid "$REAL_PID"
else
    python3 "$SCRIPT_DIR/driver.py" 127.0.0.1 "$PORT" "$WORK/phases.json" \
        --scale "$SCALE"
fi
RC=$?

if [ "$ETW" -eq 1 ]; then
    xperf -d "$(cygpath -m "$WORK")/profwin.etl" >/dev/null 2>&1
    echo "  ETL: $WORK/profwin.etl (decode per #1926: dumper, not -a profile)"
    KEEP=1
fi

if [ "$RC" -ne 0 ]; then
    echo "FAIL: driver exited $RC"
    sed 's/^/    /' "$SERVER_ERR" | tail -20
    exit 1
fi

# --- report -----------------------------------------------------------------
python3 - "$WORK/phases.json" <<'_PY'
import json, sys
with open(sys.argv[1]) as f:
    d = json.load(f)
print()
print("%-16s %10s %10s %10s %8s" % ("phase", "wall_s", "user_s", "sys_s", "count"))
tw = tu = ts = 0.0
for p in d["phases"]:
    print("%-16s %10.3f %10.3f %10.3f %8s"
          % (p["name"], p["seconds"], p.get("user_s", 0.0), p.get("sys_s", 0.0),
             p.get("count", "")))
    tw += p["seconds"]; tu += p.get("user_s", 0.0) or 0.0; ts += p.get("sys_s", 0.0) or 0.0
print("%-16s %10.3f %10.3f %10.3f" % ("TOTAL", tw, tu, ts))
_PY

[ "$KEEP" -eq 1 ] && echo "  phases.json: $WORK/phases.json"
exit 0
