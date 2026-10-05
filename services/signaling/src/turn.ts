import { isRecord, type IceServerDescriptor } from "./protocol";

const CLOUDFLARE_STUN: IceServerDescriptor = {
  urls: ["stun:stun.cloudflare.com:3478"],
};
const TURN_API_BASE = "https://rtc.live.cloudflare.com/v1/turn/keys";
const MAX_TURN_URLS = 16;
const MAX_TURN_URL_LENGTH = 1_024;

export interface IceServerResult {
  expiresAt: number | null;
  iceServers: IceServerDescriptor[];
  turnUsernames: string[];
}

export interface TurnEnvironment {
  /* Self-hosting: a JSON array of RTCIceServer objects (for example a coturn
     server with static credentials). Used when Cloudflare TURN keys are absent. */
  ICE_SERVERS_JSON?: string;
  TURN_KEY_ID?: string;
  TURN_KEY_SECRET?: string;
  TURN_TTL_SECONDS: string;
}

function parseTurnTtl(env: TurnEnvironment): number {
  const ttl = Number(env.TURN_TTL_SECONDS);
  if (!Number.isInteger(ttl) || ttl < 60 || ttl > 7_200) {
    throw new Error("TURN_TTL_SECONDS must be an integer between 60 and 7200.");
  }
  return ttl;
}

function isBrowserCompatibleTurnUrl(value: unknown): value is string {
  if (
    typeof value !== "string" ||
    value.length < 1 ||
    value.length > MAX_TURN_URL_LENGTH ||
    (!value.startsWith("turn:") && !value.startsWith("turns:"))
  ) {
    return false;
  }

  // Browsers block the alternate DNS port. Match both ?transport= and / forms.
  return !/^turns?:[^/?#]+:53(?:[/?#]|$)/iu.test(value);
}

function parseTurnResponse(value: unknown): IceServerDescriptor[] {
  if (!isRecord(value) || !Array.isArray(value.iceServers)) {
    throw new Error("TURN service returned an invalid response envelope.");
  }

  const result: IceServerDescriptor[] = [];
  for (const item of value.iceServers) {
    if (!isRecord(item)) {
      continue;
    }
    const urls = Array.isArray(item.urls)
      ? item.urls.filter(isBrowserCompatibleTurnUrl).slice(0, MAX_TURN_URLS)
      : isBrowserCompatibleTurnUrl(item.urls)
        ? [item.urls]
        : [];
    if (
      urls.length === 0 ||
      typeof item.username !== "string" ||
      item.username.length < 1 ||
      item.username.length > 512 ||
      typeof item.credential !== "string" ||
      item.credential.length < 1 ||
      item.credential.length > 512
    ) {
      continue;
    }
    result.push({
      credential: item.credential,
      credentialType: "password",
      urls,
      username: item.username,
    });
  }

  if (result.length === 0) {
    throw new Error("TURN service returned no usable browser relay URLs.");
  }
  return result;
}

/**
 * Returns STUN unconditionally. TURN is minted once per successful room/session
 * API call when both secret-backed settings are present.
 */
export async function generateIceServers(
  env: TurnEnvironment,
  roomExpiresAt: number,
  now: number,
  fetcher: typeof fetch = fetch,
  customIdentifier?: string,
): Promise<IceServerResult> {
  const keyId = env.TURN_KEY_ID?.trim();
  const keySecret = env.TURN_KEY_SECRET?.trim();
  if (!keyId || !keySecret) {
    return { expiresAt: null, iceServers: staticIceServers(env), turnUsernames: [] };
  }

  const configuredTtl = parseTurnTtl(env);
  const roomRemainingSeconds = Math.max(
    1,
    Math.ceil((roomExpiresAt - now) / 1_000),
  );
  const ttl = Math.min(configuredTtl, roomRemainingSeconds);
  const response = await fetcher(
    `${TURN_API_BASE}/${encodeURIComponent(keyId)}/credentials/generate-ice-servers`,
    {
      body: JSON.stringify({
        ttl,
        ...(customIdentifier ? { customIdentifier } : {}),
      }),
      headers: {
        Authorization: `Bearer ${keySecret}`,
        "Content-Type": "application/json",
      },
      method: "POST",
    },
  );
  if (!response.ok) {
    throw new Error(`TURN credential generation failed with ${response.status}.`);
  }

  const turnServers = parseTurnResponse(await response.json());
  return {
    expiresAt: now + ttl * 1_000,
    iceServers: [CLOUDFLARE_STUN, ...turnServers],
    turnUsernames: [...new Set(turnServers.map((server) => server.username).filter(Boolean))] as string[],
  };
}

export async function revokeTurnCredential(
  env: TurnEnvironment,
  username: string,
  fetcher: typeof fetch = fetch,
): Promise<boolean> {
  const keyId = env.TURN_KEY_ID?.trim();
  const keySecret = env.TURN_KEY_SECRET?.trim();
  if (!keyId || !keySecret) {
    return false;
  }
  const response = await fetcher(
    `${TURN_API_BASE}/${encodeURIComponent(keyId)}/credentials/${encodeURIComponent(username)}/revoke`,
    { headers: { Authorization: `Bearer ${keySecret}` }, method: "POST" },
  );
  return response.ok;
}

export async function generateIceServersWithFallback(
  env: TurnEnvironment,
  roomExpiresAt: number,
  now: number,
  customIdentifier?: string,
): Promise<IceServerResult> {
  try {
    return await generateIceServers(env, roomExpiresAt, now, fetch, customIdentifier);
  } catch (error) {
    console.error(
      JSON.stringify({
        error: error instanceof Error ? error.message : String(error),
        message: "TURN credential generation failed; using STUN only",
      }),
    );
    return { expiresAt: null, iceServers: staticIceServers(env), turnUsernames: [] };
  }
}

function staticIceServers(env: TurnEnvironment): IceServerDescriptor[] {
  const configured = env.ICE_SERVERS_JSON?.trim();
  if (!configured) return [CLOUDFLARE_STUN];
  try {
    const parsed: unknown = JSON.parse(configured);
    if (!Array.isArray(parsed)) return [CLOUDFLARE_STUN];
    const servers: IceServerDescriptor[] = parsed.filter(isRecord).flatMap((item) => {
      const urls = (typeof item.urls === "string" ? [item.urls] : item.urls);
      if (!Array.isArray(urls) || !urls.every((url) => typeof url === "string")) return [];
      return [{
        urls: urls as string[],
        ...(typeof item.username === "string" ? { username: item.username } : {}),
        ...(typeof item.credential === "string" ? { credential: item.credential } : {}),
      }];
    });
    return servers.length > 0 ? servers.slice(0, MAX_TURN_URLS) : [CLOUDFLARE_STUN];
  } catch {
    return [CLOUDFLARE_STUN];
  }
}
