#!/bin/bash
#
#   Build.sh — Standard development build.
#
#   Default: --enable-jit
#   Override: ./Build.sh --enable-realitylvls --enable-wodrealms
#
#   Can be run from anywhere.  Detects when configure needs re-running.
#   Builds softlib.rv64 if cross-compiler is available.
#
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
MUX="$REPO_ROOT/mux"
TC="$REPO_ROOT/testcases"

# Default configure options; override via command-line args.
if [ $# -gt 0 ]; then
    CONFIGURE_OPTS="$*"
else
    CONFIGURE_OPTS="--enable-jit"
fi

echo "=== Build: $CONFIGURE_OPTS ==="

# ---------------------------------------------------------------------------
# Step 1: Configure if needed.
# ---------------------------------------------------------------------------

NEED_CONFIGURE=false

if [ ! -f "$MUX/config.status" ] || [ ! -f "$MUX/Makefile" ]; then
    NEED_CONFIGURE=true
    echo "No config.status or Makefile — will run configure."
elif [ ! -f "$MUX/include/autoconf.h" ]; then
    NEED_CONFIGURE=true
    echo "No autoconf.h — will run configure."
else
    # Check if configure options changed.
    CURRENT_OPTS=$(sed -n "s/^ac_cs_config='\\(.*\\)'$/\\1/p" "$MUX/config.status" 2>/dev/null || echo "")
    if [ "$CURRENT_OPTS" != "$CONFIGURE_OPTS" ]; then
        NEED_CONFIGURE=true
        echo "Configure options changed: '$CURRENT_OPTS' -> '$CONFIGURE_OPTS'"
        echo "Running make clean first..."
        make -C "$MUX" clean 2>/dev/null || true
    fi
fi

if $NEED_CONFIGURE; then
    echo "Running: ./configure $CONFIGURE_OPTS"
    cd "$MUX"
    # shellcheck disable=SC2086
    ./configure $CONFIGURE_OPTS
    cd "$REPO_ROOT"
fi

# ---------------------------------------------------------------------------
# Step 2: Build and install.
# ---------------------------------------------------------------------------

echo "Building..."
make -C "$MUX" -j4

echo "Installing to game/bin/..."
make -C "$MUX" install

# ---------------------------------------------------------------------------
# Step 3: Build softlib.rv64.
# ---------------------------------------------------------------------------

if command -v riscv-none-elf-gcc >/dev/null 2>&1 || \
   command -v riscv64-unknown-elf-gcc >/dev/null 2>&1 || \
   command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    echo "Building softlib.rv64..."
    # A failure here used to be a warning, and the stale checked-in blob was
    # copied in its place.  That is how #1402 broke the blob build and nobody
    # noticed for days: the only thing that rebuilds the artifact swallowed
    # the error, so every subsequent edit to mux/rv64/src/ silently shipped
    # the old binary.  A box that CAN build the blob must not ship a stale
    # one -- fail here instead.
    if ! make -C "$MUX/rv64" || ! make -C "$MUX/rv64" install; then
        echo "  ERROR: rv64 build failed on a box that has a cross-compiler."
        echo "  Refusing to ship the stale checked-in blob; fix the build."
        exit 1
    fi
    echo "  softlib.rv64 built and installed."
else
    echo "No RISC-V cross-compiler; copying checked-in softlib.rv64."
    cp "$MUX/rv64/softlib.rv64" "$MUX/game/bin/softlib.rv64"
fi

# ---------------------------------------------------------------------------
# Step 4: Build testcases tools.
# ---------------------------------------------------------------------------

echo "Building testcases tools..."
make -C "$TC/tools"

# ---------------------------------------------------------------------------
# Step 5: Verify critical binaries.
# ---------------------------------------------------------------------------

MISSING=""
for bin in netmux slave libmux.so engine.so softlib.rv64; do
    if [ ! -e "$MUX/game/bin/$bin" ]; then
        MISSING="$MISSING $bin"
    fi
done

if [ -n "$MISSING" ]; then
    echo "ERROR: Missing in game/bin/:$MISSING"
    exit 1
fi

if [ ! -x "$TC/tools/unformat" ]; then
    echo "ERROR: unformat not built."
    exit 1
fi

echo "=== Build complete ==="
