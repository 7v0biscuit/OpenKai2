@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>&1
if %ERRORLEVEL% EQU 0 (
 py -3 RUN_PHASE223.py --choose-game
 set "RC=%ERRORLEVEL%"
 py -3 COLLECT_PHASE223_DIAGNOSTICS.py
) else (
 python RUN_PHASE223.py --choose-game
 set "RC=%ERRORLEVEL%"
 python COLLECT_PHASE223_DIAGNOSTICS.py
)
echo.
echo Upload UPLOAD_PHASE223_RESULTS.zip and UPLOAD_PHASE223_DIAGNOSTICS.zip
pause
exit /b %RC%
