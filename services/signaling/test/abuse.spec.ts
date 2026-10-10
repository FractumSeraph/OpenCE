import { describe, expect, it } from "vitest";

import { actorIdFor, networkKey, verificationIdFor } from "../src/abuse";
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

  it("keys IPv6 addresses on their /64 and IPv4 addresses as they are", async () => {
    expect(networkKey("192.0.2.10")).toBe("192.0.2.10");
    expect(networkKey("2001:db8:1:2:aaaa::1")).toBe("2001:0db8:0001:0002::/64");
    expect(networkKey("2001:DB8:1:2:ffff:ffff:ffff:ffff")).toBe("2001:0db8:0001:0002::/64");
    expect(networkKey("2001:db8::1")).toBe("2001:0db8:0000:0000::/64");
    expect(networkKey("::1")).toBe("0000:0000:0000:0000::/64");
    expect(networkKey("fe80::1%eth0")).toBe("fe80:0000:0000:0000::/64");
    expect(networkKey("::ffff:192.0.2.10")).toBe("192.0.2.10");
    expect(networkKey("64:ff9b::192.0.2.10")).toBe("0064:ff9b:0000:0000::/64");
    expect(networkKey("not:an::address::")).toBe("not:an::address::");
    expect(networkKey("2001:db8:1:2:3:4:5:6:7")).toBe("2001:db8:1:2:3:4:5:6:7");

    expect(await actorIdFor(requestFrom("2001:db8:1:2::10"), env)).toBe(
      await actorIdFor(requestFrom("2001:db8:1:2:dead:beef:0:1"), env),
    );
    expect(await actorIdFor(requestFrom("2001:db8:1:2::10"), env)).not.toBe(
      await actorIdFor(requestFrom("2001:db8:1:3::10"), env),
    );
    expect(await actorIdFor(requestFrom("::ffff:192.0.2.10"), env)).toBe(
      await actorIdFor(requestFrom("192.0.2.10"), env),
    );
  });
});
