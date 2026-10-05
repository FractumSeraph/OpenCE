import { afterEach, describe, expect, it, vi } from "vitest";

import type { RuntimeEnv } from "../src/env";
import { requireHumanVerification } from "../src/turnstile";

afterEach(() => {
  vi.unstubAllGlobals();
});

function verificationEnv(): RuntimeEnv {
  const values = new Map<string, string>([["verified:actor:join_room", "verified"]]);
  return {
    ENVIRONMENT: "production",
    TURNSTILE_HOSTNAMES: "game.example",
    TURNSTILE_SECRET: "test-only-turnstile-secret-long-enough",
    HALO_ABUSE: {
      get: vi.fn((key: string) => Promise.resolve(values.get(key) ?? null)),
      put: vi.fn((key: string, value: string) => {
        values.set(key, value);
        return Promise.resolve();
      }),
    },
  } as unknown as RuntimeEnv;
}

describe("Turnstile verification", () => {
  it("always verifies a supplied token and rejects a replay", async () => {
    const fetchMock = vi.fn<typeof fetch>()
      .mockResolvedValueOnce(Response.json({
        action: "create_room",
        hostname: "game.example",
        success: true,
      }))
      .mockResolvedValueOnce(Response.json({
        "error-codes": ["timeout-or-duplicate"],
        success: false,
      }));
    vi.stubGlobal("fetch", fetchMock);
    const env = verificationEnv();
    const request = new Request("https://api.example/v1/rooms", {
      headers: { "CF-Connecting-IP": "192.0.2.1" },
    });

    await requireHumanVerification(request, env, "actor", "single-use-token", "create_room");
    await expect(requireHumanVerification(
      request,
      env,
      "actor",
      "single-use-token",
      "create_room",
    )).rejects.toThrow("TURNSTILE_REJECTED");

    expect(fetchMock).toHaveBeenCalledTimes(2);
  });

  it("rejects a valid token minted for the wrong action or hostname", async () => {
    vi.stubGlobal("fetch", vi.fn<typeof fetch>().mockResolvedValue(Response.json({
      action: "join_room",
      hostname: "attacker.example",
      success: true,
    })));

    await expect(requireHumanVerification(
      new Request("https://api.example/v1/rooms"),
      verificationEnv(),
      "actor",
      "valid-looking-token",
      "create_room",
    )).rejects.toThrow("TURNSTILE_REJECTED");
  });

  it("requires a fresh token for every new room", async () => {
    const fetchMock = vi.fn<typeof fetch>().mockResolvedValue(Response.json({
      action: "create_room",
      hostname: "game.example",
      success: true,
    }));
    vi.stubGlobal("fetch", fetchMock);
    const env = verificationEnv();
    const request = new Request("https://api.example/v1/rooms");

    await requireHumanVerification(request, env, "actor", "single-use-token", "create_room");
    await expect(requireHumanVerification(
      request,
      env,
      "actor",
      undefined,
      "create_room",
    )).rejects.toThrow("TURNSTILE_REJECTED");

    expect(fetchMock).toHaveBeenCalledTimes(1);
  });

  it("allows a tokenless reconnect only after a recent server-side proof", async () => {
    const fetchMock = vi.fn<typeof fetch>();
    vi.stubGlobal("fetch", fetchMock);

    await expect(requireHumanVerification(
      new Request("https://api.example/v1/rooms"),
      verificationEnv(),
      "actor",
      undefined,
      "join_room",
    )).resolves.toBeUndefined();
    expect(fetchMock).not.toHaveBeenCalled();
  });
});
