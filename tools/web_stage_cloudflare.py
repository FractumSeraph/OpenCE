#!/usr/bin/env python3
"""Stage the browser runtime without copyrighted Halo game data.

Players select an XISO made from their own Xbox disc. The browser validates it
and keeps the required maps in origin-private local storage; no map files are
copied into the deployable asset tree.
"""

from __future__ import annotations

import hashlib
import re
import shutil
import sys
from pathlib import Path


MAX_ASSET_BYTES = 25 * 1024 * 1024
HEADERS = """/*
  Cross-Origin-Opener-Policy: same-origin
  Cross-Origin-Embedder-Policy: require-corp
  Cross-Origin-Resource-Policy: same-origin
  Referrer-Policy: no-referrer
  X-Content-Type-Options: nosniff
  Cache-Control: public, max-age=0, must-revalidate
"""

BUILD_META_PATTERN = re.compile(
    rb'<meta\b(?=[^>]*\bname=(?:["\']halo-build-id["\']|halo-build-id)(?=[\s>]))[^>]*>'
)


def checked_copy(source: Path, destination: Path) -> int:
    if not source.is_file():
        raise FileNotFoundError(f"required web asset is missing: {source}")
    size = source.stat().st_size
    if size > MAX_ASSET_BYTES:
        raise ValueError(
            f"{source.name} is {size:,} bytes; Cloudflare Assets allows "
            f"at most {MAX_ASSET_BYTES:,} bytes per file"
        )
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    return size


def asset_build_id(web_build: Path) -> str:
    digest = hashlib.sha256()
    for name in ("halo.html", "halo.js", "halo.wasm"):
        source = web_build / name
        if not source.is_file():
            raise FileNotFoundError(f"required web asset is missing: {source}")
        digest.update(name.encode("ascii"))
        with source.open("rb") as file:
            for chunk in iter(lambda: file.read(1024 * 1024), b""):
                digest.update(chunk)
    return f"web-{digest.hexdigest()[:24]}"


def stamp_build_id(path: Path, build_id: str) -> None:
    contents = path.read_bytes()
    replacement = f'<meta name="halo-build-id" content="{build_id}">'.encode("ascii")
    updated, count = BUILD_META_PATTERN.subn(replacement, contents, count=1)
    if count != 1:
        raise ValueError(f"halo build metadata is missing from {path}")
    path.write_bytes(updated)


def main() -> int:
    repository = Path(__file__).resolve().parents[1]
    web_build = repository / "build" / "web"
    output = repository / "build" / "cloudflare-web"

    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    build_id = asset_build_id(web_build)

    total = 0
    for name in (
        "halo.html",
        "halo.js",
        "halo.wasm",
    ):
        total += checked_copy(web_build / name, output / name)

    total += checked_copy(
        repository / "port" / "web" / "coi-serviceworker.js",
        output / "coi-serviceworker.js",
    )

    # Cloudflare serves index.html for the root URL. Keep halo.html too so old
    # invite links and the local development URL continue to work.
    total += checked_copy(web_build / "halo.html", output / "index.html")
    stamp_build_id(output / "halo.html", build_id)
    stamp_build_id(output / "index.html", build_id)

    ui_assets = repository / "port" / "web" / "assets" / "ui"
    if not ui_assets.is_dir():
        raise FileNotFoundError(f"required web asset directory is missing: {ui_assets}")
    for source in sorted(path for path in ui_assets.rglob("*") if path.is_file()):
        total += checked_copy(
            source,
            output / "assets" / "ui" / source.relative_to(ui_assets),
        )

    (output / "_headers").write_text(HEADERS, encoding="utf-8")
    print(
        f"Staged {total / (1024 * 1024):.1f} MiB of browser assets in {output} ({build_id})",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (FileNotFoundError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
