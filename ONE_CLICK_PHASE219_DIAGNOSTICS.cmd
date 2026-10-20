@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>&1
if %ERRORLEVEL% EQU 0 (
 py -3 COLLECT_PHASE219_DIAGNOSTICS.py
) else (
 python COLLECT_PHASE219_DIAGNOSTICS.py
)
pause
