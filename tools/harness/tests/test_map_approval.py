"""Port (this fork): the approved map list (port/linux/src/map_torrents.c's
map_torrents_approved_hash, map_torrents_approved and
map_torrents_unapproved_text, as they are): the maps' torrent index's share
column approves a host's offer, its name and versions a repository's map."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, function, mutated, read, run  # noqa: E402

CASES = ["offer-hash", "version", "text"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    "any-hash": (("map_torrents_approved_hash", "!memcmp(entry->share, hash, sizeof(entry->share))", "1"),
                 "offer-hash"),
    "any-name": (("map_torrents_approved", "if (!same_name(torrent_maps_index.names + entry->name, file))",
                  "if (0)"), "version"),
    "any-version": (("map_torrents_approved", "entry->checksum == version ||", "1 ||"), "version"),
    "no-hash-text": (("map_torrents_unapproved_text", "snprintf(digits + index * 2, 3, \"%02x\", hash[index]);",
                      "digits[0] = 0;"), "text"),
}


def generated(fault=None):
    source = read("port/linux/src/map_torrents.c").replace("\r\n", "\n")
    header = read("port/linux/src/map_torrents.h").replace("\r\n", "\n")
    entry = re.search(r"^struct index_entry\n\{\n.*?\n\};\n", source, re.S | re.M).group(0)
    index = re.search(r"^static struct\n\{\n\tint loaded;.*?\n\} torrent_maps_index;\n", source, re.S | re.M).group(0)
    defines = "\n".join(re.findall(r"^#define (?:NAME_SIZE|MAP_FILE_EXTENSION) .*$", source, re.M) +
                        re.findall(r"^#define MAP_APPROVAL_CONTACT .*$", header, re.M))
    parts = [defines, entry, index, "static void index_load(void);"]
    for name in ("same_name", "column_hash", "approval_file_name", "map_torrents_approved_hash",
                 "map_torrents_approved", "map_torrents_unapproved_text"):
        code = function(source, name)
        if fault and fault[0] == name:
            code = mutated(code, fault[1], fault[2])
        parts.append(code)
    return (("under_test.inc", "\n".join(parts) + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("map_approval", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control][0], NEGATIVE_CONTROLS[control][1]
    status, output = run(build("map_approval", generated(fault)), case)
    assert status != 0, f"{control}: {case} did not catch it"
