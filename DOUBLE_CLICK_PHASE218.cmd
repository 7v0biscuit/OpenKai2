@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>&1
if %ERRORLEVEL% EQU 0 (
 py -3 RUN_PHASE218.py --choose-game
 set "RC=%ERRORLEVEL%"
 py -3 COLLECT_PHASE218_DIAGNOSTICS.py
) else (
 python RUN_PHASE218.py --choose-game
 set "RC=%ERRORLEVEL%"
 python COLLECT_PHASE218_DIAGNOSTICS.py
)
echo.
echo Upload UPLOAD_PHASE218_RESULTS.zip and UPLOAD_PHASE218_DIAGNOSTICS.zip
pause
exit /b %RC%
