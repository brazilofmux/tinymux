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

echo "Checking for required tools..."
if ! command -v xdelta3 &> /dev/null; then
    echo "xdelta3 is required but not found. Please install using apt-cyg install xdelta3"
    exit 1
fi

# Process Windows source distribution
echo "Building Windows source distribution..."

# Read file lists
patchable_files=$(cat win32/TOC.src.patchable)
unpatched_files=$(cat win32/TOC.src.unpatched)
remove_files=$(cat win32/TOC.src.removed)

# Function to clean files
clean_files() {
    local dir=$1
    local remove_list=$2
    
    for file in $remove_list; do
        if [ -e "$dir/$file" ]; then
            echo "Removing: $dir/$file"
            rm "$dir/$file"
        fi
    done
}

# Prepare directories
rm -rf ${NEW_DIR}_src $DISTRO_DIR
cp -r ${REFERENCE_DIR}_src $DISTRO_DIR
cp -r ${REFERENCE_DIR}_src ${NEW_DIR}_src

# Copy patchable files
echo "Copying patchable files for Windows source..."
for file in $patchable_files; do
    mkdir -p $(dirname "${NEW_DIR}_src/$file")
    cp "$CHANGES_DIR/$file" "${NEW_DIR}_src/$file"
done

# Special Windows configurations
cp $CHANGES_DIR/src/autoconf-win32.h ${NEW_DIR}_src/src/autoconf.h
cp $CHANGES_DIR/src/modules/autoconf-win32.h ${NEW_DIR}_src/src/modules/autoconf.h

# Remove files
echo "Cleaning up removed files for Windows source..."
clean_files "${NEW_DIR}_src" "$remove_files"

chmod -R u+rw $DISTRO_DIR ${NEW_DIR}_src

# Generate patch files using xdelta3
echo "Generating Windows source patch files..."
rm -rf patches_src
mkdir -p patches_src

