import { DurableObject } from "cloudflare:workers";

import {
  hashesMatch,
  randomPeerId,
  randomToken,
  hashToken,
} from "./crypto";
import {
  MAX_WEBSOCKET_MESSAGE_CHARACTERS,
  IDENTIFIER_PATTERN,
  PEER_ID_PATTERN,
  SIGNALING_PROTOCOL_VERSION,
  TOKEN_PATTERN,
  parseClientMessage,
  parsePlayerProfile,
  type GameListing,
  type PeerRole,
  type PlayerProfile,
} from "./protocol";
import type { PublicGame } from "./presence";

interface RoomRow extends Record<string, SqlStorageValue> {
  build_id: string;
  capacity: number;
  created_at: number;
  expires_at: number;
  guest_ticket_hash: ArrayBuffer;
  host_ticket_hash: ArrayBuffer;
  protocol_version: number;
  room_id: string;
}

interface SessionRow extends Record<string, SqlStorageValue> {
  expires_at: number;
  identifier: string;
  peer_id: string;
  role: PeerRole;
  token_hash: ArrayBuffer;
}

interface SocketAttachment {
  departed?: boolean;
  identifier: string;
  joinedAt: number;
  /** (listingDelay's bucket: tokens left, and when last counted) */
  listingTokens?: number;
  listingTokensAt?: number;
  messageCount?: number;
  messageWindowStartedAt?: number;
  peerId: string;
  /** when a ping last refreshed the global presence lease */
  presenceRefreshedAt?: number;
  profile?: PlayerProfile;
  role: PeerRole;
}

interface PendingListing {
  guestTicket: string;
  listing: GameListing | null;
}

interface PreparedSession {
  session: MintedSession;
  tokenHash: ArrayBuffer;
}

export interface CreateRoomCommand {
  buildId: string;
  capacity: number;
  identifier: string;
  now: number;
  protocolVersion: number;
  roomId: string;
  roomTtlMs: number;
  sessionTtlMs: number;
}

export type CreateRoomResult =
  | { code: "ROOM_EXISTS"; ok: false }
  | {
      expiresAt: number;
      guestTicket: string;
      hostSession: MintedSession;
      hostTicket: string;
      ok: true;
    };

export interface CreateSessionCommand {
  buildId: string;
  identifier: string;
  now: number;
  protocolVersion: number;
  sessionTtlMs: number;
  ticket: string;
}

export type CreateSessionResult =
  | {
      code:
        | "BUILD_MISMATCH"
        | "HOST_ALREADY_CONNECTED"
        | "IDENTIFIER_IN_USE"
        | "INVALID_TICKET"
        | "PROTOCOL_MISMATCH"
        | "ROOM_EXPIRED"
        | "ROOM_FULL"
        | "ROOM_NOT_FOUND";
      ok: false;
    }
  | {
      buildId: string;
      capacity: number;
      expiresAt: number;
      ok: true;
      protocolVersion: number;
      session: MintedSession;
    };

export type CloseRoomResult =
  | { code: "INVALID_TICKET" | "ROOM_NOT_FOUND"; ok: false }
  | { ok: true };

export interface MintedSession {
  expiresAt: number;
  identifier: string;
  peerId: string;
  role: PeerRole;
  token: string;
}

function isSocketAttachment(value: unknown): value is SocketAttachment {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    return false;
  }
  const record = value as Record<string, unknown>;
  if (record.profile !== undefined && !parsePlayerProfile(record.profile).ok) {
    return false;
  }
  return (
    typeof record.joinedAt === "number" &&
    typeof record.identifier === "string" &&
    IDENTIFIER_PATTERN.test(record.identifier) &&
    typeof record.peerId === "string" &&
    PEER_ID_PATTERN.test(record.peerId) &&
    (record.role === "host" || record.role === "guest")
  );
}

function safeAttachment(socket: WebSocket): SocketAttachment | null {
  const attachment: unknown = socket.deserializeAttachment();
  return isSocketAttachment(attachment) ? attachment : null;
}

