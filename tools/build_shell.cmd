@echo off
REM ---------------------------------------------------------------------------
REM  Builds the OC Shell launcher (Windows 7 compatible) and the shim DLLs.
REM ---------------------------------------------------------------------------
setlocal
set ROOT=%~dp0..
set BUILD=%ROOT%\build
set VC="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist %VC% (
  echo ERROR: vcvarsall.bat not found
  exit /b 1
)
call %VC% x64 >nul || exit /b 1
if not exist "%BUILD%" mkdir "%BUILD%"

echo === compiling zlib ===
if not exist "%BUILD%\zlib" mkdir "%BUILD%\zlib"
for %%F in (adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil) do (
  if exist "%ROOT%\third_party\zlib-1.3.1\%%F.c" (
    cl /nologo /c /O2 /MT /GS- /W0 /D_CRT_SECURE_NO_WARNINGS /DZLIB_CONST ^
      "%ROOT%\third_party\zlib-1.3.1\%%F.c" /Fo"%BUILD%\zlib\%%F.obj" >nul || exit /b 1
  )
)
lib /nologo /OUT:"%BUILD%\zlib.lib" "%BUILD%\zlib\*.obj" >nul || exit /b 1

echo === compiling ocshell.exe ===
cl /nologo /c /O2 /MT /GS- /W3 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS ^
   /I"%ROOT%\src" /I"%ROOT%\third_party\zlib-1.3.1" ^
   "%ROOT%\src\ocshell.cpp" /Fo"%BUILD%\ocshell.obj" || exit /b 1
cl /nologo /c /O2 /MT /GS- /W3 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS ^
   /I"%ROOT%\src" /I"%ROOT%\third_party\zlib-1.3.1" ^
   "%ROOT%\src\payload.cpp" /Fo"%BUILD%\payload.obj" || exit /b 1
cl /nologo /c /O2 /MT /GS- /W3 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS ^
   /I"%ROOT%\src" /I"%ROOT%\third_party\zlib-1.3.1" ^
   "%ROOT%\src\ansi_console.cpp" /Fo"%BUILD%\ansi_console.obj" || exit /b 1
link /nologo /SUBSYSTEM:CONSOLE /ENTRY:wmainCRTStartup /OUT:"%BUILD%\ocshell.exe" ^
   "%BUILD%\ocshell.obj" "%BUILD%\payload.obj" "%BUILD%\ansi_console.obj" "%BUILD%\zlib.lib" ^
   kernel32.lib user32.lib shell32.lib advapi32.lib || exit /b 1

echo === import check ===
python "%ROOT%\tools\pe_imports.py" "%BUILD%\ocshell.exe"
echo build ok
endlocal
