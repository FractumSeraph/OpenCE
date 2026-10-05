#!/usr/bin/env python3
"""Build and run Halo in the default browser."""

from __future__ import annotations

import argparse
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Sequence


class LauncherError(RuntimeError):
    """A launcher prerequisite or command failed."""


def _command_text(command: Sequence[object]) -> str:
    return shlex.join(str(part) for part in command)


def _run(command: Sequence[object], repository: Path, dry_run: bool) -> None:
    print(f"+ {_command_text(command)}", flush=True)
    if dry_run:
        return
    try:
        subprocess.run(
            [str(part) for part in command], cwd=repository, check=True
        )
    except subprocess.CalledProcessError as error:
        raise LauncherError(
            f"command failed with exit status {error.returncode}: "
            f"{_command_text(command)}"
        ) from error


def _find_program(name: str) -> Path:
    program = shutil.which(name)
    if program is None:
        raise LauncherError(
            f"{name} is not installed; on macOS, run: brew install {name}"
        )
    return Path(program)


def _find_emcc(repository: Path) -> Path:
    local = repository / "build" / "emsdk" / "upstream" / "emscripten" / "emcc"
    if local.is_file():
        return local
    program = shutil.which("emcc")
    if program is not None:
        return Path(program)
    raise LauncherError(
        "Emscripten is not installed. Install the emsdk into build/emsdk "
        "or activate an emsdk that provides emcc on PATH."
    )


def _has_release_web_target(build_file: Path) -> bool:
    try:
        contents = build_file.read_text(encoding="utf-8")
    except FileNotFoundError:
        return False
    return (
        "build web: phony" in contents
        and "-DHALO_RELEASE" in contents
        and "-sASSERTIONS=0" in contents
    )


def _require_web_outputs(repository: Path) -> None:
    missing = [
        path
        for path in (
            repository / "build" / "web" / "halo.html",
            repository / "build" / "web" / "halo.js",
            repository / "build" / "web" / "halo.wasm",
        )
        if not path.is_file()
    ]
    if missing:
        names = ", ".join(str(path.relative_to(repository)) for path in missing)
        raise LauncherError(f"browser build is incomplete; missing: {names}")


def _port(value: str) -> int:
    try:
        port = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("port must be an integer") from error
    if not 0 <= port <= 65535:
        raise argparse.ArgumentTypeError("port must be between 0 and 65535")
    return port


def main(argv: Iterable[str] | None = None) -> int:
    repository = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--iso",
        type=Path,
        help="extract maps from this Halo Xbox XISO/ISO when assets/maps is absent",
    )
    parser.add_argument(
        "--port",
        type=_port,
        default=8765,
        help="local server port; 0 chooses an available port (default: %(default)s)",
    )
    parser.add_argument(
        "--no-open", action="store_true", help="do not open the default browser"
    )
    parser.add_argument(
        "--skip-build", action="store_true", help="serve the existing browser build"
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="print commands without running them"
    )
    arguments = parser.parse_args(argv)

    maps = repository / "assets" / "maps"
    if not (maps / "ui.map").is_file():
        if arguments.iso is None:
            raise LauncherError(
                "game data is missing; pass --iso /path/to/Halo.iso on the first run"
            )
        image = arguments.iso.expanduser().resolve()
        if not image.is_file():
            raise LauncherError(f"disc image does not exist: {image}")
        _run(
            [
                sys.executable,
                repository / "tools" / "xiso_extract.py",
                image,
                "--output",
                maps,
            ],
            repository,
            arguments.dry_run,
        )

    if not arguments.skip_build:
        ninja = _find_program("ninja")
        emcc = _find_emcc(repository)
        if not _has_release_web_target(repository / "build.ninja"):
            _run(
                [
                    sys.executable,
                    repository / "configure.py",
                    "--release",
                    "--pgo=off",
                    "--lto=off",
                    f"--web-cc={emcc}",
                ],
                repository,
                arguments.dry_run,
            )
        _run([ninja, "web"], repository, arguments.dry_run)

    if not arguments.dry_run:
        _require_web_outputs(repository)

    server = [
        sys.executable,
        repository / "tools" / "web_serve.py",
        "--port",
        arguments.port,
    ]
    if not arguments.no_open:
        server.append("--open")
    _run(server, repository, arguments.dry_run)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nStopped.")
        sys.exit(130)
    except LauncherError as error:
        sys.exit(f"web_run.py: error: {error}")
