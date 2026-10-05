# Operations and telemetry

The web client sends one aggregate performance sample per minute to the
`halo_web_performance` Analytics Engine dataset. It contains FPS, callback CPU
time, browser/Wasm memory, viewport, browser family, platform, device class,
country and Cloudflare colo. The index is a random per-page UUID; raw IP
addresses and persistent browser identifiers are not stored.

TURN credentials are tagged with an opaque, network-scoped HMAC actor ID.
Cloudflare supplies the address at its edge, and the client-controlled game
identifier is not part of this abuse identity, so changing a name or identifier
cannot reset bandwidth accounting or evade a ban. Cloudflare's TURN
GraphQL analytics can therefore rank bandwidth by actor without exposing an IP
address. The signaling Worker stores short-lived credential usernames in KV so
an actor ban both blocks future room/session creation and best-effort revokes
credentials that have not expired yet.

## Reports

Use an API token with Account Analytics Read permission:

```sh
export CLOUDFLARE_API_TOKEN=...
node tools/halo_telemetry.mjs fps 24
node tools/halo_telemetry.mjs campaign 24
node tools/halo_telemetry.mjs runtime 24
node tools/halo_telemetry.mjs crashes 24
node tools/halo_telemetry.mjs turn 7
```

The TURN report's `actorId` is the value accepted by the ban commands. The
admin token is kept outside the repository and installed as a Worker secret:

```sh
export HALO_ADMIN_TOKEN=...
node tools/halo_telemetry.mjs ban ACTOR_ID "reason"
node tools/halo_telemetry.mjs bans
node tools/halo_telemetry.mjs unban ACTOR_ID
node tools/halo_telemetry.mjs turn-live 24
node tools/halo_telemetry.mjs turn-check
node tools/halo_telemetry.mjs turn-disable
node tools/halo_telemetry.mjs turn-enable
```

The signaling Worker checks TURN analytics every five minutes. An actor that
reaches 1 GB of egress in a rolling hour is banned for 24 hours and its active
credentials are revoked. At 10 GB of total UTC-day egress the Worker revokes
all tracked credentials and stops issuing new relay credentials until UTC
midnight; free STUN and direct peer-to-peer play remain available. Because
Cloudflare TURN analytics is adaptively sampled and arrives after traffic has
already flowed, these are circuit breakers rather than byte-exact prepaid
limits; allow for a small amount of overshoot.

Because this intentionally avoids accounts and persistent tracking, players
behind the same public NAT share one bandwidth identity and cap.

Room and initial session creation also require a server-validated Turnstile
token with the expected action and approved hostname. Supplied tokens are
always sent to Siteverify, so forged, expired, cross-action and replayed tokens
are rejected. A short actor- and action-bound proof permits only automatic
session reconnection without interrupting a game with another challenge.

The browser summary schema is:

- blobs: build, browser, platform, device class, country, colo, viewport
- doubles: average FPS, minimum FPS, p95 FPS, average CPU milliseconds, p95 CPU
  milliseconds, memory bytes, sample count, duration milliseconds, DPR
- index: random page-session UUID

The `halo_web_runtime` dataset records coarse lifecycle milestones rather than
raw logs. It shows whether a session reached the renderer, initialized the
runtime, presented frames, connected a controller, entered an online state or
started/completed a map load. Slow and stalled startup/map events include the
normalized GPU class, role, direct/relay class, Halo client state and elapsed
time. The first fatal error in each page session also includes a stable
fingerprint of a locally sanitized error, a coarse category, the top function
name, page visibility, cross-origin isolation, SharedArrayBuffer, WebGL and
Gamepad capability classes, plus coarse CPU-thread and device-memory hints.
Repeated errors after the first fatal failure are suppressed. It deliberately
excludes player names, room codes, raw error text, stack traces, URLs, full GPU
strings and network addresses. Each deployed asset set receives a content-hash
build ID so regressions can be attributed to a specific rollout. Query normal
lifecycle events with `node tools/halo_telemetry.mjs runtime 24` and grouped
crash fingerprints with `node tools/halo_telemetry.mjs crashes 24`.

The `halo_web_campaign_loads` dataset receives one summary when a streamed
campaign load finishes or the page exits. It does not contain player names or
network addresses:

- blobs: build, map, outcome, browser, platform, device class, connection
  class, country, colo, viewport, random load UUID
- doubles: total, download/cache-copy and preparation milliseconds, maximum
  progress, sample count, longest observed stall, memory bytes and DPR
- index: the same random page-session UUID used by FPS telemetry

`node tools/halo_telemetry.mjs campaign 24` groups those summaries by map,
outcome, browser, device and connection class so slow campaign starts can be
separated from abandoned pages.

TURN usage itself is authoritative in Cloudflare's
`callsTurnUsageAdaptiveGroups`; `halo_turn_events` records issuance, fallback,
block, ban and unban control-plane events only.