function jsonMessage(value: unknown): string {
  return JSON.stringify(value);
}

const MAX_GUEST_WEBSOCKET_MESSAGES_PER_MINUTE = 240;
const MAX_HOST_WEBSOCKET_MESSAGES_PER_MINUTE = 16_384;

/* Pings and listings reach the one global presence object, which every room
shares, so each socket's are throttled: a ping refreshes the presence lease
(150 s) at most every 30 s (the page pings every 40 s), and a host's listings
go through a bucket of five, refilled one every 10 s (the page sends one when
its game changes and every 30 s). A listing over the bucket is not lost: the
latest one waits for the next token. */
const PRESENCE_REFRESH_MILLISECONDS = 30_000;
const LISTING_BURST = 5;
const LISTING_REFILL_MILLISECONDS = 10_000;

/**
 * Takes a listing token from the socket's bucket (updating it in place):
 * 0 when one was taken, otherwise how long until one will be there.
 */
export function listingDelay(
  state: { listingTokens?: number; listingTokensAt?: number },
  now: number,
): number {
  const tokens = typeof state.listingTokens === "number" ? state.listingTokens : LISTING_BURST;
  const since = typeof state.listingTokensAt === "number" ? Math.max(0, now - state.listingTokensAt) : 0;
  const available = Math.min(LISTING_BURST, tokens + since / LISTING_REFILL_MILLISECONDS);
  state.listingTokensAt = now;
  if (available >= 1) {
    state.listingTokens = available - 1;
    return 0;
  }
  state.listingTokens = available;
  return Math.max(1, Math.ceil((1 - available) * LISTING_REFILL_MILLISECONDS));
}

