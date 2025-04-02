#!/bin/bash
#
# REQUIRED: ReferenceDir must already exist. It may be created by untaring a
# previous distribution.
#

# Error handling
set -e  # Exit on error
set -o pipefail

# Version information
OLD_BUILD=5
OLD_VERSION="2.13.0.$OLD_BUILD"
NEW_BUILD=6
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

# Function to check if a file is binary
is_binary() {
    if file --mime "$1" | grep -q "charset=binary"; then
        return 0  # True, it's binary
    else
        return 1  # False, it's not binary
    fi
}

# Function to process source and create patches
process_distribution() {
    local dist_type=$1  # "src" or "bin"
    
    echo "Building Windows $dist_type distribution..."
    
    # Read file lists
    local patchable_files=$(cat win32/TOC.$dist_type.patchable)
    local unpatched_files=$(cat win32/TOC.$dist_type.unpatched)
    local remove_files=$(cat win32/TOC.$dist_type.removed)
    
    # Prepare directories
    rm -rf ${NEW_DIR}_$dist_type $DISTRO_DIR patches_$dist_type
    cp -r ${REFERENCE_DIR}_$dist_type $DISTRO_DIR
    cp -r ${REFERENCE_DIR}_$dist_type ${NEW_DIR}_$dist_type
    mkdir -p patches_$dist_type/bin patches_$dist_type/text
    
    # Copy patchable files
    echo "Copying patchable files for Windows $dist_type..."
    for file in $patchable_files; do
        mkdir -p $(dirname "${NEW_DIR}_$dist_type/$file")
        cp "$CHANGES_DIR/$file" "${NEW_DIR}_$dist_type/$file"
    done
    
    # Special Windows configurations for source
    if [ "$dist_type" = "src" ]; then
        cp $CHANGES_DIR/src/autoconf-win32.h ${NEW_DIR}_$dist_type/src/autoconf.h
        cp $CHANGES_DIR/src/modules/autoconf-win32.h ${NEW_DIR}_$dist_type/src/modules/autoconf.h
    fi
    
    # Remove files
    echo "Cleaning up removed files for Windows $dist_type..."
    clean_files "${NEW_DIR}_$dist_type" "$remove_files"
    
    chmod -R u+rw $DISTRO_DIR ${NEW_DIR}_$dist_type
    
    # Create a master patch manifest
    echo "# Windows $dist_type patches for MUX $OLD_VERSION to $NEW_VERSION" > patches_$dist_type/MANIFEST.txt
    echo "# Generated on $(date)" >> patches_$dist_type/MANIFEST.txt
    echo "" >> patches_$dist_type/MANIFEST.txt
    echo "# Binary files (xdelta3 format):" >> patches_$dist_type/MANIFEST.txt
    
    # Process all files to create appropriate patches
    echo "Generating patches for Windows $dist_type..."
    
    # Track files for manifest
    binary_files=()
    text_files=()
    new_files=()
    removed_files=()
    
    # Create patches for changed files and track new/removed files
    find $DISTRO_DIR -type f | while read src_file; do
        rel_path=${src_file#$DISTRO_DIR/}
        new_file="${NEW_DIR}_$dist_type/$rel_path"
        
        if [ -f "$new_file" ]; then
            # Check if files differ
            if ! cmp -s "$src_file" "$new_file"; then
                if is_binary "$src_file" || is_binary "$new_file"; then
                    # Binary file - use xdelta3
                    mkdir -p "patches_$dist_type/bin/$(dirname $rel_path)"
                    xdelta3 -e -s "$src_file" "$new_file" "patches_$dist_type/bin/$rel_path.vcdiff"
                    binary_files+=("$rel_path")
                else
                    # Text file - use standard diff
                    mkdir -p "patches_$dist_type/text/$(dirname $rel_path)"
                    diff -u "$src_file" "$new_file" > "patches_$dist_type/text/$rel_path.patch" 2>/dev/null || true
                    text_files+=("$rel_path")
                fi
            fi
        else
            # File was removed
            removed_files+=("$rel_path")
        fi
    done
    
    # Detect new files
    find ${NEW_DIR}_$dist_type -type f | while read new_file; do
        rel_path=${new_file#${NEW_DIR}_$dist_type/}
        src_file="$DISTRO_DIR/$rel_path"
        
        if [ ! -f "$src_file" ]; then
            # File was added - copy directly
            if is_binary "$new_file"; then
                mkdir -p "patches_$dist_type/bin/$(dirname $rel_path)"
                cp "$new_file" "patches_$dist_type/bin/$rel_path.new"
            else
                mkdir -p "patches_$dist_type/text/$(dirname $rel_path)"
                cp "$new_file" "patches_$dist_type/text/$rel_path.new"
            fi
            new_files+=("$rel_path")
        fi
    done
    
    # Create metadata files for patch application
    echo "${binary_files[@]}" | tr ' ' '\n' | sort > patches_$dist_type/binary_files.txt
    echo "${text_files[@]}" | tr ' ' '\n' | sort > patches_$dist_type/text_files.txt
    echo "${new_files[@]}" | tr ' ' '\n' | sort > patches_$dist_type/new_files.txt
    echo "${removed_files[@]}" | tr ' ' '\n' | sort > patches_$dist_type/removed_files.txt
    
    # Complete the manifest
    cat patches_$dist_type/binary_files.txt | while read file; do
        echo "bin/$file.vcdiff" >> patches_$dist_type/MANIFEST.txt
    done
    
    echo "" >> patches_$dist_type/MANIFEST.txt
    echo "# Text files (unified diff format):" >> patches_$dist_type/MANIFEST.txt
    cat patches_$dist_type/text_files.txt | while read file; do
        echo "text/$file.patch" >> patches_$dist_type/MANIFEST.txt
    done
    
    echo "" >> patches_$dist_type/MANIFEST.txt
    echo "# New files:" >> patches_$dist_type/MANIFEST.txt
    cat patches_$dist_type/new_files.txt | while read file; do
        if is_binary "${NEW_DIR}_$dist_type/$file"; then
            echo "bin/$file.new" >> patches_$dist_type/MANIFEST.txt
        else
            echo "text/$file.new" >> patches_$dist_type/MANIFEST.txt
        fi
    done
    
    echo "" >> patches_$dist_type/MANIFEST.txt
    echo "# Removed files:" >> patches_$dist_type/MANIFEST.txt
    cat patches_$dist_type/removed_files.txt | while read file; do
        echo "$file" >> patches_$dist_type/MANIFEST.txt
    done
    
    # Create patch application script
    cat > patches_$dist_type/apply_patch.bat <<EOF
@echo off
echo MUX $OLD_VERSION to $NEW_VERSION Patch Applier
echo =====================================
echo.

if not exist xdelta3.exe (
    echo Error: xdelta3.exe not found in current directory
    echo Please download xdelta3.exe and place it in this directory
    exit /b 1
)

if not exist "%1" (
    echo Usage: apply_patch.bat [MUX_DIRECTORY]
    echo.
    echo Please provide the path to your MUX $OLD_VERSION installation
    exit /b 1
)

set MUX_DIR=%1
echo Using MUX directory: %MUX_DIR%
echo.

echo Applying binary patches...
for /F "tokens=*" %%f in (binary_files.txt) do (
    echo Patching: %%f
    xdelta3 -d -s "%MUX_DIR%\%%f" "bin\%%f.vcdiff" "%MUX_DIR%\%%f.new"
    move /Y "%MUX_DIR%\%%f.new" "%MUX_DIR%\%%f"
)

echo.
echo Applying text patches...
for /F "tokens=*" %%f in (text_files.txt) do (
    echo Patching: %%f
    patch "%MUX_DIR%\%%f" < "text\%%f.patch"
)

echo.
echo Copying new files...
for /F "tokens=*" %%f in (new_files.txt) do (
    if exist "bin\%%f.new" (
        echo Adding: %%f
        copy /Y "bin\%%f.new" "%MUX_DIR%\%%f"
    ) else if exist "text\%%f.new" (
        echo Adding: %%f
        copy /Y "text\%%f.new" "%MUX_DIR%\%%f"
    )
)

echo.
echo Removing deleted files...
for /F "tokens=*" %%f in (removed_files.txt) do (
    echo Removing: %%f
    if exist "%MUX_DIR%\%%f" del "%MUX_DIR%\%%f"
)

echo.
echo Patch application complete!
echo Your MUX has been updated to version $NEW_VERSION
EOF
    
    # Create a combined patch archive in both formats
    echo "Creating Windows $dist_type patch archives..."
    if [ -e mux-$OLD_VERSION-$NEW_VERSION.win32.$dist_type.patch.zip ]; then
        rm mux-$OLD_VERSION-$NEW_VERSION.win32.$dist_type.patch.zip
    fi
    zip -r mux-$OLD_VERSION-$NEW_VERSION.win32.$dist_type.patch.zip patches_$dist_type/
    
    # Also create .7z format
    if command -v 7z &> /dev/null; then
        if [ -e mux-$OLD_VERSION-$NEW_VERSION.win32.$dist_type.patch.7z ]; then
            rm mux-$OLD_VERSION-$NEW_VERSION.win32.$dist_type.patch.7z
        fi
        7z a mux-$OLD_VERSION-$NEW_VERSION.win32.$dist_type.patch.7z patches_$dist_type/
    else
        echo "7z command not found, skipping .7z archive creation"
    fi
    
    # Build complete distribution
    rm -rf $DISTRO_DIR
    
    # Copy unpatched files
    echo "Copying unpatched files for Windows $dist_type..."
    for file in $unpatched_files; do
        mkdir -p $(dirname "${NEW_DIR}_$dist_type/$file")
        cp "$CHANGES_DIR/$file" "${NEW_DIR}_$dist_type/$file"
    done
    
    # Final cleanup
    echo "Final cleanup of removed files for Windows $dist_type..."
    clean_files "${NEW_DIR}_$dist_type" "$remove_files"
    
    cp -r ${NEW_DIR}_$dist_type $DISTRO_DIR
    
    # Create full distribution archives
    echo "Creating Windows $dist_type full distribution archives..."
    
    # ZIP format (primary Windows format)
    if [ -e mux-$NEW_VERSION.win32.$dist_type.zip ]; then
        rm mux-$NEW_VERSION.win32.$dist_type.zip
    fi
    zip -r mux-$NEW_VERSION.win32.$dist_type.zip $DISTRO_DIR
    
    # 7z format (also common on Windows)
    if command -v 7z &> /dev/null; then
        if [ -e mux-$NEW_VERSION.win32.$dist_type.7z ]; then
            rm mux-$NEW_VERSION.win32.$dist_type.7z
        fi
        7z a mux-$NEW_VERSION.win32.$dist_type.7z $DISTRO_DIR
    else
        echo "7z command not found, skipping .7z archive creation"
    fi
    
    # Generate checksums
    echo "Generating checksums for Windows $dist_type..."
    sha256sum mux-$OLD_VERSION-$NEW_VERSION.win32.$dist_type.patch.zip > mux-$OLD_VERSION-$NEW_VERSION.win32.$dist_type.patch.zip.sha256
    sha256sum mux-$NEW_VERSION.win32.$dist_type.zip > mux-$NEW_VERSION.win32.$dist_type.zip.sha256
    
    if command -v 7z &> /dev/null; then
        sha256sum mux-$OLD_VERSION-$NEW_VERSION.win32.$dist_type.patch.7z > mux-$OLD_VERSION-$NEW_VERSION.win32.$dist_type.patch.7z.sha256
        sha256sum mux-$NEW_VERSION.win32.$dist_type.7z > mux-$NEW_VERSION.win32.$dist_type.7z.sha256
    fi
    
    # Generate a simple README with instructions
    cat > patches_$dist_type/README.txt <<EOF
MUX $OLD_VERSION to $NEW_VERSION Patch Instructions
=================================================

This patch package will update your MUX $OLD_VERSION installation to MUX $NEW_VERSION.

Requirements:
- xdelta3.exe (for binary patches)
- patch.exe (for text patches)
- A working MUX $OLD_VERSION installation

Instructions:
1. Download xdelta3.exe from https://github.com/jmacd/xdelta/releases
   and patch.exe from http://gnuwin32.sourceforge.net/packages/patch.htm
   
2. Extract this patch package to a folder

3. Copy xdelta3.exe and patch.exe to the same folder

4. Run apply_patch.bat and provide the path to your MUX installation:
   apply_patch.bat C:\Path\to\your\mux\installation

For Unix/Linux users:
If you're applying this patch on Unix/Linux, you can use the standard xdelta3
and patch commands instead of the .bat file:

1. Apply binary patches:
   cat binary_files.txt | while read f; do 
     xdelta3 -d -s "/path/to/mux/\$f" "bin/\$f.vcdiff" "/path/to/mux/\$f.new" && 
     mv "/path/to/mux/\$f.new" "/path/to/mux/\$f"; 
   done

2. Apply text patches:
   cat text_files.txt | while read f; do
     patch "/path/to/mux/\$f" < "text/\$f.patch"; 
   done

3. Copy new files and remove deleted files as needed

For any assistance, please visit: https://tinymux.org
EOF

    echo "Process complete for $dist_type distribution!"
}

# Main execution
process_distribution "src"
process_distribution "bin"

# Create installer script for xdelta3
cat > get_xdelta3.bat <<EOF
@echo off
echo Downloading xdelta3.exe for patch application...
powershell -Command "Invoke-WebRequest -Uri 'https://github.com/jmacd/xdelta/releases/download/v3.1.0/xdelta3-3.1.0-x86_64-win64.exe.zip' -OutFile 'xdelta3.zip'"
powershell -Command "Expand-Archive -Path 'xdelta3.zip' -DestinationPath '.'"
move xdelta3-3.1.0-x86_64-win64.exe xdelta3.exe
del xdelta3.zip
echo xdelta3.exe is now ready to use
EOF

# Create installer script for patch.exe
cat > get_patch.bat <<EOF
@echo off
echo Downloading patch.exe for patch application...
powershell -Command "Invoke-WebRequest -Uri 'https://downloads.sourceforge.net/project/gnuwin32/patch/2.5.9-7/patch-2.5.9-7-bin.zip' -OutFile 'patch.zip'"
powershell -Command "Expand-Archive -Path 'patch.zip' -DestinationPath 'patch_temp'"
copy patch_temp\\bin\\patch.exe .
rmdir /s /q patch_temp
del patch.zip
echo patch.exe is now ready to use
EOF

# Copy these scripts to both patch directories
cp get_xdelta3.bat patches_src/
cp get_patch.bat patches_src/
cp get_xdelta3.bat patches_bin/
cp get_patch.bat patches_bin/

echo "Windows build process completed successfully!"
echo "---------------------------------------------"
echo "Source distribution files:"
ls -la mux-$OLD_VERSION-$NEW_VERSION.win32.src.patch.*
ls -la mux-$NEW_VERSION.win32.src.*

echo "Binary distribution files:"
ls -la mux-$OLD_VERSION-$NEW_VERSION.win32.bin.patch.*
ls -la mux-$NEW_VERSION.win32.bin.*

echo "All files have been created successfully!"