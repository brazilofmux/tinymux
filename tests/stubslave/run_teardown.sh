#!/bin/bash
#
#   run_teardown.sh — #1939 muxscript stubslave-teardown regression.
#
#   #1939 was a deterministic stack overflow that masqueraded as a
#   nondeterministic, "load-sensitive" SIGSEGV in `make test-lua-ecall`.
#   muxscript's shutdown_stubslave_parent() calls ShutdownSlave(), which
#   drives a COM round-trip through Pipe_SendReceive -> the pump
#   (script_pipepump).  When the slave has already exited, that pump's
#   pipe write fails and script_pipepump calls shutdown_stubslave_parent()
#   AGAIN -- while a ShutdownSlave() is still on the stack and
#   g_pISlaveControl is still non-null -- so it launches another
#   ShutdownSlave(), and the two recurse until the stack guard page faults.
#
#   The whole muxscript<->stubslave teardown path had no deterministic
#   coverage: the crash only surfaced probabilistically through the luajit
#   harness, and only on --enable-stubslave builds (the configure default
#   omits it, so CI never exercised this path at all).  This test forces
#   the failure deterministically: boot muxscript, kill the stubslave child
#   so the pipe is dead, then @shutdown.  Pre-fix that recurses to SIGSEGV;
#   post-fix it exits cleanly.
#
#   SKIP (green) when muxscript is missing or when the build has no
#   stubslave support (no "stubslave attached" line) -- same policy as the
#   other feature-gated harnesses.  A build without --enable-stubslave
#   cannot exhibit the bug, so there is nothing to assert.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
WORK="$SCRIPT_DIR/work"

if command -v timeout >/dev/null 2>&1; then
    TO="timeout 30"
elif command -v gtimeout >/dev/null 2>&1; then
    TO="gtimeout 30"
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

# --- build a minimal game dir -------------------------------------------
setup_work() {
    rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs" "$WORK/text"
    ( cd "$WORK" || exit 1
      ln -s "$BIN" bin
      cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" . 2>/dev/null
      cat > p.conf <<PCONF
input_database  data/p.db
output_database data/p.db.new
crash_database  data/p.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 0
mud_name StubTeardown
include alias.conf
include compat.conf
PCONF
    )
}

# Run one teardown scenario.
#   $1 = "kill"  -> kill the stubslave child before @shutdown (the #1939 trigger)
#        "clean" -> shut down with the slave still alive (baseline sanity)
# Echoes: "<rc> <slavepid-or-none> <attached|noattach>"
run_case() {
    local mode="$1"
    setup_work
    ( cd "$WORK" || exit 1
      # No cores: a pre-fix regression is a SIGSEGV (rc 139); we assert on the
      # exit code, and a 25 MB core per run is just disk churn.
      ulimit -c 0 2>/dev/null || true
      FIFO="in.fifo"; rm -f "$FIFO"; mkfifo "$FIFO"
      LD_LIBRARY_PATH="$BIN" $TO "$BIN/muxscript" -g . -c p.conf \
          < "$FIFO" > out.log 2> err.log &
      local mpid=$!
      exec 3>"$FIFO"
      echo '@create probe' >&3
      echo 'think READY|ok' >&3
      # Wait for the stubslave to attach (or decide it never will).
      local slave="" i
      for i in $(seq 1 60); do
          slave=$(grep -oE 'stubslave attached \(pid [0-9]+\)' err.log 2>/dev/null \
                  | grep -oE '[0-9]+' | head -1)
          [ -n "$slave" ] && break
          # If muxscript already exited without attaching, stop waiting.
          kill -0 "$mpid" 2>/dev/null || break
          sleep 0.1
      done
      if [ -z "$slave" ]; then
          # No stubslave in this build/run; nothing to test.
          echo '@shutdown' >&3 2>/dev/null || true
          exec 3>&-
          wait "$mpid" 2>/dev/null
          echo "0 none noattach" > rc.out
          return 0
      fi
      if [ "$mode" = "kill" ]; then
          kill -9 "$slave" 2>/dev/null
          # Let the kill land so the pipe is dead before ShutdownSlave writes.
          sleep 0.3
      fi
      echo '@shutdown' >&3
      exec 3>&-
      wait "$mpid"; local rc=$?
      echo "$rc $slave attached" > rc.out
    )
    cat "$WORK/rc.out" 2>/dev/null || echo "99 none error"
}

fail=0

echo "== #1939: kill stubslave child, then @shutdown =="
read -r rc slave att <<<"$(run_case kill)"
if [ "$att" = "noattach" ]; then
    echo "SKIP: muxscript did not attach a stubslave (in-proc-only build/run)."
    exit 0
fi
echo "   stubslave pid=$slave  exit rc=$rc"
case "$rc" in
    0)   echo "   PASS: clean teardown after slave death" ;;
    139) echo "   FAIL: SIGSEGV in teardown (the #1939 recursion is back)"; fail=1 ;;
    124) echo "   FAIL: timed out (teardown hung)"; fail=1 ;;
    *)   echo "   FAIL: unexpected exit rc=$rc"; fail=1 ;;
esac

echo "== baseline: @shutdown with the slave alive =="
read -r rc2 slave2 att2 <<<"$(run_case clean)"
echo "   stubslave pid=$slave2  exit rc=$rc2"
if [ "$rc2" = "0" ]; then
    echo "   PASS: clean teardown"
else
    echo "   FAIL: baseline teardown exited rc=$rc2"; fail=1
fi

# Leave no orphans behind.
pkill -9 -f "$BIN/stubslave" 2>/dev/null || true
rm -rf "$WORK" 2>/dev/null || true

if [ "$fail" -ne 0 ]; then
    echo "RESULT: FAIL"
    exit 1
fi
echo "RESULT: PASS"
exit 0
