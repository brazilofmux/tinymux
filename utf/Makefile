all: classify integers strings stringutil.txt SpecialCasing.txt cp1252.txt CaseFolding.txt Decompositions.txt

UnicodeData.txt:
	wget http://www.unicode.org/Public/UNIDATA/UnicodeData.txt

SpecialCasing.txt:
	wget http://www.unicode.org/Public/UNIDATA/SpecialCasing.txt

CaseFolding.txt:
	wget http://www.unicode.org/Public/UNIDATA/CaseFolding.txt

cp1252.txt:
	wget http://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP1252.TXT -O cp1252.txt

UnicodeMaster.txt: UnicodeData.txt UnicodeHan.txt
	perl buildUnicodeMaster.pl UnicodeData.txt UnicodeHan.txt > UnicodeMaster.txt

Decompositions.txt: UnicodeMaster.txt
	perl buildDecompositions.pl UnicodeMaster.txt > Decompositions.txt

cl_Printable.txt: UnicodeMaster.txt StringprepProhibited.txt
	perl buildStringprepPrintable.pl StringprepProhibited.txt UnicodeMaster.txt > cl_Printable.txt

cl_PrivateUse.txt: PrivateUse.txt
	perl buildPrivateUse.pl PrivateUse.txt > cl_PrivateUse.txt

tr_tolower.txt: UnicodeMaster.txt
	perl buildToLower.pl UnicodeMaster.txt > tr_tolower.txt

tr_toupper.txt: UnicodeMaster.txt
	perl buildToUpper.pl UnicodeMaster.txt > tr_toupper.txt

tr_totitle.txt: UnicodeMaster.txt
	perl buildToTitle.pl UnicodeMaster.txt > tr_totitle.txt

classify: classify.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp smutil.h
	g++ -O3 -g -o classify classify.cpp ConvertUTF.cpp smutil.cpp

integers: integers.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp smutil.h
	g++ -O3 -g -o integers integers.cpp ConvertUTF.cpp smutil.cpp

strings: strings.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp smutil.h
	g++ -O3 -g -o strings strings.cpp ConvertUTF.cpp smutil.cpp

stringutil.txt: classify integers strings cl_Printable.txt cl_AttrNameInitial.txt cl_AttrName.txt cl_PrivateUse.txt tr_utf8_latin1.txt tr_utf8_ascii.txt tr_Color.txt tr_tolower.txt tr_toupper.txt tr_totitle.txt
	./classify cl_print cl_Printable.txt > stringutil.txt
	./classify cl_attrnameinitial cl_AttrNameInitial.txt >> stringutil.txt
	./classify cl_attrname cl_AttrName.txt >> stringutil.txt
	./classify cl_objectname cl_ObjectName.txt >> stringutil.txt
	./classify cl_playername cl_PlayerName.txt >> stringutil.txt
	./classify cl_privateuse cl_PrivateUse.txt >> stringutil.txt
	./integers -d 63 tr_latin1 tr_utf8_latin1.txt >> stringutil.txt
	./integers -d 63 tr_ascii tr_utf8_ascii.txt >> stringutil.txt
	./strings -d tr_tolower tr_tolower.txt >> stringutil.txt
	./strings -d tr_toupper tr_toupper.txt >> stringutil.txt
	./strings -d tr_totitle tr_totitle.txt >> stringutil.txt
	./integers -d 0 tr_color tr_Color.txt >> stringutil.txt
