#!/usr/bin/env python3
"""The torrents of the Custom Edition maps, and their index
(port/assets/network/map_torrents.txt), which the game reads to download a
map it lacks when joining a game on it (port/linux/src/map_torrents.c).

    python tools/map_torrents.py make --maps <folder> [--maps <folder>...]
        [--resource-maps <folder>] --torrents <folder> [--index <file>]

walks the folders, keeps every Halo Custom Edition cache (as the game
identifies one: cache_file_formats.c), writes a single-file v1 torrent for
each into the torrents folder, and writes (or merges into) the index. The
resource maps folder gives bitmaps.map, sounds.map and loc.map, which every
Custom Edition map needs, listed with a checksum of 0.

A map is known by its file name and its header checksum (the game record's
map version a host sends), so two versions of a map with one name are two
entries. A second file of the same name and checksum is left out. The
torrents are trackerless: the game adds the trackers and web seeds of its
settings, which does not change an info hash, and a torrent a seed box is
given can have them added by any client.

Each torrent's info dictionary is exactly {length, name, piece length,
pieces}, so a machine that has the file can make the torrent again from the
index line alone (the game's host seeds the map it plays that way).

    python tools/map_torrents.py check --index <file> [--torrents <folder>]

checks an index (and that each torrent file matches its line).

The work of a run is remembered in <torrents>/.map_torrents_cache.json by
path, size and modification time, so a rerun hashes only new files.
"""

import argparse
import hashlib
import json
import os
import struct
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

INDEX_FORMAT = "halo map torrents v1"
DEFAULT_INDEX = Path(__file__).resolve().parents[1] / "port/assets/network/map_torrents.txt"
RESOURCE_MAP_NAMES = ("bitmaps.map", "sounds.map", "loc.map")

# cache_file_formats.c
HEADER_BYTES = 0x800
HEADER_SIGNATURE = struct.unpack(">I", b"head")[0]
FOOTER_SIGNATURE = struct.unpack(">I", b"foot")[0]
OPENSAUCE_SIGNATURE = struct.unpack(">I", b"yelo")[0]
VERSION_CUSTOM_EDITION = 609
VERSION_OFFSET = 0x04
FILE_LENGTH_OFFSET = 0x08
COMPRESSED_LENGTH_OFFSET = 0x0C
TAG_DATA_OFFSET_OFFSET = 0x10
TAG_DATA_SIZE_OFFSET = 0x14
NAME_OFFSET = 0x20
BUILD_OFFSET = 0x40
SCENARIO_TYPE_OFFSET = 0x60
CHECKSUM_OFFSET = 0x64
OPENSAUCE_OFFSET = 0x70
OPENSAUCE_FLAGS_OFFSET = 0x06
FOOTER_OFFSET = 0x7FC
STRING_BYTES = 32
TAG_INDEX_BYTES = 0x28
TAG_CACHE_BYTES = 0x01700000
CACHE_FILE_MAXIMUM_BYTES = 0x30000000

READ_BYTES = 4 << 20


class MapError(Exception):
    pass


# ---------- identifying a map, as the game does


def read_u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def read_s32(data, offset):
    return struct.unpack_from("<i", data, offset)[0]


def read_u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def string_field(data, offset):
    field = data[offset:offset + STRING_BYTES]
    end = field.find(b"\0")
    if end < 0:
        raise MapError("an unterminated string in the header")
    return field[:end].decode("latin-1")


