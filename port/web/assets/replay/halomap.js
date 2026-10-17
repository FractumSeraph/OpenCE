/*
  halomap.js: a Halo Custom Edition map (a cache file, engine version 609)
  read in the browser, for the replay viewer (index.html): its structure
  BSPs as meshes ready to draw, each with its shader's base map and its
  lightmap, and the sky's fog color. Plain JavaScript, no dependencies; the
  files are read through range fetchers, so only what is drawn is fetched.

  readHaloMap({ map, bitmaps, onProgress }): map and bitmaps are each a
  fetchRange(offset, length) => Promise<Uint8Array> (bitmaps: Custom
  Edition's bitmaps.map, which holds most of a map's stock bitmaps; it may
  be left out, and those bitmaps are then null). onProgress(fraction, text)
  is told how far it has got. It returns
    { name, version, scenario, bsps: [ { name, lightmaps, meshes } ], sky,
      scenery: [], warnings }
  (sky: { name, color, ambient }, the sky's outdoor fog and ambient colors,
  0 to 1, or null; warnings: what was left out of a damaged map), a mesh
  being one lightmap material of a BSP:
    { shader, shaderGroup, baseMap, lightmap, detailMap, detailScale,
      positions, normals, uvs, lightmapUvs, indices,
      transparent, twoSided, alphaTest, blend }
  positions and normals are Halo's world units and axes (z up), uvs the base
  map's (its shader's scale and offset applied), lightmapUvs the lightmap's
  (null if the material has none), indices triangles wound counterclockwise
  seen from their front (the side their normals face), as three.js draws.

  A bitmap ref ({ key, name, index, width, height, format, read }) is the
  same object wherever the same bitmap is used, so a viewer makes one
  texture of each key. read() fetches and decodes its first mipmap:
  { width, height, rgba } (Uint8Array, rows from the top). Formats decoded:
  DXT1/3/5, A8R8G8B8, X8R8G8B8, R5G6B5, A1R5G5B5, A4R4G4B4, A8, Y8, AY8,
  A8Y8; cube maps, 3D bitmaps and P8 bump maps are not (their refs are null).

  The layouts are the game's (port/linux/game/tag_schema_render.c,
  ce_bsp.c, ce_resources.c, source/structures/structure_bsp_definitions.h,
  tools/ce_cache.py), and every count and pointer read is checked against
  what holds it: a damaged map throws a HaloMapError (a damaged bitmap or
  shader is left out, and named in warnings; a bitmap's read() may throw
  one too), it never makes this read outside its file or allocate more
  than the file could describe. A failed fetch is thrown as it is. Xbox
  maps (version 5, compressed) are refused.

  The shaders' base maps: an environment shader's base map (with its
  primary detail map and scale in detailMap and detailScale; alpha tested
  if its flag says), a model shader's (its u and v scales applied), a
  transparent generic or chicago shader's first map (its scale and offset
  applied; an extended chicago shader's two-stage maps if it has no
  four-stage ones), a glass shader's diffuse map or else its tint map, a
  meter's map, water's base map, plasma's primary noise map. An
  environment is drawn as its base map multiplied by its lightmap (whose
  colors are the light that falls on it), a transparent shader by its blend
  ("alpha", "add", "multiply", ...); most transparent materials have no
  lightmap (glass may).
*/

/* ---------- constants */

// (where a Custom Edition map's tags are loaded: every pointer in them is
// an address there)
const TAG_BASE = 0x40440000;
const HEADER_SIZE = 0x800;
const VERSION_CE = 609;
// Halo PC retail's maps: the same format, without indexed tags
const VERSION_PC = 7;

// the most of each a map is read with: more would not fit in the game
const MAX_TAG_DATA = 0x4000000;
const MAX_TAGS = 0xffff;
const MAX_BSPS = 32;
const MAX_BSP_SIZE = 0x8000000;
const MAX_BLOCK = 0x100000;
const MAX_NAME = 256;
const MAX_BSP_VERTICES = 0x1000000;
const MAX_BSP_TRIANGLES = 0x1000000;
const MAX_BITMAP_SIDE = 4096; // (the game's largest texture)
const MAX_RESOURCE_SIZE = 0x400000;

// a structure BSP (structure_bsp_definitions.h, as ce_bsp.c reads it)
const BSP_HEADER_SIGNATURE = 0x73627370; // 'sbsp'
const BSP_LIGHTMAP_GROUP = 0x00;
const BSP_SURFACES = 0xf8;
const BSP_LIGHTMAPS = 0x104;
const SURFACE_SIZE = 6;
const LIGHTMAP_SIZE = 0x20;
const LIGHTMAP_BITMAP_INDEX = 0x00;
const LIGHTMAP_MATERIALS = 0x14;
const MATERIAL_SIZE = 0x100;
const MATERIAL_SHADER = 0x00;
const MATERIAL_FIRST_SURFACE = 0x14;
const MATERIAL_SURFACE_COUNT = 0x18;
const MATERIAL_VERTICES = 0xb0; // a vertex_buffer: type, pad, count
const MATERIAL_LIGHTMAP_VERTICES = 0xc4;
const MATERIAL_UNCOMPRESSED_DATA = 0xd8; // a tag_data: size, pad, file offset, address
// (an uncompressed environment vertex: position, normal, binormal,
// tangent, texture coordinates; a lightmap vertex: incident radiosity
// direction, texture coordinates)
const ENVIRONMENT_VERTEX_SIZE = 0x38;
const LIGHTMAP_VERTEX_SIZE = 0x14;
const VERTEX_TYPE_ENVIRONMENT_UNCOMPRESSED = 0;

