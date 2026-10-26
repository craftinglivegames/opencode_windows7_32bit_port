@echo off
REM ---------------------------------------------------------------------------
REM  Builds the Windows 7 compatibility shim DLLs (ocshim32.dll / ocn.dll)
REM  and produces a patched copy of the host executable.
REM ---------------------------------------------------------------------------
setlocal
set ROOT=%~dp0..
set VC="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist %VC% (
  echo ERROR: vcvarsall.bat not found
  exit /b 1
)
call %VC% x64 >nul || exit /b 1

set BUILD=%ROOT%\build
if not exist "%BUILD%" mkdir "%BUILD%"
set HOST_EXE=%1
if "%HOST_EXE%"=="" set HOST_EXE=C:\Users\build\AppData\Roaming\npm\node_modules\opencode-ai\bin\opencode.exe

echo === generating export definitions ===
python "%ROOT%\tools\shim_tool.py" gen_defs "%HOST_EXE%" "%BUILD%" || exit /b 1

echo === compiling ocshim32.dll ===
cl /nologo /c /O2 /MT /GS- /W3 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS ^
   "%ROOT%\src\ocshim32.cpp" /Fo"%BUILD%\ocshim32.obj" || exit /b 1
link /nologo /DLL /OUT:"%BUILD%\ocshim32.dll" /DEF:"%BUILD%\ocshim32.def" "%BUILD%\ocshim32.obj" kernel32.lib || exit /b 1

echo === compiling ocn.dll ===
lib /nologo /DEF:"%BUILD%\ntdll_stand_in.def" /MACHINE:X64 /OUT:"%BUILD%\ntdll_stand_in.lib" || exit /b 1
cl /nologo /c /O2 /MT /GS- /W3 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS ^
   "%ROOT%\src\ocn.cpp" /Fo"%BUILD%\ocn.obj" || exit /b 1
link /nologo /DLL /OUT:"%BUILD%\ocn.dll" /DEF:"%BUILD%\ocn.def" "%BUILD%\ocn.obj" ^
   "%BUILD%\ntdll_stand_in.lib" kernel32.lib || exit /b 1

echo === patching host executable ===
if not exist "%BUILD%\patched" mkdir "%BUILD%\patched"
python "%ROOT%\tools\shim_tool.py" patch "%HOST_EXE%" "%BUILD%\patched\opencode.exe" || exit /b 1
copy /Y "%BUILD%\ocshim32.dll" "%BUILD%\patched\" >nul
copy /Y "%BUILD%\ocn.dll" "%BUILD%\patched\" >nul

echo === import check of the shim DLLs ===
python "%ROOT%\tools\pe_imports.py" "%BUILD%\ocshim32.dll"
echo done
endlocal
