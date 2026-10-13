@echo off
rem Starts the ChupathingyCE Dedicated Server on Windows (WINDOWS.md).
rem Copy this file, set the lines below, and double-click it. Every setting
rem is an environment variable (docs/settings.md in ChupathingyCE's repository).
setlocal

rem the playlist, in the data folder (playlists\ holds the ones included)
set HALO_DEDICATED=playlists/slayer.txt
rem the server's name in the game lists (15 characters at most)
set HALO_DEDICATED_NAME=My Server
rem players a game waits for, and takes
set HALO_DEDICATED_MINIMUM_PLAYERS=1
set HALO_DEDICATED_MAXIMUM_PLAYERS=12
rem listed in the in-game Server Browser (false: only who has its invite)
set HALO_DEDICATED_PUBLIC=true
rem the control panel and API: a port on this computer, off when empty
set HALO_DEDICATED_CONTROL=8080

rem the data folder: the one with maps\ (ui.map and the multiplayer maps)
rem and playlists\; this script's own folder unless set
if not defined HALO_DATA_ROOT set HALO_DATA_ROOT=%~dp0.
if not defined HALO_SAVE_ROOT set HALO_SAVE_ROOT=%HALO_DATA_ROOT%\saves

cd /d "%~dp0"
"%~dp0chupathingyce-server.exe"
echo.
echo The server stopped (status %ERRORLEVEL%).
pause
