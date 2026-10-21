# Halo in the browser and on every platform (FractumSeraph's fork)

**Play it now in your browser: [halo.fractumseraph.net](https://halo.fractumseraph.net/)**, or
[download it](#download) for Windows, Mac, Linux and Android.

This fork combines [ChupathingyCE](https://github.com/ChupathingyCE/chupathingyce)
and [OpenCE](https://github.com/OpenCommunityEdition/OpenCE), the ports of the
Halo: Combat Evolved decompilation, keeps up with both, and adds its own
features on top. This page lists them in that order: [this fork's](#what-this-fork-adds),
then [ChupathingyCE's](#chupathingyce), then [OpenCE's](#opence).

## What this fork adds

- **Halo in your browser.** The whole game, compiled to WebAssembly: no
  install, on computers, phones and tablets, with touch controls, and it
  installs as an app. Play online with other browsers and with the
  Windows, Mac, Linux and Android builds, from the game's own menus (Create
  Game, the Server Browser) or with an invite link; voice chat; ChupathingyCE's
  stats on [stats.chupathingy.org](https://stats.chupathingy.org). The site's Custom
  Edition maps download the first time they are played and stay in the
  browser.
- **Every Custom Edition map, downloaded when a game needs it.** All 5,829
  maps of the HaloMaps.org and HaloNet archives are unpacked at
  [halomaps.fractumseraph.net](https://halomaps.fractumseraph.net/maps/),
  each with a torrent, seeded around the clock. Join a game on a map you
  don't have and the game downloads it, from the host and other players
  (map sharing, never renaming a map you have), over BitTorrent and from
  the site, checks it, and joins: nothing to find or install by hand. The
  maps page also has every `.torrent` in one download.
- **Watch games live, or again.** Servers can keep a public copy of every
  game (`network.record_public`): each player's moves, aim and kills, with
  no address or hardware id, held back 30 seconds while the game is played
  so that no one watching can help a player. Anyone watches them at
  [halo.fractumseraph.net/assets/replay](https://halo.fractumseraph.net/assets/replay/),
  live or once the game is over, through any player's eyes, behind them or
  freely, each game linked to its stats on stats.chupathingy.org.
- **Tools against cheaters.** A host checks each hit and move for shots
  through walls, off aim, teleports and flying, and refuses or logs them
  (`network.fair_play`; `sv_suspects` on a dedicated server); records every
  game for its admins (`network.record_games`), who watch it in the replay
  viewer with the hits it doubted marked; and its moderators can **Watch**
  any player through their eyes, as Halo PC's SightJack did. Hosts can share
  ban lists (`network.shared_bans`): FractumSeraph's servers publish theirs,
  without IP addresses.
- **Invite links that last.** A host's `halo://join/` link can stay the same
  from run to run (`network.lasting_invite`), to post on a website.
- **The game's maps at the first start.** The Windows, Mac, Linux and
  Android builds offer to download the Xbox maps instead of asking for a
  disc image: the multiplayer maps alone (about 0.5 GB, with Halo Custom
  Edition's shared maps), or everything with the campaign (about 2 GB); the
  campaign's maps can be downloaded later (see [Game data](#game-data)).
- **Fractum Halo for Android, signed.** An app of its own
  (`net.fractumseraph.halo`), beside ChupathingyCE's, signed with this
  fork's key so that every release installs over the last and updates
  itself: [download page](https://halo.fractumseraph.net/assets/android/index.html).
  The Windows and Linux builds keep their saves in a folder of their own
  too (`%APPDATA%\FractumHalo`, `~/.local/share/fractum-halo`).
- **The newest of both projects.** ChupathingyCE's features, plus OpenCE's
  work that ChupathingyCE hasn't taken in: field of view and viewmodel
  settings, OpenCE's touch controls (profile buttons, gyroscope aiming, aim
  assist, opacity), the high-resolution controller buttons, a screenshot
  key (F10), mods (folders of Halo PC tag files whose sounds, bitmaps and
  text play over a map's on your machine, with the main menu's Mods:
  `docs/mods.md`), the portable Linux build for SteamOS, the
  profiling build, and its CUSTOM SINGLEPLAYER and CUSTOM MULTIPLAYER map
  lists (Custom Edition campaign maps alone or as network co-op, a map's
  own `.bmp` picture and `.txt` description), your own legs in first
  person, energy shields that glow and take your armor's color, the
  shield's flare on your arms, and native builds that play with OpenCE's
  browser build over WebRTC.
- **Servers of its own.** Two dedicated servers, **FractumFFA** (free for
  all Slayer on small Custom Edition maps) and **FractumSlayer** (Team
  Slayer), which update themselves when empty and can be watched live; an
  internet-play broker (`halovps.fractumseraph.net:1883`, the first of the
  five in every build's list), the maps' BitTorrent tracker, the browser
  lobby and the gateway that lets a browser join native games.
- **Its own releases.** Every change that builds on every platform is a
  release of this repository, and the game updates itself from them.
- **Host it yourself.** The browser version comes as a kit to run on your
  own computer, a VPS or a plain web host (see [Download](#download)).

Where the two projects do the same thing two ways, the fork keeps one
working version; nothing sent over the network differs from theirs, so
players of all three play together: this fork's builds speak network
version 32, as OpenCE's and ChupathingyCE's newest do, and join hosts of
versions 11 to 33, in either project's form (`docs/delta.md`). Both
projects' text chats are one, by ChupathingyCE's own bridge: its chat is
the one typed and shown, and OpenCE players' lines reach it and its lines
reach them, on a host of either (`docs/delta.md`, Chat).

## Download

**The game for Windows, Mac, Linux and Android**, this fork's builds of the
latest `main` (each push that builds on every platform is a release,
`v<version>-fs.<run>` (the run's number in five digits, as `v0.7.1e-fs.00057`); the game updates itself from these releases). At the
first start it offers to download the game's maps (see
[Game data](#game-data)), and it downloads Custom Edition maps as a game
needs them ([docs/map_torrents.md](docs/map_torrents.md)):

| Platform | Release | Debug |
| --- | --- | --- |
| Windows (64-bit) | [chupathingyce-windows64-release.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-windows64-release.zip) | [chupathingyce-windows64-debug.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-windows64-debug.zip) |
| Windows (32-bit) | [chupathingyce-windows-release.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-windows-release.zip) | [chupathingyce-windows-debug.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-windows-debug.zip) |
| Linux (64-bit) | [chupathingyce-linux64-release.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-linux64-release.zip) | [chupathingyce-linux64-debug.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-linux64-debug.zip) |
| Linux (32-bit) | [chupathingyce-linux-release.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-linux-release.zip) | [chupathingyce-linux-debug.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-linux-debug.zip) |
| Mac | [chupathingyce-macos-release.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-macos-release.zip) | [chupathingyce-macos-debug.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-macos-debug.zip) |
| Android | [fractum-halo-android.apk](https://github.com/FractumSeraph/OpenCE/releases/latest/download/fractum-halo-android.apk) (or [chupathingyce-android-release.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-android-release.zip)) | [chupathingyce-android-debug.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-android-debug.zip) |
| Dedicated server (Linux x64) | [chupathingyce-server-linux-x64.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-server-linux-x64.zip) | |
| Dedicated server (Linux arm64) | [chupathingyce-server-linux-arm64.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-server-linux-arm64.zip) | |
| Dedicated server (Linux x86) | [chupathingyce-server-linux-x86.zip](https://github.com/FractumSeraph/OpenCE/releases/latest/download/chupathingyce-server-linux-x86.zip) | |

**The Android app** is "Fractum Halo" (`net.fractumseraph.halo`, beside
ChupathingyCE's app rather than over it), signed with this fork's key, whose
certificate's SHA-256 fingerprint is
`79:2A:A0:CD:51:3B:A0:22:0A:4E:0E:AD:65:08:33:A8:39:74:79:15:69:FA:49:57:9C:92:47:96:48:A8:85:99`;
each release installs over the one before, and the app updates itself. Its
download page, with how to install it, is
https://halo.fractumseraph.net/assets/android/index.html
(`port/web/assets/android/index.html`). Each release has the app bare as
`fractum-halo-android.apk`, and the native 64-bit build (experimental) as
`fractum-halo-android64.apk`. The debug zip's APK is signed with the build
machine's own debug key, which can differ from one build to the next, and
then does not install over an earlier one.

**The browser version, to host it yourself** (no game data included):

| File | What it is |
| --- | --- |
| [halo-server.zip](https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-server.zip) | Everything to host it: the game, its server, the lobby service for online play, the gateway for native `halo://join` links, and Node.js, for Windows and Linux x64 in one folder you can copy between them. Unzip, put the game's Xbox `.map` files in `halo-server/public/assets/maps/`, and double-click `Start Halo (Windows).bat` (or run `./start-halo.sh`). On Windows, `server\windows\update.ps1` later updates the folder in place. |
| [halo-server-windows-x64.zip](https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-server-windows-x64.zip) | The same for Windows only (smaller). |
| [halo-server-linux-x64.zip](https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-server-linux-x64.zip) | The same for Linux x64 only. [HOSTING-VPS.md](services/selfhost/HOSTING-VPS.md) sets it up on a VPS with your own domain and HTTPS; [HOSTING-STATIC.md](services/selfhost/HOSTING-STATIC.md) puts the page on a web host of its own. |
| [halo-web.zip](https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-web.zip) | The game alone, to put on any web server. Without the lobby service there is no online play. |

The kits' instructions: [services/selfhost/README.md](services/selfhost/README.md).

## Game data

At the first start the game offers to download its `maps/` folder from this
fork's site, https://halo.fractumseraph.net/assets/maps/ (the setting
`data.download_url`; empty turns it off), or to extract it from an Xbox disc
image of Halo: Combat Evolved, as ChupathingyCE's builds do (see "You need
your own copy of Halo" below). The download is either:

- **Multiplayer only** (about 0.5 GB): the multiplayer maps, the menus'
  `ui.map`, and Halo Custom Edition's shared maps (`bitmaps.map`,
  `sounds.map` and `loc.map`, into `maps_ce`), which every Custom Edition
  map needs. The campaign's ten maps are left out: the first time a campaign
  level is played, the game offers to download them (about 1.6 GB) and
  waits for them. On Android they come from the touch controls' Options,
  or from the app icon's **Download campaign maps** shortcut (hold the
  icon).
- **Everything** (about 2 GB): the campaign's maps as well.

A copy of Custom Edition's shared maps already in `maps_ce` is kept as it
is. The site's `maps.txt` names those three as `custom_maps/<name>` (its
folder beside `maps/`); builds from before this skip such lines.

## Mods

Mods are folders of Halo PC tag files, as the Halo Editing Kit and Invader
write them, in `mods/` next to `maps/`: texture, sound, text and model packs
that play over the maps' on your machine only, in any game. Each has a
`mod.toml`. Every mod in the folder is on; the main menu's Mods turns them
off and on and sets the order they load in.
Refer to [docs/mods.md](docs/mods.md).

## The browser build

The browser build is the same game and platform layer compiled to
WebAssembly with Emscripten (`tools/browser_build.py`; `HALO_BROWSER`, which also
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
ninja browser
```

The page needs cross-origin isolation (COOP/COEP headers) and the maps
beside it in `assets/maps/` (served with byte ranges), or the player chooses
an Xbox disc image in the browser. Custom Edition maps come from
`assets/custom_maps/` (listed in its `index.json`) and are kept in the
browser once read (Cache Storage). The first time a map is played, the page
fetches it and `bitmaps.map`, `sounds.map` and `loc.map` whole, a few large
ranges at a time (`port/web/fetch_path_normalization.js`); joining from the
Server Browser waits for that before connecting, the status line showing
it, because a host gives a joining machine only moments to load the map (a
native host, 15 seconds; a browser host waits 120). `tools/browser_serve.py`
serves a checkout for development. The link refuses WebAssembly signature mismatches
(`-Wl,--fatal-warnings`): a C function called through a prototype that does
not match its definition traps in a browser.

### Where the fork's parts are

- **The browser build** (`ninja browser`, `tools/browser_build.py`, `port/web`): the
  game in WebAssembly with WebGL 2, the page and its online play (rooms,
  invites, quick games, joining native hosts through the native gateway),
  touch controls, installing as an app, Custom Edition maps served by the
  site, maps kept in the browser, voice chat, text chat with ChupathingyCE
  hosts, and ChupathingyCE's Delta (Delta List, Delta
  Peer and Delta Stats) through the site's relay. Its
  details are in [The browser build](#the-browser-build) above.
- **Online services** (`services/`): the lobby service for browser games
  (`services/signaling`), the native gateway that lets a browser join a
  native `halo://join` invite (`services/native-gateway`, Rust), and the
  self-hosting server and its kits (`services/selfhost`).
- **Custom Edition maps over BitTorrent** in the native builds
  (`port/linux/src/torrent*.c`, `map_torrents.c`, [docs/map_torrents.md](docs/map_torrents.md)):
  a map missing when joining (`cache_files_map_present`, ChupathingyCE's
  check) is downloaded from other players, this fork's seed box and web
  seed (http://halomaps.fractumseraph.net/maps/) and tracker
  (halovps.fractumseraph.net:6969), or from the web seed alone when no peer
  has it (the whole file, checked against the index), and a hosted map is
  seeded. ChupathingyCE's map download offer (Online Games,
  `network.map_downloads`) downloads through them first, and asks its
  community map repositories only when they fail.
- **The maps downloaded at the first start** (`sdl_platform.c`,
  `LauncherActivity.java`; `data.download_url`).
- **Voice chat** (OpenCE's, with the Opus codec built into every port).
- **This fork's MQTT broker**, `halovps.fractumseraph.net:1883`, first in
  `brokers.txt`, and the game's broker limit raised from 4 to 5 for it.
- **Releases and updates of its own**: every push to `main` that builds is a
  release of this repository (`.github/workflows/build.yml`'s release job),
  and its builds update from them (`HALO_UPDATE_REPOSITORY`,
  `tools/ci_build.py`).
- **A workflow for the browser build** (`.github/workflows/web.yml`): it
  builds the page and the self-hosting kits and puts them on the
  `web-latest` release.

Browser-only code is inside `#ifdef HALO_BROWSER` (or in files only the browser
build compiles); nothing sent over the network is changed.

---

<a id="chupathingyce"></a>
<p align="center"><img src="docs/icon-160.png" width="120" alt=""></p>

<h1 align="center">Chupathingy</h1>

<p align="center"><b>A native port of the Halo: Combat Evolved decompilation for Windows, Mac, Linux (including Steam Deck) and Android, built for stability and community play.</b></p>

<p align="center">
<a href="https://chupathingy.org">chupathingy.org</a> ·
<a href="https://github.com/ChupathingyCE/chupathingyce/releases/latest">Download</a> ·
<a href="https://stats.chupathingy.org">Games online now</a> ·
<a href="https://discord.gg/chupathingy">Discord</a>
</p>

<p align="center">
<a href="LICENSE"><img src="https://img.shields.io/badge/license-AGPL--3.0--or--later-blue" alt="License: AGPL-3.0-or-later"></a>
<a href="https://github.com/ChupathingyCE/chupathingyce/releases/latest"><img src="https://img.shields.io/github/v/release/ChupathingyCE/chupathingyce?label=release" alt="Latest release"></a>
<a href="https://github.com/ChupathingyCE/chupathingyce/releases/tag/nightly"><img src="https://img.shields.io/badge/nightly-daily%20build-orange" alt="Latest nightly"></a>
<a href="https://github.com/ChupathingyCE/chupathingyce/actions/workflows/build.yml?query=branch%3Amain+event%3Apush"><img src="https://img.shields.io/github/actions/workflow/status/ChupathingyCE/chupathingyce/build.yml?branch=main&event=push&label=build" alt="Build status on main"></a>
<a href="https://discord.gg/chupathingy"><img src="https://img.shields.io/discord/1555995995555365045?label=discord&color=5865F2" alt="Discord"></a>
<img src="https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux%20%7C%20Android-lightgrey" alt="Platforms: Windows, macOS, Linux, Android">
</p>

> **Compatible with OpenCE build-244 (network version 32) for online play.** Players on that build join our games, and ours join games hosted on network versions 11 through 33. Browser builds can't join our games, nor we theirs.

Chupathingy is a free, open-source port of the Halo: Combat Evolved
decompilation. It runs natively on Windows, Mac (Apple silicon and Intel),
Linux (including Steam Deck) and Android, with no emulator, and it is built for stability and
community play: the original Xbox campaign and multiplayer maps, Halo PC and
Custom Edition maps, online games with the community's servers, and releases
you can count on. Xbox and Xbox 360 versions are built separately, by
[Warthog](https://github.com/ChupathingyCE/warthog) (source only, for now).

**To play:** [download](#download) the build for your system, start it, and
point it at your own copy of the game ([first start](#you-need-your-own-copy-of-halo)).
Then [join a game online](#playing-online). Bring your own copy: we never host
or share Microsoft's files.

**Help and community:** the [Discord](https://discord.gg/chupathingy) is where
people find games, ask for help and report bugs, and
[chupathingy.org](https://chupathingy.org) is the project site. Expect
rough edges, and please report them.

<p align="center"><img src="docs/screenshots/lobby.jpg" width="720" alt="A multiplayer lobby"></p>

## What Chupathingy is and isn't

- A port of the Halo: Combat Evolved decompilation for Windows, Mac, Linux
  and Android, run by its community.
- You bring your own legally owned copy of the game. We never host, share or
  distribute Microsoft's files, and the game never downloads them.
- No browser version, and no websites or domains that look official.
- A fan project, not made or endorsed by Microsoft, Bungie or 343 Industries.

## Features

**Online**
- **Online Games**, a server list in the Multiplayer menu: join a game with **A**, or host one with **Y**
- Games you host are listed for everyone, from any of our builds
- Invite links (`halo://join/…`) to send to friends; opening one joins their game
- Direct connections between players, with no port forwarding in most homes
- Cross-play with players on other builds of the same network version, both ways
- Dedicated servers anyone can run: a playlist of games, around the clock, on Linux x86, x64 or arm64

**Stats, on [stats.chupathingy.org](https://stats.chupathingy.org)**
- A carnage report for every finished game, with medals
- Service records and leaderboards, with confirmed players (your games count toward you, whatever name you use)
- Accounts, made on the site or from the game, with an encrypted backup of your player identity
- Listing a game hosted from another build, by its invite link

**The game**
- The campaign, split screen and System Link, running natively (no emulator)
- LAN parties the old way: System Link on your own network and split screen need no internet and no account
- High-res HUD and text, and widescreen menus
- Controller prompts for Xbox, PlayStation and Nintendo pads, and the keyboard
- Text chat in games with other Chupathingy players, and a follow camera while you wait to respawn
- Updates itself on Windows, Linux and Android: it checks for new releases when it starts, asks first, and only trusts the files a signed list of update files names (on Mac it opens the download)

**Platforms**

| | Windows | Mac | Linux | Android |
| --- | --- | --- | --- | --- |
| The game | Yes | Yes, Apple silicon and Intel | Yes | Yes |
| Online Games, hosting, stats | Yes | Yes | Yes | Yes |
| Dedicated server | The game, for a test | The game, for a test | Yes: x86, x64 and arm64, and Docker | |
| Updates itself | Yes | Not yet | Yes | Yes |
| Halo PC (Custom Edition) maps | Yes | Yes | Yes | Yes, see below |
| HaloMD maps | Yes | Yes | Yes | Yes, see below |
| Server Browser in the PC menus | Yes | Yes | Yes | Yes |

## ChupathingyCE's own releases

Get the latest release from the [Releases page](https://github.com/ChupathingyCE/chupathingyce/releases/latest):

| Platform | Download | Notes |
| --- | --- | --- |
| Windows | `chupathingyce-windows-release.zip` | Windows 10 or later. |
| Linux (64-bit) | `chupathingyce-linux64-release.zip` | glibc 2.31 or later (Debian 11, Ubuntu 20.04). Brings its own SDL3. See [port/linux/README.md](port/linux/README.md). |
| Linux (32-bit, older systems) | `chupathingyce-linux-release.zip` | glibc 2.31 or later (32-bit). Brings its own SDL3. See [port/linux/README.md](port/linux/README.md). |
| Android | `chupathingyce-android-release.zip` | Android 9 or later, 64-bit. See [port/android/README.md](port/android/README.md). |
| Mac | `chupathingyce-macos-release.zip` | macOS 13 or later, Apple silicon or Intel. |

The game checks for new releases when it starts and asks before updating. On Linux there is also an AppImage, on the test builds for now.

We don't pay for code signing yet, so the first start needs one extra step:

- **Windows** may warn about an unknown publisher: choose **More info → Run anyway**.
- **Mac**: move ChupathingyCE to Applications and open it. If macOS won't open
  it, go to **System Settings → Privacy & Security**, and choose **Open
  Anyway** next to ChupathingyCE. You only do this once.

## You need your own copy of Halo

Chupathingy doesn't include the game's maps, sounds or art. You need an Xbox
disc image (`.iso` or `.xiso`) of Halo: Combat Evolved. Any region works.

1. Start Chupathingy.
2. The first time, it asks where to get the maps. The default is **Xbox disc
   image**: pick your image.
3. It copies the game's `maps` folder out of the image (about 2 GB), then starts.

The same screen accepts your Halo PC retail CD image (`.iso`), which fills
`maps_pc`, and the Halo Custom Edition installer (`halocesetup_en_1.00.exe`),
which fills `maps_ce`. Or choose **Make the folders** to get empty `maps`,
`maps_ce`, `maps_pc` and `maps_md` folders and copy the files in yourself.

On Android, copy the disc image to your phone first; the maps, settings and
saves go in `/sdcard/Android/data/net.fractumseraph.halo/files`. On a
Mac, the maps, settings and saves go in
`~/Library/Application Support/ChupathingyCE`.
On Linux, the maps and settings (`config.toml`) go next to the `halo`
executable, and the saves in `~/.local/share/fractum-halo` (or
`$XDG_DATA_HOME/fractum-halo`). On Windows, the saves go in
`%APPDATA%\FractumHalo`. The first start copies your profiles, saved games
and game types from the folder earlier versions used (`halo-linux`, or
`%APPDATA%\halo` on Windows), which is left as it was. The 64-bit and
32-bit builds use the same places, so switching from one to the other keeps
your saves and settings.

**Common mistake:** the `maps` folder must hold the Xbox maps from your disc
image, not Halo PC's. Halo PC maps go in folders of their own beside it:
`maps_ce`, `maps_md` and `maps_pc` (see below). On a Mac, Linux and
Windows, if `maps` holds Halo PC maps or has no `ui.map`, the game says so
and quits.

## Playing online

| You want to | Do this |
| --- | --- |
| Join a game | **Multiplayer → Online Games**, pick a game, press **A**. Or press **Join** on [stats.chupathingy.org](https://stats.chupathingy.org). |
| Host a game | **Multiplayer → Online Games → Y (Create Game)**, or host from System Link as usual. Your game is listed online by itself, on stats.chupathingy.org and in OpenCE's in-game Server Browser (`public_lobby`/`host_public` under `[network]` in `config.toml` turn that off). |
| Invite a friend | When you host, the game copies an invite link (`halo://join/…`). Send it; opening it joins your game. |
| See your stats | Your service record is on [stats.chupathingy.org](https://stats.chupathingy.org), found by your name. Games you join count even when the host doesn't run Chupathingy: when an online game you joined ends, the game sends stats.chupathingy.org the scoreboard as your game saw it (your own line in full; everyone else's name, team, place, score, kills and deaths) with your player ID. Your copy also confirms your line in the host's report, which puts the game on your profile. To turn both off for games you join, set `report_joined_games = false` under `[network]` in `config.toml`. |
| Stop sharing your hosted games' stats | Set `report_events = false` under `[network]` in `config.toml`. It's on by default: the games you host send their Delta Stats (kills with positions, accuracy, medals, objectives) to stats.chupathingy.org for its match pages and heatmaps. Never anyone's address or hardware ID; OpenCE players are only their name, team and that game's score, kills and deaths. This stops Delta Stats only: a listed game's carnage report is still sent. To keep your hosted games listed but send neither, set `report_hosted_games = false`; `list_hosted_games = false` stops the listing too. All of these are also on the Privacy screen in Settings (Gearbox menus). |
| Make an account | On [stats.chupathingy.org/profile](https://stats.chupathingy.org/profile), or press **Start** in Online Games to make one for the player you already are. |
| Link the game without a browser (Steam Deck, Game Mode) | In Online Games, press **RB** (or **C** on the keyboard) for Link Profile. On your phone or computer, go to [stats.chupathingy.org/connect](https://stats.chupathingy.org/connect), enter the code the game shows (or scan its QR code), then press **A** in the game to confirm. |
| Use the PC menus | Set `menus = "pc"` under `[display]` in `config.toml`. **Multiplayer → Join Game → Server Browser** lists the public games of every compatible build and the games of stats.chupathingy.org, each once. |
| List a game from a build that can't list itself | Sign in on the site, open **Host a Game**, and paste your invite link. |

Everything here plays with OpenCE players on a matching network version: they
can join your games and you can join theirs. Games an OpenCE player hosts are
recorded from the reports of the Chupathingy players in them; a game only
one player reported counts only on that player's own record. Their hosts can
still list them on the site (Host a Game).

<p align="center">
<img src="docs/screenshots/site-games.jpg" width="49%" alt="stats.chupathingy.org: games and recent games">
<img src="docs/screenshots/site-medals.jpg" width="49%" alt="stats.chupathingy.org: medals">
</p>

## Features

**Online**
- **Online Games**, a server list in the Multiplayer menu: join a game with **A**, or host one with **Y**
- Games you host are listed for everyone, from any of our builds
- Invite links (`halo://join/…`) to send to friends; opening one joins their game
- Direct connections between players, with no port forwarding in most homes
- Cross-play with OpenCE players on a matching network version, both ways
- Dedicated servers anyone can run: a playlist of games, around the clock, on Linux x86, x64 or arm64

**Stats, on [stats.chupathingy.org](https://stats.chupathingy.org)**
- A carnage report for every finished game, with medals
- Service records and leaderboards, with confirmed players (your games count toward you, whatever name you use)
- Accounts, made on the site or from the game, with an encrypted backup of your player identity
- Listing a game hosted from a build that can't list itself, by its invite link

**The game**
- The campaign, split screen and System Link, running natively (no emulator)
- LAN parties the old way: System Link on your own network and split screen need no internet and no account
- High-res HUD and text, and widescreen menus
- Controller prompts for Xbox, PlayStation and Nintendo pads, and the keyboard
- Text chat in games with other Chupathingy players, and a follow camera while you wait to respawn
- Updates itself on Windows, Linux and Android: it checks for new releases when it starts, asks first, and only trusts the files a signed list of update files names (on Mac it opens the download)

**Platforms**

| | Windows | Mac | Linux | Android |
| --- | --- | --- | --- | --- |
| The game | Yes | Yes, Apple silicon and Intel | Yes | Yes |
| Online Games, hosting, stats | Yes | Yes | Yes | Yes |
| Dedicated server | The game, for a test | The game, for a test | Yes: x86, x64 and arm64, and Docker | |
| Updates itself | Yes | Not yet | Yes | Yes |
| Halo PC (Custom Edition) maps | Yes | Yes | Yes | Yes, see below |
| HaloMD maps | Yes | Yes | Yes | Yes, see below |
| Server Browser in the PC menus | Yes | Yes | Yes | Yes |

## Builds

- **Releases** are the stable builds, on the [Releases page](https://github.com/ChupathingyCE/chupathingyce/releases/latest). The game updates itself from them.
- **Nightly** is the [`nightly` pre-release](https://github.com/ChupathingyCE/chupathingyce/releases/tag/nightly): main, built every day at 04:00 UTC when it has changed. For early birds; it doesn't update itself.
- **Testing** builds are hand-rolled builds of a branch, published as pre-releases named "(test builds)". The current one is the [Grifball test](https://chupathingy.org/grifball).
- **Linux AppImage** is on the test builds for now.
- **What's coming** is the [0.8.0 milestone](https://github.com/ChupathingyCE/chupathingyce/milestone/5) and its unreleased [release notes](docs/release-notes/0.8.0.md).

## Halo PC maps

Every Chupathingy build also plays Halo PC multiplayer maps: Windows
(32-bit and 64-bit), Mac, Linux (32-bit and 64-bit), Android and the
dedicated server. Each kind has a folder of its own beside the game's
`maps` folder:

| Folder | What goes in it | Played online as |
| --- | --- | --- |
| `maps_ce` | Halo PC Custom Edition maps, with Custom Edition's `bitmaps.map`, `sounds.map` and `loc.map` | `<name>@ce` |
| `maps_md` | HaloMD maps (below) | `<name>@md` |
| `maps_pc` | the maps of your own Halo PC (retail) disc | `<name>@pc` |

Where the game's `maps` folder is:

| Platform | The folder holding `maps`, `maps_ce`, `maps_md` and `maps_pc` |
| --- | --- |
| Mac | `~/Library/Application Support/ChupathingyCE/` |
| Linux | next to the `halo` executable |
| Windows | next to `halo.exe` |
| Android | `/sdcard/Android/data/net.fractumseraph.halo/files` |

Beside the maps folders, the game keeps folders of its own:

| Folder | What is in it |
| --- | --- |
| `logs` (beside `maps`) | The game's logs: `debug.txt`, crash reports waiting to be sent, and on Windows `halo.log`. Attach these to a bug report. |
| `data` (beside `maps`) | Things you add that are not maps: your own font in `data/fonts` (`display.high_res_font`). |
| `cache` (in the save folder) | What the game can make again; safe to delete. |

Until 0.8.0, `debug.txt` was beside `maps` (and on Windows `halo.log` beside
`halo.exe`). The game leaves an old one where it is.

Copy the `.map` files from your own Halo PC install. On Android, Import
maps (hold the app's icon, or the touch controls' Options in the game)
picks `.map` files or `.zip` archives with Android's own file picker and
puts each in its folder. Include `bitmaps.map`,
`sounds.map` and `loc.map` from Custom Edition in `maps_ce`: every Halo PC
map reads them. Halo PC's own `ui.map` there adds its map names and
pictures. The maps appear in the multiplayer map list after the Xbox maps,
marked [CE], [MD] or [PC]. In Online Games, a game on a Halo PC map is
badged, and it can be joined only by players who have that map: the game
says which file is missing and where it goes. The Xbox maps from your disc
image are still needed. Chupathingy doesn't come with any of these files.

**Campaign maps, pictures and descriptions (OpenCE's).** In the PC
version's menus (`display.menus = "pc"`, the browser build's), New Game and
Create Game's Map screen list the Halo PC maps as CUSTOM MULTIPLAYER, and
their campaign maps (a single-player scenario's, which the multiplayer list
leaves out) as CUSTOM SINGLEPLAYER: a campaign map is played as a campaign
level, at a difficulty, or hosted from the Map screen as network co-op,
which players who have the map join (it is named online as any Custom
Edition map is). A `<name>.bmp` picture (uncompressed 24-bit or 32-bit) and
a `<name>.txt` description beside a map are shown for it in the map lists
and the lobby, on Windows, Mac, Linux and Android.

**HD HUDs on Custom Edition maps.** Custom Edition maps can draw their HUD in
high resolution. For the HD HUD on every map that uses Custom Edition's shared
HUD, use the `bitmaps.map`, `sounds.map` and `loc.map` from Aerocatia's
[halopc-restored](https://github.com/Aerocatia/halopc-restored/releases/tag/r220)
release r220 (the "custom edition full shared" package) in `maps_ce` instead of
the stock ones; keep a copy of yours. Maps with their own HUD use theirs.

**Playing with other builds.** Other builds play Custom Edition maps from
a `custom_maps` folder. A game on a Custom Edition map is named the same way
for both, so their players with the map join our games and we join theirs.
HaloMD and Halo PC retail games are Chupathingy's only: a player on
OpenCE is told the map is missing. Between Chupathingy players,
the map's identity (its name and a hash of its file) is checked, so a
player with a different file of the same name is told so.

**Downloading a missing map.** When a game you join is on a Custom Edition
map you don't have, the game offers to download it from the community map
repositories (the public map repositories Halo PC players have used for
years, as Chimera downloads from them: maps.halonet.net, then its mirrors
maps2 and maps1, over HTTPS): "Download <name>.map from the community map
repository?", with Yes, No and Don't ask again. The download shows its
progress and can be cancelled. Before the map goes in `maps_ce`, the game
checks it: no more than 200 MB, downloaded and unpacked; only `<name>.map`
taken from the archive, never a path from it; its size and CRC-32 as the
archive states them; a Custom Edition multiplayer map whose header names the
map asked for; and the host's version of it (the checksum in its header,
which the host advertises with its game). It never replaces a file that is
there. Then the game joins, and the map's own checks run as for any map. A
downloaded map is listed in `maps_ce/downloaded_maps.txt`: its scripts are
held to a downloaded map's tighter rules. `network.map_downloads` in
`config.toml` (or Network Settings in the PC menus) says `"ask"` (the
default), `"always"` or `"never"`. Desktop and Android builds download maps;
the dedicated server doesn't. HaloMD and Halo PC retail maps aren't
downloaded, and neither are Halo PC's own maps and files (Blood Gulch, Putput,
the campaign's levels, `ui`, `bitmaps` and the rest, also under a hashed
name), though the repositories have copies: they're Microsoft's and
Gearbox's, so the game asks you to copy them from your own Halo PC or Custom
Edition install instead ("Install this map yourself").

In this fork the offer reads "Download <name>.map from other players and the
map archives?": the map comes from the host if the host shares it (map
sharing), else over BitTorrent (other players, this fork's seed box and web
seed), else out of the HaloNet and HaloMaps archives' zips, all checked
against the maps' index, and only then from the repositories above
([docs/map_torrents.md](docs/map_torrents.md)). Maps from any of them are
listed in `maps_ce/downloaded_maps.txt`, and `"never"` stops all of them
but map sharing (which has its own setting, Network Settings > Map Sharing).
Unlike ChupathingyCE, this fork offers Halo PC's own maps too (Blood Gulch,
Putput and the rest, also under a hashed name), from its seed box and web
seed first, as its first start downloads the Xbox's maps; "Install this map
yourself" is left for `"never"`.

**When the host changes map.** If a game you're in moves on to a Custom
Edition map you don't have (a dedicated server's playlist going to its next
map, say), the game leaves it, offers the map the same way over the main
menu (or downloads it at once, with `"always"`), and then joins the same host
again, in its next lobby or the game in progress. The game's protocol gives a
machine about a minute to load a map before the host drops it, too short for
most downloads, so the player leaves and comes back rather than holding the
game up. If the map isn't downloaded (you say no, cancel it, or it fails),
the main menu says the map is missing, as it does with `"never"`.

**Maps named with a hash.** Hosts on OpenCE name a shared Custom
Edition map `<name>_<8 hex digits>` (a game on `Sneak_af352483`, say), the
digits the start of a hash of the whole map file. Chupathingy plays such a
game from your `<name>.map` (`sneak.map`) when its hash matches, and the
downloader fetches `<name>.map` for it, checking the hash too. If your
`<name>.map` is another version, the download is kept beside it as
`<name>_<digits>.map`.

**Older folders.** Earlier Chupathingy versions kept Custom Edition maps
in `maps/ce/` and HaloMD maps in `md_maps/`, and OpenCE keeps its Custom
Edition maps in `custom_maps/`. The game still plays the maps in all three.
At its first start it offers to move `maps/ce/` to `maps_ce` and `md_maps/`
to `maps_md` (each folder is moved whole: nothing is copied or deleted, and
a folder that can't be moved stays where it is). `custom_maps/` is OpenCE's
and stays where it is. `game.move_old_map_folders` in `config.toml` says
`"ask"`, `"yes"` or `"no"`; Android moves them without asking. A map file
named `<name>@ce.map` (`@md`, `@pc`), as OpenCE names them, is
found in its folder or in `maps/` itself.

**Other folders, or none.** `paths.custom_edition` in `config.toml` names
one more folder of Custom Edition maps (desktop builds), looked in after
the others: a folder of maps, or a Halo Custom Edition install, whose `maps`
folder is used, with its `bitmaps.map`, `sounds.map` and `loc.map`.
`game.custom_edition = false` turns Halo PC maps off: none is listed,
played or downloaded. Both are OpenCE's settings.

**Common mistake:** don't point `maps` itself at a Halo PC maps folder, or copy
Halo PC maps straight into it. The game can't start with Halo PC's `ui.map`
in place of the Xbox one, and Halo PC maps in `maps/` itself don't play. Keep
the Xbox maps in `maps/` and the Halo PC maps in `maps_ce`, `maps_md` or
`maps_pc`.

On Android, two limits apply. The maps need a fixed 23 MB of the app's
memory, which Android's Java runtime takes on some devices with a very large
Java heap (more than about 700 MB, such as some gaming handhelds). There
the Halo PC maps are not listed: the game says so once, when it first
lists its maps, and when a game on a Halo PC map is joined; the header of
`debug.txt` says `Custom Edition maps' tag cache not mapped`, and the
Android log `cannot reserve Custom Edition maps' tag cache`. And the
phone's texture cache is smaller than a
computer's, so on the heaviest maps (Foundation) some surfaces can show the
wrong textures in their busiest views. See [port/android/README.md](port/android/README.md).

### HaloMD maps

Chupathingy also plays HaloMD's multiplayer maps (the Mac Halo community's
maps, made for Halo PC 1.0), in every build. Bring your own: download
the maps you want from HaloMD's mod list, and put their `.map` files in
`maps_md`, beside the game's `maps` folder.

They need Custom Edition's `bitmaps.map`, `sounds.map` and `loc.map` in
`maps_ce` too: a HaloMD map keeps Halo's own textures and sounds in those
shared files, and the game reads them from Custom Edition's copies.

The maps appear in the multiplayer map list after the Halo PC maps, marked
[MD], with their names from HaloMD's list. Online, a HaloMD map is played as
`<name>@md` (`bgplus_5@md`), badged HALOMD in Online Games, and joined only by
players who have the same map file.

HaloMD's plug-ins aren't part of Chupathingy. A few maps were made for one:
the visible-object and bigger-BSP limits they raised are already raised here,
and the widescreen view is the game's own, but the maps made for gameplay
plug-ins (3rd Person, Rocket Surfing, Spartan) play without them, as plain
Halo.

## Settings (config.toml)

Most of what you can change lives in the game's menus, but everything the
port adds is in one file, `config.toml`. The game writes it the first time it
starts, with every setting listed, commented out at its default, and a line
or two saying what each one does. To change one, remove the `#` in front of it
and edit the value; the game reads the file when it starts, so restart it
after a change. A line that starts with `#` does nothing. If a change seems
to do nothing, look in `logs/debug.txt` for lines starting `config.toml` or
`settings:`: they name a misspelled setting, one in the wrong `[section]`, a
line still commented out, a line the game skipped because of a mistake (a
value missing its quotes, `menus = pc` for `menus = "pc"`, or `False` for
`false`; the other lines still count), a word a setting does not know, or a
`config.toml` that is not the one the game reads (such as `config.toml.txt`
on Windows).

| Platform | config.toml is |
| --- | --- |
| Windows | next to `halo.exe` |
| Mac | `~/Library/Application Support/ChupathingyCE/config.toml` |
| Linux | next to the `halo` executable |
| Android | in the game's data folder, `/sdcard/Android/data/net.fractumseraph.halo/files` |

A few people look for most:

| Setting | What it does |
| --- | --- |
| `display.mode` | `"fullscreen"`, `"borderless"` or `"windowed"`. F11 switches between a window and the whole screen. When it is set, `display.fullscreen` is ignored; when it is empty, `display.fullscreen = true` is borderless and `false` a window. |
| `display.window_scale` | The window's size, as a multiple of 640x480. |
| `display.vsync`, `display.max_fps` | Vertical sync, and a frame rate cap with V-Sync off (0 for twice the display's refresh rate, -1 for none). |
| `display.interpolation` | `false` keeps the game's own 30 frames a second; Video Setup's Frame Rate at 30 (Xbox) sets it. |
| `display.direct_camera` | Instant aim: the view turns the frame the mouse moves. On by default; config.toml only. |
| `display.menus` | `"chupathingy"` (the default: the Xbox's look with Halo PC's settings, profiles and more; `"pc"` in the browser), `"xbox"` (as close to the console as can be) or `"gearbox"` (Halo PC's menus with their Server Browser; `"pc"` still works). |
| `display.high_res_text` | `true` draws the game's text (menus and HUD) with sharp fonts at your resolution; `false` uses the maps' original bitmap fonts. The Server Browser and the moderation overlays use the system's fonts either way. For a blocky look overall, also set `display.resolution_scaling = "original"`. |
| `display.high_res_font`, `display.high_res_font_large` | Your own font file for the sharp text and Online Games, in place of the built-in fonts: put it in `data/fonts` beside `maps` and give its name (`high_res_font = "MyFont.ttf"`), or a full path. `_large` sets one for the large text only. Empty uses the built-in fonts; a file that can't be read is noted in `logs/debug.txt` and the built-in font is used. The font is yours: Chupathingy ships none but its own free ones, and never sends yours anywhere. |
| `display.field_of_view` | How wide the first-person view is, in degrees: 70 (the Xbox's) by default, 50 to 120. |
| `display.far_clip_distance` | How far the world is drawn: 1024 (the Xbox's) by default, up to 10000, for large maps that cut off in the distance. Drawing only. |
| `display.match_timer` | `true` shows a timed multiplayer game's time left at the top of the screen (off by default). |
| `display.player_names` | Names over players' heads: `"all"`, `"allies"`, `"enemies"` or `"none"`. |
| `game.unlock_all_levels` | `true` gives new profiles every campaign level. By default a new profile starts at The Pillar of Autumn, as on the Xbox. Profiles made by earlier versions had every level open; `profile_reset_campaign` in the console starts one over. |
| `audio.volume`, `audio.music_volume`, `audio.effects_volume` | Volumes, from 0.0 to 1.0. |
| `input.mouse_sensitivity`, `input.invert_mouse` | Mouse aim. |
| `input.keyboard_prompts` | Button prompts follow the device you used last: keys after the keyboard or mouse, as Halo PC did, and buttons after a controller. This setting is no longer in the menus; `false` keeps the controller's buttons everywhere. |
| `controls.*` | Every key: `controls.jump = "Space"`, or two at once, such as `"F, Mouse 4"`. |
| `network.browser_url` | The game list Online Games shows (stats.chupathingy.org). |
| `network.host_public` | Whether games you host are listed for everyone (true) or only joinable by invite (false). |
| `update.auto` | Whether the game updates itself. |
| `paths.data`, `paths.saves` | Where the maps and the saves are, if not the usual place. |
| `paths.logs`, `paths.cache` | Where the logs and the cache go, if not the usual place (below). |

Settings you leave at their default follow each new version's default, so
leaving the file alone is always safe. Every setting can also be given as an
environment variable for one run (the file lists each one's name), which
overrides the file.

## Run a server

The Chupathingy Dedicated Server hosts a playlist of games around the clock,
with no player of its own, and lists them on stats.chupathingy.org and in the
Server Browser. It is its own download for Linux on x86, x64 and arm64
(Oracle Cloud's free tier and Raspberry Pis included): one file, no libraries
to install, and no port forwarding. See [server/README.md](server/README.md).

## Cross-play and Delta

- **Our own releases.** Chupathingy has its own code, releases and version
  numbers, and doesn't change under you every few hours.
- **Playing together.** Chupathingy players and OpenCE players
  join each other's games when their network versions match. Chupathingy
  follows OpenCE's versions: a cross-play test
  runs against each new one, and once it passes, a signed table tells
  existing Chupathingy builds which network versions to announce and join,
  without a new download. A change to how multiplayer plays still needs a
  release. The line at the top of this page says which versions this
  release plays with.
- **Delta** is Chupathingy's network family ([docs/delta.md](docs/delta.md)):
  the game list, stats, profile links and dedicated server admin tools on
  [stats.chupathingy.org](https://stats.chupathingy.org), and a channel between
  Chupathingy machines for features of our own (how builds play together:
  [docs/delta-compatibility.md](docs/delta-compatibility.md)). Games with OpenCE players
  use the shared game protocol, so everyone plays together.

## Building it yourself

You need Python 3, [ninja](https://ninja-build.org/) and clang. The game
supplies the Xbox SDK declarations it uses, so you don't need the SDK.

```sh
python3 configure.py
ninja            # the game for the computer you're on
```

| Target | Result | Instructions |
| --- | --- | --- |
| `ninja macos` | `build/macos/ChupathingyCE.app` | [port/macos/README.md](port/macos/README.md) |
| `ninja linux64` | `build/linux64/halo` (64-bit) | [port/linux/README.md](port/linux/README.md) |
| `ninja linux` | `build/linux/halo` (32-bit) | [port/linux/README.md](port/linux/README.md) |
| `ninja windows` | `build/windows/halo.exe` | [port/windows/README.md](port/windows/README.md) |
| `ninja windows64` | `build/windows64/halo.exe`, the 64-bit game | [port/windows/README.md](port/windows/README.md#64-bit) |
| `ninja android_apk` | the Android app | [port/android/README.md](port/android/README.md) |
| `ninja server` | `build/server-<arch>/chupathingyce-server`, the dedicated server (Linux) | [server/docs/building.md](server/docs/building.md) |

Useful `configure.py` options:

| Option | What it does |
| --- | --- |
| `--release` | A release build, as players get. Without it, a failed check stops the game. |
| `--portable` | A Linux or Windows build that runs on any x86-64 computer, or a universal Mac application, to give to others. The Linux builds also run on older distributions and on SteamOS: refer to "Portable build" in [port/linux/README.md](port/linux/README.md#portable-build). |
| `--no-game-browser` | Leaves out the server list, stats and dedicated servers. |
| `--pgo=off`, `--lto=off` | Faster builds, without profile-guided or link-time optimisation. |
| `--profile` | A profiling build, which records where the game spends its time. See ["Profiling builds"](#profiling-builds-opence) below. |

The console ports (original Xbox, Xbox 360, Switch) are built from their own
repositories, with this one as a submodule: "Console ports" in
[docs/building.md](docs/building.md#console-ports).

The version being made is in `VERSION`. In this fork, every push to `main`
that builds on every platform is a release, `v<VERSION>-fs.<run>`, made by
the release job of `.github/workflows/build.yml` (see [Download](#download));
the builds of other branches and pull requests on the Actions page are for
checking changes. ChupathingyCE's own releases come from its release
workflow.

## Where it came from

Chupathingy is a port of the Halo: Combat Evolved decompilation
([punpckhdq/halo](https://github.com/punpckhdq/halo) and
[bnunu/halo-1](https://github.com/bnunu/halo-1)). It began as a fork of
OpenCE, an earlier port of that decompilation (the code is theirs up to
October 9, 2026), and has had its own code, releases and community since. The people whose work it builds on are
named in [Credits](#credits).

## Generative AI

The decompilation this project is built on, and the port it began as a fork of, were made with heavy use of generative AI. Chupathingy has its own code and releases now, but a lot of that earlier work is still in it.

We use AI tools the way developers use any other tool. A person decides what to build, reviews every change, tests it, and is responsible for it. Nothing is handed to an AI to build from start to finish without a person involved. We're also working through the code we inherited to clean up what reads as machine-written: comments that only repeat the code, unclear names, and changes nobody can explain.

If you contribute, the same applies to you: see [CONTRIBUTING](CONTRIBUTING.md#using-ai-tools).

## License

This fork (Fractum Halo, FractumSeraph/OpenCE) is free software: you can
redistribute it and modify it under the terms of the GNU Affero General
Public License as published by the Free Software Foundation, either version
3 of the License or (at your option) any later version
(`AGPL-3.0-or-later`). It comes with no warranty. The full text is in
[LICENSE](LICENSE).

- **Source code.** Its source is this repository,
  <https://github.com/FractumSeraph/OpenCE>, and each release's source is its
  tag. Whoever plays it over a network (the browser build on
  halo.fractumseraph.net, a game this fork's server hosts) is offered it
  there, as the AGPL's section 13 asks.
- **What it is made of.** ChupathingyCE's code (its base) is under the GNU
  General Public License, version 3 or later, from its 0.8.0
  ([LICENSE.GPL-3.0](LICENSE.GPL-3.0)); OpenCE's is under the AGPL 3.0 or
  later from 19 October 2026 (before that, CC0 1.0). The two licences let
  their works be combined (section 13 of each): the files of each keep their
  licence, and this fork as a whole, and its builds, are under the AGPL
  3.0 or later from the merge of OpenCE's change (2026-10-20). Before that
  merge the fork was GPL-3.0-or-later (from 2026-10-16), and before that CC0
  1.0 (its releases up to v0.7.5c-fs.00131); each stays so.
- **What they build on.** The Halo CE decompilation
  ([punpckhdq/halo](https://github.com/punpckhdq/halo), under the AGPL 3.0
  from 17 October 2026 and CC0 1.0 before, and
  [bnunu/halo-1](https://github.com/bnunu/halo-1), CC0 1.0).
- **Delta.** Delta's code is GPL from ChupathingyCE's 0.8.0; earlier versions
  of it stay CC0 for anyone already using them. The protocol itself
  ([docs/delta.md](docs/delta.md)) is a description anyone may implement,
  under any license, without using this code.
- **Third-party code, fonts and art** keep their own licenses, which are
  beside them (`port/third_party`, `port/assets/fonts`,
  `port/linux/ui/fonts`, `port/assets/prompts`, `port/assets/hud`,
  `port/assets/text_filter`), and the release downloads and the web site's
  `licenses.txt` carry their notices.
- **Halo itself is not covered.** The license covers the work of the
  authors. It gives no rights in Halo, its maps or anything else Microsoft
  owns.

### Console SDK permission

Chupathingy's code is also built for consoles by
[Warthog](https://github.com/ChupathingyCE/warthog), and anyone may build
Warthog themselves with a console maker's SDK. This permission covers that:

> **Additional permission under GNU GPL version 3 section 7**
>
> If you modify this Program, or any covered work, by linking or combining
> it with the libraries of a Microsoft Xbox or Xbox 360 software development
> kit (such as the Xbox Development Kit and the Xbox 360 SDK, including
> their `xnet.lib` and Direct3D libraries), or a modified version of those
> libraries, containing parts covered by the terms of those kits' licenses,
> the licensors of this Program grant you additional permission to convey
> the resulting work. Corresponding Source for a non-source form of such a
> combination shall include the source code of the covered work, and need
> not include the source code of those libraries.

The permission only lifts the GPL's own objection. It grants no rights in
Microsoft's libraries, whose own terms still apply, and you may remove it
from your copies (section 7). Chupathingy's and Warthog's own published
builds are made only with free tools and libraries.

## Support

Chupathingy is free and always will be. Support through
[Ko-fi](https://ko-fi.com/milenko) goes to running the site and the servers. If
there's a specific developer or port you'd like to support, ask them: they may
not be part of our community.

## Credits

- The decompilation: [punpckhdq/halo](https://github.com/punpckhdq/halo) and
  [bnunu/halo-1](https://github.com/bnunu/halo-1), of the Xbox build 2342.
- The port Chupathingy began as a fork of: OpenCE and its contributors, up to
  October 9, 2026 (see [Where it came from](#where-it-came-from)).
- Later fixes taken from OpenCE (CC0), with their authors kept in the
  history: Paul Hontz, xshxdex98, hkmodd, eltanschauung, justinfilip, Luke,
  MrBruh, Addison Bair, thelinkin3000, Brendan Turner and chestahh. The wire
  format tool (`tools/network_format.py`) is Addison Bair's and MrBruh's.
- Chupathingy: [Milenko](https://github.com/MrMilenko) and contributors. The
  icon is MrBruh's helmet, with tusks.
- Fonts: Cyborg Interface Gothic, the high-res text's font, by fizzy, made
  to match Halo's interface font from Overpass and glyphs of their own
  (SIL OFL); [Overpass](https://github.com/RedHatOfficial/Overpass) (SIL OFL);
  [Noto Sans](https://fonts.google.com/noto) (SIL OFL) and
  [Kenney's Input Prompts](https://kenney.nl/assets/input-prompts) (CC0); the
  scoreboard's platform logos are one-color marks from
  [Simple Icons](https://simpleicons.org) (CC0) and Wikimedia Commons (public
  domain), plus our own Xbox 360 ring and Wii U GamePad; the names and logos
  are their owners' trademarks, used only to identify platforms.
- Controller button icons by snapchipper, CC0 (`port/assets/prompts`).
- High-res HUD art on the Xbox maps: Aerocatia's and Jessie's HUD art from
  [Halo PC Restored](https://github.com/Aerocatia/halopc-restored) (release
  r220), used with Aerocatia's permission on the condition that both are
  credited; `port/assets/hud/NOTICE.md` lists which files are theirs.
- HD HUDs on Custom Edition maps (MCC's "0.5 HUD scale" and Chimera's
  "force HUD use high resolution scale" flags): ugoboom, tested with
  Aerocatia's [halopc-restored](https://github.com/Aerocatia/halopc-restored)
  resource maps.
- HaloMD map names: from [MacGamingMods](https://macgamingmods.com)' public
  HaloMD mod list, so the menus can show each map's own name.
- Missing Custom Edition maps: downloaded from the community map
  repositories, as Chimera uses them (maps.halonet.net, the HAC2 map
  repository, and its mirrors). Chupathingy's downloader is its own code;
  Chimera showed where the repositories are and how they answer.
- Libraries: SDL3, stb, Mbed TLS, miniupnpc, KCP, tomlc17, musl's maths,
  Expat, Monocypher, zlib, Opus, SMAA, and Project Nayuki's QR Code
  generator. Their licenses are
  beside them in `port/third_party`.

### Contributors

Fixes from other people's pull requests and forks, taken into Chupathingy
with their authors credited in each commit.

- [Tyberious](https://github.com/Tyberious) (Jeff Clark): Xbox ADPCM decoding
  without the 344 Hz buzz, vertex constant serials that never wrap, pose
  snapping for frame interpolation, and the clip-space position kept on the
  desktop (no more first-person vertex spikes).
- [thelinkin3000](https://github.com/thelinkin3000): the motion sensor for
  three and four local players (a split screen crash), and split screen
  dividers where the views meet on a wide screen.
- [xshxdex98](https://github.com/xshxdex98) ([DamnationCE](https://github.com/xshxdex98/DamnationCE)):
  the pause menu's QUIT by mouse or keyboard, and lens flares out of view no
  longer tested.
- [zimm3rmann](https://github.com/zimm3rmann): visibility test slot 0 kept
  apart from the scratch query (lens flares sharing a result).
- [nsafran1217](https://github.com/nsafran1217) (Nathan Safran): no hang
  where the visibility results buffer cannot be mapped.
- [saulob](https://github.com/saulob): the first menu frame's colors.
- [natsu-anon](https://github.com/natsu-anon): the cursor hidden during play.
- [lantos1618](https://github.com/lantos1618): no ghosting in the zoom effect
  at high resolutions; on macOS, Command-W does not quit and Command-Q asks
  twice.
- [pfista](https://github.com/pfista) (Michael Pfister): projectile trails
  ready before a weapon's first shot.
- [JoshRob297](https://github.com/JoshRob297): hosts that run their own games
  (the dedicated servers) no longer refuse every join after one arrived as a
  game ended.
- [HiIAmMoot](https://github.com/HiIAmMoot) (Mootjuh): the Android menus by
  touch, and `debug.solo_game` (a multiplayer game started alone).
- [oatkrs](https://github.com/oatkrs) (Utkarsh): crisp windowed rendering and no
  audio cut-outs on macOS, and a corrupted script thread dropped instead of
  halting the game.

### Findings and testing

- [bnunu](https://github.com/bnunu) (Jonas Volman): Custom Edition map
  findings from the custom-edition-maps branch, credited in the commits they
  led to, and the larger texture cache for Halo PC maps, which that branch had
  first.
- Sabriel and ugoboom: the Halo PC map reports and regression lists behind
  most of the Custom Edition fixes.
- MrBruh (build-145's Custom
  Edition loader, from DamnationCE's work by
  [xshxdex98](https://github.com/xshxdex98)): maps that need OpenSauce refused
  with a clear reason, the missing-map message when joining, and the
  `custom_maps\<name>` naming our hosts and clients share with OpenCE.

Halo is a trademark of Microsoft. Chupathingy is a fan project, not made or
endorsed by Microsoft, Bungie or 343 Industries, and includes none of the
game's content. The code is released under the GNU GPL, version 3 or later
(see [License](#license)).

---

<a id="opence"></a>
## OpenCE

[OpenCE](https://github.com/OpenCommunityEdition/OpenCE) is the port of the
Halo: Combat Evolved decompilation (the Xbox build 2342, from
[bnunu/halo-1](https://github.com/bnunu/halo-1), a fork of
[punpckhdq/halo](https://github.com/punpckhdq/halo)) to Linux, Windows and
Android, which ChupathingyCE and this fork build on. Everything here comes
from it first:

- **The game, running natively** (no emulator): the decompiled game's C
  sources, compiled as they are, on a platform layer that stands in for the
  Xbox (Direct3D 8 on OpenGL, DirectSound and XInput on SDL3, files,
  threads, sockets), on Linux, Windows and Android (an ILP32 AArch64 guest
  in a 64-bit app).
- **Multiplayer**: system link games of up to 128 players on up to 128
  machines, Linux, Windows and Android together; internet play by invite
  link with no server of the project's; a new netcode in which each machine
  moves its own player at once and the host decides
  ([port/linux/NETCODE.md](port/linux/NETCODE.md)); voice chat; votes to
  kick; network co-op.
- **The PC version's menus**, the Server Browser, high-resolution HUD, text
  and controller buttons, widescreen, frame interpolation, field of view
  and viewmodel settings, and every setting in `config.toml`
  ([port/linux/README.md](port/linux/README.md#settings)).
- **Halo PC Custom Edition maps**, refused with a clear reason when they
  need OpenSauce.
- **Touch controls** on Android, and the menus by touch.
- **Its own browser build** (`ninja web`, `tools/web_build.py`,
  [port/web/README.md](port/web/README.md)), played at
  [halocombatevolved.com](https://halocombatevolved.com/): browsers play
  each other in rooms over WebRTC, and the native builds take a browser's
  WebRTC on their internet play's socket. It is in this repository beside
  this fork's browser build (`ninja browser`), which is the one this fork's
  site plays.
- **Updates itself** from its releases.

Its own documents: [port/linux/README.md](port/linux/README.md) (controls,
menus, settings, internet play, how the port operates),
[port/windows/README.md](port/windows/README.md),
[port/android/README.md](port/android/README.md) and
[AGENTS.md](AGENTS.md) (the project's layout and rules). Its Discord:
[discord.gg/9gqcHyr5km](https://discord.gg/9gqcHyr5km).

### Profiling builds (OpenCE)

A profiling build records where the game spends its time, for
[Perfetto](https://ui.perfetto.dev/) and `tools/net_report.py`. The 32-bit
Linux and Windows builds and the Android app have one; the 64-bit builds,
the dedicated server and the browser build are built as without the option.

1. Enter `python configure.py --profile`, then build as usual (not with
   `--pgo=train`).
2. Start a recording: enter `profile_record` in the console (or the telnet
   console), or set `debug.profile_record = true` in `config.toml`. To
   record each game of a session, also set
   `debug.profile_record_when = "game"`.
3. Stop it: enter `profile_stop`, load another map, or quit.

| Command | Result |
| --- | --- |
| `profile_record [seconds]` | Starts a recording. With a number of seconds, it stops after that time. |
| `profile_stop` | Stops the recording. |

The game writes numbered `.part<n>.json` files under `profiles/` in its data
folder and logs their paths; they stay after the recording stops. Logs go to
standard error; a Windows release build without standard error writes them
to `halo.log` next to `halo.exe`. `python tools/net_report.py
<recording.part1.json>` reads a recording as tables, and writes
`<recording>.summary.txt`, a short text file.

`debug.profile_memory` sets a recording's memory in MB (default 256, from 4
to 1024). A recording has no length limit: the game writes a part each time
the memory is full. See the settings in
[port/linux/README.md](port/linux/README.md). On Android, the files are in
`/sdcard/Android/data/net.fractumseraph.halo/files/profiles/` (copy them with
`adb pull`). A profiling build plays with normal builds.

`python tools/ci_build.py linux profile` builds it as the GitHub workflow
does; the workflow builds it only in a run started by hand with its
`profile` box ticked.

## Testing

The tests are listed in [AGENTS.md](AGENTS.md) ("Testing"). Most run
without the game data, in GitHub Actions too.

One check needs the retail maps, so it runs on your own computer (or on a
self-hosted runner), not in GitHub Actions: `tools/net_compare.py` plays
two builds together in a network game on one computer and checks that they
stay in step. Run it when a change can alter how a build calculates the
game, as a change to the simulation, the compiler or the maths does,
before it is merged. For example, to check a build with such a change
against a build without it:

```sh
python tools/net_compare.py --a build/linux/halo --b <other checkout>/build/linux/halo \
    --data <folder with maps/> --maps bloodgulch,beavercreek,hangemhigh --swap
```

Each game prints PASS or FAIL. A FAIL names the first player, tick and
value that differ. Refer to "Comparing two builds" in
[port/linux/NETCODE.md](port/linux/NETCODE.md).

