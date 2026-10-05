# Hosting Halo on a VPS

These steps put this folder on a Linux VPS behind your own domain with a
real HTTPS certificate. With real HTTPS:

- nobody sees a certificate warning;
- phones can **install the app** (Add to Home Screen / Install app);
- family can play from anywhere, not just your home network.

The Linux kit, `halo-server-linux-x64.zip`, already contains everything for
Linux x64 (Node.js, the lobby runtime, the native gateway). Nothing needs
building on the VPS. You add the maps.

**Example values used below:** domain `halo.example.com`, VPS address
`203.0.113.10`, install folder `/opt/halo`. Replace them with yours.

---

## 1. What you need

| Item | Notes |
| --- | --- |
| A Linux **x64** VPS | Ubuntu 24.04 or Debian 12. 1 vCPU and 1 GB RAM are enough: the game runs in each player's browser, and the server only serves files and runs the lobby. About 4 GB of disk. |
| A domain name | Any registrar. You can also use a subdomain of a domain you already own. |
| SSH access | As a user with `sudo`. |

Bandwidth: each new device downloads about 25 MB to start, then streams the
maps it plays (up to about 1.8 GB for everything, usually much less). Gameplay
itself goes player-to-host, not through the VPS.

## 2. Point your domain at the VPS

At your DNS provider, create an **A record**:

| Name | Type | Value |
| --- | --- | --- |
| `halo` (for `halo.example.com`) | A | `203.0.113.10` |

Wait until `ping halo.example.com` answers from that address (minutes, at
most a few hours).

## 3. Put the kit and the maps on the VPS

On the VPS, download the newest Linux kit and unpack it into `/opt/halo`
(replace `FractumSeraph/OpenCE` if you build it from another fork):

```bash
sudo apt install -y unzip curl
sudo mkdir -p /opt/halo && sudo chown $USER /opt/halo
curl -fL -o /tmp/halo.zip https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-server-linux-x64.zip
unzip -q /tmp/halo.zip -d /tmp/halo && cp -r /tmp/halo/halo-server/. /opt/halo/ && rm -r /tmp/halo /tmp/halo.zip
```

Then copy your maps (the Xbox `.map` files, about 1.8 GB) into
`/opt/halo/public/assets/maps/`, for example from Windows PowerShell (OpenSSH
is built into Windows):

```powershell
scp C:\path\to\maps\*.map you@203.0.113.10:/opt/halo/public/assets/maps/
```

Moving an existing Halo folder from another computer works too: copy all of
it except `data\`, which holds that computer's lobby state and certificate.
The VPS makes its own.

## 4. Prepare it on the VPS

```bash
sudo useradd --system --home /opt/halo --shell /usr/sbin/nologin halo
sudo chown -R halo:halo /opt/halo
sudo chmod +x /opt/halo/start-halo.sh
```

Create `/opt/halo/config.json` (the server writes one with its defaults on
first start; this one listens only locally, since Caddy, below, faces the
internet and handles HTTPS):

```json
{
  "http": { "enabled": true, "port": 8765, "bind": "127.0.0.1" },
  "https": { "enabled": false, "port": 8443, "bind": "0.0.0.0", "certFile": "", "keyFile": "" },
  "trustProxyHeaders": true,
  "lobby": { "enabled": true, "externalUrl": "", "defaultRoomCapacity": 128, "maxRoomCapacity": 128, "roomHours": 6 },
  "iceServers": [],
  "cloudflareTurn": { "keyId": "", "keySecret": "" },
  "nativeGateway": { "enabled": true, "publicIp": "auto", "udpPortStart": 40000, "udpPortEnd": 40127, "maxSessions": 64, "allowPrivateIp": false }
}
```

Try it once by hand:

```bash
sudo -u halo /opt/halo/start-halo.sh
```

You should see `lobby service ready` and a `native gateway on UDP …` line
showing the VPS's public IP. Press Ctrl+C to stop it.

## 5. Run it as a service (starts at boot, restarts on failure)

Create `/etc/systemd/system/halo.service`:

```ini
[Unit]
Description=Halo web server
After=network-online.target
Wants=network-online.target

[Service]
User=halo
Group=halo
WorkingDirectory=/opt/halo
ExecStart=/opt/halo/start-halo.sh
Restart=on-failure
RestartSec=5
# the server stops its lobby runtime and gateway on SIGTERM
KillMode=mixed
TimeoutStopSec=15

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now halo
systemctl status halo          # should be "active (running)"
journalctl -u halo -f          # live log (Ctrl+C to leave)
```

## 6. HTTPS with Caddy (automatic Let's Encrypt certificate)

```bash
sudo apt install -y debian-keyring debian-archive-keyring apt-transport-https curl
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo apt update && sudo apt install -y caddy
```

Replace `/etc/caddy/Caddyfile` with:

```
halo.example.com {
	reverse_proxy 127.0.0.1:8765
}
```

```bash
sudo systemctl reload caddy
```

Caddy fetches and renews the certificate by itself. WebSockets (the lobby)
and the headers the server needs pass through unchanged.

**To serve under a path instead** (for example `https://example.com/halo/`),
use this in place of the block above. Nothing in the Halo folder changes:

