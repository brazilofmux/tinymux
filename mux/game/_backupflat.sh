#!/bin/sh
#
PATH=/bin:/usr/bin:/usr/local/bin:.; export PATH
#
. mux.config
#
# You'll want to use gzip if you have it. If you want really good
# compression, try 'gzip --best'. If you don't have gzip, use 'compress'.
# ZIP=gzip
#
DBDATE=`date +%m%d-%H%M`
#
if [ "$1" -a -r "$1" ]; then
    echo "Using flatfile from $1, renaming to $DATA/$GAMENAME.$DBDATE"
    mv $1 $DATA/$GAMENAME.$DBDATE
elif [ -r $DATA/$NEW_DB ]; then
    $BIN/netmux -d$DATA/$GDBM_DB -i$DATA/$NEW_DB -o$DATA/$GAMENAME.$DBDATE -u
elif [ -r $DATA/$INPUT_DB ]; then
    echo "No recent checkpoint db. Using older db."
    $BIN/netmux -d$DATA/$GDBM_DB -i$DATA/$INPUT_DB -o$DATA/$GAMENAME.$DBDATE -u
elif [ -r $DATA/$SAVE_DB ]; then
    echo "No input db. Using backup db."
    $BIN/netmux -d$DATA/$GDBM_DB -i$DATA/$SAVE_DB -o$DATA/$GAMENAME.$DBDATE -u
else
    echo "No dbs. Backup attempt failed."
fi


cd $DATA

if [ -r $GAMENAME.$DBDATE ]; then
    FILES=$GAMENAME.$DBDATE
else
    echo "No flatfile found. Aborting."
    exit
fi

if [ -r comsys.db ]; then
    cp comsys.db comsys.db.$DBDATE
    FILES="$FILES comsys.db.$DBDATE"
else
    echo "Warning: no comsys.db found."
fi

if [ -r mail.db ]; then
    cp mail.db mail.db.$DBDATE
    FILES="$FILES mail.db.$DBDATE"
else
    echo "Warning: no mail.db found."
fi

# FILES=$GAMENAME.$DBDATE comsys.db.$DBDATE mail.db.$DBDATE

echo "Compressing and removing files: $FILES"

tar czf dump.$DBDATE.tgz $FILES && rm -f $FILES &
