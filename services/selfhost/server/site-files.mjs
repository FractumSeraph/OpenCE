// What the server makes of the game's files rather than serving them as
// they are: the page (with the lobby's address and the analytics put in) and
// the Custom Edition maps' list. server.mjs answers them as they are asked
// for; static-site.mjs writes them once as files, for a host that serves
// files only (the page on a web host, the services on a VPS).

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
// (custom_edition_cache.c, HALO_WEB) instead of reading each one's first
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
