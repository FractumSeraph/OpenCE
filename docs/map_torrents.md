# Custom Edition maps over BitTorrent

A machine joining a network game on a Custom Edition map it does not have
downloads it, from the other players and from seed boxes, through
BitTorrent; and the machine hosting a game seeds the map it is on, at a
rate that leaves the game's own traffic room. There are some 6,000 Custom
Edition maps, over 500 GB: nobody keeps them all, and this way nobody has
to. No server of this project is needed, as for internet play: peers find
each other through the mainline DHT, and anyone can put a map archive on an
ordinary seed box.

The client is the port's own, in C (`port/linux/src/torrent.c` and the
`torrent_*.c` files beside it): a few thousand lines on the platform
layer's sockets and threads, built into every native port as `p2p.c` is,
so the Linux, Windows, macOS and Android builds all download and seed. The
browser build leaves it out (a page has no TCP or UDP sockets:
`WEB_EXCLUDED_PLATFORM_SOURCES` in `tools/web_build.py`, with
`port/web/src/web_map_torrents.c` in its place); it reads the site's maps
instead. It speaks the peer wire protocol
(BEP 3), the extension protocol (BEP 10) with the metadata exchange (BEP 9)
and peer exchange (BEP 11), the DHT (BEP 5), UDP and HTTP trackers (BEP 15,
BEP 3) and HTTP web seeds (BEP 19). It does not speak the obfuscation
protocol, uTP or HTTPS. `tools/torrent_check.c` runs it outside the game.

## For players

Nothing to set up. Join a game, and if its map is missing the browser's
status line shows the download ("Downloading hugeass.map: 34 MB of 92 MB
(1.2 MB/s, 5 peers)"); when it is there, the join goes ahead by itself.
Custom Edition's `bitmaps.map`, `sounds.map` and `loc.map`, which every
Custom Edition map needs, are downloaded the same way when missing. The
map goes into `maps_ce`, ChupathingyCE's folder of Custom Edition maps (a
map of the same name already there is kept as `<name>.map.old`), and is
marked downloaded (its scripts held to ChupathingyCE's tighter rules); a
download interrupted goes on from where it was, next time, from
`maps_ce\downloads`. What is missing is what ChupathingyCE's map families
(`halo_map_families.h`) do not find: the map in `maps_ce`, OpenCE's
`custom_maps` or the older folders, and the resource maps beside them.

A game hosted on a Custom Edition map is seeded while it is hosted, at
`maps.upload_limit` KB a second at most (512 by default), whatever the
number of players downloading.

The settings, in `config.toml`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `maps.torrents` | `true` | Download a map that is missing when joining; `false` never downloads (and never seeds). |
| `maps.seed` | `"host"` | Seed the map the game is on: `"host"` while hosting, `"all"` also while joined, `"off"` never. |
| `maps.upload_limit` | `512` | KB a second the seeding sends at most, in all; `0` for no limit. |
| `maps.download_limit` | `0` | KB a second a download takes at most; `0` for no limit. |
| `maps.torrent_port` | `0` | The TCP port peers connect to, and the DHT's UDP port; `0` picks one. Forward a fixed one on the router to be reachable from behind other routers. |
| `maps.dht` | `true` | Find peers through the mainline DHT. |
| `maps.trackers` | the archive's tracker | Comma-separated `udp://` and `http://` trackers to announce to. |
| `maps.web_seeds` | the archive's web seed | Comma-separated `http://` folders holding the maps by file name, downloaded from too. |
| `maps.torrent_index` | `"map_torrents.txt"` | The index of the maps' torrents, beside `config.toml`. |

A machine behind a router that forwards nothing can still download (it
connects out to seeds), and seeds to peers that can be reached; two such
machines cannot reach each other, as with any BitTorrent client. Internet
play's UPnP does not forward the torrent port yet.

## How a map is known: the index

`port/assets/network/map_torrents.txt` lists every map's torrent, one a
line: the file name, the header checksum, the size, the piece length and
the info hash. The builds put it beside the game as `map_torrents.txt`
(`maps.torrent_index`). It was made from the halomaps.org and HaloNet
archives (5,829 maps, with the three resource maps) by
`tools/map_torrents.py`.

A host's game record names its map and carries the map file's header
checksum as the map version (`cache_files_map_version`,
source/cache/cache_files.c), which is what a client checks its own copy
against. So a map is identified here by its file name and that checksum,
and two versions of a map with one name are two entries. The three
resource maps have a checksum of 0 and are found by name.

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
torrent files. `tools/test_map_torrents.py` tests the tool.

## Seeding the archive

Anyone can seed: put the maps in a folder, add the `.torrent` files to
any BitTorrent client with that folder as the save path, and let it check
them (the names in the torrents are the files' names). The DHT finds the
seed box; a tracker in `maps.trackers`, or a web seed (a plain HTTP server
with the maps in a folder), makes it found at once. The archive's own seed
box, tracker and web seed are these, and the settings' defaults:

- tracker: `udp://halovps.fractumseraph.net:6969/announce` and
  `http://halovps.fractumseraph.net:6969/announce` (opentracker, taking only
  the index's torrents);
- web seed: `http://halomaps.fractumseraph.net/maps/` (every map of the
  index under its file name; plain HTTP, as the client has no TLS);
- seed box: the same machine, seeding all of them with a libtorrent
  seeder (Transmission could not keep up with some 6,000 torrents); the
  `.torrent` files are in `http://halomaps.fractumseraph.net/torrents/`.

A web seed's file names are percent-encoded in its URLs (some 600 maps'
names have spaces, brackets or letters beyond ASCII).

## How it works in the game

- **Joining** (`ui_widget_port_join`, source/interface; the check itself,
  `ui_widget_port_join_map_fetch`, in `port/linux/game/menu_functions.c`):
  before the join starts, the advertised game's map is looked for in its
  family's folders (`map_family_find`) and its version compared with the
  host's; `map_torrents_fetch` then finds what is missing, the map (absent
  or another version) and each resource map. If anything is missing and
  the index has it all, `map_torrents_fetch` starts
  the downloads and the join waits; the browser (LAN, Direct Link or the
  server browser) shows the status line, and once every file is in place
  `map_fetch_update` focuses the game's row and posts the join again. The
  map is looked for in the index by its name and the host's version; a host
  whose version is none of the index's (a build that sends another kind of
  checksum) gets the index's map of that name, when this machine has no
  map of that name at all (one it has is that host's other version, which
  a download cannot change). A
  game no longer listed by then is logged. A download that fails shows why
  for a while. Leaving the browser leaves the download running; it is let
  go of only when the game quits (what came stays for next time).
