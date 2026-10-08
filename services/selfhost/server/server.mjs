// Self-hosted Halo web server.
//
// One process serves everything the browser build needs, from this folder,
// under any host name, port or sub-path:
//
//   <anything>/                      static game files from ../public
//   <anything>/v1/...                lobby (signaling) service, the upstream
//                                    Cloudflare Worker running in Miniflare
//   <anything>/native-gateway/v1/... native-game gateway (halo://join links)
//   <anything>/v1/public-games       the in-game server browser's public games
//                                    (public-games.mjs)
//   <anything>/v1/delta/...          ChupathingyCE's game list, asked for the
//                                    page (delta-list.mjs)
//
// Settings live in ../config.json (created with defaults on first start).
// Runtime state (lobby storage, generated secrets, TLS certificate) lives in
// ../data and can be deleted at any time to start fresh.

import crypto from "node:crypto";
import dgram from "node:dgram";
import fs from "node:fs";
import http from "node:http";
import https from "node:https";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

import { Log, LogLevel, Miniflare } from "miniflare";
import selfsigned from "selfsigned";

import { PublicGames } from "./public-games.mjs";
import { DeltaList } from "./delta-list.mjs";

const SERVER_DIR = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(SERVER_DIR, "..");
const PUBLIC_DIR = path.join(ROOT, "public");
const DATA_DIR = path.join(ROOT, "data");
// (made to stop the server: main)
const STOP_FILE = path.join(DATA_DIR, "stop-request");
const CONFIG_FILE = path.join(ROOT, "config.json");

const GATEWAY_PLACEHOLDER_URL = "wss://halo-native-gateway.invalid/v1/connect";

const DEFAULT_CONFIG = {
  http: { enabled: true, port: 8765, bind: "::" },
  https: { enabled: true, port: 8443, bind: "::", certFile: "", keyFile: "" },
  trustProxyHeaders: true,
  lobby: {
    enabled: true,
    externalUrl: "",
    defaultRoomCapacity: 128,
    maxRoomCapacity: 128,
    roomHours: 6,
  },
  iceServers: [],
  cloudflareTurn: { keyId: "", keySecret: "" },
  nativeGateway: {
    enabled: true,
    publicIp: "auto",
    udpPortStart: 40000,
    udpPortEnd: 40127,
    maxSessions: 64,
    allowPrivateIp: false,
  },
  // the in-game server browser (Join Game > Server Browser): native games
  // listed on internet play's brokers (the game's port/assets/network/brokers.txt)
  publicGames: {
    enabled: true,
    brokers: ["opence.milenko.org:1883", "broker.emqx.io:1883", "broker.hivemq.com:1883", "test.mosquitto.org:1883"],
  },
  // Delta, ChupathingyCE's game list (delta-list.mjs): the page's requests to
  // it go through this server, which has no CORS to work around. Empty url
  // or enabled false: none (the game then plays with its built-in numbers).
  delta: { enabled: true, url: "https://halo.milenko.org" },
  // optional page-view analytics: an Umami instance's tracker script (e.g.
  // "https://umami.example.com/script.js") and the site's website ID in it.
  // Empty = none. (It must send Access-Control-Allow-Origin, as Umami does:
  // the page is cross-origin isolated.)
  analytics: { umamiScriptUrl: "", umamiWebsiteId: "" },
};

// ---------------------------------------------------------------- helpers

function log(scope, message) {
  const time = new Date().toLocaleTimeString();
  console.log(`[${time}] ${scope.padEnd(7)} ${message}`);
}

function merge(defaults, value) {
  if (Array.isArray(defaults)) return Array.isArray(value) ? value : defaults;
  if (defaults && typeof defaults === "object") {
    const result = {};
    for (const key of Object.keys(defaults)) {
      result[key] = merge(defaults[key], value && typeof value === "object" ? value[key] : undefined);
    }
    return result;
  }
  return value === undefined || value === null ? defaults : value;
}

function loadConfig() {
  if (!fs.existsSync(CONFIG_FILE)) {
    fs.writeFileSync(CONFIG_FILE, JSON.stringify(DEFAULT_CONFIG, null, 2) + "\n");
    log("config", `wrote default settings to ${CONFIG_FILE}`);
    return structuredClone(DEFAULT_CONFIG);
  }
  try {
    return merge(DEFAULT_CONFIG, JSON.parse(fs.readFileSync(CONFIG_FILE, "utf8")));
  } catch (error) {
    throw new Error(`config.json is not valid JSON: ${error.message}`);
  }
}

