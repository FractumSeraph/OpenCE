# Custom Edition maps over BitTorrent

A machine joining a network game on a Custom Edition map it does not have
downloads it before it joins: first out of the zip the community's map
archives keep it in (HaloNet's and HaloMaps's), else from the other players
and from seed boxes, through BitTorrent; and the machine hosting a game
seeds the map it is on, at a rate that leaves the game's own traffic room.
Whatever its source, a map is used only once it is the index's: its
torrent's info hash checks every byte. There are some 6,000
Custom Edition maps, over 500 GB: nobody keeps them all, and this way
nobody has to. Anyone can put a map archive on an ordinary seed box or web
server.

Nothing of this goes over the game's own connections, so it needs no
network version of its own (`port/linux/NETCODE.md`): a client looks the
host's map up by the name and version (the header checksum) that the game's
advertisement and settings already carry.

The client is the port's own, in C (`port/linux/src/torrent.c` and the
`torrent_*.c` files beside it): a few thousand lines on the platform
layer's sockets and threads, built into every native port as `p2p.c` is,
so the Linux, Windows and Android builds all download and seed. The web
build leaves it out (a page has no TCP or UDP sockets, and plays no Custom
Edition maps: `WEB_EXCLUDE` in `tools/web_build.py`, with
`port/web/src/web_map_torrents.c` in its place). It speaks the peer wire
protocol (BEP 3), the extension protocol (BEP 10) with the metadata exchange
(BEP 9) and peer exchange (BEP 11), UDP and HTTP trackers (BEP 15, BEP 3)
and HTTP web seeds (BEP 19). It does not speak the DHT, the obfuscation
protocol, uTP or HTTPS. `tools/torrent_check.c` runs it outside the game.

## For players

