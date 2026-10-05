import type { RuntimeEnv } from "./env";

const SITEVERIFY_URL = "https://challenges.cloudflare.com/turnstile/v0/siteverify";
const TOKEN_MAXIMUM_LENGTH = 2_048;
const VERIFICATION_TTL_SECONDS = 1_800;

interface SiteverifyResult {
  action?: string;
  hostname?: string;
  success?: boolean;
}

function expectedHostnames(env: RuntimeEnv): Set<string> {
  return new Set(
    env.TURNSTILE_HOSTNAMES.split(",").map((value) => value.trim()).filter(Boolean),
  );
}

export async function requireHumanVerification(
  request: Request,
  env: RuntimeEnv,
  verificationId: string,
  token: string | undefined,
  expectedAction: "create_room" | "join_room",
): Promise<void> {
  if (env.ENVIRONMENT !== "production" && env.TURNSTILE_TEST_BYPASS === "true") {
    return;
  }
  /* A missing token is accepted only for short-lived automatic reconnects.
     When the client does send a token, always submit it to Siteverify. This
     preserves Turnstile's single-use guarantee instead of allowing a replay
     to hide behind the reconnect proof. */
  const verificationKey = `verified:${verificationId}:${expectedAction}`;
  if (
    expectedAction === "join_room" &&
    (!token || token.length === 0) &&
    await env.HALO_ABUSE.get(verificationKey)
  ) {
    return;
  }
  const hostnames = expectedHostnames(env);
  if (
    typeof token !== "string" || token.length === 0 ||
    token.length > TOKEN_MAXIMUM_LENGTH || hostnames.size === 0 ||
    typeof env.TURNSTILE_SECRET !== "string" || env.TURNSTILE_SECRET.length < 20
  ) {
    throw new Error("TURNSTILE_REJECTED");
  }

  let result: SiteverifyResult;
  try {
    const response = await fetch(SITEVERIFY_URL, {
      body: new URLSearchParams({
        secret: env.TURNSTILE_SECRET,
        response: token,
        remoteip: request.headers.get("CF-Connecting-IP") ?? "",
        idempotency_key: crypto.randomUUID(),
      }),
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      method: "POST",
      signal: AbortSignal.timeout(10_000),
    });
    if (!response.ok) throw new Error(`siteverify ${response.status}`);
    result = await response.json<SiteverifyResult>();
  } catch (error) {
    console.warn(JSON.stringify({
      error: error instanceof Error ? error.message : String(error),
      message: "Turnstile verification unavailable",
    }));
    throw new Error("TURNSTILE_REJECTED");
  }
  if (
    result.success !== true || result.action !== expectedAction ||
    typeof result.hostname !== "string" || !hostnames.has(result.hostname)
  ) {
    throw new Error("TURNSTILE_REJECTED");
  }
  if (expectedAction === "join_room") {
    await env.HALO_ABUSE.put(verificationKey, "verified", {
      expirationTtl: VERIFICATION_TTL_SECONDS,
    });
  }
}
