#!/bin/sh
#
# REQUIRED: ReferenceDir must already exist. It may be created by untaring a
# previous distribution.
#
OldBuild=5
OldVersion=2.6.0.$OldBuild
NewBuild=6
NewVersion=2.6.0.$NewBuild

ChangesDir=mux
ReferenceDir=mux2.6_$OldBuild
DistroDir=mux2.6
NewDir=mux2.6_$NewBuild
patchableFiles=`cat unix/TOC.patchable`
unpatchedFiles=`cat unix/TOC.unpatched`
removeFiles=`cat unix/TOC.removed`

# Build patchfile
#
rm -rf $NewDir $DistroDir
cp -r $ReferenceDir $DistroDir
cp -r $ReferenceDir $NewDir
for i in $patchableFiles;
do
    cp $ChangesDir/$i $NewDir/$i
done
for i in $removeFiles;
do
    if [ -e $NewDir/$i ]; then
        echo Removing: $NewDir/$i
        rm $NewDir/$i
    fi
done
#diff -Naudr $DistroDir $NewDir > mux-$OldVersion-$NewVersion.unix.patch
makepatch -verbose -diff "diff -Naud" $DistroDir $NewDir > mux-$OldVersion-$NewVersion.unix.patch
if [ -e mux-$OldVersion-$NewVersion.unix.patch.gz ]; then
    rm mux-$OldVersion-$NewVersion.unix.patch.gz
fi
gzip -9 mux-$OldVersion-$NewVersion.unix.patch

# Build tarball
#
rm -rf $DistroDir
for i in $unpatchedFiles;
do
    cp $ChangesDir/$i $NewDir/$i
done
for i in $removeFiles;
do
    if [ -e $NewDir/$i ]; then
        echo Removing: $NewDir/$i
        rm $NewDir/$i
    fi
done
cp -r $NewDir $DistroDir
if [ -e mux-$NewVersion.unix.tar.gz ]; then
    rm mux-$NewVersion.unix.tar.gz
fi
tar czf mux-$NewVersion.unix.tar.gz $DistroDir
if [ -e mux-$NewVersion.unix.tar.bz2 ]; then
    rm mux-$NewVersion.unix.tar.bz2
fi
tar cjf mux-$NewVersion.unix.tar.bz2 $DistroDir
