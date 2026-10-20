@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>&1
if %ERRORLEVEL% EQU 0 (
 py -3 RUN_PHASE222.py --choose-game
 set "RC=%ERRORLEVEL%"
 py -3 COLLECT_PHASE222_DIAGNOSTICS.py
) else (
 python RUN_PHASE222.py --choose-game
 set "RC=%ERRORLEVEL%"
 python COLLECT_PHASE222_DIAGNOSTICS.py
)
echo.
echo Upload UPLOAD_PHASE222_RESULTS.zip and UPLOAD_PHASE222_DIAGNOSTICS.zip
pause
exit /b %RC%
