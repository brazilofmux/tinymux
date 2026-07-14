#!/bin/bash
#
# Build the Unix source distribution: a full tarball plus an upgrade patch
# from the previous release.
#
# REQUIRED: the reference directory (mux2.14_$OLD_BUILD) must already exist.
# Create it by untarring the previous distribution, e.g.:
#     tar xzf mux-2.14.0.$OLD_BUILD.unix.tar.gz && mv mux2.14 mux2.14_$OLD_BUILD
#
# BINARY FILES: a text unified diff cannot reliably carry binary content
# (embedded NULs are mangled by diff -a and are unportable across patch
# implementations).  Any patchable file that contains a NUL byte is therefore
# EXCLUDED from the text patch and instead shipped whole in a companion
# archive (mux-OLD-NEW.unix.blobs.tar.gz).  The generated APPLY.txt documents
# the two-step upgrade.  The full tarball always contains every file.
#

# Error handling
set -e            # Exit on error
set -u            # Error on unset variables
set -o pipefail   # A failure anywhere in a pipe fails the pipe

# Version information
OLD_BUILD=8
OLD_VERSION="2.14.0.$OLD_BUILD"
NEW_BUILD=9
NEW_VERSION="2.14.0.$NEW_BUILD"

# Directory structure
CHANGES_DIR=mux
REFERENCE_DIR=mux2.14_$OLD_BUILD
DISTRO_DIR=mux2.14
NEW_DIR=mux2.14_$NEW_BUILD

# Artifact names
PATCH_FILE="mux-$OLD_VERSION-$NEW_VERSION.unix.patch"
BLOBS_FILE="mux-$OLD_VERSION-$NEW_VERSION.unix.blobs.tar.gz"
APPLY_FILE="mux-$OLD_VERSION-$NEW_VERSION.unix.APPLY.txt"
TARGZ_FILE="mux-$NEW_VERSION.unix.tar.gz"
TARBZ2_FILE="mux-$NEW_VERSION.unix.tar.bz2"

echo "Building Unix distribution for MUX $NEW_VERSION..."

# --- Preconditions --------------------------------------------------------

if [ ! -d "$REFERENCE_DIR" ]; then
    echo "ERROR: reference directory '$REFERENCE_DIR' does not exist."
    echo "       Create it from the previous release, e.g.:"
    echo "           tar xzf mux-$OLD_VERSION.unix.tar.gz"
    echo "           mv $DISTRO_DIR $REFERENCE_DIR"
    exit 1
fi
for toc in unix/TOC.patchable unix/TOC.unpatched unix/TOC.removed; do
    if [ ! -f "$toc" ]; then
        echo "ERROR: missing table-of-contents file '$toc'."
        exit 1
    fi
done
if [ ! -d "$CHANGES_DIR" ]; then
    echo "ERROR: source directory '$CHANGES_DIR' does not exist."
    exit 1
fi

# Load file lists
patchable_files=$(cat unix/TOC.patchable)
unpatched_files=$(cat unix/TOC.unpatched)
remove_files=$(cat unix/TOC.removed)

# --- Helpers --------------------------------------------------------------

# Remove the files listed in TOC.removed from the given directory.
clean_files() {
    local dir="$1"
    local file
    for file in $remove_files; do
        if [ -e "$dir/$file" ]; then
            echo "Removing: $dir/$file"
            rm "$dir/$file"
        fi
    done
}

# True if the file contains a NUL byte (i.e. is binary and cannot be carried
# in a text patch).  Portable: strip NULs and compare against the original.
is_binary() {
    ! LC_ALL=C tr -d '\000' < "$1" | cmp -s - "$1"
}

# --- Build the patch tree -------------------------------------------------

echo "Preparing directories for patch generation..."
rm -rf "$NEW_DIR" "$DISTRO_DIR"
cp -r "$REFERENCE_DIR" "$DISTRO_DIR"
cp -r "$REFERENCE_DIR" "$NEW_DIR"

echo "Copying patchable files..."
for file in $patchable_files; do
    mkdir -p "$(dirname "$NEW_DIR/$file")"
    cp "$CHANGES_DIR/$file" "$NEW_DIR/$file"
