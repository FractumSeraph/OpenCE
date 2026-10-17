/*
  halomap.js: a Halo Custom Edition map (a cache file, engine version 609)
  read in the browser, for the replay viewer (index.html): its structure
  BSPs as meshes ready to draw, each with its shader's base map and its
  lightmap, its placed scenery, its sky, and any of its models and
  animations on request. Plain JavaScript, no dependencies; the files are
  read through range fetchers, so only what is drawn is fetched.

  readHaloMap({ map, bitmaps, onProgress }): map and bitmaps are each a
  fetchRange(offset, length) => Promise<Uint8Array> (bitmaps: Custom
  Edition's bitmaps.map, which holds most of a map's stock bitmaps; it may
  be left out, and those bitmaps are then null). onProgress(fraction, text)
  is told how far it has got. It returns
    { name, version, scenario, bsps: [ { name, lightmaps, meshes } ], sky,
      scenery, warnings,
      readModel, readObjectModel, listAnimations, readAnimation, listTags }
  (warnings: what was left out of a damaged map), a BSP's mesh being one
  of its lightmap materials:
    { shader, shaderGroup, baseMap, lightmap, detailMap, detailScale,
      positions, normals, uvs, lightmapUvs, indices,
      transparent, twoSided, alphaTest, blend }
  positions and normals are Halo's world units and axes (z up), uvs the base
  map's (its shader's scale and offset applied), lightmapUvs the lightmap's
  (null if the material has none), indices triangles wound counterclockwise
  seen from their front (the side their normals face), as three.js draws.

  The sky: { name, color, ambient (its outdoor fog and ambient colors, 0 to
  1), model (its model's tag name), modelIndex, geometry (that model, read
  as readModel reads it: drawn around the eye, unlit, as Halo draws a sky
  over the fog color) } or null.

  The scenery: the scenario's placed scenery, machines, controls and light
  fixtures, each { kind ("scenery", "machine", "control", "light
  fixture"), tag, tagIndex (its object's tag), model, modelIndex (its
  model's tag, or -1), animations, animationsIndex (its animation graph's,
  or -1), variant, position, rotation (yaw, pitch, roll in radians, as
  stored), matrix (its placement as the game makes it from them), bsps
  (the BSPs it is on, a bit each) }. Its model is readModel(modelIndex,
  { variant }), in the model's space, which matrix places in the world.

  Tags are named by their name or by their index in the map's tag table
  (a protected map gives many tags one name, so the index is the surer).
  readModel(model, { variant }) and readObjectModel(object, { variant }) (any
  object: a biped, vehicle, weapon, scenery, ...; it adds object,
  objectIndex, objectGroup, animations and animationsIndex) give a model,
  read once, or null:
    { name, index, nodes, regions: [ { name, permutation, meshes } ], meshes }
  each region showing the permutation the game would for the variant (of
  those it may choose, "base" or "__base" else the first: the game picks at
  random among several) at its most detailed level, meshes all of them:
    { region, permutation, shader, shaderGroup, baseMap, positions, normals,
      uvs, indices, nodeIndices, nodeWeights, transparent, twoSided,
      alphaTest, blend }
  positions in the model's space at its default pose; each vertex moved by
  two nodes (nodeIndices, Uint16Array, two a vertex) with the weights
  nodeWeights (Float32Array, summing to 1); uvs with the model's base map
  scale and the shader's applied. Its nodes: { name, parent (-1 for a
  root), firstChild, nextSibling, translation, rotation (x, y, z, w) from
  the parent at the default pose, matrix (the default pose in the model's
  space), inverseMatrix }. A vertex is posed by each node's posed matrix
  times its inverseMatrix, weighted. Matrices are 4x4, column-major (three.js
  Matrix4.fromArray); rotations are quaternions in the usual convention
  (three.js Quaternion's), Halo's conjugated.

  listAnimations(graph): [ { index, name, type, frameCount, compressed } ];
  readAnimation(graph, index): { name, type, frameCount, nodeCount,
  loopFrame, next, nodes (the graph's node names), compressed, animated:
  { rotation, translation, scale } (Uint8Array a node: which the frames
  hold), frames } with frames a Float32Array of frameCount * nodeCount *
  8: each node's rotation (x, y, z, w), translation, scale, from its
  parent, as a model's nodes' are (its nodes are the model's, in order).
  listTags(group) gives the map's tags: [ { index, name, group } ].

  A bitmap ref ({ key, name, index, width, height, format, read }) is the
  same object wherever the same bitmap is used, so a viewer makes one
  texture of each key. read() fetches and decodes its first mipmap:
  { width, height, rgba } (Uint8Array, rows from the top). Formats decoded:
  DXT1/3/5, A8R8G8B8, X8R8G8B8, R5G6B5, A1R5G5B5, A4R4G4B4, A8, Y8, AY8,
  A8Y8; cube maps, 3D bitmaps and P8 bump maps are not (their refs are null).

  The layouts are the game's (port/linux/game/tag_schema_render.c,
  tag_schema_models.c, tag_schema_scenario.c, ce_bsp.c, ce_models.c,
  ce_resources.c, object_mesh.c, source/structures/structure_bsp_definitions.h,
  source/models/, tools/ce_cache.py), and every count and pointer read is
  checked against what holds it: a damaged map throws a HaloMapError (a
  damaged bitmap, shader, model or list of placements is left out, null,
  and named in warnings; a bitmap's read() and readAnimation may throw one
  too), it never makes this read outside its file or allocate more than
  the file could describe. A failed fetch is thrown as it is. Xbox maps
  (version 5, compressed) are refused.

  An overlay animation (type 1) is added to the base pose, as
  model_animations.c adds it: rotation base times overlay (in this
  convention), translations added, scales multiplied; its nodes not
  animated are the identity. A replacement (type 2) is a whole pose, as a
  base animation's is.

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

// a sky's model (tag_schema_render.c's sky_definition)
const SKY_MODEL = 0x00;

// a gbxmodel (model_definitions.h, ce_models.c: Halo PC's parts are larger
// than the Xbox's, their local nodes at their end; a vertex is uncompressed:
// position, normal, binormal, tangent, texture coordinates, two nodes and
// their weights)
const MODEL_SIZE = 0xe8;
const MODEL_BASE_MAP_SCALE = 0x30;
const MODEL_NODES = 0xb8;
const MODEL_REGIONS = 0xc4;
const MODEL_GEOMETRIES = 0xd0;
const MODEL_SHADERS = 0xdc;
const MODEL_NODE_SIZE = 0x9c;
const MODEL_REGION_SIZE = 0x4c;
const REGION_PERMUTATIONS = 0x40;
const PERMUTATION_SIZE = 0x58;
const PERMUTATION_FLAGS = 0x20;
const PERMUTATION_VARIANT = 0x24;
const PERMUTATION_GEOMETRIES = 0x40; // (super low to super high)
const MODEL_GEOMETRY_SIZE = 0x30;
const GEOMETRY_PARTS = 0x24;
const MODEL_SHADER_SIZE = 0x20;
const PART_SIZE = 0x84;
const PART_SHADER_INDEX = 0x04;
const PART_TRIANGLES = 0x44; // type, count, offset after the vertices
const PART_VERTICES = 0x54; // type, count, and at 0x10 the offset
const PART_LOCAL_NODES_FLAG = 2;
const PART_LOCAL_NODE_COUNT = 0x6b;
const PART_LOCAL_NODES = 0x6c;
const MODEL_VERTEX_SIZE = 0x44;
const MAX_MODEL_NODES = 64;

// an object (object_definitions.h): its model and animation graph
const OBJECT_SIZE = 0x17c;
const OBJECT_MODEL = 0x28;
const OBJECT_ANIMATIONS = 0x38;

// the scenario's placed objects drawn here (scenario_definitions.h,
// tag_schema_scenario.c): their kind, placements, palette, placement size
const SCENARIO_PLACEMENTS = [
  ["scenery", 0x210, 0x21c, 0x48],
  ["machine", 0x294, 0x2a0, 0x40],
  ["control", 0x2ac, 0x2b8, 0x40],
  ["light fixture", 0x2c4, 0x2d0, 0x58],
];
const PALETTE_ENTRY_SIZE = 0x30;

// an animation graph and its animations (model_animation_definitions.h)
const ANIMATION_GRAPH_SIZE = 0x80;
const GRAPH_NODES = 0x68;
const GRAPH_ANIMATIONS = 0x74;
const GRAPH_NODE_SIZE = 0x40;
const ANIMATION_SIZE = 0xb4;

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

  /* ---------- tags by name */

  // (a tag by its name, or by its index in the tag table: a protected map
  // gives many tags one name, of which a name finds the first of the group,
  // so what this hands back names its tags by index too)
  const byName = new Map();
  for (let index = 0; index < tagCount; index++) {
    const key = instances[index].name.toLowerCase();
    if (!byName.has(key)) byName.set(key, []);
    byName.get(key).push(index);
  }
  const findTag = (key, groups) => {
    if (Number.isInteger(key)) {
      if (key < 0 || key >= tagCount || (groups && !groups.includes(instances[key].group))) return null;
      return { index: key, ...instances[key] };
    }
    if (typeof key !== "string") return null;
    for (const index of byName.get(key.toLowerCase()) ?? []) {
      if (!groups || groups.includes(instances[index].group)) return { index, ...instances[index] };
    }
    return null;
  };

  /* ---------- models */

  // the model data block (the tags' header: its file offset, its vertices'
  // size, its size): every gbxmodel part's vertices, then their triangles
  const modelDataOffset = tags.u32(TAG_BASE + 20), vertexDataSize = tags.u32(TAG_BASE + 28);
  const modelDataSize = tags.u32(TAG_BASE + 32);
  const modelDataGood = vertexDataSize <= modelDataSize && modelDataOffset >= HEADER_SIZE &&
    modelDataOffset + modelDataSize <= fileLength;

  // a model by its tag and the variant its object is placed with, read once
  const models = new Map();
  const modelByTag = (tag, variant = 0) => {
    if (!tag) return Promise.resolve(null);
    const key = `${tag.index}:${variant | 0}`;
    if (!models.has(key)) models.set(key, readModelTag(tag, variant | 0).catch(tolerate));
    return models.get(key);
  };

  async function readModelTag(tag, variant) {
    const at = root(tag, MODEL_SIZE);
    if (at === null) fail(`${tag.name} is not a model in the map`);
    if (!modelDataGood) fail("its model data is not in it");
    // (its base map's scale: a gbxmodel's texture coordinates are fractions
    // of it, ce_models.c)
    const mapScale = [tags.f32(at + MODEL_BASE_MAP_SCALE) || 1, tags.f32(at + MODEL_BASE_MAP_SCALE + 4) || 1];
    const nodes = readNodes(tags, tags.block(at + MODEL_NODES, MODEL_NODE_SIZE, MAX_MODEL_NODES, `${tag.name}'s nodes`),
      tag.name);
    const regionBlock = tags.block(at + MODEL_REGIONS, MODEL_REGION_SIZE, 32, `${tag.name}'s regions`);
    const geometries = tags.block(at + MODEL_GEOMETRIES, MODEL_GEOMETRY_SIZE, 256, `${tag.name}'s geometries`);
    const shaderBlock = tags.block(at + MODEL_SHADERS, MODEL_SHADER_SIZE, 256, `${tag.name}'s shaders`);
    const regions = [], meshes = [];
    for (let r = 0; r < regionBlock.count; r++) {
      const region = regionBlock.address + r * MODEL_REGION_SIZE;
      const permutations = tags.block(region + REGION_PERMUTATIONS, PERMUTATION_SIZE, 32,
        `${tag.name}'s permutations`);
      if (!permutations.count) continue;
      const chosen = choosePermutation(permutations, variant);
      const permutation = permutations.address + chosen * PERMUTATION_SIZE;
      // (its most detailed geometry: super high, else the next one down)
      let geometry = -1;
      for (let level = 4; level >= 0 && geometry < 0; level--) {
        const index = tags.i16(permutation + PERMUTATION_GEOMETRIES + level * 2);
        if (index >= 0 && index < geometries.count) geometry = index;
      }
      const entry = { name: tags.string(region), permutation: tags.string(permutation), meshes: [] };
      regions.push(entry);
      if (geometry < 0) continue;
      const parts = tags.block(geometries.address + geometry * MODEL_GEOMETRY_SIZE + GEOMETRY_PARTS, PART_SIZE, 32,
        `${tag.name}'s parts`);
      for (let p = 0; p < parts.count; p++) {
        const mesh = await readPart(tag, parts.address + p * PART_SIZE, shaderBlock, nodes.length, mapScale);
        if (!mesh) continue;
        mesh.region = entry.name;
        mesh.permutation = entry.permutation;
        entry.meshes.push(mesh);
        meshes.push(mesh);
      }
    }
    return { name: tag.name, index: tag.index, nodes, regions, meshes };
  }

  // (the permutation a region shows, as objects.c chooses among those that
  // may be chosen: of the placement's variant, or of variant 0 if none is,
  // or with no variant, any below 100; of those, one named "base" or
  // "__base", else the first. The game picks at random where several are)
  function choosePermutation(permutations, variant) {
    const list = (wanted) => {
      const found = [];
      for (let i = 0; i < permutations.count; i++) {
        const at = permutations.address + i * PERMUTATION_SIZE;
        if (tags.u32(at + PERMUTATION_FLAGS) & 1) continue;
        const number = tags.i16(at + PERMUTATION_VARIANT);
        if (wanted === null ? number < 100 : number === wanted) found.push(i);
      }
      return found;
    };
    let found = variant > 0 ? list(variant) : list(null);
    if (!found.length && variant > 0) found = list(0);
    if (!found.length) return 0;
    const base = found.find((i) => /^(__)?base$/i.test(tags.string(permutations.address + i * PERMUTATION_SIZE)));
    return base ?? found[0];
  }

  // a geometry part: its vertices and its triangle strip, from the model data
  async function readPart(tag, part, shaderBlock, nodeCount, mapScale) {
    const flags = tags.u32(part);
    const shaderIndex = tags.i16(part + PART_SHADER_INDEX);
    const stripType = tags.i16(part + PART_TRIANGLES);
    const stripCount = tags.u32(part + PART_TRIANGLES + 4), stripOffset = tags.u32(part + PART_TRIANGLES + 8);
    const vertexCount = tags.u32(part + PART_VERTICES + 4), vertexOffset = tags.u32(part + PART_VERTICES + 16);
    if (!vertexCount || !stripCount) return null;
    if (vertexCount > 0xffff || stripCount > 0x30000) fail(`${tag.name}'s part has ${vertexCount} vertices`);
    const indexCount = stripType === 0 ? stripCount * 3 : stripCount + 2;
    if (vertexOffset + vertexCount * MODEL_VERTEX_SIZE > vertexDataSize ||
      stripOffset + indexCount * 2 > modelDataSize - vertexDataSize)
      fail(`${tag.name}'s part's vertices or triangles are not in the model data`);
    // (a part with local nodes names its vertices' nodes by their index in
    // them, ce_models.c)
    const localCount = flags & PART_LOCAL_NODES_FLAG ? Math.min(tags.u8(part + PART_LOCAL_NODE_COUNT), 22) : 0;
    const local = [];
    for (let i = 0; i < localCount; i++) local.push(tags.u8(part + PART_LOCAL_NODES + i));
    const shaderTag = shaderIndex >= 0 && shaderIndex < shaderBlock.count ?
      reference(tags, shaderBlock.address + shaderIndex * MODEL_SHADER_SIZE, null) : null;
    const [bytes, strip, shader] = await Promise.all([
      mapFile.read(modelDataOffset + vertexOffset, vertexCount * MODEL_VERTEX_SIZE),
      mapFile.read(modelDataOffset + vertexDataSize + stripOffset, indexCount * 2),
      shaderInfo(shaderTag),
    ]);
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const f32 = (at) => { const value = view.getFloat32(at, true); return Number.isFinite(value) ? value : 0; };
    const [uScale, vScale] = shader?.uvScale ?? [1, 1];
    const [uOffset, vOffset] = shader?.uvOffset ?? [0, 0];
    const positions = new Float32Array(vertexCount * 3), normals = new Float32Array(vertexCount * 3);
    const uvs = new Float32Array(vertexCount * 2);
    const nodeIndices = new Uint16Array(vertexCount * 2), nodeWeights = new Float32Array(vertexCount * 2);
    const node = (index) => {
      if (index >= 0 && index < localCount) index = local[index];
      return index >= 0 && index < nodeCount ? index : -1;
    };
    for (let v = 0; v < vertexCount; v++) {
      const at = v * MODEL_VERTEX_SIZE;
      for (let k = 0; k < 3; k++) {
        positions[v * 3 + k] = f32(at + k * 4);
        normals[v * 3 + k] = f32(at + 12 + k * 4);
      }
      uvs[v * 2] = f32(at + 0x30) * mapScale[0] * uScale + uOffset;
      uvs[v * 2 + 1] = f32(at + 0x34) * mapScale[1] * vScale + vOffset;
      // (two nodes and the first's weight; a node it lacks is the first)
      let first = node(view.getInt16(at + 0x38, true)), second = node(view.getInt16(at + 0x3a, true));
      let weight = Math.min(1, Math.max(0, f32(at + 0x3c)));
      if (first < 0) first = second < 0 ? 0 : second;
      if (second < 0) { second = first; weight = 1; }
      nodeIndices[v * 2] = first; nodeIndices[v * 2 + 1] = second;
      nodeWeights[v * 2] = weight; nodeWeights[v * 2 + 1] = 1 - weight;
    }
    const indices = stripIndices(strip, indexCount, stripType !== 0, vertexCount);
    return {
      shader: shader?.shader ?? null, shaderGroup: shader?.shaderGroup ?? null, baseMap: shader?.baseMap ?? null,
      positions, normals, uvs, indices, nodeIndices, nodeWeights,
      transparent: shader?.transparent ?? false, twoSided: shader?.twoSided ?? false,
      alphaTest: shader?.alphaTest ?? false, blend: shader?.blend ?? null,
    };
  }

  // an object's model, by the object's tag (any object: a biped, vehicle,
  // weapon, scenery, ...), with the object's animation graph's name
  const OBJECT_GROUPS = ["bipd", "vehi", "weap", "eqip", "garb", "proj", "scen", "mach", "ctrl", "lifi", "plac",
    "ssce", "obje"];
  const objectModelTags = (tag) => {
    const at = root(tag, OBJECT_SIZE);
    if (at === null) return { model: null, animations: null };
    return { model: reference(tags, at + OBJECT_MODEL, ["mod2", "mode"]),
      animations: reference(tags, at + OBJECT_ANIMATIONS, ["antr"]) };
  };

  /* ---------- the sky */

  let sky = null;
  {
    const skies = tags.block(scenario + SCENARIO_SKIES, 16, 32, "its skies");
    const skyTag = skies.count ? reference(tags, skies.address, ["sky "]) : null;
    const skyRoot = root(skyTag, SKY_SIZE);
    if (skyRoot !== null) {
      const color = (field) => [0, 4, 8].map((k) => Math.min(1, Math.max(0, tags.f32(field + k))));
      const modelTag = reference(tags, skyRoot + SKY_MODEL, ["mod2", "mode"]);
      sky = { name: skyTag.name, color: color(skyRoot + SKY_OUTDOOR_FOG_COLOR),
        ambient: color(skyRoot + SKY_OUTDOOR_AMBIENT_COLOR), model: modelTag ? modelTag.name : null,
        modelIndex: modelTag ? modelTag.index : -1, geometry: await modelByTag(modelTag) };
    }
  }

  /* ---------- the scenery */

  // (the scenario's scenery, machines, controls and light fixtures: each
  // placed object's tag, its model's, where it is, and its rotation as the
  // game makes it, matrix4x3_rotation_from_angles)
  const scenery = [];
  // (a damaged list is left out, and said in warnings)
  for (const [kind, placements, palette, size] of SCENARIO_PLACEMENTS) try {
    const list = tags.block(scenario + placements, size, 0x10000, `its ${kind} placements`);
    const entries = tags.block(scenario + palette, PALETTE_ENTRY_SIZE, 0x10000, `its ${kind} palette`);
    for (let i = 0; i < list.count; i++) {
      const at = list.address + i * size;
      const paletteIndex = tags.i16(at);
      if (paletteIndex < 0 || paletteIndex >= entries.count) continue;
      const objectTag = reference(tags, entries.address + paletteIndex * PALETTE_ENTRY_SIZE, OBJECT_GROUPS);
      if (!objectTag) continue;
      const { model, animations } = objectModelTags(objectTag);
      const position = [0, 4, 8].map((k) => tags.f32(at + 0x08 + k));
      const rotation = [0, 4, 8].map((k) => tags.f32(at + 0x14 + k));
      scenery.push({
        kind, tag: objectTag.name, tagIndex: objectTag.index, model: model ? model.name : null,
        modelIndex: model ? model.index : -1, animations: animations ? animations.name : null,
        animationsIndex: animations ? animations.index : -1, variant: tags.i16(at + 6), position, rotation, matrix: placementMatrix(position, rotation),
        bsps: tags.u16(at + 0x20),
      });
    }
  } catch (error) {
    tolerate(error);
  }

  /* ---------- animations */

  const animationGraph = (graph) => {
    const tag = findTag(graph, ["antr"]);
    const at = root(tag, ANIMATION_GRAPH_SIZE);
    if (at === null) fail(`${graph} is not an animation graph in the map`);
    return { tag, at, list: tags.block(at + GRAPH_ANIMATIONS, ANIMATION_SIZE, 4096, `${tag.name}'s animations`),
      nodes: tags.block(at + GRAPH_NODES, GRAPH_NODE_SIZE, MAX_MODEL_NODES, `${tag.name}'s nodes`) };
  };

  // an animation's frames: each node's rotation (a quaternion x, y, z, w,
  // in the convention the nodes' are given), translation and scale, 8
  // numbers a node, nodeCount of them a frame; animated: which nodes' the
  // frames hold (the others' are the animation's defaults, or for an
  // overlay, nothing: the identity). Its type: 0 a base animation (each
  // node's pose from its parent), 1 an overlay (added to the base's, as
  // model_animations.c adds them: rotation base times overlay in this
  // convention, translations added, scales multiplied), 2 a replacement.
  // Compressed animations are not decoded (frames null)
  function readAnimation(graphName, index) {
    const graph = animationGraph(graphName);
    if (!Number.isInteger(index) || index < 0 || index >= graph.list.count)
      fail(`${graph.tag.name} has no animation ${index}`);
    const at = graph.list.address + index * ANIMATION_SIZE;
    const nodeCount = tags.i16(at + 0x2c), frameCount = tags.i16(at + 0x22), frameSize = tags.i16(at + 0x24);
    const result = {
      name: tags.string(at), type: tags.i16(at + 0x20), frameCount, nodeCount,
      loopFrame: tags.i16(at + 0x2e), next: tags.i16(at + 0x38),
      nodes: Array.from({ length: graph.nodes.count }, (_, i) => tags.string(graph.nodes.address + i * GRAPH_NODE_SIZE)),
      compressed: (tags.u16(at + 0x3a) & 1) !== 0, frames: null, animated: null,
    };
    if (result.compressed) return result;
    if (nodeCount < 1 || nodeCount > MAX_MODEL_NODES || frameCount < 1 || frameCount > 0x2000)
      fail(`${result.name} has ${nodeCount} nodes and ${frameCount} frames`);
    // (which nodes the frames hold a rotation, translation or scale of; the
    // others' are the animation's defaults)
    const bit = (field, node) => (tags.u32(at + field + (node >> 5) * 4) >>> (node & 31)) & 1;
    let size = 0, defaults = 0;
    for (let node = 0; node < nodeCount; node++) {
      size += bit(0x6c, node) ? 8 : 0; defaults += bit(0x6c, node) ? 0 : 8;
      size += bit(0x5c, node) ? 12 : 0; defaults += bit(0x5c, node) ? 0 : 12;
      size += bit(0x7c, node) ? 4 : 0; defaults += bit(0x7c, node) ? 0 : 4;
    }
    const dataSize = tags.u32(at + 0xa0), data = tags.u32(at + 0xa0 + 12);
    const defaultSize = tags.u32(at + 0x8c), defaultData = tags.u32(at + 0x8c + 12);
    // (a base animation has every default; another may have none, its
    // nodes then the identity)
    const hasDefaults = defaultSize >= defaults;
    if (size !== frameSize || dataSize < size * frameCount || (!hasDefaults && result.type === 0))
      fail(`${result.name}'s frames are not the size its nodes say`);
    if (size) tags.at(data, size * frameCount);
    if (defaults && hasDefaults) tags.at(defaultData, defaults);
    result.animated = { rotation: new Uint8Array(nodeCount), translation: new Uint8Array(nodeCount),
      scale: new Uint8Array(nodeCount) };
    for (let node = 0; node < nodeCount; node++) {
      result.animated.rotation[node] = bit(0x6c, node);
      result.animated.translation[node] = bit(0x5c, node);
      result.animated.scale[node] = bit(0x7c, node);
    }
    const frames = new Float32Array(frameCount * nodeCount * 8);
    for (let frame = 0; frame < frameCount; frame++) {
      let cursor = data + frame * size, fallback = defaultData;
      for (let node = 0; node < nodeCount; node++) {
        const out = (frame * nodeCount + node) * 8;
        // (where the node's next value is: the frame's, its default, or none)
        const take = (flag, bytes) => {
          if (bit(flag, node)) { const from = cursor; cursor += bytes; return from; }
          if (!hasDefaults) return null;
          const from = fallback; fallback += bytes; return from;
        };
        // (a rotation: four 16-bit fractions of 32767, i j k w, which Halo
        // turns the other way to the usual convention: given conjugated)
        const r = take(0x6c, 8);
        if (r === null) setRotation(frames, out, 0, 0, 0, 1);
        else setRotation(frames, out, tags.i16(r) / 32767, tags.i16(r + 2) / 32767, tags.i16(r + 4) / 32767,
          tags.i16(r + 6) / 32767);
        const t = take(0x5c, 12);
        if (t !== null) {
          frames[out + 4] = tags.f32(t); frames[out + 5] = tags.f32(t + 4); frames[out + 6] = tags.f32(t + 8);
        }
        const k = take(0x7c, 4);
        frames[out + 7] = k === null ? 1 : tags.f32(k);
      }
    }
    result.frames = frames;
    return result;
  }

  // (the animations a graph has: their names and frame counts)
  function listAnimations(graphName) {
    const graph = animationGraph(graphName);
    return Array.from({ length: graph.list.count }, (_, i) => {
      const at = graph.list.address + i * ANIMATION_SIZE;
      return { index: i, name: tags.string(at), type: tags.i16(at + 0x20), frameCount: tags.i16(at + 0x22),
        compressed: (tags.u16(at + 0x3a) & 1) !== 0 };
    });
  }

  // the loaders handed back (a tag named by its name or its index): a model,
  // an object's model by the object's tag, an animation, the tags of a group
  const loaders = {
    readModel: (model, { variant = 0 } = {}) => modelByTag(findTag(model, ["mod2", "mode"]), variant),
    readObjectModel: async (object, { variant = 0 } = {}) => {
      const objectTag = findTag(object, OBJECT_GROUPS);
      if (!objectTag) return null;
      const { model, animations } = objectModelTags(objectTag);
      const data = await modelByTag(model, variant);
      return data && { ...data, object: objectTag.name, objectIndex: objectTag.index,
        objectGroup: objectTag.group.trim(), animations: animations ? animations.name : null,
        animationsIndex: animations ? animations.index : -1 };
    },
    readAnimation: async (graph, index) => readAnimation(graph, index),
    listAnimations: async (graph) => listAnimations(graph),
    listTags: (group) => instances.map((tag, index) => ({ index, name: tag.name, group: tag.group.trim() }))
      .filter((tag) => !group || tag.group === group.trim()),
  };

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
  return { name, version, scenario: scenarioTag.name, bsps, sky, scenery, ...loaders, warnings: [...warnings] };
}