// a scenario (scenario_definitions.h)
const SCENARIO_SKIES = 0x30;
const SCENARIO_BSPS = 0x5a4;
const SCENARIO_BSP_SIZE = 0x20;
const SCENARIO_SIZE = 0x5b0;

// a sky (tag_schema_render.c's sky_definition; its colors are Halo PC's
// tag layout, between the animation graph and the indoor fog screen)
const SKY_OUTDOOR_AMBIENT_COLOR = 0x48;
const SKY_OUTDOOR_FOG_COLOR = 0x58;
const SKY_SIZE = 0xd0;

// a bitmap group and its bitmaps (bitmap_group.h)
const BITMAP_GROUP_SIZE = 0x6c;
const BITMAP_GROUP_BITMAPS = 0x60;
const BITMAP_DATA_SIZE = 0x30;
const BITMAP_FLAG_SWIZZLED = 0x8;
const BITMAP_FLAG_EXTERNAL = 0x100; // Halo PC's: its pixels in bitmaps.map
const BITMAP_TYPE_2D = 0;
const BITMAP_TYPE_WHITE = 3; // (interface bitmaps: drawn as 2D ones)
const FORMATS = ["a8", "y8", "ay8", "a8y8", null, null, "r5g6b5", null, "a1r5g5b5", "a4r4g4b4",
  "x8r8g8b8", "a8r8g8b8", null, null, "dxt1", "dxt3", "dxt5", "p8"];
const BYTES_PER_PIXEL = { a8: 1, y8: 1, ay8: 1, p8: 1, a8y8: 2, r5g6b5: 2, a1r5g5b5: 2, a4r4g4b4: 2,
  x8r8g8b8: 4, a8r8g8b8: 4 };

// the shaders (tag_schema_render.c's definitions: their offsets)
const SHADER_TYPE = 0x24;
const TRANSPARENT_FLAGS = 0x29; // (a generic and a chicago shader's)
const TRANSPARENT_BLEND = 0x2c;
const TRANSPARENT_MAPS = 0x54;
const CHICAGO_EXTENDED_TWO_STAGE_MAPS = 0x60;
const GENERIC_MAP_SIZE = 0x64;
const CHICAGO_MAP_SIZE = 0xdc;
const BLENDS = ["alpha", "multiply", "double-multiply", "add", "subtract", "min", "max", "alpha-multiply-add"];

/* ---------- reading the files */

// (what a damaged map, or a reader that gives too few bytes, throws)
export class HaloMapError extends Error {
  constructor(message) {
    super(`Halo map: ${message}`);
    this.name = "HaloMapError";
  }
}

function fail(message) {
  throw new HaloMapError(message);
}

// (a file read through its range fetcher, in pages kept for the next reads:
// the runs of pages a read needs that are not kept are each fetched at once;
// a read larger than a few pages is fetched as it is and not kept)
function pagedFile(fetchRange, label, pageSize, keepBytes) {
  const pages = new Map(); // page number => Promise<Uint8Array>, oldest first
  let kept = 0;

  async function fetchExactly(offset, length) {
    const bytes = await fetchRange(offset, length);
    if (!(bytes instanceof Uint8Array)) fail(`${label}: its reader gave no bytes`);
    if (bytes.length < length) fail(`${label}: ${length} bytes at ${offset} are past its end`);
    return bytes.length === length ? bytes : bytes.subarray(0, length);
  }

  function keep(number, promise) {
    pages.set(number, promise);
    kept += pageSize;
    // (the oldest pages let go once too many are kept)
    for (const [old] of pages) {
      if (kept <= keepBytes) break;
      pages.delete(old);
      kept -= pageSize;
    }
    // (a failed fetch is not kept, so a later read tries again)
    promise.catch(() => { if (pages.get(number) === promise) { pages.delete(number); kept -= pageSize; } });
  }

  return {
    label,
    async read(offset, length) {
      if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(length) || offset < 0 || length < 0)
        fail(`${label}: a read of ${length} bytes at ${offset}`);
      if (length === 0) return new Uint8Array(0);
      if (length > pageSize * 8) return fetchExactly(offset, length);
      const first = Math.floor(offset / pageSize), last = Math.floor((offset + length - 1) / pageSize);
      // (the pages' promises taken as they are made, so none is let go
      // before this read has it)
      const wanted = [];
      for (let number = first; number <= last; number++) {
        if (!pages.has(number)) {
          // (a run of missing pages, fetched together; a page the file ends
          // in is shorter)
          let end = number;
          while (end + 1 <= last && !pages.has(end + 1)) end++;
          const start = number;
          const run = (async () => {
            const bytes = await fetchRange(start * pageSize, (end - start + 1) * pageSize);
            if (!(bytes instanceof Uint8Array)) fail(`${label}: its reader gave no bytes`);
            return bytes;
          })();
          for (let page = start; page <= end; page++) {
            const index = page - start;
            keep(page, run.then((bytes) => bytes.subarray(Math.min(bytes.length, index * pageSize),
              Math.min(bytes.length, (index + 1) * pageSize))));
          }
        }
        wanted.push(pages.get(number));
      }
      const parts = await Promise.all(wanted);
      const out = new Uint8Array(length);
      for (let i = 0; i < parts.length; i++) {
        const pageStart = (first + i) * pageSize;
        const from = Math.max(offset, pageStart), to = Math.min(offset + length, pageStart + pageSize);
        if (to - pageStart > parts[i].length) fail(`${label}: ${length} bytes at ${offset} are past its end`);
        out.set(parts[i].subarray(from - pageStart, to - pageStart), from - offset);
      }
      return out;
    },
  };
}

