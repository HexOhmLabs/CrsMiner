@echo off
setlocal
cd /d "%~dp0"
echo.
echo ============================================
echo   CRS Mining - Gradle wrapper setup
 echo ============================================
echo.
echo Downloading a verified Gradle wrapper from the official Gradle GitHub repository.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\prepare-gradle.ps1"
if errorlevel 1 (
    echo.
    echo PREPARATION FAILED. Review the complete error above.
    pause
    exit /b 1
)
echo.
echo Preparation successful. Open this project in Android Studio.
echo.
pause
exit /b 0
