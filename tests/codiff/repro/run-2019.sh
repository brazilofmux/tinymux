#!/bin/bash
#
#   run-2019.sh — minimal reproducer for #2019.
#
#   The DBT returns a wrong answer for correct RV64 code when block
#   chaining is enabled.  This drives the smallest case found:
#
#     * apply repro/2019-cursor-rewrite.patch to color_ops.c (guest only --
#       nothing in the working tree is touched)
#     * a guest program that calls co_insert_at TWICE on a buffer of N '|':
#       first at position +1, then at position -1
#     * run the resulting ELF through our interpreter and our DBT
#
#   Expected on a good DBT: both return N+2.
#   Observed on aarch64:    the interpreter returns N+2, the DBT returns 3,
#                           intermittently -- roughly half of runs.
#
#   The intermittency is the point.  Run it once and you may well see a
#   pass; that is why this loops and reports a rate rather than a verdict.
#   qemu-riscv64-static, if present, is run once as the external oracle.
#
#   Trigger conditions, each established by bisection (see #2019):
#     - the +1 call is REQUIRED; with only the -1 call the DBT is correct
#     - an 8-byte warm-up list suffices; it need not approach the cap
#     - warming with -1 instead of +1 does NOT trigger it
#     - N must be >= 16383 (the word count reaching the LBUF_SIZE/2 cap)
#     - it does not reproduce at small LBUF_SIZE, so iteration count matters
#
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
CODIFF=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$CODIFF/../.." && pwd)
BUILD="$HERE/build"
RUNS=${REPRO_RUNS:-40}
NW=${REPRO_NW:-20000}
WARM=${REPRO_WARM:-3}

RVCC=${RVCC:-riscv64-unknown-elf-gcc}
QEMU=${QEMU:-qemu-riscv64-static}

if ! command -v "$RVCC" >/dev/null 2>&1; then
    echo "SKIP: no $RVCC -- cannot build the guest.  Nothing was tested."
    exit 0
fi
case "$(uname -m)" in
    x86_64) BACKEND=dbt_x64_sysv ;;
    aarch64|arm64) BACKEND=dbt_a64_sysv ;;
    *) echo "SKIP: no DBT backend for $(uname -m)."; exit 0 ;;
esac

mkdir -p "$BUILD"
set -e

# Patch a COPY of color_ops.c.  The working tree is never modified, and
# color_ops.c is generated anyway -- a real change belongs in color_ops.rl.
cp "$ROOT/mux/lib/color_ops.c" "$BUILD/color_ops_cursor.c"
chmod u+w "$BUILD/color_ops_cursor.c"
patch -s -p1 -d "$BUILD" --posix -o "$BUILD/patched.c" \
      "$BUILD/color_ops_cursor.c" < "$HERE/2019-cursor-rewrite.patch" \
  || { echo "FAIL: patch did not apply -- color_ops.c has moved on."; exit 1; }
mv "$BUILD/patched.c" "$BUILD/color_ops_cursor.c"

RVFLAGS="-march=rv64imd -mabi=lp64d -O2 -fno-builtin -ffreestanding -nostdlib
         -ffunction-sections -fdata-sections -DLBUF_SIZE=32768"
RVINC="-isystem $ROOT/mux/rv64/src/include -I$ROOT/mux/include"

$RVCC $RVFLAGS $RVINC -c -o "$BUILD/co.o"   "$BUILD/color_ops_cursor.c"
$RVCC $RVFLAGS $RVINC -c -o "$BUILD/uni.o"  "$ROOT/mux/rv64/src/unicode_tables.c"
$RVCC $RVFLAGS $RVINC -c -o "$BUILD/soft.o" "$ROOT/mux/rv64/src/softlib.c"
$RVCC $RVFLAGS -DWARM_MODE=$WARM -DN_WORDS=$NW \
      -c -o "$BUILD/m.o" "$CODIFF/min_main.c"
$RVCC -nostdlib -Wl,-melf64lriscv -Wl,-e,_start -Wl,--gc-sections \
      -o "$BUILD/min.elf" \
      "$BUILD/m.o" "$BUILD/co.o" "$BUILD/uni.o" "$BUILD/soft.o"

E="$ROOT/mux/modules/engine"
g++ -std=c++17 -O2 -DHAVE_CONFIG_H -DTINYMUX_JIT -I"$ROOT/mux/include" \
    -o "$BUILD/runner" "$CODIFF/runner.cpp" \
    "$E/dbt_interp.cpp" "$E/dbt_elf64.cpp" "$E/dbt.cpp" "$E/$BACKEND.cpp"
set +e

want=$((NW + 2))
echo "=== #2019 reproducer: N=$NW warm_mode=$WARM, correct answer is $want ==="

if command -v "$QEMU" >/dev/null 2>&1; then
    echo "oracle (qemu-riscv64): $("$QEMU" "$BUILD/min.elf" 2>/dev/null | tr '\n' ' ')"
else
    echo "oracle (qemu-riscv64): absent -- skipping the external check"
fi

bad=0
for i in $(seq 1 "$RUNS"); do
    "$BUILD/runner" "$BUILD/min.elf" 2>/dev/null \
        | awk '/^--- dbt/{d=1} d && /^test ret=/{print; exit}' \
        | grep -q "^test ret=$want$" || bad=$((bad + 1))
done

echo "DBT wrong in $bad of $RUNS runs"
if [ "$bad" -gt 0 ]; then
    echo "REPRODUCED (#2019)"
    exit 1
fi
echo "not reproduced in this many runs -- #2019 is intermittent, try more"
exit 0
