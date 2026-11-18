@echo off
REM Plays like PLAY.bat (your own saves), and shows a TEST achievement popup every 30 seconds
REM from 1 minute after start - nothing is unlocked or saved.
REM The popup's fire and whoosh come from the main menu's fire swoosh, recorded when the Game
REM Menu comes in - so go through the menus into your save first (WORK_LOG 146).
REM Log: project\run\logs\popup_<date>_<time>.log
setlocal
set SPYRO_ACHTEST=3,60,30
powershell -NoProfile -ExecutionPolicy Bypass -Command "& '%~dp0run-game.ps1' -Name ('popup_' + (Get-Date -Format 'yyyy-MM-dd_HHmmss'))"
if errorlevel 1 (
    echo.
    echo Launch failed - see the message above.
    pause
)
