@echo off
REM run-msvc.bat — build and run the GANL harness on Windows (#1857).
REM
REM Builds ganl_tests.vcxproj (wselect + iocp engine matrix) and
REM ganl_connection_tests.vcxproj (ConnectionBase fake-driven scenarios).
REM Propagates a nonzero exit if either executable fails TAP.
REM
REM Usage (from a VS developer prompt, or any shell with MSBuild on PATH):
REM   mux\ganl\tests\run-msvc.bat
REM   mux\ganl\tests\run-msvc.bat Release
REM
REM Default configuration is Release|x64.

setlocal EnableExtensions
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release

set SCRIPT_DIR=%~dp0
set ROOT=%SCRIPT_DIR%..\..\..
set TESTS=%SCRIPT_DIR%
set OUTDIR=%ROOT%\mux\bin_%CONFIG%
if /I "%CONFIG%"=="Debug" set OUTDIR=%ROOT%\mux\bin_debug
if /I "%CONFIG%"=="Release" set OUTDIR=%ROOT%\mux\bin_release

where msbuild >nul 2>&1
if errorlevel 1 (
  echo ERROR: msbuild not on PATH. Open a "x64 Native Tools" VS prompt, or
  echo        run from a shell where vswhere has seeded the environment.
  exit /b 1
)

echo ==^> MSBuild ganl_tests.vcxproj /p:Configuration=%CONFIG% Platform=x64
msbuild "%TESTS%ganl_tests.vcxproj" /m /p:Configuration=%CONFIG% /p:Platform=x64 /v:minimal
if errorlevel 1 exit /b 1

echo ==^> MSBuild ganl_connection_tests.vcxproj /p:Configuration=%CONFIG% Platform=x64
msbuild "%TESTS%ganl_connection_tests.vcxproj" /m /p:Configuration=%CONFIG% /p:Platform=x64 /v:minimal
if errorlevel 1 exit /b 1

set FAIL=0

echo ==^> Run ganl_tests.exe (wselect + iocp engines)
if not exist "%OUTDIR%\ganl_tests.exe" (
  echo ERROR: missing %OUTDIR%\ganl_tests.exe
  exit /b 1
)
"%OUTDIR%\ganl_tests.exe"
if errorlevel 1 set FAIL=1

echo ==^> Run ganl_connection_tests.exe (ConnectionBase harness)
if not exist "%OUTDIR%\ganl_connection_tests.exe" (
  echo ERROR: missing %OUTDIR%\ganl_connection_tests.exe
  exit /b 1
)
"%OUTDIR%\ganl_connection_tests.exe"
if errorlevel 1 set FAIL=1

if %FAIL% NEQ 0 (
  echo FAIL: one or more GANL Windows harness legs failed
  exit /b 1
)
echo PASS: GANL Windows harness
exit /b 0
