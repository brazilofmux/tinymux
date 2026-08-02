#!/bin/sh
# Smoke tests for tools/ansify. Run from tools/ansify via `make test`.
set -e
cd "$(dirname "$0")/.."

BIN=./ansify
fail=0

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

run_case() {
    name=$1
    if cmp -s "$TMP/exp" "$TMP/out"; then
        printf '  PASS  %s\n' "$name"
    else
        printf '  FAIL  %s\n' "$name"
        printf '        expected: '; od -An -tx1 "$TMP/exp"
        printf '        got:      '; od -An -tx1 "$TMP/out"
        fail=1
    fi
}

# --- single-letter modern codes ---
printf '%%xrred%%xn\n' > "$TMP/in"
printf '\033[31mred\033[0m\n' > "$TMP/exp"
$BIN "$TMP/in" > "$TMP/out"
run_case "fg red + normal"

# --- legacy k black vs modern x black ---
printf '%%xkL%%xxM%%xn\n' > "$TMP/in"
printf '\033[30mL\033[30mM\033[0m\n' > "$TMP/exp"
$BIN "$TMP/in" > "$TMP/out"
run_case "legacy k and modern x both black"

# --- bg uppercase ---
printf '%%xRbg%%xn\n' > "$TMP/in"
printf '\033[41mbg\033[0m\n' > "$TMP/exp"
$BIN "$TMP/in" > "$TMP/out"
run_case "bg red"

# --- %% escape ---
printf '100%%%%\n' > "$TMP/in"
printf '100%%\n' > "$TMP/exp"
$BIN "$TMP/in" > "$TMP/out"
run_case "literal percent"

# --- hex truecolor ---
printf '%%x<#FF0000>r%%xn\n' > "$TMP/in"
printf '\033[38;2;255;0;0mr\033[0m\n' > "$TMP/exp"
$BIN "$TMP/in" > "$TMP/out"
run_case "hex truecolor fg"

# --- xterm 256 ---
printf '%%x<196>r%%xn\n' > "$TMP/in"
printf '\033[38;5;196mr\033[0m\n' > "$TMP/exp"
$BIN "$TMP/in" > "$TMP/out"
run_case "xterm-256 fg"

# --- strip mode ---
printf '%%xh%%xrHi%%xn\n' > "$TMP/in"
printf 'Hi\n' > "$TMP/exp"
$BIN --strip "$TMP/in" > "$TMP/out"
run_case "--strip"

# --- %c alias ---
printf '%%chHi%%cn\n' > "$TMP/in"
printf '\033[1mHi\033[0m\n' > "$TMP/exp"
$BIN "$TMP/in" > "$TMP/out"
run_case "%c alias"

# --- sample file builds without error ---
$BIN samples/connect.mux > "$TMP/sample.out"
if [ -s "$TMP/sample.out" ]; then
    printf '  PASS  samples/connect.mux\n'
else
    printf '  FAIL  samples/connect.mux\n'
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "ansify smoke: FAILED"
    exit 1
fi
echo "ansify smoke: all passed"
exit 0
