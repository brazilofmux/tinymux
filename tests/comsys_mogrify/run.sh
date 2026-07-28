#!/bin/bash
#
#   run.sh — the MOGRIFY hooks and per-player CHATFORMAT, compared across
#            both comsys implementations (#1572).
#
#   The module implemented BLOCK and NOBUFFER.  MESSAGE, OVERRIDE, FORMAT and
#   CHATFORMAT were silently ignored, so a channel configured with any of them
#   behaved differently depending on which implementation happened to be live.
#   Silence rather than an error, which is why 31 comsys cases in the corpus
#   passed identically against both.
#
#   WHY THIS IS NOT IN tests/comsys_handoff
#
#   That driver's shape is "establish state under one implementation, read it
#   under the other".  Channel DELIVERY cannot be tested that way.  bConnected
#   is runtime state, set when a player joins during that process, and it is
#   not persisted -- so a run inheriting membership from an earlier run has
#   user records with bConnected false and delivers to nobody.  That briefly
#   looked like "the module never delivers at all"; it does, verified over a
#   real socket against netmux.
#
#   So each implementation joins and speaks within ONE process here, and the
#   assertion is that the two agree.  Neither side is the oracle by fiat --
#   though in practice the engine was, except for CHATFORMAT, where the engine
#   held a process-lifetime cache of the attribute number that latched 0 when
#   the vattr did not yet exist.  That is why CHATFORMAT is deliberately set
#   AFTER traffic has already flowed below.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN="$REPO_ROOT/mux/game/bin"
WORK="$SCRIPT_DIR/work"

if command -v timeout >/dev/null 2>&1; then
    TIMEOUT="timeout 120"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT="gtimeout 120"
else
    TIMEOUT=""
fi

if [ ! -x "$BIN/muxscript" ]; then
    echo "SKIP: $BIN/muxscript not found (run 'make install' first)."
    exit 0
fi
if [ ! -r "$BIN/comsys_mod.so" ] && [ ! -r "$BIN/comsys_mod.dll" ]; then
    echo "SKIP: comsys module not built; nothing to compare against."
    exit 0
fi

npass=0; nfail=0
ok()   { npass=$((npass+1)); echo "ok $((npass+nfail)) - $1"; }
nope() { nfail=$((nfail+1)); echo "not ok $((npass+nfail)) - $1"
         [ -n "${2:-}" ] && echo "  $2"; return 0; }

setup() {
    rm -rf "$WORK"; mkdir -p "$WORK/data" "$WORK/logs"
    cp "$REPO_ROOT/mux/game/alias.conf" "$REPO_ROOT/mux/game/compat.conf" "$WORK/"
    ln -s "$BIN" "$WORK/bin"
    ln -s "$REPO_ROOT/mux/game/text" "$WORK/text"
    cp "$REPO_ROOT/mux/game/data/netmux.db" "$WORK/data/"
    for n in engine module; do
        cat > "$WORK/$n.conf" <<EOF
input_database  data/netmux.db
output_database data/netmux.db.new
crash_database  data/netmux.db.CRASH
mail_database   data/mail.db
comsys_database data/comsys.db
port 6285
mud_name Mogrify
include alias.conf
include compat.conf
EOF
    done
    echo "module comsys_mod" >> "$WORK/module.conf"
}

# Run a fresh game under $1 with the hook setup in $2, then speak, and echo
# the line the speaker received.  Fresh each time: the point is agreement on
# identical input, not accumulated state.
speak() {   # $1 = engine|module, $2 = hook setup lines, $3 = probe word
    rm -rf "$WORK/data"; mkdir -p "$WORK/data"
    cp "$REPO_ROOT/mux/game/data/netmux.db" "$WORK/data/"
    {
        echo '@ccreate mg'
        echo '@create MgObj'
        echo '@cset/object mg=MgObj'
        echo '@cset/log mg=20'
        echo 'addcom g=mg'
        printf '%s\n' "$2"
        echo "g $3"
    } > "$WORK/in.txt"
    ( cd "$WORK" || exit 1
      LD_LIBRARY_PATH="$BIN" $TIMEOUT "$BIN/muxscript" -g . -c "$1.conf" \
          < in.txt > out 2>&1 )

    # Assert we got the implementation we asked for.  Without this the whole
    # file silently degrades into engine-versus-engine.
    local want
    if [ "$1" = "module" ]; then want="Comsys: using module implementation."
    else want="Comsys: using built-in engine implementation."; fi
    if ! grep -qF "$want" "$WORK/out"; then
        echo "Bail out!  wanted '$want'; run reported:"
        grep -h "using .* implementation" "$WORK/out" | sed 's/^/    /'
        exit 1
    fi

    grep -F "$3" "$WORK/out" | grep -v "^g $3" | tail -1
}

# $1 = label, $2 = hook setup, $3 = probe word, $4 = text both must contain
agree() {
    local e m
    e=$(speak engine "$2" "$3")
    m=$(speak module "$2" "$3")
    case "$e" in *"$4"*) ;; *) nope "$1 (engine)" "got: ${e:-<nothing>}"; return ;; esac
    case "$m" in *"$4"*) ;; *) nope "$1 (module)" "got: ${m:-<nothing>}"; return ;; esac
    ok "$1 — both implementations agree"
}

setup

agree "MOGRIFY\`MESSAGE replaces the text" \
      '&MOGRIFY`MESSAGE MgObj=MOGRIFIED: %1' msgprobe "MOGRIFIED:"

agree "MOGRIFY\`FORMAT formats the message" \
      '&MOGRIFY`FORMAT MgObj=FMT{%1}' fmtprobe "FMT{"

# CHATFORMAT is set AFTER the channel has already carried traffic in this same
# run.  That ordering is the point: the engine cached the CHATFORMAT attribute
# number once per process and latched 0 when the vattr did not yet exist, so a
# CHATFORMAT created later was ignored until restart.
agree "per-player CHATFORMAT applies when set after traffic" \
      'g warmup
&CHATFORMAT me=CF{%1}' cfprobe "CF{"

# OVERRIDE: channel-wide FORMAT wins and CHATFORMAT is suppressed.
agree "MOGRIFY\`OVERRIDE lets FORMAT win" \
      '&CHATFORMAT me=CF{%1}
&MOGRIFY`OVERRIDE MgObj=1
&MOGRIFY`FORMAT MgObj=OVR{%1}' ovrprobe "OVR{"

# ...and specifically that CHATFORMAT does NOT also apply, which is the half
# that distinguishes OVERRIDE from FORMAT alone.
e=$(speak engine '&CHATFORMAT me=CF{%1}
&MOGRIFY`OVERRIDE MgObj=1
&MOGRIFY`FORMAT MgObj=OVR{%1}' ovr2probe)
m=$(speak module '&CHATFORMAT me=CF{%1}
&MOGRIFY`OVERRIDE MgObj=1
&MOGRIFY`FORMAT MgObj=OVR{%1}' ovr2probe)
case "$e$m" in
    *"CF{"*) nope "MOGRIFY\`OVERRIDE suppresses CHATFORMAT" \
                  "engine: $e / module: $m" ;;
    *) ok "MOGRIFY\`OVERRIDE suppresses CHATFORMAT — both agree" ;;
esac

echo "=== comsys mogrify: $npass passed, $nfail failed ==="
[ "$nfail" -eq 0 ] || exit 1
rm -rf "$WORK"
exit 0