/* ---------- nodes and matrices */

// (matrices are 4x4, column-major, as three.js's Matrix4.fromArray takes
// them)

// a quaternion of Halo's put at out in frames, normalized and conjugated:
// Halo's matrix of a quaternion (matrix4x3_rotation_from_quaternion) is the
// usual one's transpose, so the usual convention's rotation is its conjugate
function setRotation(frames, out, x, y, z, w) {
  const length = Math.hypot(x, y, z, w) || 1;
  frames[out] = -x / length; frames[out + 1] = -y / length; frames[out + 2] = -z / length;
  frames[out + 3] = w / length;
}

// (a matrix of a translation, a rotation (x, y, z, w) and a scale)
export function matrixFromPose(t, q, scale = 1) {
  const [x, y, z, w] = q;
  const m = new Float32Array(16);
  m[0] = (1 - 2 * (y * y + z * z)) * scale; m[1] = 2 * (x * y + z * w) * scale; m[2] = 2 * (x * z - y * w) * scale;
  m[4] = 2 * (x * y - z * w) * scale; m[5] = (1 - 2 * (x * x + z * z)) * scale; m[6] = 2 * (y * z + x * w) * scale;
  m[8] = 2 * (x * z + y * w) * scale; m[9] = 2 * (y * z - x * w) * scale; m[10] = (1 - 2 * (x * x + y * y)) * scale;
  m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1;
  return m;
}

