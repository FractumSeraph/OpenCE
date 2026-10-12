"""The map torrents' BitTorrent client (port/linux/src/torrent*.c) alone, as
tools/torrent_check.c runs it outside the game (ninja linux builds it as
build/linux/torrent_check): one copy seeds a made-up file on the loopback,
another downloads it from that peer alone, at an upload limit, and the
file that comes must be the seeded one byte for byte. No game data and no
network beyond the loopback (no trackers, no web seeds).
"""
import random
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import map_torrents  # noqa: E402

TORRENT_CHECK = ROOT / "build/linux/torrent_check"
# (a few pieces of 256 KB, the last a short one)
FILE_SIZE = 3 * 256 * 1024 + 12345
PIECE_LENGTH = 256 * 1024


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


@pytest.mark.skipif(not TORRENT_CHECK.exists(), reason="needs ninja linux's build/linux/torrent_check")
def test_a_file_seeded_on_the_loopback_is_downloaded_whole(tmp_path):
    data = random.Random(7).randbytes(FILE_SIZE)
    seeded = tmp_path / "seed" / "made_up.map"
    seeded.parent.mkdir()
    seeded.write_bytes(data)
    info = map_torrents.info_dictionary("made_up.map", FILE_SIZE, PIECE_LENGTH,
                                        map_torrents.hash_pieces(seeded, PIECE_LENGTH))
    info_hash = map_torrents.info_hash(info)
    port = free_port()
    downloaded = tmp_path / "get" / "made_up.map"
    downloaded.parent.mkdir()

    seeder = subprocess.Popen([str(TORRENT_CHECK), "seed", str(seeded), info_hash, str(PIECE_LENGTH), str(port),
                               "--upload-limit", str(4 << 20)],
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    try:
        # (the seeder says its port once it listens, then checks the file; its
        # log, on stderr, is left out)
        first = seeder.stdout.readline()
        assert re.match(rf"port {port}\b", first), first
        time.sleep(1)
        result = subprocess.run([str(TORRENT_CHECK), "get", "made_up.map", str(FILE_SIZE), info_hash,
                                 str(PIECE_LENGTH), f"127.0.0.1:{port}", str(downloaded), "60"],
                                capture_output=True, text=True, timeout=90)
    finally:
        seeder.kill()
        seeder.wait()
    assert result.returncode == 0, result.stdout[-2000:] + result.stderr[-2000:]
    assert "complete" in result.stdout
    assert downloaded.read_bytes() == data


@pytest.mark.skipif(not TORRENT_CHECK.exists(), reason="needs ninja linux's build/linux/torrent_check")
def test_a_peer_with_other_data_gives_no_file(tmp_path):
    """the downloader checks each piece: a seeder whose file is not the
    torrent's (another version of a map, say) gives nothing it keeps"""
    data = random.Random(7).randbytes(FILE_SIZE)
    wanted = tmp_path / "wanted.map"
    wanted.write_bytes(data)
    info_hash = map_torrents.info_hash(map_torrents.info_dictionary(
        "made_up.map", FILE_SIZE, PIECE_LENGTH, map_torrents.hash_pieces(wanted, PIECE_LENGTH)))
    # (the seeder's file has the size, but other bytes: it fails its own check)
    other = tmp_path / "seed" / "made_up.map"
    other.parent.mkdir()
    other.write_bytes(random.Random(8).randbytes(FILE_SIZE))
    port = free_port()
    downloaded = tmp_path / "get" / "made_up.map"
    downloaded.parent.mkdir()
    seeder = subprocess.Popen([str(TORRENT_CHECK), "seed", str(other), info_hash, str(PIECE_LENGTH), str(port)],
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    try:
        seeder.stdout.readline()
        time.sleep(1)
        result = subprocess.run([str(TORRENT_CHECK), "get", "made_up.map", str(FILE_SIZE), info_hash,
                                 str(PIECE_LENGTH), f"127.0.0.1:{port}", str(downloaded), "8"],
                                capture_output=True, text=True, timeout=60)
    finally:
        seeder.kill()
        seeder.wait()
    assert result.returncode != 0
    assert "complete" not in result.stdout
    assert not downloaded.exists() or downloaded.read_bytes() != data


@pytest.mark.skipif(not TORRENT_CHECK.exists(), reason="needs ninja linux's build/linux/torrent_check")
def test_a_map_is_taken_out_of_an_archives_zip(tmp_path):
    """the game's unzip (port/linux/src/map_archives_zip.c): the file of the
    name and size in any folder, deflated or stored; anything else refused"""
    import zipfile

    data = random.Random(9).randbytes(FILE_SIZE)
    for method in (zipfile.ZIP_DEFLATED, zipfile.ZIP_STORED):
        archive_path = tmp_path / f"pack{method}.zip"
        with zipfile.ZipFile(archive_path, "w", method) as archive:
            archive.writestr("readme.txt", "a map")
            # (one of the name but another size first: not it)
            archive.writestr("old/Made_Up.map", data[:1000])
            archive.writestr("maps/Made_Up.map", data)
        out = tmp_path / f"out{method}.map"
        result = subprocess.run([str(TORRENT_CHECK), "unzip", str(archive_path), "made_up.map", str(FILE_SIZE),
                                 str(out)], capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stdout + result.stderr
        assert out.read_bytes() == data
    # no such file, a damaged zip, a zip cut short: nothing written
    archive_path = tmp_path / f"pack{zipfile.ZIP_DEFLATED}.zip"
    whole = archive_path.read_bytes()
    out = tmp_path / "bad.map"
    for label, zip_bytes, size in (("no file of that size", whole, FILE_SIZE + 1),
                                   ("damaged data", whole[:2000] + bytes(len(whole) - 4000) + whole[-2000:], FILE_SIZE),
                                   ("cut short", whole[:len(whole) // 2], FILE_SIZE),
                                   ("not a zip", b"junk" * 100, FILE_SIZE)):
        bad = tmp_path / "bad.zip"
        bad.write_bytes(zip_bytes)
        result = subprocess.run([str(TORRENT_CHECK), "unzip", str(bad), "made_up.map", str(size), str(out)],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode != 0, label
        assert "failed" in result.stdout, label
        assert not out.exists(), label
