@echo off
setlocal

REM Install the smalltv usage daemon as a Windows startup program (silent, tray icon).
REM Installs dependencies, writes a VBS wrapper for windowless launch, and drops a
REM shortcut in the Startup folder so it runs at every login.

set DAEMON_DIR=%~dp0
set VBS_FILE=%DAEMON_DIR%run_daemon.vbs
set STARTUP_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup
set SHORTCUT=%STARTUP_DIR%\SmallTVUsageDaemon.lnk

echo Installing Python dependencies...
py -m pip install -r "%DAEMON_DIR%requirements.txt" --quiet || pip install -r "%DAEMON_DIR%requirements.txt" --quiet

echo Creating silent launcher...
(
echo Set WshShell = CreateObject^("WScript.Shell"^)
echo WshShell.Run "pythonw """ ^& Replace^(WScript.ScriptFullName, "run_daemon.vbs", "smalltv_usage_daemon.py"^) ^& """ --tray", 0, False
) > "%VBS_FILE%"

echo Creating startup shortcut...
powershell -NoProfile -Command "$ws = New-Object -ComObject WScript.Shell; $sc = $ws.CreateShortcut('%SHORTCUT%'); $sc.TargetPath = '%VBS_FILE%'; $sc.WorkingDirectory = '%DAEMON_DIR%'; $sc.Description = 'smalltv usage daemon (tray)'; $sc.Save()"

echo.
echo Installation complete. The daemon starts automatically at next login (tray icon).
echo   Start it now:     start-daemon.bat
echo   Run in console:   py smalltv_usage_daemon.py --no-tray
echo   Stop it:          right-click the tray icon ^> Quit
echo   Uninstall:        uninstall.bat
