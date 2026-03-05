@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >NUL 2>&1
copy /Y autoconf_win.h autoconf.h >NUL
echo Building buildFiles.exe...
cl /EHsc /O2 /Fe:buildFiles.exe buildFiles.cpp smutil.cpp
if errorlevel 1 exit /b 1
echo Building classify.exe...
cl /EHsc /O2 /Fe:classify.exe classify.cpp ConvertUTF.cpp smutil.cpp
if errorlevel 1 exit /b 1
echo Building integers.exe...
cl /EHsc /O2 /Fe:integers.exe integers.cpp ConvertUTF.cpp smutil.cpp
if errorlevel 1 exit /b 1
echo Building strings.exe...
cl /EHsc /O2 /Fe:strings.exe strings.cpp ConvertUTF.cpp smutil.cpp
if errorlevel 1 exit /b 1
echo All tools built successfully.
