# Linux

`ninja linux` compiles the game with clang for 32-bit x86 Linux. The result
is a native executable, `build/linux/halo`. The game shows its graphics with
OpenGL 4.5. It plays sound through SDL3. It accepts keyboard, mouse and
gamepad input.

The game is 32-bit code because its data (tags, cache files, saved games)
contains 32-bit pointers, as on the Xbox.

## Requirements

You do not need the Xbox SDK. The declarations that the game uses are in
`port/include/xdk`.

To build:

- Python and ninja (`python3` and `ninja-build` on Debian and Ubuntu,
  `python` and `ninja` on Arch Linux).
- clang and lld. A release build is optimised with the profiles in `pgo/`
  only with clang 22, the version that wrote them; with an older clang it
  is built without them. Debian and Ubuntu get clang 22 from
  [apt.llvm.org](https://apt.llvm.org) (`clang-22`, `lld-22`, `llvm-22`);
  put `/usr/lib/llvm-22/bin` first on `PATH`, or give `configure.py` the
  compiler with `--linux-cc clang-22`. Arch Linux has it as `clang` and
  `lld`. The option `--linux-cc` of `configure.py` selects a different
  compiler.
- The 32-bit glibc development files: `gcc-multilib` and `libc6-dev-i386`
  on Debian and Ubuntu, `lib32-glibc` on Arch Linux.
- The 32-bit SDL3: `libsdl3-dev:i386` on Debian and Ubuntu, `lib32-sdl3` on
  Arch Linux. The portable build does not need it: refer to "Portable
  build".

To start the game:

- glibc 2.31 or later (32-bit), for the builds from GitHub Actions, which
  are built against glibc 2.31. They should start on Debian 11, Ubuntu
  20.04, Fedora 32 and later distributions. A build that you
  make without `--portable` needs the glibc of the computer that built it,
  or a later one.
- The 32-bit OpenGL libraries: `libgl1:i386` and `libgl1-mesa-dri:i386`
  on Debian and Ubuntu, `lib32-mesa` on Arch Linux.
- The 32-bit PipeWire or PulseAudio client libraries:
  `libpipewire-0.3-0:i386` or `libpulse0:i386` on Debian and Ubuntu,
  `lib32-pipewire` or `lib32-libpulse` on Arch Linux.
- The 32-bit X11 libraries: `libx11-6:i386` and `libxext6:i386` on Debian
  and Ubuntu, `lib32-libx11` and `lib32-libxext` on Arch Linux. In a
  Wayland session, the game uses them through XWayland. SDL uses Wayland
  itself only if the 32-bit Wayland libraries are version 1.20 or later
  (`libwayland-client0:i386`, `libwayland-cursor0:i386`,
  `libwayland-egl1:i386` and `libxkbcommon0:i386` on Debian and Ubuntu,
  `lib32-wayland` and `lib32-libxkbcommon` on Arch Linux; Debian 11,
  Ubuntu 20.04 and Fedora 32 have older ones) and the compositor has the
  fifo-v1 protocol. On GNOME, the window then has borders only with the
  32-bit libdecor (`libdecor-0-0:i386` on Debian and Ubuntu,
  `lib32-libdecor` on Arch Linux).

On Debian and Ubuntu, the 32-bit packages need the i386 architecture
first: `sudo dpkg --add-architecture i386 && sudo apt update`. The builds
from GitHub Actions need no SDL3 package: they bring their own
(`libSDL3.so.0`, next to the executable).

### Portable build

The builds from GitHub Actions are portable builds (`--portable`; refer to
"Build options" in the main [README](../../README.md)). The portable build
starts on more systems than the build machine's:

- It is compiled and linked against the glibc 2.31 of Debian 11, the glibc
  of the Steam Runtime 3 ("sniper"), not against the glibc of the build
  machine. An executable needs the glibc version that it was built against,
  or a later one.
- It brings SDL 3 (`libSDL3.so.0`), built from source against the same
  glibc. The executable looks for it in its own folder first (a `DT_RPATH`
  of `$ORIGIN`, which comes before `LD_LIBRARY_PATH`), so that it uses this
  SDL even when Steam sets `LD_LIBRARY_PATH`. Few distributions have a
  32-bit SDL 3. This SDL loads X11, Wayland, libdecor, PipeWire, PulseAudio
  and ALSA when it starts, so it starts with whichever of them the system
  has. It is linked only to glibc. Its license is `SDL3-LICENSE.txt`.

The 64-bit build (`ninja linux64`, below) is portable the same way, with
Debian 11's 64-bit (amd64) packages.

At the first build, `tools/linux_sysroot.py` downloads the Debian packages
(about 25 MB) and the source of SDL. Each package comes from
`archive.debian.org` or `deb.debian.org`, or else from
`snapshot.debian.org`; the source of SDL comes from GitHub or from
`libsdl.org`. It checks each download against its SHA-256 sum. The system
root is in `build/linux/third_party/sysroot` (`build/linux64/third_party/sysroot`
for the 64-bit build). `tools/linux_sysroot.cmake` (`tools/linux64_sysroot.cmake`)
builds SDL against it.

To make the portable build you need, in addition to the tools above,
CMake, `pkg-config` and `wayland-scanner`: `cmake`, `pkg-config` and
`libwayland-bin` on Debian and Ubuntu, `cmake`, `pkgconf` and `wayland` on
Arch Linux. You do not need the
system's SDL3. You need the 32-bit glibc only for the tests. Keep the
checkout in a folder whose path has no spaces: SDL's build takes the system
root's include folders from `pkg-config`, which splits a path at its spaces,
so the portable build stops with a message there.

To see the glibc version that a build needs, enter
`readelf -V build/linux/halo | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1`.
`tools/linux_glibc_check.py`, which the GitHub Actions builds run on every
Linux executable and library they make, fails on any version later than
2.31.

## Build the game

1. Install the tools. On Debian and Ubuntu, after adding apt.llvm.org's
   `llvm-toolchain-<release>-22` repository (its instructions are at
   [apt.llvm.org](https://apt.llvm.org); its script does it with
   `sudo ./llvm.sh 22`), enter:

   ```sh
   sudo apt install clang-22 lld-22 llvm-22 gcc-multilib libc6-dev-i386 \
     python3 ninja-build cmake pkg-config libwayland-bin
   export PATH=/usr/lib/llvm-22/bin:$PATH
   ```

   On Arch Linux, enable the multilib repository and enter
   `sudo pacman -S clang lld lib32-glibc python ninja cmake pkgconf wayland`.
   GitHub Actions builds on Ubuntu 24.04 this way
   (`.github/workflows/build.yml`).
2. Go to the root folder of the repository.
3. Enter `python3 configure.py` (with `--portable` for a build that
   starts on other computers, as the releases are: "Portable build").
4. Enter `ninja linux`.

### 64-bit

`ninja linux64` builds the same game as native x86-64 code,
`build/linux64/halo`, the way the macOS build does (the 64-bit build in
[port/macos/README.md](../macos/README.md): `HALO_64BIT` and the `long`
rewrite, `tools/lp64_build.py`). It needs the 64-bit glibc and SDL3
development files (`libsdl3-dev` on Debian 13 and Ubuntu 25.04 or later,
`sdl3` on Arch Linux) instead of the 32-bit ones, and the 64-bit OpenGL and
sound libraries to start. An SDL3 built by hand is found through
`LIBRARY_PATH` when linking and `LD_LIBRARY_PATH` when starting. With
`--portable`, as the releases are built, it needs neither: it is built
against Debian 11's 64-bit glibc 2.31 and brings its own SDL3, as the 32-bit
portable build does (refer to "Portable build"). It is not
optimised with a profile (the committed profiles are the 32-bit build's).
Its releases are their own download, `chupathingyce-linux64-release.zip`,
which its self-updater asks for. It plays Halo PC's maps (`maps_ce/`,
`maps_md/`, `maps_pc/`) and plays with the 32-bit builds and the other ports over the
network, and it is a dedicated server too (`server/README.md`).

## Start the game

Enter `build/linux/halo`.

The Steam Deck is not a supported platform yet. The Linux build may run
on it: unpack `chupathingyce-linux64-release.zip` into a folder (in Desktop
Mode), and add `halo` to Steam as a non-Steam game ("Add a Game" in the
Games menu of Steam). Reports are welcome in #bug-reports on the Discord.

The game data is the folder that contains `maps/`, from an Xbox disc image
of any version of the game. The game looks for this folder in this
sequence:

1. `paths.data` in `config.toml`.
2. The current folder.
3. The folder of the executable.
4. `assets/` in the current folder, and `assets/` in the repository that
   contains the executable.

If the game finds no data (or finds an empty `maps/`), it asks where to
get it. This occurs at the first start:

- "Xbox disc image" (the default) opens a file picker. Select an Xbox disc
  image (`.xiso` or `.iso`). The game copies `maps/` next to the executable
  and shows the progress. It also copies the disc's Bink movies (`bink/`,
  the `.bik` files), only the ones `bink/` lacks.
- "Halo PC or Custom Edition" opens the same picker for optional extras: a
  Halo PC CD image (`.iso`) fills `maps_pc/` (pc_disc.c), and Custom
  Edition's installer (`halocesetup_en_1.00.exe`) fills `maps_ce/`
  (ce_installer.c). Both read the Microsoft cabinets the maps are packed in
  (cabinet.c). Only maps the folder lacks are written. The game still needs
  the Xbox maps, so it asks again afterward.
- "Make the folders" makes empty `maps/`, `maps_ce/`, `maps_pc/` and
  `maps_md/`, says what goes in each, and checks again when you say so.
- "Quit" stops the game.

Either picker finds out what a file is from its contents, not its name.

The game writes the copy to `maps.partial`. When the copy is complete, the
game changes the name to `maps`. If the copy stops before it is complete,
the game asks for the disc image again at the next start. The movies follow
the maps, each written as `<name>.partial` first. A movie that is already in
`bink/` is never replaced, and a movie that can't be copied does not stop the
extraction.

### Steam Deck

Do the set-up in Desktop Mode: unpack the release, put `maps/` next to
`halo` (or start the game one time to copy it from a disc image), and add
`halo` to Steam. Then start the game from the library in Game Mode.

- Screen: the game fills the screen without borders (`display.mode` empty,
  thus borderless). The 3D view and the HUD have the shape of the screen
  (16:10) and are drawn at its resolution; the menus are at the center.
- Controls: Steam Input gives the game a virtual controller, which the game
  operates as the controller of the Xbox. With the template that Steam selects
  ("Gamepad With Joystick Trackpad"), the buttons, sticks and triggers have
  the functions of the same controls on an Xbox controller, the right
  trackpad operates as the right stick, and the back buttons (L4, L5, R4,
  R5) do nothing until you assign them in the controller settings of the
  game in Steam. With a template that makes a trackpad a mouse, the mouse
  aims in the game and moves the pointer in the menus.
- Text: names of profiles and gametypes use the keyboard of the game, which
  the controller operates. The text fields of the menus (the name and the
  password in Server Setup, and the password of a game in the server
  browser) open the keyboard of Steam. Type the text, then select Enter on
  that keyboard.
- Frame rate: the game shows one frame for each refresh of the display. It
  follows the refresh rate and the frame limit of Quick Access >
  Performance (40 to 60 Hz on the LCD model, up to 90 Hz on the OLED model).
  The world is calculated at 30 Hz at all rates. Keep `display.vsync =
  true`. Refer to "Frame rate".
- Sleep: the clocks of the game do not count the time that the Deck sleeps,
  so the game continues from where it stopped. A network game does not
  wait: the host drops a machine that it has not heard from for 15 seconds,
  and while a Deck that hosts sleeps, the other players have no host.

## Files and folders

| Xbox drive | Folder |
| --- | --- |
| `d:\` | The data root: the folder that contains `maps/`. |
| `z:\` | `z/` in the save root. This folder contains the cache (approximately 800 MB of map data) and the saved games. |
| `u:\` | `u/` in the save root. This folder contains the user data. |

The save root is `paths.saves` in `config.toml`. If that setting is empty,
the save root is `$XDG_DATA_HOME/fractum-halo` (usually
`~/.local/share/fractum-halo`). The first time the game starts with that
save root, it copies the saves from the folder earlier versions used
(`$XDG_DATA_HOME/halo-linux` or `~/.local/share/halo-linux`), without the
map cache, which it rebuilds. That folder is left as it was. The file
`shared_saves_checked.txt` in the save root records that the copy was done;
delete it to copy again. A save root set in `paths.saves` or
`HALO_SAVE_ROOT` is never copied into.

The game makes the folders when it needs them. Names of files and folders
are not case-sensitive, as on the Xbox.

Beside the maps and the saves, the game keeps folders of its own
(`port/linux/src/port_folders.h`), the same on every platform:

| Folder | Contents |
| --- | --- |
| `logs/` in the data root | The logs: `debug.txt`, `halo.log` (Windows) and `crashes/`. Zip this folder for a bug report. `paths.logs` (`HALO_LOG_ROOT`) puts it elsewhere. |
| `data/` in the data root | What a player adds that is not a map: `data/fonts/` holds the player's own fonts (`display.high_res_font`). |
| `cache/` in the save root | What the game can make again; safe to delete. `paths.cache` (`HALO_CACHE_ROOT`) puts it elsewhere. |
| `private/` in the save root | What is the player's alone. |

Until 0.8.0, `debug.txt` and `crashes/` were in the data root itself. The
game does not move or delete them: crash reports waiting there are still
sent, and an old `debug.txt` there is left as it was. An update never
touches these folders.

These files are in the data root:

| File | Contents |
| --- | --- |
| `logs/debug.txt` | The log of the game. At start-up, the game shows the logs folder and the data root in the terminal. A crash writes its report (the faulting address and the calls that led to it) here as well; the `reference address` line at the top of each session places those addresses in the build. |
| `logs/crashes/` | Crash reports that wait to be sent (Linux and macOS; refer to "Crash reports"). |
| `init.txt` | Console commands that the game does at start-up. For example, `map_name levels\a10\a10` starts the first campaign level. |
| `tags/` | Sound tag files that replace the sounds of the maps (`audio.loose_sounds`). |

The settings are in `config.toml` next to the executable. Refer to
"Settings". Internet play's MQTT brokers are in `brokers.txt` next to it
(`network.brokers_file`).

If the game stops because of a fatal signal, it writes the address and a
backtrace to the standard error. To find the function at the address, enter
`addr2line -e build/linux/halo <address>`.

### Crash reports

Releases and nightlies on Linux and macOS also write a crash report when
the game stops because of `SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE` or
`SIGABRT` (`port/linux/src/posix_crash.c`). The report goes to `crashes/`
in the logs folder (`logs/crashes/`; reports from before 0.8.0 wait in
`crashes/` in the data root and are sent from there). When the game starts
the next time, it does what
`crash_reports.upload` says: `"yes"` sends the reports to the ChupathingyCE
site (`network.browser_url`, `POST /v1/crash`) in the background, `"no"`
deletes them, and `"ask"` asks the player first. `crashes/` keeps at most 8
reports.

A report has the same format as on Windows
(`port/linux/src/crash_report.h`), without a minidump: the build, the
system, the signal, up to 32 calls (each a module and an offset in it, for
example `halo+0x2715b4`), and the last 200 lines of `debug.txt` from before
the crash, with each public IP address replaced with `[address]`. To find
the function of a call on macOS, enter `atos -o halo -l 0x100000000
0x1002715b4` (the offset plus `0x100000000`); on Linux, enter
`addr2line -e halo 0x2715b4`.

Local builds send no crash reports. To test the crash reports with such a
build, set the `HALO_CRASH_REPORTS_ANY_BUILD` environment variable. Android
and the dedicated server have no crash reports.

## Controls

The keyboard and the mouse are a control scheme of their own for the player
of controller 1: each action has up to two keys or mouse buttons, which
Settings > Controls Setup (or `[controls]` in `config.toml`) changes. The
game adds the input of the first gamepad to controller 1. The other
gamepads operate controllers 2 to 4. With two or more players on this
computer (co-op, or split screen in a network game) and only one gamepad,
that gamepad is controller 2 (player 2) and the keyboard and mouse stay
controller 1. The gamepad changes controller only when none of its buttons
is held. The profile's button layout (Settings > Gamepads) is the
gamepads' only.

| Action | Keys and buttons (default) |
| --- | --- |
| move | W, A, S, D |
| aim | mouse (direct aim) |
| fire | left mouse button |
| throw a grenade | right mouse button, G |
| jump (and skip a cutscene) | space |
| crouch | left ctrl, C (C alone in the browser builds, where Ctrl+W closes the tab) |
| melee | F, mouse button 4 |
| reload | R |
| action (pick up, hold to swap weapons, enter or leave a vehicle; never reloads) | E |
| change the weapon | mouse wheel, 1 |
| change the grenade | X |
| flashlight | Q |
| zoom | Z, middle mouse button |
| show the scores (hold) | tab |
| pause menu | escape |
| screenshot | F10 |
| talk in voice chat (hold) | V |
| say a line to everyone (text chat) | T |
| say a line to your team | Y |
| the chat's recent lines and players (M mutes one for you) | H |

Always: \` opens the developer console, F12 releases or captures the mouse,
F11 changes between fullscreen and window. Screenshot (default F10,
rebindable under Controls Setup > Actions, below Pause Menu; not on Android or in the browser) saves a PNG of the completed
frame to `screenshots/` beside `maps/`, named `YYYY-MM-DD_HH.MM.SS.png` in
local time, and prints the filename in the console. Captures in the same
second get a numeric suffix so previous screenshots are preserved.

One movement of the mouse wheel changes the weapon one time. A second
movement after a short pause changes it again.

In the menus, the mouse moves a pointer:

- The item below the pointer gets the focus.
- A left click selects the item. On a setting with values, a click on the
  left or right half changes the value. On a button in the key of a screen
  (for example "B = Back"), a click pushes that button.
- On the on-screen keyboard (a profile's name), a left click presses the
  key below the pointer, or pushes the "B =BACK" or "A =ENTER" legend.
- A right click goes back.
- The mouse wheel moves through the items.

The keyboard also operates the menus, with keys of its own: the arrow keys
(and W, A, S, D) move, space or enter selects, escape or backspace goes
back (escape resumes the game from the pause menu), delete deletes. When the game continues, the mouse aims again. A mouse button that you hold from the menu does not fire until
you push it again.

## Menus

The menus are the PC version's (Halo Custom Edition's): its main menu and
every screen it leads to (campaign, profiles, multiplayer with its server
browser, direct IP and server setup, the gametype editor, and the profile's
settings: controls, gamepads, mouse, audio, video, network and colour),
laid out as it has them. Their pictures are the high-res redraws; the few
that are not yet (3D renders, screenshots) are drawn from the Xbox's own
menus where it has the same, else a placeholder, listed in
`port/assets/menus/NON_HANDDRAWN.md`.

The campaign is wired: Campaign, on player 1's profile (the one last used),
continues its saved game, starts a level it has reached at a difficulty (New
Game), or loads or deletes any profile's saved game (Load Game). Many of the
other functions behind the screens are not wired to this game yet
(`port/assets/menus/UNWIRED.md`): the lists they fill (profiles, maps,
servers, key bindings) are empty, and the settings they change do not
change. The screens open, close and move between each other as the PC
version's. ChupathingyCE starts on the Xbox's menus (`display.menus = "xbox"`),
which start every kind of game and have Online Games; `display.menus =
"gearbox"` (or the older `"pc"`) gives these, Gearbox's Halo PC menus.

The map lists (New Game's and Create Game's Map screen) choose, in their
first row, SINGLEPLAYER (the campaign's levels), MULTIPLAYER (the Xbox's
maps), CUSTOM SINGLEPLAYER or CUSTOM MULTIPLAYER, as OpenCE has them. CUSTOM
MULTIPLAYER lists the Halo PC maps (`maps_ce`, `maps_md`, `maps_pc`: those
the Xbox's menus list after the Xbox maps, marked [CE], [MD] or [PC]); CUSTOM
SINGLEPLAYER lists their campaign maps (a map whose scenario is a solo one,
which the multiplayer lists leave out). A campaign map is played as a level
is, at the difficulty chosen next, or from the Map screen hosted as network
co-op (Server Setup, then the lobby; split screen hosts only multiplayer
maps). A `<name>.bmp` beside a map is its picture in the lists and the lobby
(the middle of it, in the lists' picture shape; uncompressed 24-bit or 32-bit
colour, up to 8192 pixels a side), and `<name>.txt` its description, line by
line (`game/ui_map_list.c`, `game/bmp_files.c`); the browser build's page
shows the site's pictures itself.

Multiplayer > CO-OP CAMPAIGN is the Xbox's cooperative play, which the PC
version does not have: two players on this computer play the campaign in
split screen. Player 1 is the player who chose it, on the current profile.
Player 2 then chooses a profile with their own controller (a gamepad), and
New Game's levels are those either profile has reached. Either player's
controller chooses the level and the difficulty. A co-op game does
not continue a saved game of one player.

Network games have split screen too: up to 4 players on each computer. In
the game lobby, another controller presses START to join, and the new
player's profile is chosen on the ADD PLAYER screen that opens (with any
controller). With one gamepad, choose the lobby's ADD PLAYER button first:
until then that gamepad shares controller 1 with the keyboard. Two players on one profile get different names from the host. A
player's B in the lobby leaves the game alone, and the last player of the
computer leaves it for all of them. In the game, each player's pause menu
opens on their part of the screen, and its LEAVE GAME is theirs: their part
of the screen stays until the game ends. A game under way shows its own
screen before JOIN GAME: players join there the same way, START or ADD
PLAYER then START, and JOIN GAME brings them all into the game.

With the PC menus, the pause menu (escape) has SETTINGS after RESUME GAME,
which opens the profile's settings: in the campaign the game stays paused,
in a multiplayer game it goes on. A multiplayer game's host also gets END
GAME, which ends the game as its time limit does: the players stay, and
after the carnage report the host picks the next map and gametype (PICK
GAME). LEAVE GAME of the host still ends the game for everyone. The buttons
go into the map's own pause menu, so the buttons of a custom map stay. The
Xbox's pause box is drawn as it is, its middle longer to hold them; a custom
map's box keeps its size, and what is below its list moves down. The few
pictures of the settings that come from the main menu's map are not drawn
there.

In a single-player campaign, the pause menu has SETTINGS too, before REVERT
TO SAVED, in the same box: a list with room for it centres its rows, and
one without keeps its size, its rows closer. It opens the same settings,
while the game stays paused, with only Controls, Gamepads, Mouse, Audio and
Video Setup; Gamepads' OK saves the profile at once.

With the Xbox's menus, the host of a multiplayer game (one hosted from this
computer, not a dedicated server) gets END GAME in the pause menu too, after
RESUME GAME: RESUME GAME, END GAME, LEAVE GAME. It ends the game as with the
PC menus, and the Xbox's postgame screens then pick the next game. Other
players' pause menus are as the Xbox's.

The menus are XML files in `port/assets/menus` (`tools/ce_menus.py` writes
them from the PC version's tags), which the game contains. To change them,
put files in a `menus` folder next to `config.toml`: a file with the same
path replaces one of the game's, and another `.xml` file is added.
`port/assets/menus/README.md` describes the files. If a file has a problem,
the game uses the Xbox's menus and the log names the file, the line and the
problem.

## Settings

The settings are in `config.toml` next to the executable
(`build/linux/config.toml`). At the first start, the game writes the file
with a comment for each setting and its default value, commented out:

```toml
# The menus: "xbox" for the Xbox's (with Online Games), "gearbox" for
# ...
# menus = "xbox"
```

A setting that is commented out follows the default of the version that
runs, so a new version with a different default changes it. To choose a
value, remove the `# ` at the start of the line and change the value. Only
the settings that you (or the Settings menu) change are lines without `#`;
those stay as they are, also if a later version changes the default. To get
the default values again, delete the file.

The game reads the file once, when it starts: restart the game after you
edit it. If a key is not correct, or a value has the wrong type, the game
writes the line to the log and uses the default value. If a line has a
mistake that TOML cannot read, such as a word without quotes (`menus = pc`
for `menus = "pc"`) or `True` for `true`, the game skips that line, reads
all of the others, and the log names the line and the likely fix. If you
change the value of a line but do not remove its `# `, the line does
nothing, and the log names it. Words such as `"borderless"` can be in any
case; a word that a setting does not know is in the log, with what the game
uses in its place. The Settings menu (Video, Mouse, Audio, Network and Controls Setup)
changes the useful settings, writes them into the file (only their lines
change: the line of a setting at its default loses its `#`) and applies
them at once, but `audio.enabled`, and `display.menus` from the next main
menu. A new version adds its new settings to the file, commented out.

Earlier versions wrote every setting as a value. The first start of this
version updates such a file once (`config_version = 2` at the top
marks it): a setting that holds the default of this version is commented
out, so that it follows the default. A setting with another value stays,
also the default of an earlier version (`display.menus = "pc"`, the default
before 0.5.2b), which you may have chosen; the log names it. The log has one
line that tells how many lines changed.

Each setting has an environment variable. The environment variable changes
the setting for one start of the game. It has priority over the file.

| Setting | Default | Environment variable | Function |
| --- | --- | --- | --- |
| `display.mode` | `""` | `HALO_DISPLAY_MODE` | `"fullscreen"`: the display, taken at the mode of `display.resolution` (the nearest the display has), or at its desktop mode. `"borderless"`: a window over the whole desktop, whose mode does not change. `"windowed"`: a window of `display.window_size`. Empty: `display.fullscreen` decides (`true`: borderless). When it is set, `display.fullscreen` is ignored, and the log says so if both are set. F11 changes between the window and the fullscreen mode. Video Setup sets it. |
| `display.fullscreen` | `true` (`false` on macOS) | `HALO_FULLSCREEN` | `true`: borderless, as `display.mode = "borderless"`. `false`: a window, as `display.mode = "windowed"`. Used only when `display.mode` is empty. |
| `display.resolution` | `"native"` | `HALO_RESOLUTION` | What fullscreen and borderless draw at: `"native"`, the display's own resolution, or `"<width>x<height>"`, such as `"1920x1080"`, 640x480 or more. Fullscreen sets the display to it. Borderless draws at it and scales the picture to the display, where the display has room for it. The picture has 480 lines of the game and the width of the resolution's shape. A window draws at its own size instead. Video Setup's Resolution sets it, from the display's modes; it shows with Fullscreen and Borderless. |
| `display.resolution_scaling` | `"native"` | `HALO_RESOLUTION_SCALING` | `"native"`: the game draws at the resolution of the window, or of the display (or `display.resolution`) fullscreen. `"original"`: the game draws the 640x480 picture of the Xbox and scales it up to the window or the display, whatever `display.mode` is. Video Setup sets it. |
| `display.window_size` | `""` | `HALO_WINDOW_SIZE` | The size of the window, as `"<width>x<height>"`, such as `"1920x1080"`, 640x480 or more. You can change the size of the window; the game's picture takes its shape. Empty: `display.window_scale` decides. Video Setup's Window Size sets it, from sizes of each shape (4:3, 16:10, 16:9 and 21:9) that fit the desktop; it shows with Windowed. |
| `display.window_scale` | `2` | `HALO_WINDOW_SCALE` | Used when `display.window_size` is empty: the size of the window, as a multiple of 640x480. |
| `display.vsync` | `true` | `HALO_NO_VSYNC=1` sets `false` | `true`: each frame waits for the display. |
| `display.max_fps` | `0` | `HALO_MAX_FPS` | With vsync off, the most frames each second. `0`: twice the display's refresh rate. `-1`: no limit, which can hang some Intel graphics (Raptor Lake), resetting the desktop's graphics too. |
| `display.anti_aliasing` | `"off"` | `HALO_ANTI_ALIASING` | The smoothing of jagged edges, which the Xbox did not have. `"off"`: none, as on the Xbox. `"fxaa"` or `"smaa"`: a pass over the 3D view after the game draws it. The HUD and the menus stay sharp. SMAA is the sharper and costs more. `"ssaa2x"`: the game draws at two times the resolution in each direction (at most the GPU's largest texture), and the picture is scaled down. The GPU does four times the work. Not with `display.resolution_scaling = "original"`. `"msaa2x"`, `"msaa4x"` or `"msaa8x"`: each pixel of the 3D view has that many samples (at most the GPU's). On Android, `"smaa"` gives FXAA and `"ssaa2x"` none. A change applies from the next frame. Refer to "Anti-aliasing" in "What operates". |
| `debug.gpu_flush_draws` | `-1` | `HALO_GPU_FLUSH_DRAWS` | Flush the GPU's pipeline every this many draws. `-1`: every 3 on Intel graphics with Mesa's driver, which can otherwise hang in the game's long runs of small draws and reset the desktop's graphics too. `0`: never. |
| `display.interpolation` | `true` | `HALO_INTERPOLATION` | `true`: one frame for each refresh of the display. `false`: 30 frames each second, as on the Xbox. Video Setup's Frame Rate sets it on the desktop (30 (Xbox) is `false`, the others `true`); Android's has Smooth Motion. Refer to "Frame rate". |
| `display.direct_camera` | `true` | `HALO_DIRECT_CAMERA` | `true`: in first person, on foot, the view points where the player aims in each frame, not where the last tick left it. Refer to "Frame rate". Not in Video Setup: config.toml only. |
| `display.fov` | `0.0` | `HALO_FOV` | The first-person view's field of view on foot, in degrees across at 16:9, from 20 to 150. `0`: the stock view. Refer to "Field of view". |
| `display.viewmodel_fov` | `0.0` | `HALO_VIEWMODEL_FOV` | The first-person weapon's and hands' field of view, in degrees across at 16:9, from 20 to 150. `0`: the weapon's stock view, also when `display.fov` widens the world. Refer to "Field of view". |
| `display.viewmodel_visible` | `true` | `HALO_VIEWMODEL_VISIBLE` | `true`: the first-person weapon, hands and what is attached to them are drawn. `false`: they are not; firing, animation, sound and lights go on, and other players' models are drawn. |
| `display.viewmodel_shield` | `false` | `HALO_VIEWMODEL_SHIELD` | `true`: the energy shield's flare (its hits, its charging, its fall) is drawn on the first-person arms too, as on the body, with the unit's own shield shader, kept closer to the arms than to the body. The stock game draws it on the body only. |
| `display.first_person_legs` | `false` | `HALO_FIRST_PERSON_LEGS` | `true`: your own body is drawn from the waist down in first person, so you see your legs when you look down, moving as the body's animation moves them. Refer to "Field of view". |
| `display.shield_color` | `default` | `HALO_SHIELD_COLOR` | Your energy shield's flare as this machine draws it: `default` (the shield's own) or a multiplayer armor color (`white`, `black`, `red`, `blue`, `gray`, `yellow`, `green`, `pink`, `purple`, `cyan`, `cobalt`, `orange`, `teal`, `sage`, `brown`, `tan`, `maroon`, `salmon`), whatever the armor's, on the body and, with `display.viewmodel_shield`, on the first-person arms. The shield's own shader is drawn with its two colors replaced at their brightness; the flare is added to what is behind it, so `black` is drawn a faint grey. Only this machine sees it. |
| `display.far_clip_distance` | `1024.0` | `HALO_FAR_CLIP_DISTANCE` | How far the world is drawn, in world units: `1024`, as on the Xbox, up to `10000`. Raise it on large maps that cut off in the distance (Extinction, Hugeass). It is a floor under the map's own far clip, so it holds across map loads and joins, in single player and multiplayer; scripts and `init.txt` can raise the clip further but not below it. Only the picture changes: the game itself, what each machine sends and the near clip do not. Fog can still end the view sooner. Depth precision at a distance gets coarser as the value grows (about 9 units at 3000, 95 at 10000), so distant overlapping surfaces may flicker at the largest values. |
| `display.match_timer` | `false` | `HALO_MATCH_TIMER` | `true`: in a multiplayer game with a time limit, the time left is drawn at the top of the screen. `false`: the time left shows only on the scoreboard (hold BACK, or tab), as on the Xbox. |
| `display.high_res_hud` | `true` | `HALO_HIGH_RES_HUD` | `true`: the HUD (meters, counters, panels and their outlines, the motion sensor, reticles, waypoints, scopes) is drawn from the high-res assets in `port/assets/hud`, 8x the size of the maps' bitmaps. The bitmaps with English text keep the maps' own. `false`: the maps' own bitmaps. |
| `display.high_res_text` | `true` | `HALO_HIGH_RES_TEXT` | `true`: the menus' and HUD's text is drawn with the fonts in `port/assets/fonts` (Cyborg Interface Gothic, in place of the maps' Interstate) at the resolution the game draws at, laid out as before, and the menus' titles are drawn from the high-res pictures in `port/assets/titles`. `false`: the maps' bitmap fonts and titles. It covers the game's own text only: the Server Browser and the moderation overlays use the system's fonts in any case. For the blocky look of the Xbox, also set `display.resolution_scaling = "original"`. |
| `display.high_res_font` | `""` | `HALO_HIGH_RES_FONT` | The player's own font file (`.ttf` or `.otf`) for the high-res text and the menu text of Online Games, in place of the built-in fonts (`port/assets/fonts/fonts.json`). A relative path is in `data/fonts/` in the data root (a path that leaves that folder is refused); an absolute path is used as it is. A file that is missing or is not a font is logged, and the built-in font is drawn. A change applies while the game runs. |
| `display.high_res_font_large` | `""` | `HALO_HIGH_RES_FONT_LARGE` | As `display.high_res_font`, for the large text only (the font tags with the role `large` in `fonts.json`: `ui\large_ui`, `ui\interstate`; Online Games' large menu text). Empty: `display.high_res_font`. |
| `display.shadow_resolution` | `128` | `HALO_SHADOW_RESOLUTION` | The size of the maps that the shadows of the objects are drawn in, in pixels each way: `128`, `256`, `512` or `1024` (other values go down to one of these). The game draws the shadow of each object into a map of 128x128 pixels, blurs it and projects it onto the ground. On a large screen, the edges of these shadows show steps that move when the object moves. A larger map makes the edges smooth; the blur is made wider to match, so the shadows are as soft as on the Xbox. Each doubling adds two passes of the blur. `128`: as on the Xbox. |
| `display.menus` | `"xbox"` (`"pc"`, Gearbox's, in the browser build) | `HALO_MENUS` | `"xbox"`: the Xbox's menus, with Online Games (the default). `"gearbox"` (or the older `"pc"`): Gearbox's Halo PC menus, from the files in `port/assets/menus` and a `menus` folder next to `config.toml`. Refer to "Menus". |
| `display.version_watermark` | `false` | `HALO_VERSION_WATERMARK` | `true`: the build's version ("0.7.5a (5c1a363)"), which the main menu shows small and faint in the bottom right corner, is shown there while playing too: handy for screenshots and bug reports. |
| `display.player_names` | `"all"` | `HALO_PLAYER_NAMES` | In multiplayer, whose names are drawn above their heads: `"all"`, `"allies"`, `"enemies"` or `"none"`. An ally's name is drawn above the triangle the game shows over teammates. An enemy's name shows only within the motion sensor's reach, while the enemy is in sight and not camouflaged, so it never shows where an enemy hides. The gametype's motion tracker setting also applies: no names if it shows no players, only allies' if it shows only friends. |
| `display.player_name_scale` | `1.0` | `HALO_PLAYER_NAME_SCALE` | How large the players' names are drawn: `1.0` is three quarters of the size of the HUD's text, from `0.25` to `4`. With high-res text, larger names are rasterized at their size, so they stay sharp. |
| `display.follow_camera` | `"long_waits"` | `HALO_FOLLOW_CAMERA` | In multiplayer, when a dead player's camera follows a living player from behind once the original death camera is done (3 seconds): a teammate in a team game, the killer (or the leader) in a game without teams. LB and RB (Q and E, or the mouse wheel, on the keyboard) switch who, and a "FOLLOWING" line near the bottom says who; it returns to the player's own view on respawn. `"long_waits"`: only when the wait to respawn is long (`display.follow_camera_after`), the player is out of lives or odd man out, or the game type holds spawns until its next round; a short respawn keeps the original death camera. `"always"`: after every death. `"off"`: the original death camera only. Only what this machine shows changes, so it works in any host's game. Split screen: each player's own. |
| `display.follow_camera_after` | `5.0` | `HALO_FOLLOW_CAMERA_AFTER` | For `display.follow_camera`'s `"long_waits"`: how many seconds of the wait to respawn must be left once the death camera is done for the camera to follow someone. |
| `display.powerup_timers` | `false` | `HALO_POWERUP_TIMERS` | `true`: a small line of the HUD's text near the top of the view shows the seconds left on the player's active camouflage, overshield and full spectrum vision while one is on ("CAMO 12   OVERSHIELD 41"). The overshield's time is what is left of it above full: it counts down only in multiplayer, where it drains, so in campaign it shows none. Split screen: each player's own. `false`: none, as the original game. |
| `display.loading_screen_min_seconds` | `0.0` | `HALO_LOADING_SCREEN_MIN_SECONDS` | Keep the original loading screen (the picture with the glow that crosses it) up for at least this many seconds, up to 30, when a level loads, as loading from the Xbox's disc did; the port's loads are usually over before it shows. Also when the level is already loaded (a restart). Only in a game no other machine is in (campaign, split screen): a game on the network (system link, online, network co-op) loads as fast as it can, so that no machine waits on another. Never for the main menu. `0`: no wait. |
| `display.scoreboard_team_layout` | `"teams"` | `HALO_SCOREBOARD_TEAM_LAYOUT` | How the multiplayer scoreboard (hold BACK, or tab) lists a team game's players. `"teams"`: a column for each team, red on the left and blue on the right. `"score"`: all the players in order of score. With more players than fit, the mouse wheel and Page Up / Page Down scroll the scoreboard. |
| `display.scoreboard_background` | `true` | `HALO_SCOREBOARD_BACKGROUND` | `true`: the multiplayer scoreboard (hold BACK, or tab) has a panel behind its text, for clearer text. |
| `display.show_quit_players` | `true` | `HALO_SHOW_QUIT_PLAYERS` | `true`: players who quit stay on the multiplayer scoreboard (hold BACK, or tab) and the score in the corner, as in the original game. `false`: they are left off, and the players still in the game are ranked among themselves, as OpenCE does. Only what this machine draws changes. |
| `display.scoreboard_background_color` | `"16, 16, 16, 150"` | `HALO_SCOREBOARD_BACKGROUND_COLOR` | The colour of the scoreboard's panel: `"red, green, blue, alpha"`, each from `0` to `255`. Alpha `0` is see-through, `255` is solid. |
| `display.server_messages` | `true` | `HALO_SERVER_MESSAGES` | `true`: a warning or notice from a Delta server is drawn as a banner over the game; `false` hides the banner. The message is written to `debug.txt` either way. Kicks and bans are not affected. |
| `display.server_message_text_color` | `""` | `HALO_SERVER_MESSAGE_TEXT_COLOR` | The banner's text colour, `"#RRGGBB"` or `"#RRGGBBAA"`. Empty, or a value that does not parse (logged once): the default, light for a notice and amber for a warning. Applies to both kinds. |
| `display.server_message_border_color` | `""` | `HALO_SERVER_MESSAGE_BORDER_COLOR` | The banner's outline colour, `"#RRGGBB"` or `"#RRGGBBAA"`. Empty, or a value that does not parse: the default, blue for a notice and amber for a warning. Applies to both kinds. |
| `display.server_message_background_color` | `""` | `HALO_SERVER_MESSAGE_BACKGROUND_COLOR` | The banner's background colour, `"#RRGGBB"` or `"#RRGGBBAA"`. Empty, or a value that does not parse: the default, dark blue. |
| `chat.enabled` | `true` | `HALO_CHAT` | Text chat with other Chupathingy players, in a game whose host is a Chupathingy copy or server (Delta Peer's `chat`; OpenCE players are never sent a line). T says a line to everyone, Y to your team; Enter sends, Escape cancels, and while you type the keyboard drives nothing else. The lines show at the lower left above the motion sensor, in the HUD's message font and tint (a size smaller, with a heavier shadow), four at a time, and fade after 6 seconds; H shows the last ones and the players, where up and down choose one and M mutes them for you alone. `false`: no chat at all, neither said nor shown; the server's own lines still show (as `display.server_messages` says). Nothing of what is said is written down. |
| `chat.show` | `"all"` | `HALO_CHAT_SHOW` | Which players' lines show: `"all"`, `"team"` (your team's alone) or `"off"` (none; you can still talk). |
| `chat.filter` | `"mask"` | `HALO_CHAT_FILTER` | Blocked words in the lines you are shown: `"mask"` stars them out, `"hide"` leaves such a line out, `"show"` shows it as sent. The words are the blocked names list's (`server/blocked_names.txt`, built in) and your own (`chat.filter_file`). Nobody is told and nobody is kicked. |
| `chat.filter_file` | `"chat_filter.txt"` | `HALO_CHAT_FILTER_FILE` | Your own blocked words, a file in the save folder: a word a line (blocked anywhere in a word), or `whole <word>` (only as a whole word); `#` starts a comment. |
| `chat.streamer_mode` | `false` | `HALO_CHAT_STREAMER_MODE` | `true`: other players' names in chat show as their team (or "Player"), and blocked words are starred out even with `chat.filter = "show"`. |
| `chat.quick_messages` | `"Nice shot\|Need backup\|On my way\|Thanks\|Good game\|Sorry"` | `HALO_CHAT_QUICK_MESSAGES` | A controller's quick lines, separated by `\|`: D-pad up shows the next, D-pad down says it (to your team in a team game). Empty: none. |
| `display.per_pixel_lighting` | `false` | `HALO_PER_PIXEL_LIGHTING` | `false`: the models (characters, weapons, vehicles, scenery) are lit at each vertex and the light is blended between them, as on the Xbox. The light across a curved surface then shows facets, and a point light that passes close lights only the vertices it reaches. `true`: the models are lit at each pixel by the same lights (the ambient light, two distant lights and two point lights), which changes their look. |
| `display.shield_glow` | `false` | `HALO_SHIELD_GLOW` | `true`: as a unit's energy shield flares (hit, charging, falling), it lights what is around it and itself, as a plasma bolt does, in the shield shader's own color (a Spartan's gold, an Elite's blue), rising and falling with the flare seen on the body. An ordinary dynamic light of the game's, drawn with a light definition the map has (`shield_glow.c`). Up to 8 units near the camera. |
| `display.shield_glow_intensity` | `default` | `HALO_SHIELD_GLOW_INTENSITY` | The shield glow's reach and brightness: `default`, or `light_show` (three times as far, and at full strength from a third of the flare: a light is no brighter than full). The units glowing are those near any local player's camera, so each of a split screen's views has the same. |
| `audio.enabled` | `true` | `HALO_NO_AUDIO=1` sets `false` | `false`: no audio device. The sound continues without output. |
| `audio.volume` | `1.0` | `HALO_VOLUME` | The master volume. |
| `audio.music_volume` | `1.0` | `HALO_MUSIC_VOLUME` | The music's volume, of the master volume. |
| `audio.effects_volume` | `1.0` | `HALO_EFFECTS_VOLUME` | The volume of the other sounds (effects and speech), of the master volume. |
| `audio.buffer_frames` | `2048` on macOS, `1024` on Android, `512` elsewhere | `HALO_AUDIO_BUFFER_FRAMES` | The audio device's buffer, in sample frames at 48 kHz, from `64` to `8192`. Larger rides out stalls that would cut the sound out; smaller has less delay (512 is 11 ms, 1024 is 21 ms, 2048 is 43 ms). |
| `audio.reverb` | `true` | `HALO_REVERB` | `true`: the sounds of the world reverberate as the place the player is in does: the sound environments of the maps (a corridor, a cave, a large hall, outdoors) set the reverberation, as the I3DL2 reverb of the Xbox did. A sound behind a wall or a door is muffled in it too. `false`: no reverberation (sounds behind a wall are still muffled). |
| `audio.loose_sounds` | `false` | `HALO_LOOSE_SOUNDS` | For those who make sounds. `true`: each sound of a map that has a sound tag file of its name in `tags/` in the data root (for example `tags/sound/sfx/weapons/assault rifle/fire.sound`) plays from that file. The files are Halo PC tag files, as the Halo Editing Kit and Invader write them. At the console, `loose_sounds_reload` reads the files again, and `loose_sounds false` plays the sounds of the map again. When a file changes, all sounds stop. |
| `audio.voice_chat` | `"push_to_talk"` | `HALO_VOICE_CHAT` | How you talk in voice chat: `"push_to_talk"` (while `controls.push_to_talk` is held; the microphone opens when you first press it), `"open_mic"` (when the microphone hears speech), or `"off"`. You hear the other players in every case. Refer to "Voice chat". |
| `audio.voice_volume` | `1.0` | `HALO_VOICE_VOLUME` | The volume of the voices of the other players, `0` to `2`. |
| `audio.output_device`, `audio.input_device` | `"default"` | `HALO_AUDIO_OUTPUT_DEVICE`, `HALO_AUDIO_INPUT_DEVICE` | The speakers and the microphone, by the name that Settings > Audio shows, or `"default"` for the device of the system. If the device is not found, the game uses the device of the system. Not on Android. |
| `audio.resampling` | `"sinc"` | `HALO_AUDIO_RESAMPLING` | How the sounds, most of them recorded at 22 kHz, are brought to the output's 48 kHz. `"sinc"`: a windowed sinc low pass keeps each sound's band (flat to about 9 kHz for a 22 kHz sound) and nothing above it. `"linear"`: linear interpolation, as before OpenCE's build 130: the top of each sound's band is duller (-4 dB at 8 kHz) and images of the band reach up to 22 kHz, a brighter, grainier sound. |
| `input.mouse_sensitivity` | `1.0` | `HALO_MOUSE_SENSITIVITY` | The multiplier for the mouse aim. |
| `input.mouse_vertical_sensitivity` | `0.0` | `HALO_MOUSE_VERTICAL_SENSITIVITY` | The multiplier for the vertical mouse aim. `0`: the same as `input.mouse_sensitivity`. |
| `input.invert_mouse` | `false` | `HALO_MOUSE_INVERT=1` sets `true` | `true`: the vertical mouse aim is inverted. |
| `input.keyboard_prompts` | `true` | `HALO_KEYBOARD_PROMPTS` | `true`: while the keyboard or mouse was used last (or no controller is connected), the button prompts show keys, as Halo PC's did: the HUD's messages name the bound key in brackets ("Hold [E] to pick up"), "Hold BACK for score" names the scoreboard key, and the menus' button pictures ("A = SELECT") become key caps of the keys that press them (Enter, Backspace, Tab, E). A controller's button press switches them back. `false`: always the controller's buttons. Desktop only. |
| `input.mouse_aim_assist` | `false` | `HALO_MOUSE_AIM_ASSIST` | `true`: the magnetism of the controller also operates for the mouse. `false`: when the mouse moved after the right stick, the view is not slowed or dragged by a target. The autoaim of the bullets operates in both cases. |
| `controls.<action>` | (the table in "Controls") | `HALO_KEY_<ACTION>` | The keys and mouse buttons of an action, up to two, separated by a comma: `move_forward`, `move_backward`, `strafe_left`, `strafe_right`, `jump`, `crouch`, `fire`, `throw_grenade`, `melee`, `reload`, `zoom`, `switch_weapon`, `switch_grenade`, `action`, `flashlight`, `scoreboard`, `pause`, `screenshot` (F10; not on Android or in the browser), `push_to_talk`. Keys by their names (`"W"`, `"Space"`, `"Left Ctrl"`, `"F1"`), and `"Mouse Left"`, `"Mouse Right"`, `"Mouse Middle"`, `"Mouse 4"`, `"Mouse 5"`, `"Wheel"` (either way), `"Wheel Up"`, `"Wheel Down"`. In the browser build, `crouch` is `"C"` alone. |
| `game.downloaded_maps` | `""` | `HALO_DOWNLOADED_MAPS` | Maps also played as downloaded maps (a map the game downloads is marked so by itself): their names as the game names them (`bloodgulch`, `hugeass@ce`), separated by commas, or `"*"` for all the maps. The scripts of a downloaded map cannot change the settings of the player or the games of other players. `debug.txt` names what the game refused. |
| `game.bomb_spawns` | `""` | `HALO_BOMB_SPAWNS` | Where a Bomb or Grifball game you host puts the bomb on a map: `"<map> <x> <y> [<z>]"` (the map as a playlist names it: `bloodgulch`, `timberland@ce`), entries separated by commas. The bomb rests on the first ground under the point (from a little over its height, when given). A map not named has the game's own spot: Blood Gulch's halfway between its teleporters' exits, any other map's halfway between the goals. Only the host's setting counts. |
| `game.bomb_open_pitch_speed` | `0` | `HALO_BOMB_OPEN_PITCH_SPEED` | Everyone's speed in a Grifball: Open Pitch game you host, in percent (50 to 200; the carrier's 115% on top stops at 200). `0`: the game type's own, 175. |
| `game.bomb_open_pitch_carrier_health`, `game.bomb_open_pitch_carrier_resistance` | `0`, `0` | `HALO_BOMB_OPEN_PITCH_CARRIER_HEALTH`, `HALO_BOMB_OPEN_PITCH_CARRIER_RESISTANCE` | The bomb carrier in a Grifball: Open Pitch game you host: health in percent of everyone's, damage resistance in percent. `0`: the game type's own, 400 each (the carrier lives through one melee blow, not two). |
| `game.bomb_open_pitch_carrier_shields` | `"none"` | `HALO_BOMB_OPEN_PITCH_CARRIER_SHIELDS` | The bomb carrier's shields in a Grifball: Open Pitch game you host: `"none"`, `"normal"` or `"overshield"`. |
| `game.bomb_open_pitch_fumble` | `true` | `HALO_BOMB_OPEN_PITCH_FUMBLE` | `true`: in a Grifball: Open Pitch game you host, a melee blow that doesn't kill the bomb carrier knocks the bomb out of their hands. |
| `game.console_log` | `"important"` | `HALO_CONSOLE_LOG` | What the console shows on the screen. `"important"`: bans, players that the host drops for cheating, the reasons that the game refuses a command, and the asserts that stop the game. `"all"`: all the lines. `"none"`: only the asserts that stop the game. The output of a command always shows. `debug.txt` gets all the lines. |
| `game.language` | `""` | `HALO_LANGUAGE` | The language of the menus: `ja`, `de`, `fr`, `es` or `it`. Empty: English. |
| `game.enhanced_animations` | `true` | `HALO_ENHANCED_ANIMATIONS` | `true`: the player bipeds' grenade throws keep their legs moving, blended by speed and direction (crouched throws stay crouched, throws in the air use the jump's legs), Warthog and Scorpion riders stay seated to throw and let go of the grips to throw and reload, and a player turns with the aim while throwing, as while meleeing. `false`: the original animations, which freeze the legs during a throw and stand a rider up. Only in config.toml, not in the menus. |
| `game.unlock_all_levels` | `false` | `HALO_UNLOCK_ALL_LEVELS` | `true`: a profile created afterward starts with all the campaign levels finished on all the difficulties. `false`: a new profile starts at The Pillar of Autumn, and each level opens when you finish the level before it, as on the Xbox. Existing profiles keep their progress. To start a profile over, type `profile_reset_campaign` in the console. |
| `paths.data` | `""` | `HALO_DATA_ROOT` | The data root. Refer to "Start the game". |
| `paths.saves` | `""` | `HALO_SAVE_ROOT` | The save root. Refer to "Files and folders". |
| `paths.custom_edition` | `""` | `HALO_CUSTOM_EDITION_ROOT` | Desktop builds. A folder of Custom Edition maps, or a Halo Custom Edition install (then its `maps` folder), looked in last for Custom Edition maps and their `bitmaps.map`, `sounds.map` and `loc.map`: after `maps_ce`, `maps/ce` and `custom_maps`. The game names it the Xbox drive `h:\`. Empty: none. OpenCE's setting. |
| `game.custom_edition` | `true` | `HALO_CUSTOM_EDITION` | `true`: Halo PC maps are played (Custom Edition's, HaloMD's and Halo PC retail's). `false`: none of them is listed, played or downloaded, and a game on one says that they are turned off. OpenCE's setting. |
| `game.move_old_map_folders` | `"ask"` | `HALO_MOVE_OLD_MAP_FOLDERS` | The older folders of Halo PC maps (`maps/ce`, `md_maps`), which are still played from. `"ask"`: the game offers once to move them into `maps_ce` and `maps_md` (each folder moved whole, never copied; one that cannot be moved stays where it is). `"yes"`: moved without asking. `"no"`: left where they are. Android moves them unless this is `"no"`. |
| `data.download_url` | `"https://halo.fractumseraph.net/assets/maps/"` | `HALO_DATA_DOWNLOAD_URL` | Desktop builds. Where the first start offers to download the `maps` folder from, when there is none: an `https://` folder holding its files and `maps.txt` (the files' names and sizes, a line each; the `assets/maps/` of `services/selfhost`). Empty: the game offers only an Xbox disc image. |
| `paths.logs` | `""` | `HALO_LOG_ROOT` | The logs folder (`debug.txt`, `halo.log`, `crashes/`). Empty: `logs/` in the data root. Refer to "Files and folders". |
| `paths.cache` | `""` | `HALO_CACHE_ROOT` | The cache folder. Empty: `cache/` in the save root. |
| `network.address` | `""` | `HALO_NET_ADDRESS` | The IPv4 address of this machine for system link. Refer to "Play on one computer". |
| `network.broadcast` | `""` | `HALO_NET_BROADCAST` | IPv4 addresses, with commas between them, that get the broadcasts of the game. Empty: 255.255.255.255. |
| `network.online` | `true` | `HALO_NET_ONLINE` | `true`: internet play. `false`: system link on the local network only. |
| `network.join_from_clipboard` | `"ask"` | `HALO_NET_JOIN_FROM_CLIPBOARD` | An invite link on the clipboard when the game comes to the front: `"ask"` asks over the menus whether to join its game (with its name and map when the game list knows them; No is kept for that invite), `"always"` joins it, `"never"` leaves it. `true` and `false`, from older versions, mean `"always"` and `"never"`. Network Settings in the Gearbox menus. |
| `network.fair_play` | `"refuse"` | `HALO_NET_FAIR_PLAY` | When you host: check players' shots for cheats. `"refuse"` refuses a bullet's hit on a player through a solid wall, or far from where its shooter aimed (silent aim), and logs those, near misses of the checks, fast snaps of the aim and aim held on enemies behind walls (wallhacks) in `fairplay.txt` beside `bans.txt`; `"log"` only logs; `"off"` checks nothing. Refer to `server/docs/admin.md`, "Fair play checks". |
| `network.record_games` | `false` | `HALO_NET_RECORD_GAMES` | When you host: record every game to `recordings\` in the data folder (each player's position, aim, health and weapon every tick, their names, addresses and hardware ids, the fair play checks' verdicts, the kills and the map's walls), about 40 MB an hour, to watch again at https://halo.fractumseraph.net/assets/replay/. Refer to `server/docs/admin.md`, "Recordings". |
| `network.recordings_megabytes` | `2048` | `HALO_NET_RECORDINGS_MEGABYTES` | How many megabytes of recordings to keep: the oldest go first; 0 keeps them all. |
| `network.record_public` | `false` | `HALO_NET_RECORD_PUBLIC` | When you host, with `network.record_games`: also write each game's public copy to `recordings\public\`, for anyone to watch live or again in the replay viewer: no player's address or hardware id and no fair play verdicts, and the game as it is played held back `network.record_public_delay` seconds so that no one watching can tell its players where the others are. `live.json` there says which game is being written. Serve that folder alone, never `recordings\`. Refer to `server/docs/admin.md`, "Public replays and watching live". |
| `network.record_public_delay` | `30` | `HALO_NET_RECORD_PUBLIC_DELAY` | How many seconds the public copy is held back while its game is played. |
| `network.record_public_megabytes` | `2048` | `HALO_NET_RECORD_PUBLIC_MEGABYTES` | How many megabytes of public copies to keep: the oldest go first; 0 keeps them all. |
| `network.shared_bans` | `""` | `HALO_NET_SHARED_BANS` | When you host: ban lists other hosts publish, taken as your own bans: https:// addresses, with commas between them (FractumSeraph's servers' is `https://halovps.fractumseraph.net/bans/shared.txt`). They are fetched at start and every 10 minutes into `shared_bans.txt` beside `bans.txt`, and a machine whose hardware id (or address) is in one is refused. Name only lists whose publishers you trust. Refer to `server/docs/admin.md`, "Shared ban lists". |
| `network.lasting_invite` | `false` | `HALO_NET_LASTING_INVITE` | `true`: the invite link of the game you host stays the same from one run of the game to the next (a dedicated server's, to post on a website). Its key and token are kept in `lasting_invite.key` in the save folder, which only you can read. Anyone who has the link can always use it; a new password, or making the game private, still makes a new link, which is then kept. Copy the file to one machine only. `false`: a new link each run. |
| `network.map_downloads` | `"ask"` | `HALO_NET_MAP_DOWNLOADS` | A Custom Edition map a game you join needs and you don't have: over BitTorrent first (`maps.torrents`: other players, the trackers and web seeds), then from the HaloNet and HaloMaps archives (`maps.archives`), then from the community map repositories (maps.halonet.net and its mirrors, as Chimera downloads from them); a host that shares its map sends it before any of these (`network.map_share`). `"ask"`: the Online Games screen offers to download it (Yes, No, Don't ask again, which writes `"always"` or `"never"` here). `"always"`: downloaded without asking. `"never"`: the game only says which map is missing (map sharing aside), and the Server Browser's join does not download it either. Halo PC's own maps (Blood Gulch, Putput, ...) are offered too in this fork; with `"never"` the game asks you to copy them from your install. Network Settings in the PC menus. The map is checked before it goes in `maps_ce`: refer to "Halo PC maps" in the [README](../../README.md). Not on the dedicated server. |
| `network.tunnel_port` | `0` | `HALO_NET_TUNNEL_PORT` | The UDP port for internet play. `0`: the game selects a port. Refer to "Internet play". |
| `network.allow_upnp` | `true` | `HALO_NET_ALLOW_UPNP` | `true`: internet play can ask the router to forward its port (UPnP). `false`: the game does not ask. Refer to "Internet play". |
| `network.protocol` | `"auto"` | `HALO_NET_PROTOCOL` | Delta Peer, the messages between ChupathingyCE machines beside OpenCE's protocol (UDP port 5160; `docs/delta.md`). `"auto"`: Delta with the machines that speak it, plain OpenCE with the others. `"opence"`: Delta off. `"delta"`: as `"auto"` for now. |
| `network.chat` | `"on"` | `HALO_NET_CHAT` | Text chat in the games this copy hosts: `"on"`, `"team"` (team lines alone) or `"off"`. |
| `network.chat_filter` | `true` | `HALO_NET_CHAT_FILTER` | In the games this copy hosts, a line with a blocked word (the blocked names list's) is not passed on, and its sender is told. Nobody is kicked. |
| `network.share_profile` | `false` | `HALO_NET_SHARE_PROFILE` | `true`: the other ChupathingyCE players of a game see this copy's player ID (its game list profile), over Delta. |
| `network.platform_limits` | `"on"` | `HALO_NET_PLATFORM_LIMITS` | `"on"`: Delta hosts keep a game to the players this platform takes (an original Xbox: 16). `"off"`: this machine joins games of any size. |
| `network.host_platform_limits` | `true` | `HALO_NET_HOST_PLATFORM_LIMITS` | `true`: a game this machine hosts keeps to the players its Delta machines' platforms take. `false`: their limits are ignored, for testing. |
| `network.public_lobby` | `true` | `HALO_NET_PUBLIC_LOBBY` | `true`: the server browser. Public games are listed, and Join Game > Server Browser shows them. `false`: no games are listed or shown. Refer to "Server browser". |
| `network.ping_games` | `true` | `HALO_NET_PING_GAMES` | `true`: Online Games pings the listed games whose hosts answer pings, before joining (the host sees your public address, as when you join). `false`: no pings; only the estimated ones (`~85`). Refer to docs/delta.md, "Pings before joining". |
| `network.host_public` | `true` | `HALO_NET_HOST_PUBLIC` | `true`: a new game of Create Game > Internet starts as PUBLIC. `false`: it starts as PRIVATE. LISTING in Server Setup changes it for each game. Refer to "Server browser". |
| `network.coop_friendly_fire` | `"on"` | `HALO_NET_COOP_FRIENDLY_FIRE` | Whether the players of an online co-op game hurt each other: `"off"`, `"on"`, `"shields_only"` or `"explosives_only"`. FRIENDLY FIRE in co-op's Server Setup > Co-op Options writes its choice here. Their AI allies they always can, as in the campaign. |
| `network.coop_player_collisions` | `true` | `HALO_NET_COOP_PLAYER_COLLISIONS` | Whether the players of an online co-op game bump into each other. `false`: they walk through each other, so that one cannot block a doorway or stand on another; they still bump into the AI's characters. PLAYER COLLISIONS in co-op's Server Setup > Co-op Options writes its choice here. |
| `network.coop_enemies_mode` | `"per_player"` | `HALO_NET_COOP_ENEMIES_MODE` | Online co-op's extra enemies: `"none"`; `"per_player"`, each squad of enemies that a level places grows by `network.coop_enemies` for each player past the first; or `"multiplier"`, each squad is `network.coop_enemies_multiplier` times as large, for any number of players. The extra enemies stand around the squad's places, and those that a dropship has no seats for drop out of it after its passengers. EXTRA ENEMIES in co-op's Server Setup > Co-op Options writes its choice here. |
| `network.coop_enemies` | `50` | `HALO_NET_COOP_ENEMIES` | The extra enemies per player, a percentage from `25` to `200`: for each player past the first, each squad of enemies gets this much of itself more (`100`: as many again, so four players meet four times the squad), up to 8 times the squad however many players there are. PER PLAYER in co-op's Server Setup > Co-op Options writes its choice here. |
| `network.coop_enemies_multiplier` | `2` | `HALO_NET_COOP_ENEMIES_MULTIPLIER` | The static multiplier of the enemies, `2` to `32`: each squad of enemies is this many times as large. MULTIPLIER in co-op's Server Setup > Co-op Options writes its choice here. |
| `network.voice_lobby` | `true` | `HALO_NET_VOICE_LOBBY` | When you host: `true`, all players hear all players in the lobby, before and after a game. |
| `network.voice_mode` | `"team_global_enemy_proximity"` | `HALO_NET_VOICE_MODE` | When you host: who hears whom during a game. `"off"`; `"team_proximity"` (teammates who are near); `"team_enemy_proximity"` (all players who are near); `"team_global"` (all teammates); `"team_global_enemy_proximity"` (all teammates, and enemies who are near). Refer to "Voice chat". |
| `network.voice_kbps` | `24` | `HALO_NET_VOICE_KBPS` | When you host: the voice quality in the lobby and in a game, in kilobits per second, `8` to `64`. |
| `network.voice_proximity` | `15.0` | `HALO_NET_VOICE_PROXIMITY` | When you host: the distance in world units (1 unit is approximately 3 metres) at which players are near, for voice chat. `5` to `100`. |
| `network.votekick` | `true` | `HALO_NET_VOTEKICK` | When you host: `true`, the players can vote to kick a player. Refer to "Security". `false`: no votes. VOTE KICK in Server Setup > Teamplay Options (in co-op, Voice and Voting) writes its choice here. |
| `network.votekick_minutes` | `5` | `HALO_NET_VOTEKICK_MINUTES` | When you host: the minutes that a player must play on the server before the player can start a vote to kick (`0` to `60`). To vote, a player must play for 2 minutes, or for this time if it is less. |
| `network.votekick_ban_minutes` | `30` | `HALO_NET_VOTEKICK_BAN_MINUTES` | When you host: the minutes that a player who is kicked by a vote cannot join again (`1` to `1440`). |
| `network.map_share` | `true` | `HALO_NET_MAP_SHARE` | When you host: send a player who joins without the game's Custom Edition map the map, and the resource maps that it needs and that the player does not have. SHARE MAPS in Server Setup > Map Sharing writes its choice here. Refer to "Map sharing". |
| `network.map_share_upload` | `1024` | `HALO_NET_MAP_SHARE_UPLOAD` | When you host: the most kilobytes per second that the game sends of maps (`16` to `65536`). UPLOAD LIMIT in Server Setup > Map Sharing writes its choice here. |
| `network.map_seed` | `true` | `HALO_NET_MAP_SEED` | When you play in the game of another machine: help send its map to players who join without it. HELP SEND MAPS in Settings > Network Setup > Map Sharing. |
| `network.map_seed_upload` | `256` | `HALO_NET_MAP_SEED_UPLOAD` | When you play in the game of another machine: the most kilobytes per second that the game sends of maps to other players (`16` to `65536`). UPLOAD LIMIT in Settings > Network Setup > Map Sharing. |
| `network.map_download` | `true` | `HALO_NET_MAP_DOWNLOAD` | When you join a game on a Custom Edition map that you do not have: download it from the players of the game. `false`: leave the game and show which map is missing. DOWNLOAD MAPS in Settings > Network Setup > Map Sharing. |
| `network.coop_public` | `false` | `HALO_NET_COOP_PUBLIC` | `true`: an online co-op game (Create Game > Internet, a SINGLEPLAYER map) starts as PUBLIC. `false`: it starts as PRIVATE. LISTING in co-op's Server Setup writes its choice here. Refer to "Server browser". |
| `network.brokers_file` | `"brokers.txt"` | `HALO_NET_BROKERS_FILE` | The file of the public MQTT brokers that let the machines of an invite find each other, and that carry the listings of the server browser: next to `config.toml`, unless a full path. One `host:port` on each line, up to 5; `#` starts a comment. |
| `network.stun_servers` | Google and Cloudflare | `HALO_NET_STUN` | The public STUN servers (`host:port`, with commas between them) that give the internet address of a machine. |
| `network.legacy_table` | `""` | `HALO_LEGACY_TABLE` | For testing, and for admins: a legacy table file, not signed, next to `config.toml` unless a full path. Its row for the wire of the build sets the OpenCE network versions that the game announces and joins, in place of the signed tables. The log shows a warning at start. Refer to `docs/delta.md`. Empty: none. |
| `network.browser_url` | `"https://halo.milenko.org"` | `HALO_NET_BROWSER` | The game list server: hosted games are listed there, System Link shows its games, and the reports below go there. Empty: none. |
| `network.list_hosted_games` | `true` | `HALO_NET_LIST_GAMES` | `true`: the system link games this machine hosts are listed on `network.browser_url` (halo.milenko.org), where anyone can find and join them. Only public games without a password are listed. `false`: they are kept to invites and the local network, and no carnage report of them is sent. |
| `network.report_hosted_games` | `true` | `HALO_NET_REPORT_HOSTED` | The stats of the games this machine hosts: a listed game's carnage report, its Delta Stats (`network.report_events`) and the host's own players' claims of their lines. `false`: none of them are sent, and the game stays listed (`network.list_hosted_games`). On the Privacy screen (Gearbox menus). |
| `network.report_joined_games` | `true` | `HALO_NET_REPORT_GAMES` | When an internet game this machine joined ends, the game sends `network.browser_url` its scores as this machine saw them, with this copy's player ID, and confirms this machine's players' lines in the host's report of the game (`POST /v1/claim`), which puts the game on their profiles. `false`: neither, for games this machine joins. Games it hosts are still reported and confirmed. |
| `network.report_events` | `true` | `HALO_NET_REPORT_EVENTS` | Delta Stats: the games this machine hosts are recorded (kills with weapons and positions, accuracy, medals, objectives, vehicles, pickups) and sent to `network.browser_url` when each ends. Players are their names, never a hardware ID or an address; an OpenCE player is only their name, team and that game's score, place, kills and deaths. `false`: no events are recorded or sent. The carnage report of a hosted game is separate: it is sent while the game is listed (`network.list_hosted_games`). Refer to `docs/delta.md`. |
| `network.events_token` | `""` | `HALO_EVENTS_TOKEN` | Delta Stats: a dedicated server's token, from the game list's operator, so that its games count as a trusted server's. Empty: the game must be listed there, from this machine, for its events to be taken. |
| `network.events_positions` | `2` | `HALO_EVENTS_POSITIONS` | Delta Stats: the seconds between the samples of each player's position, `1` to `60`; `0`: none. |
| `network.events_limit` | `40000` | `HALO_EVENTS_LIMIT` | Delta Stats: the most events a game keeps, `64` to `200000` (about 60 bytes each); past it the position samples thin out first. |
| `network.events_part_minutes` | `30` | `HALO_EVENTS_PART_MINUTES` | Delta Stats: a long game is also sent as it stands every this many minutes (the end replaces it). `0`: only at its end. |
| `network.events_folder` | `""` | `HALO_EVENTS_FOLDER` | Delta Stats: a folder that keeps a copy of each batch sent, a JSON file each (a full path is best). Empty: none. |
| `discord.application_id` | the application of the project (Chupathingy) | `HALO_DISCORD_APPLICATION` | The Discord application the game shows as. Empty: no Discord. |
| `discord.presence` | `"full"` | `HALO_DISCORD_PRESENCE` | What Discord shows: `"full"` (game type, map, score, players, time, pictures), `"minimal"` (only "Playing Chupathingy") or `"off"`. Network Setup in the PC menus. |
| `discord.show_server_name` | `true` | `HALO_DISCORD_SHOW_SERVER_NAME` | `true`: Discord shows the name of the network game. |
| `discord.show_join` | `true` | `HALO_DISCORD_SHOW_JOIN` | `true`: while this machine hosts a public game (in the server browser, no password), Discord shows a Join game button. |
| `maps.torrents` | `true` | `HALO_MAP_TORRENTS` | `true`: a Custom Edition map that this machine does not have is downloaded when joining a game on it, from other players and seed boxes (BitTorrent), if the index of the maps' torrents has it. `false`: never downloads (and never seeds). Refer to [docs/map_torrents.md](../../docs/map_torrents.md). Not in the browser build. |
| `maps.download_screen` | `true` | `HALO_MAP_DOWNLOAD_SCREEN` | `true`: while a map downloads before its game is joined, Halo's loading screen covers the menus, its light sweeping across as the map comes, with the map's name and how much has come; B stops the download (`port/linux/game/download_screen.c`). `false`: the menus stay up, and their status line says how the download goes. Also in the browser build, for a map the page fetches before the Server Browser joins. |
| `maps.seed` | `"host"` | `HALO_MAP_SEED` | Seed the Custom Edition map that this machine's network game is on: `"host"` while hosting, `"all"` also while joined, `"off"` never. |
| `maps.upload_limit` | `512` | `HALO_MAP_UPLOAD_LIMIT` | The most that seeding sends, in KB each second, however many players download, so that the game's own traffic comes first. `0`: no limit. |
| `maps.download_limit` | `0` | `HALO_MAP_DOWNLOAD_LIMIT` | The most that a map download takes, in KB each second. `0`: no limit. |
| `maps.torrent_port` | `0` | `HALO_MAP_TORRENT_PORT` | The TCP port that the downloads of other players connect to, and the UDP port of the DHT. `0`: the game selects a port. Forward a fixed one on the router so that players behind other routers can reach this machine. |
| `maps.dht` | `true` | `HALO_MAP_DHT` | `true`: the peers of a map are found through the mainline DHT. `false`: only through the trackers. |
| `maps.trackers` | the map archive's tracker (`halovps.fractumseraph.net:6969`, UDP and HTTP) | `HALO_MAP_TRACKERS` | The trackers that every map's torrent is announced to (`udp://host:port/announce` or `http://host/announce`), with commas between them. Empty: none. |
| `maps.web_seeds` | `"http://halomaps.fractumseraph.net/maps/"` | `HALO_MAP_WEB_SEEDS` | The `http://` folders that hold the maps under their file names, with commas between them; a download also takes pieces from them. Empty: none. |
| `maps.archives` | `"halonet,halomaps"` | `HALO_MAP_ARCHIVES` | Linux, Windows and macOS. When BitTorrent does not give a map (the download fails, or brings nothing for 30 seconds), it is taken out of the zip that the index names for it in the community's archives, in this order: `halonet` (maps.halonet.net), `halomaps` (halomaps.org), and checked against the index's info hash. Empty: none. Refer to `docs/map_torrents.md`. |
| `maps.torrent_index` | `"map_torrents.txt"` | `HALO_MAP_TORRENT_INDEX` | The index of the maps' torrents (`tools/map_torrents.py`), next to `config.toml` unless a full path. The builds put `port/assets/network/map_torrents.txt` there. |
| `update.auto` | `true` | `HALO_UPDATE_AUTO` | `true`: at start-up, the game looks for a new version. Refer to "Updates". `false`: the game does not look. |
| `crash_reports.upload` | `"yes"` (this fork's; ChupathingyCE's is `"ask"`) | `HALO_CRASH_REPORTS` | Desktop builds (releases and nightlies). `"yes"`: the game sends a report of each crash to the developers, through `network.browser_url`. `"no"`: the game sends no reports. `"ask"`: the game asks after the next crash (on Linux and macOS, when it starts the next time) and writes the answer here. On the Privacy screen (Gearbox menus). Refer to "Crash reports" below and in [port/windows/README.md](../windows/README.md#crash-reports). |
| `debug.update_answer` | `""` | `HALO_UPDATE_ANSWER` | The answer to the update question, for automatic tests: `yes`, `no` or `never`. Empty: the game asks. |
| `debug.exit_after` | `0.0` | `HALO_EXIT_AFTER` | The game stops after this number of seconds. `0`: never. |
| `debug.screenshot_directory`, `debug.screenshot_every` | `""`, `0` | `HALO_SCREENSHOT_DIR`, `HALO_SCREENSHOT_EVERY` | The game writes each Nth frame to this folder as a BMP file. |
| `debug.hidden_window`, `debug.null_renderer` | `false` | `HALO_HIDDEN_WINDOW`, `HALO_NULL_RENDERER` | `true`: no visible window, or no graphics. |
| `debug.gpu_stats`, `debug.gpu_trace_frame`, `debug.gpu_trace_constants`, `debug.gpu_dump_shaders`, `debug.texture_dump_directory`, `debug.texture_log`, `debug.gl_debug`, `debug.texture_no_cache` | off | `HALO_GPU_STATS`, `HALO_GPU_TRACE`, `HALO_GPU_TRACE_CONSTANTS`, `HALO_GPU_DUMP_SHADERS`, `HALO_TEXTURE_DUMP`, `HALO_TEXTURE_LOG`, `HALO_GL_DEBUG`, `HALO_TEXTURE_NO_CACHE` | Tools to find problems in the graphics: counts for each frame, all the GL state of one frame, the GLSL code, the textures. |
| `debug.discord_log` | `false` | `HALO_DISCORD_LOG` | Log each activity sent to Discord, without the invite. |
| `debug.menu_open` | `""` | `HALO_MENU_OPEN` | Start on this screen of the menus (`main_menu/settings_select/...`, as `port/assets/menus` names it), a player profile being edited, to look at it. |
| `debug.gpu_skip_vertex_shaders`, `debug.gpu_debug_expression`, `debug.gpu_debug_flat`, `debug.gpu_debug_texture0` | off | `HALO_GPU_SKIP_VS`, `HALO_GPU_DEBUG_EXPR`, `HALO_GPU_DEBUG_FLAT`, `HALO_GPU_DEBUG_T0` | Tools to find problems in the graphics: skip the draws of a vertex shader, or replace the output of all pixel shaders with a GLSL expression (for example `t0.rgb`). |
| `debug.network_test`, `debug.network_test_start`, `debug.network_test_kill`, `debug.network_test_score`, `debug.network_test_shoot`, `debug.network_test_vehicle`, `debug.network_test_pickup`, `debug.network_test_pickup_weapon`, `debug.network_test_fall`, `debug.network_test_bomb_in_goal`, `debug.network_test_bomb_throw`, `debug.network_test_bomb_keep`, `debug.test_input`, `debug.network_test_aim`, `debug.network_test_watch`, `debug.network_test_cheat`, `debug.network_test_public` | off | `HALO_NETWORK_TEST`, `HALO_NETWORK_TEST_START`, `HALO_NETWORK_TEST_KILL`, `HALO_NETWORK_TEST_SCORE`, `HALO_NETWORK_TEST_SHOOT`, `HALO_NETWORK_TEST_VEHICLE`, `HALO_NETWORK_TEST_PICKUP`, `HALO_NETWORK_TEST_PICKUP_WEAPON`, `HALO_NETWORK_TEST_FALL`, `HALO_NETWORK_TEST_BOMB_IN_GOAL`, `HALO_NETWORK_TEST_BOMB_THROW`, `HALO_NETWORK_TEST_BOMB_KEEP`, `HALO_TEST_INPUT`, `HALO_NETWORK_TEST_AIM`, `HALO_NETWORK_TEST_WATCH`, `HALO_NETWORK_TEST_CHEAT`, `HALO_NETWORK_TEST_PUBLIC` | Automatic tests of system link (`game/network_test.c`). Refer to `NETCODE.md`. |
| `debug.chat_test` | `""` | `HALO_CHAT_TEST` | A script of text chat for the tests (`game/chat.c`): `"<seconds>:<what>[:<text>]"` steps separated by `;`, from when this copy is in a game: `all` and `team` say a line, `server` says one as the host's server, `mute` and `unmute` mute a machine as a moderator does (the host), `localmute` mutes one for this copy, `log` and `close` open and close the log, `type` shows a line being typed. |
| `debug.touch_targets` | `false` | `HALO_TOUCH_TARGETS` | Outlines the tap targets of the menus (item green, value blue, list slot yellow, legend button red, the band beside the slots of a list orange, keys of the on-screen keyboard white), marks where the last finger went down and the last tap landed for 3 seconds, and logs each tap with the target that it hit (for a value, also where it splits into previous and next): to judge the accuracy of touch. |
| `debug.solo_game` | `false` | `HALO_SOLO_GAME` | A system link or split screen game can start with one player, alone on this machine: to test multiplayer maps without a second machine. |
| `debug.network_latency`, `debug.network_loss`, `debug.network_corrupt`, `debug.network_corrupt_stream`, `debug.network_corrupt_after` | `0` | `HALO_NETWORK_LATENCY`, `HALO_NETWORK_LOSS`, `HALO_NETWORK_CORRUPT`, `HALO_NETWORK_CORRUPT_STREAM`, `HALO_NETWORK_CORRUPT_AFTER` | The game holds all the data that it receives for this number of milliseconds, ignores this percentage of the datagrams, and damages this percentage of the datagrams it receives, and this percentage of its reads of streams, at random (bytes changed, cut short, stretched or replaced), from this many seconds after the start. Use the first two to test the netcode as on the internet, and the others to test that nothing another machine sends can crash the game (a damaged stream is closed, so a little goes a long way; a host's messages to its own client are damaged too, so start damaging once the game has started). |
| `debug.log_addresses` | `false` | `HALO_LOG_ADDRESSES=1` sets `true` | `true`: the internet addresses of other machines are written whole in `debug.txt` and the console, to debug your own network. `false`: each public address is written as a tag (`addr#3f2a9c`) that matches only within one run; local network addresses are always whole. Do not post a `debug.txt` written with this on: it has the address of everyone you played with. |
| `debug.voice_test` | `false` | `HALO_VOICE_TEST` | Automatic tests of voice chat: a tone replaces the microphone, and each voice that the game hears is written to the log once each second. |
| `debug.telnet_console`, `debug.telnet_console_port` | `false`, `2323` | `HALO_TELNET_CONSOLE`, `HALO_TELNET_CONSOLE_PORT` | The game listens on 127.0.0.1, on this port, for a script console (connect with telnet). The console has no password, so only this computer can reach it. |
| `debug.profile_record`, `debug.profile_record_when`, `debug.profile_memory` | `false`, `"start"`, `256` | `HALO_PROFILE_RECORD`, `HALO_PROFILE_RECORD_WHEN`, `HALO_PROFILE_MEMORY` | Profiling builds only (`configure.py --profile`). Record without a command; `"start"` records from the first frame until the first map change, `"game"` records each game outside the main menu. Memory is 4–1024 MB. Numbered `profile_<stamp>_<role>.part<n>.json` files remain in `profiles/` after recording. Refer to "Profiling builds" in the main README. |
| `display.screen_width`, `input.touch_controls`, `input.touch_aim_assist`, `debug.sample_seconds`, `debug.memory_watch` | `0`, `"on"`, `true`, `0.0`, `true` | `HALO_SCREEN_WIDTH`, `HALO_TOUCH_CONTROLS`, `HALO_TOUCH_AIM_ASSIST`, `HALO_SAMPLE`, (none) | Android only: refer to [port/android/README.md](../android/README.md#settings). |


With Mesa drivers, the game sends its GL calls through the GL thread of
Mesa. To stop this, set the environment variable `mesa_glthread=false`.

## Updates

The builds from GitHub Actions (refer to the main [README](../../README.md#download))
can update themselves. At start-up, the game asks GitHub for the latest
release of the repository it was built in (a fork's builds, the fork's:
`HALO_UPDATE_REPOSITORY`, from `tools/ci_build.py`). The game does not wait for the answer. If the latest release is not
newer, the game does nothing.

If the latest release is newer, the game asks: "Do you want to update?"

- Select "Yes" to update. The game downloads the release for this platform,
  replaces its files and starts the new version. The old files get the
  extension `.old`. The new version deletes them.
- Select "No" to continue. The game asks again at the next start.
- Select "Do not ask again", then "Yes", to stop the questions. The game
  writes `auto = false` in the `[update]` section of `config.toml`. To get
  the questions again, set `auto = true`.

Under gamescope (the Game Mode of the Steam Deck), the game does not ask,
because a system dialog stops the game there. The game writes the new
version in the log. To update, start the game from the desktop, or set
`update_answer = "yes"` in the `[debug]` section of `config.toml`.

The game downloads through HTTPS. It examines the certificate of the server
against the certificate authorities of the system: on Linux, the bundle of
the distribution (`src/posix_update.c`, with Mbed TLS); on Windows, the
certificate store of Windows (WinHTTP). The folder of the executable must
let the game write to it.

On Linux, the downloads of the updater and of the map archives use Mbed TLS
in turn with the game list's requests. They hold the lock of the requests
only while they use Mbed TLS: while a download waits for the server, the
game list can make its requests. Mbed TLS itself is built with its own locks
(`MBEDTLS_THREADING_C`, `tools/linux_build.py`), because the DTLS of
internet play with browsers (`src/posix_dtls.c`) uses it on the thread of
the game at the same time, and does not wait for the requests.

The game installs only files that the release key signed. First, it reads
the list of update files of the release (`updates.json`) and examines the
signature of the list (`updates.json.sig`). Then it downloads the file that
the list gives for this platform and examines its size and SHA-256 against
the list. A release from before version 0.8.0 has no list. For such a
release, the game examines the signature of the zip (`<zip>.sig`).

This fork's builds (Fractum Halo, FractumSeraph/OpenCE) look for updates in
the fork's releases, which the fork's own release key signs (the release job
of `.github/workflows/build.yml`, with the `UPDATE_SIGNING_KEY` secret; its
public half and its recovery key's are in `update_key.h` and
`UpdateManifest.java` beside ChupathingyCE's). A fork build from before
2026-10-16 knows only ChupathingyCE's key, so it cannot install the fork's
releases: install a newer one by hand once.

Builds that you make yourself have no build number. They do not look for
updates.

## Frame rate

The game calculates its world at 30 Hz, as on the Xbox. On the Xbox, the
game showed one frame for each calculation (tick). This port shows one frame
for each refresh of the display, for example at 60, 120 or 240 Hz.

Each frame shows the world between the last two ticks
(`game/render_interpolation.c`):

- After each tick, the game keeps the camera, the position of each part of
  each object, and the first-person weapon.
- Each frame mixes the last two ticks. The mix agrees with the time since
  the last tick.
- Rotations use quaternions. Positions and scales are linear.
- A teleport, a respawn or a cut of the camera does not mix. It jumps.
- After a long frame (several ticks in one frame), the camera mixes the
  last tick only, as the objects do.
- In cinematics, a camera that moves with an object (the lifepod in a30,
  a Pelican) moves with the object as it is drawn.

Thus the frames are one tick (33 ms) after the calculation. The calculation
does not change.

The direction of the view is an exception. The game reads the mouse and the
sticks in each frame. In first person, on foot, each frame points the view
where the player aims at that time (`display.direct_camera`). Thus the view
turns in the frame that the mouse moves. In a vehicle and in cinematics, the
view mixes as the other things do. On Android, the view mixes as before.

To get 30 frames each second, set `display.interpolation = false`.

To see the frame rate:

1. Push \` to open the developer console.
2. Enter `display_framerate true`.

In the game of another host, the console runs only the commands that change
nothing of the game (such as `display_framerate`), and the game puts back
cheats, the game speed and the settings of the drawing that show more of
the world (such as `rasterizer_wireframe`). Refer to `NETCODE.md`.

The frame rate shows at the bottom right of the screen. It is the mean over
half a second.

## Field of view

Video Setup's FOV AND VIEWMODELS screen sets the field of view of the
first-person view (`display.fov`) and of the weapon and hands
(`display.viewmodel_fov`), from 80 to 150 degrees in steps of 5, and can
hide the weapon (`display.viewmodel_visible`). DEFAULT, the setting's `0`,
keeps the stock view. `config.toml` takes any angle from 20 to 150.

- An angle is across the screen at 16:9. Another shape keeps the same
  angle up and down, as the stock view does.
- The field of view applies on foot in first person only. Vehicles, death,
  cinematics and scripted cameras keep their own view.
- A scope's zoom levels keep their stock view: the extra width fades out as
  the zoom comes in. A view narrower than the stock one narrows the zoom
  too.
- The reticles scale with the view, so that they stay on the aim. The
  scopes' pictures keep their place.
- By default the weapon keeps its stock view when the world is wider:
  arms and a gun right against the camera stretch at a wide angle.

LEGS (`display.first_person_legs`) draws your own body in first person
from the waist down, so you see your legs when you look down, walking,
running and crouching as everyone else sees them. It is the body others
see, posed as they see it: the upper body is folded away, and the body is
set back behind the eye as far as keeps the open waist below the view
(the way Halo 2 keeps its first-person body's open top out of sight), more
the further down you look. It is drawn with the world's camera, so it keeps
its place and size at any field of view. Hit boxes, markers and everything
the game does with the body are unchanged.

Only this machine's view changes. Nothing the machines send each other
changes, so players with different settings play together.

## System link

The Xbox game lets 16 players on 4 machines play a system link game. This
port lets up to 128 players on up to 128 machines play. Each machine can
have up to 4 players (split screen).

- `include/halo_port_limits.h` sets the limits.
- `include/halo_port_capacity.h` sets the memory for the limits. The game
  state is 16 MB at `0x81A00000` (3.3 MB on the Xbox). The pools of objects,
  effects, particles, contrails, lights and sounds are also larger.

Obey these rules:

- All the machines in a game must use a build with the same limits.
- The port uses protocol version 2. It does not see the Xbox game or older
  builds of the port. They do not see the port.

These are the differences from the Xbox:

- The host waits up to 60 seconds (15 seconds on the Xbox) for the other
  machines to load the map.
- If a machine does not read the messages of the host for two seconds, the
  host removes it from the game.
- The saved games contain all the game state. Thus a saved game is 16 MB.
  Saved games from older builds of the port do not operate.
- In campaign and in games of up to 16 players, the game removes garbage
  (bodies, dropped weapons) as on the Xbox. In larger games, it keeps more
  garbage, in proportion to the players.
- The lobby shows the local machine and the first three remote machines.
  The other machines are also in the game.
- In free-for-all games, each player is a team.

Linux, Windows and Android machines can play in the same game. Each machine
simulates the players from the same inputs, and the host does not correct
all of the game. Thus each machine must calculate the same floating-point
results, and all the ports:

- Compile without fused multiply-add (`-ffp-contract=off`).
- Use the math functions of musl (`port/include/halo_math.h`,
  `port/third_party/musl-math`), not the math functions of the system.

### Play on one computer

More than one copy of the game can play on one computer. Each copy must
have a different loopback address. A copy with an address gets no
broadcasts. Thus each copy must send its broadcasts to the other copies.

For a host and two clients, enter these commands in three terminals:

```sh
HALO_NET_ADDRESS=127.0.0.200 HALO_NET_BROADCAST=127.0.0.201,127.0.0.202 build/linux/halo
HALO_NET_ADDRESS=127.0.0.201 HALO_NET_BROADCAST=127.0.0.200 build/linux/halo
HALO_NET_ADDRESS=127.0.0.202 HALO_NET_BROADCAST=127.0.0.200 build/linux/halo
```

Do not give 127.0.0.1 to a copy. Each copy gets to its own address through
127.0.0.1. Linux and Windows send all of 127.0.0.0/8 to the loopback
interface.

### Test with many machines

`tools/system_link_bots.py` adds simple machines to a game. Each machine has
one player. The machines obey the system link protocol, but they do not
calculate the game or move their players.

1. Start a game on the host.
2. Enter `python tools/system_link_bots.py --host 127.0.0.200 --machines 127 --start`.

Each machine uses its own loopback address, from 127.0.0.2. The option
`--start` starts the game when all the machines are in the lobby. If the
host has no `network.address`, do not give `--host`.

## Internet play

Machines with an invite link can play system link on the internet. This
project has no server.

When a copy of the game starts to host a system link game, it makes an
invite link: `halo://join/<64 hexadecimal digits>`. The game writes the link
to the standard error and puts it on the clipboard. Each run of the game
makes a new link, unless `network.lasting_invite` is `true`. The links of older
versions of the game (44 digits) do not operate. The game writes a message
when it gets one.

To join a game, do one of these steps:

- Open the link. The game is the handler of `halo://` links. If the game
  already operates, the new copy gives the link to it and stops. A key in a
  file that only the user can read (`halo-ce-universal.key` in
  `$XDG_RUNTIME_DIR`, else `~/.halo-ce-universal.key`; on Windows in
  `%LOCALAPPDATA%`) encrypts the link, so the programs of other users cannot
  read it.
- Copy the link (or the 64 digits) and go to the game.
- Enter `halo <link>`.
- Accept a Discord invite. Refer to "Discord".

The game also takes the address of the web build's page with an invite
(`https://halocombatevolved.com/?join=<64 digits>`), as a link.

When the machines connect, the game of the host shows in Multiplayer,
System Link. Join the game as on a local network. System link on a local
network does not need an invite.

### Browsers

The web build ([port/web/README.md](../web/README.md)) plays internet games
with the Linux, Windows and Android builds: it hosts and joins by invite, and
the server browser lists its public games and theirs. A browser has no UDP
socket, only WebRTC, so a native build takes a browser's WebRTC on the port
of its tunnel (`network.tunnel_port`), as little of it as one data channel
needs (`src/p2p_webrtc.c`):

- ICE: the native build is an ICE-lite agent. It answers the checks of the
  browser, and the browser chooses the path. The credentials come from the
  secret of the session, which both machines have. Until the browser
  connects, the native build sends STUN indications to the addresses of the
  browser, which open its NAT to the checks of the browser.
- DTLS 1.2 (`src/posix_dtls.c`, Mbed TLS: `port/third_party/mbedtls`): the
  native build is the server, with a certificate that it makes when it
  starts (ECDSA P-256). Each machine sends the SHA-256 of its certificate in
  its signalling messages, which the token seals and the proof of the
  session authenticates; the other machine accepts only that certificate.
- SCTP, over DTLS: the browser opens the association, and one data channel,
  agreed before (unordered, without retransmissions), carries the packets of
  the tunnel. They are sealed as on UDP, and KCP makes the streams reliable
  as on UDP. A packet that is lost stays lost. The native build gives up its
  own lost packets after 300 ms (FORWARD-TSN), and the lost packets of the
  browser after 200 ms if the browser has not given them up first: the SCTP
  of Chromium and Firefox (dcsctp) can stop doing so on a path that loses
  packets, Firefox's before Firefox 158 in particular when the first packet
  of the connection is lost, and the connection would then stop.
  When a connection does not flow (the native build gives up a packet of
  the browser for it, or the browser acknowledges nothing), the log says
  so once, with the state of SCTP.

Two browsers connect to each other with their own WebRTC. There is no relay:
as between native builds, two networks that do not let a direct connection
through (some company and school networks block WebRTC) cannot play
together.

### Voice chat

Network games have voice chat, on the local network and on the internet.
Hold V (`controls.push_to_talk`, Settings > Controls Setup) to talk, or set
VOICE CHAT in Settings > Audio to OPEN MIC. VOICE VOLUME sets the volume of
the other players. On Android, set VOICE CHAT to OPEN MIC to talk: the game
then asks for the microphone.

OUTPUT DEVICE and INPUT DEVICE in Settings > Audio select the speakers and
the microphone (SYSTEM DEFAULT, the default, follows the system). The list
has the devices that the computer had when the game started.

The host sets voice chat for all players in Server Setup > Teamplay Options
(`network.voice_*` in `config.toml`):

- In the lobby, before and after a game, all players hear all players
  (LOBBY VOICE CHAT).
- During a game (VOICE CHAT): OFF; TEAM NEAR; ANYONE NEAR; TEAM; or TEAM,
  ENEMIES NEAR (the default). Near is VOICE NEAR DISTANCE (45 metres is the
  default), and both players must be alive. A player who is near sounds
  quieter when further, and from their direction. In a game without teams
  all players are enemies; in co-op, all are teammates.
- The quality (VOICE QUALITY), 8 to 64 kilobits per second (24 is the
  default), in the lobby and in the game.

In co-op, Server Setup has no Teamplay Options: the same settings are in
Server Setup > Voice and Voting.

A speaker shows next to the name of a player who talks: in the lobby, on
the scoreboard, in a list at the left of the screen during a game, and
beside the name above their head (when names are shown there:
`display.player_names`). To mute a player, open the scoreboard,
right-click, click the player, and click Mute voice. You no longer hear
them, and nobody else is told.

The host sends each voice on to the players who can hear it. The host
accepts a voice only from the machine of the player (with a key that it
gives each machine on its connection), only at the quality it set, at most
50 frames each second from a machine, and from at most 8 players at a time.
The voices are compressed with Opus (`port/third_party/opus`).

### Server browser

Server Setup in Create Game > Internet has a LISTING row:

- PUBLIC (the default): the game also shows in Join Game > Server Browser
  on every machine. Anyone can see and join the game.
- PRIVATE: only players with the invite link can join.

Each new game starts as PUBLIC (`network.host_public = false` makes new
games start as PRIVATE). An online co-op game starts as PRIVATE, and keeps
the last choice of its LISTING (`network.coop_public`). A LAN game is never listed. `network.public_lobby = false` turns the server browser off.

A PUBLIC game can also have a PASSWORD (a row of Server Setup, below
LISTING). The Server Browser shows a lock at the left of a game with a
password. A player who selects that game must type the password, and JOIN
GAME joins only with the correct password. The invite link of the game joins
it without the password. The host keeps the password only while the game
runs.

In the Server Browser, select a game to join it. The game joins the invite
of the game, as for a link. When it reaches the host, it opens the lobby.
If it cannot reach the host in 30 seconds, it marks the game FAILED. A game
that is full or starting shows CLOSED. REFRESH asks the hosts for their
listings again.

How it operates (`src/p2p_lobby.c`):

- The host of a public game publishes a listing: the invite, and the name,
  map, gametype and player counts of the game. The listing goes to the
  same MQTT brokers as the invites (`network.brokers_file`), retained,
  to a topic of the host (`hceu/3/lobby/s/<hash of its key>`).
- The key of the host signs the listing (Ed25519). The key is the key of the
  invite, so no other machine can list the invite of the host, change its
  listing, or list a game with false details.
- The host publishes the listing again every 30 seconds, when the game
  changes, and when a browser asks. If the topic of the host is empty or
  holds another listing, the host publishes again in 5 seconds or less. Thus
  a broker that deletes the listing does not remove the game.
- When the host stops (it stops hosting, the game becomes private, or the
  game quits), it publishes a closed listing, and then empties its topic.
  If the host loses its connection, the broker empties its topic (the will
  of the connection). A browser removes a game that it does not hear for 90
  seconds.
- When a public game becomes private, the host makes a new invite. Thus a
  player who saw the listing cannot join with the old invite.
- The listing of a game with a password does not hold the invite in clear
  text. The secret part of the invite (its token) is encrypted with a key
  from the password (Argon2id, salted with the key of the host, then
  XChaCha20-Poly1305). The browser makes the key from the password that the
  player types, and opens the invite only if the password is correct. When
  the host sets or changes the password, it makes a new invite. A player who
  has the listing can try passwords on their own machine without the host,
  so use a long password.

A public game does not publish the address of the host. But any machine
with the invite can ask the host to connect, and the host then sends its
addresses. Thus anyone can learn the address of the host of a public game,
as for any public server.

The brokers are in `brokers.txt` next to the executable (from
`port/assets/network/brokers.txt`; on Android, the app writes it next to
`config.toml` at each start), one `host:port` on each line. The game uses
all of them at once (up to 5), so one that works is enough. Without the
file (the dedicated server's container, the macOS application, whose
`config.toml` is in Application Support), the game uses its own copy of the
list. An update replaces `brokers.txt`: to use brokers of your own, put them in another
file and name it in `network.brokers_file`. All the players must use the
same broker to see each other's games. The game uses
MQTT 5 if the broker has it, else MQTT 3.1.1. A broker that does not keep
retained messages, or does not let clients subscribe with wildcards, carries
only invites, not listings.

ChupathingyCE's own parts of the server browser:

- A game hosted from the Xbox menus (System Link, or Online Games) is an
  internet game too, and public as `network.host_public` says, so it shows
  in the Server Browser of OpenCE and ChupathingyCE. The dedicated server
  (`server/`) is public unless `HALO_DEDICATED_PUBLIC=false`.
- Builds with the game list (`configure.py --game-browser`) also show the
  games of `network.browser_url` (halo.milenko.org). While the Server
  Browser is open, the game reads the list with an HTTPS GET of
  `/v1/games.txt` (at most every 5 seconds), and shows the games of its
  network version, not its own. A game that is also listed on the brokers
  shows once, with its listing (the same invite token). Joining a game of
  the list joins its invite, as for a link.
- A game on a Halo PC map, listed as `<map>@ce` (`@md`, `@pc`), shows PC
  (MD) after the map's name. It can be joined only with the map in its
  folder (`maps_ce/`, `maps_md/`, `maps_pc/`),
  on a build that plays Halo PC maps (`HALO_CUSTOM_EDITION`); otherwise
  the Server Browser says what is missing (`game/server_browser.c`).
- Column titles sort the games (players, name, map, gametype, ping). Select
  a title again to sort the other way. The chosen game stays chosen while
  the list changes. The lines below the rows show the players (by name,
  when the host sends a roster) and the rules of the chosen game.

### Map sharing

When you join a game on a Custom Edition map that you do not have, the game
downloads the map from the players of the game, if the host shares its maps
(SHARE MAPS in Server Setup > Map Sharing, `network.map_share`, on by
default). This operates on a local network and
on the internet. It also downloads the resource maps that the map needs
(`bitmaps.map`, `sounds.map`, `loc.map`) if you do not have them. A line at
the top of the screen shows the progress. In the lobby, the game starts when
every player has the map. If the game already operates, you join it when
the download is complete. If no player sends any of the map for a minute,
you leave the game, and the main menu tells you why. If the downloaded map
cannot be played, the game deletes the files that it downloaded, and you
leave the game.

All the players help (HELP SEND MAPS in Settings > Network Setup > Map
Sharing, `network.map_seed`), as the peers of a torrent do:
the downloaded map comes in pieces from the host and from each player who
has it or has a part of it. The game checks each piece against its hash, so
a player cannot send a different map. A player who sends a bad piece is not
asked again. To keep the upload free for the game, the host sends at most
`network.map_share_upload` (1024 KB/s: UPLOAD LIMIT in Server Setup > Map
Sharing) and each other player at most `network.map_seed_upload` (256 KB/s:
UPLOAD LIMIT in Settings > Network Setup > Map Sharing). Set these lower on
a slow connection. DOWNLOAD MAPS there (`network.map_download`) turns
downloading off: you then leave a game whose map you do not have.

**This fork never renames, moves or replaces a map file.** Maps keep their
names. Since network version 29 (OpenCE's build-206), a game is on a map by
its name (`hugeass`), and says which version of it by the start of the map's
hash, so a game always loads the version of the host, whatever your copy is
called. (OpenCE's builds 189 to 201 renamed each map in `custom_maps`
`hugeass_0e2b3ee4.map` and named the game's map so, and build-206 gives
those names back; this fork did neither.)

- **Hosting.** The game names its map by its name (`hugeass`) and sends the
  start of its hash as its version; OpenCE's players (build-206 and later)
  and this fork's without the map download it. The host shares the map from
  any of its Custom Edition folders (`maps_ce`, `custom_maps`, ...).
- **Joining.** The game looks for the host's version: by the start of its
  hash (hosts of version 29), or by the checksum in its header (hosts of 28
  and before, which may name the map `hugeass_0e2b3ee4`): your own
  `hugeass.map`, or a version beside it named `hugeass_<hash>.map` (OpenCE's
  name, or one this game downloaded).
- **Downloading.** A map you do not have goes into `maps_ce` under its own
  name. When you have another version of it, the host's goes beside yours as
  `hugeass_<hash>.map`: yours is never touched, and stays the one you host
  with. A download that stops leaves a `.part` file there; the next download
  of that map continues from it.

Over the internet, the players of a game connect only to the host. To send
each other a map, a player answers the other players of the game (who have
its invite) as the host does, and a player who downloads connects to them in
the same way that it connected to the host. Map sharing uses port 5153 for
its connections. Refer to `NETCODE.md`, "Map sharing".

### Security

Only machines with the invite can find the game:

- Each copy of the game makes an Ed25519 key pair when it starts, and from
  it an X25519 key pair (Monocypher, `port/third_party/monocypher`). Its
  identifier is from the hash of its X25519 public key. The Ed25519 key
  signs the listing of a public game.
- The link contains a 16-byte hash of the public key of the host and a
  random 16-byte token. The identifier of the host is from the first 6
  bytes of the hash.
- The machines exchange their public keys and addresses through public MQTT
  brokers (`network.brokers_file`). The topics are HMACs of the token.
  A key from the token encrypts and authenticates the messages
  (`src/p2p_signal.c`, `src/p2p_crypto.c`). The host authenticates its answer
  with a key that only it and the player can calculate. Its public key must
  agree with the hash in the link. The hash is long, so no other machine can
  find a key with the same hash.
- Then the player shows in the same way that it has the private key of its
  public key. Only then does the host make a session for the player. Thus
  other machines with the invite cannot make sessions in the name of a
  player (such a session would keep the player out).
- Each two machines get the keys of their packets from their key pairs and
  a random number from each. The keys do not go through the brokers. Thus
  other machines with the invite cannot read or change the packets.
- Each packet is encrypted and authenticated, with a different key in each
  direction. A machine ignores a packet that it already received. To and
  from a browser, the packets also go through DTLS, with the certificates
  that the signalling messages named.
- A machine can send only to the ports of the game on the other machine.
- The host makes one session from each request of a player. If a person
  sends a copy of an old request again, the host ignores it. A player that
  must ask again sends a new request.
- The host tries to reach at most 8 new players at the same time. The
  other players ask again.
- The host answers a request that is not proven at most one time each
  second through each broker. It answers at most 20 of these requests each
  second, after a first 32. Each answer goes only through the broker that
  brought the request. Thus a flood of requests does not use much of the
  bandwidth of the host.
- The host does the key work of at most 20 requests each second from keys
  that it does not know, after a first 32. It keeps the key work of the
  last 256 keys. Thus the proof of a player does not need more key work. A
  flood of requests can make players join more slowly. A player asks again
  for 90 seconds.
- The host refuses the predicted movement of a player whose game runs
  faster than time (a speed hack). If the messages on the player's
  connection were also ahead for ten seconds, the host drops and bans the
  player: each player sees who in red on the console, and the host adds a
  line to `cheaters.txt` and `bans.txt` (beside `debug.txt`) with the
  address and hardware id of the player, and the Discord name and id that
  the game of the player told it, marked `(self-reported)` (a player can
  change these). The host refuses a machine whose address or hardware id is
  in `bans.txt`. If only the player's datagrams were ahead, the host does
  not drop the player, because another machine can send datagrams with the
  player's address: it adds an `unverified` line to `cheaters.txt`.
- The host can ban a player with `ban <player name>` in the developer
  console (Tab completes the name). Remove a line from `bans.txt` to unban.
  Refer to `NETCODE.md`. `kick <player name>` drops the player the same
  way, but keeps nothing: no line in `bans.txt`, and the player can join
  again at once. In co-op, `bringto` brings every player to the host.
- Players can vote to kick a player. The host turns this on or off with
  VOTE KICK in Server Setup > Teamplay Options (`network.votekick`; in
  co-op, Server Setup > Voice and Voting). Hold the scoreboard key, right-click to
  show the pointer, and click the name of the player. Then click **Start
  a vote to kick**. Other players vote in the same way, and see the vote on
  the scoreboard. `votekick <player name>` in the developer console does
  the same. The host also gets **Kick** and **Ban** in this menu: these do
  the same as the `kick` and `ban` commands (click **Ban** two times). The
  host counts the votes, and these rules prevent abuse:
  - The vote passes when more than half of the players vote for it, and at
    least two players. The player of the vote is counted, but cannot vote.
    Thus, in a game of two equal teams, one team cannot kick a player of
    the other team without help.
  - The host counts one vote for each internet address (for internet
    play, the real address of the player, not the address of the tunnel).
    Two machines at one address, or with one hardware id, have one vote.
  - To start a vote, a player must have played for
    `network.votekick_minutes` (5) on this server. To vote, a player must
    have played for 2 minutes (or less, if that setting is less). The host
    counts the time. When a player joins again, the time starts again.
    Players who cannot vote yet are not counted.
  - The host only accepts a vote that comes on the connection of the
    player, not a datagram, which another machine can send with the
    address of the player.
  - One vote runs at a time, for 45 seconds, with 30 seconds before the
    next vote. If a vote fails, the player who started it cannot start a
    vote for 5 minutes, and nobody can start a vote against the same player
    for 10 minutes.
  - Nobody can vote to kick a player of the host.
  - A player kicked by a vote cannot join again for
    `network.votekick_ban_minutes` (30), by address and hardware id.
  - A player who leaves during a vote against them is kept out the same
    way, as if the vote passed. While the vote runs, the host refuses the
    player if they try to join again. Thus nobody can avoid a vote by
    leaving.
  So that every player can be named, the host trims the spaces around a
  name and removes characters that draw as nothing. A
  letter with a mark is typed as the plain letter (`ban jose` for "José").
  A name with nothing left to type becomes "Player", and a name that another
  player already has gets a number ("Player 2"). The game refuses a profile
  name that is blank, and a multiplayer game refuses a profile whose name was
  made blank before this check.
- An invite operates while the copy of the game that made it operates.

### Connection

Each machine gets its public address from public STUN servers. Then the two
machines send packets to each other until the packets get through (UDP hole
punching). There is no relay.

Some networks give a different port for each destination (for example some
mobile and company networks). Two machines behind such networks cannot
connect. To connect, forward `network.tunnel_port` on the router of one of
the machines.

The game can ask the router to forward the port (UPnP,
`src/posix_upnp.c`, with `port/third_party/miniupnpc`):

- The host asks its router when a player uses its invite.
- A player that joins asks its router when it does not reach the host in
  5 seconds.
- The forwarded port is one more address that the machine gives to the
  other machine.
- The forward has a duration of one hour. The game makes it longer while
  it operates. When the game stops normally, it removes the forward. It
  does not remove the forward after a crash, or if a request to the router
  is still under way 3 seconds after the game starts to stop. Some routers only make forwards without a duration.
- When the game finds the router, it removes the forwards to this machine
  that have the description "Halo internet play" and that no copy of the
  game uses now (forwards that a copy of the game did not remove).
- UPnP does not help behind a second NAT, for example the NAT of a mobile
  network provider. Then the router has a private address, and the game
  does not ask.

To stop all UPnP requests, set `network.allow_upnp` to `false`.

In the game, each machine has an address in 100.64.0.0/10:

- `src/xnet.c` gives the datagrams that the bound UDP sockets of the game
  send to such an address to `src/p2p.c`. Other datagrams (of sockets that
  are not bound yet, or that are connected to the address) and the TCP
  connections of the game go through local sockets on 127.0.0.1 (or
  `network.address`). The traffic from the other machines comes to the
  game from local sockets too.
- `src/p2p.c` sends that traffic through one UDP socket. UDP datagrams go
  as they are. TCP connections go as KCP streams (`port/third_party/kcp`).
- The broadcasts of the game go to all the machines. Thus the game of the
  host shows on the other machines.

### Discord

If the Discord desktop client runs, Discord shows what the player does
(through the application of `discord.application_id`, whose name is what
Discord says is being played): the game type and the map ("Team Slayer on
Blood Gulch"), a team game's score, the network game's name and players,
the time since the game started, or the campaign's level and difficulty, or
the menus. The pictures are the game list site's, by URL. A host of a public
game (in the server browser, with no password) gets a Join game button: it
opens the game's page on chupathingy.org, which opens the invite
(`halo://join/`) in the game of the person who clicks it, Chupathingy or
OpenCE. Otherwise the buttons go to the project's site and Discord server.
Discord's own Ask to Join is not used: it works only between players of the
same Discord application. `discord.presence`, `discord.show_server_name` and
`discord.show_join` choose what shows; `debug.discord_log` logs each
activity sent (without the invite).

### Online Games and the profile

Builds with the game list (`configure.py --game-browser`) have Online Games
in the Multiplayer menu: the games on `network.browser_url`
(halo.milenko.org). The screen looks like the Xbox's menus, drawn by the
game itself in the menus' fonts (`port/assets/fonts`). Each game's map has a
tag for its family: XBX (the Xbox's maps), HCE (Custom Edition), HMD
(HaloMD) and GBX (Halo PC retail). A lock marks a game with a password, as
its public listing says. The ping column shows the round trip to a host
that this machine is connected to (a game that you join), else nothing:
nothing measures a host before you join it. Sorted by ping, such games come
last. The details show the map's picture from the menus, or from Halo PC's
`ui.map`. Without either, a stock map's picture comes from the site
(stats.chupathingy.org/art/maps), downloaded in the background only while
`network.online` is on and kept in the save root's `map_art` folder. Any
other map gets a card with its name in its family's color. RB (Link Profile)
shows only while this copy is not linked to a profile: linked on this copy
(kept in the save root's `game_list_profile.txt`), or a profile the site
has for this copy's player ID. Once linked, Start's prompt names the
profile. Each copy of the game has a player key in the save root
(`game_list_player.key`). The key confirms the player's lines in finished
games. The game sends the key only to an HTTPS server, or to a server on
this computer (`http://127.0.0.1`, `http://localhost`) for tests.

To link the game to a profile on the site, do one of these steps in Online
Games:

- Press Start. The game opens the profile page in the web browser, signed
  in as this player.
- Press RB, or C on the keyboard (Link Profile). Use this step where no web
  browser opens: Steam's Game Mode, a Steam Deck, a console. The game shows
  a short code and a QR code. On a phone or a computer, go to
  `<server>/connect` (the address that the game shows), sign in, and enter
  the code, or scan the QR code. Then the game asks "Connect this game to
  <name>?" (or "Move this game from <old name> to <name>?"). Press A to
  connect, or B to cancel. A code operates for two minutes, and the question
  for two minutes. Press RB (or C) for a new code.

You link once. The link stays until you change the save root or the player
key: the profile keeps the link, and the games the key played before it
count too. Linking by code needs no key import.

To use a key from another computer (or a backup of the profile's key) on
Linux, either copy `game_list_player.key` into the save root, or start the
game with the key's link as its argument: `halo 'halo://key/<64 hexadecimal
digits>'` (the profile page's "Copy key link"). The game asks before it
replaces its key. The file is the 32 bytes of the key. The game ignores a
key file that another user owns or that others can read, and then has no
key at all: `chmod 600 game_list_player.key`. The profile page's Install in
Game button needs a desktop that opens `halo://` links for the game
(it registers itself at start); on a Steam Deck's Game Mode, or in a
sandbox, use the argument.

Link Profile uses these requests to the server (`src/browser.c`, on the
thread of the game list): `POST /v1/connect/start` with the key (and the
name of the profile) gives `ok <code> <seconds> <token>`.
`POST /v1/connect/status` with the token, each 3 seconds, gives `pending`,
`confirm <name> [<old name>]`, `connected <name>`, `declined` or `expired`.
`POST /v1/connect/confirm` with the token and the answer gives
`connected <name>`, `declined` or `expired`. The QR code is from
`port/third_party/qrcodegen`.

## Map checks

The game reads a map's tags straight into memory and uses them as its own
structures: every pointer, count, index and enum in them is the map's, and
the game writes values into tags as it runs. So before anything reads a
map's tags, the port checks every tag against a schema of its group
(`game/tag_schema_*.c`, read by `game/tag_validate.c`), and each structure
BSP as it loads:

- Every block and every piece of data must lie in the tags (or the BSP) and
  overlap no other. Otherwise the game refuses the map.
- A block with more elements than the game has room for is cut to the
  maximum. A tag reference that is not a tag of the right group becomes
  none. So do an index past its block and an enum past its values (or they
  become 0, where the game cannot take none). A string gets its terminator.
  Values that the game sets as it runs are reset.
- Checks that the schema cannot express run last: the BSPs' and the models'
  graphs, vertex and index buffers, and indices into other tags.

Each correction goes to `debug.txt`. The game's own maps need none.
`build/linux/map_validate [--strict] map.map...` runs the same checks on map
files without the game, and `tools/test_linux_port.py` runs it on the maps
in `assets/maps`. `map_validate --fuzz <runs> map.map` changes a few words
of the tags at random in each run. The checks must not crash or hang, and a
map that they let through must need no more corrections.

When a Halo PC map is refused (its file cut short, its tags damaged, or
not a cache file of its family's version), the port also hashes the file
and says whether the file is the one it should be: a hashed name
(`<name>_<hash>.map`) says which, and in a game the host's version of the
map does. So the console and `debug.txt` say either that the file is
damaged or another version (delete it, and the next join downloads it
again) or that it is intact and the map itself is refused. A client joining
a game on such a map leaves it with that message in the main menu's error,
instead of the damaged disc error and a restart.

A map's scripts can call only the script functions that a map needs (the
allowlist in `hs/hs.c`). They cannot call the functions for files, the
saved state of the game, the console, debugging or cheats. A script that
calls one does not run. The developer console can call every function.

Defensive checks stay in the game code too. An index into a tag block, the
tags or a tag's data that is out of range gets zeros (`tag_empty_data` in
`tag_files/tag_groups.c`), not other memory.

A map's name must be its file's: the cache file slots are found by the name
in the map's header, so a map file whose header names another map (a
renamed one) is refused, not copied again for ever. The `loading.tga` a map
pack may put in the maps folder is read only if it is an uncompressed 24-bit
picture of 320 by 240, the loading screen's texture. No disc has it as a
file: the retail `default.xbe` holds the picture (the Halo ring and
LOADING...) as runs of 4-bit greys, and importing a disc
(`src/xiso.c`, `tools/xiso_extract.py`, which also takes it out alone with
`--loading-tga`) writes it as `maps/loading.tga`. Without one, the loading
screen's light sweeps over clouds the game makes (`interface/progress_bar.c`).

A checkpoint (`savegame.bin`, in the profile's folder) and a core are
images of the game state's memory: with the data arrays' pointers to their
elements, the objects' memory pool's blocks and the references to them, and
the caches' procedures. Before one is taken, each of those is checked
against what the game made at startup (`game_state_image_accept` in
`saved games/game_state.c`): an image that does not match (a damaged or
crafted file) is refused, and the level starts over.

## What operates

| Area | Status |
| --- | --- |
| Game code | All 466 C files of the game. The changes are in "Game source changes". |
| Graphics | Direct3D 8 on OpenGL 4.5 core through SDL3 (`src/d3d8_gl.c`). The port translates the NV2A vertex shaders and register combiners to GLSL; the fixed-function pipeline, which only the loading screen uses (`SetVertexShader` with 0), is a vertex shader of the port's, the positions transformed by the world, view and projection matrices before the draw. The loading screen also reads the front buffer (`GetBackBuffer` with -1), the frame before, whose blur makes its light's trails: a 640x480 render target that takes the middle of the frame once the loading screen has drawn it. It decodes all the Xbox texture formats. The vertex and index buffers come from a GL copy of the Xbox memory. |
| High-res HUD | The HUD is drawn from high-res assets: redraws at 8x the size of the maps' bitmaps (4x for the largest), in `port/assets/hud`. They cover the meters, counters, panels and their outlines, the motion sensor, reticles, waypoints and scopes, but no bitmap with English text. `tools/hud_assets.py` makes them from the SVG redraws (the weapons' reticles from those of the PC HUD sheet that match the Xbox's exactly, the sniper rifle's 2x and 10x set in Overpass), and the build embeds them in the executable. When the game uploads one of those bitmaps, `src/hud_hires.c` gives the high-res texture in its place, if the bitmap's pixels are those of the English maps: another language's maps keep their own. The game sizes and places the HUD from its tags as before. `display.high_res_hud = false` turns this off. |
| High-res text | The menus' and HUD's text is drawn with Cyborg Interface Gothic (`port/assets/fonts`, by fizzy, SIL Open Font License) in place of the maps' bitmap fonts, which are Interstate. `src/text_hires.c` rasterizes each glyph with stb_truetype (`port/third_party/stb`) at the resolution the game draws at, into an atlas that a placeholder bitmap of the game stands for. The game lays the text out from its font tags as before. The menus' titles (the screens' headers and the main menu's items) are pictures of text in the maps, so they are drawn as the high-res HUD is: `tools/title_assets.py` sets each one again in OpenCE, Roger White's public-domain Newtown respaced to match the maps' commercial title typeface (`tools/title_font.py`), at 4x the bitmap's size over its own plate or glow, each letter placed where the map's letter is, in `port/assets/titles`. The postgame carnage report's title is set over a hand-made SVG redraw of its panel (`port/assets/titles/svg`) instead. `display.high_res_text = false` turns it off. |
| Anti-aliasing | Off unless `display.anti_aliasing` is set (`src/d3d8_gl.c`, `src/xgpu_post.c`). FXAA (written in the port) and SMAA (`port/third_party/smaa`, MIT licensed, at its HIGH preset, compiled as GLSL) are passes over the 3D view of each window, after the lens flares and before the HUD and the menus (`render/render.c`). Their programs are built when the setting is chosen. Supersampling draws the render targets the size of the screen at two times the resolution in each direction, and the display blit scales them down. Multisampling draws the back buffer and its depth buffer into multisampled renderbuffers, and with them any target that is drawn together with one of them (a mirror's view, in the secondary target with the back buffer's depth buffer), so that the attachments of a framebuffer are all multisampled or none is. A target's pixels are resolved into its texture before something reads the texture (as a texture, or at the display blit). Visibility tests count samples, divided by the samples of a pixel. An alpha-tested surface (foliage, grates) covers the samples of a pixel in proportion to its alpha past the reference (`gl_SampleMask`, not on Android). |
| Sound | Xbox DirectSound on SDL3 audio (`src/dsound_sdl.c`): PCM and Xbox ADPCM, mixed at 48 kHz, with volume, pitch, mix bins, distance, stereo pan, occlusion and obstruction. There is no Doppler effect, no cones and no reverb. |
| Input | XInput on SDL3 (`src/xinput_sdl.c`): keyboard, mouse, gamepads with rumble, and the debug keyboard for the console. |
| Files | The Win32 file functions and the MSVC file functions on POSIX, with the translation of Xbox paths. |
| Threads | Threads, events, mutexes, critical sections, interlocked operations and alertable waits. |
| Memory | The port reserves the Xbox memory at `0x80000000`. Thus the game gets the fixed addresses that it expects. |
| Saved games | The Xbox `UDATA` layout, with SHA-1 signatures. |
| Networking | Winsock on BSD sockets. System link on a local network and on the internet. |
| Bink video | Not available on this build. The game skips the movies, although the disc extraction copies them to `bink/` (`d:\bink\*.bik`) for a build that plays them (macOS does). |

## How the port operates

### The compiler

`tools/linux_build.py` compiles the game with clang and these options, which
give the ABI of the MSVC compiler:

- `--target=i686-linux-gnu`: 32-bit x86.
- `-fms-extensions`: the MSVC extensions.
- `-fshort-wchar`: 16-bit `wchar_t`.
- `-malign-double`: 8-byte alignment of 64-bit members.
- `-fcommon`: tentative definitions, as in C89.

glibc gives only ISO C (`__STRICT_ANSI__`). Thus POSIX names, for example
`random`, do not conflict with the names of the game.

These files supply the MSVC functions that clang does not have:

| File | Contents |
| --- | --- |
| `include/halo_linux_prefix.h` | The first header of each file: the architecture macros of the SDK, MSVC `__inline`, SEH keywords, `__declspec(selectany)`. |
| `include/` | Headers that add MSVC names to the C runtime headers. |
| `port/include/xdk` | The Xbox SDK declarations. The compiler reads this folder after all the other folders. |
| `tools/linux_msvc_semantics.py` | Makes a header that declares each struct tag at file scope, as MSVC does. It also makes the header inline functions weak, as the COMDAT functions of MSVC. `game/msvc_comdat.c` gives one external copy of each. |
| `include/halo_linux_winsock_names.h` | Gives new names to the Winsock functions of the SDK. Thus they do not link to the glibc functions with the same names. |
| `include/halo_linux_source_fixups.h` | Declares the port's functions that the game's sources call. |

`tools/linux_link_check.py` stops the link if a weak reference has no
definition. Without this check, the linker gives the reference the address
0.

### The platform layer (`src/`)

- The files `posix_*.c` use glibc. The compiler uses the ABI of the host
  for these files, because some glibc structures have a different layout
  with `-malign-double`.
- The other files include the SDK declarations through `platform.h`. Thus
  the compiler examines each definition against the SDK prototype.
- `src/halo_linker_common.c` gives weak storage for some globals of the
  January link, and for `fast_ftol_C` and `main_crash`.
- `main/d3d_intimacy.cpp` reads a private structure of the Xbox Direct3D.
  The Linux build does not use this file. `src/d3d8_gl.c` gives
  `d3d_find_flipcount`.
- The build returns small structures and unions in registers
  (`-freg-struct-return`), as on Win32.
- The GPU driver cannot open the kernel's `trace_marker`
  (`src/posix_trace_marker.c`). SteamOS keeps kernel tracing on for its GPU
  performance captures (`gpu-trace.service`), and its Mesa then writes a
  marker for each traced driver function: on the Steam Frame, some 480,000
  writes a second, which took the game from the headset's 72 Hz to about
  50 frames a second. The Steam Deck runs the same service, but its Mesa
  (25.3, 32-bit and 64-bit) has no markers to write: there the refusal
  changes nothing (measured: the same frame times and power either way).
  `HALO_GPU_TRACE_MARKERS=1` lets the driver write them, to capture with
  gpuvis.

### Game source changes

Five files of the game have changes for clang. These changes do not change
the MSVC objects: a comparison of all 612 C objects showed no difference in
code or data.

| File | Change |
| --- | --- |
| `cseries/cseries.c` | The naked function `stristr` uses `[ebp+8]` and `[ebp+12]` for its parameters. |
| `bitmaps/bitmap_drawing.c` | `*((word *)p)++` is now `*(*(word **)&p)++`. |
| `rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.c` | `&(T *)x` is now `(T **)&x`. |
| `hs/hs.c` | Local prototypes that did not agree with `ai_script.h` are removed. |
| `units/vehicles.c` | The local prototype of `unit_update_animation` uses the type of `units.h`. |

`math/real_math.h` had a copy of `plane2d_from_points` that did not agree
with the function in `effects/decals.c`. clang used the copy, and parts of
levels were not visible. The copy now agrees with the function.

Other changes:

| File | Change |
| --- | --- |
| `scenario/scenario.c` | The BSP connection tables have names, not MSVC offsets. |
| `rasterizer/xbox/rasterizer_xbox_environment_fog.c` | A local pointer gets its value from the file-scope array with the same name. |
| `game/player_control.c` | The mouse aims the player on controller 1 directly. |
| `sound/game_sound.c` | The game calculates the obstruction of each sound one time for each tick, not for each frame. |
| `cseries/errors.c` | `debug.txt` stays open between lines. A line that is the same as the line before it is counted, not written again (`last line repeated N times`). At 64 MB, `debug.txt` becomes `debug.old.txt` and a new `debug.txt` starts. |
| `networking/`, `game/`, `interface/`, `bungie_net/network/` and the pools of objects, effects and sounds | The system link limits and the memory for them. |
| `game/`, `objects/`, `units/`, `networking/` | The distributed netcode. Refer to `NETCODE.md`. |
| `cache/cache_files.c` | When a map's tags load and unload, the port finds the bitmaps that the high-res HUD replaces (`game/hud_hires_tags.c`), and adds the tags of the menus to the menus' map (`game/menu_tags.c`). |
| `interface/ui_widget.c`, `interface/ui_widget_event_handler_functions.c`, `interface/ui_widget_game_data_input_functions.c` | The main menu is the PC version's from `port/assets/menus` (`display.menus`); the menus' widgets can call the port's functions (`game/menu_functions.c`) and send the PC version's custom activation event; the widgets' memory is 256 KB, not 16 KB; the main menu and Multiplayer clear co-op's controllers, so that a gamepad going back to controller 1 is not a controller unplugged; the lobby's split screen players leave alone, and one who quits in game is not joined to the next game (`interface/player_ui.c`); the PC menus' map lists choose the Halo PC maps of the menus' map list (`game/ui_map_list.c`), and a Halo PC campaign map is hosted as network co-op. |
| `interface/player_ui.c` | Fast setup opens the pregame screen through the port's menus (the lobby), as `interface/ui_widget.c` does. |
| `input/input_abstraction.c`, `game/player_control.c`, `game/players.c`, `game/player_queues_new.c`, `units/units.h` | The keyboard and mouse's actions (`src/xinput_sdl.c`, `include/halo_keyboard.h`) join controller 1's game controls; their reload key reloads on its own, and their action key only acts (a control flag of the port's, sent with the player's action, stops the reload the controller's X falls back to). |
| `sound/sound_manager.c`, `interface/hud.c`, `game/game_engine.c` | The music's and the other sounds' volumes; the HUD's and the scoreboard's settings are read again when Settings changes them. |
| `rasterizer/xbox/rasterizer_xbox_shadows.c` | With larger shadow maps (`display.shadow_resolution`), the blur's taps stay half a texel apart, and more passes widen it to cover the same part of the map as on the Xbox. |
| `rasterizer/rasterizer.c` | The 128 KB of vertices of the debug display of a model's vertices are static, not on the stack: the compiler inlines the function in `render_model`, and the Windows build probed the stack a page at a time for every model drawn. |
| `render/render.c` | The 3D view of each window is antialiased before the HUD is drawn (`display.anti_aliasing`). |
| `interface/hud.c` | In multiplayer, players' names are drawn above their heads (`display.player_names`, `display.player_name_scale`). |
| `interface/hud.c` | The seconds left on a player's powerups (`display.powerup_timers`, `game/powerup_timers.c`). |
| `interface/progress_bar.c` | The loading screen is made by the CPU (`game/loading_screen.c`) and put over the back buffer (`src/d3d8_gl.c`): the Xbox drew it from its front buffer, through a texture made from that buffer's address, with no vertex shader, which the port's Direct3D does not have. |
| `game/game.c` | A level's load holds the loading screen for `display.loading_screen_min_seconds`. |
| `rasterizer/rasterizer_text.c`, `text/draw_string.c` | Text is drawn from an atlas of the fonts' glyphs, rasterized at the resolution the game draws at (`src/text_hires.c`), when the font has every character of the string. Text can be drawn scaled about a point (`rasterizer_text_set_scale`), as the players' names are. Each glyph's advance is centred on the font tag character's, so the layout is the same, and a glyph is cut at a text box only where the font tag's character visibly was. |
| `interface/first_person_weapons.c` | The first-person hands are drawn with the unit's shield modifier shader, as its body is (`display.viewmodel_shield`). |
| `render/render_objects.c`, `objects/objects.c` | In first person, your own body is drawn from the waist down with the pose `first_person_legs.c` makes (`display.first_person_legs`); the waist it found and the mesh `object_mesh.c` read are forgotten on a new map. |
| `objects/object_lights.c` | Glows the port lights itself (`shield_glow.c`): dynamic lights attached to an object's node, colored and sized each frame by their maker, drawn with a light definition the map has. |
| `render/render_objects.c`, `interface/first_person_weapons.c`, `rasterizer/xbox/rasterizer_xbox_plasma_energy.c` | A local player's shield plasma is drawn in the chosen armor color (`display.shield_color`, `shield_color.c`), on the body and on the first-person arms. |
| `rasterizer/xbox/rasterizer_xbox_text.c`, `rasterizer/rasterizer_text.c` | The characters of a string are one draw, not a draw each (and a second for the drop shadow). The batch is drawn early before a character is cached and before the high-res glyph atlas starts over (`src/text_hires.c`). |
| `rasterizer/xbox/rasterizer_xbox_decals.c` | Decals drawn one after another with the same blend function and bitmap are one draw, with each decal's color in its vertices, not a draw each: a long fight leaves hundreds in view. Within a run of decals whose blend functions commute (multiply, add, min, max, reverse subtract), they are drawn by bitmap, so more of them share a draw. |
| `rasterizer/xbox/rasterizer_xbox_widgets.c`, `objects/widgets/light_volumes.c` | A light volume's sprites (a plasma bolt's glow, a couple of hundred) are one draw. |
| `rasterizer/rasterizer.h` | A frame holds 2048 transparent geometry groups, not 384 (`include/halo_port_capacity.h`): a co-op game with many added enemies asked for more, and the Xbox's pool drew the frame without the rest. |
| `structures/structure_render.c` | The structure's visible surfaces are put in order by a radix sort, not `qsort_4byte`: the same order. |
| `cache/cache_files.c`, `networking/network_client_manager.c`, `networking/network_server_manager.c`, `networking/network_server_message_handler.c`, `main/main.c`, `interface/interface.c` | Map sharing (`game/map_share.c`): a client that does not have the game's Custom Edition map asks the host for it instead of leaving, is not given time to precache it until it has it, and adds its players to a game in progress only then; the host gives the game's map its own name and its version (its hash's start), takes the requests from any machine in the game, lobby too, and does not drop a machine that downloads; the transfers run each frame, and their progress is drawn over the screen. |
| `cutscene/cinematics.c` | A scenario's titles (some Custom Edition maps show their name for the whole game) are not drawn over a menu, as the letterbox is not: the screen's overlays are drawn after the players' menus. |
| `tag_files/tag_groups.h` | `TAG_BLOCK_GET_ELEMENT` returns an element in the block without a call, and calls `tag_block_get_element_with_size` for anything else. |
| `interface/progress_bar.c`, `interface/ui_widget.c`, `rasterizer/xbox/rasterizer_xbox.c` | The loading screen's light sweeps over clouds the game makes when the maps folder has no `loading.tga`, and the rasterizer's caches of the device's states learn what the loading screen set (`rasterizer_forget_hardware_state`). The same screen covers the menus while a map downloads before its game is joined (`game/download_screen.c`, `maps.download_screen`): the menus go on behind it but take no presses, and B stops the download. |
| `input/input_xbox.c`, `physics/collision_usage.c` | The clock is read only for a button held, and for collision tests only when their times are shown: a read of the clock is slow in a browser. |

The x86 inline assembly of the game is replaced by C. Thus the compiler
can optimize that code for each processor:

| File | Assembly | Replacement |
| --- | --- | --- |
| `cseries/cseries.h` | x87 `fistp` (`fast_ftol`) | `__builtin_rint` |
| `bitmaps/bitmaps_inlines.h` | x87 conversions | C conversions |
| `math/matrix_math.c` | SSE `matrix4x3_multiply` | a C loop |
| `effects/decals.c` | an x87 conversion | a C conversion |
| `cseries/profile.c` | `rdtsc` | `QueryPerformanceCounter` |
| `cseries/cseries.c` | naked `stristr` | a C `stristr` |
| `cseries/stack_walk_windows.c` | a read of EBP | `__builtin_frame_address` |
| `interface/hud_draw.c` | a read of `[ebp+4]` | `__builtin_return_address(1)` |
| `bink/bink_playback.c` | `int 3` | `__builtin_trap` |

The x87 control and status words (`_control87`, `_statusfp`, `_clearfp` in
`src/msvc_crt.c`) use `fenv.h`. On Android, they use the FPCR and FPSR. In
the web build, the control word is only remembered.

The web build's changes (`HALO_WEB`: the main loop runs one iteration for
each of the browser's frames, the cache thread starts at a function of
`CreateThread`'s type, `debug.txt`'s lines go to the page's log) and the
declarations given their definitions' types, which WebAssembly needs, are
listed in "Game source changes" in [../web/README.md](../web/README.md).
