"""The real reading of an index line's archive columns (port/linux/src/map_archives.c, archive_source): the
BLAKE2b-256's column after the info hash (tools/map_torrents.py) is no archive's, and the archives' zips after it
are found."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, function, mutated, read, run  # noqa: E402

CASES = ["after-the-hash", "hash-alone"]

# a fault in archive_source, and the case that must catch it
NEGATIVE_CONTROLS = {
    "first-column-only": (("source = tab ? tab + 1 : NULL;", "source = NULL;"), "after-the-hash"),
    "any-key": (("same_name(source, archives[archive].key, key_length)", "1"), "hash-alone"),
}


def generated(fault=None):
    source = read("port/linux/src/map_archives.c").replace("\r\n", "\n")
    table = re.search(r"static const struct\n\{\n\tconst char \*key;.*?\n\};\n", source, re.S).group(0)
    defines = "\n".join(re.findall(r"^#define (?:WEB_SEED_ARCHIVE|ARCHIVE_COUNT) .*$", source, re.M))
    code = function(source, "archive_source")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", "\n".join([table, defines, function(source, "same_name"), code]) + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("map_archive_sources", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("map_archive_sources", generated(fault)), case)
    assert status != 0, f"{control}: {case} did not catch it"
