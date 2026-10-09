/* Emscripten FetchFS currently joins its base URL to a root-level file path
 * with two slashes. Local development servers commonly normalize that path,
 * while object storage treats it as a different key. Install this tiny shim in
 * every game worker so map requests use the canonical object-storage URL. */
;(function installHaloFetchPathNormalization(scope) {
  "use strict";

  if (!scope || typeof scope.fetch !== "function" || scope.__haloFetchNormalized) {
    return;
  }

  const nativeFetch = scope.fetch.bind(scope);

  /* Maps already downloaded are kept in the browser (Cache Storage), so a map
   * loads from this device the next time, with no download: the pieces
   * FetchFS reads (byte ranges, or whole small files), by the file's version.
   * FetchFS asks for a file's size (HEAD) the first time it opens it each
   * session; its ETag (or Last-Modified and length) is the version, so a map
   * replaced on the server is downloaded again and the old one's pieces are
   * dropped. If the server cannot be reached, a map kept here still loads.
   * shell.html's Game settings show how much is kept and can clear it. */
  const MAP_CACHE = "halo-maps-v2";
  const mapVersions = new Map();
  const VERSION_SUFFIX = "?halo-version";
  const cacheStorage = () => {
    try { return scope.caches || null; } catch (_error) { return null; }
  };
  /* (v1 kept Custom Edition maps in 4 MB pieces, which v2's 256 KB reads
   * never ask for again) */
  if (cacheStorage()) cacheStorage().delete("halo-maps-v1").catch(() => {});
  const pieceKey = (path, version, range) => scope.location.origin + path +
    "?halo-piece=" + encodeURIComponent(version) + "&range=" + encodeURIComponent(range || "all");

  function rememberVersion(path, response) {
    const length = response.headers.get("Content-Range")
      ? response.headers.get("Content-Range").split("/")[1]
      : response.headers.get("Content-Length");
    const version = response.headers.get("ETag") ||
      (response.headers.get("Last-Modified") || "") + "/" + (length || "");
    if (!length || version === "/") return Promise.resolve();
    return keepVersion(path, version, length);
  }

  async function keepVersion(path, version, length) {
    mapVersions.set(path, version);
    const storage = cacheStorage();
    if (!storage) return;
    try {
      const cache = await storage.open(MAP_CACHE);
      const recordKey = scope.location.origin + path + VERSION_SUFFIX;
      const record = await cache.match(recordKey);
      const known = record ? await record.json() : null;
      if (known && known.version === version) return;
      if (known) {
        /* (another version on the server: its old pieces are not needed) */
        for (const request of await cache.keys()) {
          if (new URL(request.url).pathname === path) await cache.delete(request);
        }
      }
      await cache.put(recordKey, new Response(JSON.stringify({ version, length: Number(length) }),
        { headers: { "Content-Type": "application/json" } }));
    } catch (_error) {
      /* (no room, or storage blocked: maps are simply downloaded each time) */
    }
  }

  /* The server's list of the Custom Edition maps (assets/custom_maps/
   * index.json, services/selfhost/server/server.mjs) gives each file's size
   * and version: FetchFS's HEAD of each is answered from it, one request
   * where the game's listing of the maps would make one per map. */
  let customMapIndex = null;
  function customMapEntry(path) {
    if (!customMapIndex) {
      const url = scope.location.origin + path.replace(/[^/]*$/, "index.json");
      customMapIndex = nativeFetch(url, { cache: "no-cache" })
        .then(response => response.ok ? response.json() : [])
        .then(files => new Map((Array.isArray(files) ? files : [])
          .filter(file => file && typeof file.name === "string" && Number.isFinite(file.size) && file.etag)
          .map(file => [file.name, file])))
        .catch(() => { customMapIndex = null; return new Map(); });
    }
    return customMapIndex.then(entries => entries.get(decodeURIComponent(path.split("/").pop())) || null);
  }

  /* the size of a map kept here, for FetchFS when the server is unreachable */
  async function offlineHead(path) {
    const storage = cacheStorage();
    if (!storage) return null;
    try {
      const cache = await storage.open(MAP_CACHE);
      const record = await cache.match(scope.location.origin + path + VERSION_SUFFIX);
      if (!record) return null;
      const known = await record.json();
      mapVersions.set(path, known.version);
      return new Response(null, { status: 200, headers: {
        "Content-Length": String(known.length), "Accept-Ranges": "bytes", "ETag": known.version } });
    } catch (_error) {
      return null;
    }
  }

  async function cachedPiece(path, range) {
    const version = mapVersions.get(path);
    const storage = cacheStorage();
    if (!version || !storage) return null;
    try {
      const cache = await storage.open(MAP_CACHE);
      const kept = await cache.match(pieceKey(path, version, range));
      if (!kept) return null;
      /* (Cache Storage keeps no partial responses: kept as 200, given back as
       * the 206 it was) */
      const status = Number(kept.headers.get("X-Halo-Status")) || 200;
      return new Response(kept.body, { status, headers: kept.headers });
    } catch (_error) {
      return null;
    }
  }

  function keepPiece(path, range, response) {
    const version = mapVersions.get(path);
    const storage = cacheStorage();
    if (!version || !storage || !(response.status === 200 || response.status === 206)) return;
    const copy = response.clone();
    const headers = new Headers(copy.headers);
    headers.set("X-Halo-Status", String(copy.status));
    storage.open(MAP_CACHE)
      .then(cache => cache.put(pieceKey(path, version, range), new Response(copy.body, { status: 200, headers })))
      .catch(() => { /* (no room: downloaded again next time) */ });
  }

  scope.fetch = async function haloFetch(resource, options) {
    const originalUrl = typeof resource === "string" || resource instanceof URL
      ? String(resource)
      : resource && resource.url;
    let isMapRequest = false;
    let isCustomMapRequest = false;

    if (originalUrl) {
      const normalizedUrl = new URL(originalUrl, scope.location.href);
      const canonicalPath = normalizedUrl.pathname.replace(
        /\/assets\/(custom_)?maps\/{2,}/g,
        (_, custom) => `/assets/${custom || ""}maps/`
      );
      if (canonicalPath !== normalizedUrl.pathname) {
        normalizedUrl.pathname = canonicalPath;
        resource = resource instanceof Request
          ? new Request(normalizedUrl.href, resource)
          : normalizedUrl.href;
      }
      isMapRequest = canonicalPath.includes("/assets/maps/");
      /* (Custom Edition maps: no XISO fallback, as no XISO has them) */
      isCustomMapRequest = canonicalPath.includes("/assets/custom_maps/");
    }

    const method = String(
      options && options.method || resource instanceof Request && resource.method || "GET"
    ).toUpperCase();

    const mapPath = (isMapRequest || isCustomMapRequest) ? new URL(
      typeof resource === "string" ? resource : resource.url, scope.location.href).pathname : null;
    const range = mapPath && (options && options.headers && new Headers(options.headers).get("Range") ||
      resource instanceof Request && resource.headers.get("Range")) || "";
    if (mapPath && method === "GET") {
      const kept = await cachedPiece(mapPath, range);
      if (kept) return kept;
    }
    if (isCustomMapRequest && method === "HEAD") {
      const entry = await customMapEntry(mapPath);
      if (entry) {
        await keepVersion(mapPath, entry.etag, entry.size);
        return new Response(null, { status: 200, headers: {
          "Content-Length": String(entry.size), "Accept-Ranges": "bytes", "ETag": entry.etag } });
      }
    }

    /* Self-hosted: maps served by this server (for example modified maps)
     * take precedence; a locally installed XISO is only the fallback. */
    let response = await nativeFetch(resource, options).catch(error => {
      if (!isMapRequest && !isCustomMapRequest) throw error;
      return null;
    });
    if (mapPath && method === "HEAD") {
      if (response && response.ok) await rememberVersion(mapPath, response);
      else if (!response) {
        const offline = await offlineHead(mapPath);
        if (offline) return offline;
      }
    }
    if (mapPath && method === "GET" && response && response.ok) keepPiece(mapPath, range, response);
    if (isMapRequest && (!response || response.status === 404) && scope.HaloXiso &&
        typeof scope.HaloXiso.responseForMapRequest === "function") {
      const localResponse = await scope.HaloXiso.responseForMapRequest(resource, options);
      if (localResponse) return localResponse;
    }
    if (!response) response = await nativeFetch(resource, options);

    /* CloudFront serves byte ranges for these objects but does not include
     * Accept-Ranges on its HEAD response. FetchFS interprets that omission as
     * "download the entire map into one JavaScript allocation", which is both
     * memory-heavy and unreliable for the large campaign maps. Advertise the
     * capability FetchFS is about to use; every range response is still checked
     * normally by fetch before its bytes are consumed. */
    if ((isMapRequest || isCustomMapRequest) && method === "HEAD" && response.ok &&
        response.headers.has("Content-Length") &&
        !response.headers.has("Accept-Ranges")) {
      const headers = new Headers(response.headers);
      headers.set("Accept-Ranges", "bytes");
      return new Response(null, {
        status: response.status,
        statusText: response.statusText,
        headers
      });
    }

    return response;
  };
  scope.__haloFetchNormalized = true;
})(globalThis);
