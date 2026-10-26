@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul || exit /b 1
cd /d "%~dp0.."
cl /nologo /c /O2 /MT /GS- /W3 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS src\ocshim32.cpp /Fo:build\ocshim32.obj || exit /b 1
link /nologo /DLL /OUT:build\ocshim32.dll /DEF:build\ocshim32.def build\ocshim32.obj kernel32.lib || exit /b 1
copy /Y build\ocshim32.dll build\payload\ocshim32.dll >nul
echo shim rebuilt
