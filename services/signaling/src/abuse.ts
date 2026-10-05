import type { RuntimeEnv } from "./env";

const ACTOR_ID_PATTERN = /^[0-9a-f]{32}$/u;
const MAX_REASON_LENGTH = 240;

export interface BanRecord {
  actorId: string;
  createdAt: number;
  expiresAt: number | null;
  reason: string;
}

function hex(bytes: Uint8Array): string {
  return Array.from(bytes, (value) => value.toString(16).padStart(2, "0")).join("");
}

async function hmac(secret: string, value: string): Promise<Uint8Array> {
  const encoder = new TextEncoder();
  const key = await crypto.subtle.importKey(
    "raw",
    encoder.encode(secret),
    { hash: "SHA-256", name: "HMAC" },
    false,
    ["sign"],
  );
  return new Uint8Array(await crypto.subtle.sign("HMAC", key, encoder.encode(value)));
}

function requireAbuseSecret(env: RuntimeEnv): string {
  if (typeof env.ABUSE_ID_SECRET !== "string" || env.ABUSE_ID_SECRET.length < 32) {
    throw new Error("ABUSE_ID_SECRET must contain at least 32 characters.");
  }
  return env.ABUSE_ID_SECRET;
}

export function actorIdIsValid(actorId: string): boolean {
  return ACTOR_ID_PATTERN.test(actorId);
}

function clientAddress(request: Request): string {
  return request.headers.get("CF-Connecting-IP") ?? "local-development";
}

async function opaqueId(env: RuntimeEnv, value: string): Promise<string> {
  const digest = await hmac(requireAbuseSecret(env), value);
  return hex(digest.slice(0, 16));
}

/**
 * A privacy-preserving visitor identity for aggregate presence counters.
 * Neither the address nor user agent is persisted; only a domain-separated
 * HMAC reaches the Durable Object.
 */
export async function presenceIdFor(
  request: Request,
  env: RuntimeEnv,
): Promise<string> {
  const userAgent = request.headers.get("User-Agent") ?? "unknown";
  return opaqueId(
    env,
    `presence\u0000${clientAddress(request)}\u0000${userAgent}`,
  );
}

/**
 * Network-scoped abuse identity without persisting a raw IP address. The
 * client-controlled game identifier is deliberately excluded so changing it
 * cannot reset TURN accounting or bypass a ban.
 */
export async function actorIdFor(
  request: Request,
  env: RuntimeEnv,
): Promise<string> {
  return opaqueId(env, `abuse\u0000${clientAddress(request)}`);
}

/**
 * A narrower subject for the short automatic-reconnect grace window. It is
 * deliberately separate from the non-forgeable network identity used for
 * bandwidth accounting and bans.
 */
export async function verificationIdFor(
  request: Request,
  identifier: string,
  env: RuntimeEnv,
): Promise<string> {
  return opaqueId(env, `verify\u0000${clientAddress(request)}\u0000${identifier}`);
}

export async function requestIsAuthorizedAdmin(
  request: Request,
  env: RuntimeEnv,
): Promise<boolean> {
  const configured = env.ADMIN_TOKEN;
  const supplied = request.headers.get("Authorization")?.replace(/^Bearer\s+/iu, "");
  if (!configured || !supplied) {
    return false;
  }
  const [configuredDigest, suppliedDigest] = await Promise.all([
    crypto.subtle.digest("SHA-256", new TextEncoder().encode(configured)),
    crypto.subtle.digest("SHA-256", new TextEncoder().encode(supplied)),
  ]);
  return crypto.subtle.timingSafeEqual(configuredDigest, suppliedDigest);
}

export async function activeBan(
  env: RuntimeEnv,
  actorId: string,
  now = Date.now(),
): Promise<BanRecord | null> {
  const record = await env.HALO_ABUSE.get<BanRecord>(`ban:${actorId}`, "json");
  if (!record) {
    return null;
  }
  if (record.expiresAt !== null && record.expiresAt <= now) {
    await env.HALO_ABUSE.delete(`ban:${actorId}`);
    return null;
  }
  return record;
}

export async function saveBan(
  env: RuntimeEnv,
  actorId: string,
  reason: string,
  ttlSeconds?: number,
): Promise<BanRecord> {
  if (!actorIdIsValid(actorId)) {
    throw new Error("actorId must be 32 lowercase hexadecimal characters.");
  }
  const now = Date.now();
  const normalizedReason = reason.trim().slice(0, MAX_REASON_LENGTH) || "Abusive TURN usage";
  const expiresAt = ttlSeconds === undefined ? null : now + ttlSeconds * 1_000;
  const record: BanRecord = { actorId, createdAt: now, expiresAt, reason: normalizedReason };
  await env.HALO_ABUSE.put(`ban:${actorId}`, JSON.stringify(record), {
    ...(ttlSeconds === undefined ? {} : { expirationTtl: ttlSeconds }),
  });
  return record;
}

export async function rememberTurnUsernames(
  env: RuntimeEnv,
  actorId: string,
  usernames: string[],
  expiresAt: number | null,
): Promise<void> {
  if (expiresAt === null || usernames.length === 0) {
    return;
  }
  const ttl = Math.max(60, Math.ceil((expiresAt - Date.now()) / 1_000) + 60);
  await Promise.all(
    usernames.map((username) =>
      env.HALO_ABUSE.put(`turn:${actorId}:${encodeURIComponent(username)}`, username, {
        expirationTtl: ttl,
      }),
    ),
  );
}

export async function activeTurnUsernames(
  env: RuntimeEnv,
  actorId: string,
): Promise<Array<{ key: string; username: string }>> {
  const prefix = `turn:${actorId}:`;
  const results: Array<{ key: string; username: string }> = [];
  let cursor: string | undefined;
  do {
    const page = await env.HALO_ABUSE.list({ cursor, limit: 1_000, prefix });
    const values = await Promise.all(
      page.keys.map(async ({ name }) => ({ key: name, username: await env.HALO_ABUSE.get(name) })),
    );
    for (const value of values) {
      if (value.username !== null) {
        results.push({ key: value.key, username: value.username });
      }
    }
    cursor = page.list_complete ? undefined : page.cursor;
  } while (cursor !== undefined);
  return results;
}

export function recordTurnEvent(
  env: RuntimeEnv,
  event: string,
  actorId: string,
  value = 1,
): void {
  env.TURN_EVENTS.writeDataPoint({
    blobs: [event, actorId],
    doubles: [value],
    indexes: [actorId],
  });
}
