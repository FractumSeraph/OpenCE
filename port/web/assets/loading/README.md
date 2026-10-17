# Halo's loading screen, for web pages

`loading-screen.js` draws the game's map download screen
(`port/linux/game/download_screen.c` over `source/interface/progress_bar.c`'s
`progress_bar_render`) on a canvas, for the site's pages: the browser build's
startup overlay (`port/web/shell.html`) and the replay viewer while it reads a
map (`port/web/assets/replay/index.html`).

The light sweeps from left to right as far as the work has come (or again and
again when that is not known), over the picture where it is, each frame's
light added to the frame before, kept nine tenths as bright and a little
larger: the glow and the streaks reaching out from the middle. The picture is
the site's game data's `assets/maps/loading.tga` (which `tools/xiso_extract.py`
writes from the disc; it is not in this repository), tinted as the game tints
it; without it, the clouds the game makes (`progress_bar_make_clouds`). The
title and the line under it are in Cyborg Interface Gothic, the game's menu
font (`CyborgInterfaceGothic-Regular.ttf`, SIL Open Font License 1.1:
`CyborgInterfaceGothic-OFL.txt`; a copy of `port/assets/fonts`'s).

    const screen = new HaloLoadingScreen(element, { title, status, progress });
    screen.set({ status, progress });  // progress 0 to 1, or null
    screen.stop();
