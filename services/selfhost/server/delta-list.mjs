// Delta: ChupathingyCE's game list and services (https://halo.milenko.org,
// its API at /api; the protocol in their repository's docs/delta.md).
//
// The site sends no CORS headers, so a page cannot call it; this server
// asks it for the page, for the paths below only, and keeps what it gets
// for a while (the site allows 60 calls a minute from an address):
//
//   GET <anything>/v1/delta/legacy      the signed legacy table: its
//   GET <anything>/v1/delta/legacy.sig  document and Ed25519 signature,
//                                       byte for byte (the game checks them:
//                                       port/web/src/web_delta.c)
//   GET <anything>/v1/delta/games       the site's live games (its /v1/games),
//                                       for the in-game Server Browser
//                                       (port/web/src/web_delta_list.c)
//
// and passes on, as they are, the page's own requests of Delta Stats and
// Link (online_client.js: a joined game's report, a player's line confirmed
// with the browser's player key, Link profile's code):
//
//   POST <anything>/v1/delta/client_report, /claim, /connect/start,
//        /connect/status, /connect/confirm
//
// Those go over IPv4: the site confirms a player's line only from the
// address the game's host had the player at, which for a browser is this
// server's native gateway's (its public IPv4 address). Each visitor may send
// a few a minute (DELTA_POSTS_A_MINUTE), so this server is no open relay.

import https from "node:https";

const LEGACY_SECONDS = 300;
const GAMES_SECONDS = 15;
const TIMEOUT_MILLISECONDS = 10000;
const MAXIMUM_BYTES = 1048576;
const MAXIMUM_POST_BYTES = 65536;
const POSTS_A_MINUTE = 20;
const POSTS = new Set(["client_report", "claim", "connect/start", "connect/status", "connect/confirm"]);

export class DeltaList {
  constructor(baseUrl, log) {
    this.base = String(baseUrl || "").replace(/\/+$/, "");
    this.log = log;
    this.cache = new Map();
    this.posters = new Map();
  }

  // the answer to a GET of the site's path, cached for seconds: { status,
  // type, body (bytes) }; status 502 when the site gave no answer
  async get(path, seconds) {
    const now = Date.now();
    const kept = this.cache.get(path);
    if (kept && now - kept.time < seconds * 1000) return kept.answer;
    if (kept && kept.pending) return kept.pending;
    const pending = this.fetch(path).then((answer) => {
      this.cache.set(path, { time: Date.now(), answer });
      return answer;
    });
    this.cache.set(path, { time: kept ? kept.time : 0, answer: kept && kept.answer, pending });
    return pending;
  }

  async fetch(path) {
    try {
      const response = await fetch(this.base + path, {
        headers: { "User-Agent": "halo-web (self-hosted; OpenCE browser build)" },
        redirect: "error",
        signal: AbortSignal.timeout(TIMEOUT_MILLISECONDS),
      });
      const body = Buffer.from(await response.arrayBuffer());
      if (body.length > MAXIMUM_BYTES) throw new Error(`${body.length} bytes`);
      return { status: response.status, type: response.headers.get("content-type") || "text/plain", body };
    } catch (error) {
      this.log("delta", `${path}: no answer from ${this.base} (${error.message})`);
      return { status: 502, type: "text/plain", body: Buffer.from("no answer from the game list\n") };
    }
  }

  // a JSON body POSTed to the site's path over IPv4: { status, type, body }
  post(path, body) {
    return new Promise((resolve) => {
      const url = new URL(this.base + path);
      const failed = (error) => {
        this.log("delta", `${path}: no answer from ${this.base} (${error.message})`);
        resolve({ status: 502, type: "text/plain", body: Buffer.from("no answer from the game list\n") });
      };
      if (url.protocol !== "https:") {
        failed(new Error("the game list must be HTTPS: a player's key goes there"));
        return;
      }
      const request = https.request(url, {
        method: "POST",
        family: 4,
        timeout: TIMEOUT_MILLISECONDS,
        headers: {
          "Content-Type": "application/json",
          "Content-Length": body.length,
          "User-Agent": "halo-web (self-hosted; OpenCE browser build)",
        },
      }, (response) => {
        const parts = [];
        let size = 0;
        response.on("data", (part) => {
          size += part.length;
          if (size <= MAXIMUM_BYTES) parts.push(part);
        });
        response.on("end", () => resolve({
          status: response.statusCode || 502,
          type: response.headers["content-type"] || "text/plain",
          body: Buffer.concat(parts),
        }));
        response.on("error", failed);
      });
      request.on("timeout", () => request.destroy(new Error("timed out")));
      request.on("error", failed);
      request.end(body);
    });
  }

  // whether a visitor may send another POST now (a minute's window)
  allowed(ip) {
    const now = Date.now();
    const times = (this.posters.get(ip) || []).filter((time) => now - time < 60000);
    if (times.length >= POSTS_A_MINUTE) return false;
    times.push(now);
    this.posters.set(ip, times);
    if (this.posters.size > 10000) this.posters.clear();
    return true;
  }

  // the request's route (server.mjs's api.rest, after "v1/"), answered;
  // false when it is not one of these. ip: the visitor's address
  handle(rest, req, res, ip) {
    const paths = {
      "delta/legacy": ["/v1/delta/legacy", LEGACY_SECONDS],
      "delta/legacy.sig": ["/v1/delta/legacy.sig", LEGACY_SECONDS],
      "delta/games": ["/v1/games", GAMES_SECONDS],
    };
    const posted = rest.startsWith("delta/") && POSTS.has(rest.slice(6));
    if (!Object.hasOwn(paths, rest) && !posted) return false;
    if (posted) {
      if (req.method !== "POST") {
        res.writeHead(405, { "Content-Type": "text/plain" }).end("POST only\n");
        return true;
      }
      if (!this.allowed(String(ip || ""))) {
        res.writeHead(429, { "Content-Type": "text/plain" }).end("too many requests\n");
        return true;
      }
      const parts = [];
      let size = 0;
      req.on("data", (part) => {
        size += part.length;
        if (size <= MAXIMUM_POST_BYTES) parts.push(part);
      });
      req.on("end", () => {
        const body = Buffer.concat(parts);
        let valid = size <= MAXIMUM_POST_BYTES;
        try {
          valid = valid && JSON.parse(body.toString("utf8")) !== null;
        } catch {
          valid = false;
        }
        if (!valid) {
          res.writeHead(400, { "Content-Type": "text/plain" }).end("a JSON object, please\n");
          return;
        }
        this.post(`/v1/${rest.slice(6)}`, body).then((answer) => {
          res.writeHead(answer.status, { "Content-Type": answer.type, "Cache-Control": "no-store" });
          res.end(answer.body);
        });
      });
      return true;
    }
    if (req.method !== "GET") {
      res.writeHead(405, { "Content-Type": "text/plain" }).end("GET only\n");
      return true;
    }
    const [path, seconds] = paths[rest];
    this.get(path, seconds).then((answer) => {
      res.writeHead(answer.status, {
        "Content-Type": answer.type,
        "Cache-Control": `public, max-age=${seconds}`,
      });
      res.end(answer.body);
    });
    return true;
  }
}
