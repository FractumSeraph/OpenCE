"""The real download of the site's files into a data root (port/linux/src/sdl_platform.c, data_download), the site a
folder and the download a copy: Halo MCC's shared sets go into maps_mcc/_shared/<set> (mcc_maps/_shared where only
that folder is), a set's file the player has is kept, the multiplayer set is copied from maps_ce when maps.txt's
SHA-256 says it is the same file, and a download that is not the file its line says is refused."""

import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, constant, enum_with, function, mutated, read, structure  # noqa: E402

CASES = ["mcc", "fallback-folder", "kept", "corrupt", "everything", "multiplayer"]

FUNCTIONS = ("data_is_campaign_map", "data_is_custom_edition_shared", "data_is_mcc_shared", "data_download_wanted",
             "data_download_received", "data_download_name_valid", "data_file_size", "data_parse_sha256",
             "data_file_matches", "data_copy_file", "data_mcc_folder", "data_download")

# a fault in sdl_platform.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "no-copy": (("if (kind == DATA_FOLDER_MCC_MULTIPLAYER && files[which].hashed)", "if (0)"),
                "data_download", "mcc"),
    "hash-ignored": (("(files[which].hashed && !data_file_matches(partial, files[which].size, files[which].sha256))",
                      "0"), "data_download", "corrupt"),
    "own-copy-replaced": (("if (!site_url[0] || data_file_size(path) >= 0)", "if (!site_url[0])"),
                          "data_download", "kept"),
    "no-fallback-folder": (("\"%s\" DATA_PATH_SEPARATOR \"mcc_maps\", root);", "\"%s\" DATA_PATH_SEPARATOR \"maps_mcc\", root);"),
                           "data_mcc_folder", "fallback-folder"),
    "half-digits": (("digest[index / 2] |= (unsigned char)value;", "digest[index / 2] = (unsigned char)value;"),
                    "data_parse_sha256", "mcc"),
    "any-set-name": (("if (!data_is_mcc_shared(kind, name))\n\t\t\t\tcontinue;", ""), "data_download", "mcc"),
}


def generated(fault=None, faulty_function=None):
    source = read("port/linux/src/sdl_platform.c").replace("\r\n", "\n")
    types = (enum_with(source, "DATA_MAPS_MULTIPLAYER") + "\n" + enum_with(source, "DATA_FOLDER_MAPS") + "\n" +
             f"#define DATA_DOWNLOAD_FILES {constant(source, 'DATA_DOWNLOAD_FILES')}\n" +
             structure(source, "data_extraction") + "\n" + structure(source, "data_download_file") + "\n" +
             structure(source, "data_download_progress") + "\n")
    text = ""
    for name in FUNCTIONS:
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("types.inc", types), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case, tmp_path):
    executable = build("data_download_files", generated())
    status, output = run_in(executable, case, tmp_path)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control, tmp_path):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run_in(build("data_download_files", generated(fault, faulty_function)), case, tmp_path)
    assert status != 0, f"{control}: {case} did not catch it"


def run_in(executable, case, folder):
    """run(), with the case's own folder for its site and data root as its second argument"""
    result = subprocess.run([str(executable), case, str(folder / "world")], capture_output=True, text=True,
                            errors="replace", timeout=60)
    return result.returncode, (result.stdout + result.stderr).strip()
