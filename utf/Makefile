all: classify transform

UnicodeData.txt:
	wget http://www.unicode.org/Public/UNIDATA/UnicodeData.txt

table: UnicodeData.txt UnicodeHan.txt
	perl buildUnicodeMaster.pl UnicodeData.txt UnicodeHan.txt > UnicodeDataNew.txt
	
printable: table UnicodeDataNew.txt StringprepProhibited.txt
	perl buildStringprepPrintable.pl StringprepProhibited.txt UnicodeDataNew.txt > cl_Printable.txt

classify: classify.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp
	g++ -O3 -fomit-frame-pointer -o classify classify.cpp ConvertUTF.cpp smutil.cpp

transform: transform.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp
	g++ -O3 -fomit-frame-pointer -o transform transform.cpp ConvertUTF.cpp smutil.cpp

stringtable: printable classify
	./classify cl_print cl_Printable.txt > stringutil.txt
