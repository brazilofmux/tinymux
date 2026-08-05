#!/bin/bash
#
#   run.sh — Profile a LIVE netmux, one phase at a time.
#
#   Spins a throwaway netmux from the starter DB on a free port, attaches
#   `perf record` to it, drives it through driver.py's phases, and then slices
#   the single recording into one profile per phase using the CLOCK_MONOTONIC
#   windows the driver wrote down.
#
#   This is the workload testcases/tools/PerfSmoke cannot be.  PerfSmoke
#   profiles muxscript: no network, no descriptors, no database writes, no
#   dump.  Everything below the `dispatch` phase is a path muxscript never
#   executes.
#
#   Opt-in, and deliberately NOT part of `make test`: it takes minutes, it is
#   timing-sensitive, and a benchmark that fails the build on a noisy box is
#   worse than no benchmark.
#
#   Needs a built netmux (`make install`), python3, and perf.
#
#   Usage: tests/profile/run.sh [--scale N] [--keep]
#          --scale N   multiply per-phase command counts (default 1)
#          --keep      keep the work directory and perf.data
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
STARTER_DB="$REPO_ROOT/mux/game/data/netmux.db"

SCALE=1
KEEP=0
LABEL=""
while [ $# -gt 0 ]; do
    case "$1" in
        --scale) SCALE=$2; shift 2 ;;
        --keep)  KEEP=1; shift ;;
        # Measure a netmux that is not this tree's.  Everything here is driven
        # from OUTSIDE the server -- commands over a socket, CPU from
        # /proc/<pid>/stat -- so nothing depends on rvbench(), benchmark(),
        # jitstats() or any other 2.14-only instrumentation.  That is what
        # makes a cross-version comparison possible at all; jitbase.sh cannot
        # do this, because benchmark() does not exist in 2.13.
        --bin)   BIN=$2; STARTER_DB=""; shift 2 ;;
        --db)    STARTER_DB=$2; shift 2 ;;
        --label) LABEL=$2; shift 2 ;;
        *) echo "unknown option: $1"; exit 2 ;;
    esac
done

# --bin without --db: look for the starter database beside it, the way a
# built tree lays it out.
if [ -z "${STARTER_DB:-}" ]; then
    for cand in "$BIN/../data/netmux.db" "$REPO_ROOT/mux/game/data/netmux.db"; do
        if [ -r "$cand" ]; then STARTER_DB=$(cd "$(dirname "$cand")" && pwd)/$(basename "$cand"); break; fi
    done
fi
BIN=$(cd "$BIN" 2>/dev/null && pwd) || { echo "SKIP: --bin directory not found."; exit 0; }
[ -n "$LABEL" ] && echo "==> label: $LABEL"

# --- Preflight (skip, don't fail, when prerequisites are absent) ------------
if [ ! -x "$BIN/netmux" ]; then
    echo "SKIP: $BIN/netmux not found — run 'make install' first."
    exit 0
fi
if [ ! -r "$STARTER_DB" ]; then
    echo "SKIP: starter DB not found at $STARTER_DB."
    exit 0
fi
for tool in python3 perf; do
    command -v "$tool" >/dev/null 2>&1 || { echo "SKIP: $tool not found."; exit 0; }
done

# perf_event_paranoid > 2 blocks even self-profiling.
PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 2)
if [ "$PARANOID" -gt 2 ]; then
    echo "SKIP: perf_event_paranoid=$PARANOID blocks user-space profiling."
    exit 0
fi

# Ask the OS for a free TCP port.  Never hardcode one: on a shared box a fixed
# port may belong to somebody else's live game.
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()') || {
    echo "SKIP: could not allocate a free port."
    exit 0
}

WORK=$(mktemp -d)
NETMUX_PID=""
# Resolved after the port answers; declared here so cleanup() can run under
# `set -u` before that point.
NETMUX_REAL=""
PERF_PID=""

cleanup() {
    if [ -n "$PERF_PID" ] && kill -0 "$PERF_PID" 2>/dev/null; then
        kill -INT "$PERF_PID" 2>/dev/null
        wait "$PERF_PID" 2>/dev/null
    fi

    # Signal netmux ITSELF first, then the launcher subshell.
    #
    # The old order was the other way round, and it leaked a server every time
    # the script was interrupted: killing the launcher makes netmux an orphan
    # (reparented to init) before the `pkill -P <launcher>` runs, so that sweep
    # finds no children and netmux survives holding its port.  Six of them
    # accumulated in one session before this was noticed -- and because the
    # port is kernel-assigned, each one is invisible until you go looking.
    #
    # SIGTERM, never SIGKILL: -9 orphans the slave and stubslave to init.
    for _pid in "$NETMUX_REAL" "$NETMUX_PID"; do
        [ -n "$_pid" ] || continue
        kill -TERM "$_pid" 2>/dev/null
    done
    if [ -n "$NETMUX_REAL" ]; then
        for _ in $(seq 1 20); do
            kill -0 "$NETMUX_REAL" 2>/dev/null || break
            sleep 0.5
        done
        # Backstop for a slave/stubslave that outlived its parent.
        pkill -TERM -P "$NETMUX_REAL" 2>/dev/null
    fi
    if [ "$KEEP" -eq 1 ]; then
        echo "kept: $WORK"
    else
        rm -rf "$WORK"
    fi
}
# EXIT alone is not enough.  Under `timeout`, or any wrapper that signals the
# script rather than letting it finish, bash runs no EXIT trap -- and this run
# leaves a netmux plus its slave and stubslave behind, holding a port.  Four
# such sets accumulated during development before this was noticed.  Trap the
# signals too, and re-raise so the exit status still says we were killed.
cleanup_and_die() {
    cleanup
    trap - EXIT
    exit 143
}
trap cleanup EXIT
trap cleanup_and_die INT TERM HUP