export function multiplyMatrices(a, b) {
  const out = new Float32Array(16);
  for (let column = 0; column < 4; column++) {
    for (let row = 0; row < 4; row++) {
      out[column * 4 + row] = a[row] * b[column * 4] + a[4 + row] * b[column * 4 + 1] +
        a[8 + row] * b[column * 4 + 2] + a[12 + row] * b[column * 4 + 3];
    }
  }
  return out;
}

// (a rotation and translation's inverse: the rotation transposed)
function invertRigid(m) {
  const out = new Float32Array(16);
  for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) out[j * 4 + i] = m[i * 4 + j];
  for (let i = 0; i < 3; i++) out[12 + i] = -(out[i] * m[12] + out[4 + i] * m[13] + out[8 + i] * m[14]);
  out[15] = 1;
  return out;
}

// a model's nodes: each one's name, parent, its default translation and
// rotation from its parent, and its default matrix in the model's space
// (the space its vertices are in) and that matrix's inverse
function readNodes(space, block, label) {
  const nodes = [];
  for (let i = 0; i < block.count; i++) {
    const at = block.address + i * MODEL_NODE_SIZE;
    const rotation = new Float32Array(4);
    setRotation(rotation, 0, space.f32(at + 0x34), space.f32(at + 0x38), space.f32(at + 0x3c), space.f32(at + 0x40));
    nodes.push({
      name: space.string(at), parent: space.i16(at + 0x24), firstChild: space.i16(at + 0x22),
      nextSibling: space.i16(at + 0x20),
      translation: [space.f32(at + 0x28), space.f32(at + 0x2c), space.f32(at + 0x30)], rotation: [...rotation],
      matrix: null, inverseMatrix: null,
    });
  }
  if (!nodes.length) fail(`${label} has no nodes`);
  // (each node's matrix after its parent's; a parent that is not a node, or
  // one that leads back to the node, makes it a root: parent -1)
  const state = new Uint8Array(nodes.length);
  const place = (i) => {
    if (state[i] === 2) return nodes[i].matrix;
    state[i] = 1;
    const parent = nodes[i].parent;
    const parentMatrix = parent >= 0 && parent < nodes.length && state[parent] !== 1 ? place(parent) : null;
    if (!parentMatrix) nodes[i].parent = -1;
    const local = matrixFromPose(nodes[i].translation, nodes[i].rotation);
    nodes[i].matrix = parentMatrix ? multiplyMatrices(parentMatrix, local) : local;
    nodes[i].inverseMatrix = invertRigid(nodes[i].matrix);
    state[i] = 2;
    return nodes[i].matrix;
  };
  for (let i = 0; i < nodes.length; i++) place(i);
  return nodes;
}

