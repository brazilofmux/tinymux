#!/bin/bash
#
#   run.sh — fault containment for the Lua JIT's ECALL table ops (#1423).
#
#   A Lua error raised inside an ECALL has no protected call frame above it,
#   so luaD_throw() reached the default panic handler and abort()ed the whole
#   process.  `local t={10,20,30} return t[2]` was enough: the lowering handed
#   lua_geti() something that was not a table, Lua raised "attempt to index a
#   ? value", and muxscript died with SIGABRT and a core.
#
#   The contract this pins: a Lua chunk must never be able to kill the
#   process.  Getting the wrong answer is a different (and much less serious)
#   bug, tracked separately — see the divergence reporting below.
#
#   Note the config.  Under the default settings the Lua JIT declines every
#   run, because fun_lua is ECALLed from inside a DBT program and
#   run_cached_program refuses a nested run (#1326).  So `lua_jit 1` alone
#   exercises nothing on the compiled path; `jit_eval_brackets 0` is what
#   actually lets the compiled Lua code execute.  Without both, this test
#   would pass no matter how broken the ECALLs were.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
WORK="$SCRIPT_DIR/work"

if command -v timeout >/dev/null 2>&1; then
    TIMEOUT="timeout 60"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT="gtimeout 60"
else
    TIMEOUT=""
fi

if [ ! -x "$BIN/muxscript" ]; then
    echo "SKIP: $BIN/muxscript not found (run 'make install' first)."
    exit 0
fi

# Chunks that reach the raising Lua C API calls in eval_ecall: table index by
# integer and by name, table store both ways, and the array pin/unpin path.
# t[2] on the currently-mislowered constructor is the one that aborted.
CASES=(
    'local t={10,20,30} return t[2]'
    'local t={a=7} return t.a'
    'local t={} t[1]=5 return t[1]'
    'local t={} t.x=5 return t.x'
    'local t={1,2,3} return #t'
    'local t={1,2,3} table.insert(t,4) return #t'
    'return mux.args[1] + mux.args[2]'
)

# Preserve a failed run's directory instead of deleting it (#1446).
#
# The crashing process's cwd IS $WORK, so the core lands there, next to
# out.log and the scratch database.  The original version of this script
# removed $WORK at the top of every run_one() and again on the way out, which
# meant a chunk could be reported as "died (rc=139)" with nothing left to
# diagnose it -- and that is exactly what blocked the investigation of #1433,
# an intermittent crash at roughly 0.4% per process, where a backtrace is the
# whole game.
#
# Same contract as Makesmoke/Smoke and their smoke.fail/: green runs clean up,
# failures leave evidence and say where it is.
#
# What survives is in.txt and p.conf -- the exact chunk and the exact config --
# plus out.log, rc.txt and the scratch database, which together are an exact
# reproduction recipe.  A core may or may not join them: the ulimit below
# permits one, but where it lands is /proc/sys/kernel/core_pattern's business,
# and a pattern that pipes to a crash reporter (as this box does) will not put
# it in the cwd.  The recipe is the part we can guarantee.
#
# Echoes the preserved path, or nothing.  run_one() is invoked inside a
# command substitution, so this cannot report back through a global -- that
# would be set in the subshell and lost.
keep_work() {
    local rc="$1" tag="$2"
    if [ "$rc" = "0" ] || [ ! -d "$WORK" ]; then
        return 0
    fi
    local dest="$SCRIPT_DIR/work.fail.$tag.$$"
    rm -rf "$dest"
    if mv "$WORK" "$dest" 2>/dev/null; then
        printf '%s' "$dest"
    fi
}

