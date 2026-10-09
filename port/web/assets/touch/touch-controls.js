/*
TOUCH-CONTROLS.JS

On-screen controls for phones and tablets, drawn over the game.

The layouts (a modern mobile-shooter layout and the original Xbox
controller's), the touch handling and the layout editor are ported from Halo
Mobile's port/web/site/input.js (https://github.com/OMG-Guest/Halo-Mobile,
CC0). There the controls write into Halo Mobile's own shared input buffer;
here they reach the game through the paths this port already has:

- the buttons and the left stick are a virtual controller that SDL finds
  through navigator.getGamepads() (and the gamepadconnected event), exactly
  like a real Xbox controller in Chrome;
- dragging a finger aims as the mouse does: platform_web_touch_look
  (port/linux/src/xinput_sdl.c) adds the motion to the mouse-look
  accumulator, so aiming is direct, not a slow virtual right stick.

Touch mode lays the game over the whole screen (touch-controls.css). It
starts by itself on phones once the game runs, from the footer's Touch
button elsewhere, or always with ?touch=1. A real controller hides the
controls while it is connected.
*/
(function haloTouchControls(global) {
  "use strict";

  if (typeof document === "undefined" || typeof navigator === "undefined") return;

  // ---------- settings (this device only)

  const SETTINGS_KEY = "halo-touch-settings";
  const settings = Object.assign(
    { layout: "modern", sensitivity: 1.4, custom: { modern: {}, xbox: {} }, opacity: { modern: 1, xbox: 1 } },
    readSettings());

  function readSettings() {
    try {
      return JSON.parse(localStorage.getItem(SETTINGS_KEY) || "{}") || {};
    } catch (_error) {
      return {};
    }
  }

  function saveSettings() {
    try {
      localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
    } catch (_error) {
      /* private browsing: the settings last until the page closes */
    }
  }

  // ---------- the virtual controller

  // The Gamepad API's standard mapping: the button indices.
  const BUTTON = {
    SOUTH: 0, EAST: 1, WEST: 2, NORTH: 3, LEFT_SHOULDER: 4, RIGHT_SHOULDER: 5,
    LEFT_TRIGGER: 6, RIGHT_TRIGGER: 7, BACK: 8, START: 9, LEFT_STICK: 10, RIGHT_STICK: 11,
    DPAD_UP: 12, DPAD_DOWN: 13, DPAD_LEFT: 14, DPAD_RIGHT: 15, GUIDE: 16,
  };
  // A press shorter than this still reaches the game: SDL samples the pad
  // once a frame, and a quick tap could fall between two samples.
  const MINIMUM_PRESS_MS = 90;

  const nativeGetGamepads = typeof navigator.getGamepads === "function" ?
    navigator.getGamepads.bind(navigator) : null;
  const virtualPad = {
    id: "Xbox 360 Controller (XInput STANDARD GAMEPAD)",
    index: 4,
    connected: false,
    mapping: "standard",
    timestamp: 0,
    axes: [0, 0, 0, 0],
    buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })),
    vibrationActuator: null,
    hapticActuators: [],
  };
  const heldSince = new Array(17).fill(0); // when each button went down
  const releaseAt = new Array(17).fill(0); // a short press's earliest release

  function realGamepads() {
    if (!nativeGetGamepads) return [];
    try {
      return Array.from(nativeGetGamepads() || []);
    } catch (_error) {
      return [];
    }
  }

  function physicalControllerCount() {
    return realGamepads().filter(pad => pad && pad.connected).length;
  }

  if (nativeGetGamepads) {
    navigator.getGamepads = function haloGetGamepads() {
      const pads = realGamepads();
      if (!virtualPad.connected) return pads;
      // past every real slot, so a controller plugged in later keeps its own
      virtualPad.index = Math.max(4, virtualPad.index, pads.length);
      while (pads.length < virtualPad.index) pads.push(null);
      releaseShortPresses();
      pads[virtualPad.index] = virtualPad;
      return pads;
    };
  }

  function touchStamp() {
    virtualPad.timestamp = performance.now();
  }

  function setButton(index, down) {
    const button = virtualPad.buttons[index];
    const now = performance.now();
    if (down) {
      releaseAt[index] = 0;
      if (!button.pressed) heldSince[index] = now;
      button.pressed = button.touched = true;
      button.value = 1;
    } else if (button.pressed) {
      if (now - heldSince[index] < MINIMUM_PRESS_MS) {
        releaseAt[index] = heldSince[index] + MINIMUM_PRESS_MS;
        return;
      }
      button.pressed = button.touched = false;
      button.value = 0;
    }
    touchStamp();
  }

  function releaseShortPresses() {
    const now = performance.now();
    for (let index = 0; index < releaseAt.length; index++) {
      if (releaseAt[index] && now >= releaseAt[index]) {
        releaseAt[index] = 0;
        const button = virtualPad.buttons[index];
        button.pressed = button.touched = false;
        button.value = 0;
        touchStamp();
      }
    }
  }

  function setStick(x, y) {
    virtualPad.axes[0] = x;
    virtualPad.axes[1] = y;
    touchStamp();
  }

  function releaseEverything() {
    for (let index = 0; index < virtualPad.buttons.length; index++) {
      const button = virtualPad.buttons[index];
      button.pressed = button.touched = false;
      button.value = 0;
      releaseAt[index] = 0;
    }
    virtualPad.axes.fill(0);
    touchStamp();
  }

  function gamepadEvent(type) {
    const event = new Event(type);
    event.gamepad = virtualPad;
    global.dispatchEvent(event);
  }

  function connectVirtualPad(connected) {
    if (virtualPad.connected === connected || !nativeGetGamepads) return;
    releaseEverything();
    virtualPad.connected = connected;
    gamepadEvent(connected ? "gamepadconnected" : "gamepaddisconnected");
  }

  // ---------- aiming

  function look(dx, dy) {
    const module = global.Module;
    const lookFn = module && module._platform_web_touch_look;
    if (typeof lookFn !== "function") return;
    const scale = settings.sensitivity * 2;
    try {
      lookFn(dx * scale, dy * scale);
    } catch (_error) {
      /* the runtime is not ready yet */
    }
  }

  // ---------- the layouts (from Halo Mobile)

  // Line icons for the modern layout (24 by 24, drawn with the current colour)
  const ICON = {
    fire: '<circle cx="12" cy="12" r="7"/><path d="M12 2v5M12 17v5M2 12h5M17 12h5"/><circle cx="12" cy="12" r="1.2" fill="currentColor"/>',
    scope: '<circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="3"/><path d="M12 4v5M12 15v5M4 12h5M15 12h5"/>',
    jump: '<path d="M12 19V6M6 11l6-6 6 6"/><path d="M5 21h14"/>',
    crouch: '<path d="M12 4v11M6 10l6 6 6-6"/><path d="M5 21h14"/>',
    reload: '<path d="M19 12a7 7 0 1 1-2.05-4.95"/><path d="M19 4v4h-4"/>',
    grenade: '<circle cx="12" cy="14" r="6"/><path d="M10 8V5h4v3M14 5l4-2"/>',
    grenadeSwap: '<circle cx="9" cy="14" r="5"/><path d="M8 9V6h2v3"/><path d="M16 5h5M19 3l2 2-2 2M21 11h-5M18 9l-2 2 2 2"/>',
    melee: '<path d="M7 13V8a1.5 1.5 0 0 1 3 0v3M10 11V6.5a1.5 1.5 0 0 1 3 0V11M13 11V7.5a1.5 1.5 0 0 1 3 0V12M16 12V9.5a1.5 1.5 0 0 1 3 0V14a6 6 0 0 1-6 6h-1a5 5 0 0 1-5-5v-2a1.5 1.5 0 0 1 0-3"/>',
    swap: '<path d="M4 8h14M14 4l4 4-4 4"/><path d="M20 16H6M10 12l-4 4 4 4"/>',
    light: '<path d="M4 9h7l5-4v14l-5-4H4z"/><path d="M19 8l2-1M19 12h3M19 16l2 1"/>',
    pause: '<path d="M9 5v14M15 5v14"/>',
    talk: '<rect x="9" y="3" width="6" height="11" rx="3"/><path d="M5 11a7 7 0 0 0 14 0M12 18v3"/>',
    score: '<path d="M4 20v-7h5v7M9.5 20V5h5v15M15 20v-4h5v4"/>',
  };

  function icon(name) {
    return `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICON[name]}</svg>`;
  }

  // [id, label (text, or an icon: {icon}), standard-mapping button, class, caption, badge]
  const TOUCH_LAYOUTS = {
    // the original Xbox controller's layout (the Controller S)
    xbox: [
      ["a", "A", BUTTON.SOUTH, "face face-a", "Jump"],
      ["b", "B", BUTTON.EAST, "face face-b", "Melee"],
      ["x", "X", BUTTON.WEST, "face face-x", "Reload"],
      ["y", "Y", BUTTON.NORTH, "face face-y", "Swap"],
      ["white", "", BUTTON.LEFT_SHOULDER, "duo duo-white", "Light"],
      ["black", "", BUTTON.RIGHT_SHOULDER, "duo duo-black", "Grenade"],
      ["fire", "R", BUTTON.RIGHT_TRIGGER, "trigger right-side", "Fire"],
      ["lt", "L", BUTTON.LEFT_TRIGGER, "trigger left-side", "Throw"],
      ["ls", "", BUTTON.LEFT_STICK, "stick-click left-click", "Crouch"],
      ["rs", "", BUTTON.RIGHT_STICK, "stick-click right-click", "Zoom"],
      ["back", "BACK", BUTTON.BACK, "system back", ""],
      ["start", "START", BUTTON.START, "system start", ""],
      ["up", "", BUTTON.DPAD_UP, "dpad up", ""],
      ["down", "", BUTTON.DPAD_DOWN, "dpad down", ""],
      ["left", "", BUTTON.DPAD_LEFT, "dpad left", ""],
      ["right", "", BUTTON.DPAD_RIGHT, "dpad right", ""],
    ],
    // a modern mobile shooter's layout: a large fire button under the right
    // thumb that also aims while held; in the menus the stick moves and
    // Jump (A) and Melee (B) select and go back, as their badges say
    modern: [
      ["fire", { icon: "fire" }, BUTTON.RIGHT_TRIGGER, "m m-fire", ""],
      ["fire2", { icon: "fire" }, BUTTON.RIGHT_TRIGGER, "m m-fire2", ""],
      ["rs", { icon: "scope" }, BUTTON.RIGHT_STICK, "m m-zoom", "Zoom"],
      ["a", { icon: "jump" }, BUTTON.SOUTH, "m m-jump", "Jump", "A"],
      ["ls", { icon: "crouch" }, BUTTON.LEFT_STICK, "m m-crouch", "Crouch"],
      ["b", { icon: "melee" }, BUTTON.EAST, "m m-melee", "Melee", "B"],
      ["x", { icon: "reload" }, BUTTON.WEST, "m m-reload", "Reload", "X"],
      ["lt", { icon: "grenade" }, BUTTON.LEFT_TRIGGER, "m m-grenade", "Grenade"],
      ["black", { icon: "grenadeSwap" }, BUTTON.RIGHT_SHOULDER, "m m-grenade-type", "Type"],
      ["y", { icon: "swap" }, BUTTON.NORTH, "m m-swap", "Swap", "Y"],
      ["white", { icon: "light" }, BUTTON.LEFT_SHOULDER, "m m-light", "Light"],
      ["back", { icon: "score" }, BUTTON.BACK, "m m-top m-score", "Score"],
      ["start", { icon: "pause" }, BUTTON.START, "m m-top m-pause", "Pause"],
      // voice chat's push to talk (controls.push_to_talk: V), held
      ["talk", { icon: "talk" }, null, "m m-top m-talk", "Talk"],
    ],
  };

  // Moves and resizes a control as the layout editor left it: its centre as
  // fractions of the screen, and a scale.
  function placeControl(element, place) {
    if (!place) {
      for (const property of ["left", "top", "right", "bottom", "translate", "scale"]) element.style[property] = "";
      return;
    }
    if (place.x !== undefined) {
      element.style.left = (place.x * 100).toFixed(2) + "%";
      element.style.top = (place.y * 100).toFixed(2) + "%";
      element.style.right = "auto";
      element.style.bottom = "auto";
      element.style.translate = "-50% -50%";
    }
    element.style.scale = place.s && place.s !== 1 ? String(place.s) : "";
  }

  // options: custom ({ control: { x, y, s } }), opacity, editor (no game input)
  function buildTouchControls(root, layoutName, options = {}) {
    const layoutKey = TOUCH_LAYOUTS[layoutName] ? layoutName : "modern";
    const layout = TOUCH_LAYOUTS[layoutKey];
    const custom = options.custom || {};
    root.classList.remove("modern", "xbox");
    root.classList.add(layoutKey);
    root.style.opacity = options.opacity && options.opacity < 1 ? String(options.opacity) : "";
    const held = new Map(); // touch identifier -> control
    const buttonState = new Map(); // control id -> count of touches
    const stick = { id: null, x: 0, y: 0, element: null, knob: null };
    const aim = { id: null, x: 0, y: 0 };

    const stickBase = document.createElement("div");
    stickBase.className = "touch-stick";
    const knob = document.createElement("div");
    knob.className = "touch-knob";
    stickBase.appendChild(knob);
    root.appendChild(stickBase);
    stick.element = stickBase;
    stick.knob = knob;

    const controls = {};
    const dpad = document.createElement("div");
    dpad.className = "touch-dpad";
    if (layout.some(([, , , cls]) => cls.startsWith("dpad"))) root.appendChild(dpad);
    for (const [id, label, button, cls, caption, badge] of layout) {
      const element = document.createElement("div");
      element.className = "touch-button " + cls;
      element.dataset.control = id;
      element.setAttribute("aria-label", caption || id);
      if (label) {
        const glyph = document.createElement("span");
        glyph.className = "glyph";
        if (label.icon) glyph.innerHTML = icon(label.icon);
        else glyph.textContent = label;
        element.appendChild(glyph);
      }
      if (badge) {
        const mark = document.createElement("span");
        mark.className = "badge badge-" + badge.toLowerCase();
        mark.textContent = badge;
        element.appendChild(mark);
      }
      if (caption) {
        const hint = document.createElement("span");
        hint.className = "caption";
        hint.textContent = caption;
        element.appendChild(hint);
      }
      (cls.startsWith("dpad") ? dpad : root).appendChild(element);
      controls[id] = { element, button };
    }

    dpad.dataset.control = "dpad";
    stickBase.dataset.control = "stick";
    for (const element of root.querySelectorAll("[data-control]")) {
      if (element.parentElement === dpad) continue;
      placeControl(element, custom[element.dataset.control]);
    }
    const stickRest = custom.stick;
    if (options.editor) return { root, layoutKey, dpad, stickBase };

    function refreshButtons() {
      const down = new Set();
      for (const [id, count] of buttonState) {
        if (count > 0) down.add(controls[id].button);
      }
      for (const { button } of Object.values(controls)) if (button !== null) setButton(button, down.has(button));
      talk((buttonState.get("talk") || 0) > 0);
      for (const id in controls) controls[id].element.classList.toggle("pressed", (buttonState.get(id) || 0) > 0);
    }

    // voice chat's push to talk: its key (controls.push_to_talk, V) held
    // while the Talk button is, as a keyboard's (SDL reads the window's keys)
    let talking = false;
    function talk(down) {
      if (down === talking) return;
      talking = down;
      global.dispatchEvent(new KeyboardEvent(down ? "keydown" : "keyup", { key: "v", code: "KeyV", bubbles: true }));
    }

    const STICK_RADIUS = 56;

    function start(event) {
      requestImmersive();
      for (const touch of event.changedTouches) {
        const target = document.elementFromPoint(touch.clientX, touch.clientY);
        const control = target && target.closest && target.closest(".touch-button");
        if (control && root.contains(control)) {
          const id = control.dataset.control;
          held.set(touch.identifier, { kind: "button", id });
          buttonState.set(id, (buttonState.get(id) || 0) + 1);
          // aiming while firing: a touch on the fire button also aims
          if (id === "fire" && aim.id === null) {
            aim.id = touch.identifier;
            aim.x = touch.clientX;
            aim.y = touch.clientY;
          }
          continue;
        }
        if (touch.clientX < global.innerWidth * (layoutKey === "modern" ? 0.45 : 0.4) && stick.id === null) {
          stick.id = touch.identifier;
          stick.x = touch.clientX;
          stick.y = touch.clientY;
          const half = (stick.element.offsetWidth / 2 || 64);
          stick.element.style.translate = "none";
          stick.element.style.left = (touch.clientX - half) + "px";
          stick.element.style.top = (touch.clientY - half) + "px";
          stick.element.style.right = "auto";
          stick.element.style.bottom = "auto";
          stick.element.classList.add("active");
          held.set(touch.identifier, { kind: "stick" });
        } else if (aim.id === null) {
          aim.id = touch.identifier;
          aim.x = touch.clientX;
          aim.y = touch.clientY;
          held.set(touch.identifier, { kind: "look" });
        }
      }
      refreshButtons();
      event.preventDefault();
    }

    function move(event) {
      for (const touch of event.changedTouches) {
        if (touch.identifier === stick.id) {
          let dx = touch.clientX - stick.x;
          let dy = touch.clientY - stick.y;
          const length = Math.hypot(dx, dy);
          if (length > STICK_RADIUS) {
            dx *= STICK_RADIUS / length;
            dy *= STICK_RADIUS / length;
          }
          stick.knob.style.transform = `translate(${dx}px, ${dy}px)`;
          setStick(dx / STICK_RADIUS, dy / STICK_RADIUS);
        }
        if (touch.identifier === aim.id) {
          const dx = touch.clientX - aim.x;
          const dy = touch.clientY - aim.y;
          aim.x = touch.clientX;
          aim.y = touch.clientY;
          look(dx, dy);
        }
      }
      event.preventDefault();
    }

    function end(event) {
      for (const touch of event.changedTouches) {
        const entry = held.get(touch.identifier);
        held.delete(touch.identifier);
        if (entry && entry.kind === "button") {
          buttonState.set(entry.id, Math.max(0, (buttonState.get(entry.id) || 0) - 1));
        }
        if (touch.identifier === stick.id) {
          stick.id = null;
          stick.knob.style.transform = "";
          stick.element.classList.remove("active");
          // back to its resting place
          placeControl(stick.element, null);
          placeControl(stick.element, stickRest);
          setStick(0, 0);
        }
        if (touch.identifier === aim.id) aim.id = null;
      }
      refreshButtons();
      event.preventDefault();
    }

    root.addEventListener("touchstart", start, { passive: false });
    root.addEventListener("touchmove", move, { passive: false });
    root.addEventListener("touchend", end, { passive: false });
    root.addEventListener("touchcancel", end, { passive: false });
    return { root, layoutKey, dpad, stickBase };
  }

  // The layout editor (from Halo Mobile): drag a control to move it, pick
  // one to size it. onDone(custom, opacity) when Done is pushed.
  function editTouchLayout(container, layoutName, custom, opacity, onDone) {
    const places = JSON.parse(JSON.stringify(custom || {}));
    let alpha = opacity || 1;
    container.textContent = "";
    const stage = document.createElement("div");
    stage.className = "touch-layer";
    container.appendChild(stage);
    const built = buildTouchControls(stage, layoutName, { custom: places, opacity: alpha, editor: true });
    const bar = document.createElement("div");
    bar.className = "editor-bar";
    bar.innerHTML = `<span class="editor-hint">Drag a control to move it. Pick one to size it.</span>
      <label>Size <input type="range" min="0.6" max="1.8" step="0.05" value="1" data-size disabled></label>
      <label>Opacity <input type="range" min="0.25" max="1" step="0.05" data-opacity></label>
      <button type="button" data-reset>Reset</button>
      <button type="button" class="primary" data-done>Done</button>`;
    container.appendChild(bar);
    const size = bar.querySelector("[data-size]");
    const fade = bar.querySelector("[data-opacity]");
    fade.value = alpha;
    let selected = null;
    const units = [...stage.querySelectorAll("[data-control]")].filter(element => element.parentElement !== built.dpad);

    function select(element) {
      if (selected) selected.classList.remove("editing");
      selected = element;
      if (!element) {
        size.disabled = true;
        return;
      }
      element.classList.add("editing");
      size.disabled = false;
      size.value = (places[element.dataset.control] && places[element.dataset.control].s) || 1;
    }

    for (const element of units) {
      element.addEventListener("pointerdown", event => {
        event.preventDefault();
        event.stopPropagation();
        select(element);
        element.setPointerCapture(event.pointerId);
        const drag = next => {
          const id = element.dataset.control;
          const x = Math.min(0.98, Math.max(0.02, next.clientX / global.innerWidth));
          const y = Math.min(0.98, Math.max(0.02, next.clientY / global.innerHeight));
          places[id] = { ...(places[id] || {}), x, y };
          placeControl(element, places[id]);
        };
        const up = () => {
          element.removeEventListener("pointermove", drag);
          element.removeEventListener("pointerup", up);
          element.removeEventListener("pointercancel", up);
        };
        element.addEventListener("pointermove", drag);
        element.addEventListener("pointerup", up);
        element.addEventListener("pointercancel", up);
      });
    }
    stage.addEventListener("pointerdown", event => { if (event.target === stage) select(null); });
    size.oninput = () => {
      if (!selected) return;
      const id = selected.dataset.control;
      places[id] = { ...(places[id] || {}), s: parseFloat(size.value) };
      placeControl(selected, places[id]);
    };
    fade.oninput = () => {
      alpha = parseFloat(fade.value);
      stage.style.opacity = alpha < 1 ? String(alpha) : "";
    };
    bar.querySelector("[data-reset]").onclick = () => {
      for (const key of Object.keys(places)) delete places[key];
      for (const element of units) placeControl(element, null);
      alpha = 1;
      fade.value = 1;
      stage.style.opacity = "";
      select(null);
    };
    bar.querySelector("[data-done]").onclick = () => {
      container.textContent = "";
      onDone(places, alpha);
    };
  }

  // ---------- touch mode in this port's shell

  const state = { active: false, layer: null, menu: null, editor: null, autoStarted: false };

  function gameArea() {
    return document.getElementById("game-area");
  }

  function isPhoneLike() {
    const query = global.matchMedia ? q => global.matchMedia(q).matches : () => false;
    return navigator.maxTouchPoints > 0 && query("(pointer: coarse)") && !query("(any-pointer: fine)");
  }

  function forcedByUrl() {
    try {
      return new URL(global.location.href).searchParams.get("touch") === "1";
    } catch (_error) {
      return false;
    }
  }

  function rebuildLayer() {
    const area = gameArea();
    if (!area) return;
    const fresh = document.createElement("div");
    fresh.className = "touch-layer";
    fresh.setAttribute("aria-hidden", "true");
    if (state.layer) state.layer.replaceWith(fresh);
    else area.appendChild(fresh);
    state.layer = fresh;
    releaseEverything();
    buildTouchControls(fresh, settings.layout, {
      custom: settings.custom[settings.layout],
      opacity: settings.opacity[settings.layout],
    });
  }

  // Android: real fullscreen and a landscape lock need a tap, so they are
  // asked for on the first touch. iPhone Safari has neither; touch mode's
  // own layout covers the screen there.
  function requestImmersive() {
    if (!state.active) return;
    const area = gameArea();
    if (area && !document.fullscreenElement && typeof area.requestFullscreen === "function") {
      const request = area.requestFullscreen({ navigationUI: "hide" });
      if (request && request.catch) request.catch(() => {});
    }
    const orientation = screen.orientation;
    if (orientation && typeof orientation.lock === "function") {
      orientation.lock("landscape").catch(() => {});
    }
  }

  function refreshControllerClass() {
    document.body.classList.toggle("has-controller", physicalControllerCount() > 0);
  }

  function setTouchMode(active) {
    if (state.active === active) return;
    state.active = active;
    document.body.classList.toggle("halo-touch-mode", active);
    connectVirtualPad(active);
    if (active) {
      rebuildLayer();
    } else {
      closeMenu();
      if (document.fullscreenElement && document.exitFullscreen) document.exitFullscreen().catch(() => {});
    }
    const toggle = document.getElementById("touch-mode-toggle");
    if (toggle) toggle.setAttribute("aria-pressed", String(active));
  }

  function buildMenuButton() {
    const area = gameArea();
    if (!area || document.querySelector(".touch-menu-button")) return;
    const button = document.createElement("button");
    button.type = "button";
    button.className = "touch-menu-button";
    button.setAttribute("aria-label", "Touch controls menu");
    button.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"><path d="M4 7h16M4 12h16M4 17h16"/></svg>';
    button.addEventListener("click", openMenu);
    area.appendChild(button);

    const hint = document.createElement("div");
    hint.className = "touch-rotate-hint";
    hint.innerHTML = '<span>Turn your phone sideways to play.</span>';
    const leave = document.createElement("button");
    leave.type = "button";
    leave.className = "touch-ui-button";
    leave.textContent = "Leave touch mode";
    leave.addEventListener("click", () => setTouchMode(false));
    hint.appendChild(leave);
    area.appendChild(hint);
  }

  function openMenu() {
    closeMenu();
    const area = gameArea();
    const menu = document.createElement("div");
    menu.className = "touch-menu";
    const percent = Math.round(settings.sensitivity / 1.4 * 100);
    menu.innerHTML = `<div class="panel" role="dialog" aria-label="Touch controls">
        <h2>Touch controls</h2>
        <label>Layout
          <select data-layout>
            <option value="modern">Modern</option>
            <option value="xbox">Original Xbox controller</option>
          </select></label>
        <label>Look speed <input type="range" min="0.4" max="3.5" step="0.1" data-sensitivity></label>
        <p class="small" data-sensitivity-value>Look speed ${percent}%</p>
        <label data-resolution-row hidden>Resolution
          <select data-resolution></select></label>
        <div class="row">
          <button type="button" class="touch-ui-button" data-edit>Edit layout</button>
          <button type="button" class="touch-ui-button" data-online>Play online…</button>
          <button type="button" class="touch-ui-button" data-install hidden>Install app</button>
        </div>
        <p class="small">Drag on the right side to aim. Press START (Pause) for the game's own menu.</p>
        <p class="small"><a href="https://github.com/FractumSeraph/OpenCE#download" target="_blank" rel="noopener noreferrer">Get the Android, Windows &amp; Linux version</a>: faster, with direct internet play.</p>
        <div class="row">
          <button type="button" class="touch-ui-button primary" data-close>Back to the game</button>
          <button type="button" class="touch-ui-button" data-leave>Leave touch mode</button>
        </div>
      </div>`;
    const layoutSelect = menu.querySelector("[data-layout]");
    layoutSelect.value = settings.layout;
    layoutSelect.onchange = () => {
      settings.layout = layoutSelect.value;
      saveSettings();
      rebuildLayer();
    };
    const sensitivity = menu.querySelector("[data-sensitivity]");
    const sensitivityValue = menu.querySelector("[data-sensitivity-value]");
    sensitivity.value = settings.sensitivity;
    sensitivity.oninput = () => {
      settings.sensitivity = parseFloat(sensitivity.value);
      sensitivityValue.textContent = `Look speed ${Math.round(settings.sensitivity / 1.4 * 100)}%`;
      saveSettings();
    };
    // the page's own Resolution setting (shell.html's Game settings, which
    // touch mode hides): the same choices, and choosing one sets it there
    const pageResolution = document.getElementById("resolution-setting");
    if (pageResolution) {
      const resolution = menu.querySelector("[data-resolution]");
      for (const option of pageResolution.options) resolution.add(new Option(option.textContent, option.value));
      resolution.value = pageResolution.value;
      resolution.onchange = () => {
        pageResolution.value = resolution.value;
        pageResolution.dispatchEvent(new Event("change", { bubbles: true }));
      };
      menu.querySelector("[data-resolution-row]").hidden = false;
    }
    menu.querySelector("[data-edit]").onclick = () => {
      closeMenu();
      openEditor();
    };
    menu.querySelector("[data-online]").onclick = () => {
      setTouchMode(false);
      const online = document.getElementById("online");
      if (online) online.click();
    };
    // assets/pwa/install.js: add Halo to the home screen
    const install = menu.querySelector("[data-install]");
    if (global.HaloInstall && global.HaloInstall.available()) {
      install.hidden = false;
      install.onclick = () => {
        closeMenu();
        global.HaloInstall.start();
      };
    }
    menu.querySelector("[data-close]").onclick = closeMenu;
    menu.querySelector("[data-leave]").onclick = () => setTouchMode(false);
    menu.addEventListener("click", event => { if (event.target === menu) closeMenu(); });
    area.appendChild(menu);
    state.menu = menu;
  }

  function closeMenu() {
    if (state.menu) state.menu.remove();
    state.menu = null;
  }

  function openEditor() {
    const area = gameArea();
    const container = document.createElement("div");
    container.className = "layout-editor";
    area.appendChild(container);
    state.editor = container;
    if (state.layer) state.layer.style.visibility = "hidden";
    editTouchLayout(container, settings.layout, settings.custom[settings.layout],
      settings.opacity[settings.layout], (custom, opacity) => {
        settings.custom[settings.layout] = custom;
        settings.opacity[settings.layout] = opacity;
        saveSettings();
        container.remove();
        state.editor = null;
        rebuildLayer();
      });
  }

  function buildToggle() {
    const controls = document.querySelector("#game-frame .game-controls");
    if (!controls || document.getElementById("touch-mode-toggle")) return;
    const toggle = document.createElement("button");
    toggle.id = "touch-mode-toggle";
    toggle.type = "button";
    toggle.className = "control-button";
    toggle.setAttribute("aria-pressed", "false");
    toggle.innerHTML = '<svg aria-hidden="true" viewBox="0 0 24 24"><path d="M9 11V5.5a1.5 1.5 0 0 1 3 0V11m0-2.5a1.5 1.5 0 0 1 3 0V11m0-1.5a1.5 1.5 0 0 1 3 0V15a6 6 0 0 1-6 6h-.5a5 5 0 0 1-4.2-2.3L5 15.5a1.5 1.5 0 0 1 2.4-1.8L9 15.5"></path></svg><span>Touch</span>';
    toggle.title = "Touch controls";
    toggle.hidden = !(navigator.maxTouchPoints > 0 || forcedByUrl());
    toggle.addEventListener("click", () => setTouchMode(!state.active));
    controls.insertBefore(toggle, controls.firstChild);
  }

  // Phones (and ?touch=1) go into touch mode by themselves once the game is
  // running and no page dialog (the online lobby, the disc image) is open.
  function maybeAutoStart() {
    if (state.autoStarted || state.active) return;
    if (!(isPhoneLike() || forcedByUrl())) return;
    const status = document.getElementById("status");
    if (!status || status.textContent !== "Running") return;
    if (document.querySelector("dialog[open]")) return;
    state.autoStarted = true;
    setTouchMode(true);
  }

  function initialise() {
    buildToggle();
    buildMenuButton();
    refreshControllerClass();
    global.addEventListener("gamepadconnected", event => {
      if (event.gamepad !== virtualPad) refreshControllerClass();
    });
    global.addEventListener("gamepaddisconnected", event => {
      if (event.gamepad !== virtualPad) refreshControllerClass();
    });
    const status = document.getElementById("status");
    if (status && global.MutationObserver) {
      new MutationObserver(maybeAutoStart).observe(status, { childList: true, characterData: true, subtree: true });
    }
    global.setInterval(maybeAutoStart, 1500);
    maybeAutoStart();
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", initialise);
  else initialise();

  global.HaloTouchControls = Object.freeze({
    enable: () => setTouchMode(true),
    disable: () => setTouchMode(false),
    isActive: () => state.active,
  });
})(globalThis);