// (bytes that are at an address: the map's tags at TAG_BASE, a BSP at its
// base address, a bitmaps.map resource at 0. Each read is checked to lie
// in them)
class Space {
  constructor(bytes, base, label) {
    this.bytes = bytes;
    this.base = base;
    this.label = label;
    this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  }

  has(address, length) {
    const at = address - this.base;
    return Number.isSafeInteger(at) && at >= 0 && length >= 0 && at + length <= this.bytes.length;
  }

  at(address, length) {
    if (!this.has(address, length))
      fail(`${this.label}: ${length} bytes at ${(address >>> 0).toString(16)} are not in it`);
    return address - this.base;
  }

  u8(address) { return this.view.getUint8(this.at(address, 1)); }
  u16(address) { return this.view.getUint16(this.at(address, 2), true); }
  i16(address) { return this.view.getInt16(this.at(address, 2), true); }
  u32(address) { return this.view.getUint32(this.at(address, 4), true); }
  f32(address) {
    const value = this.view.getFloat32(this.at(address, 4), true);
    return Number.isFinite(value) ? value : 0;
  }

  // (a tag block's elements: their count and the address of the first, all
  // of them in these bytes and no more than max)
  block(field, elementSize, max, what) {
    const count = this.u32(field), address = this.u32(field + 4);
    if (count === 0) return { count: 0, address: 0 };
    if (count > max) fail(`${this.label}: ${what} has ${count} elements (at most ${max})`);
    this.at(address, count * elementSize);
    return { count, address };
  }

  // (a terminated string of at most MAX_NAME bytes)
  string(address) {
    const start = this.at(address, 1);
    const end = Math.min(this.bytes.length, start + MAX_NAME);
    let stop = start;
    while (stop < end && this.bytes[stop] !== 0) stop++;
    let text = "";
    for (let i = start; i < stop; i++) text += String.fromCharCode(this.bytes[i]);
    return text;
  }
}

// a group tag ('bitm') as text, as it reads in a tag's instance or reference
function groupText(value) {
  return String.fromCharCode((value >>> 24) & 255, (value >>> 16) & 255, (value >>> 8) & 255, value & 255);
}

/* ---------- the map */

