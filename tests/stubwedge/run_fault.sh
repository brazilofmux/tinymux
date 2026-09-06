#!/bin/bash
#
#   run_fault.sh — #2244 stubslave protocol-fault regression.
#
#   The #2238 core forensics proved which branch ran during the farm's
#   22-day wedge: a frame header that failed validation in
#   Pipe_DecodeFrames.  That branch used to be silent -- no log, no
#   teardown, the same "false" the decoder returns when it merely needs
#   more bytes -- so the host looped back to its pump and waited for input
#   that could never make the stream parseable again.
#
#   This drives that exact fault through a real muxscript: bin/stubslave is
#   a stand-in that writes the header recovered verbatim from the farm's
#   stubslave core (type CALL, channel 0, length 0x02b90310) and then holds
#   the pipe open without ever answering -- the farm's peer, precisely.
#   muxscript's boot-time CreateInstance round-trip reads it.
#
#   Post-fix: the decoder marks the transport broken, Pipe_SendReceive gives
#   the pump one call to see it, script_pipepump logs the reason and stops
#   the stub, CreateInstance fails, and the game continues in-proc-only.
#   Pre-fix: the fault is swallowed and the process sits in the pump until
#   the #2239 stall watchdog gives up -- which is why the watchdog is set
#   far beyond this harness's timeout: a PASS here has to come from the
#   fault path, not from the watchdog rescuing it.
#
#   SKIP (green) without muxscript or on a build with no stubslave, same
#   policy as run_wedge.sh.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
WORK="$SCRIPT_DIR/work"

# Well beyond the harness timeout, so the stall watchdog cannot be what
# resolves the run.
STALL_MS=120000
if command -v timeout >/dev/null 2>&1; then
    TO="timeout 40"
elif command -v gtimeout >/dev/null 2>&1; then
    TO="gtimeout 40"
else
    TO=""
fi

if [ ! -x "$BIN/muxscript" ]; then
    echo "SKIP: $BIN/muxscript not found (run 'make install' first)."
    exit 0
fi
if [ ! -x "$BIN/stubslave" ]; then
    echo "SKIP: $BIN/stubslave not built (configure --enable-stubslave)."
    exit 0
fi

rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/bin"
( cd "$WORK" || exit 1
  # A real bin/ of symlinks, so only stubslave can be swapped for the
  # stand-in; muxscript execs "bin/stubslave" relative to its cwd.
  for f in "$BIN"/*; do
      ln -s "$f" "bin/$(basename "$f")"
  done
  rm -f bin/stubslave
  cat > bin/stubslave <<'STUB'
#!/bin/bash
# The farm stubslave's last header, byte for byte:
#   00 | 00 00 00 00 | 10 03 b9 02   (CALL, channel 0, length 45679376)
printf '\000\000\000\000\000\020\003\271\002'
# Then stay alive with the pipe open until the parent closes its end.
exec cat > /dev/null
STUB
  chmod +x bin/stubslave
  cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" . 2>/dev/null
  cat > p.conf <<PCONF
input_database  data/p.db
output_database data/p.db.new
crash_database  data/p.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 0
mud_name StubFault
include alias.conf
include compat.conf
PCONF
)

cd "$WORK" || exit 1
ulimit -c 0 2>/dev/null || true
FIFO="in.fifo"; rm -f "$FIFO"; mkfifo "$FIFO"

LD_LIBRARY_PATH="$BIN" TINYMUX_STUB_STALL_MS="$STALL_MS" \
    $TO "$BIN/muxscript" -g . -c p.conf < "$FIFO" > out.log 2> err.log &
mpid=$!
exec 3>"$FIFO"

# The fault happens during boot, before any command is read.  Wait for the
# boot to resolve either way, then drive commands through it.
for i in $(seq 1 100); do
    if grep -q "protocol error\|management interface unavailable\|stubslave attached" err.log 2>/dev/null; then
        break
    fi
    kill -0 "$mpid" 2>/dev/null || break
    sleep 0.1
done

echo 'think AFTERFAULT|still-alive' >&3
echo '@shutdown' >&3
exec 3>&-

wait "$mpid"; rc=$?

fail=0
echo "   exit rc=$rc"
case "$rc" in
    0)   echo "   PASS: server survived the corrupt frame and shut down cleanly" ;;
    124) echo "   FAIL: hung on the corrupt frame (the protocol-error branch is silent again)"; fail=1 ;;
    *)   echo "   FAIL: unexpected exit rc=$rc"; fail=1 ;;
esac

if grep -q "module transport protocol error: frame type 0, channel 0, length 45679376" err.log 2>/dev/null; then
    echo "   PASS: the fault was named, with the farm's exact length field"
else
    echo "   FAIL: no protocol-error diagnostic — the branch that ran on the farm is still silent"
    fail=1
fi

if grep -qi "channel stalled" err.log out.log 2>/dev/null; then
    echo "   FAIL: the stall watchdog fired — recovery came from the timeout, not the fault path"
    fail=1
else
    echo "   PASS: the stall watchdog did not fire; the fault path resolved it"
fi

if grep -q "AFTERFAULT|still-alive" out.log 2>/dev/null; then
    echo "   PASS: command processing continued without the stubslave"
else
    echo "   FAIL: no command output after the fault — the game did not continue"
    fail=1
fi

exit $fail
