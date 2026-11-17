@echo off
setlocal
cd /d "%~dp0"
echo.
echo ============================================
echo   CRSminer Android - Gradle setup v3 HOME fix
echo ============================================
echo.
echo Preparing the official Gradle 8.9 wrapper (SHA-256 verified).
echo Corrects the PowerShell HOME issue and avoids Java 25.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\prepare-gradle.ps1"
if errorlevel 1 (
    echo.
    echo SETUP FAILED. Copy the complete error above.
    pause
    exit /b 1
)
echo.
echo Wrapper ready. In Android Studio select Gradle JDK 17 or 21,
echo then sync and build CRSminer for Android.
pause
exit /b 0
