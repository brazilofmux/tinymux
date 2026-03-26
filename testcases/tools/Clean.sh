#!/bin/bash
#
#   Clean.sh — Nuclear clean of all build and test artifacts.
#
#   Safe to run after switching between master and release/2.13.
#   Does NOT rely on make clean (which may itself be stale).
#
#   Usage: ./testcases/tools/Clean.sh   (from anywhere)
#
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
MUX="$REPO_ROOT/mux"
TC="$REPO_ROOT/testcases"

echo "=== Clean: removing all build and test artifacts ==="

# ---------------------------------------------------------------------------
# Autotools generated files — both mux/ (2.14) and mux/src/ (2.13) layouts.
# ---------------------------------------------------------------------------

for dir in "$MUX" "$MUX/src"; do
    rm -f "$dir/config.status" "$dir/config.log" "$dir/config.cache"
done

find "$MUX" -name 'autoconf.h' -delete 2>/dev/null || true
find "$MUX" -name 'stamp-h*' -delete 2>/dev/null || true
find "$MUX" -name 'autom4te.cache' -type d -exec rm -rf {} + 2>/dev/null || true
find "$MUX" -name '.deps' -type d -exec rm -rf {} + 2>/dev/null || true
find "$MUX" -name '.dirstamp' -delete 2>/dev/null || true

# Generated Makefiles: detect by "Generated.*by configure" in first 5 lines.
# This avoids deleting hand-written ones like rv64/Makefile.
#
find "$MUX" -name 'Makefile' -type f | while read -r f; do
    if head -5 "$f" 2>/dev/null | grep -q 'Generated.*by configure'; then
        rm -f "$f"
    fi
done

# ---------------------------------------------------------------------------
# Object files and libraries.
# ---------------------------------------------------------------------------

find "$MUX" \( -name '*.o' -o -name '*.lo' -o -name '*.eo' -o -name '*.a' \) \
    -delete 2>/dev/null || true

# .d dependency files (lib/, modules/engine/)
find "$MUX" -name '*.d' -path '*/.d' -delete 2>/dev/null || true
find "$MUX/lib" -name '*.d' -delete 2>/dev/null || true
find "$MUX/modules" -name '*.d' -delete 2>/dev/null || true

# ---------------------------------------------------------------------------
# Built binaries.
# ---------------------------------------------------------------------------

rm -f "$MUX/src/netmux" "$MUX/src/slave" "$MUX/src/stubslave"
rm -f "$MUX/announce/announce"
rm -f "$MUX/src/muxescape"
rm -f "$MUX/script/muxscript"
rm -f "$MUX/src/tools/rv64strip"

# .so files under modules/ and lib/
find "$MUX/modules" -name '*.so' -delete 2>/dev/null || true
find "$MUX/lib" -name '*.so' -delete 2>/dev/null || true

# ---------------------------------------------------------------------------
# game/bin/ — remove all contents (these are symlinks and copies).
# The source softlib.rv64 in rv64/ is preserved.
# ---------------------------------------------------------------------------

if [ -d "$MUX/game/bin" ]; then
    find "$MUX/game/bin" -maxdepth 1 -type f -delete 2>/dev/null || true
    find "$MUX/game/bin" -maxdepth 1 -type l -delete 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# rv64 build artifacts (NOT the checked-in softlib.rv64 blob).
# ---------------------------------------------------------------------------

rm -f "$MUX/rv64/src/"*.o "$MUX/rv64/src/softlib.elf"

# ---------------------------------------------------------------------------
# Proxy build artifacts.
# ---------------------------------------------------------------------------

rm -f "$MUX/proxy/hydra" "$MUX/proxy/"*.o
rm -rf "$MUX/proxy/.deps"

# ---------------------------------------------------------------------------
# Testcases runtime state.
# ---------------------------------------------------------------------------

cd "$TC"
rm -rf smoke.d smoke.fail logs text
rm -f smoke.flat smoke.log netmux.log
rm -f smoke.conf alias.conf compat.conf
rm -f shutdown.status smoke.pid smoke.*.pid
rm -f bin

# ---------------------------------------------------------------------------
# Testcases/tools built binaries (NOT .c or .rl source).
# ---------------------------------------------------------------------------

rm -f "$TC/tools/unformat" "$TC/tools/reformat" "$TC/tools/test_unicode_icu"

echo "=== Clean complete ==="
