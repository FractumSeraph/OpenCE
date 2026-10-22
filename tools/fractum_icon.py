#!/usr/bin/env python3
"""Makes this fork's icons (Fractum Halo's) from its artwork,
port/assets/icon/fractum-halo.png (a ring and its moon on a starfield,
1024x1024):

    python tools/fractum_icon.py

- the web page's (port/web/assets/pwa: icon-180.png, Apple's home screen;
  icon-192.png and icon-512.png, the tab's and the installed web app's; and
  icon-maskable-512.png, the artwork within the 80% a launcher's mask keeps),
  which port/web/manifest.webmanifest, shell.html, the replay viewer and the
  Android download page name;
- the desktop windows' (port/assets/icon/fractum-halo-256.png, which
  tools/embed_assets.py embeds and sdl_platform.c gives the window);
- halo.exe's (port/windows/fractum-halo.ico, which port/windows/halo.rc puts
  in it);
- the Mac application's (port/macos/FractumHalo.icns, which port/macos/bundle.py
  copies in and Info.plist names);
- the Android app's launcher icon: an adaptive one (Android 8 and later), the
  ring and moon cut out in a soft circle as its foreground layer, sized to
  Android's 66 dp safe zone (the circle no launcher's mask cuts into), the
  starfield's navy as its background, and the ring and moon's light as its
  monochrome (themed icon) layer: port/android/app/src/main/res's
  mipmap-*/ic_fractum_*.png, mipmap-anydpi-v26/ic_fractum.xml and
  values/ic_fractum_background.xml, which AndroidManifest.xml names.

ChupathingyCE's icons (tools/android_icon.py) and OpenCE's (tools/app_icon.py)
stay in the tree under their own names. Run this again when the artwork
changes. Needs Pillow.
"""

from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
ARTWORK = ROOT / "port/assets/icon/fractum-halo.png"
PWA = ROOT / "port/web/assets/pwa"
WINDOW_ICON = ROOT / "port/assets/icon/fractum-halo-256.png"
WINDOWS_ICON = ROOT / "port/windows/fractum-halo.ico"
WINDOWS_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
MAC_ICON = ROOT / "port/macos/FractumHalo.icns"
ANDROID_RES = ROOT / "port/android/app/src/main/res"
# the layers' size and the safe zone's diameter, in dp; the screen densities
LAYER_DP = 108
SAFE_DP = 66
DENSITIES = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}
# where the ring is in the artwork, as fractions of its side: its centre and
# the radius that takes in its glow (the moon is within it)
RING_CENTRE = (0.5, 0.5)
RING_RADIUS = 0.44


def ring_cutout(artwork: Image.Image) -> Image.Image:
    """the ring and moon in a soft-edged circle on transparency, the circle
    filling the square"""
    side = artwork.size[0]
    radius = RING_RADIUS * side
    cx, cy = RING_CENTRE[0] * side, RING_CENTRE[1] * side
    box = (round(cx - radius), round(cy - radius), round(cx + radius), round(cy + radius))
    square = artwork.crop(box).convert("RGBA")
    size = square.size[0]
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).ellipse((size * 0.03, size * 0.03, size * 0.97, size * 0.97), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(size * 0.025))
    square.putalpha(mask)
    return square


def background_colour(artwork: Image.Image) -> tuple:
    """the starfield's navy just outside the ring (the median, past the stars)"""
    side = artwork.size[0]
    band = artwork.convert("RGB").resize((64, 64), Image.BOX)
    samples = [band.getpixel((x, y)) for x in range(64) for y in range(64)
               if min(x, y, 63 - x, 63 - y) < 4]
    return tuple(sorted(channel)[len(channel) // 2] for channel in zip(*samples))


def monochrome(cutout: Image.Image, background: tuple) -> Image.Image:
    """white where the ring and moon are lighter than the sky, for themed icons"""
    rgb = cutout.convert("RGB")
    sky = Image.new("RGB", rgb.size, background)
    light = ImageChops.subtract(rgb, sky).convert("L")
    light = light.point(lambda value: 0 if value < 24 else min(255, (value - 24) * 3))
    alpha = ImageChops.multiply(light, cutout.getchannel("A"))
    layer = Image.new("RGBA", rgb.size, (255, 255, 255, 0))
    layer.putalpha(alpha)
    return layer


def write_android(artwork: Image.Image) -> None:
    background = background_colour(artwork)
    cutout = ring_cutout(artwork)
    mono = monochrome(cutout, background)
    for density, scale in DENSITIES.items():
        size = round(LAYER_DP * scale)
        inner = round(SAFE_DP * scale)
        folder = ANDROID_RES / f"mipmap-{density}"
        folder.mkdir(parents=True, exist_ok=True)
        for name, source in (("ic_fractum_foreground", cutout), ("ic_fractum_monochrome", mono)):
            layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
            layer.alpha_composite(source.resize((inner, inner), Image.LANCZOS), ((size - inner) // 2, (size - inner) // 2))
            layer.save(folder / f"{name}.png", optimize=True)
    (ANDROID_RES / "mipmap-anydpi-v26").mkdir(parents=True, exist_ok=True)
    (ANDROID_RES / "mipmap-anydpi-v26/ic_fractum.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<!-- Fractum Halo's launcher icon (tools/fractum_icon.py, from port/assets/icon/fractum-halo.png) -->\n"
        '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <background android:drawable="@color/ic_fractum_background" />\n'
        '    <foreground android:drawable="@mipmap/ic_fractum_foreground" />\n'
        '    <monochrome android:drawable="@mipmap/ic_fractum_monochrome" />\n'
        "</adaptive-icon>\n", newline="\n")
    (ANDROID_RES / "values/ic_fractum_background.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<!-- Fractum Halo's launcher icon's background: the artwork's sky (tools/fractum_icon.py) -->\n"
        "<resources>\n"
        f'    <color name="ic_fractum_background">#{background[0]:02X}{background[1]:02X}{background[2]:02X}</color>\n'
        "</resources>\n", newline="\n")
    print(f"Android: background #{background[0]:02X}{background[1]:02X}{background[2]:02X}, "
          f"{len(DENSITIES)} densities")


def main() -> None:
    artwork = Image.open(ARTWORK).convert("RGB")
    assert artwork.size[0] == artwork.size[1], "the artwork is square"
    for size in (180, 192, 512):
        artwork.resize((size, size), Image.LANCZOS).save(PWA / f"icon-{size}.png", optimize=True)
    # (a maskable icon's safe zone is the middle 80%: the artwork within it,
    # on its own sky)
    maskable = Image.new("RGB", (512, 512), background_colour(artwork))
    inner = round(512 * 0.8)
    maskable.paste(artwork.resize((inner, inner), Image.LANCZOS), ((512 - inner) // 2, (512 - inner) // 2))
    maskable.save(PWA / "icon-maskable-512.png", optimize=True)
    artwork.resize((256, 256), Image.LANCZOS).save(WINDOW_ICON, optimize=True)
    artwork.save(WINDOWS_ICON, sizes=[(size, size) for size in WINDOWS_SIZES])
    artwork.save(MAC_ICON)
    write_android(artwork)
    print("web, window, Windows and Mac icons written")


if __name__ == "__main__":
    main()
