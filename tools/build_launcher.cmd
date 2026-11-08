@echo off
REM Builds OpenCode.exe - 32-bit (x86) launcher for the portable x32 package.
setlocal
set ROOT=%~dp0..
set BUILD=%ROOT%\build
set OUT=%ROOT%\dist\OpenCode.exe
set VC="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"

if not exist %VC% (
  echo ERROR: vcvarsall.bat not found: %VC%
  exit /b 1
)

call %VC% x86 >nul || exit /b 1
if not exist "%BUILD%" mkdir "%BUILD%"
if not exist "%ROOT%\dist" mkdir "%ROOT%\dist"

cl /nologo /c /MT /O2 /W3 /GS- /utf-8 /std:c++17 ^
   /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 ^
   /D_CRT_SECURE_NO_WARNINGS ^
   "%ROOT%\src\local_launcher.cpp" /Fo"%BUILD%\local_launcher.obj" || exit /b 1

link /nologo /SUBSYSTEM:WINDOWS,6.01 /ENTRY:wmainCRTStartup /MACHINE:X86 /OPT:REF /OPT:ICF ^
   /OUT:"%OUT%" "%BUILD%\local_launcher.obj" ^
   user32.lib shell32.lib advapi32.lib ws2_32.lib iphlpapi.lib || exit /b 1

if not exist "%OUT%" (
  echo ERROR: %OUT% was not produced
  exit /b 1
)

dumpbin /nologo /headers "%OUT%" | findstr /i "machine" || exit /b 1
dumpbin /nologo /headers "%OUT%" | findstr /i "subsystem" || exit /b 1
for %%F in ("%OUT%") do echo size: %%~zF bytes
echo build ok: %OUT%
endlocal
