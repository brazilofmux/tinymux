#!/bin/sh
#############################################################################
# Version 2.1.3 TinyMUSH 3.x to MUX 2.0 conversion script
# Ashen-Shugar (c) 2000, all rights reserved.
# Tested with TM 3.1 b2 and earlier, TM 3.0 p4 and earlier,  and 
# MUX 2.0.17 and below.
# Should work on later versions as long as flatfile formats do not change
# Change log:
#      07/08/02 - Enhanced TM 3.1 autorecognition support
#      05/28/02 - Add TM 3.1 conversion support
#      10/29/01 - Fix to verify input file
#      10/25/01 - Converted awk script to C code for speed
#      10/16/01 - fixed a bug in the new do_detail script
#      10/15/01 - modified to list all attribs and objs of 'missing' features
#      10/15/01 - updated the mux function/command set
#
# Unsupported and known issues:
#     - Some of the TinyMUSH 3.1 feature sets/functions not recognized.
#
if [ "$1" = "" -o "$2" = "" -o ! -f "$1" ]
then
   if [ ! -f "$1" ]
   then
      echo "Input file not found."
   else
      echo "No valid output file specified."
   fi
   echo "Syntax: $0 <infile> <outfile>"
   exit 1
fi
VLD=`grep -c "END OF DUMP" $1`
HDR=`grep -c "^+T" $1`
if [ $VLD -eq 0 -o $HDR -eq 0 ]
then
   echo "Invalid TinyMUSH 3.0/3.1 source flatfile '$1'"
   exit 1
fi
echo "What version of TinyMUSH 3 is this."
echo "0) TinyMUSH 3.0"
echo "1) TinyMUSH 3.1"
CHKVAL=0
while [ $CHKVAL -eq 0 ]
do
   echo "Please Enter Choice> "|tr -d '\012'
   read ANS
   if [ "$ANS" != "0" -a "$ANS" != "1" ]
   then
      echo "Please enter a valid number (0 or 1)"
   else
      CHKVAL=1
   fi
done
echo "Do you wish to convert TM3's %x ansi sequences"
echo "to MUX's %c ansi sequences? (Y/N) :"|tr -d '\012'
read ANS
if [ "$ANS" = "y" -o "$ANS" = "Y" ]
then
   CVTANSI=1
   echo "Ansi sequences will be converted."
else
   CVTANSI=0
   echo "Ansi sequences will _NOT_ be converted."
fi
if [ ! -f "muxcleaner" -o ! -f "30tomux" -o ! -f "31tomux" ]
then
   if [ -f "$3" ]
   then
      echo "Using user-defined C compiler '$3'"
      CC=$3
   else
      echo "Detecting C compiler..."|tr -d '\012'
      CCT=`gcc -v 2>&1|grep -c ersion`
      if [ $CCT -gt 0 ]
      then
         echo "GNU-C found (gcc)"
         CC=gcc
      else
         CCT=`cc -v 2>&1|grep -c ersion`
         if [ $CCT -gt 0 ]
         then
            echo "ANSI C found (cc)"
            CC=cc
         else
            echo "No C compiler detected.  Please pass C compiler as 3rd argument (full path)"
            echo "Example: $0 $1 $2 /usr/local/bin/gcc"
            exit 1
         fi
      fi
   fi
   echo "Compiling converters..."|tr -d '\012'
   $CC muxcleaner.c -o muxcleaner 2>/dev/null
   if [ $? -gt 0 ]
   then
      echo "Error compiling muxcleaner.c"
      exit 1
   fi
   echo "muxcleaner..."|tr -d '\012'
   $CC 30tomux.c -o 30tomux 2>/dev/null
   if [ $? -gt 0 ]
   then
      echo "Error compiling 30tomux.c"
      exit 1
   fi
   $CC 31tomux.c -o 31tomux 2>/dev/null
   if [ $? -gt 0 ]
   then
      echo "Error compiling 31tomux.c"
      exit 1
   fi
   echo "do_detail..."|tr -d '\012'
   $CC do_detail.c -o do_detail 2>/dev/null
   echo "do_detail... Compile completed."
fi
echo "Converting TinyMUSH 3.${CHKVAL} to TinyMUX 2.0"
echo "1. Cleaning up flatfile..."|tr -d '\012'
./muxcleaner $1 $1.clean 2>/dev/null
if [ $? -gt 0 ]
then
   echo "Error parsing TinyMUSH 3.${CHKVAL} flatfile."
   exit 1
fi
echo "completed."
echo "2. Converting to MUX 2.0..."|tr -d '\012'
./3${CHKVAL}tomux < $1.clean > $2 2>./tm3tomux_error.log
if [ $? -gt 0 ]
then
    echo "Error converting TinyMUSH 3.${CHKVAL} flatfile."
    exit 1
fi
if [ $CVTANSI -eq 1 ]
then
   sed "s/%x/%c/g" $2 > $2.sed 2>/dev/null
   if [ $? -gt 0 ]
   then
      echo "%x to %c conversion failed.  Continuing with conversion..."|tr -d '\012'
   else
      mv -f $2.sed $2 2>/dev/null
      echo "ansi substituted..."|tr -d '\012'
   fi
fi
echo "completed."
echo "3. Cleaning up temporary files..."|tr -d '\012'
rm -rf $1.clean 2>/dev/null
echo "completed."
echo "4. Checking for TM 3.${CHKVAL} functions that need @functions for MUX 2.0"
GRPVAL=0;export GRPVAL
NOTBOOL=`grep -ic "notbool(" $2 2>/dev/null`
if [ $NOTBOOL -gt 0 ]
then
   echo "   -- notbool ($NOTBOOL found): @admin function_alias to not()"
   GRPVAL=`grep -in "notbool(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
ASIND=`grep -ic "asind(" $2 2>/dev/null`
if [ $ASIND -gt 0 ]
then
   echo "   -- asind ($ASIND found): @function to [asin(%0,d)]"
   GRPVAL=`grep -in "asind(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
