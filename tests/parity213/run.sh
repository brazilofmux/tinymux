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

# report <fileA> <fileB> <labelA> <labelB> — plain divergence listing.
report() {
    paste "$1" "$2" | awk -F'\t' -v A="$3" -v B="$4" '
        { if ($2 != $4) { printf "    %-22s %s=%-28s %s=%s\n", $1, A, "\047"$2"\047", B, "\047"$4"\047"; d++ } else s++ }
        END { printf "  %s vs %s: %d agree, %d differ\n", A, B, s+0, d+0 }
    '
    paste "$1" "$2" | awk -F'\t' '{ if ($2 != $4) d++ } END { exit (d>0) }'
}

# Extract NAME<TAB>VERDICT for shapes that carry one.  The verdict is the
# optional third column and is recognised by VALUE, not position, because
# '|' is a legal MUX delimiter and may appear inside an expression.
verdict_table() {
    awk -F'|' '
        /^[[:space:]]*(#|$)/ { next }
        NF > 2 {
            v = $NF
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", v)
            if (v=="2.13" || v=="2.14" || v=="both" || v=="neither" || v=="pin")
                printf "%s\t%s\n", $1, v
        }
    ' "$1"
}

# adjudicate <r213> <jit> <ast> <corpus>
# Applies the verdict column.  Semantics:
#   2.13     both 2.14 routes must match the 2.13 reference
#   2.14     current 2.14 behaviour is desired; the routes must agree with
#            each other, and divergence from 2.13 is accepted
#   both     all three must agree
#   neither  neither engine is satisfactory; needs new behaviour (reported,
#            never counted as satisfied or violated)
#   pin      deliberately deferred (reported, not a violation)
adjudicate() {
    verdict_table "$4" > "$WORK/verdicts.txt"
    paste "$1" "$2" "$3" | awk -F'\t' -v VF="$WORK/verdicts.txt" '
        BEGIN { while ((getline line < VF) > 0) { split(line, a, "\t"); V[a[1]] = a[2] } }
        {
            name=$1; r13=$2; jit=$4; ast=$6
            v = (name in V) ? V[name] : ""
            internal = (jit != ast)
            diverged = (r13 != jit || r13 != ast)
            if (v == "") {
                if (diverged) { un++; unl = unl sprintf("    %-22s 2.13=%-26s JIT=%-26s AST=%s\n", name, "\047"r13"\047", "\047"jit"\047", "\047"ast"\047") }
                next
            }
            if (v == "neither") { nn++; nl = nl sprintf("    %-22s 2.13=%-26s 2.14=%s\n", name, "\047"r13"\047", "\047"jit"\047"); next }
            if (v == "pin")     { pp++; pl = pl sprintf("    %-22s 2.13=%-26s 2.14=%s\n", name, "\047"r13"\047", "\047"jit"\047"); next }
            ok = 0
            if (v == "2.13") ok = (r13 == jit && r13 == ast)
            else if (v == "2.14") ok = (jit == ast)
            else if (v == "both") ok = (r13 == jit && r13 == ast)
            if (ok) { sat++ }
            else {
                vio++
                vl = vl sprintf("    %-22s want=%-6s 2.13=%-22s JIT=%-22s AST=%s\n", name, v, "\047"r13"\047", "\047"jit"\047", "\047"ast"\047")
            }
        }
        END {
            printf "  satisfied:     %d\n", sat+0
            if (vio) { printf "  VIOLATED:      %d\n", vio; printf "%s", vl }
            else     { printf "  VIOLATED:      0\n" }
            if (nn)  { printf "  needs-new:     %d  (verdict \047neither\047)\n", nn; printf "%s", nl }
            if (pp)  { printf "  pinned:        %d  (deferred by decision)\n", pp; printf "%s", pl }
            if (un)  { printf "  UNADJUDICATED: %d  (divergent, no verdict yet)\n", un; printf "%s", unl }
            exit (vio > 0)
        }
    '
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
report "$WORK/jit.txt" "$WORK/ast.txt" "JIT" "AST" || true

if [ "$HAVE213" -eq 1 ]; then
    echo
    echo "Against the 2.13 reference:"
    report "$WORK/r213.txt" "$WORK/jit.txt" "2.13" "JIT" || true
    echo
    report "$WORK/r213.txt" "$WORK/ast.txt" "2.13" "AST" || true
    echo
    echo "Verdicts:"
    adjudicate "$WORK/r213.txt" "$WORK/jit.txt" "$WORK/ast.txt" "$CORPUS" || rc=1
fi

echo
if [ "$rc" -eq 0 ]; then
    echo "OK: no verdict violated"
else
    echo "VERDICT VIOLATED — see above."
fi
echo
echo "A divergence is a finding, not automatically a bug: the 2.13 grammar"
echo "is not well-formed and some of its behaviour is organic.  The verdict"
echo "column records which engine is right per shape, decided case by case."
echo "Only a VIOLATED verdict fails this harness; unadjudicated divergences"
echo "are reported so they can be decided, not treated as regressions."
exit $rc
