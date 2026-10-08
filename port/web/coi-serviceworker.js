/* The site's service worker.
 *
 * - Cross-origin isolation on static hosts that cannot configure headers:
 *   pages and scripts get COOP/COEP added when the host did not send them
 *   (this port's own server always does).
 * - Installability: browsers offer "Install app" / "Add to Home Screen" for a
 *   site with a manifest (manifest.webmanifest) and a service worker that
 *   handles fetches. It is registered on every visit for that reason, and
 *   reloads the page only when isolation was actually missing.
 *
 * Map streaming, the lobby service and the native gateway (…/assets/maps/,
 * …/assets/custom_maps/, …/v1/…) and other origins are never touched, so large byte-range reads
 * and WebSockets go straight to the network. */
(function bootstrapServiceWorker(scope) {
  "use strict";

  if (typeof Window !== "undefined" && scope instanceof Window) {
    if (!scope.isSecureContext || !("serviceWorker" in navigator)) return;
    const needsIsolation = !scope.crossOriginIsolated;

    let reloading = false;
    const reloadUnderWorker = () => {
      if (!needsIsolation || reloading) return;
      reloading = true;
      scope.location.reload();
    };

    navigator.serviceWorker.addEventListener("controllerchange", reloadUnderWorker);
    navigator.serviceWorker.register(document.currentScript.src, { scope: "./" })
      .then(registration => {
        if (registration.active && !navigator.serviceWorker.controller) {
          reloadUnderWorker();
        }
      })
      .catch(error => {
        // A self-signed certificate (home network HTTPS) refuses service
        // workers; the game still runs because the server sends the headers.
        console.warn("Service worker unavailable:", error);
      });
    return;
  }

  self.addEventListener("install", () => self.skipWaiting());
  self.addEventListener("activate", event => event.waitUntil(self.clients.claim()));
  self.addEventListener("fetch", event => {
    const request = event.request;
    if (request.method !== "GET") return;
    if (request.cache === "only-if-cached" && request.mode !== "same-origin") return;
    const url = new URL(request.url);
    if (url.origin !== self.location.origin) return;
    if (/\/assets\/(custom_)?maps\/|\/v1\//.test(url.pathname)) return;

    event.respondWith(fetch(request).then(response => {
      if (response.type === "opaque" || response.headers.has("Cross-Origin-Embedder-Policy")) {
        return response;
      }
      const headers = new Headers(response.headers);
      headers.set("Cross-Origin-Opener-Policy", "same-origin");
      headers.set("Cross-Origin-Embedder-Policy", "require-corp");
      headers.set("Cross-Origin-Resource-Policy", "same-origin");
      headers.set("X-Content-Type-Options", "nosniff");
      return new Response(response.body, {
        status: response.status,
        statusText: response.statusText,
        headers,
      });
    }).catch(() => (request.mode === "navigate" ? offlinePage() : Response.error())));
  });

  function offlinePage() {
    return new Response(
      "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>" +
      "<title>Halo</title><body style='margin:0;display:grid;place-items:center;height:100vh;" +
      "background:#0b0d12;color:#e7eef7;font:17px/1.5 system-ui,sans-serif;text-align:center'>" +
      "<div><h1 style='color:#79d1ff'>Halo server unreachable</h1>" +
      "<p>The computer hosting Halo is off or out of reach.<br>Try again once it is running.</p>" +
      "<button onclick='location.reload()' style='font:inherit;padding:8px 18px'>Try again</button></div>",
      { status: 503, headers: { "Content-Type": "text/html; charset=utf-8" } });
  }
})(globalThis);
