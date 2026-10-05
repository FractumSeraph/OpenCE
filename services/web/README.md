# Halo browser hosting

This package publishes the browser build with Cloudflare Workers Static Assets
and records Analytics Engine telemetry. It does not serve Halo map data.
Players choose an XISO made from their own Xbox disc; the browser validates the
image and stores only the required maps in origin-private local storage.

The current deployment is
[halo-web.otherness-bugs.workers.dev](https://halo-web.otherness-bugs.workers.dev/).
Do not add proprietary Halo game data to Static Assets, R2, or this repository.

Build and validate from the repository root:

```sh
ninja web
cd services/web
npm ci
npm run check
```

Deploy with:

```sh
npm run deploy
```

`tools/web_stage_cloudflare.py` creates the ignored
`build/cloudflare-web/` directory and fails if a required runtime asset is
missing or larger than Cloudflare's per-file limit. It deliberately never
copies files from `assets/maps`. The generated `_headers` file enables the
cross-origin isolation required by WebAssembly threads.
