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

echo "# 4. cross-family conversions run cleanly"
for from in t5x-v5.flat t5x-v5-color.flat; do
    for to in pennmush tinymush rhostmush tinymux; do
        if [ "$(conv_ok "$tmp/cf" -o "$to" "$fix/$from")" = ok ]; then
            ok "$from -> $to"
        else
            nok "$from -> $to (nonzero exit or empty output)"
        fi
    done
done

echo "# 5. --list runs"
if "$omega" --list >/dev/null 2>&1; then ok "omega --list"; else nok "omega --list"; fi

echo "# ---"
echo "# $pass passed, $fail failed"
[ "$fail" -eq 0 ]
