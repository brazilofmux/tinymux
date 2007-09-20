#!/bin/sh
if [ -z "$1" ]
then
   echo "Syntax: $0 <penn-flatfile-name>"
   exit 1
fi
if [ ! -f ./pennconv_new ]
then
   if [ ! -f "${CC}" ]
   then
      cc > /dev/null 2>&1
      if [ $? -eq 127 ]
      then
         gcc > /dev/null 2>&1
         if [ $? -eq 127 ]
         then
            echo "Compiler not found.  Please identify compiler with the CC variable in this script."
            exit 1
         fi
         CC=gcc
      else
         CC=cc
      fi
   fi
   echo "Compiling source tree for penn converter...."|tr -d '\012'
   ${CC} pennconv_new.c -o pennconv_new > /dev/null 2>&1
   if [ $? -ne 0 ]
   then
      echo "error on pennconv_new.c  Aborted."
      exit 1
   else
      echo "pennconv_new...Completed."
   fi
fi
echo "Reading attribute table.  Please wait..."|tr -d '\012'
awk ' 
BEGIN {
   in_attr = 0;
   attrcntr = 0;
}
{
   if ( match ($1, "!.*") ) {
      in_attr = 1
   }
   if ( in_attr ) {
      if ( match($1, "attrcount") ) {
         attrcount=$2
         attrcntr=1
      }
      if ( match($1, "name") && attrcntr <= attrcount ) {
         printf("%s\n", $2);
         attrcntr++
      }
   }
}' < $1 > ${1}.out 2>/dev/null
echo "...completed."
echo "Reading flag table.  Please wait..."|tr -d '\012'
awk '
{
   if ( match($1, "flagcount") ) {
      flagcount=$2
      flagcntr=1
   }
   if ( match($1, "name") && flagcntr <= flagcount ) {
       printf("%s\n", $2);
       flagcntr++
   }
   if ( flagcntr > flagcount )
      exit;
}' < $1 > ${1}.flagout
echo "...completed."
echo "Sorting file and removing duplicate entries..."|tr -d '\012'
cat ${1}.out|sed "s/\"//g"|sort -u > ${1}.tmp
rm -f ${1}.out
cat ${1}.flagout|sed "s/\"//g"|sort -u > ${1}.flagtmp
rm -f ${1}.flagout
echo "...completed."
:>${1}.id
:>${1}.flag_id
:>${1}.exception
:>${1}.flag_exception
echo "Processing flag table extraction and assigning values for $(wc -l ${1}.flagtmp|cut -f1 -d" ") flags..."|tr -d '\012'
while read flagname
do
   FLAG=$(grep "${flagname} " rhost_flagmapping.dat)
   if [ -z "${FLAG##* }" -o "${FLAG##* }" = "NONE" ]
   then
      echo "${flagname}" >> ${1}.flag_exception
      continue
   fi
   CHK=$(grep "${FLAG##* } " rhost_flags.dat)
   if [ ! -z "${CHK}" ]
   then
      echo "${flagname} ${CHK#* }" >> ${1}.flag_id
   else
      echo "${flagname}" >> ${1}.flag_exception
   fi
