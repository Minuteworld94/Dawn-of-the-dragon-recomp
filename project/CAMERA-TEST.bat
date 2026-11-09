@echo off
REM Draw-distance test view (WORK_LOG 127): boots straight into Avalar from a copy of the Avalar
REM save (project\run\tests\testuser_avalar -> project\run\testuser; your own saves are never
REM touched) and parks the camera 200 units from the bandit camp, looking at it.
REM Another view:  CAMERA-TEST.bat "eye x,y,z,look x,y,z"   e.g.  CAMERA-TEST.bat "10,40,40,0,25,-60"
setlocal
set "VIEW=%~1"
if "%VIEW%"=="" set "VIEW=10,55,140,0,25,-60"
set "RUN=%~dp0run"
if exist "%RUN%\testuser" rmdir /s /q "%RUN%\testuser"
xcopy /e /i /q /y "%RUN%\tests\testuser_avalar" "%RUN%\testuser" >nul
set SPYRO_AUTOBOOT=1
set SPYRO_CAMPULL=0.01,0
set SPYRO_CAMFIX=%VIEW%
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-game.ps1" -UserDir testuser
if errorlevel 1 (
    echo.
    echo Launch failed - see the message above.
    pause
)
