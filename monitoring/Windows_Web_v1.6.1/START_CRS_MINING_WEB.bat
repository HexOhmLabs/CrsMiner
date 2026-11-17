@echo off
setlocal
cd /d "%~dp0"
title CRS Mining Web - HexOhm
where py >nul 2>nul
if %errorlevel%==0 (
    py -3 "crs_bridge.py"
    goto :end
)
where python >nul 2>nul
if %errorlevel%==0 (
    python "crs_bridge.py"
    goto :end
)
echo.
echo Python 3 is required for local network scanning.
echo Download it from https://www.python.org/downloads/windows/
echo During installation select "Add python.exe to PATH".
echo Then double-click this file again.
:end
echo.
echo Server stopped. If an error was displayed, copy it here.
pause
