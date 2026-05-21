@echo off
setlocal
cd /d "%~dp0"
pwsh -NoProfile -ExecutionPolicy Bypass -File ".\scripts\dev\test-static.ps1"
set EXIT_CODE=%ERRORLEVEL%
pause
exit /b %EXIT_CODE%
