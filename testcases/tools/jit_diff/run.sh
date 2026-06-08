#!/bin/sh
#
# JIT differential fuzzer.
#
# Generates random nested softcode and compares each expression's result from
# the JIT (via an @if condition, which mux_exec compiles) against the
# interpreter (via an eval-bracket in a @pemit arg, which makes mux_exec bail
# the JIT).  Any mismatch is a JIT-vs-interpreter divergence — a correctness
# bug in one of the tier2 wrappers or the JIT lowering.
#
# Usage:  run.sh [count] [batch]      (defaults: 200, 50)
#         SEED=7 run.sh 400           (vary the corpus)
#
# Requires a built tree (mux/game/bin/muxscript + engine.so).  Build with
# `make install` from the repo root first.
#
# Exit status: 0 = no logic divergence, 1 = divergences found, 2 = setup error.
#
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
BIN="$REPO/mux/game/bin"

COUNT=${1:-200}
BATCH=${2:-50}

if [ ! -x "$BIN/muxscript" ] || [ ! -e "$BIN/engine.so" ]; then
    echo "ERROR: $BIN/muxscript or engine.so missing — run 'make install' first." >&2
    exit 2
fi

# Runtime timeout command (cross-platform; macOS often has gtimeout).
if command -v timeout >/dev/null 2>&1; then TIMEOUT="timeout 90"
elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT="gtimeout 90"
else TIMEOUT=""; fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/jitdiff.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/data"
ln -sfn "$BIN" "$WORK/bin"
printf 'input_database\tdata/exp.db\noutput_database\tdata/exp.db.new\n' > "$WORK/exp.conf"

DYLD_LIBRARY_PATH="$BIN"; export DYLD_LIBRARY_PATH
LD_LIBRARY_PATH="$BIN";   export LD_LIBRARY_PATH

python3 "$SCRIPT_DIR/gen.py" "$COUNT" "$BATCH" "$WORK" || exit 2

: > "$WORK/results.txt"
for bf in "$WORK"/b*.txt; do
    rm -f "$WORK"/data/exp.sqlite*
    $TIMEOUT "$BIN/muxscript" -g "$WORK" -c exp.conf < "$bf" > "$bf.log" 2>&1
    grep -aoE "[JI]~[0-9]+~[0-9A-F]*~[0-9A-F]*~" "$bf.log" >> "$WORK/results.txt"
    if grep -aq "Run away" "$bf.log"; then
        echo "WARNING: queue overflow in $(basename "$bf") — lower the batch size." >&2
    fi
done

# Compare. Fields: side~id~rawsha~strippedsha
# LOGIC   = stripped shas differ (real semantic divergence)
# COLOR   = raw differ but stripped match (internal color-encoding only)
awk -F'~' '
    $1=="J" { jr[$2]=$3; js[$2]=$4; seen[$2]=1 }
    $1=="I" { ir[$2]=$3; is[$2]=$4; seen[$2]=1 }
    END {
        for (id in seen) {
            if (jr[id]=="" || ir[id]=="") { print "MISSING " id; miss++; continue }
            if (js[id] != is[id])      { print "LOGIC " id; logic++ }
            else if (jr[id] != ir[id]) { print "COLOR " id; color++ }
        }
        printf "----\n%d compared, %d LOGIC, %d COLOR-encoding, %d missing\n",
               length(seen), logic+0, color+0, miss+0 > "/dev/stderr"
    }
' "$WORK/results.txt" | sort > "$WORK/verdict.txt"

# Report divergent expressions (LOGIC first — those are the real bugs).
status=0
for tag in LOGIC COLOR MISSING; do
    ids=$(awk -v t="$tag" '$1==t{print $2}' "$WORK/verdict.txt")
    [ -z "$ids" ] && continue
    [ "$tag" = LOGIC ] && status=1
    echo "=== $tag ==="
    for id in $ids; do
        awk -F'\t' -v i="$id" '$1==i{print i": "$2}' "$WORK/manifest.txt"
    done
done
exit $status
