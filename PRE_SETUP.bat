@echo off
setlocal
set "GKCORE_ROOT=%~dp0"
where py >nul 2>nul
if errorlevel 1 (
    where python >nul 2>nul
    if errorlevel 1 (
        echo Python 3.9 or newer is required. Install Python and enable it on PATH.
        exit /b 1
    )
    python "%GKCORE_ROOT%tools\setup.py" %*
) else (
    py -3 "%GKCORE_ROOT%tools\setup.py" %*
)
if errorlevel 1 exit /b %errorlevel%
exit /b 0
