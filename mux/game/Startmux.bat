@echo off
rem Startmux.bat - Start TinyMUX 2.x server on Windows.
rem
rem This replaces Startmux.wsf which depended on Windows Script Host.
rem Edit the variables below to match your configuration.

set BIN=.\bin
set GAMENAME=netmux
set LOGDIR=.
set PIDFILE=%GAMENAME%.pid

:loop
"%BIN%\netmux" -c %GAMENAME%.conf -p %PIDFILE% -e %LOGDIR%
if %errorlevel% equ 12345678 goto loop
