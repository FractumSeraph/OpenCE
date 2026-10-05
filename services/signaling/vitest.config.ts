import { cloudflareTest } from "@cloudflare/vitest-plugin";
import { defineConfig } from "vitest/config";

export default defineConfig({
  plugins: [
    cloudflareTest({
      miniflare: {
        bindings: {
          ABUSE_ID_SECRET: "test-only-abuse-id-secret-32-bytes-minimum",
          ADMIN_TOKEN: "test-only-admin-token-32-bytes-minimum",
          ENVIRONMENT: "test",
          ROOM_ID_SECRET: "test-only-room-id-secret-32-bytes-minimum",
          TURNSTILE_TEST_BYPASS: "true",
        },
      },
      wrangler: { configPath: "./wrangler.jsonc" },
    }),
  ],
});
