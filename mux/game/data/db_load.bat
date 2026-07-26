@echo off
rem
rem   Load a MUX flatfile into the SQLite database.
rem
rem   Usage: db_load [-f] <basename> <flatfile> [-C <comsys.db>] [-m <mail.db>]
rem
rem   Example: db_load netmux netmux.flat
rem            db_load netmux netmux.flat -C comsys.db -m mail.db
rem
rem   The SQLite database (<basename>.sqlite) is always written next to this
rem   script, i.e. into the game's data\ directory, so it matches the server's
rem   configured input_database no matter which directory you run this from.
rem   Flatfile arguments are resolved relative to your current directory.
rem
rem   Use -f to replace an existing SQLite database (the load otherwise
rem   refuses to overwrite a database that already contains a game).
rem
rem   Runs from the game directory rather than from data\: init_modules()
rem   loads .\bin\engine.dll relative to the current directory, so a conversion
rem   started from anywhere else fails with "Failed to initialize modules"
rem   (#1336).  netmux.exe is the converter -- the -d/-l flags select
rem   standalone mode, so the Unix "dbconvert" symlink has no counterpart here.
rem
rem   Note: The server should not be running during import.
rem

setlocal

rem  Capture the script name before any shift: shift moves %0 as well, so
rem  %~nx0 in the usage text would otherwise print the last shifted argument.
set "ME=%~nx0"

set "FORCE="
if /i "%~1"=="-f" (
    set "FORCE=-f"
    shift
)

if "%~1"=="" goto usage
if "%~2"=="" goto usage

set "DATADIR=%~dp0"
set "GAMEDIR=%~dp0.."
set "BASENAME=%~1"
set "FLATFILE=%~f2"
shift
shift

set "COMSYS="
set "MAIL="

:parse
if "%~1"=="" goto run
if /i "%~1"=="-C" (
    if "%~2"=="" goto usage
    set "COMSYS=%~f2"
    shift
    shift
    goto parse
)
if /i "%~1"=="-m" (
    if "%~2"=="" goto usage
    set "MAIL=%~f2"
    shift
    shift
    goto parse
)
if /i "%~1"=="-f" (
    set "FORCE=-f"
    shift
    goto parse
)
goto usage

:run
set "COPT="
if defined COMSYS set COPT=-C "%COMSYS%"
set "MOPT="
if defined MAIL set MOPT=-m "%MAIL%"

pushd "%GAMEDIR%"
if errorlevel 1 exit /b 1

echo Loading into: %DATADIR%%BASENAME%.sqlite

bin\netmux.exe -d "data\%BASENAME%" -l -i "%FLATFILE%" %FORCE% %COPT% %MOPT%
set "RC=%ERRORLEVEL%"

popd
exit /b %RC%

:usage
echo Usage: %ME% [-f] ^<basename^> ^<flatfile^> [-C ^<comsys.db^>] [-m ^<mail.db^>]
echo   e.g. %ME% netmux netmux.flat
echo        %ME% netmux netmux.flat -C comsys.db -m mail.db
echo   -f   Replace an existing SQLite database.
exit /b 1