def identify(path):
    """The identity of a Custom Edition cache: its header's name, build,
    scenario type and checksum; MapError for a file the game would not play
    as one."""
    size = path.stat().st_size
    if size < HEADER_BYTES:
        raise MapError("too small for a cache header")
    with open(path, "rb") as f:
        header = f.read(HEADER_BYTES)
    if read_u32(header, 0) != HEADER_SIGNATURE or read_u32(header, FOOTER_OFFSET) != FOOTER_SIGNATURE:
        raise MapError("not a cache file (no head and foot signatures)")
    version = read_s32(header, VERSION_OFFSET)
    if version != VERSION_CUSTOM_EDITION:
        raise MapError(f"cache version {version}, not Custom Edition's {VERSION_CUSTOM_EDITION}")
    name = string_field(header, NAME_OFFSET)
    build = string_field(header, BUILD_OFFSET)
    if read_u32(header, OPENSAUCE_OFFSET) == OPENSAUCE_SIGNATURE and \
            read_u16(header, OPENSAUCE_OFFSET + OPENSAUCE_FLAGS_OFFSET):
        raise MapError("needs OpenSauce")
    if read_u32(header, COMPRESSED_LENGTH_OFFSET):
        raise MapError("a compressed cache")
    file_length = read_u32(header, FILE_LENGTH_OFFSET) or size
    if file_length < HEADER_BYTES or file_length > CACHE_FILE_MAXIMUM_BYTES or file_length > size:
        raise MapError(f"a bad file length ({file_length})")
    tag_data_offset = read_u32(header, TAG_DATA_OFFSET_OFFSET)
    tag_data_size = read_u32(header, TAG_DATA_SIZE_OFFSET)
    if tag_data_offset < HEADER_BYTES or tag_data_size < TAG_INDEX_BYTES or tag_data_size > TAG_CACHE_BYTES or \
            tag_data_offset + tag_data_size > file_length:
        raise MapError("a bad tag data range")
    return {
        "name": name,
        "build": build,
        "scenario_type": struct.unpack_from("<h", header, SCENARIO_TYPE_OFFSET)[0],
        "checksum": read_u32(header, CHECKSUM_OFFSET),
        "size": size,
    }


# ---------- bencoding


def bencode(value):
    if isinstance(value, int):
        return b"i%de" % value
    if isinstance(value, bytes):
        return b"%d:" % len(value) + value
    if isinstance(value, str):
        return bencode(value.encode("utf-8"))
    if isinstance(value, list):
        return b"l" + b"".join(bencode(item) for item in value) + b"e"
    if isinstance(value, dict):
        # keys sorted as raw bytes, as the specification asks
        items = sorted((key.encode("utf-8") if isinstance(key, str) else key, item) for key, item in value.items())
        return b"d" + b"".join(bencode(key) + bencode(item) for key, item in items) + b"e"
    raise TypeError(f"cannot bencode {type(value).__name__}")


def bdecode(data):
    """The value bencoded in data (bytes keys and strings)."""
    def parse(index):
        char = data[index:index + 1]
        if char == b"i":
            end = data.index(b"e", index)
            return int(data[index + 1:end]), end + 1
        if char == b"l":
            items = []
            index += 1
            while data[index:index + 1] != b"e":
                item, index = parse(index)
                items.append(item)
            return items, index + 1
        if char == b"d":
            items = {}
            index += 1
            while data[index:index + 1] != b"e":
                key, index = parse(index)
                item, index = parse(index)
                items[key] = item
            return items, index + 1
        colon = data.index(b":", index)
        length = int(data[index:colon])
        return data[colon + 1:colon + 1 + length], colon + 1 + length

    value, end = parse(0)
    if end != len(data):
        raise ValueError("trailing bytes")
    return value


def piece_length_for(size):
    """A piece length that gives a file some 1,000 pieces, a power of two
    from 256 KB to 8 MB."""
    length = 256 << 10
    while length < 8 << 20 and size // length > 1500:
        length <<= 1
    return length


def hash_pieces(path, piece_length):
    """The SHA-1 of each piece of the file, concatenated."""
    pieces = []
    with open(path, "rb") as f:
        while True:
            piece = f.read(piece_length)
            if not piece:
                break
            pieces.append(hashlib.sha1(piece).digest())
    return b"".join(pieces)


def info_dictionary(name, size, piece_length, pieces):
    return {"length": size, "name": name, "piece length": piece_length, "pieces": pieces}


def info_hash(info):
    return hashlib.sha1(bencode(info)).hexdigest()


def torrent_bytes(info, comment):
    return bencode({
        "comment": comment,
        "created by": "OpenCE tools/map_torrents.py",
        "creation date": int(time.time()),
        "info": info,
    })


# ---------- the index

class Entry:
    __slots__ = ("file_name", "checksum", "size", "piece_length", "info_hash")

    def __init__(self, file_name, checksum, size, piece_length, info_hash):
        self.file_name = file_name
        self.checksum = checksum
        self.size = size
        self.piece_length = piece_length
        self.info_hash = info_hash

    @property
    def key(self):
        return (self.file_name.lower(), self.checksum)

    def line(self):
        return f"{self.file_name}\t{self.checksum:08X}\t{self.size}\t{self.piece_length}\t{self.info_hash}"


