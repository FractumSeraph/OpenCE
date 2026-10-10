'use strict';

/* The map prefetch (fetch_path_normalization.js, HaloMapPrefetch): a map
 * whose name FetchFS writes unescaped ("[", "]") is read from the pieces the
 * prefetch kept, the site's index is looked up whatever the name's case, and
 * a "%" in a name does not throw. Run with node; no network. */

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const repository = path.join(__dirname, '..', '..', '..');
const source = fs.readFileSync(path.join(repository, 'port', 'web', 'fetch_path_normalization.js'), 'utf8');

const ORIGIN = 'https://halo.test';
const MAP = '[h2] coldsnap.map';
const SIZE = 600 * 1024;
const bytes = new Uint8Array(SIZE).map((_, index) => index & 0xff);
const index = [
  { name: MAP, size: SIZE, etag: '"map-1"' },
  { name: 'bitmaps.map', size: 1024, etag: '"b"' },
  { name: 'sounds.map', size: 1024, etag: '"s"' },
  { name: 'loc.map', size: 1024, etag: '"l"' },
  { name: '100%.map', size: 1024, etag: '"p"' },
];

const requests = [];
async function nativeFetch(resource, options) {
  const url = new URL(typeof resource === 'string' ? resource : resource.url);
  const range = options && options.headers && new Headers(options.headers).get('Range');
  requests.push({ path: url.pathname, range });
  if (url.pathname === '/assets/custom_maps/index.json') return Response.json(index);
  const name = decodeURIComponent(url.pathname.split('/').pop());
  const entry = index.find(file => file.name === name);
  if (!entry) return new Response(null, { status: 404 });
  const body = entry.name === MAP ? bytes : new Uint8Array(entry.size);
  if (!range) return new Response(body, { headers: { 'Content-Length': String(body.byteLength), ETag: entry.etag } });
  const [first, last] = range.replace('bytes=', '').split('-').map(Number);
  const end = Math.min(last, body.byteLength - 1);
  return new Response(body.slice(first, end + 1), { status: 206, headers: {
    'Content-Range': `bytes ${first}-${end}/${body.byteLength}`, ETag: entry.etag } });
}

class MemoryCache {
  constructor() { this.entries = new Map(); }
  async match(key) {
    const kept = this.entries.get(typeof key === 'string' ? key : key.url);
    return kept ? kept.clone() : undefined;
  }
  async put(key, response) { this.entries.set(typeof key === 'string' ? key : key.url, response.clone()); }
  async delete(key) { return this.entries.delete(typeof key === 'string' ? key : key.url); }
  async keys() { return [...this.entries.keys()].map(url => new Request(url)); }
}
const cacheStore = new Map();
const caches = {
  async open(name) {
    if (!cacheStore.has(name)) cacheStore.set(name, new MemoryCache());
    return cacheStore.get(name);
  },
  async delete(name) { return cacheStore.delete(name); },
};

const scope = {
  fetch: nativeFetch, caches, location: new URL(ORIGIN + '/'), navigator: {},
  URL, Request, Response, Headers, AbortController, setTimeout, clearTimeout, console,
};
scope.globalThis = scope;
vm.runInNewContext(source, scope);

async function waitForPrefetch() {
  for (let tries = 0; tries < 200; tries++) {
    const status = scope.HaloMapPrefetch.status();
    if (status.state !== 0) return status;
    await new Promise(resolve => setTimeout(resolve, 5));
  }
  throw new Error('the prefetch never ended');
}

(async () => {
  /* the Server Browser asks for the map in another case than the site's */
  scope.HaloMapPrefetch.start('[H2] Coldsnap.map');
  const status = await waitForPrefetch();
  assert.equal(status.state, 1, 'the map is listed and fetched');
  assert.equal(status.total, SIZE + 3 * 1024);
  assert.ok(requests.some(request => request.path === '/assets/custom_maps/%5Bh2%5D%20coldsnap.map' && request.range),
    'the site\'s file, in its own case, is fetched by range');

  /* the game (FetchFS) reads it with "[" and "]" as they are: from the kept
   * pieces, with no request */
  const before = requests.length;
  const response = await scope.fetch(ORIGIN + '/assets/custom_maps/[h2]%20coldsnap.map',
    { headers: { Range: 'bytes=262144-524287' } });
  assert.equal(requests.length, before, 'the piece came from the cache');
  const piece = new Uint8Array(await response.arrayBuffer());
  assert.equal(piece.byteLength, 262144);
  assert.equal(piece[0], 262144 & 0xff);

  /* a "%" in a file's name */
  const head = await scope.fetch(ORIGIN + '/assets/custom_maps/100%.map', { method: 'HEAD' });
  assert.equal(head.status, 200);
  assert.equal(head.headers.get('Content-Length'), '1024');

  console.log('fetch_path_normalization_test: ok');
})().catch(error => {
  console.error(error);
  process.exit(1);
});
