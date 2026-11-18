@echo off
setlocal
cd /d "%~dp0"

echo ===========================================================
echo  HexOhm CRSminer PC - Windows EXE Builder
 echo  SHA-256d CPU miner - v0.2.3
 echo ===========================================================

set "PYCMD=py -3"
py -3 --version >nul 2>&1
if errorlevel 1 set "PYCMD=python"
%PYCMD% --version >nul 2>&1
if errorlevel 1 (
 echo Python 3 was not found. Install Python 3.11+ from python.org
 echo Enable Add python.exe to PATH if using the python command.
 pause
 exit /b 1
)

if not exist .buildvenv\Scripts\python.exe (
 echo [1/4] Creating an isolated build environment...
 %PYCMD% -m venv .buildvenv
 if errorlevel 1 goto :failed
)

 echo [2/4] Installing the build tool...
.buildvenv\Scripts\python.exe -m pip install --disable-pip-version-check "pyinstaller>=6.10,<7"
if errorlevel 1 goto :failed

.buildvenv\Scripts\python.exe -c "import struct; assert struct.calcsize('P') == 8, 'The native miner requires 64-bit Python.'"
if errorlevel 1 goto :failed

 echo [3/4] Running the offline SHA-256d and mock-pool tests...
.buildvenv\Scripts\python.exe -m unittest discover -s tests -v
if errorlevel 1 goto :failed

 if not exist "native\crs_sha256d_win64.dll" (
 echo ERROR: Native engine DLL missing; build cannot continue.
 goto :failed
)

echo [3b] Quick offline one-worker benchmark (no pool)...
.buildvenv\Scripts\python.exe bench.py --seconds 1 --workers 1
if errorlevel 1 goto :failed

echo [4/4] Building the single Windows executable...
.buildvenv\Scripts\python.exe -m PyInstaller --noconfirm --clean --onefile --windowed ^
 --name HexOhm_CRSminer_PC_v0.2.3 ^
 --icon "assets\crsminer.ico" ^
 --add-data "assets;assets" --add-binary "native\crs_sha256d_win64.dll;native" ^
 CRSminer_PC.py
if errorlevel 1 goto :failed

 echo.
 echo SUCCESS: dist\HexOhm_CRSminer_PC_v0.2.3.exe
 echo Copy this ONE exe to another Windows PC. Python is not needed there.
 echo No administrator permissions are needed to run the miner.
 echo.
pause
exit /b 0

:failed
 echo.
 echo The build or a test failed. An EXE has not been approved.
 echo Review the error messages above.
pause
exit /b 1