def read_index(path):
    entries = {}
    if not path.is_file():
        return entries
    with open(path, "r", encoding="utf-8") as f:
        first = f.readline()
        if first.strip() != f"# {INDEX_FORMAT}":
            raise SystemExit(f"{path}: not a map torrent index (its first line is not '# {INDEX_FORMAT}')")
        for number, line in enumerate(f, 2):
            line = line.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) != 5:
                raise SystemExit(f"{path}:{number}: {len(fields)} fields, not 5")
            try:
                entry = Entry(fields[0], int(fields[1], 16), int(fields[2]), int(fields[3]), fields[4].lower())
            except ValueError as error:
                raise SystemExit(f"{path}:{number}: {error}")
            if len(entry.info_hash) != 40 or any(c not in "0123456789abcdef" for c in entry.info_hash):
                raise SystemExit(f"{path}:{number}: a bad info hash")
            entries[entry.key] = entry
    return entries


def write_index(path, entries):
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f"# {INDEX_FORMAT}",
        "# The torrents of the Custom Edition maps (tools/map_torrents.py): each line a map,",
        "# tab-separated: file name, header checksum (the map version a host sends; 0 for",
        "# the resource maps, matched by name alone), size in bytes, piece length, info hash.",
        "# A map's single-file torrent has info = {length, name, piece length, pieces}.",
    ]
    for entry in sorted(entries.values(), key=lambda e: (e.file_name.lower(), e.checksum)):
        lines.append(entry.line())
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


# ---------- making

def map_files(folders):
    for folder in folders:
        folder = Path(folder)
        if not folder.is_dir():
            raise SystemExit(f"{folder}: not a folder")
        for path in sorted(folder.iterdir()):
            if path.is_file() and path.suffix.lower() == ".map":
                yield path


def file_stamp(path):
    stat = path.stat()
    return [stat.st_size, int(stat.st_mtime)]


def make_one(arguments):
    """Hashes one file (in a worker process): its index line's fields, or
    the reason it was left out."""
    path, resource = arguments
    path = Path(path)
    try:
        if resource:
            checksum = 0
            size = path.stat().st_size
            if size < HEADER_BYTES:
                raise MapError("too small")
        else:
            identity = identify(path)
            checksum = identity["checksum"]
            size = identity["size"]
        piece_length = piece_length_for(size)
        pieces = hash_pieces(path, piece_length)
        info = info_dictionary(path.name, size, piece_length, pieces)
        return {
            "file_name": path.name,
            "checksum": checksum,
            "size": size,
            "piece_length": piece_length,
            "info_hash": info_hash(info),
            "pieces": pieces.hex(),
        }
    except (MapError, OSError) as error:
        return {"file_name": path.name, "error": str(error)}


def make(args):
    torrents = Path(args.torrents)
    torrents.mkdir(parents=True, exist_ok=True)
    index_path = Path(args.index)
    entries = read_index(index_path)
    cache_path = torrents / ".map_torrents_cache.json"
    cache = {}
    if cache_path.is_file():
        with open(cache_path, "r", encoding="utf-8") as f:
            cache = json.load(f)
    work = []
    remembered_left_out = []
    skipped = 0
    for resource, folders in ((False, args.maps), (True, [args.resource_maps] if args.resource_maps else [])):
        for path in map_files(folders):
            # (the resource maps are not caches: only the resource maps pass
            # takes them)
            if resource != (path.name.lower() in RESOURCE_MAP_NAMES):
                continue
            key = str(path.resolve())
            remembered = cache.get(key)
            if remembered and remembered.get("stamp") == file_stamp(path):
                # (a file as it was: its entry, or why it was left out)
                if "error" in remembered:
                    remembered_left_out.append((path, remembered["error"]))
                else:
                    result = remembered["result"]
                    entry = Entry(result["file_name"], result["checksum"], result["size"], result["piece_length"],
                                  result["info_hash"])
                    if entry.key not in entries:
                        entries[entry.key] = entry
                skipped += 1
                continue
            work.append((key, path, resource))
    print(f"{len(work)} files to hash ({skipped} remembered from an earlier run)", flush=True)
    total_bytes = sum(path.stat().st_size for _, path, _ in work)
    done_bytes = 0
    started = time.time()
    left_out = list(remembered_left_out)
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        results = pool.map(make_one, [(str(path), resource) for _, path, resource in work])
        for (key, path, resource), result in zip(work, results):
            done_bytes += path.stat().st_size
            cache[key] = {"stamp": file_stamp(path), "result": {k: v for k, v in result.items() if k != "pieces"}}
            if "error" in result:
                cache[key]["error"] = result["error"]
                left_out.append((path, result["error"]))
            else:
                entry = Entry(result["file_name"], result["checksum"], result["size"], result["piece_length"],
                              result["info_hash"])
                duplicate = entry.key in entries
                if not duplicate:
                    entries[entry.key] = entry
                torrent_path = torrents / f"{entry.info_hash}.torrent"
                if not torrent_path.is_file():
                    info = info_dictionary(entry.file_name, entry.size, entry.piece_length,
                                           bytes.fromhex(result["pieces"]))
                    torrent_path.write_bytes(torrent_bytes(info, f"Halo Custom Edition map {entry.file_name}"))
                if duplicate:
                    left_out.append((path, "the same name and checksum as another file"))
            elapsed = time.time() - started
            rate = done_bytes / elapsed if elapsed > 0 else 0
            remaining = (total_bytes - done_bytes) / rate if rate > 0 else 0
            print(f"[{done_bytes >> 20:,} of {total_bytes >> 20:,} MB, {rate / (1 << 20):.0f} MB/s, "
                  f"{remaining / 60:.0f} min left] {path.name}"
                  + (f": {result['error']}" if "error" in result else ""), flush=True)
            if done_bytes and (done_bytes >> 30) != ((done_bytes - path.stat().st_size) >> 30):
                # (the cache and the index are saved as the run goes, so an
                # interrupted one keeps its work)
                with open(cache_path, "w", encoding="utf-8") as f:
                    json.dump(cache, f)
                write_index(index_path, entries)
    with open(cache_path, "w", encoding="utf-8") as f:
        json.dump(cache, f)
    write_index(index_path, entries)
    print(f"\n{len(entries)} maps in {index_path}; {len(left_out)} files left out", flush=True)
    for path, reason in left_out:
        print(f"  {path.name}: {reason}")