ATAND=`grep -ic "atand(" $2 2>/dev/null`
if [ $ATAND -gt 0 ]
then
   echo "   -- atand ($ATAND found): @function to [atan(%0,d)]"
   GRPVAL=`grep -in "atand(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
TAND=`grep -ic "tand(" $2 2>/dev/null`
if [ $TAND -gt 0 ]
then
   echo "   -- tand ($TAND found): @function to [tan(%0,d)]"
   GRPVAL=`grep -in "tand(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
COSD=`grep -ic "cosd(" $2 2>/dev/null`
if [ $COSD -gt 0 ]
then
   echo "   -- cosd ($COSD found): @function to [cos(%0,d)]"
   GRPVAL=`grep -in "cosd(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
ACOSD=`grep -ic "acosd(" $2 2>/dev/null`
if [ $ACOSD -gt 0 ]
then
   echo "   -- acosd ($ACOSD found): @function to [acos(%0,d)]"
   GRPVAL=`grep -in "acosd(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
DELIMIT=`grep -ic "delimit(" $2 2>/dev/null`
if [ $DELIMIT -gt 0 ]
then
   echo "   -- delimit ($DELIMIT found): @function to [edit(get(%0),%2,%1)]"
   GRPVAL=`grep -in "delimit(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
WHENTRUE=`grep -ic "whentrue(" $2 2>/dev/null`
if [ $WHENTRUE -gt 0 ]
then
   echo "   -- whentrue ($WHENTRUE found): @function/preserve to [setq(0,1)][iter(0 #-1 1 2,ifelse(and(r(0),t(##)),##,setq(0,0)))]"
   GRPVAL=`grep -in "whentrue(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
ISTRUE=`grep -ic "istrue(" $2 2>/dev/null`
if [ $ISTRUE -gt 0 ]
then
   echo "   -- istrue ($ISTRUE found): @function to [edit(trim(iter(%0,ifelse(neq(s(%1),0),[ifelse(%2,%2,%b)]##,),[ifelse(%2,%2,%b)],@@),,[ifelse(%2,%2,%b),[ifelse(%2,%2,%b)],[ifelse(%3,%3,%b)])]"
   GRPVAL=`grep -in "istrue(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
WHENFALSE=`grep -ic "whenfalse(" $2 2>/dev/null`
if [ $WHENFALSE -gt 0 ]
then
   echo "   -- whenfalse ($WHENFALSE found): @function/preserve to [setq(0,1)][iter(0 #-1 1 2,ifelse(not(and(r(0),t(##))),##,setq(0,0)))]"
   GRPVAL=`grep -in "whenfalse(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
ISFALSE=`grep -ic "isfalse(" $2 2>/dev/null`
if [ $ISFALSE -gt 0 ]
then
   echo "   -- isfalse ($ISFALSE found): @function to [edit(trim(iter(%0,ifelse(eq(s(%1),0),[ifelse(%2,%2,%b)]##,),[ifelse(%2,%2,%b)],@@),,[ifelse(%2,%2,%b),[ifelse(%2,%2,%b)],[ifelse(%3,%3,%b)])]"
   GRPVAL=`grep -in "isfalse(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
LUNION=`grep -ic "lunion(" $2 2>/dev/null`
if [ $LUNION -gt 0 ]
then
   echo "   -- lunion ($LUNION found): @function to [setunion(%0,%1,,%2,%3)]"
   GRPVAL=`grep -in "lunion(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
LINTER=`grep -ic "linter(" $2 2>/dev/null`
if [ $LINTER -gt 0 ]
then
   echo "   -- linter ($LINTER found): @function to [setinter(%0,%1,,%2,%3)]"
   GRPVAL=`grep -in "linter(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
LDIFF=`grep -ic "ldiff(" $2 2>/dev/null`
if [ $LDIFF -gt 0 ]
then
   echo "   -- ldiff ($LDIFF found): @function to [setdiff(%0,%1,,%2,%3)]"
   GRPVAL=`grep -in "ldiff(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
NATTR=`grep -ic "nattr(" $2 2>/dev/null`
if [ $NATTR -gt 0 ]
then
   echo "   -- nattr ($NATTR found): @admin function_alias to attrcnt"
   echo "      Note:  In newest MUX versions, this is automatically aliased."
   GRPVAL=`grep -in "nattr(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
NESCAPE=`grep -ic "nescape(" $2 2>/dev/null`
if [ $NESCAPE -gt 0 ]
then
   echo "   -- nescape ($NESCAPE found): @function to [escape(lit(%0))]"
   GRPVAL=`grep -in "nescape(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
NONZERO=`grep -ic "nonzero(" $2 2>/dev/null`
if [ $NONZERO -gt 0 ]
then
   echo "   -- nonzero ($NONZERO found): @function to [ifelse(gt(%0,0),%1,%2)]"
   GRPVAL=`grep -in "nonzero(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
TOSS=`grep -ic "toss(" $2 2>/dev/null`
if [ $TOSS -gt 0 ]
then
   echo "   -- toss ($TOSS found): @admin function_alias to pop()"
   GRPVAL=`grep -in "toss(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
STREQ=`grep -ic "streq(" $2 2>/dev/null`
if [ $STREQ -gt 0 ]
then
   echo "   -- streq ($STREQ found): @admin function_alias to strmatch()"
   GRPVAL=`grep -in "streq(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
NCOMP=`grep -ic "ncomp(" $2 2>/dev/null`
if [ $NCOMP -gt 0 ]
then
   echo "   -- ncomp ($NCOMP found): @admin function_alias to comp()"
   GRPVAL=`grep -in "ncomp(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
LOOP=`grep -ic "loop(" $2 2>/dev/null`
if [ $LOOP -gt 0 ]
then
   echo "   -- loop ($LOOP found): @admin function_alias to list()"
   GRPVAL=`grep -in "loop(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
FILTERBOOL=`grep -ic "filterbool(" $2 2>/dev/null`
if [ $FILTERBOOL -gt 0 ]
then
   echo "   -- filterbool ($FILTERBOOL found): @admin function_alias to filter()"
   GRPVAL=`grep -in "filterbool(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
