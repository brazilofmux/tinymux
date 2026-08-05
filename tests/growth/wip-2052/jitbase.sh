#!/bin/bash
#
# jitbase.sh — measure mux_exec cost with benchmark(), which is NOT JIT-gated.
#
# benchmark(<expr>,<iters>) loops mux_exec and returns elapsed seconds, so the
# same script run on a --enable-jit tree and a non-JIT tree compares the real
# dispatch path both ways.  That is the comparison docs/survey-perf-pass-2026-08
# records as unmeasured: rvbench's cached= skips mux_exec, its native= dispatches
# TO the JIT, and astbench's ast= calls ast_eval_node directly.
#
# Usage: jitbase.sh <label>
set -u
LABEL=${1:?need a label}
R=/Users/sdennis/tinymux
B="$R/mux/game/bin"
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/g.d"; ln -s "$B" "$W/bin"
cp "$R/mux/game/alias.conf" "$R/mux/game/compat.conf" "$W/"
cat > "$W/g.conf" <<'EOF'
input_database g.d/g.db
output_database g.d/g.db.new
crash_database g.d/g.db.CRASH
mail_database g.d/mail.db
comsys_database g.d/comsys.db
port 2864
mud_name JitBase
command_quota_increment 1000000
command_quota_max 1000000
player_queue_limit 100000
function_invocation_limit 1000000
include alias.conf
include compat.conf
EOF

# expr|iters  — scalars get many iterations, list shapes few.
CASES='add(1,2)|10000
add(rand(100),1)|10000
bound(rand(100),10,90)|10000
strcat(abc,def)|10000
ladd(lnum(100))|2000
iter(lnum(10),1)|2000
iter(lnum(50),1)|1000
iter(lnum(200),1)|300
iter(lnum(500),1)|100
iter(lnum(1000),1)|50
iter(lnum(2000),1)|20'

{
  # Shape assertions first: never time a list you have not counted.
  echo "think [words(lnum(100))]/[words(lnum(500))]/[words(lnum(2000))] @@SHAPE"
  # Warm-up pass, discarded.
  while IFS='|' read -r e n; do
      [ -n "$e" ] && echo "think [benchmark($e,3)] @@WARM"
  done <<< "$CASES"
  while IFS='|' read -r e n; do
      [ -n "$e" ] && echo "think [benchmark($e,$n)] @@X|$e|$n"
  done <<< "$CASES"
  echo "@shutdown"
} | DYLD_LIBRARY_PATH="$B" LD_LIBRARY_PATH="$B" \
    "$B/muxscript" -g "$W" -c g.conf 2>&1 \
  | grep -aE '@@(X|SHAPE)' \
  | awk -v label="$LABEL" '
      /@@SHAPE/ { sub(/ *@@SHAPE.*/,""); print "  shape(100/500/2000): " $0; next }
      {
        line=$0
        i=index(line,"@@X|"); if (i==0) next
        val=substr(line,1,i-1); rest=substr(line,i+4)
        split(rest,f,"|"); e=f[1]; n=f[2]+0
        gsub(/ /,"",val)
        printf "  %-28s %10.2f us/call\n", e, (val+0)*1e6/n
      }'
