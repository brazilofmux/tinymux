#! /bin/sh
#
#	Shell script to update the build number
#
PATH=/bin:/usr/bin:/usr/ucb
#
touch buildnum.data
bnum=`awk '{ print $1 + 1 }' < buildnum.data`
echo $bnum > buildnum.data
echo $bnum
