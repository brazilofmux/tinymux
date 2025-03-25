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

# Generate patch file
echo "Generating Windows source patch file..."
python3 -c "
import difflib, sys, os, pathlib

def compare_dirs(dir1, dir2, output):
    differences = []
    dir1 = pathlib.Path(dir1)
    dir2 = pathlib.Path(dir2)

    for path1 in dir1.glob('**/*'):
        if path1.is_file():
            rel_path = path1.relative_to(dir1)
            path2 = dir2 / rel_path

            if path2.exists() and path2.is_file():
                try:
                    with open(path1, 'r', errors='replace') as f1, open(path2, 'r', errors='replace') as f2:
                        text1 = f1.readlines()
                        text2 = f2.readlines()
                        diff = list(difflib.unified_diff(
                            text1, text2,
                            fromfile=str(path1),
                            tofile=str(path2)
                        ))
                        if diff:
                            differences.extend(diff)
                except Exception as e:
                    print(f'Skipping binary or problematic file: {path1} ({e})')

    with open(output, 'w', encoding='utf-8') as f:
        f.writelines(differences)

compare_dirs('$DISTRO_DIR', '${NEW_DIR}_src', 'mux-$OLD_VERSION-$NEW_VERSION.win32.src.patch')
" || echo "Warning: Python patch generation failed"

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

# Generate patch file
echo "Generating Windows binary patch file..."
python3 -c "
import difflib, sys, os, pathlib

def compare_dirs(dir1, dir2, output):
    differences = []
    dir1 = pathlib.Path(dir1)
    dir2 = pathlib.Path(dir2)

    for path1 in dir1.glob('**/*'):
        if path1.is_file():
            rel_path = path1.relative_to(dir1)
            path2 = dir2 / rel_path

            if path2.exists() and path2.is_file():
                try:
                    with open(path1, 'r', errors='replace') as f1, open(path2, 'r', errors='replace') as f2:
                        text1 = f1.readlines()
                        text2 = f2.readlines()
                        diff = list(difflib.unified_diff(
                            text1, text2,
                            fromfile=str(path1),
                            tofile=str(path2)
                        ))
                        if diff:
                            differences.extend(diff)
                except Exception as e:
                    print(f'Skipping binary or problematic file: {path1} ({e})')

    with open(output, 'w', encoding='utf-8') as f:
        f.writelines(differences)

compare_dirs('$DISTRO_DIR', '${NEW_DIR}_bin', 'mux-$OLD_VERSION-$NEW_VERSION.win32.bin.patch')
" || echo "Warning: Python patch generation failed"

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
sha256sum mux-$OLD_VERSION-$NEW_VERSION.win32.src.patch > mux-$OLD_VERSION-$NEW_VERSION.win32.src.patch.sha256
sha256sum mux-$NEW_VERSION.win32.src.tar.gz > mux-$NEW_VERSION.win32.src.tar.gz.sha256
sha256sum mux-$NEW_VERSION.win32.src.zip > mux-$NEW_VERSION.win32.src.zip.sha256
sha256sum mux-$OLD_VERSION-$NEW_VERSION.win32.bin.patch > mux-$OLD_VERSION-$NEW_VERSION.win32.bin.patch.sha256
sha256sum mux-$NEW_VERSION.win32.bin.tar.gz > mux-$NEW_VERSION.win32.bin.tar.gz.sha256
sha256sum mux-$NEW_VERSION.win32.bin.zip > mux-$NEW_VERSION.win32.bin.zip.sha256

echo "Windows build process completed successfully!"
