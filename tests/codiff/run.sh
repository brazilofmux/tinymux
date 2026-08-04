#!/bin/bash
#
#   run.sh — does color_ops.c mean the same thing on every route that
#            actually executes it?
#
#   color_ops.c is compiled TWICE -- once into libmux for the host, and
#   once into the freestanding RV64 blob -- and the blob is then executed
#   by two engines of our own.  So there are four implementations of the
#   same source in play, and #2019 is what happens when one of them
#   disagrees: a change can be provably correct in C, provably correct as
#   RV64, and still wrong in the artifact production runs.
#
#   Four routes, one battery, one transcript format:
#
#     host    color_ops.c as built into libmux.  When the working tree
#             holds a rewrite, this leg is the PRE-change implementation
#             (libmux is not rebuilt here), which makes it the
#             specification rather than a hand-written expectations table.
#     qemu    qemu-riscv64-static running the guest ELF.  The external
#             oracle -- the only route here that neither we nor the tree
#             wrote, so it is what settles "is the RISC-V correct".
#     interp  our rv64_interp_run over the same ELF.
#     dbt     our DBT over the same ELF.  This is what the JIT runs.
#
#   One guest binary serves the last three: it uses only Linux syscalls
#   64 (write) and 93 (exit), which is exactly what the ELF harness in
#   mux/modules/engine/dbt_test.cpp implements, so all three execute the
#   same instruction stream rather than three builds of the same source.
#
#   The battery is fixed cases + max_words-cap cases + seeded random
#   cases, with the SAME LCG on every route so the inputs are identical
#   by construction.  The cap cases are not decoration: a cap-check bug
#   moves 4 transcript lines and the random leg catches it never, while a
#   common-path bug moves 212.  Both controls are in the header.
#
#   Skips loudly, never quietly: this needs a RISC-V cross-compiler, and
#   the qemu leg needs qemu-riscv64-static.  A missing tool must read as
#   "not tested here", not as a pass.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
BUILD="$SCRIPT_DIR/build"
ITERS=${CODIFF_ITERS:-200}

RVCC=${RVCC:-riscv64-unknown-elf-gcc}
QEMU=${QEMU:-qemu-riscv64-static}

# CODIFF_CO_SRC points the GUEST legs at a different color_ops.c while the
# host leg keeps using the libmux already built -- which turns this into a
# differential of a candidate rewrite against the shipped implementation,
# and is also how the negative controls are run.  Defaults to the tree's.
CO_SRC=${CODIFF_CO_SRC:-$ROOT/mux/lib/color_ops.c}

fail=0
note() { printf '%s\n' "$*"; }

note "=== color_ops multi-route differential (#2019) ==="

# --- tool availability -------------------------------------------------
if ! command -v "$RVCC" >/dev/null 2>&1; then
    note "SKIP: no $RVCC on this box."
    note "      The guest legs (qemu/interp/dbt) cannot be built, so this"
    note "      test covers NOTHING here.  Not a pass."
    exit 0
fi

ARCH=$(uname -m)
case "$ARCH" in
    x86_64|aarch64|arm64) ;;
    *)
        note "SKIP: no DBT backend for host '$ARCH'; the DBT leg cannot run."
        exit 0
        ;;
esac

LIBMUX=""
for cand in "$ROOT/mux/lib/libmux.so" "$ROOT/mux/game/bin/libmux.so"; do
    [ -f "$cand" ] && { LIBMUX=$(dirname "$cand"); break; }
done
if [ -z "$LIBMUX" ]; then
    note "SKIP: libmux not built (run 'make install' from the repo root)."
    note "      Without it there is no host leg to compare against."
    exit 0
fi

have_qemu=1
command -v "$QEMU" >/dev/null 2>&1 || have_qemu=0
if [ "$have_qemu" = 0 ]; then
    note "NOTE: $QEMU absent -- running without the external oracle."
    note "      interp and dbt will be compared to the host leg only, so a"
    note "      fault shared by both of our engines would go unseen."
fi

mkdir -p "$BUILD"

RVFLAGS="-march=rv64imd -mabi=lp64d -O2 -fno-builtin -ffreestanding -nostdlib
         -ffunction-sections -fdata-sections -DLBUF_SIZE=32768
         -DFUZZ_ITERS=$ITERS"
