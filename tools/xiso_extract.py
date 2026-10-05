#!/usr/bin/env python3
"""Extract Halo's ``maps`` directory from an Xbox XDVDFS disc image.

The extractor deliberately writes into a temporary sibling directory and
renames it only after every file has been copied.  It never replaces an
existing destination, so an interrupted or repeated run cannot make a
partial ``assets/maps`` tree look complete.
"""

from __future__ import annotations

import argparse
import ctypes
import errno
import os
import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterable


SECTOR_SIZE = 2048
VOLUME_DESCRIPTOR_OFFSET = 0x10000
ENTRY_HEADER_SIZE = 14
ATTRIBUTE_DIRECTORY = 0x10
MAXIMUM_DIRECTORY_SIZE = 4 << 20
MAXIMUM_ENTRIES = 256
MAXIMUM_VISITED_NODES = 4096
COPY_BUFFER_SIZE = 1 << 20
VOLUME_MAGIC = b"MICROSOFT*XBOX*MEDIA"

# Plain XISO, followed by extract-xiso's GLOBAL, XGD3, and XGD1 offsets.
PARTITION_OFFSETS = (0, 0x0FD90000, 0x02080000, 0x18300000)


class XisoError(RuntimeError):
    """An invalid, incomplete, or unsupported XDVDFS image."""


@dataclass(frozen=True)
class Entry:
    name: str
    sector: int
    size: int
    is_directory: bool


@dataclass(frozen=True)
class Catalog:
    partition: int
    files: tuple[Entry, ...]

    @property
    def total_size(self) -> int:
        return sum(entry.size for entry in self.files)


class Image:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.file: BinaryIO = path.open("rb", buffering=0)
        self.initial_stat = os.fstat(self.file.fileno())
        self.partition = 0

    def close(self) -> None:
        self.file.close()

    def __enter__(self) -> "Image":
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()

    def read_at(self, offset: int, size: int, description: str) -> bytes:
        if offset < 0 or size < 0 or offset + size > self.initial_stat.st_size:
            raise XisoError(
                f"{description} lies outside the disc image; the download may be incomplete"
            )
        self.file.seek(offset)
        data = self.file.read(size)
        if len(data) != size:
            raise XisoError(
                f"could not read {description}; the disc image may still be downloading"
            )
        return data

    def ensure_unchanged(self) -> None:
        current = os.fstat(self.file.fileno())
        original = self.initial_stat
        fields = ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns")
        if any(getattr(current, field) != getattr(original, field) for field in fields):
            raise XisoError("the disc image changed while it was being read; wait for the download to finish")


def _u16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little")


def _u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little")


def _find_volume(image: Image) -> tuple[int, int]:
    for partition in PARTITION_OFFSETS:
        descriptor_offset = partition + VOLUME_DESCRIPTOR_OFFSET
        try:
            descriptor = image.read_at(
                descriptor_offset, SECTOR_SIZE, "an XDVDFS volume descriptor"
            )
        except XisoError:
            continue
        if (
            descriptor[: len(VOLUME_MAGIC)] == VOLUME_MAGIC
            and descriptor[0x7EC : 0x7EC + len(VOLUME_MAGIC)] == VOLUME_MAGIC
        ):
            image.partition = partition
            return _u32(descriptor, 20), _u32(descriptor, 24)
    raise XisoError("this is not an Xbox XDVDFS disc image")


def _validate_extent(image: Image, sector: int, size: int, description: str) -> int:
    offset = image.partition + sector * SECTOR_SIZE
    if sector < 0 or size < 0 or offset + size > image.initial_stat.st_size:
        raise XisoError(f"{description} points outside the disc image")
    return offset


def _read_directory(image: Image, sector: int, size: int, description: str) -> bytes:
    if size <= 0 or size > MAXIMUM_DIRECTORY_SIZE:
        raise XisoError(f"{description} has an invalid directory size ({size} bytes)")
    offset = _validate_extent(image, sector, size, description)
    return image.read_at(offset, size, description)


