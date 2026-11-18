@echo off
setlocal
set "DIAGDIR=%LOCALAPPDATA%\HexOhm\CRSminerPC\diagnostics"
if not exist "%DIAGDIR%" mkdir "%DIAGDIR%"
start "" explorer.exe "%DIAGDIR%"
