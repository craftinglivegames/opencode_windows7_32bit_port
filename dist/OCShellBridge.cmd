@echo off
setlocal
rem ---------------------------------------------------------------------------
rem  OCShell bridge agent - starts "opencode web" on demand for the 32-bit
rem  Windows 7 client. Listens on 0.0.0.0:8011 (see dist\bridge.ini).
rem
rem  Usage:  OCShellBridge.cmd [--port 8011]
rem ---------------------------------------------------------------------------
cd /d "%~dp0.."

set PY=python
where python >nul 2>&1
if errorlevel 1 (
  if exist "%~dp0..\python\python.exe" set "PY=%~dp0..\python\python.exe"
)

echo.
echo   OCShell bridge agent
echo   Status: http://localhost:8011/status
echo   Start:  POST http://localhost:8011/start?token=...
echo   Press Ctrl+C to stop the agent (opencode web keeps running).
echo.

"%PY%" "tools\bridge_agent.py" %*
set RC=%ERRORLEVEL%
if not "%RC%"=="0" (
  echo.
  echo [!] Bridge agent exited with code %RC%. See dist\bridge_agent.log.
  rem Do not block autostart with "pause".
)
exit /b %RC%
