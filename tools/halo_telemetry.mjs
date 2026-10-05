#!/usr/bin/env node

const ACCOUNT_ID = process.env.CLOUDFLARE_ACCOUNT_ID || "bea51ed443abd5b18e9e56723daa7a79";
const API_TOKEN = process.env.CLOUDFLARE_API_TOKEN;
const SIGNALING_URL = process.env.HALO_SIGNALING_URL || "https://halo-web-signaling.otherness-bugs.workers.dev";
const ADMIN_TOKEN = process.env.HALO_ADMIN_TOKEN;

function required(value, name) {
  if (!value) throw new Error(`${name} is required.`);
  return value;
}

async function cloudflare(path, init = {}) {
  const response = await fetch(`https://api.cloudflare.com/client/v4${path}`, {
    ...init,
    headers: { Authorization: `Bearer ${required(API_TOKEN, "CLOUDFLARE_API_TOKEN")}`, ...(init.headers || {}) },
  });
  const value = await response.json();
  if (!response.ok || value.errors?.length) throw new Error(JSON.stringify(value.errors || value));
  return value;
}

async function performanceSummary(hours = 24) {
  const sql = `
    SELECT blob2 AS browser, blob3 AS platform, blob4 AS device,
      COUNT() AS summaries, SUM(double7) AS samples,
      ROUND(AVG(double1), 1) AS avg_fps,
      ROUND(MIN(double2), 1) AS worst_fps,
      ROUND(AVG(double3), 1) AS avg_p95_fps,
      ROUND(AVG(double4), 2) AS avg_cpu_ms,
      ROUND(AVG(double5), 2) AS avg_p95_cpu_ms,
      ROUND(AVG(double6) / 1048576, 1) AS avg_memory_mb,
      SUM(double10) AS audio_callbacks,
      SUM(double11) AS late_audio_callbacks,
      ROUND(MAX(double12), 1) AS maximum_audio_gap_ms
    FROM halo_web_performance
    WHERE timestamp > NOW() - INTERVAL '${Math.max(1, Math.min(720, hours))}' HOUR
    GROUP BY browser, platform, device
    ORDER BY samples DESC`;
  const response = await fetch(
    `https://api.cloudflare.com/client/v4/accounts/${ACCOUNT_ID}/analytics_engine/sql`,
    {
      body: sql,
      headers: {
        Authorization: `Bearer ${required(API_TOKEN, "CLOUDFLARE_API_TOKEN")}`,
        "Content-Type": "text/plain",
      },
      method: "POST",
    },
  );
  const value = await response.json();
  if (!response.ok) throw new Error(JSON.stringify(value));
  console.table(value.data || value);
}

async function campaignSummary(hours = 24) {
  const sql = `
    SELECT blob2 AS map, blob3 AS outcome, blob4 AS browser, blob6 AS device,
      blob7 AS connection,
      COUNT() AS loads,
      ROUND(AVG(double1) / 1000, 1) AS avg_total_s,
      ROUND(AVG(double2) / 1000, 1) AS avg_download_s,
      ROUND(AVG(double3) / 1000, 1) AS avg_prepare_s,
      ROUND(MAX(double1) / 1000, 1) AS slowest_s,
      ROUND(AVG(double6) / 1000, 1) AS avg_longest_stall_s
    FROM halo_web_campaign_loads
    WHERE timestamp > NOW() - INTERVAL '${Math.max(1, Math.min(720, hours))}' HOUR
    GROUP BY map, outcome, browser, device, connection
    ORDER BY loads DESC, avg_total_s DESC`;
  const response = await fetch(
    `https://api.cloudflare.com/client/v4/accounts/${ACCOUNT_ID}/analytics_engine/sql`,
    {
      body: sql,
      headers: {
        Authorization: `Bearer ${required(API_TOKEN, "CLOUDFLARE_API_TOKEN")}`,
        "Content-Type": "text/plain",
      },
      method: "POST",
    },
  );
  const value = await response.json();
  if (!response.ok) throw new Error(JSON.stringify(value));
  console.table(value.data || value);
}

async function runtimeSummary(hours = 24) {
  const sql = `
    SELECT blob2 AS event, blob3 AS stage, blob4 AS browser, blob6 AS device,
      blob7 AS gpu, blob8 AS role, blob9 AS connection,
      COUNT() AS events,
      ROUND(AVG(double1) / 1000, 1) AS avg_elapsed_s,
      ROUND(MAX(double1) / 1000, 1) AS max_elapsed_s,
      ROUND(AVG(double4) / 1048576, 1) AS avg_memory_mb,
      ROUND(AVG(double5), 1) AS avg_controllers,
      ROUND(AVG(double7), 1) AS avg_client_state,
      ROUND(AVG(double8), 1) AS avg_online_state
    FROM halo_web_runtime
    WHERE timestamp > NOW() - INTERVAL '${Math.max(1, Math.min(720, hours))}' HOUR
    GROUP BY event, stage, browser, device, gpu, role, connection
    ORDER BY events DESC, max_elapsed_s DESC`;
  const response = await fetch(
    `https://api.cloudflare.com/client/v4/accounts/${ACCOUNT_ID}/analytics_engine/sql`,
    {
      body: sql,
      headers: {
        Authorization: `Bearer ${required(API_TOKEN, "CLOUDFLARE_API_TOKEN")}`,
        "Content-Type": "text/plain",
      },
      method: "POST",
    },
  );
  const value = await response.json();
  if (!response.ok) throw new Error(JSON.stringify(value));
  console.table(value.data || value);
}