export async function readHaloMap({ map, bitmaps = null, onProgress = null }) {
  if (typeof map !== "function") fail("no reader for the map");
  const progress = (fraction, text) => { if (onProgress) try { onProgress(fraction, text); } catch { /* ignored */ } };
  // (a damaged bitmap or shader is left out, its base map or lightmap null,
  // and said here, rather than the map refused: what is wrong with the map's
  // structure, or a failed fetch, is thrown)
  const warnings = new Set();
  const tolerate = (error) => {
    if (!(error instanceof HaloMapError)) throw error;
    if (warnings.size < 100) warnings.add(error.message);
    return null;
  };
  const mapFile = pagedFile(map, "the map", 0x10000, 0x2000000);
  const bitmapsFile = typeof bitmaps === "function" ? pagedFile(bitmaps, "bitmaps.map", 0x4000, 0x1000000) : null;

  // its header ("head", its version, its length, where its tags are)
  progress(0, "header");
  const header = new Space(await mapFile.read(0, HEADER_SIZE), 0, "the map's header");
  // ("head" and "foot", which read backwards in the file)
  if (header.u32(0) !== 0x68656164 || header.u32(0x7fc) !== 0x666f6f74) fail("this is not a Halo map");
  const version = header.u32(4);
  if (version === 5) fail("this is an Xbox map (compressed), which this reader does not read");
  if (version !== VERSION_CE && version !== VERSION_PC)
    fail(`version ${version} is not Custom Edition's (${VERSION_CE})`);
  const fileLength = header.u32(8);
  const tagDataOffset = header.u32(0x10), tagDataSize = header.u32(0x14);
  if (tagDataSize < 0x28 || tagDataSize > MAX_TAG_DATA || tagDataOffset < HEADER_SIZE ||
    tagDataOffset + tagDataSize > fileLength)
    fail(`its tags (${tagDataSize} bytes at ${tagDataOffset}) are not in it`);
  const name = header.string(0x20).slice(0, 31);

  // its tags, and their table
  progress(0.02, "tags");
  const tags = new Space(await mapFile.read(tagDataOffset, tagDataSize), TAG_BASE, "the map's tags");
  if (tags.u32(TAG_BASE + 0x24) !== 0x74616773) fail("its tags' header is damaged");
  const tableAddress = tags.u32(TAG_BASE), scenarioId = tags.u32(TAG_BASE + 4), tagCount = tags.u32(TAG_BASE + 12);
  if (tagCount > MAX_TAGS) fail(`it has ${tagCount} tags`);
  tags.at(tableAddress, tagCount * 32);
  const instances = [];
  for (let index = 0; index < tagCount; index++) {
    const at = tableAddress + index * 32;
    const nameAddress = tags.u32(at + 16);
    instances.push({
      group: groupText(tags.u32(at)), id: tags.u32(at + 12),
      name: tags.has(nameAddress, 1) ? tags.string(nameAddress) : `tag ${index}`,
      base: tags.u32(at + 20), indexed: tags.u32(at + 24) !== 0,
    });
  }

  // (a tag by its id, or a reference's: null if it is none, or not of one
  // of the groups by its instance or by the reference. Protected maps rename
  // some tags' groups, which the game never looks at: their scenario's, for
  // one, which the header names by its id alone)
  const tagById = (id, groups, referenceGroup = null) => {
    if (id === 0xffffffff) return null;
    const index = id & 0xffff;
    if (index >= tagCount) return null;
    const tag = instances[index];
    if (groups && !groups.includes(tag.group) && !groups.includes(referenceGroup)) return null;
    return { index, ...tag };
  };
  const reference = (space, field, groups) => tagById(space.u32(field + 12), groups, groupText(space.u32(field)));
  // (a tag's root, of size bytes, in the tags: an indexed tag's is elsewhere)
  const root = (tag, size) => {
    if (!tag || tag.indexed || !tags.has(tag.base, size)) return null;
    return tag.base;
  };

  const scenarioTag = tagById(scenarioId, null);
  const scenario = root(scenarioTag, SCENARIO_SIZE);
  if (scenario === null) fail("it has no scenario");

  /* ---------- bitmaps */

  // bitmaps.map's resources (its type, the offsets of its paths and of its
  // resources, how many: each resource its path's offset, size and offset)
  let resourcesPromise = null;
  const resources = () => resourcesPromise ??= (async () => {
    if (!bitmapsFile) return null;
    const head = new Space(await bitmapsFile.read(0, 16), 0, "bitmaps.map");
    const count = head.u32(12), tableOffset = head.u32(8);
    if (head.u32(0) !== 1 || count > 0x10000) fail("bitmaps.map is not Custom Edition's bitmaps.map");
    const table = new Space(await bitmapsFile.read(tableOffset, count * 12), 0, "bitmaps.map's resources");
    return { count, table };
  })();

  // a bitmap tag's group: its bitmaps' headers, each where its pixels are
  const bitmapGroups = new Map();
  const bitmapGroup = (tag) => {
    if (!bitmapGroups.has(tag.index)) bitmapGroups.set(tag.index, (async () => {
      let space, groupAddress;
      if (tag.indexed) {
        // (an indexed tag's group is a resource of bitmaps.map, its base
        // address the resource's index, its pointers from the resource's start)
        const table = await resources();
        if (!table || tag.base >= table.count) return null;
        const size = table.table.u32(tag.base * 12 + 4), offset = table.table.u32(tag.base * 12 + 8);
        if (size < BITMAP_GROUP_SIZE || size > MAX_RESOURCE_SIZE) fail(`${tag.name}'s resource is ${size} bytes`);
        space = new Space(await bitmapsFile.read(offset, size), 0, `${tag.name} in bitmaps.map`);
        groupAddress = 0;
      } else {
        groupAddress = root(tag, BITMAP_GROUP_SIZE);
        if (groupAddress === null) return null;
        space = tags;
      }
      const list = space.block(groupAddress + BITMAP_GROUP_BITMAPS, BITMAP_DATA_SIZE, 2048, `${tag.name}'s bitmaps`);
      const result = [];
      for (let i = 0; i < list.count; i++) {
        const at = list.address + i * BITMAP_DATA_SIZE;
        const flags = space.u16(at + 0x0e);
        result.push({
          width: space.i16(at + 4), height: space.i16(at + 6), depth: space.i16(at + 8),
          type: space.i16(at + 0x0a), format: FORMATS[space.i16(at + 0x0c)] ?? null, flags,
          offset: space.u32(at + 0x18), size: space.u32(at + 0x1c),
          // (an indexed tag's pixels, and an external bitmap's, are in
          // bitmaps.map; Halo PC retail's bitmaps.map is not Custom
          // Edition's, so a retail map's external ones are not read)
          external: tag.indexed || (flags & BITMAP_FLAG_EXTERNAL) !== 0,
        });
      }
      return result;
    })());
    return bitmapGroups.get(tag.index);
  };

  // a bitmap ref: the same object for the same bitmap; null if it cannot be
  // read or decoded
  const bitmapRefs = new Map();
  const bitmapRef = async (tag, index) => {
    if (!tag || index < 0) return null;
    const key = `${tag.index}:${index}`;
    if (bitmapRefs.has(key)) return bitmapRefs.get(key);
    bitmapRefs.set(key, (async () => {
      const group = await bitmapGroup(tag);
      const bitmap = group?.[index];
      if (!bitmap) return null;
      const { width, height, format } = bitmap;
      if (bitmap.type !== BITMAP_TYPE_2D && bitmap.type !== BITMAP_TYPE_WHITE) return null;
      if (!format || format === "p8") return null;
      if (width < 1 || height < 1 || width > MAX_BITMAP_SIDE || height > MAX_BITMAP_SIDE) return null;
      if (bitmap.external && (!bitmapsFile || version !== VERSION_CE)) return null;
      const size = firstMipmapSize(format, width, height);
      if (size > bitmap.size) return null;
      const file = bitmap.external ? bitmapsFile : mapFile;
      return {
        key, name: tag.name, index, width, height, format, external: bitmap.external,
        read: async () => {
          const pixels = await file.read(bitmap.offset, size);
          return { width, height, rgba: decodePixels(pixels, width, height, format,
            (bitmap.flags & BITMAP_FLAG_SWIZZLED) !== 0) };
        },
      };
    })().catch(tolerate));
    return bitmapRefs.get(key);
  };

  /* ---------- shaders */

  // a shader's base map and how it is drawn, by its tag's index
  const shaders = new Map();
  const shaderInfo = (tag) => {
    if (!tag) return Promise.resolve(null);
    if (!shaders.has(tag.index)) shaders.set(tag.index, readShader(tag).catch(tolerate));
    return shaders.get(tag.index);
  };
  const bitmapTag = (space, field) => reference(space, field, ["bitm"]);

  async function readShader(tag) {
    const info = {
      shader: tag.name, shaderGroup: tag.group.trim(), baseMap: null, detailMap: null, detailScale: 1,
      uvScale: [1, 1], uvOffset: [0, 0], transparent: false, twoSided: false, alphaTest: false, blend: null,
    };
    // (it is read as its type says, as Halo PC reads it, not as its group
    // does: a protected map may rename the group. Halo PC's types: the
    // Xbox's, with the extended chicago shader's after the chicago one's;
    // the port makes them the Xbox's when it loads them, tag_schema_render.c)
    const types = { 3: "senv", 4: "soso", 5: "sotr", 6: "schi", 7: "scex", 8: "swat", 9: "sgla", 10: "smet",
      11: "spla" };
    const sizes = { senv: 0x344, soso: 0x1b8, sotr: 0x6c, schi: 0x6c, scex: 0x78, sgla: 0x1e0, smet: 0x104,
      swat: 0x140, spla: 0x14c };
    if (!tags.has(tag.base, 0x28) || tag.indexed) return info;
    const kind = types[tags.i16(tag.base + SHADER_TYPE)] ?? (sizes[tag.group] ? tag.group : null);
    info.shaderGroup = kind ?? info.shaderGroup;
    const at = kind ? root(tag, sizes[kind]) : null;
    if (at === null) return info;
    let base = null;
    switch (kind) {
      case "senv": {
        // (environment: its base map at its lightmap's scale, its primary
        // detail map tiled over it; alpha tested: flags bit 0)
        info.alphaTest = (tags.u16(at + 0x28) & 1) !== 0;
        base = bitmapTag(tags, at + 0x88);
        const detail = bitmapTag(tags, at + 0xb8);
        if (detail) {
          info.detailMap = await bitmapRef(detail, 0);
          info.detailScale = tags.f32(at + 0xb4) || 1;
        }
        break;
      }
      case "soso": {
        // (model: its map's u and v scales; flags: two sided bit 1, not
        // alpha tested bit 2)
        const flags = tags.u16(at + 0x28);
        info.twoSided = (flags & 2) !== 0;
        info.alphaTest = (flags & 4) === 0;
        info.uvScale = [tags.f32(at + 0x9c) || 1, tags.f32(at + 0xa0) || 1];
        base = bitmapTag(tags, at + 0xa4);
        break;
      }
      case "sotr":
      case "schi":
      case "scex": {
        // (transparent: its first map, of its four-stage maps, or an
        // extended chicago shader's two-stage maps if it has none; flags:
        // alpha tested bit 0, two sided bit 2)
        info.transparent = true;
        const flags = tags.u8(at + TRANSPARENT_FLAGS);
        info.alphaTest = (flags & 1) !== 0;
        info.twoSided = (flags & 4) !== 0;
        info.blend = BLENDS[tags.i16(at + TRANSPARENT_BLEND)] ?? "alpha";
        const mapSize = kind === "sotr" ? GENERIC_MAP_SIZE : CHICAGO_MAP_SIZE;
        let maps = tags.block(at + TRANSPARENT_MAPS, mapSize, 64, `${tag.name}'s maps`);
        if (!maps.count && kind === "scex")
          maps = tags.block(at + CHICAGO_EXTENDED_TWO_STAGE_MAPS, mapSize, 64, `${tag.name}'s maps`);
        if (maps.count) {
          // (its scales, offsets and map: 0x04 on in a generic map, 0x54 in
          // a chicago one)
          const fields = maps.address + (kind === "sotr" ? 0x04 : 0x54);
          info.uvScale = [tags.f32(fields) || 1, tags.f32(fields + 4) || 1];
          info.uvOffset = [tags.f32(fields + 8), tags.f32(fields + 12)];
          base = bitmapTag(tags, fields + 0x18);
        }
        break;
      }
      case "sgla": {
        // (glass: its diffuse map, or its tint map)
        info.transparent = true;
        info.blend = "alpha";
        base = bitmapTag(tags, at + 0x158);
        let scale = tags.f32(at + 0x154);
        if (!base) {
          base = bitmapTag(tags, at + 0x64);
          scale = tags.f32(at + 0x60);
        }
        info.uvScale = [scale || 1, scale || 1];
        break;
      }
      case "smet":
        info.transparent = true;
        info.blend = "add";
        base = bitmapTag(tags, at + 0x4c);
        break;
      case "swat":
        // (water: its base map, usually a cube map, then null)
        info.transparent = true;
        info.blend = "alpha";
        info.twoSided = true;
        base = bitmapTag(tags, at + 0x4c);
        break;
      case "spla": {
        info.transparent = true;
        info.blend = "add";
        base = bitmapTag(tags, at + 0xd4);
        const scale = tags.f32(at + 0xd0);
        info.uvScale = [scale || 1, scale || 1];
        break;
      }
      default:
        break;
    }
    info.baseMap = await bitmapRef(base, 0);
    return info;
  }

  /* ---------- the sky */

  let sky = null;
  {
    const skies = tags.block(scenario + SCENARIO_SKIES, 16, 32, "its skies");
    const skyTag = skies.count ? reference(tags, skies.address, ["sky "]) : null;
    const skyRoot = root(skyTag, SKY_SIZE);
    if (skyRoot !== null) {
      const color = (field) => [0, 4, 8].map((k) => Math.min(1, Math.max(0, tags.f32(field + k))));
      sky = { name: skyTag.name, color: color(skyRoot + SKY_OUTDOOR_FOG_COLOR),
        ambient: color(skyRoot + SKY_OUTDOOR_AMBIENT_COLOR) };
    }
  }

  /* ---------- the structure BSPs */

  const bspList = tags.block(scenario + SCENARIO_BSPS, SCENARIO_BSP_SIZE, MAX_BSPS, "its structure BSPs");
  const bsps = [];
  for (let b = 0; b < bspList.count; b++) {
    const at = bspList.address + b * SCENARIO_BSP_SIZE;
    const offset = tags.u32(at), size = tags.u32(at + 4), base = tags.u32(at + 8);
    const bspTag = reference(tags, at + 0x10, ["sbsp"]);
    progress(0.05 + 0.9 * b / bspList.count, `structure BSP ${b + 1} of ${bspList.count}`);
    if (size < 0x18 || size > MAX_BSP_SIZE || offset < HEADER_SIZE || offset + size > fileLength)
      fail(`its structure BSP ${b} (${size} bytes at ${offset}) is not in it`);
    const space = new Space(await mapFile.read(offset, size), base, `structure BSP ${b}`);
    bsps.push(await readBsp(space, bspTag ? bspTag.name : `bsp ${b}`));
  }

  async function readBsp(space, bspName) {
    const base = space.base;
    // (its header: its structure's address, no Xbox vertex buffers, 'sbsp')
    if (space.u32(base + 20) !== BSP_HEADER_SIGNATURE) fail(`${bspName}'s header is not a Custom Edition one`);
    const structure = space.u32(base);
    space.at(structure, 0x288);
    const lightmapGroup = reference(space, structure + BSP_LIGHTMAP_GROUP, ["bitm"]);
    const surfaces = space.block(structure + BSP_SURFACES, SURFACE_SIZE, MAX_BLOCK * 4, `${bspName}'s surfaces`);
    const lightmaps = space.block(structure + BSP_LIGHTMAPS, LIGHTMAP_SIZE, 0x10000, `${bspName}'s lightmaps`);
    const surfaceIndex = (index) => space.u16(surfaces.address + index * 2);
    const meshes = [];
    let vertexTotal = 0, triangleTotal = 0;

    for (let l = 0; l < lightmaps.count; l++) {
      const lightmap = lightmaps.address + l * LIGHTMAP_SIZE;
      const bitmapIndex = space.i16(lightmap + LIGHTMAP_BITMAP_INDEX);
      const lightmapRef = bitmapIndex >= 0 ? await bitmapRef(lightmapGroup, bitmapIndex) : null;
      const materials = space.block(lightmap + LIGHTMAP_MATERIALS, MATERIAL_SIZE, 0x10000,
        `${bspName}'s lightmap materials`);
      for (let m = 0; m < materials.count; m++) {
        const material = materials.address + m * MATERIAL_SIZE;
        const vertexType = space.i16(material + MATERIAL_VERTICES);
        const vertexCount = space.u32(material + MATERIAL_VERTICES + 4);
        const lightmapVertexCount = space.u32(material + MATERIAL_LIGHTMAP_VERTICES + 4);
        const firstSurface = space.u32(material + MATERIAL_FIRST_SURFACE);
        const surfaceCount = space.u32(material + MATERIAL_SURFACE_COUNT);
        if (!vertexCount || !surfaceCount || vertexType !== VERTEX_TYPE_ENVIRONMENT_UNCOMPRESSED) continue;
        if (lightmapVertexCount && lightmapVertexCount !== vertexCount)
          fail(`${bspName}'s material has ${vertexCount} vertices and ${lightmapVertexCount} lightmap vertices`);
        if (firstSurface > surfaces.count || surfaceCount > surfaces.count - firstSurface)
          fail(`${bspName}'s material's surfaces are not in its surfaces`);
        vertexTotal += vertexCount;
        triangleTotal += surfaceCount;
        if (vertexTotal > MAX_BSP_VERTICES || triangleTotal > MAX_BSP_TRIANGLES)
          fail(`${bspName} has more vertices or triangles than Halo can draw`);
        // (its vertices, then its lightmap vertices, in its uncompressed data)
        const dataSize = space.u32(material + MATERIAL_UNCOMPRESSED_DATA);
        const data = space.u32(material + MATERIAL_UNCOMPRESSED_DATA + 12);
        const needed = vertexCount * ENVIRONMENT_VERTEX_SIZE + lightmapVertexCount * LIGHTMAP_VERTEX_SIZE;
        if (dataSize < needed) fail(`${bspName}'s material's vertices are fewer than its counts`);
        const start = space.at(data, needed);
        const view = space.view;
        const finite = (value) => (Number.isFinite(value) ? value : 0);

        const shader = await shaderInfo(reference(space, material + MATERIAL_SHADER, null));
        const [uScale, vScale] = shader?.uvScale ?? [1, 1];
        const [uOffset, vOffset] = shader?.uvOffset ?? [0, 0];
        const positions = new Float32Array(vertexCount * 3);
        const normals = new Float32Array(vertexCount * 3);
        const uvs = new Float32Array(vertexCount * 2);
        for (let v = 0; v < vertexCount; v++) {
          const p = start + v * ENVIRONMENT_VERTEX_SIZE;
          for (let k = 0; k < 3; k++) {
            positions[v * 3 + k] = finite(view.getFloat32(p + k * 4, true));
            normals[v * 3 + k] = finite(view.getFloat32(p + 12 + k * 4, true));
          }
          // (Halo's v runs down its bitmaps, as the rows are stored: three.js
          // textures uploaded with flipY false read them the same way)
          uvs[v * 2] = finite(view.getFloat32(p + 48, true)) * uScale + uOffset;
          uvs[v * 2 + 1] = finite(view.getFloat32(p + 52, true)) * vScale + vOffset;
        }
        let lightmapUvs = null;
        if (lightmapVertexCount) {
          lightmapUvs = new Float32Array(vertexCount * 2);
          const lightmapStart = start + vertexCount * ENVIRONMENT_VERTEX_SIZE;
          for (let v = 0; v < vertexCount; v++) {
            const p = lightmapStart + v * LIGHTMAP_VERTEX_SIZE;
            lightmapUvs[v * 2] = finite(view.getFloat32(p + 12, true));
            lightmapUvs[v * 2 + 1] = finite(view.getFloat32(p + 16, true));
          }
        }
        // (its triangles, of its own vertices: Halo winds them clockwise
        // seen from the front, so each is turned; a triangle naming a vertex
        // it does not have is left out)
        const indices = new Uint32Array(surfaceCount * 3);
        let used = 0;
        for (let s = 0; s < surfaceCount; s++) {
          const first = (firstSurface + s) * 3;
          const a = surfaceIndex(first), b = surfaceIndex(first + 1), c = surfaceIndex(first + 2);
          if (a >= vertexCount || b >= vertexCount || c >= vertexCount) continue;
          indices[used++] = a;
          indices[used++] = c;
          indices[used++] = b;
        }
        meshes.push({
          shader: shader?.shader ?? null, shaderGroup: shader?.shaderGroup ?? null,
          baseMap: shader?.baseMap ?? null, lightmap: lightmapRef,
          detailMap: shader?.detailMap ?? null, detailScale: shader?.detailScale ?? 1,
          positions, normals, uvs, lightmapUvs,
          indices: used === indices.length ? indices : indices.slice(0, used),
          transparent: shader?.transparent ?? false, twoSided: shader?.twoSided ?? false,
          alphaTest: shader?.alphaTest ?? false, blend: shader?.blend ?? null,
        });
      }
    }
    return { name: bspName, lightmaps: lightmapGroup ? lightmapGroup.name : null, meshes };
  }

  progress(1, "done");
  return { name, version, scenario: scenarioTag.name, bsps, sky, scenery: [], warnings: [...warnings] };
}