function loadSecrets() {
  const file = path.join(DATA_DIR, "secrets.json");
  let secrets = {};
  try {
    secrets = JSON.parse(fs.readFileSync(file, "utf8"));
  } catch {
    // Created below.
  }
  let changed = false;
  for (const name of ["roomIdSecret", "abuseIdSecret", "adminToken", "gatewaySecret"]) {
    if (typeof secrets[name] !== "string" || secrets[name].length < 32) {
      secrets[name] = crypto.randomBytes(36).toString("base64url");
      changed = true;
    }
  }
  if (changed) fs.writeFileSync(file, JSON.stringify(secrets, null, 2) + "\n", { mode: 0o600 });
  return secrets;
}

function freePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.unref();
    server.on("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const { port } = server.address();
      server.close(() => resolve(port));
    });
  });
}

function localAddresses() {
  const addresses = [];
  for (const entries of Object.values(os.networkInterfaces())) {
    for (const entry of entries || []) {
      if (entry.family === "IPv4" && !entry.internal) addresses.push(entry.address);
    }
  }
  return addresses;
}

function isPrivateIPv4(ip) {
  const [a, b] = ip.split(".").map(Number);
  return a === 10 || a === 127 || (a === 172 && b >= 16 && b <= 31) ||
    (a === 192 && b === 168) || (a === 169 && b === 254) || (a === 100 && b >= 64 && b <= 127);
}

// Public IPv4 discovery with a single STUN binding request (RFC 5389).
function stunPublicIp(host, port, timeoutMs = 3000) {
  return new Promise((resolve) => {
    const socket = dgram.createSocket("udp4");
    const transaction = crypto.randomBytes(12);
    const request = Buffer.alloc(20);
    request.writeUInt16BE(0x0001, 0);
    request.writeUInt16BE(0, 2);
    request.writeUInt32BE(0x2112a442, 4);
    transaction.copy(request, 8);
    const done = (value) => {
      clearTimeout(timer);
      socket.close();
      resolve(value);
    };
    const timer = setTimeout(() => done(null), timeoutMs);
    socket.on("error", () => done(null));
    socket.on("message", (message) => {
      if (message.length < 20 || !message.subarray(8, 20).equals(transaction)) return;
      let offset = 20;
      while (offset + 4 <= message.length) {
        const type = message.readUInt16BE(offset);
        const length = message.readUInt16BE(offset + 2);
        const value = message.subarray(offset + 4, offset + 4 + length);
        if ((type === 0x0020 || type === 0x0001) && value.length >= 8 && value[1] === 0x01) {
          const raw = value.readUInt32BE(4) ^ (type === 0x0020 ? 0x2112a442 : 0);
          done([raw >>> 24, (raw >>> 16) & 255, (raw >>> 8) & 255, raw & 255].join("."));
          return;
        }
        offset += 4 + length + ((4 - (length % 4)) % 4);
      }
      done(null);
    });
    socket.send(request, port, host);
  });
}

async function discoverPublicIp() {
  for (const [host, port] of [["stun.cloudflare.com", 3478], ["stun.l.google.com", 19302]]) {
    const ip = await stunPublicIp(host, port);
    if (ip) return ip;
  }
  return null;
}

// --------------------------------------------------------------- TLS

function tlsCredentials(config) {
  const { certFile, keyFile } = config.https;
  if (certFile && keyFile) {
    const resolve = (file) => path.resolve(ROOT, file);
    return { cert: fs.readFileSync(resolve(certFile)), key: fs.readFileSync(resolve(keyFile)), custom: true };
  }
  const dir = path.join(DATA_DIR, "tls");
  fs.mkdirSync(dir, { recursive: true });
  const names = ["localhost", os.hostname().toLowerCase()];
  const ips = ["127.0.0.1", ...localAddresses()];
  const wanted = [...names, ...ips].sort();
  const metaFile = path.join(dir, "names.json");
  try {
    const previous = JSON.parse(fs.readFileSync(metaFile, "utf8"));
    if (wanted.every((name) => previous.includes(name))) {
      return {
        cert: fs.readFileSync(path.join(dir, "cert.pem")),
        key: fs.readFileSync(path.join(dir, "key.pem")),
        custom: false,
      };
    }
  } catch {
    // Generate below.
  }
  log("https", "generating a self-signed certificate for " + wanted.join(", "));
  const pems = selfsigned.generate([{ name: "commonName", value: "Halo self-hosted" }], {
    days: 3650,
    keySize: 2048,
    algorithm: "sha256",
    extensions: [
      { name: "basicConstraints", cA: false },
      { name: "keyUsage", digitalSignature: true, keyEncipherment: true },
      { name: "extKeyUsage", serverAuth: true },
      {
        name: "subjectAltName",
        altNames: [
          ...names.map((value) => ({ type: 2, value })),
          ...ips.map((ip) => ({ type: 7, ip })),
        ],
      },
    ],
  });
  fs.writeFileSync(path.join(dir, "cert.pem"), pems.cert);
  fs.writeFileSync(path.join(dir, "key.pem"), pems.private, { mode: 0o600 });
  fs.writeFileSync(metaFile, JSON.stringify(wanted));
  return { cert: pems.cert, key: pems.private, custom: false };
}

