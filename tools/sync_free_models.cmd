@echo off
REM ---------------------------------------------------------------------------
REM  Syncs free-model entries in opencode.json from the models.dev catalog.
REM  Place this file and sync_free_models.js next to each other, e.g. in
REM  C:\OpenCode\, then just run it. Uses the bundled Node (Win7-compatible).
REM
REM  Optional args: provider ids to sync (default: all catalog-backed ones),
REM  e.g.  sync_free_models.cmd opencode-go openrouter
REM ---------------------------------------------------------------------------
setlocal
set "DIR=%~dp0"
set "NODE=%DIR%node\node.exe"
if not exist "%NODE%" (
  echo ERROR: bundled node not found: %NODE%
  echo Put sync_free_models.cmd and sync_free_models.js into your OpenCode
  echo folder (the one with node\ and app\, e.g. C:\OpenCode) and run again.
  exit /b 1
)
set "NODE_SKIP_PLATFORM_CHECK=1"
"%NODE%" "%DIR%sync_free_models.js" %*
if errorlevel 1 (
  echo FAILED - config unchanged except backup, see messages above.
  exit /b 1
)
echo.
echo Done. Restart the OpenCode server (run OpenCode.cmd again) to apply.
endlocal
