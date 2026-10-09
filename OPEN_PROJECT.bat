@echo off
setlocal
set "GKCORE_SOLUTION=%~dp0build\runtime-windows\gkcore.slnx"
if not exist "%GKCORE_SOLUTION%" set "GKCORE_SOLUTION=%~dp0build\runtime-windows\gkcore.sln"
if not exist "%GKCORE_SOLUTION%" (
    echo Run PRE_SETUP.bat first to create the Visual Studio solution.
    pause
    exit /b 1
)
start "" "%GKCORE_SOLUTION%"
