#!/bin/bash
#
#   bench.sh — Lua JIT throughput comparison (interpreter vs compiled).
#
#   NOT part of make test: this measures, it does not assert.  Timing
#   thresholds in CI are how flaky gates are born; the numbers here are
#   for humans deciding where optimization effort goes (#1309).
#
#   Method: one muxscript process per (mode, chunk).  Inside it, softcode
#   runs the chunk K times via iter() between secs(utc,6) marks, with a
#   null iter() of the same shape timed first; the reported per-call cost
#   is (lua_loop - null_loop) / K, which cancels iter/list overhead.  The
#   chunk is run once BEFORE the timed region so compilation (and the
#   compile cache) sit outside the measurement.
#
#   Config: lua_jit 0|1 with jit_eval_brackets 0 in both modes, so the
#   comparison isolates the Lua path from the softcode JIT (same
#   isolation argument as the EXEC tier in run.sh).
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
WORK="$SCRIPT_DIR/bench_work"

K=${LUAJIT_BENCH_K:-2000}

if [ ! -x "$BIN/muxscript" ]; then
    echo "SKIP: $BIN/muxscript not found (run 'make install' first)."
    exit 0
fi

# Executing shapes only -- benching a chunk that declines to the
# interpreter in jit mode measures nothing but the decline overhead.
# (That overhead is itself interesting, which is what the last entry is
# for: os.time errors on both routes and never compiles.)
CHUNKS=(
    'return mux.args[1] + mux.args[2]'
    'local a=mux.args[1]+0 return (a+1)*(a-1)'
    'local s=0 for i=1,100 do s=s+i end return s'
    'local s=1 while s<100 do s=s*2 end return s'
    'local t={} t[1]=7 t[2]=9 return t[1]+t[2]'
    'local t={a=3,b=4} return t.a+t.b'
    'local x=math.max(3,9) return x'
    'local x=string.rep("ab",2) return x'
)

bench_one() {
    local mode="$1" chunk="$2"
    rm -rf "$WORK"; mkdir -p "$WORK/data"
    ( cd "$WORK" || exit 1
      ln -s "$BIN" bin
      cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" . 2>/dev/null
      cat > p.conf <<EOF
input_database  data/p.db
output_database data/p.db.new
crash_database  data/p.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 2879
mud_name LuaJitBench
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
        echo "think WARM|[lua(probe/A,2,3)]"
        echo "think M0|[secs(utc,6)]"
        echo "think N|[strlen(iter(lnum(1,$K),##))]"
        echo "think M1|[secs(utc,6)]"
        echo "think L|[strlen(iter(lnum(1,$K),lua(probe/A,2,3)))]"
        echo "think M2|[secs(utc,6)]"
        echo "@shutdown"
      } > in.txt
      LD_LIBRARY_PATH="$BIN" "$BIN/muxscript" -g . -c p.conf < in.txt > out.log 2>&1
    )
    awk -F'|' -v k="$K" '
        /^M0\|/ { m0=$2 } /^M1\|/ { m1=$2 } /^M2\|/ { m2=$2 }
        END {
            if (m2 == "" || m1 == "" || m0 == "") { print "NA"; exit }
            us = ((m2-m1) - (m1-m0)) * 1000000 / k
            printf "%.2f", us
        }' "$WORK/out.log"
}

printf "%-52s %12s %12s %8s\n" "chunk (per-call cost, microseconds; K=$K)" "interp" "jit" "ratio"
for chunk in "${CHUNKS[@]}"; do
    ti=$(bench_one 0 "$chunk")
    tj=$(bench_one 1 "$chunk")
    ratio=$(awk -v a="$ti" -v b="$tj" 'BEGIN { if (a=="NA"||b=="NA"||b+0==0) print "NA"; else printf "%.2fx", a/b }')
    printf "%-52.52s %12s %12s %8s\n" "$chunk" "$ti" "$tj" "$ratio"
done
rm -rf "$WORK"
