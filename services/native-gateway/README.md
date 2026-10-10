# Halo native gateway

This small, isolated service lets the browser join the current Windows/Linux
`halo://join/<capability>` protocol. It does not host a Halo game and it is not
a general-purpose UDP proxy.

The Cloudflare signaling Worker remains the public control plane. It validates
the browser origin, rate limit, ban state, and Turnstile token before sending a
signed session request here. The gateway then:

1. consumes a one-time, 60-second WebSocket ticket;
2. joins the native `hceu/3` MQTT signaling topics with a fresh X25519 key;
3. validates that the host public key matches the 128-bit hash in the invite;
4. completes the native proof exchange and authenticated UDP hole punch; and
5. translates only Halo's browser socket frames to the native encrypted UDP/KCP
   tunnel.

Browser frames cannot select an IP address. Before authentication, the gateway
sends only a five-byte ping to at most four public-unicast candidates signed by
the invited host, five times a second for at most 30 seconds. Private,
loopback, link-local, multicast, benchmark, and documentation ranges are
rejected.

The invite, and so the host's key, its candidates and the tunnel's keys, are
the browser's to choose, so a sealed packet that claims to come from a
candidate proves nothing: its source address can be forged. Each candidate's
ping therefore carries four random bytes in place of the clock, and the
gateway pins as its endpoint only the candidate whose pong (which the native
host sends back to the ping's source with those four bytes unchanged,
`tunnel_received` in `port/linux/src/p2p.c`) echoes the bytes sent to that
very address. Until then it answers nothing and drops every other packet.
Native hosts need no change for this.

A session ends, and gives back its slot, its UDP port and its MQTT broker
connections however it ends (an error, a WebSocket upgrade that never
completes, a panic). A browser that stops reading its WebSocket for 10 seconds
ends it, as does a stream with more than 512 KCP segments (of at most 1 KiB)
waiting for the host.

## Configuration

Required:

- `CONTROL_SECRET` or `CONTROL_SECRET_FILE`: at least 32 characters; identical
  to the Worker's `NATIVE_GATEWAY_SECRET` Wrangler secret. Production
  deployments should mount a root-managed secret file instead of exposing the
  value in container metadata.
- `PUBLIC_WEBSOCKET_URL`: for example
  `wss://native.mitchellhynes.com/v1/connect`.
- `PUBLIC_UDP_IP`: the instance's public IPv4 address.
- `ALLOWED_ORIGINS`: comma-separated exact origins. No wildcard is accepted.

Optional:

- `BIND_ADDR` (default `127.0.0.1:8080`)
- `UDP_PORT_START` / `UDP_PORT_END` (default `40000` / `40127`)
- `MAX_SESSIONS` (default `64`)
- `MAX_SESSIONS_PER_ACTOR` (default `2`, at most `512`): one address's sessions at once (a household, a school: everyone behind one router is one address)
- `GLOBAL_DAILY_BYTE_CAP` (default `10000000000`)

Each session is additionally limited to 2 MiB/s, 1 GiB total, four reliable
streams, six hours, and a 20-second authenticated-peer timeout. The daily byte
cap fails closed until the next UTC day. Run behind a TLS terminator with TCP
8080 private, expose only the configured UDP range, and run the container as an
unprivileged user (the supplied image uses UID 10001).

## Local checks

```sh
cargo fmt --check
cargo test --locked
cargo clippy --locked --all-targets -- -D warnings
```