```
example.com {
	handle_path /halo/* {
		reverse_proxy 127.0.0.1:8765
	}
}
```

## 7. Firewall

```bash
sudo ufw allow OpenSSH
sudo ufw allow 80/tcp            # Let's Encrypt and the HTTP→HTTPS redirect
sudo ufw allow 443/tcp           # the site
sudo ufw allow 40000:40127/udp   # native gateway (halo://join links from Windows/Linux builds)
sudo ufw enable
```

If your provider also has a firewall in its control panel (AWS security
groups, Hetzner/Oracle/Google cloud firewalls), open the same ports there.

## 8. Check it

```bash
curl -s -H "Origin: https://halo.example.com" https://halo.example.com/v1/health
# {"ok":true,"v":1}
```

Then open `https://halo.example.com/` in Chrome. The game should start
without any certificate warning. On a phone, the **Install app** button (or
the ☰ menu in touch mode) adds it to the home screen.

---

## Optional: a TURN relay for players on strict networks

Some mobile carriers and workplace networks block direct peer-to-peer
connections. A TURN relay on the same VPS fixes that. It only carries game
traffic for players who need it.

```bash
sudo apt install -y coturn
```

`/etc/turnserver.conf`:

```
listening-port=3478
fingerprint
lt-cred-mech
realm=halo.example.com
user=halo:CHANGE-THIS-TO-A-LONG-RANDOM-PASSWORD
min-port=49160
max-port=49260
no-cli
no-tlsv1
no-tlsv1_1
```

```bash
sudo sed -i 's/^#TURNSERVER_ENABLED=1/TURNSERVER_ENABLED=1/' /etc/default/coturn 2>/dev/null
sudo systemctl enable --now coturn
sudo ufw allow 3478/tcp && sudo ufw allow 3478/udp && sudo ufw allow 49160:49260/udp
```

In `/opt/halo/config.json`, set `iceServers`, then `sudo systemctl restart halo`:

```json
"iceServers": [
  { "urls": ["stun:halo.example.com:3478"] },
  { "urls": ["turn:halo.example.com:3478?transport=udp", "turn:halo.example.com:3478?transport=tcp"],
    "username": "halo", "credential": "CHANGE-THIS-TO-A-LONG-RANDOM-PASSWORD" }
]
```

(Players' browsers receive these credentials. That is normal for a private
relay, but use a password you don't use anywhere else.)

## Optional: keep the site private

A public VPS serves the game, including your maps, to anyone who finds the
address. To ask for a password, add basic authentication to everything
except the lobby API. Invite links already protect the lobby, and the game
calls it without credentials.

```bash
caddy hash-password    # type a password; copy the hash it prints
```

```
halo.example.com {
	@protected not path */v1/*
	basic_auth @protected {
		family PASTE-THE-HASH-HERE
	}
	reverse_proxy 127.0.0.1:8765
}
```

Each device asks for the password once. If an installed iPhone app misbehaves
with the password prompt, use the page in Safari instead, or leave this off.

---

## Updating the game later

Download the newest kit and copy it over the old one. Your `config.json`,
`data/` and maps are not in the kit, so they stay as they are:

```bash
curl -fL -o /tmp/halo.zip https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-server-linux-x64.zip
unzip -q /tmp/halo.zip -d /tmp/halo && sudo cp -r /tmp/halo/halo-server/. /opt/halo/ && rm -r /tmp/halo /tmp/halo.zip
sudo chown -R halo:halo /opt/halo
sudo systemctl restart halo
```

Players get the new game on their next page load. (A change to only the game
files needs no restart; the kit may also update the server, so restart.)

To replace maps with your modified ones, copy them into
`/opt/halo/public/assets/maps/` the same way.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| Caddy can't get a certificate | Check that the A record points at the VPS and that ports 80 and 443 are open (`sudo journalctl -u caddy`). |
| "The private-room service is unreachable" | `systemctl status halo`; check that `curl http://127.0.0.1:8765/v1/health -H "Origin: x"` works on the VPS. |
| Native `halo://join` links don't work | The log must show `native gateway on UDP … (public IP …)`; open UDP 40000-40127 in ufw **and** the provider's firewall. |
| A friend connects to the lobby but never into the game | Their network blocks peer-to-peer: set up the TURN relay above. |
| Seeing every request helps | Add `Environment=HALO_LOG_REQUESTS=1` under `[Service]`, then `sudo systemctl daemon-reload && sudo systemctl restart halo`. |
