import { DurableObject } from "cloudflare:workers";

const PRESENCE_LEASE_MILLISECONDS = 150_000;
const PRESENCE_KEY_PATTERN = /^[A-Za-z0-9_-]{4,128}:[A-Za-z0-9_-]{4,96}$/u;
const PLAYER_ID_PATTERN = /^[0-9a-f]{32}$/u;
const SESSION_ID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/u;
const MILLISECONDS_PER_DAY = 86_400_000;

interface PresenceCountRow extends Record<string, SqlStorageValue> {
  count: number;
}

export interface PresenceSummary {
  campaign: number;
  online: number;
  today: number;
}

/**
 * A small, anonymous lease table for the public "players online" count.
 *
 * Only SignalingRoom Durable Objects can write these leases. The public API
 * can read the aggregate, but browsers cannot claim arbitrary players. A
 * lease also expires on its own so an interrupted Worker or missed close
 * event cannot leave the counter permanently inflated.
 */
export class PlayerPresence extends DurableObject<Env> {
  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    this.ctx.storage.sql.exec(`
      CREATE TABLE IF NOT EXISTS players (
        connection_key TEXT PRIMARY KEY,
        expires_at INTEGER NOT NULL
      );
      CREATE INDEX IF NOT EXISTS players_expiry ON players(expires_at);

      CREATE TABLE IF NOT EXISTS campaign_sessions (
        session_id TEXT PRIMARY KEY,
        player_id TEXT NOT NULL,
        expires_at INTEGER NOT NULL
      );
      CREATE INDEX IF NOT EXISTS campaign_sessions_expiry
        ON campaign_sessions(expires_at);

      CREATE TABLE IF NOT EXISTS daily_players (
        day INTEGER NOT NULL,
        player_id TEXT NOT NULL,
        PRIMARY KEY(day, player_id)
      );
    `);
  }

  async connected(connectionKey: string, now: number): Promise<void> {
    if (!PRESENCE_KEY_PATTERN.test(connectionKey) || !Number.isFinite(now)) {
      throw new Error("Invalid presence lease.");
    }
    this.removeExpired(now);
    this.ctx.storage.sql.exec(
      `INSERT INTO players (connection_key, expires_at) VALUES (?, ?)
       ON CONFLICT(connection_key) DO UPDATE SET expires_at = excluded.expires_at`,
      connectionKey,
      now + PRESENCE_LEASE_MILLISECONDS,
    );
  }

  async disconnected(connectionKey: string): Promise<void> {
    if (!PRESENCE_KEY_PATTERN.test(connectionKey)) {
      return;
    }
    this.ctx.storage.sql.exec(
      "DELETE FROM players WHERE connection_key = ?",
      connectionKey,
    );
  }

  async count(now: number): Promise<number> {
    if (!Number.isFinite(now)) {
      throw new Error("Invalid presence time.");
    }
    this.removeExpired(now);
    return this.ctx.storage.sql
      .exec<PresenceCountRow>("SELECT COUNT(*) AS count FROM players")
      .one().count;
  }

  async heartbeat(
    sessionId: string,
    playerId: string,
    campaign: boolean,
    now: number,
  ): Promise<PresenceSummary> {
    if (
      !SESSION_ID_PATTERN.test(sessionId) ||
      !PLAYER_ID_PATTERN.test(playerId) ||
      typeof campaign !== "boolean" ||
      !Number.isFinite(now)
    ) {
      throw new Error("Invalid presence heartbeat.");
    }

    this.removeExpired(now);
    const day = Math.floor(now / MILLISECONDS_PER_DAY);
    this.ctx.storage.sql.exec(
      `INSERT INTO daily_players (day, player_id) VALUES (?, ?)
       ON CONFLICT(day, player_id) DO NOTHING`,
      day,
      playerId,
    );
    this.ctx.storage.sql.exec(
      "DELETE FROM daily_players WHERE day < ?",
      day,
    );

    if (campaign) {
      this.ctx.storage.sql.exec(
        `INSERT INTO campaign_sessions (session_id, player_id, expires_at)
         VALUES (?, ?, ?)
         ON CONFLICT(session_id) DO UPDATE SET
           player_id = excluded.player_id,
           expires_at = excluded.expires_at`,
        sessionId,
        playerId,
        now + PRESENCE_LEASE_MILLISECONDS,
      );
    } else {
      this.ctx.storage.sql.exec(
        "DELETE FROM campaign_sessions WHERE session_id = ?",
        sessionId,
      );
    }

    return this.summary(now);
  }

  async summary(now: number): Promise<PresenceSummary> {
    if (!Number.isFinite(now)) {
      throw new Error("Invalid presence time.");
    }
    this.removeExpired(now);
    const day = Math.floor(now / MILLISECONDS_PER_DAY);
    const online = this.ctx.storage.sql
      .exec<PresenceCountRow>("SELECT COUNT(*) AS count FROM players")
      .one().count;
    const campaign = this.ctx.storage.sql
      .exec<PresenceCountRow>(
        "SELECT COUNT(DISTINCT player_id) AS count FROM campaign_sessions",
      )
      .one().count;
    const today = this.ctx.storage.sql
      .exec<PresenceCountRow>(
        "SELECT COUNT(*) AS count FROM daily_players WHERE day = ?",
        day,
      )
      .one().count;
    return { campaign, online, today };
  }

  private removeExpired(now: number): void {
    this.ctx.storage.sql.exec(
      "DELETE FROM players WHERE expires_at <= ?",
      now,
    );
    this.ctx.storage.sql.exec(
      "DELETE FROM campaign_sessions WHERE expires_at <= ?",
      now,
    );
  }
}