RVINC="-isystem $ROOT/mux/rv64/src/include -I$ROOT/mux/include"

# --- build the guest ---------------------------------------------------
note "--- building guest ($($RVCC -dumpmachine) $($RVCC -dumpversion)) ---"
set -e
$RVCC $RVFLAGS $RVINC -c -o "$BUILD/co.o"   "$CO_SRC"
$RVCC $RVFLAGS $RVINC -c -o "$BUILD/uni.o"  "$ROOT/mux/rv64/src/unicode_tables.c"
$RVCC $RVFLAGS $RVINC -c -o "$BUILD/soft.o" "$ROOT/mux/rv64/src/softlib.c"
$RVCC $RVFLAGS -I"$SCRIPT_DIR" -c -o "$BUILD/gmain.o" "$SCRIPT_DIR/guest_main.c"
$RVCC -nostdlib -Wl,-melf64lriscv -Wl,-e,_start -Wl,--gc-sections \
      -o "$BUILD/guest.elf" \
      "$BUILD/gmain.o" "$BUILD/co.o" "$BUILD/uni.o" "$BUILD/soft.o"

# --- build the host leg ------------------------------------------------
gcc -O2 -DFUZZ_ITERS=$ITERS -I"$SCRIPT_DIR" -I"$ROOT/mux/include" \
    -o "$BUILD/host.bin" "$SCRIPT_DIR/host_main.c" \
    -L"$LIBMUX" -lmux -Wl,-rpath,"$LIBMUX"

# --- build the two-engine runner ---------------------------------------
case "$ARCH" in
    x86_64) BACKEND=dbt_x64_sysv ;;
    *)      BACKEND=dbt_a64_sysv ;;
esac
E="$ROOT/mux/modules/engine"
g++ -std=c++17 -O2 -DHAVE_CONFIG_H -DTINYMUX_JIT -I"$ROOT/mux/include" \
    -o "$BUILD/runner" "$SCRIPT_DIR/runner.cpp" \
    "$E/dbt_interp.cpp" "$E/dbt_elf64.cpp" "$E/dbt.cpp" "$E/$BACKEND.cpp"
set +e

# --- run ---------------------------------------------------------------
strip_marker() { grep -v '^DONE$'; }

"$BUILD/host.bin" 2>/dev/null | strip_marker > "$BUILD/host.txt"
if [ "$have_qemu" = 1 ]; then
    "$QEMU" "$BUILD/guest.elf" 2>/dev/null | strip_marker > "$BUILD/qemu.txt"
fi

"$BUILD/runner" "$BUILD/guest.elf" > "$BUILD/routes.raw" 2>&1
awk '/^--- interp ---$/{r="i"; next}
     /^--- dbt/{r="d"; next}
     /^rc=/{next}
     r=="i"{print > BI}
     r=="d"{print > BD}' \
    BI="$BUILD/interp.txt" BD="$BUILD/dbt.txt" "$BUILD/routes.raw"
for f in interp dbt; do
    [ -f "$BUILD/$f.txt" ] && strip_marker < "$BUILD/$f.txt" > "$BUILD/$f.body" \
        && mv "$BUILD/$f.body" "$BUILD/$f.txt"
done

lines=$(wc -l < "$BUILD/host.txt")
note "--- comparing $lines transcript lines ---"

REF="$BUILD/host.txt"; REFNAME="host"
if [ "$have_qemu" = 1 ]; then REF="$BUILD/qemu.txt"; REFNAME="qemu"; fi

for r in host qemu interp dbt; do
    f="$BUILD/$r.txt"
    [ -f "$f" ] || continue
    [ "$r" = "$REFNAME" ] && continue
    if diff -q "$REF" "$f" >/dev/null 2>&1; then
        printf '  %-7s vs %-6s : OK\n' "$r" "$REFNAME"
    else
        n=$(diff "$REF" "$f" | grep -c '^[<>]')
        printf '  %-7s vs %-6s : DIFFER (%s lines)\n' "$r" "$REFNAME" "$n"
        diff "$REF" "$f" | head -6 | cut -c1-160
        fail=1
    fi
done

if [ "$fail" = 0 ]; then
    note "PASS: all routes agree."
else
    note "FAIL: routes disagree -- see above."
fi
exit $fail