/* ---------- bitmaps' pixels */

// the bytes of a bitmap's first mipmap
function firstMipmapSize(format, width, height) {
  if (format === "dxt1") return Math.ceil(width / 4) * Math.ceil(height / 4) * 8;
  if (format === "dxt3" || format === "dxt5") return Math.ceil(width / 4) * Math.ceil(height / 4) * 16;
  return width * height * BYTES_PER_PIXEL[format];
}

// (an Xbox bitmap's swizzled pixels in rows: the bits of x and y interleaved,
// x's first, as far as the smaller side goes)
function unswizzle(pixels, width, height, bytesPerPixel) {
  const out = new Uint8Array(pixels.length);
  const masks = (() => {
    let mx = 0, my = 0, bit = 1, w = width, h = height;
    while (w > 1 || h > 1) {
      if (w > 1) { mx |= bit; bit <<= 1; w >>= 1; }
      if (h > 1) { my |= bit; bit <<= 1; h >>= 1; }
    }
    return [mx, my];
  })();
  const spread = (value, mask) => {
    let result = 0;
    for (let bit = 1; mask; bit <<= 1) {
      const low = mask & -mask;
      if (value & bit) result |= low;
      mask &= mask - 1;
    }
    return result;
  };
  for (let y = 0; y < height; y++) {
    const sy = spread(y, masks[1]);
    for (let x = 0; x < width; x++) {
      const from = (spread(x, masks[0]) | sy) * bytesPerPixel, to = (y * width + x) * bytesPerPixel;
      for (let k = 0; k < bytesPerPixel; k++) out[to + k] = pixels[from + k];
    }
  }
  return out;
}

