#!/bin/sh
#
OldBuild=42
OldVersion=2.1.14.$OldBuild
NewBuild=49
NewVersion=2.1.15.$NewBuild

ChangesDir=.
ReferenceDir=mux2.1_$OldBuild
DistroDir=mux2.1
NewDir=mux2.1_$NewBuild
patchableFiles=`cat TOC21_src.patchable`
unpatchedFiles=`cat TOC21_src.unpatched`
removeFiles=`cat TOC21_src.removed`

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

patchableFiles=`cat TOC21_bin.patchable`
unpatchedFiles=`cat TOC21_bin.unpatched`
removeFiles=`cat TOC21_bin.removed`

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
