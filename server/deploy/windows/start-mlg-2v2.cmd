@echo off
rem Starts an MLG 2v2 Team Slayer server on Windows (WINDOWS.md, "An MLG 2v2
rem server"): the MLG maps (playlists\mlg_2v2.txt) with the MLG Team Slayer
rem rules (admin\gametypes\mlg_team_slayer.toml: 50 kills, no clock, no
rem motion tracker, pistol and assault rifle starts, friendly fire on, no
rem vehicles), four players, the game waiting for all four. Set the lines
rem below and double-click it. Every setting is an environment variable
rem (docs/settings.md in ChupathingyCE's repository).
setlocal

rem the playlist, in the data folder
set HALO_DEDICATED=playlists/mlg_2v2.txt
rem the server's name in the game lists (15 characters at most)
set HALO_DEDICATED_NAME=MLG 2v2
rem a 2v2: four players, and the countdown waits for all four (2 starts
rem as soon as two are in, for warming up; a team game needs one a side)
set HALO_DEDICATED_MINIMUM_PLAYERS=4
set HALO_DEDICATED_MAXIMUM_PLAYERS=4
rem listed in the in-game Server Browser (false: only who has its invite,
rem for a private scrim; the invite link is printed when the server starts)
set HALO_DEDICATED_PUBLIC=true
rem the invite link stays the same each time the server starts, to give out
set HALO_NET_LASTING_INVITE=true
rem a game nobody scores in for this many minutes ends (0: never)
set HALO_DEDICATED_IDLE_LIMIT=5
rem the control panel and API: a port on this computer, off when empty
set HALO_DEDICATED_CONTROL=8080
rem the data folder: the one with maps\ (ui.map and the multiplayer maps),
rem playlists\ and admin\gametypes\; this script's own folder unless set
if not defined HALO_DATA_ROOT set HALO_DATA_ROOT=%~dp0.
if not defined HALO_SAVE_ROOT set HALO_SAVE_ROOT=%HALO_DATA_ROOT%\saves

cd /d "%~dp0"
"%~dp0chupathingyce-server.exe"
echo.
echo The server stopped (status %ERRORLEVEL%).
pause
