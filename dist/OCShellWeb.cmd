@echo off
setlocal
rem ---------------------------------------------------------------------------
rem  Runs opencode as a web server so that 32-bit Windows 7 machines on the
rem  same LAN can use it in a browser. The runtime stays 64-bit; the client
rem  only renders the web interface.
rem
rem  Usage:  OCShellWeb.cmd [port]
rem ---------------------------------------------------------------------------
set PORT=8090
if not "%~1"=="" set PORT=%~1

if not exist "%~dp0portable\opencode.exe" (
  echo [!] portable\opencode.exe not found.
  echo     Run OCShellWin7.exe once to unpack the package.
  pause
  exit /b 1
)

echo.
echo   opencode web server
echo   Local:   http://localhost:%PORT%
echo   Network: http://%COMPUTERNAME%:%PORT%
echo.
echo   Open the network URL from a 32-bit Windows 7 machine.
echo   Press Ctrl+C to stop.
echo.

"%~dp0portable\opencode.exe" web --hostname 0.0.0.0 --port %PORT%
