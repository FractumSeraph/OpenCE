/* Private-room signalling and the browser "Play online" experience.

   Gameplay never passes through the room service.  It only exchanges room
   membership and WebRTC descriptions/candidates, then Halo's normal system-
   link packets travel through HaloWebTransport's DataChannels. */

;(function installHaloOnline(global) {
  "use strict";

  if (!global || global.HaloOnline) return;

  var PROTOCOL_VERSION = 1;
  var ROOM_CAPACITY = 128;
  var MAX_PENDING_SIGNALING_MESSAGES = ROOM_CAPACITY * 128;
  var HEARTBEAT_MILLISECONDS = 40000;
  var PRESENCE_POLL_MILLISECONDS = 30000;
  var GAME_POLL_MILLISECONDS = 200;
  /* the in-game server browser (src/web_public_games.c) */
  var PUBLIC_GAMES_TICK_MILLISECONDS = 500;
  var PUBLIC_GAMES_FETCH_MILLISECONDS = 5000;
  var PUBLIC_GAMES_RETRY_MILLISECONDS = 30000;
  var TURNSTILE_RENDER_ATTEMPTS = 80;
  var HOST_SETTINGS_STORAGE_KEY = "halo.web.host-settings.v1";
  var PLAYER_PROFILE_STORAGE_KEY = "halo.web.player-profile.v1";
  var PLAYER_NAME_MAXIMUM_LENGTH = 11;
  var LAST_MAP_INDEX = 12;
  var LAST_MODE_INDEX = 5;
  var ADVANCED_MODE_DEFAULTS = Object.freeze([
    { scoreToWin: 15, respawnSeconds: 0 },
    { scoreToWin: 50, respawnSeconds: 10 },
    { scoreToWin: 3, respawnSeconds: 10 },
    { scoreToWin: 2, respawnSeconds: 5 },
    { scoreToWin: 2, respawnSeconds: 5 },
    { scoreToWin: 3, respawnSeconds: 0 },
  ]);
  var PLAYER_STYLES = Object.freeze([
    "white", "black", "red", "blue", "sage", "yellow", "lime", "pink", "purple",
    "cyan", "cornflower", "orange", "teal", "forest", "brown", "tan", "maroon", "rose",
  ]);
  var PLAYER_STYLE_COLORS = Object.freeze({
    white: 0,
    black: 1,
    red: 2,
    blue: 3,
    sage: 4,
    yellow: 5,
    lime: 6,
    pink: 7,
    purple: 8,
    cyan: 9,
    cornflower: 10,
    orange: 11,
    teal: 12,
    forest: 13,
    brown: 14,
    tan: 15,
    maroon: 16,
    rose: 17,
  });

  var COMMAND = Object.freeze({ HOST: 1, JOIN: 2, CANCEL: 3 });
  var GAME_STATE = Object.freeze({
    IDLE: 0,
    WAITING: 1,
    HOST_STARTING: 2,
    HOSTING: 3,
    JOIN_SEARCHING: 4,
    JOIN_CONNECTING: 5,
    JOINED: 6,
    ERROR: 7,
  });
  var TRANSPORT_STATE = Object.freeze({
    DISCONNECTED: 0,
    CONNECTING: 1,
    CONNECTED: 2,
    FAILED: 3,
  });
  var GAME_ERRORS = Object.freeze({
    1: "Halo could not open the host lobby.",
    2: "Halo could not start its multiplayer client.",
    3: "Halo could not open the pregame lobby.",
    4: "The host rejected or ended the join.",
    5: "The host lobby did not answer within 90 seconds.",
  });

  var elements = {};
  var humanVerification = {
    action: null,
    busy: false,
    generation: 0,
    renderAttempts: 0,
    renderTimer: 0,
    state: "idle",
    token: null,
    widgetId: null,
  };
  var session = {
    runtimeReady: false,
    active: false,
    closing: false,
    role: null,
    room: null,
    roomTicket: null,
    inviteCode: null,
    inviteUrl: null,
    selfPeerId: null,
    iceServers: [],
    socket: null,
    socketGeneration: 0,
    operationGeneration: 0,
    heartbeatTimer: 0,
    reconnectTimer: 0,
    reconnectAttempts: 0,
    gamePollTimer: 0,
    gameCommandIssued: false,
    transportConnected: false,
    connectedPeerCount: 0,
    connectionPath: null,
    peerPromises: new Map(),
    peerIdentifiers: new Map(),
    peerStates: new Map(),
    peerAliases: new Map(),
    peerSignalTargets: new Map(),
    roster: new Map(),
    messageChain: Promise.resolve(),
    pendingInvite: null,
    profile: null,
    hostWasReady: false,
    hostSettings: null,
    guestWasJoined: false,
    leavePromise: null,
    joinRequested: false,
    wizardStep: "map",
    presenceTimer: 0,
  };

  function byId(id) {
    return document.getElementById(id);
  }

  function syncTelemetryContext() {
    if (!global.HaloTelemetry || typeof global.HaloTelemetry.setContext !== "function") return;
    global.HaloTelemetry.setContext({
      role: session.role === "host" ? "host" : (session.role === "guest" ? "guest" : "offline"),
      connection: session.connectionPath || "unknown",
    });
  }

  function telemetry(event, stage) {
    if (global.HaloTelemetry && typeof global.HaloTelemetry.event === "function") {
      global.HaloTelemetry.event(event, stage);
    }
  }

  function collectElements() {
    elements.button = byId("online");
    elements.dialog = byId("online-dialog");
    elements.close = byId("online-close");
    elements.status = byId("online-status");
    elements.description = byId("online-description");
    elements.setup = byId("online-setup");
    elements.hostForm = byId("online-host-form");
    elements.host = byId("online-host");
    elements.map = byId("online-map");
    elements.mode = byId("online-mode");
    elements.mapOptions = byId("online-map-options");
    elements.mapSearch = byId("online-map-search");
    elements.modeOptions = byId("online-mode-options");
    elements.advancedEnabled = byId("online-advanced-enabled");
    elements.advancedFields = byId("online-advanced-fields");
    elements.scoreToWin = byId("online-score-to-win");
    elements.respawnSeconds = byId("online-respawn-seconds");
    elements.lives = byId("online-lives");
    elements.healthPercent = byId("online-health-percent");
    elements.infiniteGrenades = byId("online-infinite-grenades");
    elements.shields = byId("online-shields");
    elements.invisiblePlayers = byId("online-invisible-players");
    elements.otherPlayersOnRadar = byId("online-other-players-on-radar");
    elements.joinForm = byId("online-join-form");
    elements.code = byId("online-code");
    elements.join = byId("online-join");
    elements.invite = byId("online-invite");
    elements.inviteLink = byId("invite-link");
    elements.copy = byId("invite-copy");
    elements.copyStatus = byId("invite-copy-status");
    elements.leaveHost = byId("online-leave-host");
    elements.progress = byId("online-progress");
    elements.cancel = byId("online-cancel");
    elements.detail = byId("online-detail");
    elements.wizard = byId("online-wizard");
    elements.wizardSteps = byId("online-wizard-steps");
    elements.wizardMap = byId("online-wizard-map");
    elements.wizardMode = byId("online-wizard-mode");
    elements.wizardLink = byId("online-wizard-link");
    elements.stepMap = byId("online-step-map");
    elements.stepMode = byId("online-step-mode");
    elements.stepLink = byId("online-step-link");
    elements.mapNext = byId("online-map-next");
    elements.modeBack = byId("online-mode-back");
    elements.profile = byId("online-profile");
    elements.playerName = byId("online-player-name");
    elements.styleOptions = byId("online-style-options");
    elements.profilePreview = byId("online-profile-preview");
    elements.profilePreviewName = byId("online-profile-preview-name");
    elements.spartanImage = byId("online-spartan-image");
    elements.joinConfirm = byId("online-join-confirm");
    elements.joinProfile = byId("online-join-profile");
    elements.joinSummary = byId("online-join-summary");
    elements.joinStatus = byId("online-join-status");
    elements.verification = byId("online-human-verification");
    elements.verificationStatus = byId("online-verification-status");
    elements.verificationRetry = byId("online-verification-retry");
    elements.turnstile = byId("online-turnstile");
    elements.playerSidebar = byId("player-sidebar");
    elements.playerList = byId("player-list");
    elements.playerCount = byId("player-count");
    elements.playerEmpty = byId("player-empty");
    elements.playerSidebarToggle = byId("player-sidebar-toggle");
    elements.livePlayerCount = byId("live-player-count");
    elements.livePlayerOnline = byId("live-player-online");
    elements.livePlayerCampaign = byId("live-player-campaign");
    elements.livePlayerToday = byId("live-player-today");
  }

  function playerCountLabel(count, suffix) {
    return count + (count === 1 ? " player " : " players ") + suffix;
  }

  async function refreshLivePlayerCount() {
    if (!elements.livePlayerCount || document.hidden) return;
    try {
      var snapshot = global.HaloTelemetry &&
        typeof global.HaloTelemetry.presence === "function"
        ? global.HaloTelemetry.presence() : null;
      var result = await fetchJson("/v1/presence", snapshot ? {
        body: JSON.stringify(snapshot),
        cache: "no-store",
        method: "POST",
      } : {
        cache: "no-store",
        headers: { Accept: "application/json" },
        method: "GET",
      });
      if (
        !Number.isInteger(result.online) || result.online < 0 ||
        !Number.isInteger(result.campaign) || result.campaign < 0 ||
        !Number.isInteger(result.today) || result.today < 0
      ) return;
      elements.livePlayerOnline.textContent = playerCountLabel(result.online, "online");
      elements.livePlayerCampaign.textContent = playerCountLabel(result.campaign, "in campaign");
      elements.livePlayerToday.textContent = playerCountLabel(result.today, "today");
      elements.livePlayerCount.setAttribute(
        "aria-label",
        playerCountLabel(result.online, "online") + ", " +
        playerCountLabel(result.campaign, "in campaign") + ", " +
        playerCountLabel(result.today, "today"),
      );
      elements.livePlayerCount.hidden = false;
    } catch (error) {
      /* Presence is decorative and must never interfere with the game. */
    }
  }

  function startPresencePolling() {
    if (!elements.livePlayerCount || !elements.livePlayerOnline ||
        !elements.livePlayerCampaign || !elements.livePlayerToday) return;
    refreshLivePlayerCount();
    if (session.presenceTimer) global.clearInterval(session.presenceTimer);
    session.presenceTimer = global.setInterval(
      refreshLivePlayerCount,
      PRESENCE_POLL_MILLISECONDS,
    );
    document.addEventListener("visibilitychange", function() {
      if (!document.hidden) refreshLivePlayerCount();
    });
  }

  /* Join Game > Server Browser: native hosts list public games on internet
     play's MQTT brokers, which a page cannot reach. While the in-game
     browser is open, fetch them from this site's server (which subscribes
     for us: server/public-games.mjs) and hand each listing to the game as
     it came; the game checks its signature (p2p_lobby.c). */
  /* Delta's signed legacy table (ChupathingyCE's network family:
     port/web/src/web_delta.c): its document and Ed25519 signature, from
     this site's server (which asks halo.milenko.org, whose API has no CORS:
     server/delta-list.mjs) or, failing that, from GitHub, at start and every
     four hours. The game checks the signature and takes the table only if
     it is newer than its own; nothing here is trusted. */
  var DELTA_TABLE_REFRESH_MILLISECONDS = 4 * 60 * 60 * 1000;
  var DELTA_TABLE_RETRY_MILLISECONDS = 30 * 60 * 1000;
  var DELTA_TABLE_GITHUB =
    "https://raw.githubusercontent.com/ChupathingyCE/chupathingyce/delta-table/legacy.json";

  function startDeltaLegacyTable() {
    function bytes(url) {
      return fetch(url, { credentials: "omit", cache: "no-store" }).then(function(response) {
        if (!response.ok) throw new Error("HTTP " + response.status);
        return response.arrayBuffer();
      }).then(function(buffer) { return new Uint8Array(buffer); });
    }

    /* the document at url and its signature (url.sig), handed to the game
       as one signed table: the signature's hex digits, a line feed, the
       document's bytes */
    function offer(url, fromGithub) {
      return Promise.all([bytes(url), bytes(url + ".sig")]).then(function(parts) {
        var document = parts[0];
        var signature = String.fromCharCode.apply(null, parts[1]).trim();
        if (!/^[0-9a-fA-F]{128}$/.test(signature)) throw new Error("not a signature");
        var size = 129 + document.length;
        if (size > wasmFunction("web_delta_table_buffer_size")()) throw new Error("too large");
        var buffer = wasmFunction("web_delta_table_buffer")() >>> 0;
        var memory = typeof wasmMemory !== "undefined" ? wasmMemory : null;
        var heap = new Uint8Array(memory ? memory.buffer : global.Module.HEAPU8.buffer);
        for (var index = 0; index < 128; index++) heap[buffer + index] = signature.charCodeAt(index);
        heap[buffer + 128] = 10;
        heap.set(document, buffer + 129);
        if (wasmFunction("web_delta_offer_table")(size, fromGithub ? 1 : 0) < 0) throw new Error("dropped");
      });
    }

    function refresh() {
      if (!session.runtimeReady || !global.Module ||
          typeof global.Module._web_delta_offer_table !== "function") {
        global.setTimeout(refresh, 2000);
        return;
      }
      offer(apiBase() + "/v1/delta/legacy", false)
        .catch(function() { return offer(DELTA_TABLE_GITHUB, true); })
        .then(function() { global.setTimeout(refresh, DELTA_TABLE_REFRESH_MILLISECONDS); },
          function() { global.setTimeout(refresh, DELTA_TABLE_RETRY_MILLISECONDS); });
    }

    refresh();
  }

  /* Delta Stats and Link (ChupathingyCE's game list, halo.milenko.org; its
     API at /api): this browser's player key, 32 random bytes kept here
     (like their builds' game_list_player.key), from which the site works out
     a public player ID; the key goes to the site alone, through this site's
     server (server/delta-list.mjs: the site has no CORS). Uses:
     - a game joined through an invite (a ChupathingyCE or OpenCE host): its
       report (the game's: game_engine.c, web_delta_stats.c) and then the
       local players' lines confirmed with the key, while the player shares
       their results (the panel's checkbox; on by default, as theirs is);
     - Link profile: a short code the player types at halo.milenko.org/connect,
       signed in, which links this browser's key to their profile there. */
  var DELTA_KEY_STORAGE_KEY = "halo.web.delta-player-key.v1";
  var DELTA_SHARE_STORAGE_KEY = "halo.web.delta-share.v1";
  var DELTA_PROFILE_STORAGE_KEY = "halo.web.delta-profile.v1";
  var DELTA_CLAIM_DELAY_MILLISECONDS = 4000;
  var DELTA_CLAIM_INTERVAL_MILLISECONDS = 8000;
  var DELTA_CLAIM_ATTEMPTS = 8;
  var DELTA_REPORT_ATTEMPTS = 4;
  var DELTA_CONNECT_POLL_MILLISECONDS = 3000;

  function storageGet(name) {
    try { return global.localStorage.getItem(name); } catch (error) { return null; }
  }

  function storageSet(name, value) {
    try { global.localStorage.setItem(name, value); } catch (error) { /* (private window) */ }
  }

  function deltaPlayerKey() {
    var key = storageGet(DELTA_KEY_STORAGE_KEY);
    if (key && /^[0-9a-f]{64}$/.test(key)) return key;
    var bytes = new Uint8Array(32);
    global.crypto.getRandomValues(bytes);
    key = Array.from(bytes, function(byte) { return (byte + 256).toString(16).slice(1); }).join("");
    storageSet(DELTA_KEY_STORAGE_KEY, key);
    return key;
  }

  function deltaShares() {
    return storageGet(DELTA_SHARE_STORAGE_KEY) !== "0";
  }

  /* a POST to the site through this site's server: its status and text */
  function deltaPost(path, body) {
    return fetch(apiBase() + "/v1/delta/" + path, {
      method: "POST",
      credentials: "omit",
      cache: "no-store",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    }).then(function(response) {
      return response.text().then(function(text) { return { status: response.status, text: text.trim() }; });
    });
  }

  function deltaLog(text) {
    if (global.console) global.console.log("Delta Stats: " + text);
  }

  /* the game's report of a game it joined as a client (web_delta_stats.c) */
  function deltaGameReport(reportText, namesText) {
    var invite = session.nativeInvite;
    var report, names;
    if (!invite || !deltaShares()) return;
    try {
      report = JSON.parse(reportText);
      names = JSON.parse(namesText);
    } catch (error) {
      deltaLog("the game's report could not be read");
      return;
    }
    if (!report || !Array.isArray(report.players) || !report.players.length) return;
    var body = Object.assign({ key: deltaPlayerKey(), platform: "unknown", invite: invite }, report);
    var attempt = 0;
    (function send() {
      deltaPost("client_report", body).then(function(answer) {
        if (answer.status === 200) deltaLog("the joined game's report was taken (" + answer.text + ")");
        else if ((answer.status === 429 || answer.status >= 500) && ++attempt < DELTA_REPORT_ATTEMPTS) {
          global.setTimeout(send, DELTA_CLAIM_INTERVAL_MILLISECONDS);
        } else deltaLog("the joined game's report was not taken (" + answer.status + " " + answer.text + ")");
      }, function() {
        if (++attempt < DELTA_REPORT_ATTEMPTS) global.setTimeout(send, DELTA_CLAIM_INTERVAL_MILLISECONDS);
      });
    })();
    /* each local player's line confirmed, once the host (or this report)
       has the game on the site: a 404 until then */
    (Array.isArray(names) ? names : []).slice(0, 4).forEach(function(name) {
      var tries = 0;
      function claim() {
        deltaPost("claim", { invite: invite, key: deltaPlayerKey(), platform: "unknown", name: String(name) })
          .then(function(answer) {
            if (answer.status === 200) deltaLog(name + "'s line confirmed (" + answer.text + ")");
            else if ((answer.status === 404 || answer.status === 429 || answer.status >= 500) &&
                ++tries < DELTA_CLAIM_ATTEMPTS) global.setTimeout(claim, DELTA_CLAIM_INTERVAL_MILLISECONDS);
            else deltaLog(name + "'s line was not confirmed (" + answer.status + " " + answer.text + ")");
          }, function() {
            if (++tries < DELTA_CLAIM_ATTEMPTS) global.setTimeout(claim, DELTA_CLAIM_INTERVAL_MILLISECONDS);
          });
      }
      global.setTimeout(claim, DELTA_CLAIM_DELAY_MILLISECONDS);
    });
  }

  /* the panel's Delta part: the share checkbox and Link profile */
  function startDeltaProfile() {
    var share = byId("online-delta-share");
    var linkButton = byId("online-delta-link");
    var statusText = byId("online-delta-status");
    var confirmRow = byId("online-delta-confirm");
    var question = byId("online-delta-question");
    var yes = byId("online-delta-yes");
    var no = byId("online-delta-no");
    var linking = { token: null, timer: 0, serial: 0 };
    if (!share || !linkButton || !statusText) return;

    function linked() {
      var handle = storageGet(DELTA_PROFILE_STORAGE_KEY);
      statusText.textContent = handle ? "Linked to " + handle + "." : "";
      linkButton.textContent = handle ? "Link again" : "Link my profile";
    }

    function stop(message) {
      global.clearTimeout(linking.timer);
      linking.token = null;
      linking.serial++;
      if (confirmRow) confirmRow.hidden = true;
      linkButton.disabled = false;
      if (message != null) statusText.textContent = message;
    }

    /* the site's answer to the code's status: pending, confirm <handle>
       [previous], connected <handle>, declined, expired */
    function poll(serial) {
      if (serial !== linking.serial || !linking.token) return;
      deltaPost("connect/status", { token: linking.token }).then(function(answer) {
        if (serial !== linking.serial) return;
        var words = answer.text.split(/\s+/);
        if (answer.status === 200 && words[0] === "confirm" && words[1]) {
          if (question) {
            question.textContent = "Link this browser to " + words[1] +
              (words[2] ? " (now linked to " + words[2] + ")" : "") + "?";
          }
          if (confirmRow) confirmRow.hidden = false;
          return;
        }
        if (answer.status === 200 && words[0] === "connected" && words[1]) {
          storageSet(DELTA_PROFILE_STORAGE_KEY, words[1]);
          stop(null);
          linked();
          return;
        }
        if (answer.status === 200 && (words[0] === "declined" || words[0] === "expired")) {
          stop(words[0] === "declined" ? "Not linked." : "The code expired. Ask for a new one.");
          return;
        }
        linking.timer = global.setTimeout(function() { poll(serial); }, DELTA_CONNECT_POLL_MILLISECONDS);
      }, function() {
        linking.timer = global.setTimeout(function() { poll(serial); }, DELTA_CONNECT_POLL_MILLISECONDS);
      });
    }

    function answer(accept) {
      var serial = linking.serial;
      if (!linking.token) return;
      if (confirmRow) confirmRow.hidden = true;
      deltaPost("connect/confirm", { token: linking.token, accept: accept }).then(function(result) {
        if (serial !== linking.serial) return;
        var words = result.text.split(/\s+/);
        if (result.status === 200 && words[0] === "connected" && words[1]) {
          storageSet(DELTA_PROFILE_STORAGE_KEY, words[1]);
          stop(null);
          linked();
        } else stop(words[0] === "declined" ? "Not linked." : "The link did not go through (" + result.text + ").");
      }, function() { stop("halo.milenko.org could not be reached."); });
    }

    share.checked = deltaShares();
    share.addEventListener("change", function() { storageSet(DELTA_SHARE_STORAGE_KEY, share.checked ? "1" : "0"); });
    linkButton.addEventListener("click", function() {
      var profile = readPlayerProfile();
      stop("Asking halo.milenko.org for a code…");
      linkButton.disabled = true;
      var serial = linking.serial;
      deltaPost("connect/start", { key: deltaPlayerKey(), name: profile && profile.name ? profile.name : "" })
        .then(function(result) {
          if (serial !== linking.serial) return;
          /* "ok <code> <seconds> <token>" */
          var words = result.text.split(/\s+/);
          if (result.status !== 200 || words[0] !== "ok" || !words[1] || !words[3]) {
            stop("No code: " + (result.text || "HTTP " + result.status) + ".");
            return;
          }
          linking.token = words[3];
          linkButton.disabled = false;
          statusText.textContent = "";
          statusText.append("Your code is ");
          var code = document.createElement("strong");
          code.textContent = words[1];
          statusText.append(code, ". Sign in at ");
          var page = document.createElement("a");
          page.href = "https://halo.milenko.org/connect";
          page.target = "_blank";
          page.rel = "noopener";
          page.textContent = "halo.milenko.org/connect";
          statusText.append(page, " and type it there (good for " + Math.round(Number(words[2]) / 60) + " minutes).");
          linking.timer = global.setTimeout(function() { poll(serial); }, DELTA_CONNECT_POLL_MILLISECONDS);
        }, function() { stop("halo.milenko.org could not be reached."); });
    });
    if (yes) yes.addEventListener("click", function() { answer(true); });
    if (no) no.addEventListener("click", function() { answer(false); });
    linked();
  }

  function startPublicGamesPolling() {
    var lastFetch = 0;
    var fetching = false;
    var nextDelay = PUBLIC_GAMES_FETCH_MILLISECONDS;

    function heapBytes() {
      /* (the current view: the memory may have grown) */
      var memory = typeof wasmMemory !== "undefined" ? wasmMemory : null;
      return new Uint8Array(memory ? memory.buffer : global.Module.HEAPU8.buffer);
    }

    function deliver(games) {
      var buffer = wasmFunction("web_public_games_buffer")() >>> 0;
      var capacity = wasmFunction("web_public_games_buffer_size")();
      var heard = wasmFunction("web_public_games_heard");
      for (var index = 0; index < games.length; index++) {
        var game = games[index];
        if (!game || !/^[0-9a-f]{32}$/.test(game.slot) || typeof game.payload !== "string") continue;
        var payload;
        try {
          payload = Uint8Array.from(global.atob(game.payload), function(c) { return c.charCodeAt(0); });
        } catch (error) {
          continue;
        }
        if (!payload.length || 32 + payload.length > capacity) continue;
        var heap = heapBytes();
        for (var i = 0; i < 32; i++) heap[buffer + i] = game.slot.charCodeAt(i);
        heap.set(payload, buffer + 32);
        heard(payload.length, game.retained ? 1 : 0);
      }
    }

    function tick() {
      if (!session.runtimeReady || !global.Module ||
          typeof global.Module._web_public_games_browsing !== "function") return;
      if (!global.Module._web_public_games_browsing()) {
        lastFetch = 0;
        nextDelay = PUBLIC_GAMES_FETCH_MILLISECONDS;
        return;
      }
      global.Module._web_public_games_update();
      if (fetching || (lastFetch && Date.now() - lastFetch < nextDelay)) return;
      fetching = true;
      lastFetch = Date.now();
      fetch(apiBase() + "/v1/public-games", { credentials: "omit", cache: "no-store" })
        .then(function(response) {
          if (!response.ok) throw new Error("HTTP " + response.status);
          return response.json();
        })
        .then(function(result) {
          nextDelay = PUBLIC_GAMES_FETCH_MILLISECONDS;
          if (result && result.v === 1 && Array.isArray(result.games) &&
              global.Module._web_public_games_browsing()) {
            deliver(result.games);
          }
        })
        .catch(function() {
          /* (a server without the server browser, or offline: try again later) */
          nextDelay = PUBLIC_GAMES_RETRY_MILLISECONDS;
        })
        .finally(function() { fetching = false; });
      /* (and the games other browsers host as PUBLIC: the lobby service's
       * list, this build's only, which can join them) */
      fetch(apiBase() + "/v1/public-rooms?build=" + encodeURIComponent(buildId()),
        { credentials: "omit", cache: "no-store" })
        .then(function(response) { return response.ok ? response.json() : null; })
        .then(function(result) {
          if (result && Array.isArray(result.games) && global.Module._web_public_games_browsing()) {
            deliverRooms(result.games);
          }
        })
        .catch(function() { /* (a lobby service without the list) */ });
      /* (and ChupathingyCE's game list, Delta List: the host's platform,
       * dedicated servers, who is playing, and the games only it has) */
      fetch(apiBase() + "/v1/delta/games", { credentials: "omit", cache: "no-store" })
        .then(function(response) { return response.ok ? response.json() : null; })
        .then(function(result) {
          if (result && Array.isArray(result.games) && global.Module._web_public_games_browsing()) {
            deliverDeltaGames(result.games);
          }
        })
        .catch(function() { /* (a server without Delta List) */ });
    }

    /* Delta List's games to the game (port/web/src/web_delta_list.c): each
       one's texts, ended by a zero each (who is playing: each name ended by
       a 0x1E), then its numbers */
    var DELTA_PLATFORMS = ["unknown", "pc_windows", "pc_macos", "pc_linux", "android", "steam_deck", "xbox",
      "xbox360", "wiiu", "switch"];
    var DELTA_HOSTINGS = { player: 1, dedicated: 2, official: 3 };
    var DELTA_PROTOCOLS = { delta: 1, opence: 2 };

    function deliverDeltaGames(games) {
      var buffer = wasmFunction("web_delta_games_buffer")() >>> 0;
      var capacity = wasmFunction("web_delta_games_buffer_size")();
      var add = wasmFunction("web_delta_games_add");
      var encoder = new TextEncoder();
      var text = function(value, most) { return String(value == null ? "" : value).slice(0, most); };
      var number = function(value) { return Number.isFinite(Number(value)) ? Math.trunc(Number(value)) : 0; };
      wasmFunction("web_delta_games_begin")();
      for (var index = 0; index < games.length && index < 128; index++) {
        var game = games[index];
        if (!game || typeof game.invite !== "string" || !/^[0-9a-f]{64}$/i.test(game.invite) ||
            game.verified === false) continue;
        var roster = Array.isArray(game.roster) ? game.roster : [];
        var names = roster.map(function(player) {
          return text(player && player.name, 24).replace(/[\u0000-\u001f]/g, "") + "\u001e";
        }).join("");
        var bytes = encoder.encode([game.invite, text(game.name, 64), text(game.map, 128),
          text(game.gametype, 48), names].join("\u0000") + "\u0000");
        if (bytes.length > capacity) {
          /* (too many names: who is playing left out) */
          bytes = encoder.encode([game.invite, text(game.name, 64), text(game.map, 128),
            text(game.gametype, 48), ""].join("\u0000") + "\u0000");
          if (bytes.length > capacity) continue;
          roster = [];
        }
        var memory = typeof wasmMemory !== "undefined" ? wasmMemory : null;
        new Uint8Array(memory ? memory.buffer : global.Module.HEAPU8.buffer).set(bytes, buffer);
        var platform = DELTA_PLATFORMS.indexOf(game.platform);
        add(number(game.engine), number(game.players), number(game.maximum_players), game.open ? 1 : 0,
          game.in_progress ? 1 : 0, game.teams ? 1 : 0, number(game.version), number(game.score_limit),
          game.platform ? platform : -1, DELTA_HOSTINGS[game.hosting] || 0, DELTA_PROTOCOLS[game.protocol] || 0,
          roster.length);
      }
      wasmFunction("web_delta_games_end")();
    }

    function deliverRooms(games) {
      var buffer = wasmFunction("web_public_rooms_buffer")() >>> 0;
      var capacity = wasmFunction("web_public_rooms_buffer_size")();
      var add = wasmFunction("web_public_rooms_add");
      var encoder = new TextEncoder();
      session.publicRooms = new Map();
      wasmFunction("web_public_rooms_begin")();
      games.forEach(function(game) {
        if (!game || typeof game.roomId !== "string" || typeof game.code !== "string" ||
            game.roomId === (session.room && session.room.id)) return;
        var text = encoder.encode([game.roomId, game.name, game.map, game.gametype].map(function(part) {
          return String(part || "").replace(/\0/g, "");
        }).join("\0") + "\0");
        if (text.length > capacity) return;
        heapBytes().set(text, buffer);
        session.publicRooms.set(game.roomId, game.code);
        add(game.players | 0, game.maximum | 0, game.open ? 1 : 0, game.inProgress ? 1 : 0, game.hasTeams ? 1 : 0);
      });
      wasmFunction("web_public_rooms_end")();
    }

    global.setInterval(function() {
      try {
        tick();
      } catch (error) {
        /* the runtime is still starting, or has stopped */
      }
    }, PUBLIC_GAMES_TICK_MILLISECONDS);
  }

  function buildId() {
    var page = new URL(global.location.href);
    var meta = document.querySelector('meta[name="halo-build-id"]');
    var value = meta && meta.content;
    var pageIsLoopback = page.hostname === "127.0.0.1" || page.hostname === "localhost";
    /* A query override is useful while testing two local builds, but a public
       invite must not be able to opt an incompatible client into a room. */
    if (pageIsLoopback && page.searchParams.get("build")) {
      value = page.searchParams.get("build");
    }
    return value && /^[A-Za-z0-9._-]{1,96}$/.test(value) ? value : "development";
  }

  function apiBase() {
    /* Self-hosted: an invite link from another host of this build joins that
       host's lobby service directly. */
    if (session.apiBaseOverride) return session.apiBaseOverride;
    var page = new URL(global.location.href);
    var query = page.searchParams.get("signal");
    var meta = document.querySelector('meta[name="halo-signaling-url"]');
    var pageIsLoopback = page.hostname === "127.0.0.1" || page.hostname === "localhost";
    if (query) {
      var override = new URL(query, global.location.href);
      var overrideIsLoopback = override.hostname === "127.0.0.1" ||
        override.hostname === "localhost";
      if (!pageIsLoopback || !overrideIsLoopback) {
        throw new Error("Custom room services are allowed only for loopback development.");
      }
    }
    var configured = query || (meta && meta.content);
    if (configured) {
      var parsed = new URL(configured, global.location.href);
      if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
        throw new Error("The room service URL must use HTTP or HTTPS.");
      }
      return parsed.href.replace(/\/$/, "");
    }
    if ((page.hostname === "127.0.0.1" || page.hostname === "localhost") &&
        page.port !== "8787") {
      return page.protocol + "//" + page.hostname + ":8787";
    }
    return page.origin;
  }

  function turnstileSiteKey() {
    var meta = document.querySelector('meta[name="halo-turnstile-sitekey"]');
    var value = meta && meta.content;
    return value && /^0x[A-Za-z0-9_-]{20,120}$/.test(value) ? value : null;
  }

  function clearTurnstileTimer() {
    if (humanVerification.renderTimer) global.clearTimeout(humanVerification.renderTimer);
    humanVerification.renderTimer = 0;
  }

  function turnstileReady(action) {
    return !turnstileSiteKey() ||
      (humanVerification.action === action && !!humanVerification.token);
  }

  function syncVerificationButtons() {
    if (elements.host) {
      elements.host.disabled = humanVerification.busy || !session.runtimeReady ||
        !turnstileReady("create_room");
    }
    if (elements.joinProfile) {
      elements.joinProfile.disabled = humanVerification.busy;
    }
  }

  function setVerificationState(state, message) {
    humanVerification.state = state;
    if (elements.verification) {
      elements.verification.hidden = !turnstileSiteKey();
      elements.verification.dataset.state = state;
    }
    if (elements.verificationStatus) {
      elements.verificationStatus.textContent = message || "";
      elements.verificationStatus.hidden = !message;
    }
    if (elements.verificationRetry) {
      elements.verificationRetry.hidden = state !== "error";
    }
    syncVerificationButtons();
  }

  function resetTurnstile() {
    humanVerification.token = null;
    if (turnstileSiteKey()) {
      setVerificationState("loading", "Checking that you're human…");
    }
    if (global.turnstile && humanVerification.widgetId !== null) {
      try { global.turnstile.reset(humanVerification.widgetId); } catch (error) { /* not rendered */ }
    }
  }

  function renderTurnstile(action, force) {
    var sitekey = turnstileSiteKey();
    if (!sitekey || !elements.turnstile) {
      setVerificationState("ready", "");
      return;
    }
    if (!force && humanVerification.action === action &&
        humanVerification.widgetId !== null) return;
    clearTurnstileTimer();
    var changedAction = humanVerification.action !== action;
    humanVerification.action = action;
    humanVerification.token = null;
    if (changedAction || force) {
      humanVerification.generation++;
      humanVerification.renderAttempts = 0;
      setVerificationState("loading", "Checking that you're human…");
    }
    if (!global.turnstile || typeof global.turnstile.render !== "function") {
      humanVerification.renderAttempts++;
      if (humanVerification.renderAttempts >= TURNSTILE_RENDER_ATTEMPTS) {
        setVerificationState(
          "error",
          "Human verification is taking longer than expected. Try it again.");
        return;
      }
      humanVerification.renderTimer = global.setTimeout(function() {
        renderTurnstile(action);
      }, 150);
      return;
    }
    if (humanVerification.widgetId !== null) {
      try { global.turnstile.remove(humanVerification.widgetId); } catch (error) { /* stale widget */ }
      humanVerification.widgetId = null;
    }
    elements.turnstile.replaceChildren();
    var generation = humanVerification.generation;
    try {
      humanVerification.widgetId = global.turnstile.render(elements.turnstile, {
        action: action,
        appearance: "interaction-only",
        callback: function(token) {
          if (generation !== humanVerification.generation || humanVerification.action !== action) return;
          humanVerification.token = token;
          setVerificationState(
            "ready",
            action === "join_room"
              ? (session.runtimeReady ? "Verified — ready to join." : "Verified — Halo is still loading.")
              : "Verified — ready to create your link.");
          if (action !== "join_room" || session.runtimeReady) setStatus("");
          maybeStartRequestedJoin();
        },
        "error-callback": function() {
          if (generation !== humanVerification.generation) return;
          humanVerification.token = null;
          setVerificationState(
            "error",
            "We couldn't verify you this time. Check your connection and try again.");
        },
        "expired-callback": function() {
          if (generation !== humanVerification.generation) return;
          humanVerification.token = null;
          setVerificationState("loading", "Verification expired — checking again…");
          try {
            global.turnstile.reset(humanVerification.widgetId);
          } catch (error) {
            setVerificationState("error", "Verification expired. Try it again.");
          }
        },
        "timeout-callback": function() {
          if (generation !== humanVerification.generation) return;
          humanVerification.token = null;
          setVerificationState("error", "Human verification timed out. Try it again.");
        },
        sitekey: sitekey,
        size: "flexible",
        theme: "dark",
      });
    } catch (error) {
      humanVerification.widgetId = null;
      setVerificationState("error", "Human verification could not start. Try it again.");
    }
  }

  function consumeTurnstile(action) {
    if (!turnstileSiteKey()) return null;
    if (humanVerification.action !== action || !humanVerification.token) {
      renderTurnstile(action);
      throw new Error(humanVerification.state === "error" ?
        "Use Try again to restart human verification." :
        "One moment — human verification is still finishing.");
    }
    var token = humanVerification.token;
    humanVerification.token = null;
    return token;
  }

  /* (a token if human verification has one ready, else none: a room the
   * game's menus ask for cannot wait on the widget; a lobby service that
   * requires verification then refuses it, and fail() shows why) */
  function consumeTurnstileIfReady(action) {
    if (!turnstileSiteKey() || humanVerification.action !== action || !humanVerification.token) return null;
    var token = humanVerification.token;
    humanVerification.token = null;
    return token;
  }

  function maybeStartRequestedJoin() {
    if (!session.joinRequested || humanVerification.busy) return;
    var invite = session.pendingInvite;
    if (!invite) {
      session.joinRequested = false;
      setStatus("That invite is no longer available.", "error");
      return;
    }
    if (!session.runtimeReady) {
      setStatus("Halo is still loading. Your game will join automatically when it is ready.");
      return;
    }
    if (!turnstileReady("join_room")) {
      setStatus("Finishing human verification…");
      renderTurnstile("join_room", humanVerification.state === "error");
      return;
    }
    try {
      readPlayerProfile();
      session.joinRequested = false;
      join(invite, consumeTurnstile("join_room")).catch(fail);
    } catch (error) {
      session.joinRequested = false;
      setStatus(error.message, "error");
    }
  }

  function requestJoinFromProfile() {
    session.joinRequested = true;
    maybeStartRequestedJoin();
  }

  function showDialog() {
    if (!elements.dialog.open) elements.dialog.showModal();
  }

  /* SDL listens for keyboard events on window so the game keeps receiving
     input when its canvas has focus. Keyboard events from modal and sidebar
     controls bubble there too unless their surfaces contain them. Do not
     prevent the default: text editing, control activation, and Escape's
     native dialog behavior must keep working. */
  function containDialogKeyboardEvent(event) {
    event.stopPropagation();
  }

  function setHeader(text, state) {
    elements.button.textContent = text;
    elements.button.dataset.state = state || "offline";
  }

  function setStatus(text, tone) {
    var message = String(text || "").trim();
    elements.status.textContent = message;
    elements.status.hidden = !message;
    if (tone) elements.status.dataset.tone = tone;
    else delete elements.status.dataset.tone;
    if (elements.joinStatus) {
      var joinView = elements.dialog && elements.dialog.dataset.view === "join";
      elements.joinStatus.textContent = message;
      elements.joinStatus.hidden = !message || !joinView;
      if (tone) elements.joinStatus.dataset.tone = tone;
      else delete elements.joinStatus.dataset.tone;
    }
  }

  function setBusy(busy) {
    humanVerification.busy = !!busy;
    elements.map.disabled = !!busy;
    elements.mode.disabled = !!busy;
    setPickerLocked(elements.mapOptions, "halo-map-choice", !!busy);
    setPickerLocked(elements.modeOptions, "halo-mode-choice", !!busy);
    elements.join.disabled = !!busy || !session.runtimeReady;
    elements.code.disabled = !!busy;
    if (elements.mapNext) elements.mapNext.disabled = !!busy;
    if (elements.modeBack) elements.modeBack.disabled = !!busy;
    syncAdvancedSettingsState(!!busy);
    syncVerificationButtons();
    setProfileLocked(!!busy || session.active);
  }

  function pickerInputs(container, name) {
    if (!container || typeof container.querySelectorAll !== "function") return [];
    return Array.prototype.slice.call(
      container.querySelectorAll('input[name="' + name + '"]'));
  }

  function setPickerLocked(container, name, locked) {
    pickerInputs(container, name).forEach(function(input) {
      input.disabled = !!locked;
    });
  }

  function syncPickerCards(container, name, select) {
    if (!select) return;
    pickerInputs(container, name).forEach(function(input) {
      var selected = input.value === select.value;
      input.checked = selected;
      input.setAttribute("aria-checked", selected ? "true" : "false");
      if (typeof input.closest === "function") {
        var card = input.closest("[data-picker-option], label");
        if (card && card.dataset) card.dataset.selected = selected ? "true" : "false";
      }
    });
  }

  function syncHostPickerCards() {
    syncPickerCards(elements.mapOptions, "halo-map-choice", elements.map);
    syncPickerCards(elements.modeOptions, "halo-mode-choice", elements.mode);
  }

  function attachPickerEvents(container, name, select, onChange) {
    if (!container || !select) return;
    container.addEventListener("change", function(event) {
      var input = event.target;
      if (!input || input.name !== name || input.disabled) return;
      select.value = input.value;
      syncPickerCards(container, name, select);
      if (onChange) onChange();
    });
    select.addEventListener("change", function() {
      syncPickerCards(container, name, select);
      if (onChange) onChange();
    });
  }

  function profileStyleInputs() {
    if (!elements.styleOptions || typeof elements.styleOptions.querySelectorAll !== "function") {
      return [];
    }
    return Array.prototype.slice.call(
      elements.styleOptions.querySelectorAll('input[name="player-style"]'));
  }

  function setProfileLocked(locked) {
    if (elements.playerName) elements.playerName.disabled = !!locked;
    profileStyleInputs().forEach(function(input) { input.disabled = !!locked; });
  }

  function generatedPlayerName() {
    var value = Math.floor(Math.random() * 900) + 100;
    try {
      if (global.crypto && typeof global.crypto.getRandomValues === "function") {
        var random = new Uint16Array(1);
        global.crypto.getRandomValues(random);
        value = 100 + (random[0] % 900);
      }
    } catch (error) {
      /* A friendly fallback does not require cryptographic randomness. */
    }
    return "Spartan " + value;
  }

  function normalizePlayerProfile(value) {
    var source = value || {};
    var name = String(source.name || "").replace(/\s+/g, " ").trim();
    var style = String(source.style || "sage").toLowerCase();
    if (name.length < 1 || name.length > PLAYER_NAME_MAXIMUM_LENGTH ||
        !/^[A-Za-z0-9][A-Za-z0-9 ._'-]*$/.test(name)) {
      throw new Error("Use 1–11 basic letters or numbers for your player name.");
    }
    if (PLAYER_STYLES.indexOf(style) < 0) {
      throw new Error("Choose a valid player style.");
    }
    return { name: name, style: style };
  }

  function selectedPlayerStyle() {
    var inputs = profileStyleInputs();
    var selected = inputs.find(function(input) { return input.checked; });
    return selected ? selected.value : "sage";
  }

  function renderPlayerProfilePreview(profile) {
    if (elements.profilePreview) elements.profilePreview.dataset.style = profile.style;
    if (elements.profilePreviewName) elements.profilePreviewName.textContent = profile.name;
    if (elements.spartanImage) {
      if (elements.spartanImage.dataset.style !== profile.style) {
        elements.spartanImage.src = "assets/ui/spartan/" + profile.style + ".png";
        elements.spartanImage.dataset.style = profile.style;
      }
      elements.spartanImage.alt = profile.name + " in " + profile.style + " armor";
    }
  }

  function writePlayerProfile(profile) {
    if (elements.playerName) elements.playerName.value = profile.name;
    profileStyleInputs().forEach(function(input) {
      input.checked = input.value === profile.style;
    });
    renderPlayerProfilePreview(profile);
  }

  function readPlayerProfile() {
    return normalizePlayerProfile({
      name: elements.playerName ? elements.playerName.value :
        (session.profile && session.profile.name),
      style: selectedPlayerStyle(),
    });
  }

  function savePlayerProfile(profile) {
    session.profile = profile;
    writePlayerProfile(profile);
    try {
      global.localStorage.setItem(PLAYER_PROFILE_STORAGE_KEY, JSON.stringify(profile));
    } catch (error) {
      /* A blocked store should never prevent joining a game. */
    }
    updateLocalRoster();
  }

  function restorePlayerProfile() {
    var profile = { name: generatedPlayerName(), style: "sage" };
    try {
      var saved = JSON.parse(global.localStorage.getItem(PLAYER_PROFILE_STORAGE_KEY));
      profile = normalizePlayerProfile(saved);
    } catch (error) {
      /* First-time and stale profiles get a friendly, editable default. */
    }
    savePlayerProfile(profile);
  }

  function applyPlayerCustomization(profile) {
    var fn = global.Module && global.Module._platform_web_online_set_player_customization;
    if (typeof fn !== "function") return;
    var args = [PLAYER_STYLE_COLORS[profile.style]];
    for (var index = 0; index < PLAYER_NAME_MAXIMUM_LENGTH; index++) {
      args.push(index < profile.name.length ? profile.name.charCodeAt(index) : 0);
    }
    if (!fn.apply(null, args)) {
      throw new Error("Halo could not apply your player customization.");
    }
  }

  function setWizardStep(step) {
    session.wizardStep = step;
    if (elements.wizard) elements.wizard.dataset.step = step;
    if (elements.stepMap) elements.stepMap.hidden = step !== "map";
    if (elements.stepMode) elements.stepMode.hidden = step !== "mode";
    /* The invite lives in the persistent player sidebar once hosting starts;
       it is not a third wizard step. Keep these guards for stale shells while
       allowing the Link markup to be removed entirely. */
    if (elements.stepLink) elements.stepLink.hidden = true;
    if (elements.wizardLink) elements.wizardLink.hidden = true;
    var order = ["map", "mode"];
    var current = order.indexOf(step);
    [elements.wizardMap, elements.wizardMode]
      .forEach(function(indicator, index) {
        if (!indicator) return;
        if (index === current) indicator.setAttribute("aria-current", "step");
        else indicator.removeAttribute("aria-current");
        indicator.dataset.complete = index < current ? "true" : "false";
      });
  }

  function playerFallbackName(player) {
    if (player.peerId === session.selfPeerId && session.profile) return session.profile.name;
    return player.role === "host" ? "Host" : "Joining…";
  }

  function normalizedRosterPlayer(value) {
    if (!value || typeof value.peerId !== "string" ||
        !/^[hg]_[A-Za-z0-9_-]{16}$/.test(value.peerId) ||
        (value.role !== "host" && value.role !== "guest")) return null;
    var profile = null;
    if (value.profile !== null && value.profile !== undefined) {
      try { profile = normalizePlayerProfile(value.profile); } catch (error) { return null; }
    }
    return { peerId: value.peerId, role: value.role, profile: profile };
  }

  function renderRoster() {
    if (!elements.playerSidebar) return;
    elements.playerSidebar.hidden = false;
    elements.playerSidebar.dataset.onlineActive = session.active ? "true" : "false";
    if (!session.active && elements.playerSidebar.dataset.collapsed === "true") {
      delete elements.playerSidebar.dataset.collapsed;
      if (elements.playerSidebarToggle) {
        elements.playerSidebarToggle.setAttribute("aria-expanded", "true");
        elements.playerSidebarToggle.setAttribute("aria-label", "Collapse player list");
        elements.playerSidebarToggle.textContent = "⌃";
      }
    }
    var players = Array.from(session.roster.values());
    players.sort(function(left, right) {
      if (left.role !== right.role) return left.role === "host" ? -1 : 1;
      var leftName = left.profile ? left.profile.name : playerFallbackName(left);
      var rightName = right.profile ? right.profile.name : playerFallbackName(right);
      return leftName.localeCompare(rightName);
    });
    if (elements.playerCount) {
      elements.playerCount.textContent = players.length + "/" + ROOM_CAPACITY;
      elements.playerCount.setAttribute(
        "aria-label",
        "Players in room: " + players.length + " of " + ROOM_CAPACITY);
    }
    if (elements.playerEmpty) elements.playerEmpty.hidden = players.length !== 0;
    if (elements.playerList && typeof document.createElement === "function") {
      while (elements.playerList.firstChild) elements.playerList.removeChild(elements.playerList.firstChild);
      players.forEach(function(player) {
        var profile = player.profile || {
          name: playerFallbackName(player),
          style: player.peerId === session.selfPeerId && session.profile ?
            session.profile.style : "sage",
        };
        var row = document.createElement("li");
        row.className = "player-row";
        row.dataset.style = profile.style;
        row.dataset.role = player.role;
        row.dataset.self = player.peerId === session.selfPeerId ? "true" : "false";
        var swatch = document.createElement("span");
        swatch.className = "player-swatch";
        swatch.setAttribute("aria-hidden", "true");
        var label = document.createElement("span");
        label.className = "player-name";
        label.textContent = profile.name;
        var role = document.createElement("span");
        role.className = "player-role";
        role.textContent = player.peerId === session.selfPeerId ? "You" :
          (player.role === "host" ? "Host" : "Player");
        row.appendChild(swatch);
        row.appendChild(label);
        row.appendChild(role);
        elements.playerList.appendChild(row);
      });
    }
  }

  function replaceRoster(players) {
    if (!Array.isArray(players) || players.length > ROOM_CAPACITY) return;
    var next = new Map();
    players.forEach(function(value) {
      var player = normalizedRosterPlayer(value);
      if (player) next.set(player.peerId, player);
    });
    session.roster = next;
    updateLocalRoster();
    renderRoster();
  }

  function updateLocalRoster() {
    if (!session.selfPeerId || !session.profile || !session.role) return;
    session.roster.set(session.selfPeerId, {
      peerId: session.selfPeerId,
      profile: session.profile,
      role: session.role,
    });
    renderRoster();
  }

  function selectHasIndex(select, index) {
    return Array.prototype.some.call(select.options, function(option) {
      return option.value === String(index);
    });
  }

  function validatedIndex(value, maximum, select, label) {
    if (value === null || value === undefined || String(value).trim() === "") {
      throw new Error("Choose a " + label + ".");
    }
    var index = Number(value);
    if (!Number.isInteger(index) || index < 0 || index > maximum ||
        !selectHasIndex(select, index)) {
      throw new Error("Choose a valid " + label + ".");
    }
    return index;
  }

  function selectedLabel(select, index) {
    var option = Array.prototype.find.call(select.options, function(candidate) {
      return candidate.value === String(index);
    });
    return option ? option.textContent.trim() : "";
  }

  function integerSetting(value, minimum, maximum, label) {
    var parsed = Number(value);
    if (!Number.isInteger(parsed) || parsed < minimum || parsed > maximum) {
      throw new Error(label + " must be between " + minimum + " and " + maximum + ".");
    }
    return parsed;
  }

  function advancedDefaults(modeIndex) {
    var preset = ADVANCED_MODE_DEFAULTS[modeIndex] || ADVANCED_MODE_DEFAULTS[0];
    return {
      scoreToWin: preset.scoreToWin,
      respawnSeconds: preset.respawnSeconds,
      lives: 0,
      healthPercent: 100,
      infiniteGrenades: false,
      shields: true,
      invisiblePlayers: false,
      otherPlayersOnRadar: true,
    };
  }

  function normalizeAdvancedSettings(value, modeIndex) {
    if (!value) return null;
    var defaults = advancedDefaults(modeIndex);
    return {
      scoreToWin: integerSetting(value.scoreToWin, 1, 1000, "Score to win"),
      respawnSeconds: integerSetting(value.respawnSeconds, 0, 30, "Respawn delay"),
      lives: integerSetting(value.lives, 0, 99, "Lives"),
      healthPercent: integerSetting(value.healthPercent, 25, 400, "Health"),
      infiniteGrenades: value.infiniteGrenades === undefined ?
        defaults.infiniteGrenades : !!value.infiniteGrenades,
      shields: value.shields === undefined ? defaults.shields : !!value.shields,
      invisiblePlayers: value.invisiblePlayers === undefined ?
        defaults.invisiblePlayers : !!value.invisiblePlayers,
      otherPlayersOnRadar: value.otherPlayersOnRadar === undefined ?
        defaults.otherPlayersOnRadar : !!value.otherPlayersOnRadar,
    };
  }

  function readAdvancedSettings(modeIndex) {
    if (!elements.advancedEnabled || !elements.advancedEnabled.checked) return null;
    return normalizeAdvancedSettings({
      scoreToWin: elements.scoreToWin.value,
      respawnSeconds: elements.respawnSeconds.value,
      lives: elements.lives.value,
      healthPercent: elements.healthPercent.value,
      infiniteGrenades: elements.infiniteGrenades.checked,
      shields: elements.shields.checked,
      invisiblePlayers: elements.invisiblePlayers.checked,
      otherPlayersOnRadar: elements.otherPlayersOnRadar.checked,
    }, modeIndex);
  }

  function writeAdvancedSettings(value, modeIndex) {
    if (!elements.advancedEnabled || !elements.advancedFields) return;
    var settings = value ? normalizeAdvancedSettings(value, modeIndex) : advancedDefaults(modeIndex);
    elements.advancedEnabled.checked = !!value;
    elements.scoreToWin.value = String(settings.scoreToWin);
    elements.respawnSeconds.value = String(settings.respawnSeconds);
    elements.lives.value = String(settings.lives);
    elements.healthPercent.value = String(settings.healthPercent);
    elements.infiniteGrenades.checked = settings.infiniteGrenades;
    elements.shields.checked = settings.shields;
    elements.invisiblePlayers.checked = settings.invisiblePlayers;
    elements.otherPlayersOnRadar.checked = settings.otherPlayersOnRadar;
    syncAdvancedSettingsState(false);
  }

  function syncAdvancedSettingsState(busy) {
    if (!elements.advancedEnabled || !elements.advancedFields) return;
    elements.advancedEnabled.disabled = !!busy;
    elements.advancedFields.disabled = !!busy || !elements.advancedEnabled.checked;
    elements.advancedFields.dataset.enabled = elements.advancedEnabled.checked ? "true" : "false";
  }

  function resetAdvancedDefaultsForMode() {
    if (!elements.mode || !elements.advancedEnabled || elements.advancedEnabled.checked) return;
    var modeIndex = validatedIndex(elements.mode.value, LAST_MODE_INDEX, elements.mode, "mode");
    writeAdvancedSettings(null, modeIndex);
  }

  function normalizeHostSettings(value) {
    var source = value || {
      mapIndex: elements.map.value,
      modeIndex: elements.mode.value,
    };
    var mapIndex = validatedIndex(source.mapIndex, LAST_MAP_INDEX, elements.map, "map");
    var modeIndex = validatedIndex(source.modeIndex, LAST_MODE_INDEX, elements.mode, "mode");
    return {
      mapIndex: mapIndex,
      modeIndex: modeIndex,
      mapName: selectedLabel(elements.map, mapIndex),
      modeName: selectedLabel(elements.mode, modeIndex),
      advanced: value ? normalizeAdvancedSettings(source.advanced, modeIndex) :
        readAdvancedSettings(modeIndex),
    };
  }

  function restoreHostSettings() {
    elements.map.value = "0";
    elements.mode.value = "0";
    try {
      var saved = JSON.parse(global.localStorage.getItem(HOST_SETTINGS_STORAGE_KEY));
      var settings = normalizeHostSettings(saved);
      elements.map.value = String(settings.mapIndex);
      elements.mode.value = String(settings.modeIndex);
      writeAdvancedSettings(settings.advanced, settings.modeIndex);
    } catch (error) {
      /* Missing, blocked, or stale storage falls back to Battle Creek + Slayer. */
      writeAdvancedSettings(null, 0);
    }
    syncHostPickerCards();
  }

  /* The Custom Edition multiplayer maps the game found in the server's
   * custom_maps folder (src/web_online_ui.c, publish_custom_maps), offered
   * after the Xbox's 13 levels with the map indices the game takes. */
  var XBOX_MAP_COUNT = 13;
  var MAXIMUM_CUSTOM_MAPS = 256 - XBOX_MAP_COUNT;
  var customMaps = [];

  function addCustomMapChoices() {
    var document = global.document;
    if (!document || !elements.map || !elements.mapOptions || !customMaps.length) return;
    customMaps.forEach(function(map, index) {
      var value = String(XBOX_MAP_COUNT + index);
      if (selectHasIndex(elements.map, value)) return;
      var option = document.createElement("option");
      option.value = value;
      option.textContent = map.name;
      elements.map.appendChild(option);

      var label = document.createElement("label");
      var input = document.createElement("input");
      var card = document.createElement("span");
      var picture = document.createElement("span");
      var name = document.createElement("span");
      label.className = "visual-choice";
      label.setAttribute("data-picker-option", "");
      input.type = "radio";
      input.name = "halo-map-choice";
      input.value = value;
      card.className = "visual-choice-card map-choice-card";
      picture.className = "custom-map-picture";
      picture.textContent = "Custom Edition";
      /* (its file's name, for its picture: customMapPictures) */
      picture.dataset.customMap = String(map.level || "").split("\\").pop();
      name.className = "choice-label";
      name.textContent = map.name;
      card.appendChild(picture);
      card.appendChild(name);
      label.appendChild(input);
      label.appendChild(card);
      elements.mapOptions.appendChild(label);
    });
    LAST_MAP_INDEX = XBOX_MAP_COUNT - 1 + customMaps.length;
    if (elements.mapSearch) elements.mapSearch.hidden = false;
    /* (a saved Custom Edition map could not be chosen before the list came) */
    if (!session.active) {
      try {
        var saved = JSON.parse(global.localStorage.getItem(HOST_SETTINGS_STORAGE_KEY));
        if (saved && saved.mapIndex >= XBOX_MAP_COUNT && selectHasIndex(elements.map, saved.mapIndex)) {
          elements.map.value = String(saved.mapIndex);
        }
      } catch (error) {
        /* (blocked storage: the choice stays) */
      }
    }
    syncHostPickerCards();
  }

  /* A map's picture: <name>.bmp beside it in the server's custom_maps folder,
   * the one OpenCE's menus show too (the server lists the folder:
   * assets/custom_maps/index.json). */
  function customMapPictures() {
    if (!global.fetch || !elements.mapOptions) return;
    global.fetch("assets/custom_maps/index.json", { cache: "no-cache" })
      .then(function(response) { return response.ok ? response.json() : []; })
      .then(function(files) {
        var pictures = {};
        (Array.isArray(files) ? files : []).forEach(function(file) {
          /* (each a name, or its name with its size and version) */
          var name = typeof file === "string" ? file : file && file.name;
          if (typeof name === "string" && /\.bmp$/i.test(name)) pictures[name.slice(0, -4).toLowerCase()] = name;
        });
        Array.prototype.forEach.call(elements.mapOptions.querySelectorAll("[data-custom-map]"), function(picture) {
          var file = pictures[picture.dataset.customMap.toLowerCase()];
          if (!file) return;
          var image = global.document.createElement("img");
          image.src = "assets/custom_maps/" + encodeURIComponent(file);
          image.alt = "";
          image.loading = "lazy";
          picture.replaceWith(image);
        });
      })
      .catch(function() { /* (no pictures: the cards keep their label) */ });
  }

  global.haloOnlineCustomMaps = function(maps) {
    customMaps = (Array.isArray(maps) ? maps : []).filter(function(map) {
      return map && typeof map.name === "string" && map.name.trim();
    }).slice(0, MAXIMUM_CUSTOM_MAPS);
    addCustomMapChoices();
    customMapPictures();
  };

  function saveHostSettings(settings) {
    try {
      var saved = {
        mapIndex: settings.mapIndex,
        modeIndex: settings.modeIndex,
      };
      if (settings.advanced) saved.advanced = settings.advanced;
      global.localStorage.setItem(HOST_SETTINGS_STORAGE_KEY, JSON.stringify(saved));
    } catch (error) {
      /* Private browsing may make local storage unavailable; hosting still works. */
    }
  }

  function hostSettingsLabel() {
    return session.hostSettings ?
      session.hostSettings.mapName + " · " + session.hostSettings.modeName :
      "Your game";
  }

  function connectedFriendsLabel(count) {
    return count === 1 ? "1 friend connected" : count + " friends connected";
  }

  function requireCurrentOperation(generation) {
    if (generation !== session.operationGeneration || !session.active || session.closing) {
      var error = new Error("Online operation was canceled.");
      error.haloCanceled = true;
      throw error;
    }
  }

  function isCurrentSocketOperation(socketGeneration, operationGeneration) {
    return socketGeneration === session.socketGeneration &&
      operationGeneration === session.operationGeneration &&
      session.active && !session.closing;
  }

  function showSetup() {
    session.joinRequested = false;
    if (elements.dialog) elements.dialog.dataset.view = "setup";
    if (elements.wizardSteps) elements.wizardSteps.hidden = false;
    elements.setup.hidden = false;
    elements.invite.hidden = true;
    elements.progress.hidden = true;
    if (elements.joinConfirm) elements.joinConfirm.hidden = true;
    setWizardStep("map");
    setProfileLocked(false);
    renderTurnstile("create_room");
    setStatus("");
    elements.description.textContent =
      "Host and join from Halo's own Multiplayer menu, or start a quick game here.";
  }

  function showProgress() {
    if (elements.dialog) elements.dialog.dataset.view = "progress";
    if (elements.wizardSteps) elements.wizardSteps.hidden = session.role === "guest";
    elements.setup.hidden = true;
    elements.invite.hidden = true;
    elements.progress.hidden = false;
    if (elements.joinConfirm) elements.joinConfirm.hidden = true;
  }

  function showInvite() {
    if (elements.wizardSteps) elements.wizardSteps.hidden = true;
    elements.setup.hidden = true;
    elements.progress.hidden = true;
    elements.invite.hidden = false;
    if (elements.joinConfirm) elements.joinConfirm.hidden = true;
    if (elements.playerSidebar) elements.playerSidebar.hidden = false;
    elements.inviteLink.value = session.inviteUrl || "";
    /* Hosting setup is complete. The invite remains visible beside the game,
       so dismiss the wizard instead of replacing it with a third screen. */
    if (elements.dialog.open) elements.dialog.close();
  }

  function showJoinConfirmation(invite) {
    session.pendingInvite = invite;
    if (elements.dialog) elements.dialog.dataset.view = "join";
    if (elements.wizardSteps) elements.wizardSteps.hidden = true;
    elements.setup.hidden = true;
    elements.invite.hidden = true;
    elements.progress.hidden = true;
    if (elements.joinConfirm) elements.joinConfirm.hidden = false;
    if (elements.joinSummary) elements.joinSummary.textContent =
      "Choose your name and color, then join your friend's game.";
    elements.description.textContent = "You're invited.";
    setHeader("Ready to join", "waiting");
    setStatus(session.runtimeReady ? "" : "Loading Halo…");
    setProfileLocked(false);
    renderTurnstile("join_room");
    setBusy(false);
  }

  function parseInvite(value) {
    var inviteSignal = null;
    var text = String(value || "").trim();
    if (!text || text.length > 1024) throw new Error("Paste a valid invite link.");
    var nativeInvite = /^halo:\/\/join\/([0-9a-fA-F]{64})$/.exec(text);
    if (nativeInvite) {
      return {
        code: "halo://join/" + nativeInvite[1].toLowerCase(),
        kind: "native",
      };
    }
    try {
      var url = new URL(text);
      if (url.protocol.toLowerCase() === "halo:") {
        if (url.hostname.toLowerCase() !== "join" ||
            !/^\/[0-9a-fA-F]{64}$/.test(url.pathname)) {
          throw new Error("That Halo invite link is not valid.");
        }
        return {
          code: "halo://join/" + url.pathname.slice(1).toLowerCase(),
          kind: "native",
        };
      }
      var fragment = new URLSearchParams(url.hash.replace(/^#/, ""));
      text = fragment.get("join") || "";
      if ((url.protocol === "https:" || url.protocol === "http:") &&
          url.origin !== global.location.origin) {
        /* Directory of the inviting page, e.g. https://other.example/halo */
        inviteSignal = new URL(".", url).href.replace(/\/$/, "");
      }
    } catch (error) {
      if (error && error.message === "That Halo invite link is not valid.") throw error;
      /* A room code is expected not to be a URL. */
    }
    try {
      text = decodeURIComponent(text);
    } catch (error) {
      throw new Error("That invite link is malformed.");
    }
    var separator = text.indexOf(".");
    if (separator <= 0 || separator === text.length - 1) {
      throw new Error("That invite link is incomplete.");
    }
    var roomId = text.slice(0, separator);
    var ticket = text.slice(separator + 1);
    if (!/^[A-Za-z0-9_-]{4,64}$/.test(roomId) ||
        !/^[A-Za-z0-9_-]{16,256}$/.test(ticket)) {
      throw new Error("That invite link is not valid.");
    }
    return { code: text, kind: "web", roomId: roomId, ticket: ticket, signal: inviteSignal };
  }

  function takeInviteFromLocation() {
    var fragment = new URLSearchParams(global.location.hash.replace(/^#/, ""));
    var invite = fragment.get("join");
    if (!invite) return null;
    /* Capabilities in fragments do not reach the server.  Remove it from the
       address bar as soon as this page has copied it into memory. */
    var sanitized = new URL(global.location.href);
    sanitized.hash = "";
    sanitized.searchParams.delete("signal");
    history.replaceState(null, "", sanitized.pathname + sanitized.search);
    return invite;
  }

  function makeInviteUrl(code) {
    var url = new URL(global.location.href);
    url.searchParams.delete("signal");
    url.hash = "join=" + encodeURIComponent(code);
    return url.href;
  }

  async function fetchJson(path, options) {
    var response;
    try {
      response = await fetch(apiBase() + path, Object.assign({
        credentials: "omit",
        headers: { "Content-Type": "application/json" },
      }, options || {}));
    } catch (error) {
      throw new Error("The private-room service is unreachable.");
    }
    var result = null;
    try {
      result = await response.json();
    } catch (error) {
      /* A proxy error page is not useful to the player. */
    }
    if (!response.ok) {
      var message = result && result.error &&
        (result.error.message || (typeof result.error === "string" && result.error));
      if (response.status === 404) message = "That invite expired or is not valid.";
      if (response.status === 409 && !message) message = "That room is full or no longer available.";
      var requestError = new Error(message || "The private-room service rejected the request.");
      requestError.haloCode = result && result.error && result.error.code;
      requestError.haloStatus = response.status;
      throw requestError;
    }
    return result;
  }

  function wasmFunction(name) {
    var fn = global.Module && global.Module["_" + name];
    if (typeof fn !== "function") throw new Error("Halo is still starting.");
    return fn;
  }

  function requestGame(command) {
    if (!wasmFunction("platform_web_online_request")(command)) {
      throw new Error("Halo could not accept the online-play request.");
    }
  }

  function requestConfiguredHost(settings) {
    var accepted;
    if (settings.advanced) {
      var rules = (settings.advanced.infiniteGrenades ? 1 : 0) |
        (settings.advanced.shields ? 2 : 0) |
        (settings.advanced.invisiblePlayers ? 4 : 0) |
        (settings.advanced.otherPlayersOnRadar ? 8 : 0);
      accepted = wasmFunction("platform_web_online_host_advanced_configured")(
        settings.mapIndex,
        settings.modeIndex,
        settings.advanced.scoreToWin,
        settings.advanced.respawnSeconds,
        settings.advanced.lives,
        settings.advanced.healthPercent,
        rules);
    } else {
      accepted = wasmFunction("platform_web_online_host_configured")(
        settings.mapIndex, settings.modeIndex);
    }
    if (!accepted) {
      throw new Error("Halo could not accept those host settings.");
    }
  }

  function gameState() {
    return wasmFunction("platform_web_online_get_state")();
  }

  function gameError() {
    return wasmFunction("platform_web_online_get_error")();
  }

  function setGameTransportState(value) {
    if (!session.runtimeReady) return;
    wasmFunction("platform_web_online_set_transport_state")(value);
  }

  function transport() {
    if (!global.HaloWebTransport || !global.HaloWebTransport.isSupported()) {
      throw new Error("This browser does not support WebRTC multiplayer.");
    }
    return global.HaloWebTransport;
  }

  function localIdentifier() {
    return transport().getLocalIdentifier();
  }

  function wireSignal(signal) {
    if (signal && signal.description) {
      return { kind: "description", description: signal.description };
    }
    if (signal && Object.prototype.hasOwnProperty.call(signal, "candidate")) {
      return { kind: "candidate", candidate: signal.candidate };
    }
    throw new Error("WebRTC produced an unsupported signal.");
  }

  function transportSignal(signal) {
    if (!signal || typeof signal !== "object") throw new Error("The host sent an invalid signal.");
    if (signal.kind === "description") return { description: signal.description };
    if (signal.kind === "candidate") return { candidate: signal.candidate };
    /* Accept the direct transport shape for local/older signalling servers. */
    if (signal.description || Object.prototype.hasOwnProperty.call(signal, "candidate")) return signal;
    throw new Error("The host sent an unsupported signal.");
  }

  function sendSocket(message) {
    if (!session.socket || session.socket.readyState !== WebSocket.OPEN) {
      throw new Error("The room connection is temporarily unavailable.");
    }
    session.socket.send(JSON.stringify(message));
  }

  function configureTransport(iceServers) {
    transport().configure({
      iceServers: iceServers || [],
      onSignal: function(event) {
        if (!session.active || session.closing || !event ||
            !session.peerPromises.has(event.peerId)) return;
        sendSocket({
          v: PROTOCOL_VERSION,
          type: "signal",
          to: session.peerSignalTargets.get(event.peerId) || event.peerId,
          signal: wireSignal(event.signal),
        });
      },
      onStateChange: function(event) {
        handleTransportState(event);
      },
      onError: function(event) {
        var message = event && event.error && event.error.message ?
          event.error.message : "The browser connection failed.";
        if (session.active) setStatus(message, "error");
      },
    });
  }

  function ensurePeer(peer, socketGeneration, operationGeneration) {
    if (!isCurrentSocketOperation(socketGeneration, operationGeneration)) {
      return Promise.resolve(null);
    }
    if (!peer || typeof peer.peerId !== "string" ||
        typeof peer.identifier !== "string" ||
        (peer.role !== "host" && peer.role !== "guest")) {
      return Promise.reject(new Error("The room returned an invalid peer."));
    }
    if (peer.peerId === session.selfPeerId) return Promise.resolve(null);
    /* Halo uses a host-client star. Guests never need guest-to-guest browser
       transports, even though older room services may announce every member. */
    if (peer.role === session.role) return Promise.resolve(null);
    var existing = session.peerPromises.get(peer.peerId);
    if (existing) return existing;
    var normalizedIdentifier = peer.identifier.toLowerCase();
    var connectedDuplicate = null;
    session.peerIdentifiers.forEach(function(identifier, peerId) {
      if (peerId !== peer.peerId && identifier === normalizedIdentifier) {
        if (session.peerStates.get(peerId) === "connected") {
          connectedDuplicate = peerId;
          return;
        }
        /* A refreshed browser receives a new signaling peer ID but retains its
           Halo network identifier. Replace the stale WebRTC transport before
           registering the new one so both cannot share one virtual address. */
        removePeer(peerId);
      }
    });
    if (connectedDuplicate) {
      session.peerAliases.set(peer.peerId, connectedDuplicate);
      session.peerSignalTargets.set(connectedDuplicate, peer.peerId);
      return session.peerPromises.get(connectedDuplicate) || Promise.resolve(null);
    }
    var rawAdding = transport().addPeer({
      peerId: peer.peerId,
      remoteIdentifier: normalizedIdentifier,
      initiator: session.role === "host",
      polite: session.role !== "host",
      iceServers: session.iceServers,
    });
    var adding = rawAdding.then(function(result) {
      if (!isCurrentSocketOperation(socketGeneration, operationGeneration)) {
        if (session.peerPromises.get(peer.peerId) === adding) removePeer(peer.peerId);
        var error = new Error("Peer registration was canceled.");
        error.haloCanceled = true;
        throw error;
      }
      return result;
    });
    session.peerIdentifiers.set(peer.peerId, normalizedIdentifier);
    session.peerPromises.set(peer.peerId, adding);
    session.peerAliases.set(peer.peerId, peer.peerId);
    session.peerSignalTargets.set(peer.peerId, peer.peerId);
    adding.catch(function() {
      if (session.peerPromises.get(peer.peerId) === adding) {
        session.peerPromises.delete(peer.peerId);
        session.peerIdentifiers.delete(peer.peerId);
        session.peerAliases.delete(peer.peerId);
        session.peerSignalTargets.delete(peer.peerId);
      }
    });
    return adding;
  }

  function removePeer(peerId) {
    var transportPeerId = session.peerAliases.get(peerId) || peerId;
    session.peerAliases.forEach(function(mappedPeerId, signalingPeerId) {
      if (mappedPeerId === transportPeerId) session.peerAliases.delete(signalingPeerId);
    });
    session.peerSignalTargets.delete(transportPeerId);
    session.peerPromises.delete(transportPeerId);
    session.peerIdentifiers.delete(transportPeerId);
    session.peerStates.delete(transportPeerId);
    transport().removePeer(transportPeerId);
    updateAggregateTransportState();
  }

  function updateAggregateTransportState() {
    var values = Array.from(session.peerStates.values());
    var connected = values.filter(function(value) { return value === "connected"; }).length;
    var connecting = values.some(function(value) { return value === "connecting"; });
    var failed = values.some(function(value) { return value === "failed"; });
    session.connectedPeerCount = connected;
    session.transportConnected = connected > 0;
    if (session.transportConnected) setGameTransportState(TRANSPORT_STATE.CONNECTED);
    else if (connecting) setGameTransportState(TRANSPORT_STATE.CONNECTING);
    else if (failed) setGameTransportState(TRANSPORT_STATE.FAILED);
    else setGameTransportState(TRANSPORT_STATE.DISCONNECTED);

    if (session.role === "host") {
      if (connected) {
        setHeader(connectedFriendsLabel(connected), "connected");
        setStatus(connected === 1 ?
          "Your friend is connected. Press Start Game in Halo when ready." :
          connected + " friends are connected. Press Start Game in Halo when ready.");
      } else if (session.active) {
        setHeader("Waiting for friends", "waiting");
      }
    }
  }

  function handleTransportState(event) {
    if (!session.active || !event || !event.peerId ||
        !session.peerPromises.has(event.peerId)) return;
    session.peerStates.set(event.peerId, event.state);
    updateAggregateTransportState();
    if (event.state === "connected") {
      if (event.detail === "native-gateway") {
        session.connectionPath = "gateway";
        syncTelemetryContext();
        telemetry("transport_connected", "native-gateway");
        elements.detail.textContent = "Connected to a native Halo host";
      } else {
        determineConnectionPath(event.peerId);
      }
      if (session.role === "guest" && !session.gameCommandIssued) {
        try {
          applyPlayerCustomization(session.profile);
          requestGame(COMMAND.JOIN);
          session.gameCommandIssued = true;
          startGamePolling();
          setStatus("Connected. Finding your friend's Halo lobby…");
        } catch (error) {
          fail(error);
        }
      } else if (session.role === "host") {
        global.setTimeout(function() {
          if (elements.dialog.open && session.active) elements.dialog.close();
          var canvas = byId("canvas");
          if (canvas) canvas.focus();
        }, 700);
      }
    } else if (event.state === "connecting" && session.role === "guest") {
      setStatus(event.detail === "native-gateway" ?
        "Reaching the native Halo host…" : "Connecting directly to your friend…");
    } else if (event.state === "failed" && session.role === "guest") {
      fail(new Error(event.detail || "Could not connect to the host."));
    }
  }

  async function determineConnectionPath(peerId) {
    try {
      await new Promise(function(resolve) { setTimeout(resolve, 500); });
      var reports = await transport().getStats(peerId);
      var selected = null;
      reports.forEach(function(report) {
        if (report.type === "candidate-pair" &&
            (report.selected || (report.nominated && report.state === "succeeded"))) {
          selected = report;
        }
      });
      if (!selected) return;
      var local = reports.get(selected.localCandidateId);
      var remote = reports.get(selected.remoteCandidateId);
      session.connectionPath =
        (local && local.candidateType === "relay") ||
        (remote && remote.candidateType === "relay") ? "relay" : "direct";
      syncTelemetryContext();
      telemetry("transport_connected", session.connectionPath);
      elements.detail.textContent = session.connectionPath === "relay" ?
        "Connected through a privacy-compatible relay" :
        "Connected directly peer-to-peer";
    } catch (error) {
      /* Connection-path reporting is diagnostic and never blocks play. */
    }
  }

  async function handleRoomMessage(message, generation, operation) {
    if (!isCurrentSocketOperation(generation, operation)) return;
    if (!message || message.v !== PROTOCOL_VERSION || typeof message.type !== "string") {
      throw new Error("The room service sent an incompatible message.");
    }
    if (message.type === "welcome") {
      session.selfPeerId = message.self && message.self.peerId;
      session.role = message.self && message.self.role;
      syncTelemetryContext();
      updateLocalRoster();
      var peers = Array.isArray(message.peers) ? message.peers : [];
      await Promise.all(peers.map(function(peer) {
        return ensurePeer(peer, generation, operation);
      }));
      return;
    }
    if (message.type === "peer-joined") {
      var joined = normalizedRosterPlayer(message.peer);
      if (joined) {
        session.roster.set(joined.peerId, joined);
        renderRoster();
      }
      await ensurePeer(message.peer, generation, operation);
      return;
    }
    if (message.type === "peer-left") {
      session.roster.delete(message.peerId);
      renderRoster();
      var departedTransportPeerId = session.peerAliases.get(message.peerId) || message.peerId;
      if (session.peerStates.get(departedTransportPeerId) === "connected") {
        session.peerAliases.delete(message.peerId);
        if (session.peerSignalTargets.get(departedTransportPeerId) === message.peerId) {
          session.peerSignalTargets.delete(departedTransportPeerId);
        }
        elements.detail.textContent =
          "Gameplay is still connected directly; the room link closed.";
        return;
      }
      removePeer(departedTransportPeerId);
      if (session.role === "guest" && message.reason === "host-disconnected") {
        fail(new Error("The host closed the room."));
      }
      return;
    }
    if (message.type === "roster") {
      replaceRoster(message.players);
      return;
    }
    if (message.type === "signal") {
      var transportPeerId = session.peerAliases.get(message.from) || message.from;
      var peerPromise = session.peerPromises.get(transportPeerId);
      if (!peerPromise) {
        /* A rejected guest can have another frame already in flight. It must
           not turn a peer-scoped failure into destruction of the host room. */
        if (session.role === "host") return;
        throw new Error("A signal arrived from an unknown host.");
      }
      try {
        await peerPromise;
        if (!isCurrentSocketOperation(generation, operation)) return;
        await transport().handleSignal(transportPeerId, transportSignal(message.signal));
      } catch (error) {
        if (!isCurrentSocketOperation(generation, operation)) return;
        removePeer(transportPeerId);
        if (session.role === "guest") throw error;
        setStatus("A guest sent invalid connection data and was disconnected.", "error");
      }
      return;
    }
    if (message.type === "error") {
      if (session.role === "host" &&
          (message.code === "PEER_NOT_FOUND" ||
           message.code === "SIGNAL_ROUTE_FORBIDDEN" ||
           message.code === "SIGNAL_DIRECTION_INVALID")) {
        /* A late or rejected guest signal is peer-scoped. The private host
           lobby remains usable for a fresh connection. */
        return;
      }
      throw new Error(message.message || "The private room reported an error.");
    }
    /* pong and future optional messages need no action. */
  }

  function websocketUrl(value) {
    var service = new URL(apiBase());
    var url = new URL(value, service);
    /* Self-hosted: the lobby service only knows its own loopback address, so
       re-root its /v1/... socket paths under the lobby URL this page uses.
       That stays correct behind any reverse proxy or sub-path. */
    if (/^\/v1\//.test(url.pathname)) {
      url = new URL(url.pathname.slice(1) + url.search, service.href.replace(/\/?$/, "/"));
    }
    if (url.protocol === "http:") url.protocol = "ws:";
    if (url.protocol === "https:") url.protocol = "wss:";
    var expectedProtocol = service.protocol === "https:" ? "wss:" : "ws:";
    if (url.protocol !== expectedProtocol || url.host !== service.host) {
      throw new Error("The room returned an invalid WebSocket URL.");
    }
    return url.href;
  }

  function stopHeartbeat() {
    if (session.heartbeatTimer) global.clearInterval(session.heartbeatTimer);
    session.heartbeatTimer = 0;
  }

  function startHeartbeat(generation) {
    stopHeartbeat();
    session.heartbeatTimer = global.setInterval(function() {
      if (generation !== session.socketGeneration || !session.active) return;
      try {
        sendSocket({ v: PROTOCOL_VERSION, type: "ping", nonce: String(Date.now()) });
      } catch (error) {
        /* The close event owns reconnect behavior. */
      }
    }, HEARTBEAT_MILLISECONDS);
  }

  function openSocket(socketUrl, operation) {
    return new Promise(function(resolve, reject) {
      var generation = ++session.socketGeneration;
      var socket;
      try {
        socket = new WebSocket(websocketUrl(socketUrl));
      } catch (error) {
        reject(error);
        return;
      }
      session.socket = socket;
      var settled = false;
      var pendingMessageCount = 0;
      var messageChain = Promise.resolve();
      session.messageChain = messageChain;
      var timeout = global.setTimeout(function() {
        if (!settled) {
          settled = true;
          socket.close();
          reject(new Error("The private room took too long to connect."));
        }
      }, 12000);
      socket.onopen = function() {
        if (!isCurrentSocketOperation(generation, operation)) {
          global.clearTimeout(timeout);
          if (!settled) {
            settled = true;
            reject(new Error("The room connection was canceled."));
          }
          socket.close();
          return;
        }
        global.clearTimeout(timeout);
        try {
          sendSocket({
            v: PROTOCOL_VERSION,
            type: "profile",
            profile: session.profile,
          });
        } catch (error) {
          settled = true;
          socket.close();
          reject(error);
          return;
        }
        settled = true;
        session.reconnectAttempts = 0;
        startHeartbeat(generation);
        resolve();
      };
      socket.onmessage = function(event) {
        if (!isCurrentSocketOperation(generation, operation) || typeof event.data !== "string") return;
        pendingMessageCount++;
        if (pendingMessageCount > MAX_PENDING_SIGNALING_MESSAGES) {
          socket.close(1008, "Too many pending signaling messages");
          fail(new Error("The room sent too many connection messages."));
          return;
        }
        var message;
        try {
          message = JSON.parse(event.data);
        } catch (error) {
          pendingMessageCount--;
          if (isCurrentSocketOperation(generation, operation)) {
            fail(new Error("The private room sent malformed data."));
          }
          return;
        }
        messageChain = messageChain.then(function() {
          return handleRoomMessage(message, generation, operation);
        }).catch(function(error) {
          if (isCurrentSocketOperation(generation, operation)) fail(error);
        }).finally(function() {
          pendingMessageCount--;
        });
        session.messageChain = messageChain;
      };
      socket.onerror = function() {
        if (!settled) {
          global.clearTimeout(timeout);
          settled = true;
          reject(new Error("The private room WebSocket could not connect."));
        }
      };
      socket.onclose = function() {
        if (!settled) {
          global.clearTimeout(timeout);
          settled = true;
          reject(new Error("The room connection closed before it was ready."));
          return;
        }
        if (generation !== session.socketGeneration ||
            operation !== session.operationGeneration) return;
        stopHeartbeat();
        if (session.transportConnected && session.role !== "host") {
          elements.detail.textContent =
            "Gameplay is still connected directly; restoring the room link…";
        }
        if (session.active && !session.closing) scheduleReconnect();
      };
    });
  }

  async function createSession(ticket, turnstileToken) {
    var body = {
      protocolVersion: PROTOCOL_VERSION,
      buildId: buildId(),
      identifier: localIdentifier(),
      ticket: ticket,
    };
    if (turnstileToken) body.turnstileToken = turnstileToken;
    return fetchJson("/v1/rooms/" + encodeURIComponent(session.room.id) + "/sessions", {
      method: "POST",
      body: JSON.stringify(body),
    });
  }

  async function createNativeSession(invite, turnstileToken) {
    var body = {
      protocolVersion: PROTOCOL_VERSION,
      buildId: buildId(),
      identifier: localIdentifier(),
      invite: invite,
    };
    if (turnstileToken) body.turnstileToken = turnstileToken;
    return fetchJson("/v1/native/sessions", {
      method: "POST",
      body: JSON.stringify(body),
    });
  }

  /* Self-hosted: the gateway runs beside the lobby service and is reached
     through it at <lobby>/native-gateway/v1/connect. */
  function gatewaySocketUrl(value) {
    var url = new URL(value);
    if (url.hostname !== "halo-native-gateway.invalid") return value;
    var base = new URL(apiBase().replace(/\/?$/, "/"));
    base.protocol = base.protocol === "https:" ? "wss:" : "ws:";
    return new URL("native-gateway" + url.pathname + url.search, base).href;
  }

  function validateNativeSessionResponse(result) {
    var descriptor = result && result.session;
    if (!result || result.v !== PROTOCOL_VERSION || result.kind !== "native" ||
        !descriptor || typeof descriptor.peerId !== "string" ||
        !/^[0-9a-f]{12}$/.test(descriptor.localIdentifier || "") ||
        !/^[0-9a-f]{12}$/.test(descriptor.remoteIdentifier || "") ||
        !/^wss?:\/\//i.test(descriptor.websocketUrl || "") ||
        !/^[A-Za-z0-9_-]{32,128}$/.test(descriptor.ticket || "")) {
      throw new Error("The native gateway returned an incomplete response.");
    }
  }

  async function connectNativeInvite(invite, turnstileToken, operation) {
    var result = await createNativeSession(invite.code, turnstileToken);
    requireCurrentOperation(operation);
    validateNativeSessionResponse(result);
    var descriptor = result.session;
    await transport().setLocalIdentifier(descriptor.localIdentifier);
    session.room = { id: "native" };
    session.roomTicket = null;
    /* (the invite's digits: the joined game's, for Delta Stats) */
    session.nativeInvite = invite.code.slice("halo://join/".length);
    session.selfPeerId = "native-self-" + descriptor.localIdentifier;
    session.connectionPath = "gateway";
    syncTelemetryContext();
    updateLocalRoster();
    session.roster.set(descriptor.peerId, {
      peerId: descriptor.peerId,
      profile: null,
      role: "host",
    });
    renderRoster();
    configureTransport([]);
    var adding = transport().addGatewayPeer({
      peerId: descriptor.peerId,
      remoteIdentifier: descriptor.remoteIdentifier,
      ticket: descriptor.ticket,
      websocketUrl: gatewaySocketUrl(descriptor.websocketUrl),
    });
    session.peerIdentifiers.set(descriptor.peerId, descriptor.remoteIdentifier);
    session.peerPromises.set(descriptor.peerId, adding);
    session.peerAliases.set(descriptor.peerId, descriptor.peerId);
    try {
      await adding;
      requireCurrentOperation(operation);
    } catch (error) {
      if (session.peerPromises.get(descriptor.peerId) === adding) {
        session.peerPromises.delete(descriptor.peerId);
        session.peerIdentifiers.delete(descriptor.peerId);
        session.peerAliases.delete(descriptor.peerId);
      }
      throw error;
    }
    setGameTransportState(TRANSPORT_STATE.CONNECTING);
    setStatus("Invite accepted. Reaching the native host…");
  }

  function scheduleReconnect() {
    if (!session.active || session.closing || session.reconnectTimer) return;
    var operation = session.operationGeneration;
    var delay = Math.min(8000, 500 * Math.pow(2, session.reconnectAttempts++));
    session.reconnectTimer = global.setTimeout(function() {
      session.reconnectTimer = 0;
      if (operation !== session.operationGeneration || !session.active) return;
      reconnect(operation).catch(function(error) {
        if (operation !== session.operationGeneration) return;
        if (session.transportConnected) {
          elements.detail.textContent = "Gameplay is connected; room recovery is still retrying.";
          scheduleReconnect();
        /* A failed WebSocket upgrade can leave its 30-second server-side
           reservation in place. Keep retrying long enough to outlive it. */
        } else if (session.reconnectAttempts < 7) {
          scheduleReconnect();
        } else {
          fail(error);
        }
      });
    }, delay);
  }

  async function reconnect(operation) {
    if (!session.transportConnected) {
      /* The replacement signaling session gets a new peer ID. Any WebRTC
         negotiation that never connected belongs to the old identity and
         must be rebuilt so the host produces a fresh offer. */
      Array.from(session.peerPromises.keys()).forEach(removePeer);
    }
    var result = await createSession(session.roomTicket);
    requireCurrentOperation(operation);
    if (Array.isArray(result.iceServers)) session.iceServers = result.iceServers;
    configureTransport(session.iceServers);
    await openSocket(result.session.websocketUrl, operation);
    requireCurrentOperation(operation);
    elements.detail.textContent = session.connectionPath === "relay" ?
      "Connected through a relay" : "Connected peer-to-peer";
  }

  function validateRoomResponse(result) {
    if (!result || result.v !== PROTOCOL_VERSION || !result.room ||
        !result.session || !result.session.websocketUrl ||
        !result.session.peerId) {
      throw new Error("The room service returned an incomplete response.");
    }
  }

  function isTurnstileRejection(error) {
    return error && error.haloStatus === 403 && error.haloCode === "TURNSTILE_REJECTED";
  }

  async function recoverTurnstile(action, invite) {
    resetTurnstile();
    await leave(false);
    showDialog();
    if (action === "join_room") showJoinConfirmation(invite);
    else showSetup();
    setVerificationState(
      "error",
      "We couldn't verify you this time. Try again — you won't need to refresh.");
    setBusy(false);
  }

  async function host(value, turnstileToken) {
    if (!session.runtimeReady) throw new Error("Halo is still starting.");
    var settings = normalizeHostSettings(value);
    var profile = readPlayerProfile();
    saveHostSettings(settings);
    savePlayerProfile(profile);
    await leave(false);
    var operation = ++session.operationGeneration;
    session.active = true;
    session.role = "host";
    syncTelemetryContext();
    session.hostSettings = settings;
    session.closing = false;
    renderRoster();
    showDialog();
    showProgress();
    setBusy(true);
    setHeader("Opening room…", "waiting");
    setStatus("Preparing " + hostSettingsLabel() + "…");
    var recoveredVerification = false;
    try {
      var roomRequest = {
        protocolVersion: PROTOCOL_VERSION,
        buildId: buildId(),
        capacity: ROOM_CAPACITY,
        identifier: localIdentifier(),
      };
      if (turnstileToken) roomRequest.turnstileToken = turnstileToken;
      var result = await fetchJson("/v1/rooms", {
        method: "POST",
        body: JSON.stringify(roomRequest),
      });
      requireCurrentOperation(operation);
      var normalized = {
        v: result.v,
        room: result.room,
        session: result.host && result.host.session,
      };
      validateRoomResponse(normalized);
      session.room = result.room;
      session.roomTicket = result.host.ticket;
      session.selfPeerId = result.host.session.peerId;
      updateLocalRoster();
      session.inviteCode = result.invite && result.invite.code;
      if (!session.inviteCode) throw new Error("The room did not return an invite.");
      /* Keep the visible host and path that the player opened. This lets the
         same signaling service support a staged origin without leaking its
         canonical production URL into preview invites. */
      session.inviteUrl = makeInviteUrl(session.inviteCode);
      showInvite();
      session.iceServers = Array.isArray(result.iceServers) ? result.iceServers : [];
      configureTransport(session.iceServers);
      await openSocket(result.host.session.websocketUrl, operation);
      requireCurrentOperation(operation);
      applyPlayerCustomization(profile);
      requestConfiguredHost(settings);
      session.gameCommandIssued = true;
      startGamePolling();
      setHeader("Preparing lobby…", "waiting");
      setStatus("Opening Halo's lobby with " + hostSettingsLabel() + "…");
    } catch (error) {
      if (operation === session.operationGeneration && (!error || !error.haloCanceled)) {
        if (isTurnstileRejection(error)) {
          recoveredVerification = true;
          await recoverTurnstile("create_room");
        } else {
          fail(error);
        }
      }
    } finally {
      if (!recoveredVerification) resetTurnstile();
      if (operation === session.operationGeneration) setBusy(false);
    }
  }

  /* The game's own menus made a server (Multiplayer > Create Game:
   * src/web_online_ui.c, web_online_game_hosting): a room for it, as host()
   * opens, but the game keeps the server and lobby it made (host() has the
   * game make its own), and nothing is asked: the invite shows beside the
   * game and in Server Setup (web_online_invite_set). Backing out of the
   * game's lobby ends the room (pollGame: IDLE). */
  async function hostFromGame(options) {
    if (!session.runtimeReady) return;
    if (session.active && session.role === "host" && session.fromGame && session.inviteUrl) {
      /* (another Create Game while its room is open: the same room) */
      sendInviteToGame();
      return;
    }
    if (session.active) {
      /* (a room this browser was in: left, without the reset of the game a
       * leave asks for, which would end the server just made) */
      session.gameCommandIssued = false;
      await leave(false);
    }
    var operation = ++session.operationGeneration;
    session.active = true;
    session.role = "host";
    session.fromGame = true;
    session.hostSettings = null;
    session.closing = false;
    syncTelemetryContext();
    renderRoster();
    setHeader("Opening room…", "waiting");
    try {
      var roomRequest = {
        protocolVersion: PROTOCOL_VERSION,
        buildId: buildId(),
        capacity: ROOM_CAPACITY,
        identifier: localIdentifier(),
      };
      var turnstileToken = consumeTurnstileIfReady("create_room");
      if (turnstileToken) roomRequest.turnstileToken = turnstileToken;
      var result = await fetchJson("/v1/rooms", {
        method: "POST",
        body: JSON.stringify(roomRequest),
      });
      requireCurrentOperation(operation);
      validateRoomResponse({ v: result.v, room: result.room, session: result.host && result.host.session });
      session.room = result.room;
      session.roomTicket = result.host.ticket;
      session.selfPeerId = result.host.session.peerId;
      updateLocalRoster();
      session.inviteCode = result.invite && result.invite.code;
      if (!session.inviteCode) throw new Error("The room did not return an invite.");
      session.inviteUrl = makeInviteUrl(session.inviteCode);
      session.iceServers = Array.isArray(result.iceServers) ? result.iceServers : [];
      configureTransport(session.iceServers);
      await openSocket(result.host.session.websocketUrl, operation);
      requireCurrentOperation(operation);
      sendInviteToGame();
      showInvite();
      session.hostWasReady = true;
      session.gameCommandIssued = true;
      startGamePolling();
      setHeader("Waiting for friends", "waiting");
    } catch (error) {
      if (operation === session.operationGeneration && (!error || !error.haloCanceled)) {
        /* (the game keeps its server: a LAN game of this browser alone) */
        session.gameCommandIssued = false;
        fail(error);
      }
    }
  }

  /* A game of the menus' own, PUBLIC in Server Setup: its listing, to the
   * lobby service's list of public games (GET /v1/public-rooms, which other
   * browsers' in-game Server Browser shows), when the game changes it and
   * every 30 seconds (or the listing lapses); no longer public: taken off. The
   * room's guest ticket comes with it, proving the invite the list gives. */
  var LISTING_REFRESH_MILLISECONDS = 30000;

  function syncListing() {
    if (!session.fromGame || !session.socket || session.socket.readyState !== 1 || !session.inviteCode) return;
    var changes;
    try {
      changes = wasmFunction("web_online_listing_changes")();
    } catch (error) {
      return;
    }
    var now = Date.now();
    if (changes === session.listingChanges &&
        (!session.listingSent || now - session.listingSentAt < LISTING_REFRESH_MILLISECONDS)) return;
    var game;
    try {
      var buffer = wasmFunction("web_online_listing_buffer")() >>> 0;
      var memory = typeof wasmMemory !== "undefined" ? wasmMemory : null;
      var heap = new Uint8Array(memory ? memory.buffer : global.Module.HEAPU8.buffer);
      var end = buffer;
      while (heap[end] && end - buffer < 1024) end++;
      game = JSON.parse(new TextDecoder().decode(heap.slice(buffer, end)));
    } catch (error) {
      return;
    }
    session.listingChanges = changes;
    var listed = !!(game && game.listed);
    if (!listed && !session.listingSent) return;
    session.listingSentAt = now;
    session.listingSent = listed;
    try {
      sendSocket({
        v: PROTOCOL_VERSION,
        type: "listing",
        guestTicket: session.inviteCode.slice(session.inviteCode.indexOf(".") + 1),
        listing: listed ? {
          name: String(game.name || ""), map: String(game.map || ""), gametype: String(game.gametype || ""),
          players: game.players | 0, maximum: game.maximum | 0,
          open: !!game.open, inProgress: !!game.inProgress, hasTeams: !!game.hasTeams,
        } : null,
      });
    } catch (error) {
      /* (the socket closing: the listing lapses) */
    }
  }

  /* the room's invite link, to the game (Server Setup shows it, and copies
   * it: p2p_invite_link) */
  function sendInviteToGame() {
    try {
      var bytes = new TextEncoder().encode(session.inviteUrl || "");
      var buffer = wasmFunction("web_online_invite_buffer")() >>> 0;
      var capacity = wasmFunction("web_online_invite_buffer_size")();
      if (!bytes.length || bytes.length > capacity) return;
      var memory = typeof wasmMemory !== "undefined" ? wasmMemory : null;
      new Uint8Array(memory ? memory.buffer : global.Module.HEAPU8.buffer).set(bytes, buffer);
      wasmFunction("web_online_invite_set")(bytes.length);
    } catch (error) {
      /* (an older game: the page alone shows the invite) */
    }
  }

  async function join(value, turnstileToken) {
    if (!session.runtimeReady) {
      showDialog();
      showJoinConfirmation(value);
      return;
    }
    var profile = readPlayerProfile();
    savePlayerProfile(profile);
    await leave(false);
    var operation = ++session.operationGeneration;
    var invite;
    var recoveredVerification = false;
    try {
      invite = parseInvite(value);
    } catch (error) {
      fail(error);
      return;
    }
    session.active = true;
    session.role = "guest";
    syncTelemetryContext();
    session.closing = false;
    session.room = invite.kind === "native" ? { id: "native" } : { id: invite.roomId };
    session.roomTicket = invite.kind === "native" ? null : invite.ticket;
    session.profile = profile;
    writePlayerProfile(profile);
    showDialog();
    showProgress();
    setBusy(true);
    setHeader("Joining friend…", "waiting");
    setStatus("Opening your friend's private room…");
    try {
      if (invite.kind === "native") {
        await connectNativeInvite(invite, turnstileToken, operation);
        return;
      }
      session.apiBaseOverride = invite.signal || null;
      var result;
      try {
        result = await createSession(invite.ticket, turnstileToken);
      } catch (error) {
        if (!session.apiBaseOverride ||
            (error && error.haloStatus && error.haloCode !== "ORIGIN_FORBIDDEN")) throw error;
        session.apiBaseOverride = null;
        result = await createSession(invite.ticket, turnstileToken);
      }
      requireCurrentOperation(operation);
      validateRoomResponse(result);
      session.room = result.room;
      session.selfPeerId = result.session.peerId;
      updateLocalRoster();
      session.iceServers = Array.isArray(result.iceServers) ? result.iceServers : [];
      configureTransport(session.iceServers);
      await openSocket(result.session.websocketUrl, operation);
      requireCurrentOperation(operation);
      setGameTransportState(TRANSPORT_STATE.CONNECTING);
      setStatus("Room found. Connecting directly to your friend…");
    } catch (error) {
      if (operation === session.operationGeneration && (!error || !error.haloCanceled)) {
        if (isTurnstileRejection(error)) {
          recoveredVerification = true;
          await recoverTurnstile("join_room", value);
        } else {
          fail(error);
        }
      }
    } finally {
      if (!recoveredVerification) resetTurnstile();
      if (operation === session.operationGeneration) setBusy(false);
    }
  }

  function startGamePolling() {
    if (session.gamePollTimer) return;
    session.gamePollTimer = global.setInterval(pollGame, GAME_POLL_MILLISECONDS);
  }

  function stopGamePolling() {
    if (session.gamePollTimer) global.clearInterval(session.gamePollTimer);
    session.gamePollTimer = 0;
  }

  function pollGame() {
    if (!session.active || !session.runtimeReady || !session.gameCommandIssued) return;
    var state;
    try {
      state = gameState();
    } catch (error) {
      return;
    }
    elements.dialog.dataset.gameState = String(state);
    if (state === GAME_STATE.ERROR) {
      fail(new Error(GAME_ERRORS[gameError()] || "Halo could not enter the online lobby."));
      return;
    }
    if (session.role === "host") {
      if (state === GAME_STATE.HOSTING) {
        syncListing();
        if (!session.hostWasReady) showInvite();
        session.hostWasReady = true;
        setHeader(session.connectedPeerCount ?
          connectedFriendsLabel(session.connectedPeerCount) : "Waiting for friends",
          session.connectedPeerCount ? "connected" : "waiting");
        setStatus(session.connectedPeerCount ?
          connectedFriendsLabel(session.connectedPeerCount) + ". " + hostSettingsLabel() +
            " is ready — press Start Game in Halo." :
          hostSettingsLabel() + " is ready — send the invite link to your friends.");
      } else if (state === GAME_STATE.WAITING) {
        setStatus("Waiting for Halo's main menu…");
      } else if (state === GAME_STATE.HOST_STARTING) {
        setStatus("Opening Halo's multiplayer lobby…");
      } else if (session.hostWasReady && state === GAME_STATE.IDLE) {
        leave(true);
      }
      return;
    }
    if (state === GAME_STATE.WAITING) {
      setStatus("Waiting for Halo's main menu…");
    } else if (state === GAME_STATE.JOIN_SEARCHING) {
      setStatus("Connected. Finding your friend's Halo lobby…");
    } else if (state === GAME_STATE.JOIN_CONNECTING) {
      setStatus("Halo found the lobby. Joining…");
    } else if (state === GAME_STATE.JOINED) {
      session.guestWasJoined = true;
      setHeader("Connected to friend", "connected");
      setStatus("You're in the lobby.");
      global.setTimeout(function() {
        if (elements.dialog.open && session.active) elements.dialog.close();
        var canvas = byId("canvas");
        if (canvas) canvas.focus();
      }, 700);
    } else if (session.guestWasJoined && state === GAME_STATE.IDLE) {
      leave(true);
    }
  }

  function resetSessionState() {
    session.apiBaseOverride = null;
    session.nativeInvite = null;
    session.active = false;
    session.role = null;
    session.fromGame = false;
    session.listingChanges = -1;
    session.listingSent = false;
    session.listingSentAt = 0;
    session.room = null;
    session.roomTicket = null;
    session.inviteCode = null;
    session.inviteUrl = null;
    session.selfPeerId = null;
    session.iceServers = [];
    session.socket = null;
    session.reconnectAttempts = 0;
    session.gameCommandIssued = false;
    session.transportConnected = false;
    session.connectedPeerCount = 0;
    session.connectionPath = null;
    session.peerPromises.clear();
    session.peerIdentifiers.clear();
    session.peerStates.clear();
    session.peerAliases.clear();
    session.peerSignalTargets.clear();
    session.roster.clear();
    session.messageChain = Promise.resolve();
    session.hostWasReady = false;
    session.hostSettings = null;
    session.guestWasJoined = false;
    session.pendingInvite = null;
    session.joinRequested = false;
    session.wizardStep = "map";
    syncTelemetryContext();
    renderRoster();
  }

  async function leave(returnToSetup) {
    session.operationGeneration++;
    if (session.leavePromise) return session.leavePromise;
    session.leavePromise = (async function() {
      session.closing = true;
      var pendingWork = [session.messageChain].concat(Array.from(session.peerPromises.values()));
      if (session.role === "host" && session.room && session.roomTicket) {
        fetchJson("/v1/rooms/" + encodeURIComponent(session.room.id), {
          method: "DELETE",
          body: JSON.stringify({ ticket: session.roomTicket }),
        }).catch(function() {
          /* The room expires automatically if revocation cannot reach the service. */
        });
      }
      stopHeartbeat();
      stopGamePolling();
      if (session.reconnectTimer) global.clearTimeout(session.reconnectTimer);
      session.reconnectTimer = 0;
      session.socketGeneration++;
      if (session.socket) {
        try { session.socket.close(1000, "left room"); } catch (error) { /* closed */ }
      }
      if (global.HaloWebTransport) global.HaloWebTransport.disconnectAll();
      await Promise.allSettled(pendingWork);
      /* A peer registration can finish after the first disconnectAll(). Clear
         it before a replacement room is allowed to start. */
      if (global.HaloWebTransport) global.HaloWebTransport.disconnectAll();
      if (session.runtimeReady && session.gameCommandIssued) {
        try { requestGame(COMMAND.CANCEL); } catch (error) { /* runtime shutting down */ }
      }
      try { setGameTransportState(TRANSPORT_STATE.DISCONNECTED); } catch (error) { /* runtime unavailable */ }
      resetSessionState();
      setHeader("Play online", "offline");
      elements.detail.textContent = "Private invite room · gameplay connects peer-to-peer when possible";
      if (returnToSetup !== false) {
        showSetup();
        setBusy(false);
      }
    })();
    try {
      await session.leavePromise;
    } finally {
      session.leavePromise = null;
      session.closing = false;
    }
  }

  function fail(error) {
    var message = error && error.message ? error.message : "Online play failed.";
    telemetry("online_error", "online");
    var wasActive = session.active;
    leave(false).then(function() {
      showDialog();
      showSetup();
      setStatus(message, "error");
      setBusy(false);
    });
    if (!wasActive) {
      showDialog();
      showSetup();
      setStatus(message, "error");
    }
  }

  async function copyInvite() {
    var value = session.inviteUrl;
    if (!value) return;
    var copied = false;
    try {
      if (!navigator.clipboard || typeof navigator.clipboard.writeText !== "function") {
        throw new Error("Clipboard API unavailable");
      }
      await navigator.clipboard.writeText(value);
      copied = true;
    } catch (error) {
      elements.inviteLink.focus();
      elements.inviteLink.select();
      try {
        copied = typeof document.execCommand === "function" &&
          document.execCommand("copy") === true;
      } catch (fallbackError) {
        copied = false;
      }
    }
    if (!copied) {
      elements.copy.textContent = "Copy link";
      if (elements.copyStatus) {
        elements.copyStatus.textContent = "Link selected — press ⌘/Ctrl+C to copy.";
        elements.copyStatus.hidden = false;
      }
      return;
    }
    if (elements.copyStatus) elements.copyStatus.hidden = true;
    elements.copy.textContent = "Copied!";
    global.setTimeout(function() { elements.copy.textContent = "Copy link"; }, 1400);
  }

  function attachEvents() {
    ["keydown", "keyup", "keypress"].forEach(function(type) {
      elements.dialog.addEventListener(type, containDialogKeyboardEvent);
      elements.playerSidebar.addEventListener(type, containDialogKeyboardEvent);
    });
    attachPickerEvents(elements.mapOptions, "halo-map-choice", elements.map);
    /* (Find a map: the cards whose name has what is typed) */
    if (elements.mapSearch) {
      elements.mapSearch.addEventListener("input", function() {
        var wanted = elements.mapSearch.value.trim().toLowerCase();
        Array.prototype.forEach.call(elements.mapOptions.querySelectorAll("[data-picker-option]"), function(card) {
          var name = card.querySelector(".choice-label");
          card.hidden = !!wanted && !(name && name.textContent.toLowerCase().indexOf(wanted) >= 0);
        });
      });
    }
    attachPickerEvents(
      elements.modeOptions,
      "halo-mode-choice",
      elements.mode,
      resetAdvancedDefaultsForMode);
    if (elements.advancedEnabled) {
      elements.advancedEnabled.addEventListener("change", function() {
        syncAdvancedSettingsState(false);
      });
    }
    elements.button.addEventListener("click", function() {
      if (session.active && session.role === "host" && session.hostWasReady) {
        showInvite();
        if (elements.inviteLink) elements.inviteLink.focus();
        return;
      }
      showDialog();
      if (!session.active && session.pendingInvite) showJoinConfirmation(session.pendingInvite);
      else if (!session.active) showSetup();
      else showProgress();
    });
    elements.close.addEventListener("click", function() {
      session.joinRequested = false;
      elements.dialog.close();
    });
    elements.dialog.addEventListener("cancel", function(event) {
      event.preventDefault();
      session.joinRequested = false;
      elements.dialog.close();
    });
    elements.hostForm.addEventListener("submit", function(event) {
      event.preventDefault();
      if (session.wizardStep === "map" && elements.stepMap) {
        try {
          validatedIndex(elements.map.value, LAST_MAP_INDEX, elements.map, "map");
          setWizardStep("mode");
          setStatus("");
        } catch (error) {
          setStatus(error.message, "error");
        }
        return;
      }
      try {
        host(undefined, consumeTurnstile("create_room")).catch(fail);
      } catch (error) {
        setStatus(error.message, "error");
      }
    });
    if (elements.mapNext) {
      elements.mapNext.addEventListener("click", function() {
        try {
          validatedIndex(elements.map.value, LAST_MAP_INDEX, elements.map, "map");
          setWizardStep("mode");
          setStatus("");
        } catch (error) {
          setStatus(error.message, "error");
        }
      });
    }
    if (elements.modeBack) {
      elements.modeBack.addEventListener("click", function() {
        setWizardStep("map");
        setStatus("");
      });
    }
    elements.joinForm.addEventListener("submit", function(event) {
      event.preventDefault();
      try {
        var invite = parseInvite(elements.code.value);
        showDialog();
        showJoinConfirmation(invite.code);
      } catch (error) {
        setStatus(error.message, "error");
      }
    });
    if (elements.joinProfile) {
      elements.joinProfile.addEventListener("click", function() {
        requestJoinFromProfile();
      });
    }
    if (elements.verificationRetry) {
      elements.verificationRetry.addEventListener("click", function() {
        var action = elements.dialog && elements.dialog.dataset.view === "join" ?
          "join_room" : "create_room";
        setStatus("");
        renderTurnstile(action, true);
      });
    }
    var updateProfilePreview = function() {
      try {
        var profile = readPlayerProfile();
        session.profile = profile;
        renderPlayerProfilePreview(profile);
        try {
          global.localStorage.setItem(PLAYER_PROFILE_STORAGE_KEY, JSON.stringify(profile));
        } catch (error) { /* Persistence is optional. */ }
      } catch (error) {
        if (elements.profilePreviewName && elements.playerName) {
          elements.profilePreviewName.textContent = elements.playerName.value || "Player";
        }
      }
    };
    if (elements.playerName) elements.playerName.addEventListener("input", updateProfilePreview);
    if (elements.styleOptions) elements.styleOptions.addEventListener("change", updateProfilePreview);
    elements.copy.addEventListener("click", function() {
      copyInvite().catch(function() {
        elements.inviteLink.focus();
        elements.inviteLink.select();
        if (elements.copyStatus) {
          elements.copyStatus.textContent = "Link selected — press ⌘/Ctrl+C to copy.";
          elements.copyStatus.hidden = false;
        }
      });
    });
    elements.leaveHost.addEventListener("click", function() { leave(true).catch(fail); });
    elements.cancel.addEventListener("click", function() { leave(true).catch(fail); });
  }

  function initialize() {
    collectElements();
    restoreHostSettings();
    addCustomMapChoices();
    restorePlayerProfile();
    attachEvents();
    renderRoster();
    setBusy(false);
    startPresencePolling();
    startPublicGamesPolling();
    startDeltaLegacyTable();
    startDeltaProfile();
    session.pendingInvite = takeInviteFromLocation();
    if (session.pendingInvite) {
      showDialog();
      showJoinConfirmation(session.pendingInvite);
    }
  }

  global.HaloOnline = Object.freeze({
    runtimeReady: function() {
      session.runtimeReady = true;
      setBusy(false);
      try {
        transport();
      } catch (error) {
        fail(error);
        return;
      }
      if (session.pendingInvite) {
        showJoinConfirmation(session.pendingInvite);
        maybeStartRequestedJoin();
      }
    },
    host: host,
    hostFromGame: hostFromGame,
    /* a browser's public game chosen in the in-game Server Browser
     * (src/web_public_games.c): its room, joined as an invite to it is */
    joinPublicRoom: function(roomId) {
      var code = session.publicRooms && session.publicRooms.get(roomId);
      if (!code) {
        console.warn("web online: the public room " + roomId + " is not one this page has listed");
        return Promise.resolve();
      }
      return join(makeInviteUrl(code)).catch(fail);
    },
    join: join,
    leave: function() { return leave(true); },
    /* a joined game's report (src/web_delta_stats.c) */
    deltaGameReport: deltaGameReport,
  });

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initialize, { once: true });
  } else {
    initialize();
  }
})(typeof window !== "undefined" ? window : null);
