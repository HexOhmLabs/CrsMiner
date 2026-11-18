@echo off
setlocal
cd /d "%~dp0"
set "PYCMD=py -3"
py -3 --version >nul 2>&1
if errorlevel 1 set "PYCMD=python"
%PYCMD% --version >nul 2>&1
if errorlevel 1 (
 echo ERROR: Python 3.11+ is required to run the standalone benchmark.
 pause
 exit /b 1
)
%PYCMD% bench.py --seconds 5
echo.
echo Benchmark is offline: no pool login, no wallet, no LAN requests.
pause
