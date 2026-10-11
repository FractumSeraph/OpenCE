"""The loading screen's picture taken out of a default.xbe
(tools/xiso_extract.py's loading_picture; port/linux/src/xiso.c does the
same): made-up executables, no game data."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from xiso_extract import LOADING_HEIGHT, LOADING_WIDTH, loading_picture  # noqa: E402

PIXELS = LOADING_WIDTH * LOADING_HEIGHT
SIZES = LOADING_WIDTH.to_bytes(4, "little") + LOADING_HEIGHT.to_bytes(4, "little")


def runs(greys):
    """greys (multiples of 16) as the executable's runs: a byte each, the
    run's length in the high nibble, the grey's high bits in the low one"""
    out = bytearray()
    at = 0
    while at < len(greys):
        length = 1
        while at + length < len(greys) and greys[at + length] == greys[at] and length < 15:
            length += 1
        out.append(length << 4 | greys[at] >> 4)
        at += length
    return bytes(out)


def picture():
    """a made-up picture: bands of grey down the rows"""
    return bytes(((y // 15) % 16) << 4 for y in range(LOADING_HEIGHT) for _ in range(LOADING_WIDTH))


def test_the_picture_is_found_and_written_as_the_tga_the_game_reads():
    greys = picture()
    # (another 320, 240 before it, whose runs do not fill a picture)
    xbe = b"\xcc" * 1000 + SIZES + b"\x10\x00" + b"\x90" * 64 + SIZES + runs(greys) + b"\xcc" * 1000
    tga = loading_picture(xbe)
    assert tga is not None
    # (progress_bar.c: an uncompressed 24-bit picture of 320 by 240)
    assert tga[2] == 2 and tga[16] == 24
    assert int.from_bytes(tga[12:14], "little") == LOADING_WIDTH
    assert int.from_bytes(tga[14:16], "little") == LOADING_HEIGHT
    assert len(tga) == 18 + PIXELS * 3
    # (rows from the bottom, the grey in each channel)
    body = tga[18:]
    for y in (0, 100, LOADING_HEIGHT - 1):
        row = body[(LOADING_HEIGHT - 1 - y) * LOADING_WIDTH * 3:][: LOADING_WIDTH * 3]
        assert row == bytes(grey for grey in greys[y * LOADING_WIDTH:(y + 1) * LOADING_WIDTH] for _ in range(3))


def test_runs_that_do_not_fill_the_picture_exactly_are_not_it():
    greys = picture()
    assert loading_picture(b"\xcc" * 100) is None
    assert loading_picture(SIZES + runs(greys)[:-1]) is None
    # (a run past the picture's end: its last 15 pixels as 14 and then 2)
    assert loading_picture(SIZES + runs(greys)[:-1] + b"\xe0\x20") is None
    # (a run of none)
    assert loading_picture(SIZES + runs(greys)[:-1] + b"\x00\xf0") is None


if __name__ == "__main__":
    test_the_picture_is_found_and_written_as_the_tga_the_game_reads()
    test_runs_that_do_not_fill_the_picture_exactly_are_not_it()
    print("ok")
