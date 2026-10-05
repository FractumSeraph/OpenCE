@echo off
rem Started by the "Halo Web Server" scheduled task (install-autostart.ps1).
rem Runs the server from this folder with its output in logs\halo-server.log.
cd /d "%~dp0..\.."
if not exist logs mkdir logs
rem keep the log small: start a new one past 10 MB
if exist logs\halo-server.log for %%F in (logs\halo-server.log) do if %%~zF GTR 10485760 move /y logs\halo-server.log logs\halo-server.old.log >nul
echo ===== %date% %time% starting >> logs\halo-server.log
"server\runtime\win32-x64\node.exe" server\server.mjs >> logs\halo-server.log 2>&1
