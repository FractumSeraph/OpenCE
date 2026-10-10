# Delta in the browser build

[Delta](https://halo.milenko.org/delta) is ChupathingyCE's network family:
what their builds say to each other and to their site beyond OpenCE's game
protocol, which stays OpenCE's byte for byte. Its specification is
`docs/delta.md` in [ChupathingyCE/chupathingyce](https://github.com/ChupathingyCE/chupathingyce)
(CC0, like OpenCE), and every part of it falls back silently to plain OpenCE
when the other side does not speak it. The browser build speaks the parts
below. It compiles ChupathingyCE's own Delta code (`port/linux/src/delta*.c`,
`port/linux/include/delta.h`), as this fork's native builds, which are
ChupathingyCE's, do; what is the browser's own is in `port/web` or inside
`#ifdef HALO_WEB`.

| Part | In the browser |
| --- | --- |
| The legacy number | Joins hosts of network versions 11 to 24, as their builds do (OpenCE: its own version only). |
| The signed legacy table | Fetched and checked (Ed25519); its kill switch obeyed. |
| Delta List | The in-game Server Browser shows each game's host (DEDICATED, LINUX, DELTA / OPENCE), who is playing and the score to win, and lists the games only their site has. |
| Delta Peer | A client of their hosts: the handshake, its platform key (platform "unknown": their registry has no browser), the Custom Edition map's identity (size and BLAKE2b-256 hash), a dedicated server's notices on the HUD. |
| Delta Stats | A game joined through an invite reported, and the player's lines confirmed with this browser's player key (the player can turn it off). |
| Delta Link | Link profile: a code typed at halo.milenko.org/connect links this browser's key to a profile there. |

Not done: hosting with Delta (a browser's games are its own rooms, which
native builds cannot join), the `profile` capability (no player ID is
shared in games), signing in as a server's moderator, chat, event logs.

## What is here

| File | What it is |
| --- | --- |
| `port/linux/include/delta.h`, `port/linux/src/delta*.c` | ChupathingyCE's own, as their builds compile them: the registries, the compatibility table of OpenCE's network versions (`DELTA_LEGACY_VERSIONS`), the signed legacy table, Delta Peer's wire format and sessions. |
| `web_delta.c` | Hands the signed legacy table that the page fetched to `delta.c` (`web_delta_offer_table`), which checks it and keeps it as it keeps any other. |
| `web_delta_peer.c` | Delta Peer's client side, over a socket of the game's Winsock layer; the map identity check; notices. |
| `web_delta_list.c` | Delta List's games for the Server Browser (`menu_functions.c`). |
| `web_delta_stats.c` | A joined game's report (`game_engine.c`, HALO_WEB) to the page. |

The shared files come with each merge of ChupathingyCE (UPDATING.md in the
build folder): nothing is copied by hand. Until October 9, 2026 the
browser had copies of their files in `port/web/src/delta/`; the merge that
built the browser on ChupathingyCE's code (7adb9436) removed them.

The hooks in shared files, all under `HALO_WEB`: `network_client_manager.c`
(the join range; Delta Peer's frame, its stop, the joined game's
advertisement flags, notices to the HUD), `p2p_lobby.c` (the listings'
range), `menu_functions.c` (the Server Browser's Delta List), `game_engine.c`
(the joined game's report) and `main.c` (its frame). If OpenCE changes those
places, keep the hooks.

## The join range (the legacy number)

OpenCE's builds join only hosts of their exact network version. Delta's
builds join every version back to the newest *breaking* one of the
compatibility table (version 11 today), because the changes since were
additive: messages an older machine drops. The browser build does the same:
`network_client_manager.c`'s join check and `p2p_lobby.c`'s listings (both
under `HALO_WEB`) use `delta_legacy_minimum()` and `delta_legacy_maximum()`.
So the in-game Server Browser lists, and joins, OpenCE and ChupathingyCE
games of versions 11 to 24 (24 being this build's own).

**When OpenCE raises its network version**, the new version has no row in
`DELTA_LEGACY_VERSIONS` (`port/linux/include/delta.h`, ChupathingyCE's
file) until ChupathingyCE adds one, once their automation has classified
the raise as additive or breaking; the fork takes it in with their next
merge. The nightly update says so (`CHECK: Delta`).

## The signed legacy table

ChupathingyCE publishes a JSON table, signed with their Ed25519 key
(`delta_key.h`), at `https://halo.milenko.org/v1/delta/legacy` (and `.sig`),
and on GitHub (`delta-table` branch). Its rows are by *wire* (the revision of
the game protocol a build speaks); the browser build has ChupathingyCE's
wire (`DELTA_WIRE` in `port/linux/include/delta.h`), so the table's row for
it sets the browser's numbers as it does their builds'. The browser also
takes its **kill switch** (`disabled_capabilities`: a Delta capability
found unsafe, which this build then never uses) and its serial, which Delta
Peer passes on to hosts with an older one (as theirs do).

The page (`online_client.js`, `startDeltaLegacyTable`) fetches it at start
and every four hours through this site's server
(`services/selfhost/server/delta-list.mjs`; the site sends no CORS headers),
or from GitHub, and hands it to the game (`web_delta_offer_table`), which
checks its size, then its signature, then reads it with a strict parser, and
takes it only when its serial is newer. The log says
`Delta: legacy table N from Delta List: ...`.

## Delta List

While the in-game Server Browser is open the page reads the site's live
games (`/v1/delta/games`, this server's copy of their `/v1/games`, kept 15
seconds) and hands them to `web_delta_list.c`. By each game's invite token,
the Server Browser shows the score to win; below the rows, the host
(`DEDICATED, LINUX, DELTA`, `WINDOWS HOST, DELTA`, `OPENCE`) and who is
playing; the dedicated icon; and a ChupathingyCE host's `<map>@ce` as
`<map> CE`. Games only the site lists come after the listings, joined by
their invite like any.

## Delta Peer

A browser that joins a host whose advertisement has Delta's flag says HELLO
to its port 5160 through the native gateway's tunnel (which carries any
port) and the host answers WELCOME; no answer in 4 seconds is the legacy
protocol alone. The game never waits for it. The log says `Delta Peer: the
host speaks Delta (build ChupathingyCE 0.7.1d, pc_linux, ...); capabilities
0x413, agreed 0x411`.

**Map identity.** A ChupathingyCE host says its Custom Edition map's file
size and BLAKE2b-256 hash. The browser cannot hash a map it has not
downloaded whole, so the site's server hashes each one once, in the
background (`services/selfhost/server/map-hashes.mjs`, about five minutes for
130 maps; kept in `data\custom-map-hashes.json` by name, size and time) and
lists the hash in `custom_maps/index.json`. A browser whose copy differs
from the host's leaves the game and says so; one the site has not hashed
yet is not checked.

## Delta Stats and Link

The page keeps a player key (32 random bytes, in the browser's storage:
clearing the site's data makes a new one) from which the site works out a
public player ID. A few seconds after a game the browser joined through an
invite ends, the game hands its report (as this machine has it: the host's
statistics, which every client is sent) to the page, which sends it
(`/v1/client_report`) and then confirms the local players' lines
(`/v1/claim`), while the player shares their results (Play online, *Stats
on halo.milenko.org*; on by default, as their builds' setting is). The site
confirms a line only from the address the host had the player at: for a
browser, the native gateway's, which is why this server sends these over
IPv4. All browser players of one site share that address.

*Link my profile* asks the site for a code (`/v1/connect/start`), which the
player types at halo.milenko.org/connect while signed in; the page asks
whether to link to that profile and confirms it.

## Tests

`build\tools\smoke\delta.mjs http://localhost:8767/` checks the legacy
table; `--join` then joins an empty ChupathingyCE dedicated server through
the site's gateway and checks the handshake (`--join <invite>` joins that
game: one on a Custom Edition map checks the map identity too). The site
needs a native gateway for `--join` (the staging copy's config can turn one
on, on UDP ports apart from the live one's).
