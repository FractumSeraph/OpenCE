// BLAKE2b (RFC 7693) with a digest of any length up to 64 bytes, in plain
// JavaScript: Node's crypto has BLAKE2b-512 alone, and Delta's map identity
// (ChupathingyCE's docs/delta.md, "Map identity") is BLAKE2b-256 of a
// map's every byte. 64-bit words are kept as pairs of 32-bit halves.

const IV = new Uint32Array([
  0xf3bcc908, 0x6a09e667, 0x84caa73b, 0xbb67ae85, 0xfe94f82b, 0x3c6ef372, 0x5f1d36f1, 0xa54ff53a,
  0xade682d1, 0x510e527f, 0x2b3e6c1f, 0x9b05688c, 0xfb41bd6b, 0x1f83d9ab, 0x137e2179, 0x5be0cd19,
]);

const SIGMA = [
  [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
  [14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3],
  [11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4],
  [7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8],
  [9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13],
  [2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9],
  [12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11],
  [13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10],
  [6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5],
  [10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0],
  [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
  [14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3],
].map((row) => row.map((index) => index * 2));

const v = new Uint32Array(32);
const m = new Uint32Array(32);

// v[a] += v[b] + (x, y) (64-bit, as halves at 2a, 2a+1)
function add(a, b) {
  const low = v[a] + v[b];
  let high = v[a + 1] + v[b + 1];
  if (low >= 0x100000000) high++;
  v[a] = low;
  v[a + 1] = high;
}

function addWord(a, low0, high0) {
  let low = v[a] + low0;
  let high = v[a + 1] + high0;
  if (low >= 0x100000000) high++;
  v[a] = low;
  v[a + 1] = high;
}

function mix(a, b, c, d, x, y) {
  addWord(a, m[x], m[x + 1]);
  add(a, b);
  // d = (d ^ a) >>> 32
  let low = v[d] ^ v[a];
  let high = v[d + 1] ^ v[a + 1];
  v[d] = high;
  v[d + 1] = low;
  add(c, d);
  // b = (b ^ c) >>> 24
  low = v[b] ^ v[c];
  high = v[b + 1] ^ v[c + 1];
  v[b] = (low >>> 24) ^ (high << 8);
  v[b + 1] = (high >>> 24) ^ (low << 8);
  addWord(a, m[y], m[y + 1]);
  add(a, b);
  // d = (d ^ a) >>> 16
  low = v[d] ^ v[a];
  high = v[d + 1] ^ v[a + 1];
  v[d] = (low >>> 16) ^ (high << 16);
  v[d + 1] = (high >>> 16) ^ (low << 16);
  add(c, d);
  // b = (b ^ c) >>> 63
  low = v[b] ^ v[c];
  high = v[b + 1] ^ v[c + 1];
  v[b] = (high >>> 31) ^ (low << 1);
  v[b + 1] = (low >>> 31) ^ (high << 1);
}

export class Blake2b {
  constructor(outputLength = 32) {
    if (!(outputLength >= 1 && outputLength <= 64)) throw new RangeError("BLAKE2b's digest is 1 to 64 bytes");
    this.outputLength = outputLength;
    this.h = new Uint32Array(IV);
    this.h[0] ^= 0x01010000 ^ outputLength;
    this.block = new Uint8Array(128);
    this.filled = 0;
    // (the bytes compressed so far, as two 32-bit halves of a 64-bit count)
    this.countLow = 0;
    this.countHigh = 0;
  }

  compress(last) {
    const h = this.h;
    const block = this.block;
    for (let index = 0; index < 16; index++) v[index] = h[index];
    for (let index = 0; index < 16; index++) v[16 + index] = IV[index];
    v[24] ^= this.countLow;
    v[25] ^= this.countHigh;
    if (last) {
      v[28] = ~v[28];
      v[29] = ~v[29];
    }
    for (let index = 0; index < 32; index++) {
      const at = index * 4;
      m[index] = block[at] | (block[at + 1] << 8) | (block[at + 2] << 16) | (block[at + 3] << 24);
    }
    for (let round = 0; round < 12; round++) {
      const s = SIGMA[round];
      mix(0, 8, 16, 24, s[0], s[1]);
      mix(2, 10, 18, 26, s[2], s[3]);
      mix(4, 12, 20, 28, s[4], s[5]);
      mix(6, 14, 22, 30, s[6], s[7]);
      mix(0, 10, 20, 30, s[8], s[9]);
      mix(2, 12, 22, 24, s[10], s[11]);
      mix(4, 14, 16, 26, s[12], s[13]);
      mix(6, 8, 18, 28, s[14], s[15]);
    }
    for (let index = 0; index < 16; index++) h[index] = h[index] ^ v[index] ^ v[16 + index];
  }

  count(bytes) {
    const low = this.countLow + bytes;
    this.countHigh = (this.countHigh + Math.floor(low / 0x100000000)) >>> 0;
    this.countLow = low >>> 0;
  }

  update(data) {
    const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    let at = 0;
    while (at < bytes.length) {
      // (a full block is compressed only once more follows: the last is
      // compressed by digest)
      if (this.filled === 128) {
        this.count(128);
        this.compress(false);
        this.filled = 0;
      }
      const take = Math.min(128 - this.filled, bytes.length - at);
      this.block.set(bytes.subarray(at, at + take), this.filled);
      this.filled += take;
      at += take;
    }
    return this;
  }

  digest() {
    this.count(this.filled);
    this.block.fill(0, this.filled);
    this.compress(true);
    const out = new Uint8Array(this.outputLength);
    for (let index = 0; index < this.outputLength; index++) {
      out[index] = this.h[index >> 2] >>> (8 * (index & 3));
    }
    return Buffer.from(out);
  }
}
