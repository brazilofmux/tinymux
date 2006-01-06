#! /bin/sh
#
#	Shell script to update the build number
#
PATH=/bin:/usr/bin:/usr/ucb
#
bnum=`cat buildnum.data 2>/dev/null || echo 0`
bnum=`expr "$bnum" + 1`
echo $bnum > buildnum.data
echo $bnum
