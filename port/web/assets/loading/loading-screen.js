/* Halo's loading screen, as the game draws a map's download
(source/interface/progress_bar.c, progress_bar_render; the fork's
port/linux/game/download_screen.c): a light sweeping from left to right over
the picture, as far as the work has come, each frame's light laid over the
frame before, which is kept nine tenths as bright, a little larger and a
little blurred, so that the light glows and leaves streaks reaching out from
the middle. The picture is the game's loading.tga, read from the site's game
data (assets/maps/loading.tga, tools/xiso_extract.py writes it), tinted as
the game tints it (tgaLoadImageData); without it, the game's own clouds
(progress_bar_make_clouds). Under it, the title and a line of how it goes,
as the download screen writes them.

    const screen = new HaloLoadingScreen(element, { title, status });
    screen.set({ title, status, progress });   // progress 0 to 1, or null
    screen.stop();

A null progress sweeps the light again and again (the work's share is not
known). A classic script: window.HaloLoadingScreen. */
(function () {
  "use strict";

  // (the game's 640x480 screen; its frames, as the port's menus draw them,
  // 60 a second: the light's pace and its trails' length are per frame)
  const WIDTH = 640, HEIGHT = 480, PICTURE_WIDTH = 320, PICTURE_HEIGHT = 240;
  const FRAME_MS = 1000 / 60;
  // (a sweep when the share is not known, in frames)
  const SWEEP_FRAMES = 300;
  const HERE = document.currentScript && document.currentScript.src ? document.currentScript.src : location.href;
  const FONT_URL = new URL("CyborgInterfaceGothic-Regular.ttf", HERE).href;
  const PICTURE_URL = new URL("../maps/loading.tga", HERE).href;

  let fontLoaded = null;
  function loadFont() {
    if (!fontLoaded && typeof FontFace === "function") {
      const face = new FontFace("Cyborg Interface Gothic", `url("${FONT_URL}")`);
      fontLoaded = face.load().then(f => { document.fonts.add(f); }).catch(() => {});
    }
    return fontLoaded;
  }

  /* progress_bar_cloud_lattice: a value of the clouds' lattice, 0 to 1 */
  function lattice(x, y) {
    let hash = (Math.imul(x, 374761393) + Math.imul(y, 668265263)) >>> 0;
    hash = Math.imul(hash ^ (hash >>> 13), 1274126177) >>> 0;
    hash = (hash ^ (hash >>> 16)) >>> 0;
    return (hash & 0xffff) / 65535;
  }

  /* progress_bar_make_clouds: soft clouds, four octaves of smoothed value
  noise that wrap at the picture's edges, tinted as the game tints
  loading.tga (red a quarter, green half, blue all of the intensity) */
  let clouds = null;
  function makeClouds() {
    if (clouds) return clouds;
    clouds = document.createElement("canvas");
    clouds.width = PICTURE_WIDTH;
    clouds.height = PICTURE_HEIGHT;
    const context = clouds.getContext("2d");
    const image = context.createImageData(PICTURE_WIDTH, PICTURE_HEIGHT);
    for (let y = 0; y < PICTURE_HEIGHT; y++) {
      for (let x = 0; x < PICTURE_WIDTH; x++) {
        let value = 0, weight = 0.5, cells = 4;
        for (let octave = 0; octave < 4; octave++, cells *= 2, weight *= 0.5) {
          const u = x * cells / PICTURE_WIDTH, v = y * cells / PICTURE_HEIGHT;
          const x0 = Math.floor(u), y0 = Math.floor(v);
          let fx = u - x0, fy = v - y0;
          fx = fx * fx * (3 - 2 * fx);
          fy = fy * fy * (3 - 2 * fy);
          const a = lattice(octave * 1000 + x0 % cells, y0 % cells);
          const b = lattice(octave * 1000 + (x0 + 1) % cells, y0 % cells);
          const c = lattice(octave * 1000 + x0 % cells, (y0 + 1) % cells);
          const d = lattice(octave * 1000 + (x0 + 1) % cells, (y0 + 1) % cells);
          value += weight * ((a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy);
        }
        value = Math.min(1, Math.max(0, (value - 0.25) * 1.6));
        const intensity = Math.floor(value * 255);
        const at = (y * PICTURE_WIDTH + x) * 4;
        image.data[at] = intensity >> 2;
        image.data[at + 1] = (intensity & ~1) >> 1;
        image.data[at + 2] = intensity;
        image.data[at + 3] = 255;
      }
    }
    context.putImageData(image, 0, 0);
    return clouds;
  }

  /* progress_bar_load_loading_texture: loading.tga, an uncompressed 24-bit
  320x240 picture (the game reads no other), its rows from the bottom, each
  pixel's first byte its intensity (tgaLoadImageData); null without it */
  let picture = null;
  function loadPicture() {
    if (!picture) {
      picture = fetch(PICTURE_URL).then(answer => answer.ok ? answer.arrayBuffer() : null).then(buffer => {
        if (!buffer || buffer.byteLength < 18) return null;
        const bytes = new Uint8Array(buffer);
        const width = bytes[12] | bytes[13] << 8, height = bytes[14] | bytes[15] << 8;
        const start = 18 + bytes[0];
        if (bytes[2] !== 2 || bytes[16] !== 24 || width !== PICTURE_WIDTH || height !== PICTURE_HEIGHT ||
          bytes.length < start + width * height * 3) return null;
        const c = document.createElement("canvas");
        c.width = width;
        c.height = height;
        const context = c.getContext("2d");
        const image = context.createImageData(width, height);
        for (let y = 0; y < height; y++) {
          for (let x = 0; x < width; x++) {
            const intensity = bytes[start + (y * width + x) * 3];
            const at = ((height - y - 1) * width + x) * 4;
            image.data[at] = intensity >> 2;
            image.data[at + 1] = (intensity & ~1) >> 1;
            image.data[at + 2] = intensity;
            image.data[at + 3] = 255;
          }
        }
        context.putImageData(image, 0, 0);
        return c;
      }).catch(() => null).then(c => c || makeClouds());
    }
    return picture;
  }

  /* progress_bar_create_mask_texture: the light, 128 columns wide and the
  screen's height, brightest in its middle; 5 of 255 beyond it */
  let band = null;
  function makeBand() {
    if (band) return band;
    band = document.createElement("canvas");
    band.width = 128;
    band.height = 16;
    const context = band.getContext("2d");
    const image = context.createImageData(128, 16);
    for (let y = 0; y < 16; y++) {
      const glow = Math.pow(Math.sin(3.1415 * (y / 15)), 0.75);
      for (let x = 0; x < 128; x++) {
        const intensity = Math.floor((Math.sin(3.1415 * (x / 127)) * glow * (250 / 255) + 5 / 255) * 255);
        const at = (y * 128 + x) * 4;
        image.data[at] = image.data[at + 1] = image.data[at + 2] = intensity;
        image.data[at + 3] = 255;
      }
    }
    context.putImageData(image, 0, 0);
    return band;
  }

  function canvas(width, height) {
    const c = document.createElement("canvas");
    c.width = width;
    c.height = height;
    return c;
  }

  class HaloLoadingScreen {
    constructor(element, options) {
      loadFont();
      this.element = element;
      this.root = document.createElement("div");
      this.root.className = "halo-loading-screen";
      this.root.setAttribute("aria-hidden", "true");
      this.root.innerHTML = '<div class="halo-loading-frame"><canvas></canvas>' +
        '<div class="halo-loading-title"></div><div class="halo-loading-status"></div></div>';
      this.screen = this.root.querySelector("canvas");
      this.screen.width = WIDTH;
      this.screen.height = HEIGHT;
      this.context = this.screen.getContext("2d");
      this.context.fillStyle = "#000";
      this.context.fillRect(0, 0, WIDTH, HEIGHT);
      this.before = canvas(WIDTH, HEIGHT);
      this.layer = canvas(WIDTH, HEIGHT);
      this.titleElement = this.root.querySelector(".halo-loading-title");
      this.statusElement = this.root.querySelector(".halo-loading-status");
      element.prepend(this.root);
      // (progress_bar_render's: the light where it is drawn, its wobble)
      this.target = null;
      this.shown = 0;
      this.last = 0;
      this.wobble = 0;
      this.sweep = 0;
      this.clock = 0;
      this.running = true;
      this.picture = null;
      loadPicture().then(c => { this.picture = c; });
      this.set(options || {});
      this.frame = this.frame.bind(this);
      requestAnimationFrame(this.frame);
    }

    set(options) {
      if ("title" in options) this.titleElement.textContent = (options.title || "").toUpperCase();
      if ("status" in options) this.statusElement.textContent = options.status || "";
      if ("progress" in options) {
        const p = options.progress;
        if (p === null || p === undefined || !isFinite(p)) this.target = null;
        // (download_screen.c: only forward, as a share falls back as the
        // next file's size is learned)
        else this.target = Math.max(this.target === null ? 0 : this.target, Math.min(1, Math.max(0.001, p)));
      }
    }

    stop() {
      this.running = false;
      this.root.remove();
    }

    frame(now) {
      if (!this.running) return;
      requestAnimationFrame(this.frame);
      if (document.hidden) return;
      if (!this.clock) this.clock = now - FRAME_MS;
      // (at most a few frames caught up: a hidden tab's time is not)
      let frames = Math.min(4, Math.floor((now - this.clock) / FRAME_MS));
      if (frames <= 0) return;
      this.clock += frames * FRAME_MS;
      if (now - this.clock > FRAME_MS * 4) this.clock = now;
      while (frames-- > 0) this.step();
    }

    step() {
      let target = this.target;
      if (target === null) {
        // (not known: the light goes across again and again)
        this.sweep = (this.sweep + 1) % SWEEP_FRAMES;
        target = (this.sweep + 1) / SWEEP_FRAMES;
        if (this.sweep === 0) this.last = 0;
      }
      this.wobble += 0.01;
      let progress = (Math.sin(this.wobble) * 0.05 + target - this.last) * 0.2 + this.last;
      if (progress > 1) progress = 1;
      if (progress < 0) progress = 0;
      this.last = progress;
      const brightness = Math.pow(Math.max(0, Math.sin(progress * 3.14159)), 0.9);
      const context = this.context;

      // the frame before, nine tenths as bright, its 634 middle columns over
      // a little more than the screen (draw_fade_layer; its blur is a tenth
      // of a pixel, which leaves the streaks sharp)
      const before = this.before.getContext("2d");
      before.globalCompositeOperation = "copy";
      before.drawImage(this.screen, 0, 0);
      context.globalCompositeOperation = "copy";
      context.fillStyle = "#000";
      context.fillRect(0, 0, WIDTH, HEIGHT);
      context.globalCompositeOperation = "source-over";
      context.globalAlpha = progress < 0.9 ? 0.9 : Math.max(0, 0.9 - (progress - 0.9) * 9);
      context.drawImage(this.before, 3, 0, 634, HEIGHT, -WIDTH / 256, -HEIGHT / 256,
        WIDTH * (1 + 1 / 128), HEIGHT * (1 + 1 / 128));
      context.globalAlpha = 1;
      if (!this.picture) return;

      // the picture where the light is, added (draw_initial_layer): the
      // light's columns from progress*768 - 128
      const layer = this.layer.getContext("2d");
      layer.globalCompositeOperation = "copy";
      layer.imageSmoothingEnabled = true;
      layer.drawImage(this.picture, 0, 0, WIDTH, HEIGHT);
      layer.globalCompositeOperation = "multiply";
      layer.fillStyle = "rgb(5,5,5)";
      const position = progress * 768 - 128;
      layer.fillRect(0, 0, Math.max(0, position), HEIGHT);
      layer.fillRect(position + 128, 0, WIDTH, HEIGHT);
      layer.drawImage(makeBand(), position, 0, 128, HEIGHT);
      context.globalCompositeOperation = "lighter";
      context.globalAlpha = brightness;
      context.drawImage(this.layer, 0, 0);
      context.globalAlpha = 1;
      context.globalCompositeOperation = "source-over";
    }
  }

  // (the look: the game's 4:3 screen in the middle of black, the text at
  // the download screen's places, in the menus' font and colors)
  const style = document.createElement("style");
  style.textContent = `
.halo-loading-screen{position:absolute;inset:0;background:#000;overflow:hidden;display:grid;place-items:center;z-index:0;
  container-type:size}
.halo-loading-frame{position:relative;width:min(100cqw,133.33cqh);aspect-ratio:4/3;container-type:size}
.halo-loading-frame canvas{position:absolute;inset:0;width:100%;height:100%;display:block}
.halo-loading-title,.halo-loading-status{position:absolute;left:2%;right:2%;text-align:center;white-space:nowrap;
  overflow:hidden;text-overflow:ellipsis;font-family:"Cyborg Interface Gothic","Overpass","Segoe UI",sans-serif}
.halo-loading-title{top:62.5%;font-size:4.2cqh;color:rgb(230,237,252);letter-spacing:.02em}
.halo-loading-status{top:69.6%;font-size:2.9cqh;color:rgb(143,166,199)}
`;
  document.head.append(style);

  window.HaloLoadingScreen = HaloLoadingScreen;
})();
