#!/usr/bin/env python3
"""Package the self-hosting kit: the game, its server, and everything they run on.

    python tools/selfhost_package.py --platform linux-x64 \
        --site dist/halo-web.zip --signaling path/to/index.js \
        --gateway path/to/halo-native-gateway --node path/to/node \
        --node-license path/to/LICENSE [--output dist/halo-server-linux-x64.zip]

The zip holds one folder, halo-server/, which runs as it is (see
services/selfhost/README.md): the launchers and docs (services/selfhost),
public/ (the game, from halo-web.zip; no maps), and server/ with the
server, its npm packages for this platform (`npm ci --omit=dev`, run here:
so build each platform's kit on that platform), the lobby service bundle,
the native gateway and Node.js. No config.json and no data/: the server
creates them on first start, and an update copied over a kit keeps them.
"""

from __future__ import annotations

import argparse
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
SELFHOST = REPOSITORY / "services" / "selfhost"
FOLDER = "halo-server"
PLATFORMS = {"linux-x64": "", "win32-x64": ".exe"}
# (what halo-web.zip has only for serving it on its own)
SITE_ONLY = {"serve.py", "README.txt"}


def copy_selfhost(stage: Path) -> None:
    for path in sorted(SELFHOST.rglob("*")):
        relative = path.relative_to(SELFHOST)
        # (a checkout's local runs: see .gitignore)
        if relative.parts[0] in {"public", "data", "logs", "config.json"} or (
            relative.parts[0] == "server" and len(relative.parts) > 1 and
            relative.parts[1] in {"node_modules", "signaling", "bin", "runtime"}
        ):
            continue
        if path.is_file():
            target = stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)


def copy_site(site_zip: Path, public: Path) -> None:
    with zipfile.ZipFile(site_zip) as archive:
        for name in archive.namelist():
            if name.endswith("/"):
                continue
            parts = name.split("/", 1)
            if len(parts) != 2 or parts[0] != "halo-web" or parts[1] in SITE_ONLY:
                continue
            if parts[1].lower().endswith(".map"):
                sys.exit(f"{site_zip} holds game data ({name}); refusing to package it")
            target = public / parts[1]
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(name))


def npm_install(server: Path) -> None:
    npm = shutil.which("npm")
    if not npm:
        sys.exit("npm is not on PATH")
    command = [npm, "ci", "--omit=dev", "--no-audit", "--no-fund"]
    if os.name == "nt":
        command = ["cmd", "/c", *command]
    subprocess.run(command, cwd=server, check=True)


def executable(relative: str, platform: str) -> bool:
    if platform == "win32-x64":
        return False
    return (
        relative.endswith(".sh")
        or relative == "server/runtime/linux-x64/node"
        or relative == "server/bin/halo-native-gateway-linux-x64"
        or relative.endswith("/workerd-linux-64/bin/workerd")
    )


def write_zip(stage: Path, output: Path, platform: str) -> int:
    count = 0
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(stage.rglob("*")):
            relative = path.relative_to(stage).as_posix()
            # (npm's command shims: links, and not needed to run)
            if "/node_modules/.bin/" in f"/{relative}" or path.is_symlink() or not path.is_file():
                continue
            info = zipfile.ZipInfo(f"{FOLDER}/{relative}", date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            mode = 0o755 if executable(relative, platform) else 0o644
            info.external_attr = (stat.S_IFREG | mode) << 16
            archive.writestr(info, path.read_bytes())
            count += 1
    return count


def merge(windows_zip: Path, linux_zip: Path, output: Path) -> int:
    """One kit for both: what either has, so the folder runs on Windows and
    Linux x64 alike (as a folder copied between them must)."""
    windows, linux = zipfile.ZipFile(windows_zip), zipfile.ZipFile(linux_zip)
    entries: dict[str, tuple[zipfile.ZipFile, zipfile.ZipInfo]] = {}
    for info in windows.infolist():
        if not info.is_dir():
            entries[info.filename] = (windows, info)
    for info in linux.infolist():
        if info.is_dir():
            continue
        name = info.filename
        if name not in entries:
            entries[name] = (linux, info)
            continue
        ours = windows.read(name)
        theirs = linux.read(info)
        if ours == theirs:
            continue
        if ours.replace(b"\r\n", b"\n") == theirs.replace(b"\r\n", b"\n"):
            # (a Windows checkout's line endings: either works; take Linux's)
            entries[name] = (linux, info)
        elif name.endswith("/node_modules/workerd/bin/workerd"):
            # npm's install puts Linux's engine here too, for the workerd
            # command alone; Miniflare finds each platform's engine in its
            # own package. The script the Windows install keeps runs on both.
            continue
        elif name.endswith("/node_modules/.package-lock.json"):
            # (npm's record of what it installed: not read at run time)
            continue
        else:
            sys.exit(f"the kits differ in {name}; cannot merge them")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(entries):
            source, info = entries[name]
            archive.writestr(info, source.read(info))
    print(f"Merged {windows_zip.name} and {linux_zip.name}: {output} "
          f"({len(entries)} files, {output.stat().st_size / 1e6:.1f} MB)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--merge", nargs=2, type=Path, metavar=("WINDOWS_KIT", "LINUX_KIT"),
                        help="instead: one kit for Windows and Linux from the two (dist/halo-server.zip)")
    parser.add_argument("--platform", choices=sorted(PLATFORMS))
    parser.add_argument("--site", type=Path, help="halo-web.zip (tools/web_package.py)")
    parser.add_argument("--signaling", type=Path, help="the lobby service bundle (index.js)")
    parser.add_argument("--gateway", type=Path, help="the native gateway executable")
    parser.add_argument("--node", type=Path, help="the Node.js executable")
    parser.add_argument("--node-license", type=Path, help="Node.js's LICENSE")
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    if arguments.merge:
        return merge(*arguments.merge, arguments.output or REPOSITORY / "dist" / "halo-server.zip")
    missing = [name for name in ("platform", "site", "signaling", "gateway", "node", "node_license")
               if getattr(arguments, name) is None]
    if missing:
        parser.error("needs --" + ", --".join(name.replace("_", "-") for name in missing))
    platform = arguments.platform
    suffix = PLATFORMS[platform]
    output = arguments.output or REPOSITORY / "dist" / f"halo-server-{platform}.zip"

    with tempfile.TemporaryDirectory() as temporary:
        stage = Path(temporary) / FOLDER
        copy_selfhost(stage)
        copy_site(arguments.site, stage / "public")
        (stage / "public" / "assets" / "maps").mkdir(parents=True, exist_ok=True)
        (stage / "public" / "assets" / "custom_maps").mkdir(parents=True, exist_ok=True)
        server = stage / "server"
        (server / "signaling").mkdir(parents=True)
        shutil.copy2(arguments.signaling, server / "signaling" / "index.js")
        (server / "bin").mkdir()
        shutil.copy2(arguments.gateway, server / "bin" / f"halo-native-gateway-{platform}{suffix}")
        (server / "runtime" / platform).mkdir(parents=True)
        shutil.copy2(arguments.node, server / "runtime" / platform / f"node{suffix}")
        shutil.copy2(arguments.node_license, server / "runtime" / "NODE-LICENSE.txt")
        npm_install(server)
        if platform == "linux-x64":
            # (the Windows service scripts mean nothing on Linux)
            shutil.rmtree(server / "windows")
            (stage / "Start Halo (Windows).bat").unlink()
        else:
            (stage / "start-halo.sh").unlink()
        count = write_zip(stage, output, platform)

    print(f"Packaged the {platform} kit: {output} ({count} files, {output.stat().st_size / 1e6:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
