"""An MCC map's sound or bitmap stored under another tag's name is found at its position in the map's own
resource file, as MCC finds it; never to choose a shared file."""
import pytest

from tools.harness import build, constant, function, mutated, read, run, structure

SOURCE = read("port/linux/game/mcc_resources.c")


def code(source=SOURCE):
    limits = "\n".join(f"#define {name} {value}" for name, value in (
        ("MCC_RESOURCE_METADATA_LIMIT", constant(source.replace("0x1000000u", "0x1000000"),
                                                 "MCC_RESOURCE_METADATA_LIMIT")),
        ("MCC_PACKAGE_BASE_SETS", constant(read("port/linux/game/mcc_package.h"), "MCC_PACKAGE_BASE_SETS"))))
    under_test = "\n".join([structure(source, "mcc_resource_item"), structure(source, "mcc_resource_file"),
                            structure(source, "mcc_resources"), structure(source, "mcc_resource_probe")] +
                           [function(source, name) for name in (
                               # (port, this fork: its items sorted, and MCC's shared sets read for a
                               # Workshop item's map, mcc_resource_read_from)
                               "mcc_resource_compare", "mcc_resource_close", "mcc_resource_open",
                               "mcc_resource_read_from", "mcc_resource_read",
                               "mcc_resource_check", "mcc_resource_equal", "mcc_resources_select_paths",
                               "mcc_resources_dispose")])
    # (its header and table read by ChupathingyCE's one reader of resource maps, header only)
    under_test = '#include "content_resource_map.h"\n' + under_test
    return build("mcc_resource_position", (("limits.inc", limits), ("under_test.inc", under_test),
                                           ("content_resource_map.h", read("port/linux/game/content_resource_map.h")),
                                           ("content_bytes.h", read("port/linux/game/content_bytes.h"))))


@pytest.mark.parametrize("case", ["lookups", "decoding", "shared"])
def test_resource_found_by_position(case):
    status, output = run(code(), case)
    assert status == 0, output


def test_negative_control_shared_file_by_position():
    """A shared file that finds a member by its position is caught."""
    status, _ = run(code(mutated(SOURCE, "if (shared || !file->own) return 0;", "if (shared) return 0;")), "shared")
    assert status != 0


def test_negative_control_decoder_read_whole():
    """A decoder's read held to the member's whole size is caught."""
    status, _ = run(code(mutated(SOURCE, "file->items[i].size>=bytes", "file->items[i].size==bytes")), "decoding")
    assert status != 0
