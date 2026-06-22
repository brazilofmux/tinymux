#!/bin/sh
# Omega converter test pool.
#
# Builds ../omega if needed, then exercises three classes of behavior against
# the committed fixtures:
#
#   1. Idempotent round-trip   - reading and rewriting a flatfile at the same
#                                version reproduces it byte-for-byte.
#   2. Color form fidelity     - 24-bit color survives v5<->v4<->v3 migration
#                                (the delta <-> two-code-point boundary).
#   3. Cross-family conversion - every supported conversion path runs cleanly
#                                and produces non-empty output.
#
# Exits non-zero if any check fails.

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
conv=$here/..
omega=$conv/omega
fix=$here/fixtures

if [ ! -x "$omega" ]; then
    echo "# building omega ..."
    ( cd "$conv" && make ) || { echo "Bail out! build failed"; exit 1; }
fi

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

pass=0
fail=0

ok()   { pass=$((pass + 1)); echo "ok $((pass + fail)) - $1"; }
nok()  { fail=$((fail + 1)); echo "not ok $((pass + fail)) - $1"; }

# same(desc, a, b): assert two files are byte-identical.
same() {
    if cmp -s "$2" "$3"; then ok "$1"; else nok "$1 (files differ)"; fi
}

# conv(out, args...): run omega quietly; echo "ok" on success else "".
conv_ok() {
    out=$1; shift
    if "$omega" "$@" "$out" >/dev/null 2>&1 && [ -s "$out" ]; then
        echo ok
    fi
}

# verBuild(out, ver, src): produce src at TinyMUX version ver.
verBuild() { "$omega" -v "$2" "$3" "$1" >/dev/null 2>&1; }

# verOf(file): print the TinyMUX flatfile version (low byte of +X header).
verOf() {
    n=$(head -n 1 "$1" | sed -n 's/^+X\([0-9][0-9]*\).*/\1/p')
    [ -n "$n" ] && echo $((n % 256))
}

# detect(file): print omega's "Detected input:" label for file.
detect() { "$omega" "$1" /dev/null 2>&1 | sed -n 's/^Detected input: //p'; }

# wantDetect(desc, file, substring): assert the detected label contains substring.
wantDetect() {
    d=$(detect "$2")
    case $d in
        *"$3"*) ok "$1" ;;
        *)      nok "$1 (detected '$d', wanted '$3')" ;;
    esac
}

