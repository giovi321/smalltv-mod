@echo off
setlocal

REM Undo install.bat: remove the login autostart and stop a running instance.
REM The daemon files stay; delete the folder to remove them entirely.

echo Stopping any running smalltv usage daemon...
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"name='pythonw.exe' OR name='python.exe'\" | Where-Object { $_.CommandLine -match 'smalltv_usage_daemon' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }"

set "SHORTCUT=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\SmallTVUsageDaemon.lnk"
if exist "%SHORTCUT%" ( del "%SHORTCUT%" & echo Removed startup shortcut. ) else ( echo No startup shortcut found. )
if exist "%~dp0run_daemon.vbs" ( del "%~dp0run_daemon.vbs" & echo Removed run_daemon.vbs. )

echo.
echo Done - the smalltv daemon will no longer start at login.
endlocal
