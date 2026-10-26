@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul || exit /b 1
cl /nologo /O2 /MT "%~dp0cpuid_test.c" /Fe:"%~dp0..\dist\cpuid.exe" >nul || exit /b 1
echo built