export class SignalingRoom extends DurableObject<Env> {
  /** a host's latest listing that waits for a token, by peer */
  private readonly pendingListings = new Map<string, PendingListing>();

  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
  }

  private initializeStorage(): void {
    this.ctx.storage.sql.exec(`
      CREATE TABLE IF NOT EXISTS room (
        singleton INTEGER PRIMARY KEY CHECK (singleton = 1),
        room_id TEXT NOT NULL,
        build_id TEXT NOT NULL,
        protocol_version INTEGER NOT NULL,
        capacity INTEGER NOT NULL,
        created_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL,
        host_ticket_hash BLOB NOT NULL,
        guest_ticket_hash BLOB NOT NULL
      );
      CREATE TABLE IF NOT EXISTS pending_sessions (
        peer_id TEXT PRIMARY KEY,
        role TEXT NOT NULL CHECK (role IN ('host', 'guest')),
        identifier TEXT NOT NULL,
        token_hash BLOB NOT NULL,
        expires_at INTEGER NOT NULL
      );
      CREATE INDEX IF NOT EXISTS pending_sessions_expiry
        ON pending_sessions(expires_at);
    `);
  }

  async createRoom(command: CreateRoomCommand): Promise<CreateRoomResult> {
    const hostTicket = randomToken();
    const guestTicket = randomToken();
    const hostSessionToken = randomToken();
    const [hostTicketHash, guestTicketHash, hostSessionTokenHash] = await Promise.all([
      hashToken(hostTicket),
      hashToken(guestTicket),
      hashToken(hostSessionToken),
    ]);

    // All operations below are synchronous until the reservation is committed,
    // so concurrent create calls cannot both observe an empty room.
    this.initializeStorage();
    if (this.getRoom() !== null) {
      return { code: "ROOM_EXISTS", ok: false };
    }

    const expiresAt = command.now + command.roomTtlMs;
    const hostSession = this.prepareSession(
      "host",
      command.identifier,
      command.now,
      command.sessionTtlMs,
      hostSessionToken,
      hostSessionTokenHash,
    );

    this.ctx.storage.transactionSync(() => {
      this.ctx.storage.sql.exec(
        `INSERT INTO room (
          singleton, room_id, build_id, protocol_version, capacity,
          created_at, expires_at, host_ticket_hash, guest_ticket_hash
        ) VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?)`,
        command.roomId,
        command.buildId,
        command.protocolVersion,
        command.capacity,
        command.now,
        expiresAt,
        hostTicketHash,
        guestTicketHash,
      );
      this.insertSession(hostSession);
    });

    await this.ctx.storage.setAlarm(expiresAt);

    return {
      expiresAt,
      guestTicket,
      hostSession: hostSession.session,
      hostTicket,
      ok: true,
    };
  }

  async createSession(
    command: CreateSessionCommand,
  ): Promise<CreateSessionResult> {
    const newSessionToken = randomToken();
    const [providedTicketHash, newSessionTokenHash] = await Promise.all([
      hashToken(command.ticket),
      hashToken(newSessionToken),
    ]);

    // From this point through insertSession(), no operation yields. This makes
    // the capacity check and reservation atomic under a Durable Object's input
    // gate even when many friends click the invite simultaneously.
    const room = this.getRoom();
    if (room === null) {
      await this.expireRoom();
      return { code: "ROOM_NOT_FOUND", ok: false };
    }

    const isHost = hashesMatch(providedTicketHash, room.host_ticket_hash);
    const isGuest = hashesMatch(providedTicketHash, room.guest_ticket_hash);
    if (!isHost && !isGuest) {
      return { code: "INVALID_TICKET", ok: false };
    }
    if (command.now >= room.expires_at) {
      await this.expireRoom();
      return { code: "ROOM_EXPIRED", ok: false };
    }
    if (command.protocolVersion !== room.protocol_version) {
      return { code: "PROTOCOL_MISMATCH", ok: false };
    }
    if (command.buildId !== room.build_id) {
      return { code: "BUILD_MISMATCH", ok: false };
    }

    this.removeExpiredSessions(command.now);
    const role: PeerRole = isHost ? "host" : "guest";
    const activeConnections = this.connections();
    const pendingSessions = this.pendingSessionCount(command.now);

    if (
      role === "host" &&
      (activeConnections.some(({ attachment }) => attachment.role === "host") ||
        this.pendingRoleCount("host", command.now) > 0)
    ) {
      return { code: "HOST_ALREADY_CONNECTED", ok: false };
    }
    if (
      activeConnections.some(
        ({ attachment }) => attachment.identifier === command.identifier,
      ) ||
      this.pendingIdentifierCount(command.identifier, command.now) > 0
    ) {
      return { code: "IDENTIFIER_IN_USE", ok: false };
    }
    if (activeConnections.length + pendingSessions >= room.capacity) {
      return { code: "ROOM_FULL", ok: false };
    }

    const session = this.prepareSession(
      role,
      command.identifier,
      command.now,
      command.sessionTtlMs,
      newSessionToken,
      newSessionTokenHash,
    );
    this.insertSession(session);

    return {
      buildId: room.build_id,
      capacity: room.capacity,
      expiresAt: room.expires_at,
      ok: true,
      protocolVersion: room.protocol_version,
      session: session.session,
    };
  }

  async closeRoom(ticket: string): Promise<CloseRoomResult> {
    const room = this.getRoom();
    if (room === null) {
      await this.expireRoom();
      return { code: "ROOM_NOT_FOUND", ok: false };
    }
    if (!hashesMatch(await hashToken(ticket), room.host_ticket_hash)) {
      return { code: "INVALID_TICKET", ok: false };
    }
    await this.expireRoom();
    return { ok: true };
  }

  override async fetch(request: Request): Promise<Response> {
    if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket") {
      return new Response("Expected a WebSocket upgrade.", { status: 426 });
    }

    const room = this.getRoom();
    if (room === null) {
      await this.expireRoom();
      return new Response("Room not found.", { status: 404 });
    }
    const now = Date.now();
    if (now >= room.expires_at) {
      await this.expireRoom();
      return new Response("Room expired.", { status: 410 });
    }

    const url = new URL(request.url);
    const peerId = url.searchParams.get("peer");
    const token = url.searchParams.get("token");
    if (
      peerId === null ||
      !PEER_ID_PATTERN.test(peerId) ||
      token === null ||
      !TOKEN_PATTERN.test(token)
    ) {
      return new Response("Malformed session.", { status: 400 });
    }

    const providedTokenHash = await hashToken(token);

    // Re-read after hashing so two upgrades cannot both consume one session.
    this.removeExpiredSessions(now);
    const session = this.getSession(peerId);
    if (
      session === null ||
      session.expires_at <= now ||
      !hashesMatch(providedTokenHash, session.token_hash)
    ) {
      return new Response("Session is invalid or expired.", { status: 401 });
    }

    const existingConnections = this.connections();
    if (
      existingConnections.some(
        ({ attachment }) => attachment.peerId === session.peer_id,
      )
    ) {
      return new Response("Peer is already connected.", { status: 409 });
    }
    if (
      session.role === "host" &&
      existingConnections.some(({ attachment }) => attachment.role === "host")
    ) {
      return new Response("Host is already connected.", { status: 409 });
    }
    if (existingConnections.length >= room.capacity) {
      return new Response("Room is full.", { status: 409 });
    }

    this.ctx.storage.sql.exec(
      "DELETE FROM pending_sessions WHERE peer_id = ?",
      peerId,
    );

    const pair = new WebSocketPair();
    const client = pair[0];
    const server = pair[1];
    const attachment: SocketAttachment = {
      identifier: session.identifier,
      joinedAt: now,
      messageCount: 0,
      messageWindowStartedAt: now,
      peerId: session.peer_id,
      presenceRefreshedAt: now,
      role: session.role,
    };

    this.ctx.acceptWebSocket(server, [
      `peer:${attachment.peerId}`,
      `role:${attachment.role}`,
    ]);
    server.serializeAttachment(attachment);
    this.updatePresence(room.room_id, attachment.peerId, true);
    server.send(
      jsonMessage({
        peers: existingConnections
          .filter(({ attachment: peer }) => peer.role !== attachment.role)
          .map(({ attachment: peer }) => ({
            identifier: peer.identifier,
            peerId: peer.peerId,
            role: peer.role,
          })),
        room: {
          buildId: room.build_id,
          capacity: room.capacity,
          expiresAt: room.expires_at,
          id: room.room_id,
          protocolVersion: room.protocol_version,
        },
        self: {
          identifier: attachment.identifier,
          peerId: attachment.peerId,
          role: attachment.role,
        },
        type: "welcome",
        v: SIGNALING_PROTOCOL_VERSION,
      }),
    );

    this.broadcastToRole(
      {
        peer: {
          identifier: attachment.identifier,
          peerId: attachment.peerId,
          role: attachment.role,
        },
        type: "peer-joined",
        v: SIGNALING_PROTOCOL_VERSION,
      },
      attachment.role === "host" ? "guest" : "host",
      server,
    );
    this.broadcastRoster();

    return new Response(null, { status: 101, webSocket: client });
  }

  override webSocketMessage(
    socket: WebSocket,
    rawMessage: string | ArrayBuffer,
  ): void {
    const sender = safeAttachment(socket);
    if (sender === null || sender.departed === true) {
      socket.close(1008, "Missing connection state.");
      return;
    }

    const now = Date.now();
    if (
      typeof sender.messageWindowStartedAt !== "number" ||
      now - sender.messageWindowStartedAt >= 60_000
    ) {
      sender.messageWindowStartedAt = now;
      sender.messageCount = 0;
    }
    sender.messageCount =
      (typeof sender.messageCount === "number" ? sender.messageCount : 0) + 1;
    try {
      socket.serializeAttachment(sender);
    } catch (error) {
      console.error(
        JSON.stringify({
          error: error instanceof Error ? error.message : String(error),
          message: "failed to persist room WebSocket rate state",
          peerId: sender.peerId,
        }),
      );
      this.retireSocket(socket, 1011, "Connection state failed.");
      return;
    }
    const messageLimit =
      sender.role === "host"
        ? MAX_HOST_WEBSOCKET_MESSAGES_PER_MINUTE
        : MAX_GUEST_WEBSOCKET_MESSAGES_PER_MINUTE;
    if (sender.messageCount > messageLimit) {
      this.retireSocket(socket, 1008, "Signaling rate exceeded.");
      return;
    }

    if (
      typeof rawMessage !== "string" ||
      rawMessage.length > MAX_WEBSOCKET_MESSAGE_CHARACTERS
    ) {
      this.retireSocket(socket, 1009, "Signaling message is too large.");
      return;
    }

    let decoded: unknown;
    try {
      decoded = JSON.parse(rawMessage);
    } catch {
      this.sendError(socket, "INVALID_MESSAGE", "Message must be valid JSON.");
      return;
    }

    const parsed = parseClientMessage(decoded);
    if (!parsed.ok) {
      this.sendError(socket, "INVALID_MESSAGE", parsed.message);
      return;
    }

    const message = parsed.value;
    if (message.type === "ping") {
      const room = this.getRoom();
      if (
        room !== null &&
        !(typeof sender.presenceRefreshedAt === "number" &&
          now - sender.presenceRefreshedAt >= 0 &&
          now - sender.presenceRefreshedAt < PRESENCE_REFRESH_MILLISECONDS)
      ) {
        sender.presenceRefreshedAt = now;
        if (!this.persistAttachment(socket, sender)) {
          return;
        }
        this.updatePresence(room.room_id, sender.peerId, true);
      }
      try {
        socket.send(
          jsonMessage({
            ...(message.nonce === undefined
              ? {}
              : { nonce: message.nonce }),
            type: "pong",
            v: SIGNALING_PROTOCOL_VERSION,
          }),
        );
      } catch (error) {
        console.error(
          JSON.stringify({
            error: error instanceof Error ? error.message : String(error),
            message: "failed to send room WebSocket pong",
            peerId: sender.peerId,
          }),
        );
        this.retireSocket(socket, 1011, "Signaling delivery failed.");
      }
      return;
    }

    if (message.type === "listing") {
      if (sender.role !== "host") {
        this.sendError(socket, "LISTING_FORBIDDEN", "Only the host lists its game.");
        return;
      }
      const pending = this.pendingListings.get(sender.peerId);
      if (pending !== undefined) {
        // (already waiting for a token: the latest listing goes then)
        pending.guestTicket = message.guestTicket;
        pending.listing = message.listing;
        return;
      }
      this.pendingListings.set(sender.peerId, {
        guestTicket: message.guestTicket,
        listing: message.listing,
      });
      this.flushListing(socket, sender.peerId);
      return;
    }

    if (message.type === "profile") {
      sender.profile = message.profile;
      try {
        socket.serializeAttachment(sender);
      } catch (error) {
        console.error(
          JSON.stringify({
            error: error instanceof Error ? error.message : String(error),
            message: "failed to persist player profile",
            peerId: sender.peerId,
          }),
        );
        this.retireSocket(socket, 1011, "Connection state failed.");
        return;
      }
      this.broadcastRoster();
      return;
    }

    const target = this.connectionForPeer(message.to);
    if (target === undefined) {
      this.sendError(socket, "PEER_NOT_FOUND", "Target peer is not connected.");
      return;
    }
    if (
      sender.role === target.attachment.role ||
      (sender.role === "guest" && target.attachment.role !== "host")
    ) {
      this.sendError(
        socket,
        "SIGNAL_ROUTE_FORBIDDEN",
        "Signals must travel between the host and a guest.",
      );
      return;
    }
    if (
      "description" in message.signal &&
      ((sender.role === "host" && message.signal.description.type !== "offer") ||
        (sender.role === "guest" && message.signal.description.type !== "answer"))
    ) {
      this.sendError(
        socket,
        "SIGNAL_DIRECTION_INVALID",
        "The session description direction is invalid for this peer.",
      );
      return;
    }

    try {
      target.socket.send(
        jsonMessage({
          from: sender.peerId,
          signal: message.signal,
          type: "signal",
          v: SIGNALING_PROTOCOL_VERSION,
        }),
      );
    } catch (error) {
      console.error(
        JSON.stringify({
          error: error instanceof Error ? error.message : String(error),
          message: "failed to relay room WebSocket message",
          peerId: target.attachment.peerId,
        }),
      );
      this.retireSocket(target.socket, 1011, "Signaling delivery failed.");
      this.sendError(socket, "PEER_NOT_FOUND", "Target peer is not connected.");
    }
  }

  override webSocketClose(socket: WebSocket): void {
    this.announceDeparture(socket);
  }

  override webSocketError(socket: WebSocket, error: unknown): void {
    const attachment = safeAttachment(socket);
    console.error(
      JSON.stringify({
        error: error instanceof Error ? error.message : String(error),
        message: "room WebSocket error",
        peerId: attachment?.peerId ?? "unknown",
      }),
    );
    this.retireSocket(socket, 1011, "WebSocket error.");
  }

  override async alarm(): Promise<void> {
    await this.expireRoom();
  }

  private announceDeparture(socket: WebSocket): void {
    const attachment = safeAttachment(socket);
    if (attachment === null || attachment.departed === true) {
      return;
    }
    attachment.departed = true;
    const room = this.getRoom();
    if (room !== null) {
      this.updatePresence(room.room_id, attachment.peerId, false);
      if (attachment.role === "host") {
        this.unlistGame();
      }
    }
    try {
      socket.serializeAttachment(attachment);
    } catch {
      // The socket may already be fully closed; readyState filtering still
      // prevents it from participating in room membership.
    }
    this.broadcastToRole(
      {
        identifier: attachment.identifier,
        peerId: attachment.peerId,
        reason: attachment.role === "host" ? "host-disconnected" : "disconnected",
        type: "peer-left",
        v: SIGNALING_PROTOCOL_VERSION,
      },
      attachment.role === "host" ? "guest" : "host",
      socket,
    );
    this.broadcastRoster();
  }

  private broadcastRoster(): void {
    const connections = this.connections();
    const encoded = jsonMessage({
      players: connections.map(({ attachment }) => ({
        peerId: attachment.peerId,
        profile: attachment.profile ?? null,
        role: attachment.role,
      })),
      type: "roster",
      v: SIGNALING_PROTOCOL_VERSION,
    });
    for (const { socket } of connections) {
      try {
        socket.send(encoded);
      } catch (error) {
        console.error(
          JSON.stringify({
            error: error instanceof Error ? error.message : String(error),
            message: "failed to send room roster",
          }),
        );
        this.retireSocket(socket, 1011, "Signaling delivery failed.");
      }
    }
  }

  private broadcastToRole(
    message: unknown,
    role: PeerRole,
    excluded?: WebSocket,
  ): void {
    const encoded = jsonMessage(message);
    for (const { socket } of this.connections(role)) {
      if (socket === excluded) {
        continue;
      }
      try {
        socket.send(encoded);
      } catch (error) {
        console.error(
          JSON.stringify({
            error: error instanceof Error ? error.message : String(error),
            message: "failed to send room WebSocket message",
          }),
        );
        this.retireSocket(socket, 1011, "Signaling delivery failed.");
      }
    }
  }

  private retireSocket(socket: WebSocket, code: number, reason: string): void {
    this.announceDeparture(socket);
    try {
      socket.close(code, reason);
    } catch {
      // An errored socket can already be closed by the runtime.
    }
  }

  private connections(): Array<{
    attachment: SocketAttachment;
    socket: WebSocket;
  }>;
  private connections(role: PeerRole): Array<{
    attachment: SocketAttachment;
    socket: WebSocket;
  }>;
  private connections(role?: PeerRole): Array<{
    attachment: SocketAttachment;
    socket: WebSocket;
  }> {
    const result: Array<{
      attachment: SocketAttachment;
      socket: WebSocket;
    }> = [];
    for (const socket of this.ctx.getWebSockets()) {
      if (socket.readyState !== WebSocket.OPEN) {
        continue;
      }
      const attachment = safeAttachment(socket);
      if (
        attachment !== null &&
        attachment.departed !== true &&
        (role === undefined || attachment.role === role)
      ) {
        result.push({ attachment, socket });
      }
    }
    return result;
  }

  private connectionForPeer(peerId: string):
    | { attachment: SocketAttachment; socket: WebSocket }
    | undefined {
    for (const socket of this.ctx.getWebSockets(`peer:${peerId}`)) {
      if (socket.readyState !== WebSocket.OPEN) {
        continue;
      }
      const attachment = safeAttachment(socket);
      if (
        attachment !== null &&
        attachment.departed !== true &&
        attachment.peerId === peerId
      ) {
        return { attachment, socket };
      }
    }
    return undefined;
  }

  /**
   * The host's public game (GET /v1/public-rooms), with the room's invite
   * code, which the host proves by sending the room's guest ticket; or
   * (null) no longer listed.
   */
  /** sends the peer's pending listing if its bucket has a token, otherwise
   * once it will have */
  private flushListing(socket: WebSocket, peerId: string): void {
    const pending = this.pendingListings.get(peerId);
    if (pending === undefined) {
      return;
    }
    const attachment = safeAttachment(socket);
    if (attachment === null || attachment.departed === true || attachment.peerId !== peerId) {
      this.pendingListings.delete(peerId);
      return;
    }
    const delay = listingDelay(attachment, Date.now());
    if (!this.persistAttachment(socket, attachment)) {
      this.pendingListings.delete(peerId);
      return;
    }
    if (delay > 0) {
      setTimeout(() => this.flushListing(socket, peerId), delay);
      return;
    }
    this.pendingListings.delete(peerId);
    void this.listGame(pending.guestTicket, pending.listing).catch((error) => {
      console.error(
        JSON.stringify({
          error: error instanceof Error ? error.message : String(error),
          message: "failed to list the room's game",
        }),
      );
    });
  }

  /** (false: the state could not be kept, and the socket is retired) */
  private persistAttachment(socket: WebSocket, attachment: SocketAttachment): boolean {
    try {
      socket.serializeAttachment(attachment);
      return true;
    } catch (error) {
      console.error(
        JSON.stringify({
          error: error instanceof Error ? error.message : String(error),
          message: "failed to persist room WebSocket state",
          peerId: attachment.peerId,
        }),
      );
      this.retireSocket(socket, 1011, "Connection state failed.");
      return false;
    }
  }

  private async listGame(guestTicket: string, listing: GameListing | null): Promise<void> {
    const room = this.getRoom();
    if (room === null) {
      return;
    }
    const presence = this.env.PRESENCE.getByName("global");
    if (listing === null) {
      await presence.listGame(room.room_id, null, Date.now());
      return;
    }
    if (!hashesMatch(await hashToken(guestTicket), room.guest_ticket_hash)) {
      return;
    }
    const game: PublicGame = {
      ...listing,
      buildId: room.build_id,
      code: `${room.room_id}.${guestTicket}`,
      roomId: room.room_id,
    };
    await presence.listGame(room.room_id, game, Date.now());
  }

  /** (the room's game, no longer public: its host left, or the room ended) */
  private unlistGame(): void {
    const room = this.getRoom();
    if (room === null) {
      return;
    }
    void this.env.PRESENCE.getByName("global").listGame(room.room_id, null, Date.now()).catch(() => {
      // The listing lapses on its own (PUBLIC_GAME_LEASE_MILLISECONDS).
    });
  }

  private async expireRoom(): Promise<void> {
    const room = this.getRoom();
    if (room !== null) {
      for (const { attachment } of this.connections()) {
        this.updatePresence(room.room_id, attachment.peerId, false);
      }
      this.unlistGame();
    }
    for (const socket of this.ctx.getWebSockets()) {
      try {
        socket.close(4001, "Room expired.");
      } catch {
        // The socket may already have closed between enumeration and close().
      }
    }
    await this.ctx.storage.deleteAll();
  }

  private updatePresence(
    roomId: string,
    peerId: string,
    connected: boolean,
  ): void {
    const connectionKey = `${roomId}:${peerId}`;
    const presence = this.env.PRESENCE.getByName("global");
    const operation = connected
      ? presence.connected(connectionKey, Date.now())
      : presence.disconnected(connectionKey);
    this.ctx.waitUntil(operation.catch((error: unknown) => {
      console.error(
        JSON.stringify({
          error: error instanceof Error ? error.message : String(error),
          message: "failed to update global player presence",
        }),
      );
    }));
  }

  private getRoom(): RoomRow | null {
    try {
      return (
        this.ctx.storage.sql
          .exec<RoomRow>(
            `SELECT room_id, build_id, protocol_version, capacity, created_at,
                    expires_at, host_ticket_hash, guest_ticket_hash
               FROM room WHERE singleton = 1`,
          )
          .toArray()[0] ?? null
      );
    } catch (error) {
      if (error instanceof Error && error.message.includes("no such table")) {
        return null;
      }
      throw error;
    }
  }

  private getSession(peerId: string): SessionRow | null {
    return (
      this.ctx.storage.sql
        .exec<SessionRow>(
          `SELECT peer_id, role, identifier, token_hash, expires_at
             FROM pending_sessions WHERE peer_id = ?`,
          peerId,
        )
        .toArray()[0] ?? null
    );
  }

  private insertSession(prepared: PreparedSession): void {
    const { session, tokenHash } = prepared;
    this.ctx.storage.sql.exec(
      `INSERT INTO pending_sessions
         (peer_id, role, identifier, token_hash, expires_at)
       VALUES (?, ?, ?, ?, ?)`,
      session.peerId,
      session.role,
      session.identifier,
      tokenHash,
      session.expiresAt,
    );
  }

  private prepareSession(
    role: PeerRole,
    identifier: string,
    now: number,
    ttlMs: number,
    token: string,
    tokenHash: ArrayBuffer,
  ): PreparedSession {
    return {
      session: {
        expiresAt: now + ttlMs,
        identifier,
        peerId: randomPeerId(role),
        role,
        token,
      },
      tokenHash,
    };
  }

  private pendingRoleCount(role: PeerRole, now: number): number {
    return this.ctx.storage.sql
      .exec<{ count: number }>(
        `SELECT COUNT(*) AS count FROM pending_sessions
          WHERE role = ? AND expires_at > ?`,
        role,
        now,
      )
      .one().count;
  }

  private pendingIdentifierCount(identifier: string, now: number): number {
    return this.ctx.storage.sql
      .exec<{ count: number }>(
        `SELECT COUNT(*) AS count FROM pending_sessions
          WHERE identifier = ? AND expires_at > ?`,
        identifier,
        now,
      )
      .one().count;
  }

  private pendingSessionCount(now: number): number {
    return this.ctx.storage.sql
      .exec<{ count: number }>(
        "SELECT COUNT(*) AS count FROM pending_sessions WHERE expires_at > ?",
        now,
      )
      .one().count;
  }

  private removeExpiredSessions(now: number): void {
    this.ctx.storage.sql.exec(
      "DELETE FROM pending_sessions WHERE expires_at <= ?",
      now,
    );
  }

  private sendError(socket: WebSocket, code: string, message: string): void {
    try {
      socket.send(
        jsonMessage({
          code,
          message,
          type: "error",
          v: SIGNALING_PROTOCOL_VERSION,
        }),
      );
    } catch (error) {
      console.error(
        JSON.stringify({
          error: error instanceof Error ? error.message : String(error),
          message: "failed to send room WebSocket error",
        }),
      );
      this.retireSocket(socket, 1011, "Signaling delivery failed.");
    }
  }
}