def _walk_directory(table: bytes, want_directories: bool) -> list[Entry]:
    entries: list[Entry] = []
    visited: set[int] = set()

    def walk(pointer: int, depth: int) -> None:
        offset = pointer * 4
        if depth > 64:
            raise XisoError("an XDVDFS directory tree is nested too deeply")
        if len(visited) >= MAXIMUM_VISITED_NODES:
            raise XisoError("an XDVDFS directory tree has too many nodes")
        if offset in visited:
            raise XisoError("an XDVDFS directory tree contains a cycle")
        if offset < 0 or offset + ENTRY_HEADER_SIZE > len(table):
            raise XisoError("an XDVDFS directory entry points outside its table")

        visited.add(offset)
        left = _u16(table, offset)
        right = _u16(table, offset + 2)
        # 0xffff marks padding or an empty directory.
        if left == 0xFFFF:
            return

        name_length = table[offset + 13]
        name_end = offset + ENTRY_HEADER_SIZE + name_length
        if name_length == 0 or name_end > len(table):
            raise XisoError("an XDVDFS directory entry has an invalid name")

        if left:
            walk(left, depth + 1)

        name_bytes = table[offset + ENTRY_HEADER_SIZE : name_end]
        try:
            name = name_bytes.decode("ascii")
        except UnicodeDecodeError as error:
            raise XisoError("an XDVDFS filename is not ASCII") from error
        if name in (".", "..") or "/" in name or "\\" in name or "\0" in name:
            raise XisoError(f"unsafe filename in the disc image: {name!r}")

        attributes = table[offset + 12]
        is_directory = bool(attributes & ATTRIBUTE_DIRECTORY)
        if is_directory == want_directories:
            if len(entries) >= MAXIMUM_ENTRIES:
                raise XisoError("an XDVDFS directory contains too many entries")
            entries.append(
                Entry(
                    name=name,
                    sector=_u32(table, offset + 4),
                    size=_u32(table, offset + 8),
                    is_directory=is_directory,
                )
            )

        if right:
            walk(right, depth + 1)

    walk(0, 0)
    return entries


def read_catalog(image: Image) -> Catalog:
    root_sector, root_size = _find_volume(image)
    root = _read_directory(image, root_sector, root_size, "the XDVDFS root directory")
    map_directories = [
        entry for entry in _walk_directory(root, True) if entry.name.casefold() == "maps"
    ]
    if len(map_directories) != 1:
        raise XisoError("the disc image does not contain exactly one maps directory")

    maps_entry = map_directories[0]
    maps = _read_directory(image, maps_entry.sector, maps_entry.size, "the maps directory")
    files = tuple(_walk_directory(maps, False))
    if not files:
        raise XisoError("the maps directory is empty")

    folded_names: set[str] = set()
    for entry in files:
        folded = entry.name.casefold()
        if folded in folded_names:
            raise XisoError(f"the maps directory contains a duplicate filename: {entry.name}")
        folded_names.add(folded)
        _validate_extent(image, entry.sector, entry.size, f"maps/{entry.name}")
    if "ui.map" not in folded_names:
        raise XisoError("the maps directory has no ui.map; this is not a Halo disc")

    image.ensure_unchanged()
    return Catalog(partition=image.partition, files=files)


def _format_size(size: int) -> str:
    value = float(size)
    for suffix in ("B", "KiB", "MiB", "GiB"):
        if value < 1024.0 or suffix == "GiB":
            return f"{value:.1f} {suffix}"
        value /= 1024.0
    raise AssertionError("unreachable")


def _copy_entry(image: Image, entry: Entry, destination: Path) -> None:
    source_offset = _validate_extent(image, entry.sector, entry.size, f"maps/{entry.name}")
    remaining = entry.size
    image.file.seek(source_offset)
    # O_BINARY: on Windows os.open defaults to text mode, which would turn
    # every 0x0A byte of a map into 0x0D 0x0A.
    descriptor = os.open(
        destination,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0),
        0o644,
    )
    try:
        while remaining:
            chunk = image.file.read(min(COPY_BUFFER_SIZE, remaining))
            if not chunk:
                raise XisoError(
                    f"could not finish reading maps/{entry.name}; the image may be incomplete"
                )
            view = memoryview(chunk)
            while view:
                written = os.write(descriptor, view)
                if written <= 0:
                    raise OSError(f"could not write {destination}")
                view = view[written:]
            remaining -= len(chunk)
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _fsync_directory(path: Path) -> None:
    if os.name == "nt":
        return  # Windows cannot open a directory as a file descriptor.
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _lexists(path: Path) -> bool:
    return os.path.lexists(path)


