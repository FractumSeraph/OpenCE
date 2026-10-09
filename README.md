# OpenCE in the browser (FractumSeraph's fork)

This fork is [OpenCE](https://github.com/OpenCommunityEdition/OpenCE) with a
browser version added: the same game compiled to WebAssembly, playable in
Chrome, Edge, Firefox and on phones, online with other browsers and with the
Windows/Linux builds of OpenCE. The browser port comes from
[web-halo](https://github.com/ecumene/web-halo), merged onto OpenCE and kept
up to date with it. It runs at
[halo.fractumseraph.net](https://halo.fractumseraph.net/).

**Download it** (built from the latest `main`; no game data included):

| File | What it is |
| --- | --- |
| [halo-server.zip](https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-server.zip) | Everything to host it: the game, its server, the lobby service for online play, the gateway for native `halo://join` links, and Node.js, for Windows and Linux x64 in one folder you can copy between them. Unzip, put the game's Xbox `.map` files in `halo-server/public/assets/maps/`, and double-click `Start Halo (Windows).bat` (or run `./start-halo.sh`). On Windows, `server\windows\update.ps1` later updates the folder in place. |
| [halo-server-windows-x64.zip](https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-server-windows-x64.zip) | The same for Windows only (smaller). |
| [halo-server-linux-x64.zip](https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-server-linux-x64.zip) | The same for Linux x64 only. [HOSTING-VPS.md](services/selfhost/HOSTING-VPS.md) sets it up on a VPS with your own domain and HTTPS. |
| [halo-web.zip](https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-web.zip) | The game alone, to put on any web server. Without the lobby service there is no online play. |

The kits' instructions: [services/selfhost/README.md](services/selfhost/README.md).

The `main` branch here is OpenCE's `main` with the changes below merged in.
Everything after this section is OpenCE's own README, with a row for the
browser added to its tables and a [Browser build](#browser-build) section at
the end.

## What this fork changes compared with OpenCE

**The native builds are meant to be the same as OpenCE's.** Browser-only code
is inside `#ifdef HALO_WEB` (or in files only the browser build compiles), and
nothing sent over the network is changed: browser and native players share
games, on the same network version as OpenCE. This fork only builds and tests
the browser version, so a native build here has not been checked beyond
OpenCE's own CI.

### Added: the browser build

- **`ninja web`** builds `build/web/halo.html`, `halo.js` and `halo.wasm` with
  Emscripten (`tools/web_build.py`; `configure.py --web-cc PATH`). Threads
  (pthreads), WebGL 2 through the Android OpenGL ES code, maps read from the
  web server with byte ranges, saves and settings in the browser's private
  storage. The link fails on WebAssembly signature mismatches, which would
  otherwise crash at run time.
- **The browser platform layer** (`port/web/src`): storage and map mounting
  (`web_platform.c`), sockets carried over WebRTC between browsers
  (`web_loopback_net.c`), the page's hosting and invite flow
  (`web_online_ui.c`), the in-game server browser (`web_public_games.c`), and
  UPnP stubs (`web_upnp.c`).
- **Hosting from the game's own menus:** in the browser, Multiplayer > Create
  Game (Internet or LAN) opens a room of the page's for the server the menus
  made (`web_online_game_hosting`, `online_client.js` `hostFromGame`): its
  invite link shows beside the game and on Server Setup's INVITE LINK row.
  An Internet game whose LISTING is PUBLIC is listed, once its lobby opens,
  on the lobby service's list of public games (`GET /v1/public-rooms`; the
  host's page sends the game's listing over its room's connection), which
  the in-game Server Browser shows first; choosing one joins its room
  (`web_public_games.c`). A browser game has no password row.
- **The page** (`port/web/shell.html`, `online_client.js`,
  `library_web_transport.js`): loading screen, a "Play online" panel for the
  player's name and armor, a quick game (a map and mode picked there, up to
  128 players), and joining invites from
  any copy of the site, on any domain, or a native `halo://join` link. Without
  server-hosted maps, players can load the maps from their own Xbox disc image
  (`xiso.js`). The layout keeps its panels off the game on phones in either
  orientation.
- **Touch controls** for phones and tablets (`port/web/assets/touch`, after
  [Halo Mobile](https://github.com/OMG-Guest/Halo-Mobile)): a floating stick,
  aiming by dragging, every button, two layouts (modern or original Xbox
  controller), an on-screen editor, and the page's resolution choice in their
  menu. A connected gamepad hides them.
- **Installing as an app** (PWA): `port/web/manifest.webmanifest`,
  `port/web/assets/pwa`, and `coi-serviceworker.js`, which also provides the
  cross-origin isolation that threads need.
- **The in-game server browser** (Multiplayer > Join Game > Server Browser)
  lists the public games of native hosts. The site's server relays their
  signed listings from internet play's brokers, the game checks the
  signatures (`p2p_lobby.c`), and choosing one joins it through the native
  gateway. (The relay is part of the self-hosting server: `public-games.mjs`.)
- **Halo Custom Edition maps** in the browser, with OpenCE's Custom Edition
  support: the site serves them, with Custom Edition's resource maps, from
  `public/assets/custom_maps` (the server lists the folder for the game,
  `web_platform.c` mounts it), Create Game's map list and the page's quick
  game host them, and the Custom Edition tag cache's fixed addresses are kept
  free of the browser's allocator (`xbox_memory.c`). A map's `<name>.bmp`
  beside it is its picture, in the game's menus and on the quick game's
  card. The server's `custom_maps/index.json` gives each file's size and
  version, so listing a hundred maps or more reads only their headers.
- **Maps kept in the browser** (`fetch_path_normalization.js`): the pieces of
  the maps a player has loaded stay in the browser's Cache Storage, by each
  file's version (its ETag), so a map loads without a download the next time
  and even when the server cannot be reached; Game settings shows how much
  is kept and can clear it.
- **Voice chat in the browser** (OpenCE's: `network_voice.c`,
  `voice_audio.c`, Opus): the web build compiles Opus as the Linux build
  does, the microphone opens through SDL (the browser asks the player the
  first time; HTTPS only), voices travel over the browser's game
  connections like the game's own traffic, to other browsers and to native
  hosts, and the touch controls have a Talk button (push to talk: it holds
  V). The scoreboard's pointer (muting a player) works with a mouse.
- **No map torrents in the browser:** the native ports download missing
  Custom Edition maps over BitTorrent (docs/map_torrents.md), but a page has
  no TCP or UDP sockets, so the web build leaves the client out
  (`WEB_EXCLUDED_PLATFORM_SOURCES` in `tools/web_build.py`) and
  `port/web/src/web_map_torrents.c` stands in for it: the browser plays
  the Custom Edition maps its site serves, as with `maps.torrents` off.
- **ChupathingyCE's Delta** (`port/web/src/delta`, and its README): the
  browser joins hosts of OpenCE network versions 11 to 24 as ChupathingyCE's
  builds do, checks their signed legacy table, shows their game list's
  details in the Server Browser (`web_delta_list.c`), shakes hands with
  their hosts over Delta Peer (their `delta_peer.c` and `delta_wire.c`, as
  they are; `web_delta_peer.c`), leaves a Custom Edition game whose map file
  is not the host's, and reports the games it joins to halo.milenko.org
  (`web_delta_stats.c`, Play online's *Stats on halo.milenko.org*).

### Added: online services

- **`services/signaling`**: the lobby service for browser games (a Cloudflare
  Worker: rooms, invite tickets and WebRTC signalling; the self-hosting server
  runs it locally in Miniflare), with a list of browser-hosted public games
  (`GET /v1/public-rooms`): a host sends its game's listing on its room's
  connection with the room's guest ticket, the room checks it and hands it
  to the global presence object, and a listing lapses 90 seconds after its
  last refresh or at once when its host leaves.
- **`services/native-gateway`**: lets a browser join a native host's
  `halo://join` invite. It speaks OpenCE's internet-play protocol (`hceu/3`
  over the MQTT brokers, then the encrypted UDP tunnel), with OpenCE's own
  broker added to its list. Written in Rust.
- **`services/selfhost`**: the self-hosting server the kits run. One Node.js
  process serves the game (with byte ranges and the isolation headers), runs
  the lobby service in Miniflare, starts the gateway, relays the server
  browser's public games (`public-games.mjs`), asks ChupathingyCE's game
  list for the page (`delta-list.mjs`), hashes the Custom Edition maps for
  Delta's map check (`map-hashes.mjs`), and can add Umami analytics.
  It works under any domain or sub-path, and comes with launchers, a Windows
  autostart task and a VPS guide. `tools/selfhost_package.py` builds the
  kits.
- **`services/web`**, **`infra/aws-native-gateway`**, **`docs/telemetry.md`**,
  **`tools/halo_telemetry.mjs`**: web-halo's own hosting (Cloudflare, AWS) and
  telemetry, kept as they came.

### Changed: shared code

- **Inside `HALO_WEB`**, the browser's versions of:
  - **graphics** (`d3d8_gl.c`, `d3d8_resources.c`, `xbox_textures.c`,
    `memory_watch.c`, `gl.h`, `gl_functions.c`): WebGL 2 instead of desktop
    OpenGL, texture changes found by content checks instead of page
    protection, and Xbox colour order converted before upload;
  - **memory and CPU** (`xbox_memory.c`, `msvc_crt.c`): the Xbox address
    window inside WebAssembly memory, and the floating-point control word,
    which WebAssembly does not have, kept for the game to read back;
  - **platform** (`sdl_platform.c`, `xinput_sdl.c`, `dsound_sdl.c`,
    `port_config.c`): the frame driven by the browser, touch aiming, browser
    audio, and browser defaults for some settings (for example crouch on `C`);
  - **networking** (`posix_net.c`, `xnet.c`, `p2p.c`, `p2p_lobby.c`): sockets
    through the browser layer, invites handed to the page, the page's room's
    invite as the hosted game's, the hosted game's listing read for the
    page, and Delta's version range in the listings shown;
  - **menus** (`menu_functions.c`, `player_ui.c`): Create Game opens a page
    room, Server Setup shows its invite and LISTING (no PASSWORD), the server
    browser lists browser games first and joins through the page, shows
    Delta List's details, and the page's hosting takes a Custom Edition map;
  - **game code** (`source/`): a player re-sent to a browser client that
    missed it (`network_server_message_handler.c`,
    `network_client_manager.c`), Delta's join range and Delta Peer's client
    hooks (`network_client_manager.c`), a joined game's report for Delta
    Stats (`game_engine.c`), a fatal error ending the browser runtime
    instead of looping (`main.c`), and small fixes in the cache, saved-game
    and rasterizer code.
- **Outside `HALO_WEB`**, changes that do not alter what the game does:
  - the main loop split into one frame per call (`main_loop_iteration` in
    `main.c`), which the browser needs and the native loop calls in turn;
  - functions only the browser calls: a quick network-server setup with a
    chosen map and gametype (`player_ui.c`), and `main_campaign_in_progress`;
  - fixes that let the code compile for WebAssembly: missing prototypes and
    includes (`players.h`, `object_types.c` and others), and string tables
    stored as arrays (`saved_game_files.c`);
  - `configure.py`'s `--web-cc` option and the `ninja web` target.

### Added: repository files

- **Tools**: `tools/web_build.py` (the build), `web_serve.py` and `web_run.py`
  (a local server with the headers the page needs), `web_stage_cloudflare.py`,
  `xiso_extract.py` (maps from an Xbox disc image), and tests under
  `port/web/tests` and `tools/test_*.py`.
- **A workflow for the browser build** (`.github/workflows/web.yml`): every
  push builds it, runs the browser tests and packages
  [`halo-web.zip`](https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-web.zip)
  (`tools/web_package.py`), then builds the two self-hosting kits, each on
  its own platform (Windows and Linux). On `main` all three zips go on the
  `web-latest` release. OpenCE's own `build.yml` (the native builds and their releases)
  is unchanged. web-halo's workflows, which deployed its own services, are
  not included.

---

# Halo: Combat Evolved for Linux, Windows and Android

[![Join our Discord](https://invidget.switchblade.xyz/9gqcHyr5km)](https://discord.gg/9gqcHyr5km)

This project is a port of the Halo: Combat Evolved decompilation to Linux,
Windows and Android. The decompilation is of the Xbox build 2342
(`cachebeta.exe`, SHA-256
`4cc87b45f721270392a96f1674ed2b5cd4a7bb4355faeab4531d1cf1884d9520`).

<img width="1289" height="995" alt="The game on Linux" src="https://github.com/user-attachments/assets/0d3ad50f-f8b8-46cf-aef8-e3661da2a7d7" />

The port starts from the decompilation of [bnunu/halo-1](https://github.com/bnunu/halo-1).
That project is a fork of [punpckhdq/halo](https://github.com/punpckhdq/halo).

## Download

GitHub Actions builds the game for each commit. These links download the
builds of the latest release:

| Platform | Release | Debug |
| --- | --- | --- |
| Linux | [halo-linux-release.zip](https://github.com/OpenCommunityEdition/OpenCE/releases/latest/download/halo-linux-release.zip) | [halo-linux-debug.zip](https://github.com/OpenCommunityEdition/OpenCE/releases/latest/download/halo-linux-debug.zip) |
| Windows | [halo-windows-release.zip](https://github.com/OpenCommunityEdition/OpenCE/releases/latest/download/halo-windows-release.zip) | [halo-windows-debug.zip](https://github.com/OpenCommunityEdition/OpenCE/releases/latest/download/halo-windows-debug.zip) |
| Android | [halo-android-release.zip](https://github.com/OpenCommunityEdition/OpenCE/releases/latest/download/halo-android-release.zip) | [halo-android-debug.zip](https://github.com/OpenCommunityEdition/OpenCE/releases/latest/download/halo-android-debug.zip) |

Use the release build to play. The debug build stops at the first failed
assertion and writes it to the log. Use the debug build to find and report
problems.

The game updates itself. At start-up it looks for a newer release, and asks
if you want to install it. Refer to "Updates" in
[port/linux/README.md](port/linux/README.md#updates).

Each build of the `main` branch that passes on all three platforms is a new
release. The [Releases](https://github.com/OpenCommunityEdition/OpenCE/releases)
page keeps the last five releases. If the latest build has a problem, get
an older build from that page.

## Game data

The port does not include the game data. Download an Xbox disc image
(`.xiso` or `.iso`) of Halo: Combat Evolved. All versions of the game
operate. The maps of the European (PAL) version were made for a slower
console. The port changes them to play as the North American (NTSC) maps do,
so players of the two versions can play together.

1. Start the game.
2. At the first start, the game asks for the disc image. Select it.
3. The game extracts the `maps/` folder. Then the game starts.

On Linux and Windows, the game puts `maps/` next to the executable. On
Android, copy the disc image to the phone first. The app puts `maps/` in its
data folder. Refer to [port/android/README.md](port/android/README.md).

## Platforms

Each platform has its own instructions:

| Platform | Instructions |
| --- | --- |
| Linux (32-bit x86 executable, OpenGL 4.5, SDL3) | [port/linux/README.md](port/linux/README.md) |
| Windows (32-bit x86 executable, OpenGL 4.5, SDL3) | [port/windows/README.md](port/windows/README.md) |
| Android (arm64 app, OpenGL ES 3, SDL3) | [port/android/README.md](port/android/README.md) |
| Browser (WebAssembly, WebGL 2, SDL3) | [Browser build](#browser-build) below |

The Linux README also gives the controls, the settings and the multiplayer
functions. These are almost the same on all platforms.

## Multiplayer

The game can play system link games on a local network and on the internet:

- A system link game can have up to 128 players on up to 128 machines.
- Linux, Windows and Android machines can play in the same game.
- An invite link lets a machine join a game on the internet. No server of
  this project is necessary.
- The netcode is new. Each machine moves its own player at once,
  and the host makes the decisions for the game. Refer to
  [port/linux/NETCODE.md](port/linux/NETCODE.md).
- A Custom Edition map that a machine does not have is downloaded when
  it joins a game on it, over BitTorrent, from the other players and
  from seed boxes; the host seeds the map it plays. Refer to
  [docs/map_torrents.md](docs/map_torrents.md).

## Build the game

You do not need the Xbox SDK. The port supplies the SDK declarations that
the game uses. Refer to [port/include/xdk](port/include/xdk/README.md).

To build the game:

1. Install Python and [ninja](https://ninja-build.org/).
2. Install the tools for your platform. Refer to the README for the
   platform.
3. In the root folder of the repository, enter `python configure.py`.
4. Enter `ninja` with the target for the platform:

| Target | Result |
| --- | --- |
| `ninja linux` | `build/linux/halo` |
| `ninja windows` (on Windows) | `build/windows/halo.exe` and `SDL3.dll` |
| `ninja android_apk` | `port/android/app/build/outputs/apk/debug/app-debug.apk` |
| `ninja web` | `build/web/halo.html`, `halo.js` and `halo.wasm` (needs Emscripten: `configure.py --web-cc PATH`) |

If you enter `ninja` without a target, ninja builds the game for the
computer that you use.

`tools/ci_build.py` makes the same builds as GitHub Actions. For example,
enter `python tools/ci_build.py linux release`.

### Build options

Give these options to `configure.py`:

| Option | Result |
| --- | --- |
| (none) | A debug build. A failed assertion stops the game. |
| `--release` | A release build. The game does not examine assertions, as in the retail game. |
| `--portable` | The Linux and Windows builds operate on all x86-64 processors. The Linux build also operates on older distributions and on SteamOS: refer to "Portable build" in [port/linux/README.md](port/linux/README.md#portable-build). Use this option for builds that you give to other persons. |
| `--lto=thin`, `--lto=off` | Less link-time optimization. The link is faster. |
| `--pgo=off` | No profile-guided optimization. |
| `--pgo=train` | Records a new optimization profile. Refer to "Optimization profiles". |

Without `--portable`, the Linux and Windows builds use all the instructions
of the processor that builds them (`-march=native`). Such a build does not
always start on a different computer.

### Optimization profiles

The builds use profiles of the game to optimize the code:

- `pgo/halo_linux.profdata` for Linux and Android.
- `pgo/halo_windows.profdata` for Windows.

The profiles need clang 22 or later. With an older clang, the builds do not
use the profiles.

To record a new profile:

1. Delete the profile.
2. Enter `python configure.py --pgo=train`.
3. Enter `ninja linux` or `ninja windows`.

The build then plays the main menu and the first minute of each campaign
level. This procedure continues for approximately 15 minutes. The game
data must be in `assets/`.

## Browser build

The browser build is the same game and platform layer compiled to
WebAssembly with Emscripten (`tools/web_build.py`; `HALO_WEB`, which also
selects the Android OpenGL ES code paths). It comes from the web-halo port
(github.com/ecumene/web-halo) merged onto this repository, with:

- touch controls for phones and tablets (`port/web/assets/touch`, after Halo
  Mobile's), aiming through the mouse path (`platform_web_touch_look`);
- installing as an app (`port/web/manifest.webmanifest`, `port/web/assets/pwa`);
- online games through a lobby service (`services/signaling`) and WebRTC,
  and joining native hosts' `halo://join` invites through
  `services/native-gateway`, which speaks this port's internet-play protocol.

Build it with Emscripten 6:

```
python configure.py --release --web-cc /path/to/emsdk/upstream/emscripten/emcc
ninja web
```

The page needs cross-origin isolation (COOP/COEP headers) and the maps
beside it in `assets/maps/` (served with byte ranges), or the player chooses
an Xbox disc image in the browser. `tools/web_serve.py` serves a checkout for
development. The link refuses WebAssembly signature mismatches
(`-Wl,--fatal-warnings`): a C function called through a prototype that does
not match its definition traps in a browser.
