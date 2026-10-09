@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Clean-GeneratedFiles.ps1" %*
exit /b %errorlevel%