find $DISTRO_DIR -type f | while read src_file; do
    rel_path=${src_file#$DISTRO_DIR/}
    new_file="${NEW_DIR}_src/$rel_path"
    
    if [ -f "$new_file" ]; then
        # Create directory structure in patches
        mkdir -p "patches_src/$(dirname $rel_path)"
        
        # Check if files are different
        if ! cmp -s "$src_file" "$new_file"; then
            echo "Creating patch for: $rel_path"
            xdelta3 -e -s "$src_file" "$new_file" "patches_src/$rel_path.vcdiff"
        fi
    else
        # File was removed
        echo "File removed: $rel_path"
        echo "REMOVED" > "patches_src/$rel_path.removed"
    fi
done

# Find new files
find ${NEW_DIR}_src -type f | while read new_file; do
    rel_path=${new_file#${NEW_DIR}_src/}
    src_file="$DISTRO_DIR/$rel_path"
    
    if [ ! -f "$src_file" ]; then
        # File was added
        echo "File added: $rel_path"
        mkdir -p "patches_src/$(dirname $rel_path)"
        cp "$new_file" "patches_src/$rel_path.new"
    fi
done

# Create a manifest file
echo "Creating patch manifest for Windows source..."
find patches_src -type f | sort > patches_src/MANIFEST

# Create a combined patch archive
echo "Creating Windows source patch archive..."
if [ -e mux-$OLD_VERSION-$NEW_VERSION.win32.src.patch.tar.gz ]; then
    rm mux-$OLD_VERSION-$NEW_VERSION.win32.src.patch.tar.gz
fi
tar czf mux-$OLD_VERSION-$NEW_VERSION.win32.src.patch.tar.gz patches_src/

# Build source distribution
rm -rf $DISTRO_DIR

# Copy unpatched files
echo "Copying unpatched files for Windows source..."
for file in $unpatched_files; do
    mkdir -p $(dirname "${NEW_DIR}_src/$file")
    cp "$CHANGES_DIR/$file" "${NEW_DIR}_src/$file"
done

# Final cleanup
echo "Final cleanup of removed files for Windows source..."
clean_files "${NEW_DIR}_src" "$remove_files"

cp -r ${NEW_DIR}_src $DISTRO_DIR

# Create archives
echo "Creating Windows source archives..."
if [ -e mux-$NEW_VERSION.win32.src.tar.gz ]; then
    rm mux-$NEW_VERSION.win32.src.tar.gz
fi
tar czf "mux-$NEW_VERSION.win32.src.tar.gz" $DISTRO_DIR

if [ -e mux-$NEW_VERSION.win32.src.zip ]; then
    rm mux-$NEW_VERSION.win32.src.zip
fi
zip -r "mux-$NEW_VERSION.win32.src.zip" $DISTRO_DIR

# Process Windows binary distribution
echo "Building Windows binary distribution..."

# Read file lists
patchable_files=$(cat win32/TOC.bin.patchable)
unpatched_files=$(cat win32/TOC.bin.unpatched)
remove_files=$(cat win32/TOC.bin.removed)

# Prepare directories
rm -rf ${NEW_DIR}_bin $DISTRO_DIR
cp -r ${REFERENCE_DIR}_bin $DISTRO_DIR
cp -r ${REFERENCE_DIR}_bin ${NEW_DIR}_bin

# Copy patchable files
echo "Copying patchable files for Windows binary..."
for file in $patchable_files; do
    mkdir -p $(dirname "${NEW_DIR}_bin/$file")
    cp "$CHANGES_DIR/$file" "${NEW_DIR}_bin/$file"
done

# Remove files
echo "Cleaning up removed files for Windows binary..."
clean_files "${NEW_DIR}_bin" "$remove_files"

chmod -R u+rw $DISTRO_DIR ${NEW_DIR}_bin

# Generate patch files using xdelta3
echo "Generating Windows binary patch files..."
rm -rf patches_bin
mkdir -p patches_bin

find $DISTRO_DIR -type f | while read bin_file; do
    rel_path=${bin_file#$DISTRO_DIR/}
    new_file="${NEW_DIR}_bin/$rel_path"
    
    if [ -f "$new_file" ]; then
        # Create directory structure in patches
        mkdir -p "patches_bin/$(dirname $rel_path)"
        
        # Check if files are different
        if ! cmp -s "$bin_file" "$new_file"; then
            echo "Creating patch for: $rel_path"
            xdelta3 -e -s "$bin_file" "$new_file" "patches_bin/$rel_path.vcdiff"
        fi
    else
        # File was removed
        echo "File removed: $rel_path"
        echo "REMOVED" > "patches_bin/$rel_path.removed"
    fi
done

# Find new files
find ${NEW_DIR}_bin -type f | while read new_file; do
    rel_path=${new_file#${NEW_DIR}_bin/}
    bin_file="$DISTRO_DIR/$rel_path"
    
    if [ ! -f "$bin_file" ]; then
        # File was added
        echo "File added: $rel_path"
        mkdir -p "patches_bin/$(dirname $rel_path)"
        cp "$new_file" "patches_bin/$rel_path.new"
    fi
done

# Create a manifest file
echo "Creating patch manifest for Windows binary..."
find patches_bin -type f | sort > patches_bin/MANIFEST

# Create a combined patch archive
echo "Creating Windows binary patch archive..."
if [ -e mux-$OLD_VERSION-$NEW_VERSION.win32.bin.patch.tar.gz ]; then
    rm mux-$OLD_VERSION-$NEW_VERSION.win32.bin.patch.tar.gz
fi
tar czf mux-$OLD_VERSION-$NEW_VERSION.win32.bin.patch.tar.gz patches_bin/

# Build binary distribution
rm -rf $DISTRO_DIR

# Copy unpatched files
echo "Copying unpatched files for Windows binary..."
for file in $unpatched_files; do
    mkdir -p $(dirname "${NEW_DIR}_bin/$file")
    cp "$CHANGES_DIR/$file" "${NEW_DIR}_bin/$file"
done

# Final cleanup
echo "Final cleanup of removed files for Windows binary..."
clean_files "${NEW_DIR}_bin" "$remove_files"

cp -r ${NEW_DIR}_bin $DISTRO_DIR

# Create archives
echo "Creating Windows binary archives..."
if [ -e mux-$NEW_VERSION.win32.bin.tar.gz ]; then
    rm mux-$NEW_VERSION.win32.bin.tar.gz
fi
tar czf "mux-$NEW_VERSION.win32.bin.tar.gz" $DISTRO_DIR

if [ -e mux-$NEW_VERSION.win32.bin.zip ]; then
    rm mux-$NEW_VERSION.win32.bin.zip
fi
zip -r "mux-$NEW_VERSION.win32.bin.zip" $DISTRO_DIR

# Generate checksums
echo "Generating checksums..."
sha256sum mux-$OLD_VERSION-$NEW_VERSION.win32.src.patch.tar.gz > mux-$OLD_VERSION-$NEW_VERSION.win32.src.patch.tar.gz.sha256
sha256sum mux-$NEW_VERSION.win32.src.tar.gz > mux-$NEW_VERSION.win32.src.tar.gz.sha256
sha256sum mux-$NEW_VERSION.win32.src.zip > mux-$NEW_VERSION.win32.src.zip.sha256
sha256sum mux-$OLD_VERSION-$NEW_VERSION.win32.bin.patch.tar.gz > mux-$OLD_VERSION-$NEW_VERSION.win32.bin.patch.tar.gz.sha256
sha256sum mux-$NEW_VERSION.win32.bin.tar.gz > mux-$NEW_VERSION.win32.bin.tar.gz.sha256
sha256sum mux-$NEW_VERSION.win32.bin.zip > mux-$NEW_VERSION.win32.bin.zip.sha256

echo "Windows build process completed successfully!"
