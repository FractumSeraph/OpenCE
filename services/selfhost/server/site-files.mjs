// What the server makes of the game's files rather than serving them as
// they are: the page (with the lobby's address and the analytics put in) and
// the Custom Edition maps' list. server.mjs answers them as they are asked
// for; static-site.mjs writes them once as files, for a host that serves
// files only (the page on a web host, the services on a VPS).

import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";

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

// The page as served: config.lobby.externalUrl (the lobby service of another
// server) in its halo-signaling-url meta tag, and the analytics.
export function pageHtml(html, config) {
  if (config.lobby && config.lobby.externalUrl) {
    html = html.replace(
      // Matches the tag whether or not Emscripten minified the page.
      /<meta\b(?=[^>]*\bname=["']?halo-signaling-url["']?(?=[\s>]))[^>]*>/,
      `<meta name="halo-signaling-url" content="${attribute(config.lobby.externalUrl)}">`,
    );
  }
  return injectAnalytics(html, config);
}

// A map's header (its first 2048 bytes, a cache file's whole header), by its
// name, size and time: the game lists the maps from these
// (custom_edition_cache.c, HALO_BROWSER) instead of reading each one's first
// 256 KB piece from the server, which for a hundred maps was over 30 MB
// before the menus answered on a phone.
const MAP_HEADER_BYTES = 2048;
const mapHeaders = new Map();

function mapHeader(folder, name, stat) {
  if (!/\.map$/i.test(name)) return null;
  const key = `${name}|${stat.size}|${Math.floor(stat.mtimeMs)}`;
  if (mapHeaders.has(key)) return mapHeaders.get(key);
  let header = null;
  try {
    const descriptor = fs.openSync(path.join(folder, name), "r");
    try {
      const bytes = Buffer.alloc(Math.min(MAP_HEADER_BYTES, stat.size));
      const read = fs.readSync(descriptor, bytes, 0, bytes.length, 0);
      if (read === bytes.length) header = bytes.toString("base64");
    } finally {
      fs.closeSync(descriptor);
    }
  } catch {
    // (unreadable: the game reads the file itself)
  }
  if (mapHeaders.size > 4096) mapHeaders.clear();
  mapHeaders.set(key, header);
  return header;
}

// The Halo Custom Edition maps in public/assets/custom_maps (with Custom
// Edition's bitmaps.map, sounds.map and loc.map, and each map's optional .txt
// and .bmp): the game cannot list a folder on the server, so it reads this,
// as custom_maps/index.json. Each file's size and ETag (server.mjs's) come
// with it, so that the game need not ask for each one's size before it lists
// them (the page's fetch shim answers from this), a map's BLAKE2b-256 once
// it is known (map-hashes.mjs: Delta's map identity), and a map's header.
export function customMapList(folder, mapHashes) {
  let files = [];
  try {
    files = fs.readdirSync(folder, { withFileTypes: true })
      .filter((entry) => entry.isFile() && /^[^/\\]{1,100}\.(map|txt|bmp)$/i.test(entry.name))
      .map((entry) => {
        const stat = fs.statSync(path.join(folder, entry.name));
        const file = {
          name: entry.name,
          size: stat.size,
          etag: `"${stat.size.toString(16)}-${Math.floor(stat.mtimeMs).toString(16)}"`,
        };
        const hash = mapHashes && mapHashes.get(entry.name, stat);
        if (hash) file.blake2b = hash;
        const header = mapHeader(folder, entry.name, stat);
        if (header) file.header = header;
        return file;
      })
      .sort((a, b) => a.name.localeCompare(b.name));
  } catch {
    // No folder: no Custom Edition maps.
  }
  return files;
}

// The game's own maps (public/assets/maps) as maps.txt, for the desktop and
// Android games' first start, which can download the maps folder from here
// (data.download_url, port/linux/src/sdl_platform.c; LauncherActivity.java):
// a line each, the file's name, a tab, its size in bytes. With them the
// loading screen's picture, loading.tga, when the folder has it
// (tools/xiso_extract.py --loading-tga makes it from the disc). Then Halo
// Custom Edition's shared maps (bitmaps.map, sounds.map, loc.map), which
// every Custom Edition map needs, when the custom_maps folder beside it has
// them, as custom_maps/<name>: the games put them in maps_ce, and builds
// from before skip such lines (a name with a folder). Last Halo MCC's shared
// sets (MCC's halo1/maps/bitmaps.map, and halo1/maps/custom_edition's
// bitmaps.map and sounds.map, which some Workshop maps leave their textures
// and sounds to), when the mcc_shared folder beside it has them, as
// mcc_shared/<set>/<name>: the games put them in maps_mcc/_shared/<set>,
// with Everything or when an MCC map needs them. Their lines carry each
// file's SHA-256 as a third column: the games check the download by it, and
// copy maps_ce's bitmaps.map and sounds.map in place of the multiplayer
// set's when they are the same file (MCC's are Custom Edition's own).
// After them Halo PC's own bitmaps.map and sounds.map (a Halo PC retail
// install's), when the pc_shared folder beside it has them, as
// pc_shared/<name> with their SHA-256: the games put them in maps_pc, where
// Halo PC retail maps and HaloMD's read their stock textures and sounds.
// Every start downloads the shared files it lacks in the background
// (data.shared_files); the maps themselves come only with the first start.
const fileHashes = new Map();

function fileSha256(file, stat) {
  const key = `${file}|${stat.size}|${Math.floor(stat.mtimeMs)}`;
  if (fileHashes.has(key)) return fileHashes.get(key);
  const hash = crypto.createHash("sha256");
  const bytes = Buffer.alloc(1 << 20);
  const descriptor = fs.openSync(file, "r");
  try {
    let read;
    while ((read = fs.readSync(descriptor, bytes, 0, bytes.length, null)) > 0) hash.update(bytes.subarray(0, read));
  } finally {
    fs.closeSync(descriptor);
  }
  const digest = hash.digest("hex");
  fileHashes.set(key, digest);
  return digest;
}

export const CUSTOM_EDITION_SHARED_MAPS = ["bitmaps.map", "sounds.map", "loc.map"];
export const MCC_SHARED_FILES = ["singleplayer/bitmaps.map", "multiplayer/bitmaps.map", "multiplayer/sounds.map"];
export const HALO_PC_SHARED_MAPS = ["bitmaps.map", "sounds.map"];

export function gameMapList(folder) {
  let lines = [];
  try {
    lines = fs.readdirSync(folder, { withFileTypes: true })
      .filter((entry) => entry.isFile() && (/^[A-Za-z0-9_.-]{1,60}\.map$/.test(entry.name) || entry.name === "loading.tga"))
      .map((entry) => `${entry.name}\t${fs.statSync(path.join(folder, entry.name)).size}`)
      .sort();
  } catch {
    // (no maps here: players bring their own)
  }
  if (lines.length) {
    for (const name of CUSTOM_EDITION_SHARED_MAPS) {
      try {
        const stat = fs.statSync(path.join(folder, "..", "custom_maps", name));
        if (stat.isFile()) lines.push(`custom_maps/${name}\t${stat.size}`);
      } catch {
        // (not here: the games fetch it when a Custom Edition map needs it)
      }
    }
    for (const name of MCC_SHARED_FILES) {
      try {
        const file = path.join(folder, "..", "mcc_shared", ...name.split("/"));
        const stat = fs.statSync(file);
        if (stat.isFile()) lines.push(`mcc_shared/${name}\t${stat.size}\t${fileSha256(file, stat)}`);
      } catch {
        // (not here: players install MCC's own)
      }
    }
    for (const name of HALO_PC_SHARED_MAPS) {
      try {
        const file = path.join(folder, "..", "pc_shared", name);
        const stat = fs.statSync(file);
        if (stat.isFile()) lines.push(`pc_shared/${name}\t${stat.size}\t${fileSha256(file, stat)}`);
      } catch {
        // (not here: players copy their own Halo PC's)
      }
    }
  }
  return lines.length ? lines.join("\n") + "\n" : "";
}