# --- Build a throwaway game instance ----------------------------------------
mkdir -p "$WORK/data"
# Prefer the conf files that ship beside the binary under test: 2.13 and 2.14
# do not necessarily accept the same directives, and feeding one line's
# alias/compat files to the other is how a cross-version run turns into a
# comparison of config parsing.
for f in alias.conf compat.conf; do
    if [ -r "$BIN/../$f" ]; then
        cp "$BIN/../$f" "$WORK/"
    else
        cp "$REPO_ROOT/mux/game/$f" "$WORK/"
    fi
done
ln -s "$BIN" "$WORK/bin"
ln -s "$REPO_ROOT/mux/game/text" "$WORK/text"
cp "$STARTER_DB" "$WORK/data/netmux.db"

# Quotas are raised so the phases measure the command path rather than the
# queue's shedding behaviour (tests/stress/stress_driver.py documents what
# happens when you do not: QueueMax halts and flushes the owner, and a halted
# object silently queues nothing, so every later command is a no-op).
cat > "$WORK/netmux.conf" <<EOF
input_database  data/netmux.db
output_database data/netmux.db.new
crash_database  data/netmux.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port $PORT
mud_name PerfMUX
master_room #2
command_quota_increment 10000000
command_quota_max 10000000
player_queue_limit 100000000
# do_dump() -> fork_and_dump() forks and returns, so with fork_dump on, the
# dump phase times the fork and not the dump (0.016s regardless of database
# size, on the first run).  Inline it so the phase measures the work.
fork_dump no
include alias.conf
include compat.conf
EOF

echo "==> netmux on port $PORT (work: $WORK)"

# PERF_QUIET=1 discards the server's stderr instead of writing it to a file.
# It matters more than it looks: GANL_EPOLL_DEBUG is gated on NDEBUG, nothing
# in this build ever defines NDEBUG, and the epoll engine has 106 of those
# sites -- so a default build emits about 7 formatted, flushed std::cerr lines
# per command.  Profiling with that landing in a file partly measures the log.
# The default keeps it, because it is also how you diagnose a failed run.
if [ "${PERF_QUIET:-0}" = "1" ]; then
    SERVER_ERR=/dev/null
else
    SERVER_ERR=netmux.log
fi

# --- boot: process start until the port answers -----------------------------
BOOT_T0=$(python3 -c 'import time; print(repr(time.monotonic()))')
( cd "$WORK" && LD_LIBRARY_PATH="$BIN" ./bin/netmux -c netmux.conf > "$SERVER_ERR" 2>&1 ) &
NETMUX_PID=$!

UP=0
for _ in $(seq 1 60); do
    if python3 -c "import socket,sys; s=socket.socket(); s.settimeout(0.5); sys.exit(0 if s.connect_ex(('127.0.0.1',$PORT))==0 else 1)" 2>/dev/null; then
        UP=1
        break
    fi
    if ! kill -0 "$NETMUX_PID" 2>/dev/null; then
        echo "FAIL: netmux exited during startup:"
        sed 's/^/    /' "$WORK/netmux.log" 2>/dev/null | tail -20
        exit 1
    fi
    sleep 0.25
done
BOOT_T1=$(python3 -c 'import time; print(repr(time.monotonic()))')

if [ "$UP" -ne 1 ]; then
    echo "FAIL: netmux did not start listening on port $PORT."
    exit 1
fi
python3 -c "print('  %-16s %8.3fs' % ('boot', $BOOT_T1 - $BOOT_T0))"

# $NETMUX_PID is the backgrounded SUBSHELL, not netmux: `( cd X && cmd ) &`
# leaves the subshell in place as cmd's parent.  Attaching perf to it collects
# exactly nothing -- perf's inherit only covers children created AFTER the
# attach, and netmux is already running by the time the port answers.  Find
# the real process.
NETMUX_REAL=$(pgrep -P "$NETMUX_PID" 2>/dev/null | head -1)
[ -n "$NETMUX_REAL" ] || NETMUX_REAL=$NETMUX_PID
echo "  netmux pid $NETMUX_REAL (launcher $NETMUX_PID)"

