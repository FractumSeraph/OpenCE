// The in-game server browser's public games (Join Game > Server Browser).
//
// Native OpenCE hosts publish a signed listing of each public game to
// hceu/3/lobby/s/<key hash> on the public MQTT brokers (port/linux/src/
// p2p_lobby.c). A browser cannot open TCP connections, so this server
// subscribes for it and hands the listings over as they came:
//
//   GET <prefix>/v1/public-games  ->  { v: 1, games: [{ slot, payload, retained }] }
//
// payload is the listing's bytes in base64. The game checks each signature
// itself (p2p_lobby.c), so this relay is trusted with nothing: it can drop
// or delay listings, never forge one. Joining a listed game uses its invite
// through the native gateway, like a pasted halo://join link.
//
// The brokers are only contacted while someone has the server browser open
// (and for IDLE_MS after), and the hosts are asked to publish again at most
// every QUERY_INTERVAL_MS.

import crypto from "node:crypto";
import net from "node:net";

const SLOT_PREFIX = "hceu/3/lobby/s/";
const QUERY_TOPIC = "hceu/3/lobby/q";
const SLOT_PATTERN = /^hceu\/3\/lobby\/s\/([0-9a-f]{32})$/;
// (p2p_lobby.c's GAME_EXPIRY: a live host publishes every 30 seconds)
const GAME_EXPIRY_MS = 90_000;
const IDLE_MS = 5 * 60_000;
const QUERY_INTERVAL_MS = 20_000;
const KEEP_ALIVE_SECONDS = 60;
const MAXIMUM_GAMES = 256;
const MAXIMUM_PAYLOAD = 512;
const MAXIMUM_PACKET = 64 * 1024;

const mqttString = (text) => {
  const bytes = Buffer.from(text);
  return Buffer.concat([Buffer.from([bytes.length >> 8, bytes.length & 255]), bytes]);
};

function mqttPacket(header, body) {
  const length = [];
  let remaining = body.length;
  do {
    let digit = remaining % 128;
    remaining = Math.floor(remaining / 128);
    if (remaining) digit |= 128;
    length.push(digit);
  } while (remaining);
  return Buffer.concat([Buffer.from([header, ...length]), body]);
}

class Broker {
  constructor(host, port, owner) {
    this.host = host;
    this.port = port;
    this.owner = owner;
    this.socket = null;
    this.ready = false;
    this.failures = 0;
    this.retryTimer = null;
    this.pingTimer = null;
  }

  start() {
    if (this.socket || this.retryTimer) return;
    const socket = net.connect(this.port, this.host);
    let buffer = Buffer.alloc(0);
    this.socket = socket;
    socket.setTimeout(30_000);
    socket.on("connect", () => {
      const clientId = "halo-web-" + crypto.randomBytes(8).toString("hex");
      // MQTT 3.1.1, clean session
      socket.write(mqttPacket(0x10, Buffer.concat([
        mqttString("MQTT"), Buffer.from([4, 0x02, 0, KEEP_ALIVE_SECONDS]), mqttString(clientId),
      ])));
    });
    socket.on("data", (data) => {
      buffer = Buffer.concat([buffer, data]);
      for (;;) {
        let length = 0, multiplier = 1, index = 1, digit;
        do {
          if (index >= buffer.length) return;
          digit = buffer[index++];
          length += (digit & 127) * multiplier;
          multiplier *= 128;
        } while (digit & 128 && index < 5);
        if (length > MAXIMUM_PACKET) {
          socket.destroy();
          return;
        }
        if (buffer.length < index + length) return;
        const header = buffer[0];
        const body = buffer.subarray(index, index + length);
        buffer = buffer.subarray(index + length);
        this.packet(header, body);
      }
    });
    socket.on("timeout", () => socket.destroy());
    socket.on("error", () => {});
    socket.on("close", () => {
      clearInterval(this.pingTimer);
      this.socket = null;
      this.ready = false;
      if (!this.owner.active) return;
      this.failures++;
      const delay = Math.min(300_000, 2000 * 2 ** Math.min(this.failures, 8));
      this.retryTimer = setTimeout(() => {
        this.retryTimer = null;
        if (this.owner.active) this.start();
      }, delay);
    });
  }

  stop() {
    clearTimeout(this.retryTimer);
    this.retryTimer = null;
    if (this.socket) {
      this.socket.end(Buffer.from([0xe0, 0]));
      this.socket.destroy();
    }
  }

  packet(header, body) {
    const type = header >> 4;
    if (type === 2) {
      if (body.length < 2 || body[1] !== 0) {
        this.socket.destroy();
        return;
      }
      this.ready = true;
      this.failures = 0;
      this.socket.write(mqttPacket(0x82, Buffer.concat([
        Buffer.from([0, 1]), mqttString(SLOT_PREFIX + "+"), Buffer.from([0]),
      ])));
      this.query();
      this.pingTimer = setInterval(() => this.socket?.write(Buffer.from([0xc0, 0])), (KEEP_ALIVE_SECONDS / 2) * 1000);
    } else if (type === 3 && body.length >= 2) {
      const qos = (header >> 1) & 3;
      const topicLength = body.readUInt16BE(0);
      const topic = body.subarray(2, 2 + topicLength).toString("latin1");
      const payload = body.subarray(2 + topicLength + (qos ? 2 : 0));
      const match = SLOT_PATTERN.exec(topic);
      if (match) this.owner.heard(match[1], payload, (header & 1) === 1);
    }
  }

  query() {
    if (this.ready) this.socket.write(mqttPacket(0x30, Buffer.concat([mqttString(QUERY_TOPIC), crypto.randomBytes(8)])));
  }
}

export class PublicGames {
  constructor(brokers, log) {
    this.log = log;
    this.active = false;
    this.games = new Map();
    this.lastRequest = 0;
    this.lastQuery = 0;
    this.idleTimer = null;
    this.brokers = brokers.map((spec) => {
      const [host, port] = String(spec).split(":");
      return new Broker(host, Number(port) || 1883, this);
    }).filter((broker) => broker.host);
  }

  heard(slot, payload, retained) {
    // (an emptied slot is no news: p2p_lobby.c ignores it too)
    if (!payload.length || payload.length > MAXIMUM_PAYLOAD) return;
    const known = this.games.get(slot);
    if (!known && this.games.size >= MAXIMUM_GAMES) return;
    // a live publish wins over a retained copy of the same slot
    if (known && retained && !known.retained && Date.now() - known.heard < GAME_EXPIRY_MS) return;
    this.games.set(slot, { payload: Buffer.from(payload), retained, heard: Date.now() });
  }

  list() {
    const now = Date.now();
    this.lastRequest = now;
    if (!this.active) {
      this.active = true;
      this.lastQuery = now;
      this.log("lobby", `server browser: asking ${this.brokers.length} broker(s) for public games`);
      for (const broker of this.brokers) broker.start();
    } else if (now - this.lastQuery >= QUERY_INTERVAL_MS) {
      this.lastQuery = now;
      for (const broker of this.brokers) broker.query();
    }
    clearTimeout(this.idleTimer);
    this.idleTimer = setTimeout(() => this.stop(), IDLE_MS);
    const games = [];
    for (const [slot, game] of this.games) {
      if (now - game.heard > GAME_EXPIRY_MS) {
        this.games.delete(slot);
        continue;
      }
      games.push({ slot, payload: game.payload.toString("base64"), retained: game.retained });
    }
    return { v: 1, games };
  }

  stop() {
    this.active = false;
    this.games.clear();
    for (const broker of this.brokers) broker.stop();
  }
}
