import { describe, expect, it, vi } from "vitest";

import { generateIceServers } from "../src/turn";

describe("TURN credentials", () => {
  it("mints credentials with a bounded TTL and removes browser-blocked port 53", async () => {
    const mockFetch = vi.fn<typeof fetch>(async (input, init) => {
      expect(String(input)).toBe(
        "https://rtc.live.cloudflare.com/v1/turn/keys/test-key/credentials/generate-ice-servers",
      );
      expect(init?.headers).toMatchObject({
        Authorization: "Bearer test-secret",
        "Content-Type": "application/json",
      });
    expect(JSON.parse(String(init?.body))).toEqual({ ttl: 1_800 });
      return Response.json(
        {
          iceServers: [
            { urls: ["stun:stun.cloudflare.com:3478"] },
            {
              credential: "temporary-password",
              urls: [
                "turn:turn.cloudflare.com:53?transport=udp",
                "turn:turn.cloudflare.com:3478?transport=udp",
                "turns:turn.cloudflare.com:443?transport=tcp",
              ],
              username: "temporary-user",
            },
          ],
        },
        { status: 201 },
      );
    });
    const now = 1_000_000;

    const result = await generateIceServers(
      {
        TURN_KEY_ID: "test-key",
        TURN_KEY_SECRET: "test-secret",
        TURN_TTL_SECONDS: "3600",
      },
      now + 1_800_000,
      now,
      mockFetch,
    );

    expect(mockFetch).toHaveBeenCalledOnce();
    expect(result.expiresAt).toBe(now + 1_800_000);
    expect(result.turnUsernames).toEqual(["temporary-user"]);
    expect(result.iceServers).toEqual([
      { urls: ["stun:stun.cloudflare.com:3478"] },
      {
        credential: "temporary-password",
        credentialType: "password",
        urls: [
          "turn:turn.cloudflare.com:3478?transport=udp",
          "turns:turn.cloudflare.com:443?transport=tcp",
        ],
        username: "temporary-user",
      },
    ]);
  });

  it("does not call TURN when secrets are absent", async () => {
    const mockFetch = vi.fn<typeof fetch>();
    const result = await generateIceServers(
      { TURN_TTL_SECONDS: "3600" },
      Date.now() + 3_600_000,
      Date.now(),
      mockFetch,
    );

    expect(mockFetch).not.toHaveBeenCalled();
    expect(result).toEqual({
      expiresAt: null,
      iceServers: [{ urls: ["stun:stun.cloudflare.com:3478"] }],
      turnUsernames: [],
    });
  });
});
