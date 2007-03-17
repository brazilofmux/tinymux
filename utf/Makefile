all: classify transform stringutil.txt

UnicodeData.txt:
	wget http://www.unicode.org/Public/UNIDATA/UnicodeData.txt

UnicodeDataNew.txt: UnicodeData.txt UnicodeHan.txt
	perl buildUnicodeMaster.pl UnicodeData.txt UnicodeHan.txt > UnicodeDataNew.txt

cl_Printable.txt: UnicodeDataNew.txt StringprepProhibited.txt
	perl buildStringprepPrintable.pl StringprepProhibited.txt UnicodeDataNew.txt > cl_Printable.txt

cl_PrivateUse.txt: PrivateUse.txt
	perl buildPrivateUse.pl PrivateUse.txt > cl_PrivateUse.txt

classify: classify.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp smutil.h
	g++ -O3 -g -o classify classify.cpp ConvertUTF.cpp smutil.cpp

transform: transform.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp smutil.h
	g++ -O3 -g -o transform transform.cpp ConvertUTF.cpp smutil.cpp

stringutil.txt: classify transform cl_Printable.txt cl_AttrNameInitial.txt cl_AttrName.txt cl_PrivateUse.txt tr_utf8_latin1.txt tr_utf8_ascii.txt tr_Color.txt
	./classify cl_print cl_Printable.txt > stringutil.txt
	./classify cl_attrnameinitial cl_AttrNameInitial.txt >> stringutil.txt
	./classify cl_attrname cl_AttrName.txt >> stringutil.txt
	./classify cl_objectname cl_ObjectName.txt >> stringutil.txt
	./classify cl_upper cl_Upper.txt >> stringutil.txt
	./classify cl_lower cl_Lower.txt >> stringutil.txt
	./classify cl_privateuse cl_PrivateUse.txt >> stringutil.txt
	./transform -c 63 tr_latin1 tr_utf8_latin1.txt >> stringutil.txt
	./transform -c 63 tr_ascii tr_utf8_ascii.txt >> stringutil.txt
	./transform tr_tolower tr_tolower.txt >> stringutil.txt
	./transform tr_toupper tr_toupper.txt >> stringutil.txt
	./transform -c 0 tr_color tr_Color.txt >> stringutil.txt
