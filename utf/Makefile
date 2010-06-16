# Local copy of ftp://www.unicode.org/
#
UNICODE=~/unicode

all: classify integers strings utf8tables.cpp.txt utf8tables.h.txt SpecialCasing.txt CaseFolding.txt Decompositions.txt buildFiles

UnicodeData.txt:
	( if [ -f $(UNICODE)/UNIDATA/UnicodeData.txt ]; then ln -s $(UNICODE)/UNIDATA/UnicodeData.txt UnicodeData.txt; else wget http://www.unicode.org/Public/UNIDATA/UnicodeData.txt; fi )

SpecialCasing.txt:
	( if [ -f $(UNICODE)/UNIDATA/SpecialCasing.txt ]; then ln -s $(UNICODE)/UNIDATA/SpecialCasing.txt SpecialCasing.txt; else wget http://www.unicode.org/Public/UNIDATA/SpecialCasing.txt; fi )

CaseFolding.txt:
	( if [ -f $(UNICODE)/UNIDATA/CaseFolding.txt ]; then ln -s $(UNICODE)/UNIDATA/CaseFolding.txt CaseFolding.txt; else wget http://www.unicode.org/Public/UNIDATA/CaseFolding.txt; fi )

UnicodeMaster.txt tr_totitle.txt tr_tolower.txt tr_toupper.txt cl_PrivateUse.txt Decompositions.txt cl_Printable.txt tr_decimal_value.txt: UnicodeData.txt UnicodeHan.txt buildFiles
	./buildFiles

classify: classify.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp smutil.h
	g++ -O3 -g -o classify classify.cpp ConvertUTF.cpp smutil.cpp

integers: integers.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp smutil.h
	g++ -O3 -g -o integers integers.cpp ConvertUTF.cpp smutil.cpp

strings: strings.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp smutil.h
	g++ -O3 -g -o strings strings.cpp ConvertUTF.cpp smutil.cpp

buildFiles: buildFiles.cpp smutil.cpp smutil.h
	g++ -O3 -g -o buildFiles buildFiles.cpp smutil.cpp

utf8tables.cpp.txt utf8tables.h.txt: classify integers strings cl_Printable.txt cl_AttrNameInitial.txt cl_AttrName.txt cl_PrivateUse.txt tr_utf8_ascii.txt tr_utf8_cp437.txt tr_utf8_latin1.txt tr_utf8_latin2.txt tr_Color.txt tr_tolower.txt tr_toupper.txt tr_totitle.txt cl_8859_1.txt cl_8859_2.txt
	-rm utf8tables.cpp.txt utf8tables.h.txt
	./classify cl_print cl_Printable.txt
	./classify cl_attrnameinitial cl_AttrNameInitial.txt
	./classify cl_attrname cl_AttrName.txt
	./classify cl_objectname cl_ObjectName.txt
	./classify cl_playername cl_PlayerName.txt
	./classify cl_8859_1 cl_8859_1.txt
	./classify cl_8859_2 cl_8859_2.txt
	./integers -d 63 tr_ascii tr_utf8_ascii.txt
	./integers -d 63 tr_cp437 tr_utf8_cp437.txt
	./integers -d 63 tr_latin1 tr_utf8_latin1.txt
	./integers -d 63 tr_latin2 tr_utf8_latin2.txt
	./strings -d tr_tolower tr_tolower.txt
	./strings -d tr_toupper tr_toupper.txt
	./strings -d tr_totitle tr_totitle.txt
	./strings -d tr_foldmatch tr_foldmatch.txt
	./integers -d 0 tr_color tr_Color.txt
	#./integers -d 0 tr_class tr_class.txt
