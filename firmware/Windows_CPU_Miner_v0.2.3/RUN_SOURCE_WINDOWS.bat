@echo off
setlocal
cd /d "%~dp0"
py -3 CRSminer_PC.py
if errorlevel 9009 python CRSminer_PC.py
if errorlevel 1 (
 echo The app could not be launched. Check Python 3 installation.
 pause
)