RESTARTTIME=`grep -ic "restarttime(" $2 2>/dev/null`
if [ $RESTARTTIME -gt 0 ]
then
   echo "   -- restarttime ($RESTARTTIME found): @admin function_alias to starttime()"
   GRPVAL=`grep -in "restarttime(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
LRAND=`grep -ic "lrand(" $2 2>/dev/null`
if [ $LRAND -gt 0 ]
then
   echo "   -- lrand ($LRAND found): @function to below:"
   echo "      [edit(iter(lnum(%2),add(%0,rand(add(sub(%1,%0),1)))),%b,ifelse(strlen(%3),%3,%b))]"
   echo "      Newest versions of MUX 2.1+ has this.  Verify before adding."
   GRPVAL=`grep -in "lrand(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
ORBOOL1=`grep -ic "\[orbool(" $2 2>/dev/null`
ORBOOL2=`grep -ic "^orbool(" $2 2>/dev/null`
ORBOOL3=`grep -ic ",orbool(" $2 2>/dev/null`
ORBOOL4=`grep -ic "(orbool(" $2 2>/dev/null`
ORBOOL5=`grep -ic " orbool(" $2 2>/dev/null`
ORBOOL=`expr $ORBOOL1 + $ORBOOL1 + $ORBOOL3 + $ORBOOL4 + $ORBOOL5`
if [ $ORBOOL -gt 0 ]
then
   echo "   -- orbool ($ORBOOL found): @admin function_alias to or()"
#  echo "   -- orbool ($ORBOOL found): @function to below:"
#  echo "      [s(\[or\([edit(%0,ifelse(%1,%1,%b),\,)]\)\])]"
   GRPVAL=`grep -in "\[orbool(" $2|cut -f1 -d ":"`
   GRPVAL="$GRPVAL `grep -in '^orbool(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ',orbool(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in '(orbool(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ' orbool(' $2|cut -f1 -d ':'`"
   LASTQ=0
   for q in $GRPVAL
   do
      if [ "$LASTQ" = "$Q" ]
      then
         continue
      fi
      LASTQ=$q
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
POPN=`grep -ic "popn(" $2 2>/dev/null`
if [ $POPN -gt 0 ]
then
   echo "   -- popn ($POPN found): @function to below:"
   echo "      [edit(iter(lnum(max(0,%1),max(0,%2)),pop(%0,##)),%b,ifelse(words(%3),%3,%b))]"
   GRPVAL=`grep -in "popn(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
XORBOOL=`grep -ic "xorbool(" $2 2>/dev/null`
if [ $XORBOOL -gt 0 ]
then
#  echo "   -- xorbool ($XORBOOL found): @function to below:"
#  echo "      [s(\[xor\([edit(%0,ifelse(%1,%1,%b),\,)]\)\])]"
   echo "   -- xorbool ($XORBOOL found): @admin function_alias to xor()"
   GRPVAL=`grep -in "xorbool(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
LORBOOL=`grep -ic "lorbool(" $2 2>/dev/null`
if [ $LORBOOL -gt 0 ]
then
   echo "   -- lorbool ($LORBOOL found): @function to below:"
   echo "      [s(\[or\([edit(%0,ifelse(%1,%1,%b),\,)]\)\])]"
   GRPVAL=`grep -in "lorbool(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
LOR=`grep -ic "lor(" $2 2>/dev/null`
if [ $LOR -gt 0 ]
then
   echo "   -- lor ($LOR found): @function to below:"
   echo "      [s(\[or\([edit(%0,ifelse(%1,%1,%b),\,)]\)\])]"
   GRPVAL=`grep -in "lor(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
LMIN=`grep -ic "lmin(" $2 2>/dev/null`
if [ $LMIN -gt 0 ]
then
   echo "   -- lmin ($LMIN found): @function to below:"
   echo "      [s(\[min\([edit(%0,ifelse(%1,%1,%b),\,)]\)\])]"
   GRPVAL=`grep -in "lmin(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
LMAX=`grep -ic "lmax(" $2 2>/dev/null`
if [ $LMAX -gt 0 ]
then
   echo "   -- lmax ($LMAX found): @function to below:"
   echo "      [s(\[max\([edit(%0,ifelse(%1,%1,%b),\,)]\)\])]"
   GRPVAL=`grep -in "lmax(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
LADD=`grep -ic "ladd(" $2 2>/dev/null`
if [ $LADD -gt 0 ]
then
   echo "   -- ladd ($LADD found): @function to below:"
   echo "      [s(\[add\([edit(%0,ifelse(%1,%1,%b),\,)]\)\])]"
   echo "      Newest versions of MUX 2.1+ has this.  Verify before adding."
   GRPVAL=`grep -in "ladd(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
LAND=`grep -ic "land(" $2 2>/dev/null`
if [ $LAND -gt 0 ]
then
   echo "   -- land ($LAND found): @function to below:"
   echo "      [s(\[and\([edit(%0,ifelse(%1,%1,%b),\,)]\)\])]"
   GRPVAL=`grep -in "land(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