Nothing to set up. Join a game (LAN, Direct Link or the server browser),
and if its Custom Edition map is missing, Halo's loading screen covers the
menus while it downloads, its light sweeping from left to right as the map
comes, with the map's name and how much has come ("34 MB of 92 MB (1.2 MB/s,
5 peers)"); when it is there, the join goes ahead by itself. B stops the
download. With `maps.download_screen = false` the menus stay up and the
games' list's status line shows the download instead ("Downloading
hugeass.map: 34 MB of 92 MB (1.2 MB/s, 5 peers)").

The loading screen's picture is the Xbox's own (the Halo ring and
LOADING...), which importing a disc image writes into the maps folder as
`loading.tga` (the retail disc has no such file: its `default.xbe` holds the
picture, taken out by `port/linux/src/xiso.c`, the Android app's
`XisoExtractor.java` and the web page's `xiso-worker.js`). Without one, from
a maps folder copied by hand, the light sweeps over clouds the game makes.

Most maps come from the community's archives, the zips their authors
published: HaloNet's map repository (maps.halonet.net) and HaloMaps
(halomaps.org), tried in `maps.archives`'s order ("Downloading hugeass.map
from HaloNet: 12 MB of 40 MB"); a map that none of them gives, or that is
not the index's version once out of its zip, comes over BitTorrent. The
archives are fetched over HTTPS, which the Linux and Windows builds have
(the self-updater's); Android takes every map over BitTorrent.

Custom Edition's `bitmaps.map`, `sounds.map` and `loc.map`, which every
Custom Edition map needs, are downloaded the same way when missing. A map
goes into `custom_maps`; a download interrupted goes on from where it was,
next time, from `custom_maps/downloads`.

No map already there is ever replaced, renamed or changed. When the host's
map is another version of one this machine has (`hugeass.map` with another
header checksum), the host's goes beside it, under its own name, in
`custom_maps/versions/<checksum>/hugeass.map` (the checksum as 8
hexadecimal digits), and a client of a game on that version plays that one
(`CUSTOM_EDITION_VERSIONS_FOLDER`, `port/linux/game/custom_edition_cache.h`).
The map lists show only the maps of `custom_maps` itself, so hosting a game
on `hugeass` hosts the player's own.

A game hosted on a Custom Edition map is seeded while it is hosted, at
`maps.upload_limit` KB a second at most (512 by default), whatever the
number of players downloading.

The settings, in `config.toml` (`port/linux/README.md` has every setting):

| Setting | Default | Meaning |
| --- | --- | --- |
| `maps.torrents` | `true` | Download a map that is missing when joining; `false` never downloads (and never seeds). |
| `maps.archives` | `"halonet,halomaps"` | The community's map archives a missing map is first taken from, out of the zip that holds it, in this order: `halonet` (maps.halonet.net), `halomaps` (halomaps.org). Empty: BitTorrent alone. Linux and Windows. |
| `maps.download_screen` | `true` | Halo's loading screen over the menus while a map downloads (B stops it); `false` leaves the menus up, with the status line. |
| `maps.seed` | `"host"` | Seed the map the game is on: `"host"` while hosting, `"all"` also while joined, `"off"` never. |
| `maps.upload_limit` | `512` | KB a second the seeding sends at most, in all; `0` for no limit. |
| `maps.download_limit` | `0` | KB a second a download takes at most; `0` for no limit. |
| `maps.torrent_port` | `0` | The TCP port peers connect to (and the UDP port the UDP trackers answer on); `0` picks one. Forward a fixed one on the router to be reachable from behind other routers. |
| `maps.trackers` | the archive's tracker | Comma-separated `udp://` and `http://` trackers to announce to. |
| `maps.web_seeds` | the archive's web seed | Comma-separated `http://` folders holding the maps by file name, downloaded from too. |
| `maps.torrent_index` | `"map_torrents.txt"` | The index of the maps' torrents, beside `config.toml`. |

Nothing is downloaded or seeded while Custom Edition maps cannot be played
(`game.custom_edition`). A machine behind a router that forwards nothing can
still download (it connects out to seeds and fetches from the web seeds),
and seeds to peers that can reach it; two such machines cannot reach each
other, as with any BitTorrent client. Internet play's UPnP does not forward
the torrent port yet.

## How a map is known: the index

`port/assets/network/map_torrents.txt` lists every map's torrent, one a
line: the file name, the header checksum, the size, the piece length and
the info hash, then, a column each, the archives' zips that hold it:
`halonet:<zip file name>` (https://maps.halonet.net/maps/<zip file name>) and
`halomaps:<file id>` (https://halomaps.org/hce/detail.cfm?fid=<file id>);
5,826 of the 5,829 maps have at least one. The builds put it beside the game as `map_torrents.txt`
(`maps.torrent_index`; Android's APK has it as an asset, which the app
copies into its data folder as it does `brokers.txt`). It was made from the
halomaps.org and HaloNet archives (5,829 maps, with the three resource
maps) by `tools/map_torrents.py`.

A host's game names its map (`custom_maps\<name>`) and carries the map
file's header checksum as the map version (`cache_files_map_version`,
source/cache/cache_files.c: `custom_edition_map_checksum`), which is what a
client checks its own copy against. So a map is identified here by its file
name and that checksum, and two versions of a map with one name are two
entries. The three resource maps have a checksum of 0 and are found by
name. A host's version that is not in the index is not downloaded.

Each torrent is a single-file v1 torrent whose info dictionary is exactly
`{length, name, piece length, pieces}`, in that order, with the piece
length a power of two from 256 KB to 8 MB. So a machine that has the file
can make the torrent again from the index line alone: that is how the host
seeds the map it plays, with no torrent file on disk, and how the client
checks a torrent's metadata from peers against the index (its hash must be
the info hash, and its length and piece length the index's). The torrents
have no trackers in them: the game adds `maps.trackers` and
`maps.web_seeds` at runtime, which does not change an info hash, and a seed
box's client adds its own.

To make the torrents and the index from a folder of maps:

```
python tools/map_torrents.py make --maps <folder> [--maps <folder>...]
    --resource-maps <Custom Edition maps folder> --torrents <output folder>
```

which writes `<info hash>.torrent` files (for seed boxes) and merges the
entries into the index, remembering the files hashed in
`<output folder>/.map_torrents_cache.json` so that a rerun hashes only new
ones. `python tools/map_torrents.py check` checks an index against its
torrent files. The archives' columns come from copies of their zips:

```
python tools/map_torrents.py sources --halonet <folder> --halomaps <folder>
```

with HaloNet's zips under their names and HaloMaps's named
`<file id>-<anything>.zip`; a zip holds a map when one of its files has the
map's name (in any folder), size and header checksum.
`tools/test_map_torrents.py` tests the tool.

## Seeding the archive

Anyone can seed: put the maps in a folder, add the `.torrent` files to
any BitTorrent client with that folder as the save path, and let it check
them (the names in the torrents are the files' names). A tracker in
`maps.trackers`, or a web seed (a plain HTTP server with the maps in a
folder), makes it found. The archive's own seed box, tracker and web seed
are these, and the settings' defaults:

- tracker: `udp://halovps.fractumseraph.net:6969/announce` and
  `http://halovps.fractumseraph.net:6969/announce` (opentracker, taking only
  the index's torrents);
- web seed: `http://halomaps.fractumseraph.net/maps/` (every map of the
  index under its file name; plain HTTP, as the client has no TLS);
- seed box: seeding all of them with a libtorrent seeder; the `.torrent`
  files are in `http://halomaps.fractumseraph.net/torrents/`.

A web seed's file names are percent-encoded in its URLs (some 600 maps'
names have spaces, brackets or letters beyond ASCII).

## How it works in the game

- **Joining** (`ui_widget_port_join`, source/interface; the check itself,
  `ui_widget_port_join_map_fetch`, in `port/linux/game/menu_functions.c`):
  before the join starts, `custom_edition_cache_files_missing`
  (`port/linux/game/custom_edition_cache.c`) names what this machine lacks
  of the advertised game: the map (absent, not a Custom Edition cache, or
  another version than the host's, in the versions folder too) and each
  resource map. If anything is missing and the index has it all,
  `map_torrents_fetch` starts the downloads and the join waits; the download
  screen (`port/linux/game/download_screen.c`) covers the menus with Halo's
  loading screen (`interface/progress_bar.c`) at the download's share, the
  menus going on behind it without taking presses (with
  `maps.download_screen` off, the games' list shows the status line), and
  once every file is in place `map_fetch_update` focuses the game's row and
  posts the join again. A game no longer listed by then is logged. A
  download that fails shows why for a while. B on the download screen stops
  the download and the join that waited for it
  (`ui_widget_port_map_fetch_cancel`; what came stays for next time). With
  the screen off, leaving the list leaves the download running; it is let
  go of only when the game quits.
- **The archives** (`port/linux/src/map_archives.c`): a file whose index
  line names zips of the archives in `maps.archives` is taken from them
  first, one file at a time, on a thread of its own: HaloNet's zip by its
  URL; HaloMaps's as its page's Download button fetches it (the file's page
  gives a token, which a POST of the page's form sends back for the zip).
  Both over HTTPS with the self-updater's download (`update.h`:
  `update_request`, Mbed TLS on Linux and WinHTTP on Windows, the servers'
  certificates checked). The zip goes beside the download
  (`custom_maps/downloads/<name>.zip`) and the map, its file of the map's
  name and size in any folder, is taken out of it (`map_archives_zip.c`:
  stored or deflated with the port's zlib, its CRC checked, the zip's
  directory read with every offset and size checked); the zip is deleted
  after. The map is then added to the BitTorrent client as a seed, which
  hashes it: one that is the torrent's is put in place as a downloaded one
  is; one that is not, or a file no archive gave, is downloaded over
  BitTorrent. B stops the archive's download at its next block.
- **A map changed after joining** (the host changes map in the lobby:
  `cache_files_map_present`, as the host's settings arrive): the files are
  downloaded the same way, and the error the player gets says to join again
  when it is done, instead of where to copy the map from.
- **The version played** (`custom_edition_cache_present`): a client checking
  the host's map remembers the host's version, and while it is a client the
  game opens `custom_maps/versions/<checksum>/<name>.map` for that map when
  there is one, else the map of that name; `network_game_client_dispose`
  forgets it (`custom_edition_map_version_forget`).
- **Loading screen** (`interface/progress_bar.c`): the game's own, which the
  port did not draw (it uses the fixed-function pipeline: `d3d8_gl.c` now
  has a vertex shader for it, `nv2a_fixed_function_glsl`, and transforms its
  positions by the world, view and projection matrices), with its trail
  (each frame draws the last one again, a little larger and fainter: the
  front buffer, `GetBackBuffer` with -1, which `d3d8_gl.c` keeps as a
  640x480 render target), drawn in the screen's middle 640 columns as the
  menus are on a wide screen. After it, the rasterizer's caches of the
  device's states are made the device's again
  (`rasterizer_forget_hardware_state`), since it sets them itself.
- **Seeding** (`map_torrents_playing`): the server tells its map as it
  updates its listing (`network_game_server_list`), the client its as the
  host's settings arrive, and both say none as they are disposed of. The
  map seeded is the host's, or with `maps.seed = "all"` the client's too;
  it is checked (hashed) when seeding starts, which takes a second or two
  for a large map, on the client's thread.
- **The client** (`torrent.c`) runs on a thread of its own, waiting on its
  sockets with `posix_socket_select` and ticking every 100 ms; the game's
  thread calls its API under its lock (`torrent_status_get` for the status
  line, once every half second). Pieces are written to the file as their
  blocks come, and a piece is read back and hashed once whole; a bad one is
  asked for again. Rate limits are token buckets over the whole client.
  Peers come from the trackers, other peers (PEX) and incoming connections
  on the listening port; web seeds fetch whole pieces by byte range (a
  server must answer 206 with the range asked for), over
  `TORRENT_MAXIMUM_WEB_SEEDS` (8) connections shared out among the web
  seeds given (those past the eighth are left out). When no peer has given
  the metadata within 10 seconds, the whole file is fetched from the web
  seeds and checked against the info hash; three wrong answers in a row on
  a connection stop that.

## Tests

- `tools/test_map_torrents.py`: the tool's torrents and index, from tiny
  caches made in memory; and the committed index's form.
- `tools/test_torrent_check.py`: the client alone (`tools/torrent_check.c`,
  built with the client and the platform layer's POSIX files), one copy
  seeding a made-up file on the loopback and another downloading it, which
  must give the file byte for byte. By hand, with a map from the index:

  ```
  torrent_check seed bloodgulch.map 384911e8... 262144 6890 --upload-limit 524288
  torrent_check get bloodgulch.map 13936092 384911e8... 262144 127.0.0.1:6890 out.map
  ```

- `tools/test_torrent_check.py` also takes a map out of zips made in the
  test (`torrent_check unzip`: deflated and stored, in a folder, beside one
  of its name and another size), and refuses damaged ones.
- `port/web/tests/xiso.test.js`: the loading screen's picture taken out of
  made-up executables (`xiso-worker.js`'s `loadingPicture`); `xiso.c` and
  `XisoExtractor.java` decode it the same way.

## Not yet

- The host could tell a joining client its own torrent port, so the
  client downloads from the host at once (a peer hint in the join;
  `torrent_add_peer` is there for it). That would change what the machines
  send each other, and so the network version.
- UPnP for the torrent port (internet play's `posix_upnp_forward_udp` does
  UDP only).
- The mainline DHT (BEP 5), so that seed boxes are found without a tracker.
- HTTPS trackers and web seeds (no TLS on every port), the obfuscation
  protocol, uTP, IPv6.
- Fetching the index from a URL, so new maps need no new build.
