#!/bin/sh
#
# 2.13 <-> 2.14 parser parity jig.
#
# MUSH function-call recognition is context sensitive in ways a tokenizer
# cannot decide on its own: `add(` is a call and `foo(` is not, and only a
# function-table lookup separates them.  The grammar is not well-formed,
# so there is no tidy rule to check against — the specification is what
# 2.13 actually does.  This harness measures that instead of theorising
# about it.
#
# It runs a corpus of parser shapes through up to three engines, all
# driven identically (a real netmux on a scratch port, over a socket, via
# tests/parity213/probe.py) so that no difference can be an artifact of
# one side running under muxscript:
#
#   2.14 JIT   this tree, default conf (jit_eval_brackets on)
#   2.14 AST   this tree, jit_eval_brackets 0
#   2.13       reference, if a built 2.13 tree is available
#
# The 2.14-internal comparison always runs and needs nothing extra: the
# two routes disagreeing with each other is a defect regardless of what
# 2.13 says.  The 2.13 leg is added when a reference tree is found.
#
# Finding the 2.13 tree:
#   MUX213_ROOT=/path/to/2.13/checkout   (must be built)
# otherwise a few conventional locations are tried.  Build one with:
#   git worktree add /path/to/2.13 origin/release/2.13
#   cd /path/to/2.13/mux/src && ./configure --enable-realitylvls \
#       --enable-wodrealms && make install
# Note 2.13 builds from mux/src and has no muxscript, which is why this
# harness talks to a live server.
#
# Usage:  run.sh [corpus-file]
# Exit:   0 = no divergence, 1 = divergence, 2 = setup error.
#
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO/mux/game/bin"
CORPUS=${1:-$SCRIPT_DIR/corpus.txt}

if [ ! -x "$BIN/netmux" ]; then
    echo "SKIP: $BIN/netmux not found — run 'make install' from the repo root first."
    exit 2
fi
if [ ! -f "$CORPUS" ]; then
    echo "ERROR: corpus not found: $CORPUS" >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 not available."
    exit 2
fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/parity213.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' EXIT

free_port() {
    python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()'
}

# probe_engine <tree-root> <extra-conf-line> <outfile> <label>
probe_engine() {
    _root=$1; _extra=$2; _out=$3; _label=$4
    _bin="$_root/mux/game/bin"
    _db="$_root/mux/game/data/netmux.db"
    if [ ! -x "$_bin/netmux" ] || [ ! -f "$_db" ]; then
        echo "  $_label: unavailable (no netmux or starter db under $_root)"
        return 1
    fi
    _w="$WORK/$_label"
    mkdir -p "$_w/data"
    ln -sfn "$_bin" "$_w/bin"
    cp "$_db" "$_w/data/netmux.db"
    _p=$(free_port) || return 1
    {
        printf 'input_database\tdata/netmux.db\n'
        printf 'output_database\tdata/netmux.db.new\n'
        printf 'crash_database\tdata/netmux.db.CRASH\n'
        printf 'port\t%s\n' "$_p"
        [ -n "$_extra" ] && printf '%s\n' "$_extra"
    } > "$_w/t.conf"

    ( cd "$_w" && LD_LIBRARY_PATH="$_bin" ./bin/netmux -c t.conf > netmux.log 2>&1 &
      echo $! > "$_w/pid" )

    _up=0
    for _i in $(seq 1 40); do
        if python3 -c "import socket,sys;s=socket.socket();s.settimeout(0.4);sys.exit(0 if s.connect_ex(('127.0.0.1',$_p))==0 else 1)" 2>/dev/null; then
            _up=1; break
        fi
    done
    if [ "$_up" -eq 0 ]; then
        echo "  $_label: FAILED to start (see $_w/netmux.log)"
        [ -f "$_w/pid" ] && kill "$(cat "$_w/pid")" 2>/dev/null
        return 1
    fi

    python3 "$SCRIPT_DIR/probe.py" "$_p" "$CORPUS" > "$_out"
    [ -f "$_w/pid" ] && kill "$(cat "$_w/pid")" 2>/dev/null
    sleep 0.4
    _n=$(wc -l < "$_out")
    _blank=$(grep -c '<NO-OUTPUT>' "$_out" || true)
    echo "  $_label: $_n shapes ($_blank with no output)"
    return 0
}

# report <fileA> <fileB> <labelA> <labelB>
report() {
    paste "$1" "$2" | awk -F'\t' -v A="$3" -v B="$4" '
        { if ($2 != $4) { printf "    %-22s %s=%-28s %s=%s\n", $1, A, "\047"$2"\047", B, "\047"$4"\047"; d++ } else s++ }
        END { printf "  %s vs %s: %d agree, %d differ\n", A, B, s+0, d+0 }
    '
    paste "$1" "$2" | awk -F'\t' '{ if ($2 != $4) d++ } END { exit (d>0) }'
}

echo "==> 2.13/2.14 parser parity jig"
echo "    corpus: $CORPUS ($(grep -cvE '^[[:space:]]*(#|$)' "$CORPUS") shapes)"
echo

echo "Probing engines:"
probe_engine "$REPO" ""                       "$WORK/jit.txt" "2.14-JIT" || exit 2
probe_engine "$REPO" "jit_eval_brackets	0"    "$WORK/ast.txt" "2.14-AST" || exit 2

M213=""
if [ -n "${MUX213_ROOT:-}" ]; then
    M213="$MUX213_ROOT"
else
    for _c in "$HOME/tinymux-213" "$HOME/mux-2.13" "/tmp/mux213"; do
        [ -x "$_c/mux/game/bin/netmux" ] && { M213="$_c"; break; }
    done
fi
HAVE213=0
if [ -n "$M213" ]; then
    if probe_engine "$M213" "" "$WORK/r213.txt" "2.13"; then
        HAVE213=1
    fi
else
    echo "  2.13: not found (set MUX213_ROOT to a built 2.13 tree to enable)"
fi

echo
rc=0

echo "Internal consistency (no 2.13 needed):"
report "$WORK/jit.txt" "$WORK/ast.txt" "JIT" "AST" || rc=1

if [ "$HAVE213" -eq 1 ]; then
    echo
    echo "Against the 2.13 reference:"
    report "$WORK/r213.txt" "$WORK/jit.txt" "2.13" "JIT" || rc=1
    echo
    report "$WORK/r213.txt" "$WORK/ast.txt" "2.13" "AST" || rc=1
fi

echo
if [ "$rc" -eq 0 ]; then
    echo "OK: no divergence"
else
    echo "DIVERGENCE FOUND — see above."
    echo
    echo "Note: a divergence here is a finding, not automatically a bug."
    echo "The 2.13 grammar is not well-formed and some of its behaviour is"
    echo "organic; deciding which shapes should change is a design call."
fi
exit $rc