def _atomic_rename_noreplace(source: Path, destination: Path) -> None:
    """Atomically rename a directory, failing if destination already exists."""
    if os.name == "nt":
        # os.rename on Windows already refuses to replace an existing path.
        os.rename(source, destination)
        return
    libc = ctypes.CDLL(None, use_errno=True)
    source_bytes = os.fsencode(source)
    destination_bytes = os.fsencode(destination)

    if sys.platform == "darwin" and hasattr(libc, "renamex_np"):
        renamex_np = libc.renamex_np
        renamex_np.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint)
        renamex_np.restype = ctypes.c_int
        # sys/stdio.h: RENAME_EXCL prevents replacement as part of the rename.
        if renamex_np(source_bytes, destination_bytes, 0x00000004) == 0:
            return
        error_number = ctypes.get_errno()
        if error_number == errno.EEXIST:
            raise XisoError(f"destination already exists; refusing to replace it: {destination}")
        raise OSError(error_number, os.strerror(error_number), destination)

    if sys.platform.startswith("linux") and hasattr(libc, "renameat2"):
        renameat2 = libc.renameat2
        renameat2.argtypes = (
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_uint,
        )
        renameat2.restype = ctypes.c_int
        # linux/fs.h: RENAME_NOREPLACE, with both paths relative to cwd.
        if renameat2(-100, source_bytes, -100, destination_bytes, 1) == 0:
            return
        error_number = ctypes.get_errno()
        if error_number == errno.EEXIST:
            raise XisoError(f"destination already exists; refusing to replace it: {destination}")
        raise OSError(error_number, os.strerror(error_number), destination)

    # The browser target is developed on macOS, where renamex_np is available.
    # Retain a conservative compatibility fallback for other Python hosts.
    if _lexists(destination):
        raise XisoError(f"destination already exists; refusing to replace it: {destination}")
    os.rename(source, destination)


def extract_maps(image_path: Path, destination: Path) -> Catalog:
    image_path = image_path.expanduser().resolve(strict=True)
    destination = destination.expanduser().resolve(strict=False)
    destination.parent.mkdir(parents=True, exist_ok=True)

    if _lexists(destination):
        raise XisoError(f"destination already exists; refusing to replace it: {destination}")

    lock_path = destination.parent / f".{destination.name}.extract.lock"
    try:
        lock_descriptor = os.open(lock_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError as error:
        raise XisoError(f"another extraction appears to be active ({lock_path})") from error

    try:
        with Image(image_path) as image:
            catalog = read_catalog(image)
            free_space = shutil.disk_usage(destination.parent).free
            if free_space < catalog.total_size + (64 << 20):
                raise XisoError(
                    f"not enough free space: need about {_format_size(catalog.total_size)}, "
                    f"have {_format_size(free_space)}"
                )

            print(
                f"Found {len(catalog.files)} files ({_format_size(catalog.total_size)}) "
                f"in XDVDFS partition 0x{catalog.partition:x}."
            )
            with tempfile.TemporaryDirectory(
                dir=destination.parent, prefix=f".{destination.name}.partial-"
            ) as temporary_name:
                temporary = Path(temporary_name)
                for index, entry in enumerate(catalog.files, 1):
                    print(
                        f"[{index:02d}/{len(catalog.files):02d}] "
                        f"maps/{entry.name} ({_format_size(entry.size)})",
                        flush=True,
                    )
                    _copy_entry(image, entry, temporary / entry.name)

                image.ensure_unchanged()
                _fsync_directory(temporary)
                # The lock serializes this tool's writers.  The final rename is atomic,
                # so readers see either no maps directory or the complete directory.
                _atomic_rename_noreplace(temporary, destination)
                _fsync_directory(destination.parent)

            print(f"Maps ready at {destination}")
            return catalog
    finally:
        os.close(lock_descriptor)
        try:
            lock_path.unlink()
        except FileNotFoundError:
            pass


def inspect_image(image_path: Path) -> Catalog:
    with Image(image_path.expanduser().resolve(strict=True)) as image:
        return read_catalog(image)


def _print_catalog(catalog: Catalog) -> None:
    print(
        f"XDVDFS partition 0x{catalog.partition:x}: {len(catalog.files)} files, "
        f"{_format_size(catalog.total_size)}"
    )
    for entry in catalog.files:
        print(f"{entry.name}\t{entry.size}")


def main(argv: Iterable[str] | None = None) -> int:
    repository = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="Halo Xbox XISO/XDVDFS image")
    parser.add_argument(
        "--output",
        type=Path,
        default=repository / "assets" / "maps",
        help="final maps directory (default: %(default)s)",
    )
    parser.add_argument(
        "--list-only", action="store_true", help="validate and list files without extracting"
    )
    arguments = parser.parse_args(argv)

    try:
        if arguments.list_only:
            _print_catalog(inspect_image(arguments.image))
        else:
            extract_maps(arguments.image, arguments.output)
    except (OSError, XisoError) as error:
        parser.exit(1, f"xiso_extract.py: error: {error}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