done < ${1}.flagtmp
rm -f ${1}.flagtmp
echo "$(wc -l ${1}.flag_exception|cut -f1 -d" ") flags non-convertable."|tr -d '\012'
echo "...Completed."
ATRCNTR=512
ATRRENAME=1
FMAX=$(wc -l ${1}.tmp|cut -f1 -d" ")
((F10=${FMAX} \* 10 / 100))
((F20=${FMAX} \* 20 / 100))
((F30=${FMAX} \* 30 / 100))
((F40=${FMAX} \* 40 / 100))
((F50=${FMAX} \* 50 / 100))
((F60=${FMAX} \* 60 / 100))
((F70=${FMAX} \* 70 / 100))
((F80=${FMAX} \* 80 / 100))
((F90=${FMAX} \* 90 / 100))
echo "Processing attribute table extraction and asigning unique identifiers for $(wc -l ${1}.tmp|cut -f1 -d" ") attributes."
echo "This may take a while [several minutes]."
echo "Please wait... [ 0%..."|tr -d '\012'
FCNTR=1
while read attrname
do
   case ${FCNTR} in
   ${F10}) echo "10%..."|tr -d '\012' >&2
           ;;
   ${F20}) echo "20%..."|tr -d '\012' >&2
           ;;
   ${F30}) echo "30%..."|tr -d '\012' >&2
           ;;
   ${F40}) echo "40%..."|tr -d '\012' >&2
           ;;
   ${F50}) echo "50%..."|tr -d '\012' >&2
           ;;
   ${F60}) echo "60%..."|tr -d '\012' >&2
           ;;
   ${F70}) echo "70%..."|tr -d '\012' >&2
           ;;
   ${F80}) echo "80%..."|tr -d '\012' >&2
           ;;
   ${F90}) echo "90%..."|tr -d '\012' >&2
           ;;
   esac
   ((FCNTR=${FCNTR}+1))
   if [ -z "$(echo "${attrname}"|tr -d " ")" ]
   then
      continue
   fi
   CHK=$(grep "^${attrname} " rhost_attrs.dat 2>/dev/null)
   CHK2=$(grep -ic "^${attrname} " rhost.dat 2>/dev/null)
   CHK3=$(echo ${attrname}|cut -c1|tr -d "A-Za-z_~#")
   if [ ! -z "${CHK3}" ]
   then
      BEF="X${attrname}"
   else
      BEF="${attrname}"
   fi
   if [ -z "${CHK}" ]
   then
      if [ ${#BEF} -gt 59 -o ${CHK2} -gt 0 ]
      then
         NEWATTR="$(echo "${BEF}"|cut -c1-59)$(echo ${ATRRENAME}|awk '{printf("%03d\n", $1)}'))"
         echo "${attrname} ${NEWATTR}" >> ${1}.exception
         echo "${NEWATTR} ${ATRCNTR}" >> ${1}.id
         ((ATRCNTR=${ATRCNTR}+1))
      else
         echo "${BEF} ${ATRCNTR}" >> ${1}.id
         if [ "${BEF}" != "${attrname}" ]
         then
            echo "${attrname} ${BEF}" >> ${1}.exception
         fi
      fi
      ((ATRCNTR=${ATRCNTR}+1))
   else
      echo "${CHK}" >> ${1}.id
   fi
done < ${1}.tmp > ${1}.err
rm -f ${1}.tmp
echo "100% ] Finished."
echo "A total of $(wc -l ${1}.exception|cut -f1 -d" ") attributes had to be renamed."
echo "Building flatfile.  This may take a SHORT while.  Please wait..."|tr -d '\012'
TOTOBJS=$(grep -c "^\![0-9]" $1)
echo "+V74247" > ${1}_rhost.conv
echo "+S${TOTOBJS}" >> ${1}_rhost.conv
echo "+N${ATRCNTR}" >> ${1}_rhost.conv
while read line1 line2
do
   if [ ${line2} -gt 256 ]
   then
      echo "+A${line2}" >> ${1}_rhost.conv
      echo "1:${line1}" >> ${1}_rhost.conv
   fi
done < ${1}.id
echo "Done."
echo "Rewriting object data structures.  This will take a long time."
echo "Please wait..."|tr -d '\012'
./pennconv_new ${1} rhost ${TOTOBJS}
cat ${1}.out >> ${1}_rhost.conv
rm -f ${1}.out
echo "Finished."
echo "Correlating error reports."
echo "Attribute conversion errors..."|tr -d '\012'
echo "----------------------------------------------------------------------" >> ${1}.err
echo "Attributes that had to be renamed due to naming conventions in Rhost" >> ${1}.err
echo "----------------------------------------------------------------------" >> ${1}.err
while read line1 line2 
do
   echo "Attribute '${line1}' had to be renamed '${line2}'." >> ${1}.err
done < ${1}.exception
echo "Finished."
echo "Flag conversion errors..."|tr -d '\012'
echo "----------------------------------------------------------------------" >> ${1}.err
echo "Flags removed for not having any relation to existing Rhost flags" >> ${1}.err
echo "----------------------------------------------------------------------" >> ${1}.err
while read line1
do
   echo "Flag ${line1} does not exist within Rhost." >> ${1}.err
done < ${1}.flag_exception
echo "Finished."
echo "Cleaning up temporary files..."|tr -d '\012'
rm -f korongil_outdb.exception
rm -f korongil_outdb.flag_exception
rm -f korongil_outdb.flag_id
rm -f korongil_outdb.id
echo "finished."
echo ""
echo "Error reports are located in: ${1}.err"
echo "The Rhost compatable flatfile is: ${1}_rhost.conv"
echo ""
echo "Convertion completed."