echo "# 1. idempotent round-trip"
for f in "$fix"/*.flat; do
    name=$(basename "$f")
    "$omega" "$f" "$tmp/rt" >/dev/null 2>&1
    same "round-trip $name" "$f" "$tmp/rt"
done

echo "# 2. color form fidelity (v5 <-> v4 <-> v3)"
color=$fix/t5x-v5-color.flat
# v5 -> v4 -> v5 must reproduce the original v5 bytes.
verBuild "$tmp/c_v4" 4 "$color"
verBuild "$tmp/c_v4_v5" 5 "$tmp/c_v4"
same "v5->v4->v5 preserves 24-bit color" "$color" "$tmp/c_v4_v5"
# v5 -> v3 -> v5 (v3 also uses the delta form).
verBuild "$tmp/c_v3" 3 "$color"
verBuild "$tmp/c_v3_v5" 5 "$tmp/c_v3"
same "v5->v3->v5 preserves 24-bit color" "$color" "$tmp/c_v3_v5"
# v4 -> v5 -> v4 must reproduce the delta form.
verBuild "$tmp/c_v4_v5_v4" 4 "$tmp/c_v4_v5"
same "v4->v5->v4 preserves delta form" "$tmp/c_v4" "$tmp/c_v4_v5_v4"

echo "# 3. version ladder produces the requested version"
for v in 1 2 3 4 5; do
    verBuild "$tmp/lad" "$v" "$fix/t5x-v5.flat"
    got=$(verOf "$tmp/lad")
    if [ "$got" = "$v" ]; then ok "-v $v writes version $v"
    else nok "-v $v wrote version ${got:-?}"; fi
done

echo "# 4. auto-detection identifies each fixture's family/version"
wantDetect "detect t5x-v5.flat as TinyMUX v5"        "$fix/t5x-v5.flat"       "TinyMUX v5"
wantDetect "detect t5x-v5-color.flat as TinyMUX v5"  "$fix/t5x-v5-color.flat" "TinyMUX v5"
wantDetect "detect p6h-new.flat as PennMUSH"         "$fix/p6h-new.flat"      "PennMUSH"
wantDetect "detect t6h-3.1.flat as TinyMUSH"         "$fix/t6h-3.1.flat"      "TinyMUSH"
wantDetect "detect r7h-v7.flat as RhostMUSH"         "$fix/r7h-v7.flat"       "RhostMUSH"

echo "# 5. TinyMUSH version ladder"
# 3.0 / 3.1 / 3.2 are distinguished by header feature flags.
for spec in "3.0=TinyMUSH 3.0" "3.1p4=3.1p0 to 3.1p4" "3.2=3.2 or later"; do
    id=${spec%%=*}; want=${spec#*=}
    "$omega" -o tinymush -v "$id" "$fix/t5x-v5.flat" "$tmp/t6" >/dev/null 2>&1
    wantDetect "t6h -v $id produces '$want'" "$tmp/t6" "$want"
done
# 3.1p4 vs 3.1p6 differ only by the extra-escape style of attribute content; on
# escape-free data 3.1p6 re-detects as the 3.1p4 bucket.  (A byte compare would
# be flaky -- the TinyMUSH writer embeds wall-clock timestamps.)
"$omega" -o tinymush -v 3.1p6 "$fix/t5x-v5.flat" "$tmp/t6b" >/dev/null 2>&1
wantDetect "t6h -v 3.1p6 coincides with 3.1p4 on escape-free data" "$tmp/t6b" "3.1p0 to 3.1p4"

echo "# 6. PennMUSH new->old downgrade is rejected (known limitation)"
if "$omega" -o pennmush -v old "$fix/p6h-new.flat" "$tmp/p" >/dev/null 2>&1; then
    nok "p6h -v old should be rejected"
else
    ok "p6h -v old rejected cleanly (no crash)"
fi

echo "# 7. cross-family conversions run cleanly (both directions)"
# TinyMUX -> every family.
for from in t5x-v5.flat t5x-v5-color.flat; do
    for to in pennmush tinymush rhostmush tinymux; do
        if [ "$(conv_ok "$tmp/cf" -o "$to" "$fix/$from")" = ok ]; then
            ok "$from -> $to"
        else
            nok "$from -> $to (nonzero exit or empty output)"
        fi
    done
done
# Every family -> TinyMUX.
for from in p6h-new.flat t6h-3.1.flat r7h-v7.flat; do
    if [ "$(conv_ok "$tmp/cf" -o tinymux "$fix/$from")" = ok ]; then
        ok "$from -> tinymux"
    else
        nok "$from -> tinymux (nonzero exit or empty output)"
    fi
done

echo "# 8. extraction does not silently truncate an expanding value"
# t5x-v5-expand.flat's first attribute is 32000 '  ' pairs; each extracts as
# '%b ' (~96KB), which overflowed the old 1*LBUF extraction buffer.  All 32000
# must survive.
"$omega" -x 1 "$fix/t5x-v5-expand.flat" "$tmp/ex" 2>/dev/null
nbsp=$(grep -o '%b ' "$tmp/ex" | wc -l)
if [ "$nbsp" -ge 32000 ]; then ok "extract preserves expanding value ($nbsp '%b ')"
else nok "extract truncated expanding value ($nbsp '%b ', want >=32000)"; fi

echo "# 9. RhostMUSH color (%c..) is decoded on import (rhost -> t5x)"
# r7h-color.flat has 24-bit FG #C88764 / BG #143C5A in a user attribute.
# rhost -> t5x must parse the %c codes into PUA; round-tripping back to rhost
# must reproduce them.  (Also exercises the rhost->t5x object-header fix: the
# converted t5x has to re-parse for the back-conversion to see the attribute.)
"$omega" -o tinymux -v 5 "$fix/r7h-color.flat" "$tmp/rc_t5x" >/dev/null 2>&1
"$omega" -o rhostmush "$tmp/rc_t5x" "$tmp/rc_back" >/dev/null 2>&1
rcolor=yes
for c in '%c<#C88764>' '%C<#143C5A>'; do
    grep -qaF "$c" "$tmp/rc_back" || rcolor=no
done
if [ "$rcolor" = yes ]; then ok "rhost 24-bit color survives rhost->t5x->rhost"
else nok "rhost color lost on rhost->t5x import"; fi

echo "# 10. t5x <-> TinyMUSH preserves Latin-1 text and color"
# t5x-v5-latin.flat has intense+red "Café" (UTF-8) in a user attribute.
# t5x -> tinymush -> t5x must keep the color (PUA) and the accented 'é'
# (the old ConvertT5XValue mapped every >=0x7F byte to '?').
"$omega" -o tinymush "$fix/t5x-v5-latin.flat" "$tmp/tm" >/dev/null 2>&1
"$omega" -o tinymux -v 5 "$tmp/tm" "$tmp/tm_back" >/dev/null 2>&1
# intense + FG-red PUA, "Caf", é (UTF-8), reset PUA -- exact round-trip.
want=$(printf '\357\224\201\357\230\201Caf\303\251\357\224\200')
if grep -qaF "$want" "$tmp/tm_back"; then ok "t5x->tinymush->t5x keeps Latin-1 + color"
else nok "t5x->tinymush lost Latin-1 or color"; fi

echo "# 11. PennMUSH color markup is decoded on import (penn -> t5x)"
# p6h-color.flat has 24-bit FG #C88764 markup (\x02c#C88764\x03) in an attribute.
# penn -> t5x must parse it to PUA; routing on to rhost must reproduce it.
"$omega" -o tinymux -v 5 "$fix/p6h-color.flat" "$tmp/pc_t5x" >/dev/null 2>&1
"$omega" -o rhostmush "$tmp/pc_t5x" "$tmp/pc_back" >/dev/null 2>&1
if grep -qaF '%c<#C88764>' "$tmp/pc_back"; then ok "penn 24-bit color survives penn->t5x"
else nok "penn color lost on penn->t5x import"; fi

echo "# 12. t5x -> PennMUSH -> t5x preserves color and Unicode"
# t5x-v5-latin.flat has intense+red "Café" in a user attr.  Out to Penn it
# becomes \x02ch\x03\x02cr\x03 markup; back in it must restore the same PUA, and
# 'é' must survive as UTF-8.
"$omega" -o pennmush "$fix/t5x-v5-latin.flat" "$tmp/pn" >/dev/null 2>&1
"$omega" -o tinymux -v 5 "$tmp/pn" "$tmp/pn_back" >/dev/null 2>&1
want=$(printf '\357\224\201\357\230\201Caf\303\251\357\224\200')
if grep -qaF "$want" "$tmp/pn_back"; then ok "t5x->penn->t5x keeps color + Unicode"
else nok "t5x->penn->t5x lost color or Unicode"; fi

echo "# 13. --list runs"
if "$omega" --list >/dev/null 2>&1; then ok "omega --list"; else nok "omega --list"; fi

echo "# ---"
echo "# $pass passed, $fail failed"
[ "$fail" -eq 0 ]
