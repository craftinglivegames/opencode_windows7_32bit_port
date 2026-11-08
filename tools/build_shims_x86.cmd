@echo off
REM ---------------------------------------------------------------------------
REM  Builds the x86 (Win7 x32) compatibility shims for Node.js 22 and patches
REM  node.exe to load them instead of KERNEL32/ADVAPI32.
REM
REM  Usage:
REM    tools\build_shims_x86.cmd [path\to\node.exe]
REM
REM  Default input:  build\node-x86\node.exe
REM  Output:         build\nodeshim_x86\
REM                  node.exe + ocshim32.dll + ocadv32.dll
REM ---------------------------------------------------------------------------
setlocal
set ROOT=%~dp0..
set NODE=%1
if "%NODE%"=="" set NODE=%ROOT%\build\node-x86\node.exe
set OUT=%ROOT%\build\nodeshim_x86
set VC="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"

if not exist "%NODE%" (
  echo ERROR: node.exe not found: %NODE%
  echo Download node-v22.23.4-win-x86.zip, extract node.exe and pass its path:
  echo   tools\build_shims_x86.cmd C:\path\to\node.exe
  exit /b 1
)
if not exist %VC% (
  echo ERROR: vcvarsall.bat not found: %VC%
  exit /b 1
)

call %VC% x86 >nul || exit /b 1
if not exist "%OUT%" mkdir "%OUT%"

echo === generating export definitions from node.exe imports ===
python "%ROOT%\tools\shim_tool.py" gen_defs "%NODE%" "%OUT%" || exit /b 1

echo === compiling ocshim32.dll (x86) ===
cl /nologo /c /O2 /MT /GS- /W3 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS ^
   "%ROOT%\src\ocshim32.cpp" /Fo"%OUT%\ocshim32.obj" || exit /b 1
link /nologo /DLL /OUT:"%OUT%\ocshim32.dll" /DEF:"%OUT%\ocshim32.def" ^
   "%OUT%\ocshim32.obj" kernel32.lib || exit /b 1

echo === compiling ocadv32.dll (x86) ===
cl /nologo /c /O2 /MT /GS- /W3 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS ^
   "%ROOT%\src\ocadv32.cpp" /Fo"%OUT%\ocadv32.obj" || exit /b 1
link /nologo /DLL /OUT:"%OUT%\ocadv32.dll" /DEF:"%ROOT%\build\ocadv32.def" ^
   "%OUT%\ocadv32.obj" advapi32.lib || exit /b 1

echo === patching node.exe (KERNEL32 -> ocshim32.dll, ADVAPI32 -> ocadv32.dll) ===
python "%ROOT%\tools\shim_tool.py" patch "%NODE%" "%OUT%\node.exe" || exit /b 1
copy /Y "%OUT%\ocshim32.dll" "%OUT%\" >nul
copy /Y "%OUT%\ocadv32.dll" "%OUT%\" >nul

echo === verification ===
python "%ROOT%\tools\pe_imports.py" "%OUT%\ocshim32.dll"
for %%F in ("%OUT%\node.exe" "%OUT%\ocshim32.dll" "%OUT%\ocadv32.dll") do echo %%~nxF: %%~zF bytes

echo done: %OUT%
endlocal
