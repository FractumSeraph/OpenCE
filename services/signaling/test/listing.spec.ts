import { describe, expect, it } from "vitest";

import { listingDelay } from "../src/room";

describe("listing throttle", () => {
  it("lets five listings through at once, then one every 10 seconds", () => {
    const state: { listingTokens?: number; listingTokensAt?: number } = {};
    for (let index = 0; index < 5; index += 1) {
      expect(listingDelay(state, 1_000)).toBe(0);
    }
    expect(listingDelay(state, 1_000)).toBe(10_000);
    expect(listingDelay(state, 6_000)).toBe(5_000);
    expect(listingDelay(state, 11_000)).toBe(0);
    expect(listingDelay(state, 11_000)).toBe(10_000);
    // (a quiet two minutes refill the bucket, but never past five)
    for (let index = 0; index < 5; index += 1) {
      expect(listingDelay(state, 131_000)).toBe(0);
    }
    expect(listingDelay(state, 131_000)).toBeGreaterThan(0);
  });
});
