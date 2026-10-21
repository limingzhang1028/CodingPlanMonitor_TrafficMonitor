@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "OUT=%ROOT%build\out"
set "VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"

if not exist "%VSDEVCMD%" (
    echo Visual Studio Build Tools 2022 was not found.
    exit /b 1
)

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1

if not exist "%OUT%" mkdir "%OUT%"

set "COMMON=/nologo /std:c++17 /Zc:__cplusplus /EHsc /MD /O2 /DNDEBUG /DUNICODE /D_UNICODE /utf-8 /W4 /Fo:"%OUT%\\""
set "INCLUDES=/I"%ROOT%src" /I"%ROOT%third_party\TrafficMonitor\include" /I"%ROOT%third_party\nlohmann-json\single_include""

cl %COMMON% %INCLUDES% /LD ^
    "%ROOT%src\CcSwitchSource.cpp" ^
    "%ROOT%src\QuotaClient.cpp" ^
    "%ROOT%src\QuotaItem.cpp" ^
    "%ROOT%src\QuotaParser.cpp" ^
    "%ROOT%src\QuotaPlugin.cpp" ^
    "%ROOT%src\QuotaService.cpp" ^
    "%ROOT%src\QuotaSnapshot.cpp" ^
    "%ROOT%src\SelectorDialog.cpp" ^
    "%ROOT%src\Utf8.cpp" ^
    /Fe:"%OUT%\KimiQuotaPlugin.dll" ^
    /link /NOLOGO winhttp.lib winsqlite3.lib user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1

cl %COMMON% %INCLUDES% ^
    "%ROOT%tests\quota_parser_tests.cpp" ^
    "%ROOT%src\QuotaParser.cpp" ^
    "%ROOT%src\QuotaSnapshot.cpp" ^
    "%ROOT%src\Utf8.cpp" ^
    /Fe:"%OUT%\quota_parser_tests.exe" ^
    /link /NOLOGO user32.lib
if errorlevel 1 exit /b 1

cl %COMMON% %INCLUDES% ^
    "%ROOT%tools\live_quota_check.cpp" ^
    "%ROOT%src\CcSwitchSource.cpp" ^
    "%ROOT%src\QuotaClient.cpp" ^
    "%ROOT%src\QuotaParser.cpp" ^
    "%ROOT%src\QuotaSnapshot.cpp" ^
    "%ROOT%src\Utf8.cpp" ^
    /Fe:"%OUT%\live_quota_check.exe" ^
    /link /NOLOGO winhttp.lib winsqlite3.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1

"%OUT%\quota_parser_tests.exe"
if errorlevel 1 exit /b 1

echo Built %OUT%\KimiQuotaPlugin.dll
