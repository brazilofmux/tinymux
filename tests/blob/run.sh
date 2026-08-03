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
#   Three checks, because they have different portability:
#
#   0. DO THE TWO COMMITTED COPIES AGREE.  mux/rv64/ and
#      mux/game/bin/ are both committed and can drift apart.
#      Needs no compiler, so it runs everywhere.
#
#   1. DOES IT BUILD.  Runs anywhere a cross-compiler exists, whatever
#      its version.  This is the check that would have caught #1402.
#
#   2. DOES IT MATCH THE COMMITTED ARTIFACT.  The build is byte-repro-
#      ducible for a GIVEN compiler, but NOT across compiler versions,
#      so this one is only meaningful on a box running the toolchain
#      that produced the artifact.  The fleet is deliberately not
#      uniform -- newest GCC on macOS, a release or two behind on the
#      Linux boxes, and Windows cannot cross-compile at all -- so a
#      blind hash comparison would report staleness on every box except
#      the one that last regenerated the blob.  softlib.rv64.toolchain
#      records the builder; compare hashes only when it matches, and say
#      plainly when it does not.
#
#   Whoever regenerates the blob owns the hash check, since `make -C
#   mux/rv64` rewrites the stamp as a side effect of building.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
RV64="$REPO_ROOT/mux/rv64"
SHIPPED="$RV64/softlib.rv64"
STAMP="$RV64/softlib.rv64.toolchain"

hash_of() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        sha256sum "$1" | awk '{print $1}'
    fi
}

# ---- Check 0: do the two committed copies agree?  Portable everywhere. ----
#
#   `make -C mux/rv64 install` copies the artifact to mux/game/bin/, and BOTH
#   copies are committed.  They can therefore drift independently, and did:
#   241c89bd8 regenerated mux/rv64/softlib.rv64 and left game/bin/ holding the
#   previous binary, so master carried two different blobs at once for several
#   hours.  Nothing noticed, because each copy is self-consistent on its own
#   and the checks below only ever looked at mux/rv64/.
#
#   game/bin/ is the copy a fresh checkout has without running the rv64 install
#   step, so it is the one an unsuspecting build actually loads.
#
#   This is a plain file comparison with no cross-compiler involved, so unlike
#   everything below it runs on every box -- including Windows and end-user
#   machines, which cannot build the blob at all.

INSTALLED="$REPO_ROOT/mux/game/bin/softlib.rv64"

if [ ! -f "$SHIPPED" ]; then
    echo "FAIL: $SHIPPED is missing."
    exit 1
fi

if [ ! -f "$INSTALLED" ]; then
    echo "FAIL: $INSTALLED is missing."
    echo "  Both copies are committed artifacts.  Restore it with:"
    echo "      make -C mux/rv64 install"
    exit 1
fi

if [ "$(hash_of "$SHIPPED")" != "$(hash_of "$INSTALLED")" ]; then
    echo "FAIL: the two committed copies of softlib.rv64 disagree."
    echo "  mux/rv64/softlib.rv64:     $(hash_of "$SHIPPED")"
    echo "  mux/game/bin/softlib.rv64: $(hash_of "$INSTALLED")"
    echo
    echo "  Regenerating the blob updates mux/rv64/; only 'install' copies it"
    echo "  to game/bin/.  A regeneration commit that forgets the second file"
    echo "  leaves the tree with two different blobs, each looking correct in"
    echo "  isolation.  Fix with:"
    echo "      make -C mux/rv64 install"
    echo "  and commit BOTH paths."
    exit 1
fi

echo "ok: both committed copies of softlib.rv64 agree."

CC_RV=""
for cand in riscv-none-elf-gcc riscv64-unknown-elf-gcc riscv64-linux-gnu-gcc; do
    if command -v "$cand" >/dev/null 2>&1; then
        CC_RV=$cand
        break
    fi
done

if [ -z "$CC_RV" ]; then
    echo "SKIP: no RISC-V cross-compiler on this box (the normal case for"
    echo "      end users, and for Windows, which cannot build the blob)."
    exit 0
fi

HERE="$($CC_RV -dumpmachine) $($CC_RV -dumpversion)"
before=$(hash_of "$SHIPPED")

cp "$SHIPPED" "$SHIPPED.staleguard.bak"
if [ -f "$STAMP" ]; then
    cp "$STAMP" "$STAMP.staleguard.bak"
fi
restore() {
    mv -f "$SHIPPED.staleguard.bak" "$SHIPPED" 2>/dev/null
    if [ -f "$STAMP.staleguard.bak" ]; then
        mv -f "$STAMP.staleguard.bak" "$STAMP" 2>/dev/null
    else
        rm -f "$STAMP" 2>/dev/null
    fi
    return 0
}
trap restore EXIT

# ---- Check 1: does the blob build at all?  Portable across toolchains. ----

echo "==> Rebuilding softlib.rv64 with $CC_RV ($HERE)"
if ! make -C "$RV64" clean >/dev/null 2>&1 \
   || ! make -C "$RV64" CC="$CC_RV" >/dev/null 2>&1; then
    echo "FAIL: the blob build is broken on this box."
    echo "  A broken blob build is invisible in normal use -- nothing in"
    echo "  'make test' regenerates the artifact -- so every edit to"
    echo "  mux/rv64/src/ silently ships the old binary (#1402 sat like"
    echo "  this for days).  Run 'make -C mux/rv64' to see the error."
    exit 1
fi

# ---- Check 2: does it match?  Only where the toolchain agrees. ----

if [ ! -f "$STAMP.staleguard.bak" ]; then
    echo "ok: blob builds cleanly."
    echo "    Staleness not checked: no committed toolchain stamp, so the"
    echo "    artifact cannot be attributed to a compiler.  Regenerating"
    echo "    the blob writes mux/rv64/softlib.rv64.toolchain; commit it"
    echo "    to enable the comparison."
    exit 0
fi

BUILT_BY=$(sed -n 's/^toolchain: //p' "$STAMP.staleguard.bak")

if [ "$BUILT_BY" != "$HERE" ]; then
    echo "ok: blob builds cleanly."
    echo "    Staleness NOT checked -- the committed artifact was built by"
    echo "      $BUILT_BY"
    echo "    and this box has"
    echo "      $HERE"
    echo "    The build is byte-reproducible only within one toolchain, so"
    echo "    comparing across versions would report staleness that isn't"
    echo "    there.  A box running the recorded toolchain covers that."
    exit 0
fi

after=$(hash_of "$SHIPPED")

if [ "$before" != "$after" ]; then
    echo "FAIL: softlib.rv64 is STALE relative to mux/rv64/src/."
    echo "  committed: $before"
    echo "  rebuilt:   $after"
    echo "  toolchain: $HERE (the same one that built the artifact)"
    echo
    echo "  The JIT loads the committed artifact, so whatever changed in"
    echo "  the source is inert at run time while this suite stays green."
    echo "  Regenerate and commit it (the stamp updates automatically):"
    echo "      make -C mux/rv64 && make -C mux/rv64 install"
    exit 1
fi

echo "ok: softlib.rv64 matches its source ($before)"
echo "    verified against $HERE"
exit 0