# $1 = lua_jit setting, $2 = chunk, $3 = tag for a preserved workdir.
# Echoes "<rc>|<result>|<kept-dir-or-empty>".
run_one() {
    local mode="$1" chunk="$2" tag="${3:-run}"
    rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs" "$WORK/text"
    ( cd "$WORK" || exit 1
      # A core is only useful if the shell allowed one to be written.
      ulimit -c unlimited 2>/dev/null || true
      ln -s "$BIN" bin
      cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" . 2>/dev/null
      cat > p.conf <<EOF
input_database  data/p.db
output_database data/p.db.new
crash_database  data/p.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 2879
mud_name LuaEcall
command_quota_increment 200000
command_quota_max 200000
include alias.conf
include compat.conf
lua_jit $mode
jit_eval_brackets 0
EOF
      {
        echo "@create probe"
        echo "&A probe=$chunk"
        echo "think HASJIT|[strmatch(jitstats(),*eval_attempts=*)]"
        echo "think RESULT|[lua(probe/A,2,3)]"
        echo "@shutdown"
      } > in.txt
      LD_LIBRARY_PATH="$BIN" $TIMEOUT "$BIN/muxscript" -g . -c p.conf < in.txt > out.log 2>&1
      echo "$?" > rc.txt
    )
    local rc res kept
    rc=$(cat "$WORK/rc.txt" 2>/dev/null || echo 99)
    res=$(grep -oE "^RESULT\|.*" "$WORK/out.log" 2>/dev/null | head -1)
    res=${res#RESULT|}
    # Everything is read out of $WORK before keep_work moves it aside.
    kept=$(keep_work "$rc" "$tag")
    echo "$rc|$res|$kept"
}

# Split a run_one result into rc / result / kept path.
split3() {
    R_RC=${1%%|*}
    local rest=${1#*|}
    R_RES=${rest%%|*}
    R_KEPT=${rest#*|}
}

# Preflight: no JIT in this build means nothing here is meaningful.
#
# rc is checked before HASJIT: on a crash the workdir has been moved aside, so
# grepping $WORK would find nothing and we would report SKIP for what is
# actually a dead baseline.
split3 "$(run_one 1 'return 1' baseline)"
if [ "$R_RC" != "0" ]; then
    echo "FAIL: baseline 'return 1' did not survive (rc=$R_RC)."
    [ -n "$R_KEPT" ] && echo "      evidence: $R_KEPT"
    exit 1
fi
if ! grep -q "^HASJIT|1" "$WORK/out.log" 2>/dev/null; then
    echo "SKIP: engine JIT not present in this build (configure --enable-jit)."
    rm -rf "$WORK"
    exit 0
fi

fails=0
diverged=0
printf '%-46s %-6s %-10s %-10s %s\n' chunk rc interp jit verdict
n=0
for chunk in "${CASES[@]}"; do
    n=$((n+1))
    split3 "$(run_one 0 "$chunk" "interp$n")"; irc=$R_RC; ires=$R_RES; ikept=$R_KEPT
    split3 "$(run_one 1 "$chunk" "jit$n")";    jrc=$R_RC; jres=$R_RES; jkept=$R_KEPT

    verdict="ok"
    if [ "$jrc" != "0" ]; then
        # The whole point of #1423: a chunk must not be able to kill us.
        verdict="FAIL: compiled path died (rc=$jrc)"
        [ -n "$jkept" ] && verdict="$verdict evidence=$jkept"
        fails=$((fails+1))
    elif [ "$irc" != "0" ]; then
        verdict="FAIL: interpreter died (rc=$irc)"
        [ -n "$ikept" ] && verdict="$verdict evidence=$ikept"
        fails=$((fails+1))
    elif [ "$ires" != "$jres" ]; then
        # Wrong answers are real bugs but they belong to #1421/#1422/#1424/
        # #1425, not to fault containment.  Report, don't fail: this test
        # must not go red for a bug it does not own.
        verdict="diverges (see #1424) interp=$ires jit=$jres"
        diverged=$((diverged+1))
    fi
    printf '%-46s %-6s %-10s %-10s %s\n' "${chunk:0:46}" "$jrc" "${ires:0:10}" "${jres:0:10}" "$verdict"
done

rm -rf "$WORK"
echo
echo "chunks: ${#CASES[@]}   crashes: $fails   divergences: $diverged"
if [ "$fails" -ne 0 ]; then
    echo "=== tests/luajit: FAILED ($fails chunk(s) killed the process) ==="
    echo "Preserved workdirs (core, out.log, scratch db) for each failure:"
    ls -d "$SCRIPT_DIR"/work.fail.*."$$" 2>/dev/null | sed 's/^/  /'
    exit 1
fi
echo "=== tests/luajit: PASSED (no chunk killed the process) ==="
exit 0
