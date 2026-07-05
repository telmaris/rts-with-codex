@echo off
setlocal

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0measure_coverage.ps1" %*
set EXIT_CODE=%ERRORLEVEL%
echo.
echo Press any key to close...
pause >nul
exit /b %EXIT_CODE%
