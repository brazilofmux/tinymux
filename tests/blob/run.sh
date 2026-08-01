#!/bin/bash
#
#   run.sh — is the shipped softlib.rv64 what its source produces?
#
#   softlib.rv64 is a checked-in binary that the JIT loads at run time.
#   Nothing in the normal build regenerates it, so a change to
#   mux/rv64/src/softlib.c is INERT until someone rebuilds by hand --
#   and the suite stays green while the server runs the old code.  That
#   is not hypothetical: #1915's first fix shipped source-only and did
#   nothing, and the blob build had been broken since #1402 (a missing
#   strtoll declaration) with no way to notice, because the one place
#   that rebuilds it (testcases/tools/Build.sh) downgraded the failure
#   to a warning and copied the stale artifact.
#
#   So: if this box can build the blob, it must match what is committed.
#   Two failures are caught by one check -- a blob that cannot be built,
#   and a blob that is stale relative to its source.
#
#   Skips cleanly without a cross-toolchain, which is the normal case for
#   end users and most developers.  The build is byte-reproducible with a
#   given toolchain, which is what makes the comparison meaningful.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
RV64="$REPO_ROOT/mux/rv64"
SHIPPED="$RV64/softlib.rv64"

hash_of() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        sha256sum "$1" | awk '{print $1}'
    fi
}

CC_RV=""
for cand in riscv-none-elf-gcc riscv64-unknown-elf-gcc riscv64-linux-gnu-gcc; do
    if command -v "$cand" >/dev/null 2>&1; then
        CC_RV=$cand
        break
    fi
done

if [ -z "$CC_RV" ]; then
    echo "SKIP: no RISC-V cross-compiler; cannot verify softlib.rv64 against its source."
    exit 0
fi

if [ ! -f "$SHIPPED" ]; then
    echo "FAIL: $SHIPPED is missing."
    exit 1
fi

before=$(hash_of "$SHIPPED")
cp "$SHIPPED" "$SHIPPED.staleguard.bak"
restore() { mv -f "$SHIPPED.staleguard.bak" "$SHIPPED" 2>/dev/null || true; }
trap restore EXIT

echo "==> Rebuilding softlib.rv64 with $CC_RV to compare against the committed artifact"
if ! make -C "$RV64" clean >/dev/null 2>&1 \
   || ! make -C "$RV64" CC="$CC_RV" >/dev/null 2>&1; then
    echo "FAIL: the blob build is broken on this box."
    echo "  A broken blob build is invisible in normal use -- nothing in"
    echo "  'make test' regenerates the artifact -- so every edit to"
    echo "  mux/rv64/src/ silently ships the old binary (#1402 sat like"
    echo "  this for days).  Run 'make -C mux/rv64' to see the error."
    exit 1
fi

after=$(hash_of "$SHIPPED")

if [ "$before" != "$after" ]; then
    echo "FAIL: softlib.rv64 is STALE relative to mux/rv64/src/."
    echo "  committed: $before"
    echo "  rebuilt:   $after"
    echo
    echo "  The JIT loads the committed artifact, so whatever changed in"
    echo "  the source is inert at run time while this suite stays green."
    echo "  Regenerate and commit it:"
    echo "      make -C mux/rv64 && make -C mux/rv64 install"
    exit 1
fi

echo "ok: softlib.rv64 matches its source ($before)"
exit 0
