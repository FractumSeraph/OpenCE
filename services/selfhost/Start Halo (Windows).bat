@echo off
rem Starts the Halo web server from this folder. Close this window to stop it.
title Halo server
cd /d "%~dp0"
set "HALO_NODE=server\runtime\win32-x64\node.exe"
if not exist "%HALO_NODE%" set "HALO_NODE=node"
"%HALO_NODE%" server\server.mjs
echo.
pause