// a bitmap's first mipmap as RGBA, 4 bytes a pixel, rows from the top
export function decodePixels(pixels, width, height, format, swizzled = false) {
  const rgba = new Uint8Array(width * height * 4);
  if (format === "dxt1" || format === "dxt3" || format === "dxt5") {
    decodeDxt(pixels, width, height, format, rgba);
    return rgba;
  }
  const bytesPerPixel = BYTES_PER_PIXEL[format];
  if (!bytesPerPixel) fail(`bitmap format ${format} is not decoded`);
  if (swizzled) pixels = unswizzle(pixels, width, height, bytesPerPixel);
  const view = new DataView(pixels.buffer, pixels.byteOffset, pixels.byteLength);
  const count = width * height;
  for (let i = 0, o = 0; i < count; i++, o += 4) {
    let r, g, b, a = 255;
    switch (format) {
      case "a8": r = g = b = 255; a = pixels[i]; break;
      case "y8": r = g = b = pixels[i]; break;
      case "ay8": r = g = b = a = pixels[i]; break;
      case "a8y8": r = g = b = pixels[i * 2]; a = pixels[i * 2 + 1]; break;
      case "r5g6b5": {
        const v = view.getUint16(i * 2, true);
        r = ((v >> 11) & 31) * 255 / 31; g = ((v >> 5) & 63) * 255 / 63; b = (v & 31) * 255 / 31;
        break;
      }
      case "a1r5g5b5": {
        const v = view.getUint16(i * 2, true);
        a = v & 0x8000 ? 255 : 0;
        r = ((v >> 10) & 31) * 255 / 31; g = ((v >> 5) & 31) * 255 / 31; b = (v & 31) * 255 / 31;
        break;
      }
      case "a4r4g4b4": {
        const v = view.getUint16(i * 2, true);
        a = ((v >> 12) & 15) * 17; r = ((v >> 8) & 15) * 17; g = ((v >> 4) & 15) * 17; b = (v & 15) * 17;
        break;
      }
      // (in memory: blue, green, red, alpha)
      case "x8r8g8b8": b = pixels[i * 4]; g = pixels[i * 4 + 1]; r = pixels[i * 4 + 2]; break;
      case "a8r8g8b8": b = pixels[i * 4]; g = pixels[i * 4 + 1]; r = pixels[i * 4 + 2]; a = pixels[i * 4 + 3]; break;
      default: fail(`bitmap format ${format} is not decoded`);
    }
    rgba[o] = r; rgba[o + 1] = g; rgba[o + 2] = b; rgba[o + 3] = a;
  }
  return rgba;
}

