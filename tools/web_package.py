#!/usr/bin/env python3
"""Package a finished `ninja web` build as a zip that runs anywhere.

    python tools/web_package.py [--output dist/halo-web.zip]

The zip holds one folder, halo-web/, ready to serve as it is:

    index.html, halo.html     the page (with the build's id stamped in)
    halo.js, halo.wasm        the game
    coi-serviceworker.js      cross-origin isolation, where headers cannot
    manifest.webmanifest      installing as an app
    assets/                   touch controls, app icons, lobby artwork
    assets/maps/              empty: put the game's .map files here, or let
                              each player load their own Xbox disc image
    serve.py                  a local server with the headers the game needs
    README.txt

No game data is packaged. The build id is a hash of the page and the game,
so only players with the same build share an online lobby.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import zipfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
FOLDER = "halo-web"
# (Emscripten minifies the shell: attributes reordered, quotes dropped)
BUILD_META = re.compile(
    rb'<meta\b(?=[^>]*\bname=(?:["\']halo-build-id["\']|halo-build-id)(?=[\s>]))[^>]*>'
)

README = """Halo: Combat Evolved in the browser (OpenCE)
Build {build_id}, from {source}

1. Maps. Put the game's Xbox .map files in assets/maps/ (ui.map, the
   campaign's a10 ... d40 and the multiplayer maps, under those names).
   Without ui.map there, each player is asked for an Xbox disc image of
   their own instead.

2. Serve this folder. The game needs these headers on every response:
       Cross-Origin-Opener-Policy: same-origin
       Cross-Origin-Embedder-Policy: require-corp
   and byte ranges for the maps. To try it on this computer (Python 3):
       python serve.py
   then open http://127.0.0.1:8000/ in Chrome, Edge or Firefox. Browsers run
   the game only over HTTPS or on localhost. (Where headers cannot be set,
   coi-serviceworker.js provides the isolation after one reload.)

Online play ("Play online", invite links, joining native halo://join links)
needs the lobby service and the native gateway beside the page, at
<this folder>/v1/... (services/signaling and services/native-gateway in the
repository). Without them, campaign and the rest of the game work offline.
"""


def stamp(html: bytes, build_id: str) -> bytes:
    html, count = BUILD_META.subn(f'<meta name="halo-build-id" content="{build_id}">'.encode(), html)
    if count != 1:
        sys.exit("halo.html has no halo-build-id meta tag")
    return html


def server_script() -> bytes:
    """tools/web_serve.py, serving its own folder and opening index.html."""
    text = (REPOSITORY / "tools" / "web_serve.py").read_text(encoding="utf-8")
    replacements = [
        ("repository = Path(__file__).resolve().parents[1]", "repository = Path(__file__).resolve().parent"),
        ('url = f"http://{browser_host}:{port}/build/web/halo.html"', 'url = f"http://{browser_host}:{port}/"'),
    ]
    for old, new in replacements:
        if text.count(old) != 1:
            sys.exit(f"tools/web_serve.py changed; update web_package.py ({old!r})")
        text = text.replace(old, new)
    return text.encode("utf-8")


def source_description() -> str:
    head = REPOSITORY / ".git"
    try:
        import subprocess

        return subprocess.run(
            ["git", "-C", str(REPOSITORY), "describe", "--always", "--dirty"],
            check=True, capture_output=True, text=True,
        ).stdout.strip() or "a local checkout"
    except Exception:
        return "a local checkout" if head.exists() else "a source copy"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=REPOSITORY / "dist" / "halo-web.zip")
    arguments = parser.parse_args()

    web = REPOSITORY / "build" / "web"
    port_web = REPOSITORY / "port" / "web"
    game = {}
    digest = hashlib.sha256()
    for name in ("halo.html", "halo.js", "halo.wasm"):
        path = web / name
        if not path.is_file():
            sys.exit(f"missing {path}: run `ninja web` first")
        game[name] = path.read_bytes()
        digest.update(game[name])
    build_id = f"selfhost-{digest.hexdigest()[:20]}"
    game["halo.html"] = stamp(game["halo.html"], build_id)

    files: dict[str, bytes] = {
        "index.html": game["halo.html"],
        **game,
        "coi-serviceworker.js": (port_web / "coi-serviceworker.js").read_bytes(),
        "manifest.webmanifest": (port_web / "manifest.webmanifest").read_bytes(),
        "serve.py": server_script(),
        "README.txt": README.format(build_id=build_id, source=source_description()).encode("utf-8"),
        "assets/maps/PUT-MAPS-HERE.txt": b"The game's Xbox .map files go in this folder (see README.txt).\n",
    }
    assets = port_web / "assets"
    for path in sorted(assets.rglob("*")):
        relative = path.relative_to(assets)
        # (never game data, whatever a checkout holds there)
        if path.is_file() and relative.parts[0] != "maps" and path.suffix.lower() != ".map":
            files[f"assets/{relative.as_posix()}"] = path.read_bytes()

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(arguments.output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in files.items():
            archive.writestr(f"{FOLDER}/{name}", data)
    size = arguments.output.stat().st_size
    print(f"Packaged build {build_id}: {arguments.output} ({len(files)} files, {size / 1e6:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
