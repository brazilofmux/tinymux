#!/bin/bash
#
#   run.sh — an unreadable configuration file must be fatal (#1601).
#
#   cf_read() returns -1 when the top-level config cannot be read, and for
#   years its only call site -- CGameEngine::LoadGame -- discarded that.  The
#   game then came up on compiled-in defaults and reported success.  On
#   netmux that meant a mistyped -c path produced a live server listening on
#   the default port, serving whatever database the defaults named, while the
#   real one sat untouched; the sole diagnostic was one log line between two
#   others that read like success.  On muxscript it meant a harness could not
#   tell a green run against the intended database from a green run against
#   an empty default one -- which is what #1598 was circling when it blamed a
#   hang.
#
#   Half of what this file pins is the failures.  The other half is the two
#   cases that must STAY non-fatal, and those are the ones actually at risk:
#   a future reader who sees "unreadable config is fatal" and generalizes it
#   to "any config problem is fatal" breaks both.
#
#     * An unrecognized directive is not fatal.  Games carry config files
#       forward across releases, and refusing to boot over one stale line is
#       worse than logging it.  cf_include calls cf_set() per line and
#       discards its return, which is what makes this work; that discard is
#       load-bearing, not an oversight to match up with the other one.
#
#     * An empty file is not fatal.  It opens, yields no directives, and is
#       the supported way to ask for the compiled-in defaults on purpose --
#       the escape hatch that makes the fatal case affordable.
#
#   The good-config case asserts the values actually took effect rather than
#   just that the process exited 0.  Exit status alone cannot distinguish
#   "read the config" from "silently used defaults" -- that is precisely the
#   bug, and a test that only checked rc would have passed against it.
#
#   netmux is not driven here.  It shares the defect through the same
#   LoadGame call, but booting it needs a port and a database, and muxscript
#   reaches the identical code with neither.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
WORK="$SCRIPT_DIR/work"

if command -v timeout >/dev/null 2>&1; then
    TIMEOUT="timeout 90"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT="gtimeout 90"
else
    TIMEOUT=""
fi

if [ ! -x "$BIN/muxscript" ]; then
    echo "SKIP: $BIN/muxscript not found (run 'make install' first)."
    exit 0
fi

fails=0

# Build a throwaway game and run muxscript against config file $1.  The
# config itself is written by the caller into $WORK before this runs.
setup() {
    rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs" "$WORK/text"
    ( cd "$WORK" || exit 1
      ln -s "$BIN" bin
      cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" . 2>/dev/null
    )
    cat > "$WORK/good.conf" <<'EOF'
input_database  data/p.db
output_database data/p.db.new
crash_database  data/p.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 2897
mud_name ConfigProbe
include alias.conf
include compat.conf
EOF
}

# $1 = label, $2 = -c argument, $3 = expected "ok" or "fatal"
# Echoes rc and captures output in $WORK/out.log.
probe() {
    local label="$1" conf="$2" expect="$3" rc
    ( cd "$WORK" || exit 1
      echo 'think MUDNAME=[config(mud_name)]' > in.txt
      LD_LIBRARY_PATH="$BIN" $TIMEOUT "$BIN/muxscript" -g . -c "$conf" \
          < in.txt > out.log 2>&1
      echo "$?" > rc.txt )
    rc=$(cat "$WORK/rc.txt" 2>/dev/null || echo "no-rc")

    if [ "$expect" = "fatal" ]; then
        if [ "$rc" = "0" ]; then
            echo "FAIL: $label -- exited 0; an unreadable config must be fatal"
            sed -n '1,4p' "$WORK/out.log"
            fails=$((fails + 1))
            return
        fi
        if ! grep -q "cannot read configuration file" "$WORK/out.log"; then
            echo "FAIL: $label -- rc=$rc but no diagnostic naming the config"
            sed -n '1,4p' "$WORK/out.log"
            fails=$((fails + 1))
            return
        fi
        echo "ok: $label (rc=$rc, diagnostic names the file)"
    else
        if [ "$rc" != "0" ]; then
            echo "FAIL: $label -- rc=$rc; this case must remain non-fatal"
            sed -n '1,4p' "$WORK/out.log"
            fails=$((fails + 1))
            return
        fi
        echo "ok: $label (rc=0)"
    fi
}

setup

# 1. The good config is read AND applied.  mud_name proves it: the default is
#    "MUX", so reading back "ConfigProbe" cannot happen on the defaults path.
probe "good config exits 0" "good.conf" "ok"
if ! grep -q "MUDNAME=ConfigProbe" "$WORK/out.log"; then
    echo "FAIL: good config -- mud_name did not take effect (defaults used?)"
    grep -m1 MUDNAME "$WORK/out.log" || echo "  (no MUDNAME line at all)"
    fails=$((fails + 1))
else
    echo "ok: good config applied (mud_name=ConfigProbe, not the default MUX)"
fi

# 2. A config file that does not exist is fatal.
probe "missing config is fatal" "nosuch.conf" "fatal"

# 3. A directory is fatal.  fopen() succeeds on a directory and only the read
#    fails, so before #1601 this was indistinguishable from an empty file.
mkdir -p "$WORK/adir"
probe "directory as config is fatal" "adir" "fatal"

# 4. An unrecognized directive must NOT be fatal (forward compatibility).
printf 'no_such_directive_at_all yes\nmud_name ConfigProbe\n' > "$WORK/stale.conf"
probe "unknown directive stays non-fatal" "stale.conf" "ok"

# 5. An empty file must NOT be fatal -- the documented way to ask for defaults.
: > "$WORK/empty.conf"
probe "empty config stays non-fatal" "empty.conf" "ok"

if [ "$fails" -eq 0 ]; then
    echo "=== config: all cases passed ==="
    rm -rf "$WORK"
    exit 0
fi
echo "=== config: $fails failure(s); artifacts kept in $WORK ==="
exit 1
