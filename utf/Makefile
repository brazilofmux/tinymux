all: classify transform stringutil.txt

UnicodeData.txt:
	wget http://www.unicode.org/Public/UNIDATA/UnicodeData.txt

UnicodeDataNew.txt: UnicodeData.txt UnicodeHan.txt
	perl buildUnicodeMaster.pl UnicodeData.txt UnicodeHan.txt > UnicodeDataNew.txt

cl_Printable.txt: UnicodeDataNew.txt StringprepProhibited.txt
	perl buildStringprepPrintable.pl StringprepProhibited.txt UnicodeDataNew.txt > cl_Printable.txt

classify: classify.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp smutil.h
	g++ -O3 -g -o classify classify.cpp ConvertUTF.cpp smutil.cpp

transform: transform.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp smutil.h
	g++ -O3 -g -o transform transform.cpp ConvertUTF.cpp smutil.cpp

stringutil.txt: classify transform cl_Printable.txt cl_AttrNameInitial.txt cl_AttrName.txt tr_utf8_latin1.txt tr_utf8_ascii.txt
	./classify cl_print cl_Printable.txt > stringutil.txt
	./classify cl_attrnameinitial cl_AttrNameInitial.txt >> stringutil.txt
	./classify cl_attrname cl_AttrName.txt >> stringutil.txt
	./transform -c tr_latin1 tr_utf8_latin1.txt >> stringutil.txt
	./transform -c tr_ascii tr_utf8_ascii.txt >> stringutil.txt
