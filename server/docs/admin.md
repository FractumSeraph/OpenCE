# Running a server: commands, console and control API

Who may run which command (moderators, admins, owners), the control panel's
accounts, remote access over HTTPS and the optional link to halo.milenko.org
are in [moderation.md](moderation.md).

A running server takes commands, named after Halo PC's dedicated server's
(`sv_players`, `sv_kick`, `sv_map`, ...), from three places:

- **its console**: type them where the server runs;
- **a startup file**: commands run once the server first hosts;
- **its control API**: HTTP and JSON on a port of its own, off unless you
  turn it on, for scripts and bots, with a **web admin page** on the same
  port.

Every command runs on the game's main thread, between frames, whichever
place it came from. Kicks, bans and map changes use what the game's network
protocol already does: players need nothing new to play on a server that
uses them, and nothing about joining changes. No command, response or log
line the API hands out shows a player's address, but `sv_suspects` (this
fork's, a moderator's: who the fair play checks found, kept for a ban).

## The commands

`<player>` is a player's number from `sv_players`, or their name: the
whole name in either case, else the beginning of one name (`sv_kick mil`).
A name with spaces goes in double quotes: `sv_kick "Master Chief"`.

A reason (`sv_warn`, `sv_kick`, `sv_ban`) is the rest of the line, up to
127 characters: `sv_warn 2 Please keep it friendly`. In double quotes it is
the quoted text alone, and nothing may follow it:
`sv_ban "Master Chief" 2h "camping, again"`. On `sv_ban` it comes after
the duration.

