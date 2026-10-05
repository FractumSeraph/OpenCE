/*
INSTALL.JS

Installing the site as an app (a Progressive Web App): an icon on the home
screen that opens Halo full screen, sideways, without the browser's bars.

- Chrome, Edge and Samsung Internet (Android and desktop) offer their own
  install prompt; the "Install app" button shows it.
- iPhone and iPad cannot be prompted: the button opens the steps (Share,
  then Add to Home Screen). Android browsers that give no prompt get theirs.
- Once installed and opened from the home screen, the button stays hidden.

The guidance and its wording follow Halo Mobile's launcher
(https://github.com/OMG-Guest/Halo-Mobile, CC0). The manifest is
manifest.webmanifest; the service worker is coi-serviceworker.js.
*/
(function haloInstall(global) {
  "use strict";

  if (typeof document === "undefined") return;

  const standalone = navigator.standalone === true ||
    (global.matchMedia && (global.matchMedia("(display-mode: standalone)").matches ||
      global.matchMedia("(display-mode: fullscreen)").matches));
  const ios = /iPhone|iPad|iPod/.test(navigator.userAgent) ||
    (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);
  const android = /Android/i.test(navigator.userAgent);
  let installPrompt = null;
  let installed = standalone;

  global.addEventListener("beforeinstallprompt", event => {
    event.preventDefault();
    installPrompt = event;
    refresh();
  });
  global.addEventListener("appinstalled", () => {
    installed = true;
    installPrompt = null;
    refresh();
  });

  const SHEETS = {
    ios: `<h2>Add Halo to your Home Screen</h2>
      <ol class="steps">
        <li><span class="step-icon"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3v12M7.5 7.5 12 3l4.5 4.5"/><path d="M8 10H6v11h12V10h-2"/></svg></span>
          <span>Tap <strong>Share</strong> in Safari's toolbar (at the bottom, or at the top on iPad).</span></li>
        <li><span class="step-icon"><svg viewBox="0 0 24 24" aria-hidden="true"><rect x="4" y="4" width="16" height="16" rx="3"/><path d="M12 8v8M8 12h8"/></svg></span>
          <span>Scroll down and tap <strong>Add to Home Screen</strong>, then <strong>Add</strong>.</span></li>
        <li><span class="step-icon"><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="8"/><path d="M8 12l3 3 5-6"/></svg></span>
          <span>Open <strong>Halo</strong> from your Home Screen. It runs full screen.</span></li>
      </ol>
      <p class="small">In Chrome or Edge on iPhone, Share is at the top right. The app keeps its own saves, separate from the browser's.</p>`,
    android: `<h2>Install Halo</h2>
      <ol class="steps">
        <li><span class="step-icon"><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="5" r="1.6"/><circle cx="12" cy="12" r="1.6"/><circle cx="12" cy="19" r="1.6"/></svg></span>
          <span>In Chrome, tap the <strong>⋮</strong> menu at the top right.</span></li>
        <li><span class="step-icon"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 4v11M7.5 10.5 12 15l4.5-4.5"/><path d="M5 20h14"/></svg></span>
          <span>Tap <strong>Install app</strong> (or <strong>Add to Home screen</strong>), then <strong>Install</strong>.</span></li>
        <li><span class="step-icon"><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="8"/><path d="M8 12l3 3 5-6"/></svg></span>
          <span>Open <strong>Halo</strong> from your home screen or app drawer.</span></li>
      </ol>
      <p class="small">Samsung Internet: tap ☰, then <strong>Add page to</strong>, then <strong>Home screen</strong>.</p>`,
  };

  function showSheet(kind) {
    closeSheet();
    const sheet = document.createElement("div");
    sheet.className = "install-sheet";
    sheet.setAttribute("role", "dialog");
    sheet.innerHTML = SHEETS[kind] + '<button type="button" data-close>Got it</button>';
    sheet.querySelector("[data-close]").addEventListener("click", closeSheet);
    // inside the game area, so it shows over touch mode and in fullscreen
    (document.getElementById("game-area") || document.body).appendChild(sheet);
  }

  function closeSheet() {
    document.querySelectorAll(".install-sheet").forEach(sheet => sheet.remove());
  }

  function available() {
    return !installed && global.isSecureContext !== false && (installPrompt !== null || ios || android);
  }

  async function start() {
    if (installPrompt) {
      const prompt = installPrompt;
      installPrompt = null;
      prompt.prompt();
      const choice = await prompt.userChoice.catch(() => null);
      if (choice && choice.outcome === "accepted") installed = true;
      refresh();
      return;
    }
    showSheet(ios ? "ios" : "android");
  }

  function refresh() {
    const button = document.getElementById("install-app");
    if (button) button.hidden = !available();
  }

  function initialise() {
    if (standalone) document.body.classList.add("pwa-standalone");
    const controls = document.querySelector("#game-frame .game-controls");
    if (controls && !document.getElementById("install-app")) {
      const button = document.createElement("button");
      button.id = "install-app";
      button.type = "button";
      button.className = "control-button";
      button.title = "Install Halo as an app";
      button.innerHTML = '<svg aria-hidden="true" viewBox="0 0 24 24"><path d="M12 4v11M7.5 10.5 12 15l4.5-4.5"></path><path d="M5 20h14"></path></svg><span>Install app</span>';
      button.addEventListener("click", start);
      controls.appendChild(button);
    }
    refresh();
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", initialise);
  else initialise();

  global.HaloInstall = Object.freeze({ available, start, standalone: () => installed });
})(globalThis);
