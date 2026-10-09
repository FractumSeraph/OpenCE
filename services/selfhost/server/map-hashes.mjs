// The Custom Edition maps' BLAKE2b-256 hashes, for Delta's map identity
// (ChupathingyCE's docs/delta.md, "Map identity"): a ChupathingyCE host tells
// its clients the size and hash of its map's file, and a client whose file
// differs leaves rather than play another map. The browser cannot hash a map
// it has not downloaded whole, so this server hashes each one once, on a
// thread of its own, keeps the result in data/custom-map-hashes.json (by
// the file's name, size and time), and lists it in custom_maps/index.json
// (server.mjs) for the game (port/web/src/web_delta_peer.c).

import fs from "node:fs";
import path from "node:path";
import { Worker, isMainThread, parentPort } from "node:worker_threads";
import { Blake2b } from "./blake2b.mjs";

// (Custom Edition's shared resources are no game's map)
const SHARED = new Set(["bitmaps.map", "sounds.map", "loc.map"]);

if (!isMainThread) {
  // the worker: each file asked for, hashed, answered
  parentPort.on("message", ({ file, key }) => {
    try {
      const hash = new Blake2b(32);
      const buffer = Buffer.alloc(1 << 20);
      const descriptor = fs.openSync(file, "r");
      try {
        let read;
        while ((read = fs.readSync(descriptor, buffer, 0, buffer.length)) > 0) hash.update(buffer.subarray(0, read));
      } finally {
        fs.closeSync(descriptor);
      }
      parentPort.postMessage({ key, hash: hash.digest().toString("hex") });
    } catch (error) {
      parentPort.postMessage({ key, error: error.message });
    }
  });
}

export class MapHashes {
  constructor(folder, cacheFile, log) {
    this.folder = folder;
    this.cacheFile = cacheFile;
    this.log = log;
    this.queue = [];
    this.busy = false;
    this.worker = null;
    this.saveTimer = null;
    try {
      this.cache = JSON.parse(fs.readFileSync(cacheFile, "utf8"));
      if (!this.cache || typeof this.cache !== "object") this.cache = {};
    } catch {
      this.cache = {};
    }
  }

  // every map of the folder hashed that is not yet (at start)
  scan() {
    try {
      for (const entry of fs.readdirSync(this.folder, { withFileTypes: true })) {
        if (entry.isFile()) this.get(entry.name, fs.statSync(path.join(this.folder, entry.name)));
      }
    } catch {
      // (no folder: no Custom Edition maps)
    }
  }

  // the file's hash (hex) if it is known for its size and time, else null;
  // one not known is hashed in the background
  get(name, stat) {
    if (!/\.map$/i.test(name) || SHARED.has(name.toLowerCase())) return null;
    const key = `${name}|${stat.size}|${Math.floor(stat.mtimeMs)}`;
    const kept = this.cache[name];
    if (kept && kept.key === key && /^[0-9a-f]{64}$/.test(kept.blake2b)) return kept.blake2b;
    if (!this.queue.some((item) => item.key === key)) this.queue.push({ name, key });
    this.next();
    return null;
  }

  // resolves once every map asked for so far is hashed (static-site.mjs,
  // which writes the list once)
  settled() {
    if (!this.queue.length) return Promise.resolve();
    this.worker?.ref();
    return new Promise((resolve) => (this.waiting ||= []).push(resolve));
  }

  next() {
    if (this.busy || !this.queue.length) return;
    if (!this.worker) {
      this.worker = new Worker(new URL(import.meta.url));
      this.worker.unref();
      this.worker.on("message", (answer) => this.answered(answer));
      this.worker.on("error", (error) => {
        this.log("maps", `hashing stopped: ${error.message}`);
        this.worker = null;
        this.busy = false;
      });
    }
    const { name, key } = this.queue[0];
    this.busy = true;
    if (!this.hashing) {
      this.hashing = true;
      this.log("maps", "hashing the Custom Edition maps for Delta's map identity (once; kept in data)");
    }
    this.worker.postMessage({ file: path.join(this.folder, name), key });
  }

  answered({ key, hash, error }) {
    const item = this.queue.shift();
    this.busy = false;
    if (item && item.key === key) {
      if (hash) this.cache[item.name] = { key, blake2b: hash };
      else this.log("maps", `${item.name}: not hashed (${error})`);
      clearTimeout(this.saveTimer);
      this.saveTimer = setTimeout(() => this.save(), 2000);
    }
    if (!this.queue.length && this.hashing) {
      this.hashing = false;
      this.log("maps", "the Custom Edition maps are hashed");
    }
    if (!this.queue.length && this.waiting) {
      clearTimeout(this.saveTimer);
      this.save();
      this.worker?.unref();
      for (const resolve of this.waiting.splice(0)) resolve();
    }
    this.next();
  }

  save() {
    try {
      fs.mkdirSync(path.dirname(this.cacheFile), { recursive: true });
      fs.writeFileSync(`${this.cacheFile}.new`, JSON.stringify(this.cache));
      fs.renameSync(`${this.cacheFile}.new`, this.cacheFile);
    } catch (error) {
      this.log("maps", `the maps' hashes could not be kept: ${error.message}`);
    }
  }
}
