// Writes what server.mjs makes of the game's files as plain files, for a web
// host that serves files only, with this server (on a VPS) running the
// services: see HOSTING-STATIC.md.
//
//   node server/static-site.mjs <output folder> [--signaling-url <url>]
//
// writes, from ../public and ../config.json:
//
//   index.html, halo.html            the page, with the lobby's address
//                                    (--signaling-url, else the config's
//                                    lobby.externalUrl) and the analytics
//   assets/custom_maps/index.json    the Custom Edition maps' list, with
//                                    their BLAKE2b hashes (hashed once, kept
//                                    in ../data like the server's)
//
// Put these over a copy of ../public on the web host.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { MapHashes } from "./map-hashes.mjs";
import { customMapList, pageHtml } from "./site-files.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const PUBLIC_DIR = path.join(ROOT, "public");
const CUSTOM_MAPS_DIR = path.join(PUBLIC_DIR, "assets", "custom_maps");

const args = process.argv.slice(2);
const option = (name) => {
  const at = args.indexOf(name);
  if (at < 0) return null;
  const value = args[at + 1];
  args.splice(at, 2);
  return value;
};
const signalingUrl = option("--signaling-url");
const output = args[0];
if (!output || args.length !== 1) {
  console.error("usage: node server/static-site.mjs <output folder> [--signaling-url <url>]");
  process.exit(2);
}

let config = {};
try {
  config = JSON.parse(fs.readFileSync(path.join(ROOT, "config.json"), "utf8"));
} catch {
  // (no config: no analytics)
}
config.lobby = { ...(config.lobby || {}) };
if (signalingUrl) config.lobby.externalUrl = signalingUrl;
if (!config.lobby.externalUrl) {
  console.error("the lobby's address is needed: --signaling-url https://services.example.com/ (or lobby.externalUrl)");
  process.exit(2);
}

const log = (scope, message) => console.log(`${scope}: ${message}`);
const write = (name, data) => {
  const file = path.join(output, name);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, data);
  console.log(`wrote ${name} (${data.length} bytes)`);
};

for (const name of ["index.html", "halo.html"]) {
  write(name, pageHtml(fs.readFileSync(path.join(PUBLIC_DIR, name), "utf8"), config));
}

const hashes = new MapHashes(CUSTOM_MAPS_DIR, path.join(ROOT, "data", "custom-map-hashes.json"), log);
hashes.scan();
await hashes.settled();
const maps = customMapList(CUSTOM_MAPS_DIR, hashes);
write("assets/custom_maps/index.json", JSON.stringify(maps));
console.log(`${maps.length} Custom Edition files listed; lobby at ${config.lobby.externalUrl}`);