X1=`grep -ic "\[andbool(" $2 2>/dev/null`
X2=`grep -ic "^andbool(" $2 2>/dev/null`
X3=`grep -ic ",andbool(" $2 2>/dev/null`
X4=`grep -ic "(andbool(" $2 2>/dev/null`
X5=`grep -ic " andbool(" $2 2>/dev/null`
XF=`expr $X1 + $X2 + $X3 + $X4 + $X5`
if [ $XF -gt 0 ]
then
   echo "   -- andbool ($XF found):  @admin function_alias to and()"
   GRPVAL=`grep -in "\[andbool(" $2|cut -f1 -d ":"`
   GRPVAL="$GRPVAL `grep -in '^andbool(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ',andbool(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in '(andbool(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ' andbool(' $2|cut -f1 -d ':'`"
   LASTQ=0
   for q in $GRPVAL
   do
      if [ "$LASTQ" = "$q" ]
      then
         continue
      fi
      LASTq=$q
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
LANDBOOL=`grep -ic "landbool(" $2 2>/dev/null`
if [ $LANDBOOL -gt 0 ]
then
   echo "   -- landbool ($LANDBOOL found): @function to below:"
   echo "      [s(\[and\([edit(%0,ifelse(%1,%1,%b),\,)]\)\])]"
   GRPVAL=`grep -in "landbool(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
SWAP=`grep -ic "swap(" $2 2>/dev/null`
if [ $SWAP -gt 0 ]
then
   echo "   -- swap ($SWAP found): @function/preserve to below:"
   echo "      [setq(0,[pop(%0)] [pop(%0)])][push(%0,first(r(0)))][push(%0,rest(r(0)))]"
   GRPVAL=`grep -in "swap(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
RIGHT=`grep -ic "right(" $2 2>/dev/null`
if [ $RIGHT -gt 0 ]
then
   echo "   -- right ($RIGHT found): @function to below:"
   echo "      [reverse(mid(reverse(%0),%1,7996))]"
   GRPVAL=`grep -in "right(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
LEFT=`grep -ic "left(" $2 2>/dev/null`
if [ $LEFT -gt 0 ]
then
   echo "   -- left ($LEFT found): @function to below:"
   echo "      [delete(%0,%1,7996)]"
   GRPVAL=`grep -in "left(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
echo "5. Checking for TM 3.${CHKVAL} functions that need to be recoded to work."
FND=0
WHILE=`grep -ic "while(" $2 2>/dev/null`
if [ $WHILE -gt 0 ]
then
   echo "   -- while ($WHILE found)"
   GRPVAL=`grep -in "while(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
UNTIL=`grep -ic "until(" $2 2>/dev/null`
if [ $UNTIL -gt 0 ]
then
   echo "   -- until ($UNTIL found)"
   GRPVAL=`grep -in "until(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
UNLOAD=`grep -ic "unload(" $2 2>/dev/null`
if [ $UNLOAD -gt 0 ]
then
   echo "   -- unload ($UNLOAD found)"
   GRPVAL=`grep -in "unload(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
FORCE=`grep -ic "force(" $2 2>/dev/null`
if [ $FORCE -gt 0 ]
then
   echo "   -- force ($FORCE found)"
   GRPVAL=`grep -in "force(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
TRIGGER=`grep -ic "trigger(" $2 2>/dev/null`
if [ $TRIGGER -gt 0 ]
then
   echo "   -- trigger ($TRIGGER found)"
   GRPVAL=`grep -in "trigger(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
SWITCHALL=`grep -ic "switchall(" $2 2>/dev/null`
if [ $SWITCHALL -gt 0 ]
then
   echo "   -- switchall ($SWITCHALL found)"
   GRPVAL=`grep -in "switchall(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
X1=`grep -ic "\[structure(" $2 2>/dev/null`
X2=`grep -ic "^structure(" $2 2>/dev/null`
X3=`grep -ic ",structure(" $2 2>/dev/null`
X4=`grep -ic "(structure(" $2 2>/dev/null`
X5=`grep -ic " structure(" $2 2>/dev/null`
XF=`expr $X1 + $X2 + $X3 + $X4 + $X5`
if [ $XF -gt 0 ]
then
   echo "   -- structure ($XF found)"
   GRPVAL=`grep -in "\[structure(" $2|cut -f1 -d ":"`
   GRPVAL="$GRPVAL `grep -in '^structure(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ',structure(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in '(structure(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ' structure(' $2|cut -f1 -d ':'`"
   LASTQ=0
   for q in $GRPVAL
   do
      if [ "$LASTQ" = "$q" ]
      then
         continue
      fi
      LASTq=$q
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
UNSTRUCTURE=`grep -ic "unstructure(" $2 2>/dev/null`
if [ $UNSTRUCTURE -gt 0 ]
then
   echo "   -- unstructure ($UNSTRUCTURE found)"
   GRPVAL=`grep -in "unstructure(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
STORE=`grep -ic "store(" $2 2>/dev/null`
if [ $STORE -gt 0 ]
then
   echo "   -- store ($STORE found)"
   GRPVAL=`grep -in "store(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
