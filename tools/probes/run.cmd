@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0..\.."
where py >nul 2>&1
if %ERRORLEVEL% EQU 0 (
 py -3 tools/probes/run.py --choose-game
 set "RC=!ERRORLEVEL!"
 py -3 tools/probes/collect.py
) else (
 python tools/probes/run.py --choose-game
 set "RC=!ERRORLEVEL!"
 python tools/probes/collect.py
)
echo.
echo Reports are available in reports\probes.
pause
exit /b %RC%
