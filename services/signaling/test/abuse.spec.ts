import { describe, expect, it } from "vitest";

import { actorIdFor, verificationIdFor } from "../src/abuse";
import type { RuntimeEnv } from "../src/env";

const env = {
  ABUSE_ID_SECRET: "test-only-abuse-id-secret-32-bytes-minimum",
} as RuntimeEnv;

function requestFrom(address: string): Request {
  return new Request("https://api.example/", {
    headers: { "CF-Connecting-IP": address },
  });
}

describe("abuse identities", () => {
  it("cannot be reset by forging a new game identifier", async () => {
    const request = requestFrom("192.0.2.10");
    const first = await actorIdFor(request, env);
    const second = await actorIdFor(request, env);

    expect(first).toBe(second);
    expect(first).not.toBe(await actorIdFor(requestFrom("192.0.2.11"), env));
  });

  it("binds reconnect proofs to both the network and game identifier", async () => {
    const request = requestFrom("192.0.2.10");
    const first = await verificationIdFor(request, "001122334455", env);
    const second = await verificationIdFor(request, "66778899aabb", env);

    expect(first).not.toBe(second);
    expect(first).not.toBe(
      await verificationIdFor(requestFrom("192.0.2.11"), "001122334455", env),
    );
  });
});