STEP=`grep -ic "step(" $2 2>/dev/null`
if [ $STEP -gt 0 ]
then
   echo "   -- step ($STEP found)"
   GRPVAL=`grep -in "step(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
X1=`grep -ic "\[z(" $2 2>/dev/null`
X2=`grep -ic "^z(" $2 2>/dev/null`
X3=`grep -ic ",z(" $2 2>/dev/null`
X4=`grep -ic "(z(" $2 2>/dev/null`
X5=`grep -ic " z(" $2 2>/dev/null`
XF=`expr $X1 + $X2 + $X3 + $X4 + $X5`
if [ $XF -gt 0 ]
then
   echo "   -- z ($XF found)"
   GRPVAL=`grep -in "\[z(" $2|cut -f1 -d ":"`
   GRPVAL="$GRPVAL `grep -in '^z(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ',z(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in '(z(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ' z(' $2|cut -f1 -d ':'`"
   LASTQ=0
   for q in $GRPVAL
   do
      if [ "$LASTQ" = "$q" ]
      then
         continue
      fi
      LASTq=$q
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
X1=`grep -ic "\[x(" $2 2>/dev/null`
X2=`grep -ic "^x(" $2 2>/dev/null`
X3=`grep -ic ",x(" $2 2>/dev/null`
X4=`grep -ic "(x(" $2 2>/dev/null`
X5=`grep -ic " x(" $2 2>/dev/null`
XF=`expr $X1 + $X2 + $X3 + $X4 + $X5`
if [ $XF -gt 0 ]
then
   echo "   -- x ($XF found)"
   GRPVAL=`grep -in "\[x(" $2|cut -f1 -d ":"`
   GRPVAL="$GRPVAL `grep -in '^x(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ',x(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in '(x(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ' x(' $2|cut -f1 -d ':'`"
   LASTQ=0
   for q in $GRPVAL
   do
      if [ "$LASTQ" = "$q" ]
      then
         continue
      fi
      LASTq=$q
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
T1=`grep -ic "\[t(" $2 2>/dev/null`
T2=`grep -ic "^t(" $2 2>/dev/null`
T3=`grep -ic ",t(" $2 2>/dev/null`
T4=`grep -ic "(t(" $2 2>/dev/null`
T5=`grep -ic " t(" $2 2>/dev/null`
TF=`expr $T1 + $T2 + $T3 + $T4 + $T5`
if [ $TF -gt 0 ]
then
   echo "   -- t ($TF found)"
   FND=1
   echo "      Newest versions of MUX 2.1+ has this.  Verify before adding."
   GRPVAL=`grep -in "\[t(" $2|cut -f1 -d ":"`
   GRPVAL="$GRPVAL `grep -in '^t(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ',t(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in '(t(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ' t(' $2|cut -f1 -d ':'`"
   LASTQ=0
   for q in $GRPVAL
   do
      if [ "$LASTQ" = "$q" ]
      then
         continue
      fi
      LASTq=$q
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi
SETX=`grep -ic "setx(" $2 2>/dev/null`
if [ $SETX -gt 0 ]
then
   echo "   -- setx ($SETX found)"
   GRPVAL=`grep -in "setx(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
SQL=`grep -ic "sql(" $2 2>/dev/null`
if [ $SQL -gt 0 ]
then
   echo "   -- sql ($SQL found)"
   GRPVAL=`grep -in "sql(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
SPEAK=`grep -ic "speak(" $2 2>/dev/null`
if [ $SPEAK -gt 0 ]
then
   echo "   -- speak ($SPEAK found)"
   GRPVAL=`grep -in "speak(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
SEES=`grep -ic "sees(" $2 2>/dev/null`
if [ $SEES -gt 0 ]
then
   echo "   -- sees ($SEES found)"
   GRPVAL=`grep -in "sees(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
LSTRUCTURES=`grep -ic "lstructures(" $2 2>/dev/null`
if [ $LSTRUCTURES -gt 0 ]
then
   echo "   -- lstructures ($LSTRUCTURES found)"
   GRPVAL=`grep -in "lstructures(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
LVARS=`grep -ic "lvars(" $2 2>/dev/null`
if [ $LVARS -gt 0 ]
then
   echo "   -- lvars ($LVARS found)"
   GRPVAL=`grep -in "lvars(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
LOCALIZE=`grep -ic "localize(" $2 2>/dev/null`
if [ $LOCALIZE -gt 0 ]
then
   echo "   -- localize ($LOCALIZE found)"
   GRPVAL=`grep -in "localize(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
LOAD=`grep -ic "load(" $2 2>/dev/null`
if [ $LOAD -gt 0 ]
then
   echo "   -- load ($LOAD found)"
   GRPVAL=`grep -in "load(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
LINSTANCES=`grep -ic "linstances(" $2 2>/dev/null`
if [ $LINSTANCES -gt 0 ]
then
   echo "   -- linstances ($LINSTANCES found)"
   GRPVAL=`grep -in "linstances(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
LASTCREATE=`grep -ic "lastcreate(" $2 2>/dev/null`
if [ $LASTCREATE -gt 0 ]
then
   echo "   -- lastcreate ($LASTCREATE found)"
   GRPVAL=`grep -in "lastcreate(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
LASTMOD=`grep -ic "lastmod(" $2 2>/dev/null`
if [ $LASTMOD -gt 0 ]
then
   echo "   -- lastmod ($LASTMOD found)"
   GRPVAL=`grep -in "lastmod(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
LASTACCESS=`grep -ic "lastaccess(" $2 2>/dev/null`
if [ $LASTACCESS -gt 0 ]
then
   echo "   -- lastaccess ($LASTACCESS found)"
   GRPVAL=`grep -in "lastaccess(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
LEDIT=`grep -ic "ledit(" $2 2>/dev/null`
if [ $LEDIT -gt 0 ]
then
   echo "   -- ledit ($LEDIT found)"
   GRPVAL=`grep -in "ledit(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
DESTRUCT=`grep -ic "destruct(" $2 2>/dev/null`
if [ $DESTRUCT -gt 0 ]
then
   echo "   -- destruct ($DESTRUCT found)"
   GRPVAL=`grep -in "destruct(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
HASMODULE=`grep -ic "hasmodule(" $2 2>/dev/null`
if [ $HASMODULE -gt 0 ]
then
   echo "   -- hasmodule ($HASMODULE found)"
   GRPVAL=`grep -in "hasmodule(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
ENTRANCES=`grep -ic "entrances(" $2 2>/dev/null`
if [ $ENTRANCES -gt 0 ]
then
   echo "   -- entrances ($ENTRANCES found)"
   GRPVAL=`grep -in "entrances(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
DUP=`grep -ic "dup(" $2 2>/dev/null`
if [ $DUP -gt 0 ]
then
   echo "   -- dup ($DUP found)"
   GRPVAL=`grep -in "dup(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CONSTRUCT=`grep -ic "construct(" $2 2>/dev/null`
if [ $CONSTRUCT -gt 0 ]
then
   echo "   -- construct ($CONSTRUCT found)"
   GRPVAL=`grep -in "construct(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CONFIG=`grep -ic "config(" $2 2>/dev/null`
if [ $CONFIG -gt 0 ]
then
   echo "   -- config ($CONFIG found)"
   GRPVAL=`grep -in "config(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
COMMAND=`grep -ic "command(" $2 2>/dev/null`
if [ $COMMAND -gt 0 ]
then
   echo "   -- command ($COMMAND found)"
   GRPVAL=`grep -in "command(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CLEARVARS=`grep -ic "clearvars(" $2 2>/dev/null`
if [ $CLEARVARS -gt 0 ]
then
   echo "   -- clearvars ($CLEARVARS found)"
   GRPVAL=`grep -in "clearvars(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CHOMP=`grep -ic "chomp(" $2 2>/dev/null`
if [ $CHOMP -gt 0 ]
then
   echo "   -- chomp ($CHOMP found)"
   GRPVAL=`grep -in "chomp(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CORBOOL=`grep -ic "corbool(" $2 2>/dev/null`
if [ $CORBOOL -gt 0 ]
then
   echo "   -- corbool ($CORBOOL found)"
   GRPVAL=`grep -in "corbool(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
COR=`grep -ic "cor(" $2 2>/dev/null`
if [ $COR -gt 0 ]
then
   echo "   -- cor ($COR found)"
   GRPVAL=`grep -in "cor(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
CAND=`grep -ic "cand(" $2 2>/dev/null`
if [ $CAND -gt 0 ]
then
   echo "   -- cand ($CAND found)"
   GRPVAL=`grep -in "cand(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
CANDBOOL=`grep -ic "candbool(" $2 2>/dev/null`
if [ $CANDBOOL -gt 0 ]
then
   echo "   -- candbool ($CANDBOOL found)"
   GRPVAL=`grep -in "candbool(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
fi   
X1=`grep -ic "\[border(" $2 2>/dev/null`
X2=`grep -ic "^border(" $2 2>/dev/null`
X3=`grep -ic ",border(" $2 2>/dev/null`
X4=`grep -ic "(border(" $2 2>/dev/null`
X5=`grep -ic " border(" $2 2>/dev/null`
XF=`expr $X1 + $X2 + $X3 + $X4 + $X5`
if [ $XF -gt 0 ]
then
   echo "   -- border ($XF found)"
   GRPVAL=`grep -in "\[border(" $2|cut -f1 -d ":"`
   GRPVAL="$GRPVAL `grep -in '^border(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ',border(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in '(border(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ' border(' $2|cut -f1 -d ':'`"
   LASTQ=0
   for q in $GRPVAL
   do
      if [ "$LASTQ" = "$q" ]
      then
         continue
      fi
      LASTq=$q
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
RBORDER=`grep -ic "rborder(" $2 2>/dev/null`
if [ $RBORDER -gt 0 ]
then
   echo "   -- rborder ($RBORDER found)"
   GRPVAL=`grep -in "rborder(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi   
CBORDER=`grep -ic "cborder(" $2 2>/dev/null`
if [ $CBORDER -gt 0 ]
then
   echo "   -- cborder ($CBORDER found)"
   GRPVAL=`grep -in "cborder(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi   
X1=`grep -ic "\[tables(" $2 2>/dev/null`
X2=`grep -ic "^tables(" $2 2>/dev/null`
X3=`grep -ic ",tables(" $2 2>/dev/null`
X4=`grep -ic "(tables(" $2 2>/dev/null`
X5=`grep -ic " tables(" $2 2>/dev/null`
XF=`expr $X1 + $X2 + $X3 + $X4 + $X5`
if [ $XF -gt 0 ]
then
   echo "   -- tables ($XF found)"
   GRPVAL=`grep -in "\[tables(" $2|cut -f1 -d ":"`
   GRPVAL="$GRPVAL `grep -in '^tables(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ',tables(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in '(tables(' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in ' tables(' $2|cut -f1 -d ':'`"
   LASTQ=0
   for q in $GRPVAL
   do
      if [ "$LASTQ" = "$q" ]
      then
         continue
      fi
      LASTq=$q
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CTABLES=`grep -ic "ctables(" $2 2>/dev/null`
if [ $CTABLES -gt 0 ]
then
   echo "   -- ctables ($CTABLES found)"
   GRPVAL=`grep -in "ctables(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi   
RTABLES=`grep -ic "rtables(" $2 2>/dev/null`
if [ $RTABLES -gt 0 ]
then
   echo "   -- rtables ($RTABLES found)"
   GRPVAL=`grep -in "rtables(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi   
SESSION=`grep -ic "session(" $2 2>/dev/null`
if [ $SESSION -gt 0 ]
then
   echo "   -- session ($SESSION found)"
   GRPVAL=`grep -in "session(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi   
XVARS=`grep -ic "xvars(" $2 2>/dev/null`
if [ $XVARS -gt 0 ]
then
   echo "   -- xvars ($XVARS found)"
   GRPVAL=`grep -in "xvars(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
LASTCREATE=`grep -ic "lastcreate(" $2 2>/dev/null`
if [ $LASTCREATE -gt 0 ]
then
   echo "   -- lastcreate ($LASTCREATE found)"
   GRPVAL=`grep -in "lastcreate(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
MODIFY=`grep -ic "modify(" $2 2>/dev/null`
if [ $MODIFY -gt 0 ]
then
   echo "   -- modify ($MODIFY found)"
   GRPVAL=`grep -in "modify(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
RESTARTS=`grep -ic "restarts(" $2 2>/dev/null`
if [ $RESTARTS -gt 0 ]
then
   echo "   -- restarts ($RESTARTS found)"
   GRPVAL=`grep -in "restarts(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REMIT=`grep -ic "remit(" $2 2>/dev/null`
if [ $REMIT -gt 0 ]
then
   echo "   -- remit ($REMIT found)"
   echo "      Newest versions of MUX 2.1+ has this.  Verify before adding."
   GRPVAL=`grep -in "remit(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REGPARSE=`grep -ic "regparse(" $2 2>/dev/null`
if [ $REGPARSE -gt 0 ]
then
   echo "   -- regparse ($REGPARSE found)"
   GRPVAL=`grep -in "regparse(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
PROGRAMMER=`grep -ic "programmer(" $2 2>/dev/null`
if [ $PROGRAMMER -gt 0 ]
then
   echo "   -- programmer ($PROGRAMMER found)"
   GRPVAL=`grep -in "programmer(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
WILDPARSE=`grep -ic "wildparse(" $2 2>/dev/null`
if [ $WILDPARSE -gt 0 ]
then
   echo "   -- wildparse ($WILDPARSE found)"
   GRPVAL=`grep -in "wildparse(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
WILDMATCH=`grep -ic "wildmatch(" $2 2>/dev/null`
if [ $WILDMATCH -gt 0 ]
then
   echo "   -- wildmatch ($WILDMATCH found)"
   GRPVAL=`grep -in "wildmatch(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
WILDGREP=`grep -ic "wildgrep(" $2 2>/dev/null`
if [ $WILDGREP -gt 0 ]
then
   echo "   -- wildgrep ($WILDGREP found)"
   GRPVAL=`grep -in "wildgrep(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
WAIT=`grep -ic "wait(" $2 2>/dev/null`
if [ $WAIT -gt 0 ]
then
   echo "   -- wait ($WAIT found)"
   GRPVAL=`grep -in "wait(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
XCON=`grep -ic "xcon(" $2 2>/dev/null`
if [ $XCON -gt 0 ]
then
   echo "   -- xcon ($XCON found)"
   GRPVAL=`grep -in "xcon(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
WRITABLE=`grep -ic "writable(" $2 2>/dev/null`
if [ $WRITABLE -gt 0 ]
then
   echo "   -- writable ($WRITABLE found)"
   GRPVAL=`grep -in "writable(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
WRITE=`grep -ic "write(" $2 2>/dev/null`
if [ $WRITE -gt 0 ]
then
   echo "   -- write ($WRITE found)"
   GRPVAL=`grep -in "write(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
WIPE=`grep -ic "wipe(" $2 2>/dev/null`
if [ $WIPE -gt 0 ]
then
   echo "   -- wipe ($WIPE found)"
   GRPVAL=`grep -in "wipe(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REGEDITALLI=`grep -ic "regeditalli(" $2 2>/dev/null`
if [ $REGEDITALLI -gt 0 ]
then
   echo "   -- regeditalli ($REGEDITALLI found)"
   GRPVAL=`grep -in "regeditalli(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REGEDITALL=`grep -ic "regeditall(" $2 2>/dev/null`
if [ $REGEDITALL -gt 0 ]
then
   echo "   -- regeditall ($REGEDITALL found)"
   GRPVAL=`grep -in "regeditall(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REGEDITI=`grep -ic "regediti(" $2 2>/dev/null`
if [ $REGEDITI -gt 0 ]
then
   echo "   -- regediti ($REGEDITI found)"
   GRPVAL=`grep -in "regediti(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REGEDIT=`grep -ic "regedit(" $2 2>/dev/null`
if [ $REGEDIT -gt 0 ]
then
   echo "   -- regedit ($REGEDIT found)"
   GRPVAL=`grep -in "regedit(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REGRABALLI=`grep -ic "regraballi(" $2 2>/dev/null`
if [ $REGRABALLI -gt 0 ]
then
   echo "   -- regraballi ($REGRABALLI found)"
   GRPVAL=`grep -in "regraballi(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REGRABALL=`grep -ic "regraball(" $2 2>/dev/null`
if [ $REGRABALL -gt 0 ]
then
   echo "   -- regraball ($REGRABALL found)"
   GRPVAL=`grep -in "regraball(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REGREPI=`grep -ic "regrepi(" $2 2>/dev/null`
if [ $REGREPI -gt 0 ]
then
   echo "   -- regrepi ($REGREPI found)"
   GRPVAL=`grep -in "regrepi(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REGREP=`grep -ic "regrep(" $2 2>/dev/null`
if [ $REGREP -gt 0 ]
then
   echo "   -- regrep ($REGREP found)"
   GRPVAL=`grep -in "regrep(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REGMATCHI=`grep -ic "regmatchi(" $2 2>/dev/null`
if [ $REGMATCHI -gt 0 ]
then
   echo "   -- regmatchi ($REGMATCHI found)"
   GRPVAL=`grep -in "regmatchi(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REGPARSEI=`grep -ic "regparsei(" $2 2>/dev/null`
if [ $REGPARSEI -gt 0 ]
then
   echo "   -- regparsei ($REGPARSEI found)"
   GRPVAL=`grep -in "regparsei(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
REGPARSE=`grep -ic "regparse(" $2 2>/dev/null`
if [ $REGPARSE -gt 0 ]
then
   echo "   -- regparse ($REGPARSE found)"
   GRPVAL=`grep -in "regparse(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
READ=`grep -ic "read(" $2 2>/dev/null`
if [ $READ -gt 0 ]
then
   echo "   -- read ($READ found)"
   GRPVAL=`grep -in "read(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
LET=`grep -ic "let(" $2 2>/dev/null`
if [ $LET -gt 0 ]
then
   echo "   -- let ($LET found)"
   GRPVAL=`grep -in "let(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
HTML_ESCAPE=`grep -ic "html_escape(" $2 2>/dev/null`
if [ ${HTML_ESCAPE} -gt 0 ]
then
   echo "   -- html_escape (${HTML_ESCAPE} found)"
   GRPVAL=`grep -in "html_escape(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
URL_UNESCAPE=`grep -ic "url_unescape(" $2 2>/dev/null`
if [ ${URL_UNESCAPE} -gt 0 ]
then
   echo "   -- url_unescape (${URL_UNESCAPE} found)"
   GRPVAL=`grep -in "url_unescape(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
URL_ESCAPE=`grep -ic "url_escape(" $2 2>/dev/null`
if [ ${URL_ESCAPE} -gt 0 ]
then
   echo "   -- url_escape (${URL_ESCAPE} found)"
   GRPVAL=`grep -in "url_escape(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
HTML_UNESCAPE=`grep -ic "html_unescape(" $2 2>/dev/null`
if [ ${HTML_UNESCAPE} -gt 0 ]
then
   echo "   -- html_unescape (${HTML_UNESCAPE} found)"
   GRPVAL=`grep -in "html_unescape(" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
if [ $FND -eq 0 ]
then
   echo "   -- None Found"
fi
echo "6. Checking for TM 3.${CHKVAL} commands that need to be recoded to work."
FND=0
CMD_EVAL=`grep -ic "@eval" $2 2>/dev/null`
if [ ${CMD_EVAL} -gt 0 ]
then
   echo "   -- @eval (${CMD_EVAL} found)"
   GRPVAL=`grep -in "@eval" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_SQLDISCONNECT=`grep -ic "@sqldisconnect" $2 2>/dev/null`
if [ ${CMD_SQLDISCONNECT} -gt 0 ]
then
   echo "   -- @sqldisconnect (${CMD_SQLDISCONNECT} found)"
   GRPVAL=`grep -in "@sqldisconnect" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_SQLCONNECT=`grep -ic "@sqlconnect" $2 2>/dev/null`
if [ ${CMD_SQLCONNECT} -gt 0 ]
then
   echo "   -- @sqlconnect (${CMD_SQLCONNECT} found)"
   GRPVAL=`grep -in "@sqlconnect" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_SQL=`grep -ic "@sql" $2 2>/dev/null`
if [ ${CMD_SQL} -gt 0 ]
then
   echo "   -- @sql (${CMD_SQL} found)"
   GRPVAL=`grep -in "@sql" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_REFERENCE=`grep -ic "@reference" $2 2>/dev/null`
if [ ${CMD_REFERENCE} -gt 0 ]
then
   echo "   -- @reference (${CMD_REFERENCE} found)"
   GRPVAL=`grep -in "@reference" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_REDIRECT=`grep -ic "@redirect" $2 2>/dev/null`
if [ ${CMD_REDIRECT} -gt 0 ]
then
   echo "   -- @redirect (${CMD_REDIRECT} found)"
   GRPVAL=`grep -in "@redirect" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_LOGROTATE=`grep -ic "@logrotate" $2 2>/dev/null`
if [ ${CMD_LOGROTATE} -gt 0 ]
then
   echo "   -- @logrotate (${CMD_LOGROTATE} found)"
   GRPVAL=`grep -in "@logrotate" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_HOOK=`grep -ic "@hook" $2 2>/dev/null`
if [ ${CMD_HOOK} -gt 0 ]
then
   echo "   -- @hook (${CMD_HOOK} found)"
   GRPVAL=`grep -in "@hook" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_HASHRESIZE=`grep -ic "@hashresize" $2 2>/dev/null`
if [ ${CMD_HASHRESIZE} -gt 0 ]
then
   echo "   -- @hashresize (${CMD_HASHRESIZE} found)"
   GRPVAL=`grep -in "@hashresize" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_FREELIST=`grep -ic "@freelist" $2 2>/dev/null`
if [ ${CMD_FREELIST} -gt 0 ]
then
   echo "   -- @freelist (${CMD_FREELIST} found)"
   GRPVAL=`grep -in "@freelist" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_FLOATERS=`grep -ic "@floaters" $2 2>/dev/null`
if [ ${CMD_FLOATERS} -gt 0 ]
then
   echo "   -- @floaters (${CMD_FLOATERS} found)"
   GRPVAL=`grep -in "@floaters" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_CRONTAB=`grep -ic "@crontab" $2 2>/dev/null`
if [ ${CMD_CRONTAB} -gt 0 ]
then
   echo "   -- @crontab (${CMD_CRONTAB} found)"
   GRPVAL=`grep -in "@crontab" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_CRONDEL=`grep -ic "@crondel" $2 2>/dev/null`
if [ ${CMD_CRONDEL} -gt 0 ]
then
   echo "   -- @crondel (${CMD_CRONDEL} found)"
   GRPVAL=`grep -in "@crondel" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_CRON1=`grep -ic "@cron$" $2 2>/dev/null`
CMD_CRON2=`grep -ic "@cron;" $2 2>/dev/null`
CMD_CRON3=`grep -ic "@cron " $2 2>/dev/null`
CMD_CRON=`expr $CMD_CRON1 + $CMD_CRON2 + $CMD_CRON3`
if [ ${CMD_CRON} -gt 0 ]
then
   echo "   -- @cron (${CMD_CRON} found)"
   GRPVAL=`grep -in "@cron " $2|cut -f1 -d ":"`
   GRPVAL="$GRPVAL `grep -in '@cron$' $2|cut -f1 -d ':'`"
   GRPVAL="$GRPVAL `grep -in '@cron;' $2|cut -f1 -d ':'`"
   LASTQ=0
   for q in $GRPVAL
   do
      if [ "$LASTQ" = "$q" ]
      then
         continue
      fi
      LASTQ=$q
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
CMD_CHANNEL=`grep -ic "@channel" $2 2>/dev/null`
if [ ${CMD_CHANNEL} -gt 0 ]
then
   echo "   -- @channel (${CMD_CHANNEL} found)"
   GRPVAL=`grep -in "@channel" $2|cut -f1 -d ":"`
   for q in $GRPVAL
   do
      TVAL=`./do_detail 1 $q $2`
      TVAR2=`echo $TVAL|cut -f2 -d" "`
      if [ "$TVAR2" -gt "255" ]
      then
         ./do_detail 2 $TVAL $2
      else
         TVAR=`echo $TVAL|cut -f1 -d" "`
         GVAL=`grep " $TVAR2 " muxattrs|cut -f1 -d" "`
         echo "      >Object #$TVAR Attrib: $GVAL"
      fi
   done
   FND=1
fi
if [ $FND -eq 0 ]
then
   echo "   -- None Found"
fi
CHK=`cat ./tm3tomux_error.log|wc -l 2>/dev/null`
if [ $CHK -gt 0 ]
then
   echo "Conversion completed though some information may be lost."
   echo "Please review the file 'tm3tomux_error.log' for more information."
   echo "You may type 'more tm3tomux_error.log' now."
   echo "To load the database, use: db_load $2"
else
   echo "Conversion completed successfully.  You may db_load $2 into MUX 2"
fi
echo ""
echo "Please refer to the current documentation in MUX2 regarding loading"
echo "In flatfiles.  This will be a native MUX2 flatfile."