// ---------------------------------------------------------- native gateway

function gatewayBinary() {
  const suffix = process.platform === "win32" ? ".exe" : "";
  const file = path.join(SERVER_DIR, "bin", `halo-native-gateway-${process.platform}-${process.arch}${suffix}`);
  return fs.existsSync(file) ? file : null;
}

async function startGateway(config, secrets) {
  const settings = config.nativeGateway;
  if (!settings.enabled) return null;
  const binary = gatewayBinary();
  if (!binary) {
    log("gateway", `no gateway binary for ${process.platform}-${process.arch}; native halo:// invites are disabled`);
    return null;
  }
  if (process.platform !== "win32") {
    try { fs.chmodSync(binary, 0o755); } catch { /* read-only copy */ }
  }
  let publicIp = settings.publicIp;
  if (!publicIp || publicIp === "auto") {
    publicIp = await discoverPublicIp();
    if (!publicIp) {
      log("gateway", "could not discover this network's public IP; set nativeGateway.publicIp in config.json");
      return null;
    }
  }
  if (isPrivateIPv4(publicIp) && !settings.allowPrivateIp) {
    log("gateway", `public IP ${publicIp} is private; set nativeGateway.allowPrivateIp to use it`);
    return null;
  }
  const port = await freePort();
  const env = {
    ...process.env,
    BIND_ADDR: `127.0.0.1:${port}`,
    CONTROL_SECRET: secrets.gatewaySecret,
    PUBLIC_WEBSOCKET_URL: GATEWAY_PLACEHOLDER_URL,
    PUBLIC_UDP_IP: publicIp,
    ALLOWED_ORIGINS: "*",
    ALLOW_ANY_ORIGIN: "true",
    ALLOW_PRIVATE_UDP_IP: settings.allowPrivateIp ? "true" : "false",
    UDP_PORT_START: String(settings.udpPortStart),
    UDP_PORT_END: String(settings.udpPortEnd),
    MAX_SESSIONS: String(settings.maxSessions),
    RUST_LOG: process.env.RUST_LOG || "warn",
  };
  const state = { port, publicIp, child: null, stopping: false };
  const launch = () => {
    const child = spawn(binary, [], { env, stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
    state.child = child;
    for (const stream of [child.stdout, child.stderr]) {
      stream.setEncoding("utf8");
      stream.on("data", (text) => {
        for (const line of text.split(/\r?\n/)) if (line.trim()) log("gateway", line.trim());
      });
    }
    child.on("exit", (code) => {
      if (state.stopping) return;
      log("gateway", `exited with code ${code}; restarting in 5 seconds`);
      setTimeout(launch, 5000);
    });
  };
  launch();
  process.on("exit", () => {
    state.stopping = true;
    state.child?.kill();
  });
  log("gateway", `native gateway on UDP ${settings.udpPortStart}-${settings.udpPortEnd} (public IP ${publicIp})`);
  return state;
}

// ------------------------------------------------------------------ lobby

async function startLobby(config, secrets, gateway) {
  if (!config.lobby.enabled) return null;
  if (!fs.existsSync(path.join(SERVER_DIR, "signaling", "index.js"))) {
    log("lobby", "no lobby service in server/signaling/index.js (bundle services/signaling); online play is off");
    return null;
  }
  const port = await freePort();
  const lobby = config.lobby;
  const bindings = {
    ENVIRONMENT: "selfhost",
    ALLOWED_ORIGINS: "*",
    ALLOW_NO_ORIGIN: "false",
    PUBLIC_GAME_URL: "http://localhost/",
    ROOM_TTL_SECONDS: String(Math.round(Math.min(24, Math.max(0.1, lobby.roomHours)) * 3600)),
    SESSION_TTL_SECONDS: "30",
    TURN_TTL_SECONDS: "3600",
    TURNSTILE_HOSTNAMES: "",
    TURNSTILE_SECRET: "",
    TURNSTILE_TEST_BYPASS: "true",
    CLOUDFLARE_ACCOUNT_ID: "",
    TURN_ACTOR_HOURLY_EGRESS_CAP_BYTES: "1000000000",
    TURN_GLOBAL_DAILY_EGRESS_CAP_BYTES: "10000000000",
    DEFAULT_ROOM_CAPACITY: String(lobby.defaultRoomCapacity),
    MAX_ROOM_CAPACITY: String(lobby.maxRoomCapacity),
    NATIVE_GATEWAY_CONTROL_URL: gateway ? `http://127.0.0.1:${gateway.port}/v1/sessions` : "",
    NATIVE_GATEWAY_SECRET: secrets.gatewaySecret,
    ROOM_ID_SECRET: secrets.roomIdSecret,
    ABUSE_ID_SECRET: secrets.abuseIdSecret,
    ADMIN_TOKEN: secrets.adminToken,
    ICE_SERVERS_JSON: config.iceServers.length ? JSON.stringify(config.iceServers) : "",
  };
  if (config.cloudflareTurn.keyId && config.cloudflareTurn.keySecret) {
    bindings.TURN_KEY_ID = config.cloudflareTurn.keyId;
    bindings.TURN_KEY_SECRET = config.cloudflareTurn.keySecret;
  }
  const mf = new Miniflare({
    host: "127.0.0.1",
    port,
    log: new Log(LogLevel.WARN),
    handleRuntimeStdio(stdout, stderr) {
      // The Worker logs one JSON line per request; show only the interesting ones.
      for (const stream of [stdout, stderr]) {
        stream.setEncoding("utf8");
        stream.on("data", (text) => {
          for (const line of text.split(/\r?\n/)) {
            if (!line.trim() || line.includes('"message":"request complete"')) continue;
            log("lobby", line.trim());
          }
        });
      }
    },
    defaultPersistRoot: path.join(DATA_DIR, "lobby"),
    durableObjectsPersist: true,
    kvPersist: true,
    unsafeTriggerHandlers: true,
    telemetry: { enabled: false },
    name: "halo-signaling",
    scriptPath: path.join(SERVER_DIR, "signaling", "index.js"),
    modules: true,
    compatibilityDate: "2026-07-30",
    compatibilityFlags: ["nodejs_compat"],
    bindings,
    durableObjects: {
      ROOMS: { className: "SignalingRoom", useSQLite: true },
      PRESENCE: { className: "PlayerPresence", useSQLite: true },
    },
    kvNamespaces: { HALO_ABUSE: "halo-abuse" },
    ratelimits: {
      ROOM_CREATE_LIMITER: { namespace_id: "41001", simple: { limit: 20, period: 60 } },
      SESSION_CREATE_LIMITER: { namespace_id: "41002", simple: { limit: 512, period: 60 } },
      TURN_ISSUE_LIMITER: { namespace_id: "41003", simple: { limit: 30, period: 60 } },
    },
    analyticsEngineDatasets: { TURN_EVENTS: { dataset: "halo_turn_events" } },
  });
  await mf.ready;
  if (bindings.TURN_KEY_ID) {
    // The upstream Worker enforces Cloudflare TURN bandwidth caps on a cron.
    setInterval(() => {
      fetch(`http://127.0.0.1:${port}/cdn-cgi/handler/scheduled?cron=*/5+*+*+*+*`).catch(() => {});
    }, 5 * 60 * 1000).unref();
  }
  log("lobby", "lobby service ready (rooms up to " + lobby.maxRoomCapacity + " players)");
  return { mf, port };
}

// ------------------------------------------------------------ static files

const CONTENT_TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".wasm": "application/wasm",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".webmanifest": "application/manifest+json; charset=utf-8",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".svg": "image/svg+xml",
  ".ico": "image/x-icon",
  ".webp": "image/webp",
  ".txt": "text/plain; charset=utf-8",
  ".map": "application/octet-stream", // Halo map files, not source maps
};

const attribute = (value) => String(value).replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");

// config.analytics: the Umami tracker, added to each page as it is served
// (the game build itself carries none). crossorigin: the page requires CORS
// or CORP of every cross-origin script (Cross-Origin-Embedder-Policy).
function injectAnalytics(html, config) {
  const { umamiScriptUrl, umamiWebsiteId } = config.analytics || {};
  if (!umamiScriptUrl || !umamiWebsiteId || !/^https?:\/\//i.test(umamiScriptUrl)) return html;
  const tag = `<script defer crossorigin="anonymous" src="${attribute(umamiScriptUrl)}" data-website-id="${attribute(umamiWebsiteId)}"></script>`;
  return /<\/head>/i.test(html) ? html.replace(/<\/head>/i, tag + "</head>") : html;
}

function isolationHeaders(res) {
  // SharedArrayBuffer (WebAssembly threads) requires cross-origin isolation.
  res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  res.setHeader("Cross-Origin-Resource-Policy", "same-origin");
  res.setHeader("X-Content-Type-Options", "nosniff");
}

function resolveStatic(urlPath) {
  let decoded;
  try {
    decoded = decodeURIComponent(urlPath);
  } catch {
    return null;
  }
  if (decoded.includes("\0")) return null;
  const segments = decoded.split("/").filter((part) => part && part !== ".");
  if (segments.includes("..")) return null;
  // Try the full path first, then without leading segments, so a reverse
  // proxy that forwards /halo/index.html unchanged still finds index.html.
  const trailing = decoded.endsWith("/");
  for (let start = 0; start < segments.length; start++) {
    const candidate = path.join(PUBLIC_DIR, ...segments.slice(start));
    if (!candidate.startsWith(PUBLIC_DIR)) return null;
    try {
      const stat = fs.statSync(candidate);
      if (stat.isFile() || stat.isDirectory()) return { file: candidate, stat, trailing };
    } catch {
      // Keep looking.
    }
  }
  // "/" itself, or a directory-style path under an unknown proxy prefix.
  if (segments.length === 0 || trailing) {
    return { file: PUBLIC_DIR, stat: fs.statSync(PUBLIC_DIR), trailing: true };
  }
  return null;
}

// The Halo Custom Edition maps in public/assets/custom_maps (with Custom
// Edition's bitmaps.map, sounds.map and loc.map, and each map's optional .txt
// and .bmp): the game cannot list a folder on the server, so it reads this.
// Each file's size and ETag (serveStatic's) come with it, so that the game
// need not ask for each one's size before it lists them (the page's fetch
// shim answers from this).
function serveCustomMapList(req, res) {
  let files = [];
  try {
    const folder = path.join(PUBLIC_DIR, "assets", "custom_maps");
    files = fs.readdirSync(folder, { withFileTypes: true })
      .filter((entry) => entry.isFile() && /^[^/\\]{1,100}\.(map|txt|bmp)$/i.test(entry.name))
      .map((entry) => {
        const stat = fs.statSync(path.join(folder, entry.name));
        return {
          name: entry.name,
          size: stat.size,
          etag: `"${stat.size.toString(16)}-${Math.floor(stat.mtimeMs).toString(16)}"`,
        };
      })
      .sort((a, b) => a.name.localeCompare(b.name));
  } catch {
    // No folder: no Custom Edition maps.
  }
  const body = Buffer.from(JSON.stringify(files));
  isolationHeaders(res);
  res.setHeader("Content-Type", "application/json; charset=utf-8");
  res.setHeader("Cache-Control", "no-cache");
  res.setHeader("Content-Length", body.length);
  res.writeHead(200);
  res.end(req.method === "HEAD" ? undefined : body);
}

function serveStatic(req, res, config) {
  const url = new URL(req.url, "http://localhost");
  if (req.method !== "GET" && req.method !== "HEAD") {
    res.writeHead(405, { Allow: "GET, HEAD" }).end();
    return;
  }
  if (/\/assets\/custom_maps\/index\.json$/.test(url.pathname)) {
    serveCustomMapList(req, res);
    return;
  }
  const found = resolveStatic(url.pathname);
  if (!found) {
    res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" }).end("Not found");
    return;
  }
  let { file, stat } = found;
  if (stat.isDirectory()) {
    if (!found.trailing && url.pathname !== "/") {
      // Relative redirect keeps working behind any proxy prefix.
      const name = url.pathname.split("/").filter(Boolean).pop();
      res.writeHead(301, { Location: `${name}/${url.search}` }).end();
      return;
    }
    file = path.join(file, "index.html");
    try {
      stat = fs.statSync(file);
    } catch {
      res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" }).end("Not found");
      return;
    }
  }
  const type = CONTENT_TYPES[path.extname(file).toLowerCase()] || "application/octet-stream";
  isolationHeaders(res);
  res.setHeader("Content-Type", type);
  res.setHeader("Cache-Control", "no-cache");
  res.setHeader("Accept-Ranges", "bytes");

  if (type.startsWith("text/html")) {
    let html = fs.readFileSync(file, "utf8");
    if (config.lobby.externalUrl) {
      html = html.replace(
        // Matches the tag whether or not Emscripten minified the page.
        /<meta\b(?=[^>]*\bname=["']?halo-signaling-url["']?(?=[\s>]))[^>]*>/,
        `<meta name="halo-signaling-url" content="${config.lobby.externalUrl.replace(/"/g, "&quot;")}">`,
      );
    }
    html = injectAnalytics(html, config);
    const body = Buffer.from(html);
    res.setHeader("Content-Length", body.length);
    res.writeHead(200);
    res.end(req.method === "HEAD" ? undefined : body);
    return;
  }

  const etag = `"${stat.size.toString(16)}-${Math.floor(stat.mtimeMs).toString(16)}"`;
  res.setHeader("ETag", etag);
  res.setHeader("Last-Modified", stat.mtime.toUTCString());
  const range = req.headers.range;
  if (!range && req.headers["if-none-match"] === etag) {
    res.writeHead(304).end();
    return;
  }
  let start = 0;
  let end = stat.size - 1;
  let status = 200;
  if (range) {
    const match = /^bytes=(\d*)-(\d*)$/.exec(range.trim());
    if (!match || (!match[1] && !match[2])) {
      res.writeHead(416, { "Content-Range": `bytes */${stat.size}` }).end();
      return;
    }
    if (match[1]) {
      start = Number(match[1]);
      end = match[2] ? Math.min(Number(match[2]), stat.size - 1) : stat.size - 1;
    } else {
      start = Math.max(0, stat.size - Number(match[2]));
    }
    if (start >= stat.size || start > end) {
      res.writeHead(416, { "Content-Range": `bytes */${stat.size}` }).end();
      return;
    }
    status = 206;
    res.setHeader("Content-Range", `bytes ${start}-${end}/${stat.size}`);
  }
  res.setHeader("Content-Length", end - start + 1);
  res.writeHead(status);
  if (req.method === "HEAD" || stat.size === 0) {
    res.end();
    return;
  }
  const stream = fs.createReadStream(file, { start, end, highWaterMark: 1 << 20 });
  stream.on("error", () => res.destroy());
  stream.pipe(res);
}

// ----------------------------------------------------------------- routing

const HOP_BY_HOP = new Set([
  "connection", "keep-alive", "proxy-authenticate", "proxy-authorization",
  "te", "trailer", "transfer-encoding", "upgrade",
]);

function clientIp(req, config) {
  if (config.trustProxyHeaders) {
    const forwarded = req.headers["x-forwarded-for"];
    if (typeof forwarded === "string" && forwarded.trim()) return forwarded.split(",")[0].trim();
    const real = req.headers["x-real-ip"] || req.headers["cf-connecting-ip"];
    if (typeof real === "string" && real.trim()) return real.trim();
  }
  return String(req.socket.remoteAddress || "unknown").replace(/^::ffff:/, "");
}

// Splits "<prefix>/v1/<rest>" and recognises "<prefix>/native-gateway/v1/<rest>".
function route(rawUrl) {
  const url = new URL(rawUrl, "http://localhost");
  const match = /^(.*?)\/v1\/(.*)$/.exec(url.pathname);
  if (!match) return null;
  const gateway = match[1].endsWith("/native-gateway") || match[1] === "native-gateway";
  return { target: gateway ? "gateway" : "lobby", path: `/v1/${match[2]}${url.search}`, rest: match[2] };
}

function forwardHeaders(req, config) {
  const headers = {};
  for (const [name, value] of Object.entries(req.headers)) {
    if (!HOP_BY_HOP.has(name)) headers[name] = value;
  }
  headers["cf-connecting-ip"] = clientIp(req, config);
  return headers;
}

function proxyHttp(req, res, port, targetPath, config) {
  const upstream = http.request({
    host: "127.0.0.1",
    port,
    method: req.method,
    path: targetPath,
    headers: forwardHeaders(req, config),
  }, (response) => {
    const headers = { ...response.headers };
    for (const name of Object.keys(headers)) if (HOP_BY_HOP.has(name)) delete headers[name];
    res.writeHead(response.statusCode || 502, headers);
    response.pipe(res);
  });
  upstream.on("error", () => {
    if (!res.headersSent) {
      res.writeHead(502, { "Content-Type": "application/json" });
    }
    res.end(JSON.stringify({ error: { code: "UNAVAILABLE", message: "The lobby service is unavailable." } }));
  });
  req.pipe(upstream);
}

function proxyUpgrade(req, socket, head, port, targetPath, config) {
  const upstream = net.connect(port, "127.0.0.1", () => {
    const lines = [`${req.method} ${targetPath} HTTP/1.1`];
    const raw = req.rawHeaders;
    for (let index = 0; index < raw.length; index += 2) {
      const name = raw[index].toLowerCase();
      if (name === "cf-connecting-ip" || name === "host") continue;
      lines.push(`${raw[index]}: ${raw[index + 1]}`);
    }
    lines.push(`Host: 127.0.0.1:${port}`);
    lines.push(`CF-Connecting-IP: ${clientIp(req, config)}`);
    upstream.write(lines.join("\r\n") + "\r\n\r\n");
    if (head && head.length) upstream.write(head);
    upstream.pipe(socket);
    socket.pipe(upstream);
  });
  const close = () => {
    upstream.destroy();
    socket.destroy();
  };
  upstream.on("error", close);
  socket.on("error", close);
  upstream.on("close", () => socket.destroy());
  socket.on("close", () => upstream.destroy());
}

// Browsers only allow WebAssembly threads on HTTPS or localhost. Someone who
// opens http://<LAN IP>:<http port>/ is sent to the HTTPS port instead. Requests
// that came through a reverse proxy (X-Forwarded-*) or use a host name are
// left alone.
function redirectToHttps(req, res, config) {
  if (req.socket.encrypted || !config.https.enabled) return false;
  if (req.headers["x-forwarded-proto"] || req.headers["x-forwarded-for"]) return false;
  const host = String(req.headers.host || "").replace(/:\d+$/, "");
  if (!/^\d+\.\d+\.\d+\.\d+$/.test(host) || host.startsWith("127.")) return false;
  res.writeHead(302, { Location: `https://${host}:${config.https.port}${req.url}` }).end();
  return true;
}

function makeHandlers(config, lobby, gateway, publicGames, deltaList) {
  const portFor = (target) => (target === "gateway" ? gateway && gateway.port : lobby && lobby.port);
  const request = (req, res) => {
    if (process.env.HALO_LOG_REQUESTS) {
      res.on("finish", () => {
        const range = req.headers.range ? ` [${req.headers.range}]` : "";
        log("http", `${req.method} ${req.url}${range} -> ${res.statusCode} ${res.getHeader("content-length") ?? ""}`);
      });
    }
    const api = route(req.url);
    if (api) {
      if (api.target === "lobby" && api.rest.startsWith("telemetry/")) {
        res.writeHead(204).end();
        return;
      }
      if (api.target === "lobby" && api.rest === "public-games") {
        if (!publicGames || req.method !== "GET") {
          res.writeHead(publicGames ? 405 : 503, { "Content-Type": "application/json" });
          res.end(JSON.stringify({ error: { code: "DISABLED", message: "The server browser is off on this server." } }));
          return;
        }
        res.writeHead(200, { "Content-Type": "application/json", "Cache-Control": "no-store" });
        res.end(JSON.stringify(publicGames.list()));
        return;
      }
      if (api.target === "lobby" && api.rest.startsWith("delta/")) {
        if (!deltaList) {
          res.writeHead(503, { "Content-Type": "text/plain" }).end("the game list is off on this server\n");
          return;
        }
        if (deltaList.handle(api.rest.replace(/\?.*$/, ""), req, res)) return;
        res.writeHead(404, { "Content-Type": "text/plain" }).end("not found\n");
        return;
      }
      const port = portFor(api.target);
      if (!port) {
        res.writeHead(503, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ error: { code: "DISABLED", message: "This service is disabled on this server." } }));
        return;
      }
      proxyHttp(req, res, port, api.path, config);
      return;
    }
    if (redirectToHttps(req, res, config)) return;
    serveStatic(req, res, config);
  };
  const upgrade = (req, socket, head) => {
    const api = route(req.url);
    const port = api && portFor(api.target);
    if (!port) {
      socket.end("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
      return;
    }
    proxyUpgrade(req, socket, head, port, api.path, config);
  };
  return { request, upgrade };
}

// "::" listens on IPv6 and IPv4 both (a reverse proxy that tries a name's
// IPv6 address first then reaches it at once); a machine without IPv6 gets
// IPv4 alone.
function listen(server, port, bind, label) {
  return new Promise((resolve, reject) => {
    const failed = (error) => {
      if (bind === "::" && ["EAFNOSUPPORT", "EADDRNOTAVAIL", "EINVAL"].includes(error.code)) {
        log("server", `${label}: no IPv6 here (${error.code}); listening on IPv4 only`);
        bind = "0.0.0.0";
        server.once("error", failed);
        server.listen(port, bind, resolve);
      } else if (error.code === "EADDRINUSE") {
        reject(new Error(`${label} port ${port} is already in use; change it in config.json`));
      } else {
        reject(error);
      }
    };
    server.once("error", failed);
    server.listen(port, bind, resolve);
  });
}

// -------------------------------------------------------------------- main

async function main() {
  fs.mkdirSync(DATA_DIR, { recursive: true });
  // (a stop asked for before this start is not for it)
  try { fs.unlinkSync(STOP_FILE); } catch {}
  const config = loadConfig();
  const secrets = loadSecrets();

  if (!fs.existsSync(path.join(PUBLIC_DIR, "halo.wasm"))) {
    log("warn", "public/halo.wasm is missing; the game files have not been built into this folder");
  }
  if (!fs.existsSync(path.join(PUBLIC_DIR, "assets", "maps", "ui.map"))) {
    log("warn", "public/assets/maps/ui.map is missing; players will be asked for their own XISO");
  }

  const gateway = await startGateway(config, secrets);
  const lobby = await startLobby(config, secrets, gateway);
  const publicGames = config.publicGames.enabled ? new PublicGames(config.publicGames.brokers, log) : null;
  const deltaList = config.delta.enabled && config.delta.url ? new DeltaList(config.delta.url, log) : null;
  const { request, upgrade } = makeHandlers(config, lobby, gateway, publicGames, deltaList);
  const servers = [];
  const urls = [];
  const hosts = ["localhost", ...localAddresses()];

  if (config.http.enabled) {
    const server = http.createServer(request);
    server.on("upgrade", upgrade);
    await listen(server, config.http.port, config.http.bind, "HTTP");
    servers.push(server);
    urls.push(`http://localhost:${config.http.port}/   (this computer only; other devices need HTTPS)`);
  }
  if (config.https.enabled) {
    const tls = tlsCredentials(config);
    const server = https.createServer({ cert: tls.cert, key: tls.key }, request);
    server.on("upgrade", upgrade);
    await listen(server, config.https.port, config.https.bind, "HTTPS");
    servers.push(server);
    for (const host of hosts) urls.push(`https://${host}:${config.https.port}/`);
    if (!tls.custom) {
      urls.push("(self-signed certificate: each device shows a one-time warning; choose Advanced > Proceed)");
    }
  }

  console.log("\n  Halo is running. Open one of these in Chrome, Edge or Firefox:\n");
  for (const url of urls) console.log("    " + url);
  console.log("\n  Press Ctrl+C to stop.\n");

  let stopping = false;
  const shutdown = async () => {
    if (stopping) return;
    stopping = true;
    log("server", "stopping");
    // (gone in 5 s even if the lobby's workerd is slow to close: the
    // scripts that restart the server wait for this process to end)
    setTimeout(() => process.exit(0), 5000).unref();
    for (const server of servers) server.close();
    if (gateway) {
      gateway.stopping = true;
      gateway.child?.kill();
    }
    if (lobby) await lobby.mf.dispose().catch(() => {});
    process.exit(0);
  };
  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
  // A stop asked for by a file (server/windows restart.ps1 and update.ps1):
  // whoever can change this folder can stop the server, however it was
  // started (a Windows task's processes cannot always be ended from outside
  // its session).
  setInterval(() => {
    if (!fs.existsSync(STOP_FILE)) return;
    try { fs.unlinkSync(STOP_FILE); } catch {}
    log("server", `asked to stop (${path.relative(ROOT, STOP_FILE)})`);
    shutdown();
  }, 1000).unref();
}

main().catch((error) => {
  console.error(`\n  Could not start Halo: ${error.message}\n`);
  process.exit(1);
});
