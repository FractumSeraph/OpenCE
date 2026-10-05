import {
  activeTurnUsernames,
  recordTurnEvent,
  saveBan,
} from "./abuse";
import type { RuntimeEnv } from "./env";
import { revokeTurnCredential } from "./turn";

interface UsageRow {
  dimensions?: { customIdentifier?: string };
  sum?: { egressBytes?: number; ingressBytes?: number };
}

function positiveInteger(value: string, name: string): number {
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed <= 0) throw new Error(`${name} must be a positive integer.`);
  return parsed;
}

async function revokeActor(env: RuntimeEnv, actorId: string): Promise<number> {
  const credentials = await activeTurnUsernames(env, actorId);
  const results = await Promise.all(credentials.map(async ({ key, username }) => {
    const revoked = await revokeTurnCredential(env, username);
    if (revoked) await env.HALO_ABUSE.delete(key);
    return revoked;
  }));
  return results.filter(Boolean).length;
}

async function usage(env: RuntimeEnv, from: string, to: string): Promise<{
  actors: UsageRow[];
  total: UsageRow[];
}> {
  if (!/^[0-9a-f]{32}$/u.test(env.CLOUDFLARE_ACCOUNT_ID)) throw new Error("Invalid account ID.");
  const query = `query {
    viewer { accounts(filter: { accountTag: "${env.CLOUDFLARE_ACCOUNT_ID}" }) {
      actors: callsTurnUsageAdaptiveGroups(
        filter: { datetimeMinute_gt: ${JSON.stringify(from)}, datetimeMinute_lt: ${JSON.stringify(to)} }
        limit: 1000
        orderBy: [sum_egressBytes_DESC]
      ) { dimensions { customIdentifier } sum { egressBytes ingressBytes } }
      total: callsTurnUsageAdaptiveGroups(
        filter: { datetimeMinute_gt: ${JSON.stringify(from)}, datetimeMinute_lt: ${JSON.stringify(to)} }
        limit: 1
      ) { sum { egressBytes ingressBytes } }
    } }
  }`;
  const response = await fetch("https://api.cloudflare.com/client/v4/graphql", {
    body: JSON.stringify({ query }),
    headers: {
      Authorization: `Bearer ${env.CLOUDFLARE_ANALYTICS_TOKEN}`,
      "Content-Type": "application/json",
    },
    method: "POST",
  });
  const result = await response.json<{
    data?: { viewer?: { accounts?: Array<{ actors?: UsageRow[]; total?: UsageRow[] }> } };
    errors?: unknown[];
  }>();
  if (!response.ok || result.errors?.length) throw new Error(`TURN analytics query failed with ${response.status}.`);
  const account = result.data?.viewer?.accounts?.[0];
  return { actors: account?.actors ?? [], total: account?.total ?? [] };
}

export async function turnUsageSummary(env: RuntimeEnv, hours: number): Promise<{
  actors: Array<{ actorId: string; egressBytes: number; ingressBytes: number }>;
  egressBytes: number;
  ingressBytes: number;
}> {
  if (!Number.isFinite(hours) || hours < 1 || hours > 168) {
    throw new Error("hours must be between 1 and 168.");
  }
  const now = new Date();
  const result = await usage(env, new Date(now.getTime() - Math.floor(hours) * 3_600_000).toISOString(), now.toISOString());
  return {
    actors: result.actors.flatMap((row) => {
      const actorId = row.dimensions?.customIdentifier;
      if (!actorId || !/^[0-9a-f]{32}$/u.test(actorId)) return [];
      return [{
        actorId,
        egressBytes: Number(row.sum?.egressBytes ?? 0),
        ingressBytes: Number(row.sum?.ingressBytes ?? 0),
      }];
    }),
    egressBytes: Number(result.total[0]?.sum?.egressBytes ?? 0),
    ingressBytes: Number(result.total[0]?.sum?.ingressBytes ?? 0),
  };
}

export async function turnIsDisabled(env: RuntimeEnv): Promise<boolean> {
  return (await env.HALO_ABUSE.get("turn:disabled")) !== null;
}

async function revokeAllActiveCredentials(env: RuntimeEnv): Promise<number> {
  let cursor: string | undefined;
  let revoked = 0;
  do {
    const page = await env.HALO_ABUSE.list({
      ...(cursor ? { cursor } : {}),
      limit: 1_000,
      prefix: "turn:",
    });
    for (const key of page.keys) {
      if (key.name === "turn:disabled") continue;
      const username = await env.HALO_ABUSE.get(key.name);
      if (username && await revokeTurnCredential(env, username)) {
        await env.HALO_ABUSE.delete(key.name);
        revoked += 1;
      }
    }
    cursor = page.list_complete ? undefined : page.cursor;
  } while (cursor);
  return revoked;
}

export async function enforceTurnBandwidthCaps(env: RuntimeEnv, now = new Date()): Promise<void> {
  if (!env.CLOUDFLARE_ANALYTICS_TOKEN) {
    console.error(JSON.stringify({ message: "TURN bandwidth caps skipped: analytics token missing" }));
    return;
  }
  const hourlyCap = positiveInteger(env.TURN_ACTOR_HOURLY_EGRESS_CAP_BYTES, "TURN_ACTOR_HOURLY_EGRESS_CAP_BYTES");
  const dailyCap = positiveInteger(env.TURN_GLOBAL_DAILY_EGRESS_CAP_BYTES, "TURN_GLOBAL_DAILY_EGRESS_CAP_BYTES");
  const to = now.toISOString();
  const hour = await usage(env, new Date(now.getTime() - 3_600_000).toISOString(), to);

  for (const row of hour.actors) {
    const actorId = row.dimensions?.customIdentifier;
    const egress = Number(row.sum?.egressBytes ?? 0);
    if (actorId && /^[0-9a-f]{32}$/u.test(actorId) && egress >= hourlyCap) {
      await saveBan(env, actorId, `Automatic TURN bandwidth cap (${egress} bytes/hour)`, 86_400);
      const revoked = await revokeActor(env, actorId);
      recordTurnEvent(env, "auto-banned", actorId, egress);
      console.warn(JSON.stringify({ actorId, egress, message: "TURN actor cap exceeded", revoked }));
    }
  }

  const dayStart = new Date(now);
  dayStart.setUTCHours(0, 0, 0, 0);
  const day = await usage(env, dayStart.toISOString(), to);
  const dailyEgress = Number(day.total[0]?.sum?.egressBytes ?? 0);
  if (dailyEgress >= dailyCap) {
    const secondsToMidnight = Math.max(60, Math.ceil((dayStart.getTime() + 86_400_000 - now.getTime()) / 1_000));
    await env.HALO_ABUSE.put("turn:disabled", String(dailyEgress), { expirationTtl: secondsToMidnight });
    const revoked = await revokeAllActiveCredentials(env);
    console.error(JSON.stringify({ dailyEgress, message: "Global TURN daily cap exceeded; relay disabled", revoked }));
  }
}
