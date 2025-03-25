#!/bin/bash
#
# REQUIRED: ReferenceDir must already exist. It may be created by untaring a
# previous distribution.
#

# Error handling
set -e  # Exit on error
set -o pipefail

# Version information
OLD_BUILD=4
OLD_VERSION="2.13.0.$OLD_BUILD"
NEW_BUILD=5
NEW_VERSION="2.13.0.$NEW_BUILD"

# Directory structure
CHANGES_DIR=mux
REFERENCE_DIR=mux2.13_$OLD_BUILD
DISTRO_DIR=mux2.13
NEW_DIR=mux2.13_$NEW_BUILD

# Load file lists
patchable_files=$(cat unix/TOC.patchable)
unpatched_files=$(cat unix/TOC.unpatched)
remove_files=$(cat unix/TOC.removed)

echo "Building Unix distribution for MUX $NEW_VERSION..."

# Function to clean files
clean_files() {
    local dir=$1

    for file in $remove_files; do
        if [ -e "$dir/$file" ]; then
            echo "Removing: $dir/$file"
            rm "$dir/$file"
        fi
    done
}

# Build patchfile
echo "Preparing directories for patch generation..."
rm -rf $NEW_DIR $DISTRO_DIR
cp -r $REFERENCE_DIR $DISTRO_DIR
cp -r $REFERENCE_DIR $NEW_DIR

echo "Copying patchable files..."
for file in $patchable_files; do
    # Ensure directory exists
    mkdir -p $(dirname "$NEW_DIR/$file")
    cp "$CHANGES_DIR/$file" "$NEW_DIR/$file"
done

echo "Cleaning up removed files..."
clean_files $NEW_DIR

echo "Generating patch file..."
if ! diff -Naudr $DISTRO_DIR $NEW_DIR > mux-$OLD_VERSION-$NEW_VERSION.unix.patch; then
    echo "Warning: diff command returned non-zero exit code $?, but this is often normal for diff"
    echo "Checking if patch file was created..."
    if [ -s mux-$OLD_VERSION-$NEW_VERSION.unix.patch ]; then
        echo "Patch file was created successfully."
    else
        echo "ERROR: Patch file is empty or wasn't created. Exiting."
        exit 1
    fi
fi

if [ -e mux-$OLD_VERSION-$NEW_VERSION.unix.patch.gz ]; then
    rm mux-$OLD_VERSION-$NEW_VERSION.unix.patch.gz
fi
echo "Compressing patch file..."
gzip -9 mux-$OLD_VERSION-$NEW_VERSION.unix.patch

# Build tarball
echo "Preparing for tarball creation..."
rm -rf $DISTRO_DIR

echo "Copying unpatched files..."
for file in $unpatched_files; do
    # Ensure directory exists
    mkdir -p $(dirname "$NEW_DIR/$file")
    cp "$CHANGES_DIR/$file" "$NEW_DIR/$file"
done

echo "Final cleanup of removed files..."
clean_files $NEW_DIR

echo "Creating distribution directory..."
cp -r $NEW_DIR $DISTRO_DIR

echo "Creating compressed tarballs..."
if [ -e mux-$NEW_VERSION.unix.tar.gz ]; then
    rm mux-$NEW_VERSION.unix.tar.gz
fi
tar czf mux-$NEW_VERSION.unix.tar.gz $DISTRO_DIR

if [ -e mux-$NEW_VERSION.unix.tar.bz2 ]; then
    rm mux-$NEW_VERSION.unix.tar.bz2
fi
tar cjf mux-$NEW_VERSION.unix.tar.bz2 $DISTRO_DIR

# Generate checksums
echo "Generating checksums..."
sha256sum mux-$OLD_VERSION-$NEW_VERSION.unix.patch.gz > mux-$OLD_VERSION-$NEW_VERSION.unix.patch.gz.sha256
sha256sum mux-$NEW_VERSION.unix.tar.gz > mux-$NEW_VERSION.unix.tar.gz.sha256
sha256sum mux-$NEW_VERSION.unix.tar.bz2 > mux-$NEW_VERSION.unix.tar.bz2.sha256

echo "Unix build process completed successfully!"
