#!/bin/sh
#
# REQUIRED: The two ReferenceDir must already exist. They may be created by
# untaring a previous distribution.
#
OldBuild=54
OldVersion=2.2.4.$OldBuild
NewBuild=56
NewVersion=2.2.5.$NewBuild

ChangesDir=mux
ReferenceDir=mux2.2_$OldBuild
DistroDir=mux2.2
NewDir=mux2.2_$NewBuild
patchableFiles=`cat win32/TOC.src.patchable`
unpatchedFiles=`cat win32/TOC.src.unpatched`
removeFiles=`cat win32/TOC.src.removed`

# Build source patchfile
#
rm -rf $NewDir\_src $DistroDir
cp -r $ReferenceDir\_src $DistroDir
cp -r $ReferenceDir\_src $NewDir\_src
for i in $patchableFiles;
do
    cp $ChangesDir/$i $NewDir\_src/$i
done
for i in $removeFiles;
do
    if [ -e $NewDir\_src/$i ]; then
        echo Removing: $NewDir\_src/$i
        rm $NewDir\_src/$i
    fi
done
chmod -R u+rw $DistroDir $NewDir\_src

/cygdrive/c/binpatch/genpatch $DistroDir $NewDir\_src -d mux-$OldVersion-$NewVersion.win32.src.utp

# Build source tarballs
#
rm -rf $DistroDir
for i in $unpatchedFiles;
do
    cp $ChangesDir/$i $NewDir\_src/$i
done
for i in $removeFiles;
do
    if [ -e $NewDir\_src/$i ]; then
        echo Removing: $NewDir\_src/$i
        rm $NewDir\_src/$i
    fi
done
cp -r $NewDir\_src $DistroDir
chmod -R u+rw $DistroDir $NewDir\_src
if [ -e mux-$NewVersion.win32.src.tar.gz ]; then
    rm mux-$NewVersion.win32.src.tar.gz
fi
tar czf mux-$NewVersion.win32.src.tar.gz $DistroDir
if [ -e mux-$NewVersion.win32.src.j ]; then
    rm mux-$NewVersion.win32.src.j
fi
jar32 a -m4 -r mux-$NewVersion.win32.src.j $DistroDir\\
if [ -e mux-$NewVersion.win32.src.zip ]; then
    rm mux-$NewVersion.win32.src.zip
fi
pkzip -add -recurse -path -maximum mux-$NewVersion.win32.src.zip $DistroDir\\\*.\*

patchableFiles=`cat win32/TOC.bin.patchable`
unpatchedFiles=`cat win32/TOC.bin.unpatched`
removeFiles=`cat win32/TOC.bin.removed`

# Build binary patchfile
#
rm -rf $NewDir\_bin $DistroDir
cp -r $ReferenceDir\_bin $DistroDir
cp -r $ReferenceDir\_bin $NewDir\_bin
for i in $patchableFiles;
do
    cp $ChangesDir/$i $NewDir\_bin/$i
done
for i in $removeFiles;
do
    if [ -e $NewDir\_bin/$i ]; then
        echo Removing: $NewDir\_bin/$i
        rm $NewDir\_bin/$i
    fi
done
chmod -R u+rw $DistroDir $NewDir\_bin

/cygdrive/c/binpatch/genpatch $DistroDir $NewDir\_bin -d mux-$OldVersion-$NewVersion.win32.bin.utp

# Build binary tarballs
#
rm -rf $DistroDir
for i in $unpatchedFiles;
do
    cp $ChangesDir/$i $NewDir\_bin/$i
done
for i in $removeFiles;
do
    if [ -e $NewDir\_bin/$i ]; then
        echo Removing: $NewDir\_bin/$i
        rm $NewDir\_bin/$i
    fi
done
cp -r $NewDir\_bin $DistroDir
chmod -R u+rw $DistroDir $NewDir\_bin
if [ -e mux-$NewVersion.win32.bin.tar.gz ]; then
    rm mux-$NewVersion.win32.bin.tar.gz
fi
tar czf mux-$NewVersion.win32.bin.tar.gz $DistroDir
if [ -e mux-$NewVersion.win32.bin.j ]; then
    rm mux-$NewVersion.win32.bin.j
fi
jar32 a -m4 -r mux-$NewVersion.win32.bin.j $DistroDir\\
if [ -e mux-$NewVersion.win32.bin.zip ]; then
    rm mux-$NewVersion.win32.bin.zip
fi
pkzip -add -recurse -path -maximum mux-$NewVersion.win32.bin.zip $DistroDir\\\*.\*
