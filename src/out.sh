#!/bin/sh
for i in $@
do
    j=`basename $i .c`.cpp
    echo Renaming file $i to $j
    mv $i $j
done