# USER SPACE ONLY (`:u`).  kptr_restrict blocks /proc/kallsyms here, so kernel
# samples cannot be symbolized and land in one [unknown] bucket -- 72-100% of
# every phase on the first run, which drowns everything actionable.  The driver
# accounts for kernel time exactly from /proc/<pid>/stat instead, so nothing is
# hidden; it is just counted rather than sampled.
#
# netmux forks the slave and (when built with --enable-stubslave) the
# stubslave; perf inherits into children, so their cost is not invisible.
perf record -e task-clock:u -g --call-graph dwarf,16384 -F 997 -k CLOCK_MONOTONIC \
    -o "$WORK/perf.data" -p "$NETMUX_REAL" > "$WORK/perf.log" 2>&1 &
PERF_PID=$!
sleep 1              # let perf attach before the first phase starts

# Fail loudly rather than reporting empty phase profiles as if they were real.
if ! kill -0 "$PERF_PID" 2>/dev/null; then
    echo "WARN: perf record exited immediately:"
    sed 's/^/    /' "$WORK/perf.log" | tail -5
fi

# --- Drive the phases -------------------------------------------------------
echo "==> phases (scale=$SCALE)"
python3 "$SCRIPT_DIR/driver.py" 127.0.0.1 "$PORT" "$WORK/phases.json" \
    --scale "$SCALE" --pid "$NETMUX_REAL"
RC=$?

kill -INT "$PERF_PID" 2>/dev/null
wait "$PERF_PID" 2>/dev/null
PERF_PID=""

if [ "$RC" -ne 0 ]; then
    echo "FAIL: driver exited $RC"
    sed 's/^/    /' "$WORK/netmux.log" | tail -20
    exit 1
fi

# --- Slice the recording, one profile per phase ------------------------------
if [ ! -s "$WORK/perf.data" ]; then
    echo "FAIL: perf.data is empty."
    sed 's/^/    /' "$WORK/perf.log" | tail -10
    exit 1
fi

# A perf.data with a header but no samples slices into "no samples in window"
# for every phase, which reads exactly like a quiet server.  It is not: it is
# a broken attach.  Say so.
NSAMP=$(perf report -i "$WORK/perf.data" --header-only 2>/dev/null \
        | sed -n 's/^# sample duration : *\([0-9.]*\).*/\1/p')
if [ -z "$NSAMP" ] || [ "$NSAMP" = "0.000" ]; then
    echo "FAIL: perf.data contains no samples — the attach did not take."
    sed 's/^/    /' "$WORK/perf.log" | tail -10
    exit 1
fi

OUT="${PERF_OUT:-$REPO_ROOT/tests/profile/out}"
mkdir -p "$OUT"
cp "$WORK/perf.data" "$WORK/phases.json" "$WORK/netmux.log" "$OUT/" 2>/dev/null

echo
echo "==> READ THE CPU COLUMNS, NOT THE RATES."
echo "    Wall-clock throughput saturates around 6000 cmd/s once the server's"
echo "    debug log is out of the way (PERF_QUIET=1), and it saturates at the"
echo "    SAME rate for every phase -- at which point the driver, not netmux,"
echo "    is the limit.  The per-phase usr=/sys= figures come from"
echo "    /proc/<pid>/stat and keep discriminating after the rates stop."
echo
echo "==> per-phase profile (self time by source file)"
python3 - "$WORK/phases.json" "$WORK/perf.data" <<'PYEOF'
import json, subprocess, sys

phases = json.load(open(sys.argv[1]))["phases"]
data = sys.argv[2]

for p in phases:
    print()
    hdr = "%s  (%.3fs wall" % (p["name"], p["seconds"])
    if p.get("per_sec"):
        hdr += ", %.1f cmd/s, %.0f us each" % (p["per_sec"], p["usec_each"])
    hdr += "; server cpu %.2fs user + %.2fs sys" % (p["user_s"], p["sys_s"])
    print(hdr + ")  -- " + p["note"])
    print("    [percentages below are of USER time only; sys is counted above]")
    out = subprocess.run(
        ["perf", "report", "-i", data, "--stdio", "--no-children",
         "--sort", "srcfile", "-g", "none", "--percent-limit", "1.0",
         "--time", "%.6f,%.6f" % (p["start"], p["stop"])],
        capture_output=True, text=True).stdout
    rows = [l.strip() for l in out.splitlines()
            if l.strip().startswith(("0.", "1.", "2.", "3.", "4.", "5.",
                                     "6.", "7.", "8.", "9.")) or "%" in l[:10]]
    if not rows:
        print("    (no samples in window)")
    for r in rows[:12]:
        print("    " + r)
PYEOF

echo
echo "artifacts in $OUT (perf.data, phases.json, netmux.log)"
echo "  slice by hand:  perf report -i $OUT/perf.data --time <start>,<stop>"