// (DXT's 4x4 blocks: DXT1 two 5:6:5 colors and 2 bits a pixel; DXT3 4 bits
// of alpha a pixel before them, DXT5 two alphas and 3 bits a pixel)
function decodeDxt(pixels, width, height, format, rgba) {
  const blockSize = format === "dxt1" ? 8 : 16;
  const blocksWide = Math.ceil(width / 4), blocksHigh = Math.ceil(height / 4);
  const colors = new Uint8Array(16), alphas = new Uint8Array(8);
  const expand = (v, k) => { colors[k] = ((v >> 11) & 31) * 255 / 31; colors[k + 1] = ((v >> 5) & 63) * 255 / 63;
    colors[k + 2] = (v & 31) * 255 / 31; colors[k + 3] = 255; };
  for (let by = 0; by < blocksHigh; by++) {
    for (let bx = 0; bx < blocksWide; bx++) {
      const block = (by * blocksWide + bx) * blockSize;
      const c = block + (format === "dxt1" ? 0 : 8);
      const c0 = pixels[c] | (pixels[c + 1] << 8), c1 = pixels[c + 2] | (pixels[c + 3] << 8);
      expand(c0, 0);
      expand(c1, 4);
      if (c0 > c1 || format !== "dxt1") {
        for (let k = 0; k < 3; k++) {
          colors[8 + k] = (2 * colors[k] + colors[4 + k]) / 3;
          colors[12 + k] = (colors[k] + 2 * colors[4 + k]) / 3;
        }
        colors[11] = colors[15] = 255;
      } else {
        for (let k = 0; k < 3; k++) { colors[8 + k] = (colors[k] + colors[4 + k]) / 2; colors[12 + k] = 0; }
        colors[11] = 255;
        colors[15] = 0;
      }
      if (format === "dxt5") {
        const a0 = pixels[block], a1 = pixels[block + 1];
        alphas[0] = a0; alphas[1] = a1;
        if (a0 > a1) for (let k = 1; k < 7; k++) alphas[k + 1] = ((7 - k) * a0 + k * a1) / 7;
        else {
          for (let k = 1; k < 5; k++) alphas[k + 1] = ((5 - k) * a0 + k * a1) / 5;
          alphas[6] = 0; alphas[7] = 255;
        }
      }
      const bits = pixels[c + 4] | (pixels[c + 5] << 8) | (pixels[c + 6] << 16) | (pixels[c + 7] << 24);
      for (let py = 0; py < 4; py++) {
        const y = by * 4 + py;
        if (y >= height) break;
        for (let px = 0; px < 4; px++) {
          const x = bx * 4 + px;
          if (x >= width) continue;
          const n = py * 4 + px, o = (y * width + x) * 4, k = ((bits >>> (n * 2)) & 3) * 4;
          rgba[o] = colors[k]; rgba[o + 1] = colors[k + 1]; rgba[o + 2] = colors[k + 2]; rgba[o + 3] = colors[k + 3];
          if (format === "dxt3") {
            const nibble = (pixels[block + (n >> 1)] >> ((n & 1) * 4)) & 15;
            rgba[o + 3] = nibble * 17;
          } else if (format === "dxt5") {
            // (48 bits of 3-bit indices after the two alphas)
            const bit = n * 3, byte = block + 2 + (bit >> 3), shift = bit & 7;
            const index = ((pixels[byte] | ((pixels[byte + 1] ?? 0) << 8)) >> shift) & 7;
            rgba[o + 3] = alphas[index];
          }
        }
      }
    }
  }
}
