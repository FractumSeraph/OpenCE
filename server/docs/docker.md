# Docker and systemd

The server needs nothing from the system it runs on, so a container is
optional: the [quick start](../README.md#quick-start) runs it as it is. A
container is the easy way to keep one running, restart it when it stops,
and keep several apart on one machine; `server/deploy/` has what the `[D]`
servers on stats.chupathingy.org run with.

## The image

[`server/deploy/Dockerfile`](../deploy/Dockerfile) puts the server on
Alpine Linux. One Dockerfile makes the image for every architecture: its
build folder holds each architecture's server as
`bin/<architecture>/chupathingyce-server`, with Docker's names for them:
`386` (the x86 server), `amd64` (x64) and `arm64` (arm64).

```sh
mkdir -p image/bin/amd64
cp server/deploy/Dockerfile image/
cp chupathingyce-server-linux-x64/chupathingyce-server image/bin/amd64/
docker build --build-arg TARGETARCH=amd64 -t chupathingyce-server image
```

(`TARGETARCH` names the folder; BuildKit sets it from the platform, but
Docker's legacy builder, where buildx isn't installed, sets none.)

Nothing runs while the image builds, so one machine can make all three at
once (with each server in its folder):

```sh
docker buildx build --platform linux/386,linux/amd64,linux/arm64 -t chupathingyce-server image
```

The image:

- runs the server as user 1000, not root (`--user` to choose another: the
  owner of your data folder);
- has the data folder at `/data` (`HALO_DATA_ROOT`), the saves in
  `/data/saves`;
- stops the server with SIGTERM, which withdraws its game from the lists.

```sh
docker run -d --name my-server --restart unless-stopped --network host \
  --user "$(id -u):$(id -g)" --cap-drop ALL --security-opt no-new-privileges \
  --memory 1g --pids-limit 256 \
  -e HALO_DEDICATED=playlists/free_for_all.txt -e HALO_DEDICATED_NAME="My Server" \
  -v /path/to/data:/data chupathingyce-server
```

Use the host's network (`--network host`): internet play's hole punching
does not get through a bridge's NAT to players behind their own. The server
needs no open ports.

To type commands into it, run it with `-it` and `docker attach` to it; for
its control API (`HALO_DEDICATED_CONTROL`), see
[admin.md](admin.md#docker): with the host's network it stays on the
host's 127.0.0.1.

## A service on a Linux host

`server/deploy/deploy.sh` installs or updates the server on a Linux host
with Docker, over SSH, as the `halo-dedicated` systemd service:

1. Copy the maps to the host's `/opt/halo-dedicated/data/maps`: `ui.map`
   and the multiplayer maps; Custom Edition maps with their `bitmaps.map`,
   `sounds.map` and `loc.map` to `/opt/halo-dedicated/data/maps_ce`,
   HaloMD maps to `/opt/halo-dedicated/data/maps_md`, Halo PC retail maps to
   `/opt/halo-dedicated/data/maps_pc` (the older `maps/ce/` and `md_maps`
   are still read). An Xbox map pack goes in its own folder there: Halo
   CE+'s maps (its `maps` folder, `maplist.txt` included) in
   `/opt/halo-dedicated/data/maps_cex`, played in playlists as
   `<name>@cex` ([Playlists](playlists.md)). The data folder is mounted
   whole, so a pack folder needs no mount of its own.
2. Run `server/deploy/deploy.sh user@host path/to/chupathingyce-server`,
   with the server for the host's architecture. It copies the server and
   the playlists, builds the image for that architecture, and installs and
   starts the service. With `--no-restart` it installs them and leaves the
   running server alone, to restart when nobody is playing.

A second image, for a server of another architecture on the same host,
takes `--image`: `deploy.sh --image halo-dedicated-ce user@host
chupathingyce-server-linux-x64/chupathingyce-server` builds a 64-bit
`halo-dedicated-ce` image (for Halo PC maps) beside a 32-bit
`halo-dedicated`, from a build folder of its own
(`/opt/halo-dedicated/image-ce`), and leaves every service as it is: the
services that run it are restarted by hand.

The settings are in `/opt/halo-dedicated/dedicated.env` on the host (from
`deploy/dedicated.env` the first time); after a change,
`sudo systemctl restart halo-dedicated`. The server's log is
`/opt/halo-dedicated/data/logs/debug.txt` (until 0.8.0,
`/opt/halo-dedicated/data/debug.txt`), the service's
`journalctl -u halo-dedicated`.

The service runs the server as the data folder's owner (uid 1000 on the
host; an account of its own is better still), without capabilities or a way
to gain privileges, its memory and processes capped: it reads packets from
anyone who joins.

### More servers on the same host

Each further server is the `halo-dedicated@<name>` service: the same image,
its settings in `deploy/instances/<name>.env` (`team.env`: Team Slayer on
every map), its own data folder `/opt/halo-dedicated/instances/<name>`
(saves, `logs/debug.txt`), and the first server's maps and playlists, read-only.
All play on the host's network, each with system link on a loopback address
of its own (`HALO_NET_ADDRESS`: 127.0.0.2 the first, 127.0.0.3 the team
server, 127.0.0.4 `max.env`'s 32-player Slayer, 127.0.0.5 `bloodgulch.env`'s
128-player Blood Gulch), since two cannot share its port on one address.
After `deploy.sh`, run `server/deploy/deploy-instance.sh user@host team`.
Its log is `journalctl -u halo-dedicated@team`.

## Without Docker

The server is one program, so systemd can run it directly too. A unit such
as this one, with a user of its own:

```ini
[Unit]
Description=ChupathingyCE Dedicated Server
After=network-online.target
Wants=network-online.target

[Service]
User=halo
WorkingDirectory=/srv/halo
Environment=HALO_DEDICATED=playlists/free_for_all.txt
Environment="HALO_DEDICATED_NAME=My Server"
Environment=HALO_SAVE_ROOT=/srv/halo/saves
ExecStart=/srv/halo/chupathingyce-server
Restart=always
RestartSec=5
NoNewPrivileges=yes
CapabilityBoundingSet=
ProtectSystem=strict
ReadWritePaths=/srv/halo
PrivateTmp=yes
MemoryMax=1G

[Install]
WantedBy=multi-user.target
```

### Updates on their own, and backups

FractumSeraph's two servers (FractumFFA and FractumKing, one program in
`/opt/halo-dedicated/bin`) keep themselves up to date and backed up with
these, in `server/deploy`, which another host can adapt (names and paths are
at the top of each):

