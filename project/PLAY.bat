@echo off
REM Double-click to play. Extra options can be passed through, e.g.:  PLAY.bat --vsync=false
setlocal
if "%~1"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-game.ps1"
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-game.ps1" -Extra %*
)
if errorlevel 1 (
    echo.
    echo Launch failed - see the message above.
    pause
)
