"""The real choice of the files a first start downloads (port/linux/src/sdl_platform.c, data_download_wanted):
Multiplayer only leaves out the campaign's ten maps and Halo MCC's shared sets; the campaign's later download takes
those ten alone; MCC's shared files, when an MCC map needs them, take MCC's two sets alone; Everything takes every
file."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["multiplayer", "campaign", "everything", "mcc", "shared"]

# a fault in sdl_platform.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "campaign-kept": (("(folder == DATA_FOLDER_MAPS && !data_is_campaign_map(name))", "TRUE"),
                      "data_download_wanted", "multiplayer"),
    "shared-in-campaign": (("return folder == DATA_FOLDER_MAPS && data_is_campaign_map(name);",
                            "return folder != DATA_FOLDER_MAPS || data_is_campaign_map(name);"),
                           "data_download_wanted", "campaign"),
    "mcc-in-multiplayer": (("folder == DATA_FOLDER_CUSTOM_EDITION ||", "folder != DATA_FOLDER_MAPS ||"),
                           "data_download_wanted", "multiplayer"),
    "mcc-takes-all": (("return folder == DATA_FOLDER_MCC_SINGLEPLAYER || folder == DATA_FOLDER_MCC_MULTIPLAYER;",
                       "return TRUE;"), "data_download_wanted", "mcc"),
    "mcc-sounds-anywhere": (("(folder == DATA_FOLDER_MCC_MULTIPLAYER && !SDL_strcasecmp(name, \"sounds.map\"))",
                             "!SDL_strcasecmp(name, \"sounds.map\")"), "data_is_mcc_shared", "mcc"),
    "level-prefix": (("(!name[3] || !SDL_strcasecmp(name + 3, \".map\"))", "1"), "data_is_campaign_map", "multiplayer"),
    "level-missing": (("\"d20\", ", ""), "data_is_campaign_map", "campaign"),
    "shared-takes-maps": (("return folder != DATA_FOLDER_MAPS;", "return TRUE;"), "data_download_wanted", "shared"),
    "pc-in-multiplayer": (("return folder == DATA_FOLDER_CUSTOM_EDITION || (folder",
                           "return folder == DATA_FOLDER_HALO_PC || folder == DATA_FOLDER_CUSTOM_EDITION || (folder"),
                          "data_download_wanted", "shared"),
    "pc-names": (("return !SDL_strcasecmp(name, \"bitmaps.map\") || !SDL_strcasecmp(name, \"sounds.map\");",
                  "return 1;"), "data_is_halo_pc_shared", "shared"),
}


def generated(fault=None, faulty_function=None):
    source = read("port/linux/src/sdl_platform.c").replace("\r\n", "\n")
    text = ""
    for name in ("data_is_campaign_map", "data_is_mcc_shared", "data_is_halo_pc_shared", "data_download_wanted"):
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    enums = enum_with(source, "DATA_MAPS_MULTIPLAYER") + "\n" + enum_with(source, "DATA_FOLDER_MAPS") + "\n"
    return (("wanted.inc", enums), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("data_download_wanted", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("data_download_wanted", generated(fault, faulty_function)), case)
    assert status != 0, f"{control}: {case} did not catch it"
