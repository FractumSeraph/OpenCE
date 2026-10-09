# The page on a web host, the services on a VPS

[HOSTING-VPS.md](HOSTING-VPS.md) runs everything on one VPS. The game's files
are most of the traffic (about 25 MB for each new device, then the maps it
plays: gigabytes for all of them), and the services are small. So they can
be split:

- **a web host** (any host that serves files over HTTPS and lets you set
  response headers: a seedbox's web space, shared hosting) serves the page,
  the game and the maps, from a copy of `public/`;
- **a VPS** (small: 1 vCPU, 1 GB) runs this server for the services alone:
  the lobby, the native gateway (UDP 40000–40127), the server browser's list
  and the Delta relay.

Example names below: the page at `https://halo.example.com/`, the services
at `https://services.example.com/`.

## 1. The VPS

Follow [HOSTING-VPS.md](HOSTING-VPS.md) for `services.example.com` (no maps
needed there), with two changes:

- in `config.json`, let the page's site use this server:

  ```json
  "cors": { "allowedOrigins": ["https://halo.example.com"] }
  ```

  (The lobby and the gateway take any page's requests already; this is for
  the server browser's list and the Delta relay.)
- Caddy can send everything that is not a service to the page:

  ```
  services.example.com {
  	@services path */v1/*
  	handle @services {
  		reverse_proxy 127.0.0.1:8765
  	}
  	handle {
  		redir https://halo.example.com{uri}
  	}
  }
  ```

## 2. The page's files

The server makes two things on the fly that a web host cannot: the page
with the lobby's address (and the analytics) put in, and the Custom Edition
maps' list (`assets/custom_maps/index.json`, with each map's header and
BLAKE2b hash). On a computer with this folder and the maps:

```bash
node server/static-site.mjs static --signaling-url https://services.example.com/
```

writes them into `static/` (`index.html`, `halo.html`,
`assets/custom_maps/index.json`; hashing new maps takes a while once).
Upload `public/` to the web host, then `static/` over it. Run it again, and
upload again, whenever the game or the maps change.

## 3. The web host's headers

The game needs these on every response of the page's site (WebAssembly
threads need cross-origin isolation):

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
Cache-Control: no-cache
```

and `.wasm` served as `application/wasm`, and byte ranges (every common
server does them). With nginx, in the site's `server` block:

```nginx
add_header Cross-Origin-Opener-Policy same-origin always;
add_header Cross-Origin-Embedder-Policy require-corp always;
add_header Cache-Control no-cache;
location ~* \.wasm$ { types { } default_type application/wasm; }
```

With Apache, in `.htaccess` (mod_headers):

```apache
Header always set Cross-Origin-Opener-Policy "same-origin"
Header always set Cross-Origin-Embedder-Policy "require-corp"
Header set Cache-Control "no-cache"
AddType application/wasm .wasm
```

Where headers cannot be set at all, `coi-serviceworker.js` (already in the
page) gives the isolation after one reload.

## 4. Check it

```bash
curl -sI https://halo.example.com/ | grep -i cross-origin
curl -s -H "Origin: https://halo.example.com" https://services.example.com/v1/health
```

then open `https://halo.example.com/` and host a game from Play online.
