all: classify transform

classify: classify.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp
	g++ -O3 -fomit-frame-pointer -o classify classify.cpp ConvertUTF.cpp smutil.cpp

transform: transform.cpp ConvertUTF.cpp ConvertUTF.h smutil.cpp
	g++ -O3 -fomit-frame-pointer -o transform transform.cpp ConvertUTF.cpp smutil.cpp
