# The dedicated server on Windows

`chupathingyce-server.exe` is the same server as the Linux download (the
same code, network version, playlists, settings, console, control panel and
moderation), built for Windows 10 and 11. It is a console program: it runs in
a Command Prompt window, or as a scheduled task with no window.

| Download | For |
| --- | --- |
| `chupathingyce-server-windows-x64` | 64-bit Windows: almost every PC. Plays Xbox and Halo PC (Custom Edition, HaloMD) maps. |
| `chupathingyce-server-windows-x86` | 32-bit Windows, and the Xbox maps (it can load the others, with less room for their textures and sounds). |

Each holds `chupathingyce-server.exe`, `SDL3.dll` (which it needs beside it),
`start-server.cmd`, the `playlists` folder, `brokers.txt` and the licenses.

## Quick start

1. Unpack the download into a folder, for example `C:\HaloServer`.
2. Make a `maps` folder in it, and copy in the game's `ui.map` and the
   multiplayer maps (the North American Xbox disc's, as the players have).
   Halo PC maps go in folders beside `maps`: `maps_ce` (with Custom
   Edition's `bitmaps.map`, `sounds.map` and `loc.map`), `maps_md`,
   `maps_pc` ([Playlists](playlists.md)).
3. Open `start-server.cmd` in Notepad, set the playlist, the server's name
   and the rest, save it, and double-click it.

The window shows what the server does, and takes its commands (`help` lists
them; [Running a server](admin.md)). Ctrl+C, or closing the window, stops it:
it withdraws its game from the lists on the way out.

The first time, Windows Firewall asks whether `chupathingyce-server.exe` may
use the network. Allow it on private networks, and on public networks too if
this computer is on one, or players cannot reach the server.

On a home connection you don't need to forward any ports: players reach the
server as they reach anyone's invite link. On a cloud server, open one UDP
port, as on Linux (the README's "It shows, but nobody can join").

## Settings

Every setting is an environment variable, as on Linux
([Settings](settings.md)). `start-server.cmd` sets the common ones with
`set NAME=value` lines; add any other the same way. The data folder
(`HALO_DATA_ROOT`) is the script's folder unless set: the one with `maps`
and `playlists` in it. The server writes its settings file (`config.toml`)
beside the program, and its log (`debug.txt`) in the data folder.

## Sending maps

A player who joins without the game's Custom Edition map gets it from the
server (OpenCE's map sharing, network version 27 and later), as from the
Linux server: the map is named by its name, with the start of its hash as
its version (network version 29), and `HALO_NET_MAP_SHARE=false` turns it
off ([the README](../README.md)). The server never renames or
changes its map files.

## The control panel

`HALO_DEDICATED_CONTROL=8080` turns on the control API and the web control
panel on this computer: open `http://127.0.0.1:8080/`. The first start prints
the control API's token and the panel's setup code once
([Moderators and the control panel](moderation.md)). To reach the panel from
other computers, give an address and port (`0.0.0.0:8443`): it is then
HTTPS, with a certificate the server makes for itself, and the port must be
allowed through Windows Firewall.

## Running without a window

To start the server with Windows and keep it running without anyone signed
in, make a scheduled task (Task Scheduler, Create Task):

- General: "Run whether user is signed in or not".
- Triggers: "At startup".
- Actions: start `C:\HaloServer\chupathingyce-server.exe` in
  `C:\HaloServer`. A task has no `start-server.cmd` window, so put the
  settings in the task's environment instead: a small `.cmd` file with the
  `set` lines and the program's path as the action does it.
- Settings: untick "Stop the task if it runs longer than".

Its console is then off (no window to type in); use the control panel.

## What differs from Linux

- Ctrl+C, Ctrl+Break and closing the window stop the server, as SIGTERM and
  SIGINT do on Linux.
- Windows has no file modes beyond the owner's: the server does not warn
  about files others can read (on Linux it does for `control_credentials.txt`
  and `control_accounts.txt`); keep the data folder where only you and the
  server's account can read it.
- A crash is reported as the Windows game's are: the first time, a window
  asks whether to send crash reports to ChupathingyCE's developers (the
  answer is kept in `config.toml`, `crash_reports.upload`: set it to `"no"`
  beforehand for a server nobody watches, or `"yes"`); the reports wait in
  `crashes\` beside the program until they are sent.
- Building it: `python configure.py`, then `ninja server-windows64` (or
  `ninja server-windows`) on Windows, with the game's requirements
  ([port/windows/README.md](../../port/windows/README.md)). It is
  `build/server-windows64/chupathingyce-server.exe`.