async function crashSummary(hours = 24) {
  const sql = `
    SELECT blob1 AS build, blob2 AS event, blob3 AS stage, blob4 AS browser,
      blob6 AS device, blob7 AS gpu, blob13 AS category, blob14 AS fingerprint,
      blob15 AS top_frame, blob16 AS visibility, blob17 AS isolation,
      blob18 AS shared_memory, blob19 AS webgl, blob20 AS gamepad_api,
      COUNT() AS events, COUNT(DISTINCT index1) AS sessions,
      ROUND(AVG(double1) / 1000, 1) AS avg_elapsed_s,
      ROUND(AVG(double4) / 1048576, 1) AS avg_memory_mb,
      ROUND(AVG(double11), 1) AS avg_cpu_threads,
      ROUND(AVG(double12), 1) AS avg_device_memory_gb
    FROM halo_web_runtime
    WHERE timestamp > NOW() - INTERVAL '${Math.max(1, Math.min(720, hours))}' HOUR
      AND blob2 IN ('runtime_error', 'runtime_abort', 'webgl_context_lost')
      AND blob13 NOT IN ('', 'none')
    GROUP BY build, event, stage, browser, device, gpu, category, fingerprint,
      top_frame, visibility, isolation, shared_memory, webgl, gamepad_api
    ORDER BY sessions DESC, events DESC`;
  const response = await fetch(
    `https://api.cloudflare.com/client/v4/accounts/${ACCOUNT_ID}/analytics_engine/sql`,
    {
      body: sql,
      headers: {
        Authorization: `Bearer ${required(API_TOKEN, "CLOUDFLARE_API_TOKEN")}`,
        "Content-Type": "text/plain",
      },
      method: "POST",
    },
  );
  const value = await response.json();
  if (!response.ok) throw new Error(JSON.stringify(value));
  console.table(value.data || value);
}

async function turnSummary(days = 7) {
  const to = new Date();
  const from = new Date(to.getTime() - Math.max(1, Math.min(90, days)) * 86_400_000);
  const query = `query TurnUsage($accountId: String!, $dateFrom: Date!, $dateTo: Date!) {
    viewer { accounts(filter: { accountTag: $accountId }) {
      callsTurnUsageAdaptiveGroups(
        filter: { date_geq: $dateFrom, date_leq: $dateTo }
        limit: 100
        orderBy: [sum_egressBytes_DESC, sum_ingressBytes_DESC]
      ) {
        dimensions { customIdentifier keyId }
        sum { egressBytes ingressBytes }
        avg { concurrentConnectionsFiveMinutes }
      }
    } }
  }`;
  const result = await cloudflare("/graphql", {
    body: JSON.stringify({
      query,
      variables: {
        accountId: ACCOUNT_ID,
        dateFrom: from.toISOString().slice(0, 10),
        dateTo: to.toISOString().slice(0, 10),
      },
    }),
    headers: { "Content-Type": "application/json" },
    method: "POST",
  });
  const rows = result.data?.viewer?.accounts?.[0]?.callsTurnUsageAdaptiveGroups || [];
  console.table(rows.map((row) => ({
    actorId: row.dimensions.customIdentifier || "untagged",
    egressGB: (Number(row.sum.egressBytes || 0) / 1e9).toFixed(3),
    ingressGB: (Number(row.sum.ingressBytes || 0) / 1e9).toFixed(3),
    avgConnections: Number(row.avg.concurrentConnectionsFiveMinutes || 0).toFixed(1),
    keyId: row.dimensions.keyId,
  })));
}

async function signaling(path, init = {}) {
  const response = await fetch(`${SIGNALING_URL}${path}`, {
    ...init,
    headers: {
      Authorization: `Bearer ${required(ADMIN_TOKEN, "HALO_ADMIN_TOKEN")}`,
      ...(init.body ? { "Content-Type": "application/json" } : {}),
    },
  });
  if (response.status === 204) return null;
  const value = await response.json();
  if (!response.ok) throw new Error(JSON.stringify(value));
  return value;
}

const [command = "help", first, second] = process.argv.slice(2);
try {
  if (command === "fps") await performanceSummary(Number(first) || 24);
  else if (command === "campaign") await campaignSummary(Number(first) || 24);
  else if (command === "runtime") await runtimeSummary(Number(first) || 24);
  else if (command === "crashes") await crashSummary(Number(first) || 24);
  else if (command === "turn") await turnSummary(Number(first) || 7);
  else if (command === "turn-live") console.dir(
    await signaling(`/v1/admin/turn?hours=${Math.max(1, Math.min(168, Number(first) || 24))}`),
    { depth: null },
  );
  else if (command === "turn-check") console.dir(
    await signaling("/v1/admin/turn/check", { method: "POST" }),
    { depth: null },
  );
  else if (command === "turn-disable") console.dir(
    await signaling("/v1/admin/turn/disable", { method: "POST" }),
    { depth: null },
  );
  else if (command === "turn-enable") {
    await signaling("/v1/admin/turn/disable", { method: "DELETE" });
    console.log("TURN credential issuance enabled.");
  }
  else if (command === "bans") console.dir(await signaling("/v1/admin/bans"), { depth: null });
  else if (command === "ban") {
    required(first, "actorId");
    console.dir(await signaling("/v1/admin/bans", {
      body: JSON.stringify({ actorId: first, reason: second || "Abusive TURN usage" }), method: "POST",
    }), { depth: null });
  } else if (command === "unban") {
    required(first, "actorId");
    await signaling(`/v1/admin/bans/${encodeURIComponent(first)}`, { method: "DELETE" });
    console.log(`Unbanned ${first}.`);
  } else {
    console.log("Usage: halo_telemetry.mjs fps [hours] | campaign [hours] | runtime [hours] | crashes [hours] | turn [days] | turn-live [hours] | turn-check | turn-disable | turn-enable | bans | ban ACTOR_ID [reason] | unban ACTOR_ID");
  }
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
}
