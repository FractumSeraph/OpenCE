# Halo (self-hosted web version)

This folder is a complete, portable Halo: Combat Evolved web server: the
WebAssembly game, the multiplayer lobby service and the gateway for native
`halo://join` invites. You add the maps (see [Maps](#maps)). Nothing needs to
be installed. Copy the whole folder to another computer, or put it behind any
domain or sub-path, and it keeps working without edits.

It is built from [FractumSeraph/OpenCE](https://github.com/FractumSeraph/OpenCE)
(OpenCE with the browser port); the newest kits are on its
[web-latest release](https://github.com/FractumSeraph/OpenCE/releases/tag/web-latest):
`halo-server-windows-x64.zip` and `halo-server-linux-x64.zip`.

## Start it

| Host | How |
| --- | --- |
| Windows (x64) | Double-click **`Start Halo (Windows).bat`**. Close the window to stop. |
| Linux (x64) | Run `./start-halo.sh` (or `sh start-halo.sh`). Ctrl+C stops it. |

The window lists the addresses to open:

- **This computer:** `http://localhost:8765/`
- **Other devices on your network:** `https://<this-computer's-IP>:8443/`.
  The certificate is self-signed, so each device shows a one-time warning.
  Choose **Advanced → Proceed**. If someone opens the plain `http://<IP>:8765`
  address, they are redirected to HTTPS automatically. Browsers only run the
  game over HTTPS or on localhost.

The first time it starts on Windows, allow `node.exe` through the firewall on
**private networks**.

Players need a current desktop Chrome, Edge or Firefox.

## Play together

Everything happens in Halo's own **Multiplayer** menu:

1. One player picks **Create Game → Internet** (or **LAN**), then a map and the
   Server Setup options, and **Start Game** (up to 128 players). The page
   opens a room for that game: its invite link shows beside the game (with
   **Copy link**) and on Server Setup's INVITE LINK row.
2. Friends join with that link, or, for an **Internet** game whose
   **LISTING** is **PUBLIC**, from **Join Game → Server Browser**, where it is
   listed first, above the native games. A browser-hosted game cannot have a
   password; leave it PRIVATE to keep it to invited friends.
3. The host starts the match when everyone is ready. Backing out of the game,
   or **Close room** beside it, ends the room and takes the game off the list.

The **Play online** button beside the game sets your name and armor colour,
joins an invite link, and has a **Quick game** section that starts a lobby on
a map and mode picked right there.

The invite box (**Have an invite link?**) accepts:

- invite links from this server;
- invite links from another copy of this folder running under a different
  domain (the browser contacts that copy's lobby directly);
- `halo://join/…` links created by the Windows/Linux builds of the game. These
  go through the native gateway (see "Internet play" below).

Gameplay traffic goes directly between each player and the host's browser
(WebRTC). Only the lobby handshake goes through this server.

**Native public games:** the in-game **Server Browser** also lists the public
games hosted with the Windows/Linux builds of OpenCE. Choosing one joins it
through the native gateway, like a `halo://join` link. This server fetches the
list from internet play's brokers (`server/public-games.mjs`, only while
someone has the browser open). The game checks each listing's signature
itself. The browser-hosted public games come from the lobby service
(`/v1/public-rooms`, `services/signaling`): a host's page sends its game's
listing over its room's connection, and a listing lapses within 90 seconds
of its host leaving.

**ChupathingyCE (Delta):** the browser also speaks the parts of
[Delta](https://halo.milenko.org/delta), ChupathingyCE's network family, that
a player needs (`port/web/src/delta/README.md`): it joins hosts of OpenCE
network versions 11 to 24 as their builds do; the Server Browser shows what
their game list (halo.milenko.org) says of each game (the host's platform,
dedicated servers, who is playing); it shakes hands with their hosts (Delta
Peer) and leaves a Custom Edition game whose map file is not the host's;
and a game joined through an invite counts on halo.milenko.org (Play
online, *Stats on halo.milenko.org*: the player can turn it off, and link
their profile there). Their site has no CORS, so this server asks it for
the page (`server/delta-list.mjs`, `/v1/delta/...`), and it hashes the
Custom Edition maps once for the map check (`server/map-hashes.mjs`, in the
background, about five minutes for 130 maps; kept in
`data\custom-map-hashes.json`). `delta.enabled: false` in `config.json`
turns the site's part off.

**Keep the game visible while you play, especially the host.** Browsers
pause background tabs, and after about a minute in the background the game
times out and the match ends ("A networking error has occurred"). A window
that is visible but not focused is fine. To try two players on one computer,
use two separate browser windows side by side, not two tabs.

Troubleshooting: run the server with the environment variable
`HALO_LOG_REQUESTS=1` to log every request it serves.

## Start automatically with Windows

`server\windows\install-autostart.ps1` (run once as administrator) adds a
scheduled task, **Halo Web Server**, that starts the server at boot before
anyone logs in, restarts it if it stops, and logs to `logs\halo-server.log`.
It runs as the account that installed it, without storing its password, so
that account can restart and update the server later without administrator
rights (`-AsSystem` runs it as SYSTEM instead, which then needs an
administrator for each restart and update).
`server\windows\uninstall-autostart.ps1` removes it. After moving this folder,
run the installer again.

While the task runs, don't also start `Start Halo (Windows).bat`: the ports
are already in use. To stop or start it by hand, run
`Stop-ScheduledTask "Halo Web Server"` or `Start-ScheduledTask "Halo Web Server"`
in PowerShell.

After changing `config.json` or the files in `server\`, restart it with
`server\windows\restart.ps1`. It asks the server to stop by creating
`data\stop-request` (the server stops itself within a second, with its
gateway and lobby), also ends a server process left holding the ports, and
logs to `logs\restart.log`. Creating that file stops the server however it
was started.

## Phones and tablets (touch controls)

Open the game on a phone (`https://<this-computer's-IP>:8443/` at home) and
hold it sideways. Once the game has loaded, touch controls appear by
themselves:

- **Move:** put your left thumb anywhere on the left side; a stick appears
  under it.
- **Aim:** drag on the right side. The large fire button also aims while
  you hold it.
- **Buttons:** jump, crouch, melee, reload, swap weapon, grenade (and its
  type), flashlight, zoom, pause and score. In the game's menus, use the
  stick to move and **Jump (A)** / **Melee (B)** to select and go back.
- **The ☰ button (top left):** choose the **Modern** layout or the
  **Original Xbox controller** layout, change the look speed, **Edit layout**
  (drag any control to move it, pick one to resize it, set the opacity),
  the **Resolution**, or open **Play online** for your name, armor and
  invite links (games are made in Halo's own Multiplayer menu).

These settings are saved on each device. Connecting a Bluetooth or USB
controller hides the touch controls while it is connected. On a tablet or
touchscreen laptop, use the **Touch** button under the game to switch touch
controls on or off. Adding `?touch=1` to the address forces them on.

The touch controls are ported from
[Halo Mobile](https://github.com/OMG-Guest/Halo-Mobile) (CC0).

## Install as an app

Halo can be installed like an app: an icon on the home screen that opens it
full screen and sideways, without the browser's bars.

- **Android, and Chrome or Edge on a computer:** tap **Install app** under
  the game (or in the ☰ menu in touch mode), then **Install**.
- **iPhone or iPad:** tap **Install app** for the steps: **Share** →
  **Add to Home Screen** → **Add**.

Installing needs a **real HTTPS certificate**, such as a domain with Let's
Encrypt (see [HOSTING-VPS.md](HOSTING-VPS.md)). On the home network's
self-signed address, phones can play in the browser but most won't install
the app. An installed app on iPhone keeps its own saves, separate from
Safari's.

## Maps

Maps are served from **`public/assets/maps/`**, so players never pick an ISO.
The kit comes without them: copy the game's Xbox-format `.map` files there
(`tools/xiso_extract.py` in the repository takes them from an Xbox disc
image). Replace any file with your modified version and players get it the
next time they load the page. The game expects these
file names:

```
ui.map  a10 a30 a50 b30 b40 c10 c20 c40 d20 d40 (campaign)
beavercreek bloodgulch boardingaction carousel chillout damnation
hangemhigh longest prisoner putput ratrace sidewinder wizard (multiplayer)
```

If `ui.map` is missing, the page falls back to asking each player for their
own XISO.

### Halo Custom Edition maps

Custom Edition maps (Coldsnap, Extinction, Halo PC's Ice Fields, Death Island
and so on) are served from **`public/assets/custom_maps/`**, beside `maps/`.
Put there the maps, and the three resource maps every Custom Edition map
needs, from a Halo Custom Edition install's `maps` folder: `bitmaps.map`,
`sounds.map` and `loc.map`. A map's optional `<name>.txt` (its description)
and `<name>.bmp` (its picture in the game's menus) can go beside it. Players
download nothing themselves: the game reads what it needs from the server,
as it does the Xbox maps.

The game's menus list them as CUSTOM MULTIPLAYER (and CUSTOM SINGLEPLAYER)
maps (Create Game's map list has them, with their pictures), the Play online
panel's Quick game offers the multiplayer ones after the Xbox levels (with
a Find a map box), and the in-game server browser can join native OpenCE
games on any of them the server has. A map added while the server runs
shows up the next time a player loads the page. Updates (`update.ps1`)
leave the folder alone.

The server lists the folder for the game at `assets/custom_maps/index.json`
(each file's name, size and version, each map's header, its first 2 KB,
and a map's BLAKE2b-256 hash once the server has made it: Delta's map
check), so the game lists the maps without downloading any of them (about
400 KB for 130 maps); a map is read, in 256 KB pieces, only when it is
played. A static web host without this server needs that file made beside
the maps.

The lobby artwork in `public/assets/ui/` is placeholder art. Replace any image
with your own, keeping the same file name.

## Internet play / your own domain

**Step-by-step VPS setup (domain, HTTPS, firewall, optional TURN relay and
password): [HOSTING-VPS.md](HOSTING-VPS.md).**

Put any HTTPS reverse proxy in front of `http://<this-computer>:8765`, with or
without a sub-path. For example, Caddy:

```
halo.example.com {
    reverse_proxy 127.0.0.1:8765
}
example.com {
    handle_path /halo/* {
        reverse_proxy 127.0.0.1:8765
    }
}
```

Cloudflare Tunnel, nginx and similar work the same way. WebSockets must be
allowed, and `X-Forwarded-For`/`X-Forwarded-Proto` should be set. Most
proxies do this by default.

- **Native `halo://join` links** need UDP ports **40000-40127** forwarded to
  this computer. The gateway detects your public IP automatically; set
  `nativeGateway.publicIp` if detection picks the wrong one.
- **Players behind strict NATs** may need a TURN relay. Add one to
  `iceServers` in `config.json`, for example a coturn server:

  ```json
  "iceServers": [
    { "urls": ["stun:stun.cloudflare.com:3478"] },
    { "urls": ["turn:turn.example.com:3478"], "username": "halo", "credential": "secret" }
  ]
  ```

## Settings (`config.json`)

| Setting | Meaning |
| --- | --- |
| `http.port` / `https.port` | Ports for plain HTTP (localhost, reverse proxy) and HTTPS (LAN). |
| `https.certFile` / `https.keyFile` | Use your own certificate instead of the generated one (paths relative to this folder). |
| `lobby.externalUrl` | Use another server's lobby instead of this one (e.g. `https://halo.example.com/`). Leave empty normally. |
| `lobby.maxRoomCapacity` | Players per lobby (the game's maximum is 128). |
| `lobby.roomHours` | How long a lobby invite stays valid. |
| `iceServers` | STUN/TURN servers handed to players. |
| `nativeGateway.*` | Native-invite gateway: enable/disable, public IP, UDP port range, `maxSessions` (native games joined at once, 128: one UDP port each) and `maxSessionsPerActor` (from one address, 128: every device behind one router counts as one address). |
| `publicGames.enabled` / `publicGames.brokers` | The in-game server browser's list of public games, and the MQTT brokers it comes from. |
| `delta.enabled` / `delta.url` | ChupathingyCE's game list (`https://halo.milenko.org`), which this server asks for the page: the signed legacy table, its live games for the Server Browser, and a player's stats and Link profile. `false` or empty: none (the game then plays with its built-in numbers). |
| `analytics.umamiScriptUrl` / `analytics.umamiWebsiteId` | Optional Umami page-view tracking: the tracker script's URL and the website ID. Added to the page when served; leave empty for none. |

## Folder layout

```
Start Halo (Windows).bat, start-halo.sh   launchers
config.json                               settings
public/                                   game files and maps (what browsers download)
server/                                   server, bundled Node.js, lobby runtime, gateway
data/                                     created at first start: lobby state, secrets,
                                          certificate, the Custom Edition maps' hashes.
                                          Delete it to reset.
logs/                                     the autostart task's and updates' logs
backups/                                  what the last updates replaced (update.ps1)
```

## Updating

**On Windows**, in PowerShell (as administrator only for a server installed
with `-AsSystem`):

```powershell
powershell -ExecutionPolicy Bypass -File server\windows\update.ps1
```

It downloads the newest kit (`halo-server.zip`, Windows and Linux), stops
the server, moves what it replaces to `backups\<time>\` (it keeps the newest
three), puts the new kit in and starts the server again. If the server does
not answer afterwards, it puts the backup back. Kept as they are:
`config.json`, `data\`, `logs\`, the maps, and lobby artwork you replaced.
`-ServerOnly` keeps this folder's game files and updates only the server;
`-Kit <zip or URL>` installs another kit. What happened goes to
`logs\update.log`. Players get the new game on their next page load.

**Anywhere else**, download the newest kit and copy its contents over this
folder (your `config.json`, `data/` and maps are not in the kit, so they are
kept), then restart the server. [HOSTING-VPS.md](HOSTING-VPS.md) has the
commands for a Linux VPS.

## Building it yourself

The kit is built by the repository's `Web build` workflow
(`.github/workflows/web.yml`): the game (`ninja web` with Emscripten), the
lobby service (`services/signaling`, bundled with `wrangler deploy --dry-run`),
the gateway (`services/native-gateway`, `cargo build --release`), Node.js,
and this server (`services/selfhost`), put together by
`tools/selfhost_package.py`.

To run the server from a checkout (Node.js 22 or newer):

```
# the game: unzip dist/halo-web.zip (tools/web_package.py, after ninja web)
# and rename its halo-web folder to services/selfhost/public
# the lobby service
cd services/signaling
npm ci
npx wrangler deploy --dry-run --outdir ../selfhost/server/signaling
# the gateway, for native halo:// invites (optional; Rust): copy
# target/release/halo-native-gateway[.exe] to
# services/selfhost/server/bin/halo-native-gateway-<linux-x64|win32-x64>[.exe]
cd ../native-gateway
cargo build --release
# the server
cd ../selfhost/server
npm ci --omit=dev
node server.mjs
```

Without the lobby service or the gateway, the server starts without online
play or native invites, and says so.