done

echo "Cleaning up removed files..."
clean_files "$NEW_DIR"

# --- Identify binary blobs among the patchable files ----------------------

echo "Scanning for binary files (excluded from the text patch)..."
binary_paths=()
for file in $patchable_files; do
    if [ -f "$NEW_DIR/$file" ] && is_binary "$NEW_DIR/$file"; then
        binary_paths+=("$file")
        echo "  binary: $file"
    fi
done

# Which blobs actually changed (or are new)?  Only those need delivering to
# upgraders; unchanged blobs are already present in their tree.  Determined
# now, while $NEW_DIR still holds the blobs.
changed_blobs=()
for b in ${binary_paths[@]+"${binary_paths[@]}"}; do
    if [ ! -f "$DISTRO_DIR/$b" ] || ! cmp -s "$DISTRO_DIR/$b" "$NEW_DIR/$b"; then
        changed_blobs+=("$b")
    fi
done

# --- Companion archive for changed binary blobs ---------------------------

rm -f "$BLOBS_FILE" "$APPLY_FILE"
if [ "${#changed_blobs[@]}" -gt 0 ]; then
    echo "Packaging ${#changed_blobs[@]} changed binary blob(s) into $BLOBS_FILE..."
    # Stage the blobs under the distribution's on-disk name (DISTRO_DIR) so a
    # plain "tar xzf" overlays the upgraded tree in place.
    blob_stage=".blobstage_$NEW_BUILD"
    rm -rf "$blob_stage"
    for b in "${changed_blobs[@]}"; do
        mkdir -p "$blob_stage/$DISTRO_DIR/$(dirname "$b")"
        cp "$NEW_DIR/$b" "$blob_stage/$DISTRO_DIR/$b"
        echo "  blob: $b"
    done
    tar czf "$BLOBS_FILE" -C "$blob_stage" "$DISTRO_DIR"
    rm -rf "$blob_stage"

    # Human-readable upgrade instructions (auditable; no opaque script).
    {
        echo "Upgrading TinyMUX $OLD_VERSION -> $NEW_VERSION (Unix)"
        echo
        echo "From the directory that contains your '$DISTRO_DIR/' tree:"
        echo
        echo "  1. Apply the source patch:"
        echo "       gunzip -c $PATCH_FILE.gz | patch -p0"
        echo
        echo "  2. Overlay the prebuilt binary blob(s) that cannot be carried"
        echo "     in a text patch:"
        echo "       tar xzf $BLOBS_FILE"
        echo
        echo "  3. Rebuild as usual."
        echo
        echo "Blobs delivered in $BLOBS_FILE:"
        for b in "${changed_blobs[@]}"; do
            echo "  $DISTRO_DIR/$b"
        done
        echo
        echo "Verify signatures/checksums (.asc/.sha256) on every artifact"
        echo "before applying."
    } > "$APPLY_FILE"
else
    echo "No binary blobs changed; no companion archive needed."
fi

# --- Generate the text patch (binaries stashed out for clean headers) -----

# Remove the binaries from BOTH trees so diff never sees them: the patch is
# pure text with conventional headers, and no NUL bytes can leak in.  $NEW_DIR
# is restored afterwards (it feeds the self-test and the full tarball);
# $DISTRO_DIR is rebuilt from $NEW_DIR later regardless.
for b in ${binary_paths[@]+"${binary_paths[@]}"}; do
    rm -f "$DISTRO_DIR/$b" "$NEW_DIR/$b"
done

echo "Generating patch file..."
if ! diff -Naudr "$DISTRO_DIR" "$NEW_DIR" > "$PATCH_FILE"; then
    # diff exits 1 when files differ; that is the normal case here.
    if [ ! -s "$PATCH_FILE" ]; then
        echo "ERROR: patch file is empty or wasn't created. Exiting."
        exit 1
    fi
fi

# Restore the new blobs to $NEW_DIR for the self-test and the tarball.
for b in ${binary_paths[@]+"${binary_paths[@]}"}; do
    mkdir -p "$(dirname "$NEW_DIR/$b")"
    cp "$CHANGES_DIR/$b" "$NEW_DIR/$b"
done

