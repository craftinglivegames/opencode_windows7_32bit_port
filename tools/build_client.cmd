@echo off
REM ---------------------------------------------------------------------------
REM  Builds OCShellClient.exe - 32-bit (x86) Windows 7 launcher for the
REM  opencode web UI.  Static CRT (/MT), no external dependencies.
REM
REM  Output: dist\OCShellClient.exe
REM ---------------------------------------------------------------------------
setlocal
set ROOT=%~dp0..
set BUILD=%ROOT%\build
set OUT=%ROOT%\dist\OCShellClient.exe
set VC="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"

if not exist %VC% (
  echo ERROR: vcvarsall.bat not found: %VC%
  exit /b 1
)

echo === x86 environment ===
call %VC% x86 >nul || exit /b 1

if not exist "%BUILD%" mkdir "%BUILD%"
if not exist "%ROOT%\dist" mkdir "%ROOT%\dist"

echo === compiling client.cpp (x86) ===
cl /nologo /c /MT /O2 /W3 /GS- /utf-8 ^
   /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 ^
   /D_CRT_SECURE_NO_WARNINGS ^
   "%ROOT%\src\client.cpp" /Fo"%BUILD%\client.obj" || exit /b 1

echo === linking dist\OCShellClient.exe ===
link /nologo /SUBSYSTEM:WINDOWS,6.01 /MACHINE:X86 /OPT:REF /OPT:ICF ^
   /OUT:"%OUT%" "%BUILD%\client.obj" ^
   user32.lib winhttp.lib advapi32.lib shell32.lib ws2_32.lib || exit /b 1

if not exist "%OUT%" (
  echo ERROR: %OUT% was not produced
  exit /b 1
)

echo === verification ===
dumpbin /nologo /headers "%OUT%" | findstr /i "machine" || exit /b 1
dumpbin /nologo /headers "%OUT%" | findstr /i "subsystem" || exit /b 1
for %%F in ("%OUT%") do echo size: %%~zF bytes

echo.
echo build ok: %OUT%
endlocal
