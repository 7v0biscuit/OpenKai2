@echo off
setlocal
cd /d "%~dp0..\.."
where py >nul 2>&1
if %ERRORLEVEL% EQU 0 (
 py -3 tools/probes/collect.py
) else (
 python tools/probes/collect.py
)
pause