# Guard: the text patch must contain no NUL bytes.  If it does, a binary
# leaked past the stash and the patch would be unportable/corrupt.
if is_binary "$PATCH_FILE"; then
    echo "ERROR: '$PATCH_FILE' contains NUL bytes (a binary leaked into the"
    echo "       text patch). Refusing to ship a corrupt patch."
    exit 1
fi
echo "Patch is text-only ($(grep -ac '^diff -Naudr' "$PATCH_FILE") files)."

# --- Self-test: patch + blobs must reconstruct the new tree exactly -------

echo "Verifying the patch reconstructs $NEW_VERSION..."
verify_root=".verify_$NEW_BUILD"
rm -rf "$verify_root"
mkdir -p "$verify_root"
cp -r "$REFERENCE_DIR" "$verify_root/$DISTRO_DIR"
(
    cd "$verify_root"
    patch -p0 --no-backup-if-mismatch < "../$PATCH_FILE" > patch.log 2>&1
)
if [ "${#changed_blobs[@]}" -gt 0 ]; then
    tar xzf "$BLOBS_FILE" -C "$verify_root"
fi
if diff -Naqr "$verify_root/$DISTRO_DIR" "$NEW_DIR" > "$verify_root/diff.log" 2>&1; then
    echo "Self-test passed: patch + blobs == $NEW_VERSION tree."
else
    echo "ERROR: self-test FAILED. Applying the patch (and blobs) does not"
    echo "       reproduce the $NEW_VERSION tree. Differences:"
    cat "$verify_root/diff.log"
    exit 1
fi
rm -rf "$verify_root"

# --- Compress the patch ---------------------------------------------------

rm -f "$PATCH_FILE.gz"
echo "Compressing patch file..."
gzip -9 "$PATCH_FILE"

# --- Build the full tarball -----------------------------------------------

echo "Preparing for tarball creation..."
rm -rf "$DISTRO_DIR"

echo "Copying unpatched files..."
for file in $unpatched_files; do
    mkdir -p "$(dirname "$NEW_DIR/$file")"
    cp "$CHANGES_DIR/$file" "$NEW_DIR/$file"
done

echo "Final cleanup of removed files..."
clean_files "$NEW_DIR"

echo "Creating distribution directory..."
cp -r "$NEW_DIR" "$DISTRO_DIR"

echo "Creating compressed tarballs..."
rm -f "$TARGZ_FILE" "$TARBZ2_FILE"
tar czf "$TARGZ_FILE" "$DISTRO_DIR"
tar cjf "$TARBZ2_FILE" "$DISTRO_DIR"

# --- Verify archive integrity ---------------------------------------------

echo "Verifying archives..."
gzip -t "$PATCH_FILE.gz"
gzip -t "$TARGZ_FILE"
tar tjf "$TARBZ2_FILE" > /dev/null
if [ "${#changed_blobs[@]}" -gt 0 ]; then
    gzip -t "$BLOBS_FILE"
fi

# --- Generate checksums ---------------------------------------------------

echo "Generating checksums..."
sha256sum "$PATCH_FILE.gz" > "$PATCH_FILE.gz.sha256"
sha256sum "$TARGZ_FILE"    > "$TARGZ_FILE.sha256"
sha256sum "$TARBZ2_FILE"   > "$TARBZ2_FILE.sha256"
if [ "${#changed_blobs[@]}" -gt 0 ]; then
    sha256sum "$BLOBS_FILE" > "$BLOBS_FILE.sha256"
fi

echo "Unix build process completed successfully!"
echo
echo "Artifacts:"
echo "  $TARGZ_FILE          (full source, gzip)"
echo "  $TARBZ2_FILE         (full source, bzip2)"
echo "  $PATCH_FILE.gz       (text upgrade patch, $OLD_VERSION -> $NEW_VERSION)"
if [ "${#changed_blobs[@]}" -gt 0 ]; then
    echo "  $BLOBS_FILE   (companion binary blobs)"
    echo "  $APPLY_FILE   (upgrade instructions)"
fi
echo "  ...plus a .sha256 for each archive. Remember to GPG-sign (.asc) all"
echo "  artifacts, including the companion blob archive."