# ---------- checking

def check(args):
    index_path = Path(args.index)
    entries = read_index(index_path)
    failures = 0
    torrents = Path(args.torrents) if args.torrents else None
    for entry in entries.values():
        if entry.size < HEADER_BYTES or entry.piece_length < 16384 or entry.piece_length & (entry.piece_length - 1):
            print(f"{entry.file_name}: a bad size or piece length")
            failures += 1
        if torrents:
            path = torrents / f"{entry.info_hash}.torrent"
            if not path.is_file():
                print(f"{entry.file_name}: no torrent file {path.name}")
                failures += 1
                continue
            torrent = bdecode(path.read_bytes())
            info = torrent[b"info"]
            expected = info_dictionary(entry.file_name, entry.size, entry.piece_length, info[b"pieces"])
            if hashlib.sha1(bencode(info)).hexdigest() != entry.info_hash or \
                    hashlib.sha1(bencode(expected)).hexdigest() != entry.info_hash:
                print(f"{entry.file_name}: the torrent file does not match its line")
                failures += 1
            pieces = (entry.size + entry.piece_length - 1) // entry.piece_length
            if len(info[b"pieces"]) != 20 * pieces:
                print(f"{entry.file_name}: {len(info[b'pieces']) // 20} piece hashes, not {pieces}")
                failures += 1
    print(f"{len(entries)} entries, {failures} problems")
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    make_parser = commands.add_parser("make", help="make torrents and the index from map folders")
    make_parser.add_argument("--maps", action="append", default=[], metavar="FOLDER", required=True,
                             help="a folder of Custom Edition maps (may be given several times)")
    make_parser.add_argument("--resource-maps", metavar="FOLDER",
                             help="a folder with Custom Edition's bitmaps.map, sounds.map and loc.map")
    make_parser.add_argument("--torrents", metavar="FOLDER", required=True,
                             help="where the .torrent files go (named by info hash)")
    make_parser.add_argument("--index", metavar="FILE", default=str(DEFAULT_INDEX),
                             help=f"the index to write or merge into (default: {DEFAULT_INDEX})")
    make_parser.add_argument("--jobs", type=int, default=max(2, min(8, (os.cpu_count() or 2) // 2)),
                             help="files hashed at once")
    make_parser.set_defaults(run=make)
    check_parser = commands.add_parser("check", help="check an index and its torrent files")
    check_parser.add_argument("--index", metavar="FILE", default=str(DEFAULT_INDEX))
    check_parser.add_argument("--torrents", metavar="FOLDER")
    check_parser.set_defaults(run=check)
    args = parser.parse_args()
    sys.exit(args.run(args) or 0)


if __name__ == "__main__":
    main()