- **A map changed after joining** (the host changes map in the lobby:
  `cache_files_map_present`, as the host's settings arrive): the files are
  downloaded the same way, and the error the player gets says to join again
  when it is done, instead of where to copy the map from.
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
  Peers come from the DHT, the trackers, other peers (PEX) and incoming
  connections on the listening port; web seeds fetch whole pieces by byte
  range (a server must answer 206 with the range asked for), over eight
  connections shared out among the web seeds given, at least one each.

## Tests

- `tools/test_map_torrents.py`: the tool's torrents and index, from tiny
  caches made in memory; and the committed index's form.
- `tools/torrent_check.c`: the client alone, seeding a file on one port
  and downloading it on another (two copies, on the loopback), as
  `tools/test_linux_port.py` does for the lobby. By hand, with a map from
  the index:

  ```
  torrent_check seed bloodgulch.map 384911e8... 262144 6890 --upload-limit 524288
  torrent_check get bloodgulch.map 13936092 384911e8... 262144 127.0.0.1:6890 out.map
  ```

  which was checked to give the file byte for byte, at the limit (512 KB/s).

## Not yet

- The host could tell a joining client its own torrent port, so the
  client downloads from the host at once without the DHT (a peer hint in
  the join; `torrent_add_peer` is there for it). Left for when the join
  handshake is signed.
- UPnP for the torrent port (internet play's `posix_upnp_forward_udp` does
  UDP only).
- HTTPS trackers and web seeds (no TLS on every port), the obfuscation
  protocol, uTP, IPv6.
- Fetching the index from a URL, so new maps need no new build.