// an object's placement as the game makes it: its rotation from yaw, pitch
// and roll (matrix4x3_rotation_from_angles), forward and up from it, left
// from them (matrix4x3_rotation_from_vectors), and its position
function placementMatrix(position, [yaw, pitch, roll]) {
  const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
  const cr = Math.cos(roll), sr = Math.sin(roll);
  const forward = [cy * cp, sy * cr - cy * sp * sr, cy * sp * cr + sy * sr];
  const up = [-sp, -(cp * sr), cp * cr];
  const left = [up[1] * forward[2] - up[2] * forward[1], up[2] * forward[0] - up[0] * forward[2],
    up[0] * forward[1] - up[1] * forward[0]];
  return Float32Array.from([...forward, 0, ...left, 0, ...up, 0, ...position, 1]);
}

// (a triangle strip's triangles, each odd one turned back, its degenerate
// ones and those naming vertices it lacks left out; or a list's), wound
// counterclockwise from the front as three.js draws
function stripIndices(bytes, count, isStrip, vertexCount) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const out = [];
  for (let i = 0; i + 2 < count; i += isStrip ? 1 : 3) {
    const a = view.getUint16(i * 2, true);
    let b = view.getUint16(i * 2 + 2, true), c = view.getUint16(i * 2 + 4, true);
    if (a === b || b === c || a === c || a >= vertexCount || b >= vertexCount || c >= vertexCount) continue;
    if (isStrip && i & 1) [b, c] = [c, b];
    out.push(a, c, b);
  }
  return Uint32Array.from(out);
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
