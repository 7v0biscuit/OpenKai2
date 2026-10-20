@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>&1
if %ERRORLEVEL% EQU 0 (
 py -3 RUN_PHASE224.py --choose-game
 set "RC=%ERRORLEVEL%"
 py -3 COLLECT_PHASE224_DIAGNOSTICS.py
) else (
 python RUN_PHASE224.py --choose-game
 set "RC=%ERRORLEVEL%"
 python COLLECT_PHASE224_DIAGNOSTICS.py
)
echo.
echo Upload UPLOAD_PHASE224_RESULTS.zip and UPLOAD_PHASE224_DIAGNOSTICS.zip
pause
exit /b %RC%
