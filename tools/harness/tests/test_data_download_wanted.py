"""The real choice of the files a first start downloads (port/linux/src/sdl_platform.c, data_download_wanted):
Multiplayer only leaves out the campaign's ten maps and nothing else; the campaign's later download takes those ten
alone; Everything takes every file."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["multiplayer", "campaign", "everything"]

# a fault in sdl_platform.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "campaign-kept": (("return custom_edition || !data_is_campaign_map(name);", "return TRUE;"),
                      "data_download_wanted", "multiplayer"),
    "shared-in-campaign": (("return !custom_edition && data_is_campaign_map(name);",
                            "return custom_edition || data_is_campaign_map(name);"),
                           "data_download_wanted", "campaign"),
    "level-prefix": (("(!name[3] || !SDL_strcasecmp(name + 3, \".map\"))", "1"), "data_is_campaign_map", "multiplayer"),
    "level-missing": (("\"d20\", ", ""), "data_is_campaign_map", "campaign"),
}


def generated(fault=None, faulty_function=None):
    source = read("port/linux/src/sdl_platform.c").replace("\r\n", "\n")
    text = ""
    for name in ("data_is_campaign_map", "data_download_wanted"):
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("wanted.inc", enum_with(source, "DATA_MAPS_MULTIPLAYER") + "\n"), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("data_download_wanted", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("data_download_wanted", generated(fault, faulty_function)), case)
    assert status != 0, f"{control}: {case} did not catch it"
