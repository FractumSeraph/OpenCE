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

const LEGACY_SECONDS = 300;
const TIMEOUT_MILLISECONDS = 10000;
const MAXIMUM_BYTES = 65536;

export class DeltaList {
  constructor(baseUrl, log) {
    this.base = String(baseUrl || "").replace(/\/+$/, "");
    this.log = log;
    this.cache = new Map();
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

  // the request's route (server.mjs's api.rest, after "v1/"), answered;
  // false when it is not one of these
  handle(rest, req, res) {
    if (rest !== "delta/legacy" && rest !== "delta/legacy.sig") return false;
    if (req.method !== "GET") {
      res.writeHead(405, { "Content-Type": "text/plain" }).end("GET only\n");
      return true;
    }
    this.get(`/v1/${rest}`, LEGACY_SECONDS).then((answer) => {
      res.writeHead(answer.status, {
        "Content-Type": answer.type,
        "Cache-Control": `public, max-age=${LEGACY_SECONDS}`,
      });
      res.end(answer.body);
    });
    return true;
  }
}
