Rem @Echo Off
del *.dll
del *.exe
set TimeURL=http://timestamp.digicert.com
set P12=d:\certs\brazilofmux.p12
echo %1%
for %%i in (comsys.dll engine.dll exp3.dll libmux.dll mail.dll muxscript.exe netmux.exe sqlproxy.dll sqlslave.dll) do (
	copy /y ..\..\bin_release\%%i %%i
)
copy /y ..\..\src\pcre2\Release\pcre2-8.dll pcre2-8.dll
copy /y ..\..\rv64\softlib.rv64 softlib.rv64
set VCREDIST=C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Redist\MSVC\14.50.35710\x64\Microsoft.VC145.CRT
copy /y "%VCREDIST%\msvcp140.dll" msvcp140.dll
copy /y "%VCREDIST%\vcruntime140.dll" vcruntime140.dll
copy /y "%VCREDIST%\vcruntime140_1.dll" vcruntime140_1.dll
signtool.exe sign /fd sha512 /td sha512 /f %P12% /p %1 /tr %TimeURL% /v /d "Comsys Module" /du "http://www.tinymux.org" comsys.dll
signtool.exe sign /fd sha512 /td sha512 /f %P12% /p %1 /tr %TimeURL% /v /d "Engine Module" /du "http://www.tinymux.org" engine.dll
signtool.exe sign /fd sha512 /td sha512 /f %P12% /p %1 /tr %TimeURL% /v /d "Exp3 Module" /du "http://www.tinymux.org" exp3.dll
signtool.exe sign /fd sha512 /td sha512 /f %P12% /p %1 /tr %TimeURL% /v /d "Module Support for TinyMUX" /du "http://www.tinymux.org" libmux.dll
signtool.exe sign /fd sha512 /td sha512 /f %P12% /p %1 /tr %TimeURL% /v /d "Mail Module" /du "http://www.tinymux.org" mail.dll
signtool.exe sign /fd sha512 /td sha512 /f %P12% /p %1 /tr %TimeURL% /v /d "TinyMUX Script Runner" /du "http://www.tinymux.org" muxscript.exe
signtool.exe sign /fd sha512 /td sha512 /f %P12% /p %1 /tr %TimeURL% /v /d "TinyMUX Server" /du "http://www.tinymux.org" netmux.exe
signtool.exe sign /fd sha512 /td sha512 /f %P12% /p %1 /tr %TimeURL% /v /d "PCRE2 Regular Expression Library" /du "http://www.tinymux.org" pcre2-8.dll
signtool.exe sign /fd sha512 /td sha512 /f %P12% /p %1 /tr %TimeURL% /v /d "SQL Connector Proxy/Stub" /du "http://www.tinymux.org" sqlproxy.dll
signtool.exe sign /fd sha512 /td sha512 /f %P12% /p %1 /tr %TimeURL% /v /d "SQL Connector" /du "http://www.tinymux.org" sqlslave.dll