- `halo-gameservers-update`, run every 30 minutes by
  `halo-gameservers-update.service` and `.timer`: when the fork's latest
  release is newer than the program, and the public game list says both
  servers have had no players for a minute, it checks the zip against
  GitHub's SHA-256 digest, runs the new program once (`--version`), keeps
  the old one as `chupathingyce-server.previous` and restarts both servers;
  if either is not running a minute later, it puts the previous program
  back. Never in the middle of a game. Its log is
  `journalctl -u halo-gameservers-update`.
- `halo-backup`, run nightly by `halo-backup.service` and `.timer`: a
  `.tar.gz` of the servers' settings (env files, admin settings, playlists,
  control panel accounts and audit log, Delta Control links, lasting invite
  keys, game state), the shared ban list, the web server's, MQTT broker's
  and tracker's settings, the units and scripts, and the newest Umami
  database dump (no maps, programs or logs), sent over SSH to a seedbox
  whose `authorized_keys` lets that key run `halo-backup-receive` and
  nothing else (`restrict,command="$HOME/bin/halo-backup-receive"`). That
  keeps the newest 14, readable by its account alone; they are not
  encrypted. Restore one with `tar -xzf halovps-<date>.tar.gz -C /`.
- `halo-alerts`, run by `halo-alerts.service`: posts to a Discord channel
  when the fair play checks make a player a suspect (their name, why, the
  game's recording: no address or hardware id), when a game server
  crashes (systemd starts it again within seconds), and when a game server,
  the web services, Caddy, the tracker or MQTT is not running, the site or
  the lobby does not answer, a server is off the game list for ten
  minutes, the disk has under 2 GB left, or a server update, backup or ban
  list run failed; and again when it is back. The webhook's URL goes in
  `/etc/halo-alerts/webhook`, readable by root alone (anyone with it can
  post to the channel); `halo-alerts --test` posts a line to try it.
- `halo-recordings-offload`, run every 10 minutes by
  `halo-recordings-offload.service` and `.timer`: each recording whose game
  is over (not written to for 5 minutes) goes to the seedbox over SSH
  (`/root/.ssh/halo_recordings`, whose `authorized_keys` line there runs
  `halo-recordings-receive` and nothing else: it keeps them in
  `~/halo-recordings/<server>/`, the oldest going past 200 GB), is checked
  there by its SHA-256, listed in the folder's `archived.txt` and deleted
  here. The control panel still lists and saves them
  ([admin.md](admin.md#recordings)): nginx on the seedbox serves
  `/halo-archive/` only with the token in `/etc/caddy/halo-archive.env`
  (given to Caddy by `caddy.service.d/halo-archive.conf`).

## The game list's probe

The server program is also the game list's probe when `HALO_PROBE` holds an
invite (the digits after `halo://join/`). It reads the game the invite
leads to, the way a joining player's copy sees it advertised, prints it as
one line and exits, without joining the game or taking a place in it.
stats.chupathingy.org uses it to list games hosted by copies without the game
list: a signed-in player gives their invite, and the site checks it before
listing it and while it is listed.

```
HALO_PROBE=068f5721cffe... chupathingyce-server
probe: {"ok": true, "name": "Milenko Slayer", "map": "chillout", "engine": "slayer", "players": 0, "maximum_players": 12, "open": true, "teams": false, "network_version": 24, "compatible": true, "machines": 1, "score_limit": 25, "in_progress": false}
```

It needs only `maps/ui.map` in the data folder (and about 33 MB for its
saves), and takes about 3 seconds. A host that does not answer in 20
seconds gives `{"ok": false, "error": "no answer from the host"}` (exit
status 1).

`deploy/deploy-probe.sh user@host path/to/chupathingyce-server "<site's SSH key>"`
puts it on a dedicated server's host beside the server, which it leaves as
it is: a `halo-probe` image (the same Dockerfile), `probe.sh`, which runs
one probe in a container of its own that goes when it is done, and a
`probe` user whose key may only ask for a probe (`probe-ssh.sh`).
Run again without the key, it updates the image and the scripts and keeps
the `probe` user's key.