| Command | What it does |
| --- | --- |
| `help [command]` | Lists the commands, or tells what one does. |
| `sv_status` | The server: name, version and network version, the OpenCE build it follows (the legacy table's, [Delta](../../docs/delta.md#following-opence)), state (lobby, loading, in game, carnage report), map and game type, playlist entry, players, whether it is public, uptime. |
| `sv_players` | The players: number, name, team, score and ping (in a game), and their machine's hardware id. |
| `sv_warn <player> <reason>` | Warns the player: a ChupathingyCE game shows the reason (Delta Peer); the warning is in the audit file either way. |
| `sv_kick <player> [reason]` | Drops the player, and every other player on their machine (split screen). They see "game closed" and may join again; a ChupathingyCE game is told the reason first. |
| `sv_ban <player> [duration] [reason]` | Drops the player and keeps their machine out: for ever, or for a while (`30m`, `2h`, `7d`, `1w`, `1d12h`; a bare number is minutes). Every player in the game is told. bans.txt's reason says by whom, how long and why. |
| `sv_unban <ban>` | Takes a ban out, by its number in `sv_banlist`. |
| `sv_banlist` | The bans: number, when, hardware id, how long is left, the players' names and why. Never their addresses. |
| `sv_suspects [last]` | What the fair play checks found of this game's players (`last`: the last game's): their bullets' hits on players checked, of them through a wall and far off aim (each with how many were refused), snaps of the aim, headshot kills of kills, how much of the time an enemy near them was behind a wall they aimed at one, whether they are a suspect, their address and hardware id ([below](#fair-play-checks)). A moderator's (warn). |
| `sv_map <map> <game type>` | Plays that map and game type now: the game in progress ends at once (no carnage report), and after the new one the playlist goes on where it was. In the lobby it is set at once. Maps: `bloodgulch`, a Custom Edition map as `<name>@ce`, a HaloMD map as `<name>@md`, a Halo PC map as `<name>@pc` ([Playlists](playlists.md)). Game types: `slayer`, `team_slayer`, `ctf`, `king`, `oddball`, `race`, ... |
| `sv_maps` | The maps this server can play (its multiplayer maps: Xbox, `<name>@ce`, `<name>@md`, `<name>@pc`) and the game types `sv_map` takes. |
| `sv_mapcycle` | The playlist, and which entry is playing or next. |
| `sv_mapcycle_next` | Skips to the playlist's next entry now (the game in progress ends without its carnage report). |
| `sv_end_game` | Ends the game in progress, as its score limit would: the carnage report shows, then the next entry's lobby opens. |
| `sv_maxplayers [count]` | Shows or sets the most players a game takes (1 to 128; no fewer than `HALO_DEDICATED_MINIMUM_PLAYERS`). In the lobby at once, otherwise from the next lobby. |
| `sv_name [name]` | Shows or sets the server's name on the lists (15 characters at most). In the lobby at once, otherwise from the next lobby. |
| `sv_name_filter [0\|1]` | Shows or sets the name filter: with `1`, a player whose name is in the data folder's `blocked_names.txt` is warned and kicked ([settings.md](settings.md#blocked-names)). Saved in the settings file, and in effect at once. Off unless set. |
| `sv_playlists`, `sv_playlist <name>`, `sv_playlist_new`, `sv_playlist_add`, `sv_playlist_remove`, `sv_playlist_move`, `sv_playlist_delete`, `sv_playlist_use`, `sv_mapcycle_add`, `sv_mapcycle_del` | Playlists made and edited on the server ([playlists.md](playlists.md#playlists-made-on-the-server)). |
| `sv_gametypes`, `sv_gametype <name>`, `sv_gametype_new`, `sv_gametype_set`, `sv_gametype_delete` | Game type files ([playlists.md](playlists.md#game-type-files)). |
| `sv_settings`, `sv_set <setting> <value>` | The settings file ([settings.md](settings.md)). |
| `sv_mod_list`, `sv_mod_add`, `sv_mod_remove` | Moderators by moderator key ([moderation.md](moderation.md#the-consoles-commands)). |
| `sv_link`, `sv_unlink`, `sv_link_status` | The link to halo.milenko.org ([moderation.md](moderation.md#the-link-to-halomilenkoorg-optional)). |
| `sv_admin_list`, `sv_admin_add <name>`, `sv_admin_rotate <name>`, `sv_admin_remove <name>` | The control API's credentials, one for each admin ([below](#a-token-for-each-admin)). The console's only. |
| `sv_account_list`, `sv_account_invite <role>`, `sv_account_role`, `sv_account_remove`, `sv_account_reset`, `sv_account_setup` | The control panel's accounts ([moderation.md](moderation.md#accounts-and-invitations)). The console's only. |

A team game cannot start with one player; with one player waiting, the
server plays the next entry without teams instead, whether the team game
was the playlist's or `sv_map`'s.

What a command changes lasts until the server stops: the settings in its
environment are what it starts with again.

### Bans

A ban is a line in `bans.txt` in the data folder, the same file the game's
own host ban command and its cheat detection write. Each line has the
machine's hardware id (`hwid=`, a hash the game makes from the machine's
own id; not a serial, and the same on every server) and its address
(`ip=`); a machine with either is refused when it joins. A ban for a while
has `until=` (seconds since 1970, UTC) and is ignored once that has passed.

The file is read on every join, so editing it by hand takes effect at once,
without a restart: delete a line to lift a ban, or add `#` before it.
Keep the file private: it holds the banned players' addresses, which no
command ever shows.

A machine that tells no hardware id (macOS builds have none today) is
banned by its address alone; `sv_ban` says so.

### Shared ban lists

A server can take the bans other hosts publish as its own (this fork:
`port/linux/src/shared_bans.c`). Name their lists in
`HALO_NET_SHARED_BANS` (`network.shared_bans`): https:// addresses, with
commas between them, for example FractumSeraph's servers' list:

```
HALO_NET_SHARED_BANS=https://halovps.fractumseraph.net/bans/shared.txt
```

The server fetches them at start and every 10 minutes into
`shared_bans.txt` beside `bans.txt` (a list that cannot be fetched keeps its
bans of the time before), and refuses a machine whose hardware id (or
address, if a list has one) is in them, as it does its own bans. A list is
`bans.txt`'s form, a line a ban; nothing else of it is kept. `sv_banlist` and
`sv_unban` are about `bans.txt` alone: a shared ban is lifted by its
publisher. Name only lists whose publishers you trust: their bans are
yours.

To publish your own, serve your bans over HTTPS without their addresses, as
FractumSeraph's servers do with `server/deploy/halo-bans-publish` (run every
2 minutes by `halo-bans-publish.service` and `.timer` beside it, its file
served by the web server): each ban's time, hardware id, players, reason and
end, never its IP address or Discord user, and no ban without a hardware id.
Two servers on one machine share one list by linking their `bans.txt` to
one file (each service allowed to write where it is: `ReadWritePaths`).

### Fair play checks

This fork's servers check every bullet a player reports hitting another
player (`port/linux/game/network_fairplay.c`). In this netcode the shooter's
own game decides what its shots hit and tells the server, which already
checks the weapon, its rate and range and where the target was; a modified
game can still claim hits its player never aimed at (silent aim) or that
went through walls. The server knows where each player's eyes were and
where they aimed every tick, and the map's walls, so for each such hit:

- **Through a wall**: no point the shooter was at in the time the shot
  flew had a line of sight to where it hit (the map's solid surfaces; not
  glass, fences or grates). It is refused when the wall is solid: blocked
  both ways, and from around the shooter's eyes too, so a corner they
  peeked past never counts.
- **Off aim**: the shooter never aimed within 15 degrees of where it hit.
  It is refused beyond 45 degrees, which no spread reaches. Guided and
  falling projectiles (needles, plasma, grenades) are not checked, nor
  shots from vehicles.
- **Snap**: the aim turned faster than 25 degrees a tick just before it
  landed on the target. Only noted: people flick too.

Every few ticks it also notes, for each player, whether an enemy near them
is behind a wall, and whether they aim at one that is.

**Teleports and flying.** A player's game tells the server where they are,
and the server already refuses a position that is too far from where it
has them, too fast since the last it took, or higher above the ground than
a jump takes them; it keeps its own copy, which falls and walks as their
input has it, so the cheat snaps back. The checks count those refusals: a
burst of them too far or too fast is a teleport attempt, a refusal too
high a tick of flying, and so is a player hovering (in the air for over 2.5
seconds, rising and falling less than half a world unit). They are never
counted for 2 seconds after the server moves a player itself (a
teleporter, a new life, getting into or out of a vehicle), and each line
says the player's ping (a bad link has the odd refusal; a cheat, many).
Three teleport attempts or three seconds of flying in a game make a
suspect. A player who sees
through walls (ESP) follows hidden enemies far more than one who guesses
where someone will come round a corner.

`HALO_NET_FAIR_PLAY` (`network.fair_play`): `refuse` (the default) refuses
the hits above and logs; `log` only logs; `off` checks nothing. A player is
a **suspect** after 3 refused hits in a game, or when, after a minute in
all of enemies near them being hidden, they aimed at one a quarter of that
time. The server never kicks or bans by itself: the judgement is yours.

What it finds:

- `fairplay.txt` in the data folder, a line each in `bans.txt`'s form (when,
  `ip=`, `hwid=`, Discord user, the players' names), its reason what was
  seen: `fair play: through solid wall on Bob, 14.2 away, 38 deg off aim
  (refused)`. At most 8 such lines for a player a game, a line when they
  become a suspect, and at the end of each game a line with their counts
  for each player with anything to look at. Keep it private, as `bans.txt`.
- `fair play:` lines in the log.
- `sv_suspects` (and `GET /v1/suspects`, `/v1/suspects/last`, and the
  control panel's **Suspects** tab): the game's players and the last game's.

A suspect is someone to watch, not proof: look at their counts beside the
others', and at what they did, before you ban: watch them live
([moderation.md](moderation.md#watching-a-player-this-fork)), or in a
recording.

### Recordings

With `HALO_NET_RECORD_GAMES=true` (`network.record_games`) the server
records every game (`port/linux/game/network_recording.c`) to
`recordings/<date>_<time>_<map>.hrec` in the data folder: the map's walls,
each player's name, team, address and hardware id, every tick each living
player's position, eye height, aim, crouch, triggers, zoom, health, weapon
and vehicle, each bullet's hit on a player with the fair play checks'
verdict, and each kill. That is about 40 MB an hour of a full game; once
the folder holds more than `HALO_NET_RECORDINGS_MEGABYTES`
(`network.recordings_megabytes`, 2048 by default; 0 keeps everything) the
oldest go. A game in progress is on disk to within 10 seconds. Keep the
folder private: it holds players' addresses.

To watch one, save it from the control panel's **Suspects** tab
(**Recordings**: `GET /v1/recordings`, and `/v1/recordings/<name>` for one;
a moderator's, as `sv_suspects` is, and each one taken is in the audit
file), or copy it off the server, and open it at
https://halo.fractumseraph.net/assets/replay/ (`port/web/assets/replay`).
The file is read in your browser and sent nowhere. The page draws the map's
walls and every player with their aim. It follows a player through their
eyes (1), from behind them (2) or freely (3), with players behind walls
shown through them (X). Its list and timeline mark the hits the checks
refused (red) or doubted (orange), and the kills; a click on one jumps to
two seconds before it, through the shooter's eyes. **Record video** saves
the view as a video for a ban's evidence. A recording a server keeps
somewhere private opens with `index.html?file=<address>`.

Recordings may live somewhere else once their game is over: list them in
`recordings/archived.txt`, a line each (name, bytes, time in seconds since
1970, with tabs between), and delete them from the folder. The panel lists
them too, as archived, and saves one from `/archive/<name>` on its own
address, which the web server in front of it must answer. FractumSeraph's
servers move theirs to a seedbox every 10 minutes
(`server/deploy/halo-recordings-offload`, its timer, and
`halo-recordings-receive` on the seedbox, its SSH key allowed nothing
else); Caddy answers `/archive/` by asking the panel whether the request's
login may (`forward_auth` to `/v1/recordings`), then fetching the file from
the seedbox with a token its nginx asks for:

```
@archive path_regexp ^/archive/[A-Za-z0-9_.-]+\.hrec$
handle @archive {
	forward_auth 127.0.0.1:8080 {
		uri /v1/recordings
	}
	uri strip_prefix /archive
	rewrite * /halo-archive/halo-dedicated{uri}
	reverse_proxy https://halo.fractumseraph.net {
		header_up Host halo.fractumseraph.net
		header_up X-Halo-Archive {env.HALO_ARCHIVE_TOKEN}
		header_up -Cookie
		header_up -Authorization
	}
}
handle {
	reverse_proxy 127.0.0.1:8080
}
```

### Public replays and watching live

With `HALO_NET_RECORD_PUBLIC=true` (`network.record_public`) as well, the
server writes each game's public copy to `recordings/public/` under the same
name, for anyone to watch, live or again, in the replay viewer. It is the
recording without anything private:

- each player's name, team and every move are there; their addresses and
  hardware ids are not (empty);
- the hits are there, without the fair play checks' verdicts, how far off
  aim they were or how far they went (zeros), and the moves the host refused
  are left out: a suspicion is for moderators, not for everyone;
- while the game is played, each part is written only
  `HALO_NET_RECORD_PUBLIC_DELAY` seconds (`network.record_public_delay`, 30
  by default) after it happened, so that no one watching can tell its
  players where the others are; the rest is written when the game ends.

`recordings/public/live.json` says which copy is being written (`"live":
true`, its file, server, map, game type, start and delay); once the game's
carnage report is on the game list, `<name>.json` beside its copy gives the
report's address, which the viewer links. The oldest go once the folder
holds more than `HALO_NET_RECORD_PUBLIC_MEGABYTES`
(`network.record_public_megabytes`, 2048 by default; 0 keeps everything).

Serve `recordings/public/` alone, never `recordings/`, with its listing in
JSON and range requests (the viewer reads a game being played a part at a
time), to any origin. FractumSeraph's servers do it with Caddy:

```
handle_path /replays/fractumffa/* {
	root * /opt/halo-dedicated/data/recordings/public
	header Access-Control-Allow-Origin *
	header Access-Control-Expose-Headers "Content-Range, Content-Length, Accept-Ranges"
	header Cache-Control "no-cache"
	file_server browse {
		hide *.new
	}
}
```

The viewer then watches a server's game as it is played at
`index.html?live=<that folder's address>`, and opens a finished one at
`index.html?file=<its address>`. Opened with neither, it lists the games of
the servers named in `servers.json` beside it (`[{"name", "live": the
folder's address, "replays": where finished ones are kept, if elsewhere}]`,
or `"public"` for one folder holding both): FractumSeraph's at
https://halo.fractumseraph.net/assets/replay/. A public copy shows no fair
play panels.

A small server need not keep its finished games. FractumSeraph's VPS keeps
only the game being played: `halo-recordings-offload` moves each finished
public copy and its `<name>.json` to the seedbox (`halo-recordings-receive
public <server> <name> <bytes>`, the same restricted key), into
`~/halo-replays/<server>/`, which nginx serves to anyone at
https://halo.fractumseraph.net/replays/ with its listing in JSON
(`autoindex_format json`), up to 100 GB, the oldest going first. The
servers' own limits there are small (`HALO_NET_RECORD_PUBLIC_MEGABYTES=300`,
`HALO_NET_RECORDINGS_MEGABYTES=1024`).

### Why there is no `sv_password`

A join password needs the player's game to send one. A join request in the
game's protocol carries no password a player could type (its join token is
the game's own), and no build asks for one, so a password would be a
protocol change. It is left out until the game has a way to carry one that
every build understands.

## The console

When the server runs in a terminal, type commands into it; their output
appears below, between the server's own lines:

```
sv_players
  #  name         team  score   ping  id
  1  Milenko      red      12     38  3f2a9c1b5d...
  2  Odb718       blue      9     61  none
sv_kick 2
kicked Odb718
```

The console reads standard input only when it is a terminal. In a
container, run it with `docker run -it` (and `docker attach` to it), or set
`HALO_DEDICATED_CONSOLE=true` to read commands from a pipe; `false` turns
the console off even in a terminal.

## Startup commands

`HALO_DEDICATED_COMMANDS` names a file in the data folder (as
`HALO_DEDICATED` names the playlist): one command a line, run in order once
the server first hosts its game. Their output is in the log. Lines that
begin with `#` are comments.

```
# startup.txt
sv_name "Friday Night"
sv_maxplayers 16
sv_map ratrace slayer
```

## The control API

Off unless `HALO_DEDICATED_CONTROL` is set:

| `HALO_DEDICATED_CONTROL` | Listens on |
| --- | --- |
| (not set), `false` | nothing: no API |
| `8080` | `127.0.0.1:8080`, this machine only |
| `127.0.0.1:8080` | the same |
| `[::1]:8080` | IPv6's loopback |
| `0.0.0.0:8443`, `192.0.2.10:8443` | every address, or that one: beyond this machine, HTTPS only ([moderation.md](moderation.md#remote-access-safely)). |

On the loopback address it is plain HTTP: reach it through an SSH tunnel
(below), or put a reverse proxy with TLS in front. Beyond the loopback
address it is HTTPS, with the server's own certificate (its fingerprint
printed at start) or yours.

### The token

There is no default password. The first time the API is turned on, the
server makes a token and prints it once, on its output (not in
`debug.txt`):

```
ChupathingyCE Dedicated Server: the control API's token, shown this once:

    chce_4f0c...e91a
```

Copy it somewhere safe. The server keeps only a salted Argon2id hash of it,
in `control_credentials.txt` in the data folder (readable by its owner
only). With Docker the token is in `docker logs` too; once you have it,
`docker logs` can be cleared by recreating the container.

Lost it, or think someone else has it? Delete `control_credentials.txt` and
restart the server: it makes a new token, and the old one stops working.

Every request sends it:

```
Authorization: Bearer chce_4f0c...e91a
```

### Endpoints

| Request | Answer |
| --- | --- |
| `GET /v1/status` | `sv_status` as JSON. |
| `GET /v1/players` | `sv_players` as JSON. |
| `POST /v1/command` with `{"command": "<command>"}` | `{"ok": true, "output": "..."}`: whether the command did what it was asked, and what it printed. Any command above. |
| `GET /v1/bans` | `sv_banlist` as JSON: `{"bans": [{"number": 1, "when": "2026-10-05 02:18:03", "id": "c382...", "players": "Player", "reason": "...", "until": 1791182883, "seconds_left": 1793}], "count": 1}`. `until` and `seconds_left` are `null` for a ban for ever. |
| `GET /v1/mapcycle` | `sv_mapcycle` as JSON: the playlist's `entries` (`number`, `map`, `game_type`), the `entry` playing, `state`, and what a command `chosen` now or set `next` (or `null`). |
| `GET /v1/maps` | `sv_maps` as JSON: `{"maps": ["beavercreek", ...], "game_types": ["slayer", ...]}`. |
| `GET /v1/playlists`, `/v1/gametypes`, `/v1/settings`, `/v1/moderators`, `/v1/link` | `sv_playlists`, `sv_gametypes`, `sv_settings`, `sv_mod_list`, `sv_link_status` as JSON. |
| `POST /v1/query` with `{"command": "sv_playlist big"}` | A read command's JSON (`sv_playlist`, `sv_gametype`, ...). |
| `POST /v1/file` with `{"kind": "playlist", "name": "big", "text": "..."}` | A playlist's (or, `"gametype"`, a game type's) file, saved whole, checked first: `{"ok", "output"}`. |
| `GET /v1/audit` | The audit file's last 200 lines (JSON objects). |
| `GET /v1/log?since=<n>` | The server's recent log lines after line `n` (`0` or none: the oldest kept), at most 500: `{"lines": [{"n": 1, "time": 1791168674, "text": "..."}], "next": 212, "missed": false}`. Ask again with `since=` the `next` you got for the lines after those. `missed` is true when lines after `since` were no longer kept (the last 1024 are). A `next` lower than your `since` means the server restarted. |

Errors are `{"error": "..."}` with an HTTP status: 400 (a request it does
not take), 401 (no token, or the wrong one), 403 (a web session's change
without its CSRF token, or from another site; or a role that does not
allow it), 404, 405, 413 (too large), 415
(not JSON), 429 (too many wrong tokens: wait as `Retry-After` says), 503
(the server did not answer in time, as while a map loads: try again).

```sh
TOKEN=chce_4f0c...e91a
curl -s -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8080/v1/status
curl -s -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8080/v1/players
curl -s -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"command": "sv_kick 2"}' http://127.0.0.1:8080/v1/command
curl -s -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"command": "sv_map bloodgulch ctf"}' http://127.0.0.1:8080/v1/command
curl -s -H "Authorization: Bearer $TOKEN" "http://127.0.0.1:8080/v1/log?since=0"
```

```json
{"name": "My Server", "version": "0.7.1d", "network_version": 24,
 "following": "Following OpenCE build-145 (table 2)", "state": "in_game",
 "map": "bloodgulch", "game_type": "slayer", "chosen": false, "next_map": null,
 "next_game_type": null, "playlist": "playlists/slayer.txt", "entry": 2, "entries": 5,
 "players": 3, "maximum_players": 12, "minimum_players": 1, "public": true,
 "idle_limit_minutes": 5, "uptime_seconds": 86400}
```

`state` is `starting`, `lobby`, `loading`, `in_game` or `postgame`. In
`/v1/players`, `team` is `red`, `blue` or `null` (no teams), and `score` and
`ping` are `null` outside a game; `id` is the machine's hardware id, or
`null` if it told none.

### A token for each admin

Each line of `control_credentials.txt` is a credential: a name, an id, and
the hash of its token. The server's console manages them (never the API, so
no token ever crosses the network to be made):

| Console command | What it does |
| --- | --- |
| `sv_admin_list` | The credentials: name, id, and how many web sessions each has. |
| `sv_admin_add <name>` | A new credential for another admin, its token printed once on the console. |
| `sv_admin_rotate <name>` | A new token for a credential (and a new id), printed once. The old token and every web session made with it stop working at once. |
| `sv_admin_remove <name>` | Takes a credential out: its token and web sessions stop working at once. The last one cannot be removed (add another first). |

`<name>` is the credential's name or id. Every audit line names the
credential (`control: web ops 1a2b3c4d: sv_kick 2`), so each admin's
actions can be told apart. At most 8 credentials. The file is written
again whole (a new file, then renamed over the old), so if it has a line
that is no credential the server refuses to change it until you fix it.
Deleting the file and restarting still gives a single new token, as
before.

### Reaching it safely

**SSH tunnel** (nothing to set up on the server but SSH):

```sh
ssh -N -L 8080:127.0.0.1:8080 you@your-server
# then, on your machine:
curl -s -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8080/v1/status
```

**Straight over the network**: `HALO_DEDICATED_CONTROL=0.0.0.0:8443` is
HTTPS ([moderation.md](moderation.md#remote-access-safely)): check the
certificate's fingerprint once, then log in with your account.

**Tailscale** (or another private network): listen on the server's
Tailscale address only, `HALO_DEDICATED_CONTROL=100.x.y.z:8443`, and reach
it from your other machines on the tailnet (HTTPS; or plain HTTP with
`HALO_DEDICATED_CONTROL_TLS=off`, which a private address allows, with a
warning). Use Tailscale's access rules to say which machines may reach the
port.

**A reverse proxy with TLS** (Caddy, nginx), for something on the internet
that must reach it: keep the server on `127.0.0.1:8080`, proxy to it, and
let the proxy do HTTPS. Behind a proxy every request comes from the proxy's
address, so wrong tokens from anyone count against that one address (see
below).

### Docker

Our images run the server as uid 1000, with the data folder at `/data`;
`control_credentials.txt` is written there, so the data folder must be
writable by that user.

- With `--network host` (as the [Docker guide](docker.md) runs it), the
  container shares the host's network: `HALO_DEDICATED_CONTROL=8080` is the
  host's `127.0.0.1:8080`, reachable from the host only. Nothing else to do.
- On Docker's own network (`-p`), the server must listen on the container's
  interface, `HALO_DEDICATED_CONTROL=0.0.0.0:8080` (HTTPS, then: open
  `https://127.0.0.1:8080/` and check the fingerprint; or plain HTTP with
  `HALO_DEDICATED_CONTROL_TLS=off`, as Docker's network is private), and the
  port is published to the host's loopback only: `-p 127.0.0.1:8080:8080`. A bare `-p 8080:8080` publishes it to the
  world, past most host firewalls.
- Several servers on one host each need a port of their own.

### What it does to keep strangers out

- Off unless turned on; on the loopback address unless told otherwise.
- No default password: a random 256-bit token, shown once, kept only as a
  salted Argon2id hash (19 MiB, 2 passes); tokens are compared in constant
  time. A token checked right is remembered for the run as a keyed hash, so
  only the first request of a run pays for Argon2id.
- Wrong tokens: five from one address in a minute refuse that address for
  five minutes (429), and at most 30 tokens a minute are checked in all, a
  burst of 10. A token already checked right this run still gets in, so
  someone guessing cannot lock you out.
- Every request is read whole, within limits, before anything is done
  with it: 8 KB of request line and headers, 4 KB of body, 32 headers, one
  request a connection, 10 seconds to send it, 16 connections at once.
  Anything that is not plainly a request the API takes is refused:
  `Transfer-Encoding`, `Expect`, repeated headers, bare line ends, control
  characters, escapes in paths, bodies on a GET, more than the
  `Content-Length`, a command that is not printable ASCII.
- No shell, and nothing but the commands above.
- An audit line in the log for every command the API runs, with the
  credential it came with (`control: api admin e76c2bf3: sv_kick 2`), and
  for every wrong token (with the address as the log shows addresses: a
  public one as a tag). Reads (status, players, log) are not logged: a web
  page asks for them every few seconds. Console commands are logged too
  (`control: console: ...`).
- The log lines it hands out hide any public address, even when the log
  writes them whole (`debug.log_addresses`).

## The web admin page

The control API's port also serves a web page for running the server from a
browser, on a computer or a phone:

- **Status**: name, state, map and game type, what is next, players,
  playlist entry, uptime, version.
- **Players**: number, name, team, score, ping; **Kick** and **Ban** (30
  minutes to 30 days, or for ever), each asked for again before it is done.
  If the player at that number changed in between, nothing is done.
- **Maps**: play any of the server's maps with any game type now, skip to
  the playlist's next entry, end the game; the playlist, and what is
  playing.
- **Bans**: every ban, how long is left; **Unban**.
- **Log**: the server's log as it is written (public addresses hidden).
- **Settings**: the server's name and the most players (until it
  restarts).

It is on whenever the control API is (`HALO_DEDICATED_CONTROL`), and off
otherwise. Open `http://127.0.0.1:8080/` (through one of the ways below)
and log in with your account (the first owner's is made from the setup
link the server prints: [moderation.md](moderation.md#the-first-owner)), or
with a control token. The page is the server's own: its few
files are built into the program, no fonts, scripts or anything else come
from elsewhere, and nothing on the server's disk is ever served.

**Never put it on a public port.** It is plain HTTP; reach it the ways the
API is reached:

- **SSH tunnel**: `ssh -N -L 8080:127.0.0.1:8080 you@your-server`, then open
  `http://127.0.0.1:8080/` on your machine.
- **Tailscale**: listen on the server's Tailscale address and open it from
  your tailnet.
- **A TLS reverse proxy**: keep the server on `127.0.0.1:8080` and let the
  proxy serve HTTPS. The proxy must pass the browser's `Host` (Caddy does;
  with nginx, `proxy_set_header Host $host;`) or set `X-Forwarded-Host`,
  and say `X-Forwarded-Proto: https` so the session cookie is marked
  `Secure`:

```
# Caddy
admin.example.org {
    reverse_proxy 127.0.0.1:8080
}
```

```
# nginx, inside a server block with TLS
location / {
    proxy_pass http://127.0.0.1:8080;
    proxy_set_header Host $host;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_set_header X-Forwarded-For $remote_addr;
}
```

If the page is reached straight over the network, with no reverse proxy's
headers, from somewhere other than the server itself, the server warns of
it once in its log.

### How the page keeps strangers out

- A login takes a control token and starts a session: a random 256-bit id
  in a cookie that is `HttpOnly` (no script can read it), `SameSite=Strict`
  (no other site's page can send it), `Path=/`, and `Secure` when the
  reverse proxy says the request came over HTTPS. The server keeps only a
  keyed hash of the id, compared in constant time.
- A session ends after 30 minutes without anything done (the page's own
  polling does not count), 12 hours after its login, on **Log out**, when
  the server restarts, or when its token is rotated or removed. At most 32
  at once.
- Every change (a command, logout) needs the session's CSRF token in an
  `X-CSRF-Token` header, compared in constant time, and is refused if it
  comes from another site's page (`Sec-Fetch-Site`) or another origin
  (`Origin` against `Host` or `X-Forwarded-Host`). A login is checked for
  the same origin too.
- Logins count against the same limits as the API's tokens: five wrong ones
  from an address in a minute refuse it for five minutes, and 30 tokens a
  minute are checked in all.
- Every response has a strict `Content-Security-Policy` (`default-src
  'self'`; no inline script or style, nothing from anywhere else, no
  framing), `X-Frame-Options: DENY`, `Referrer-Policy: no-referrer`,
  `X-Content-Type-Options: nosniff` and `Cache-Control: no-store`.
- The page shows what the server sends as text, never as HTML.
- An audit line for every login (`control: web login: admin e76c2bf3, from
  127.0.0.1`), logout, wrong token and refused change, and for every
  command a session runs (`control: web admin e76c2bf3: sv_kick 2`).
- Scripts keep using `Authorization: Bearer <token>`, which needs no
  cookie or CSRF token.

The page's endpoints, for the curious: `POST /v1/login` with `{"token":
"..."}` (sets the cookie; answers the credential's `name`, `id` and the
session's `csrf`), `GET /v1/session` (the same, for a page reloaded), and
`POST /v1/logout`. Everything else is the API's endpoints above, with the
cookie instead of a bearer token.

## Settings

| Variable | Default | What it does |
| --- | --- | --- |
| `HALO_DEDICATED_CONSOLE` | (a terminal: on) | `true`: read commands on standard input even when it is not a terminal; `false`: never. |
| `HALO_DEDICATED_COMMANDS` | (none) | A file of commands in the data folder, run once the server first hosts. |
| `HALO_DEDICATED_CONTROL` | (off) | The control API's and web admin page's address: a port (`8080`, on 127.0.0.1), or address and port. |
