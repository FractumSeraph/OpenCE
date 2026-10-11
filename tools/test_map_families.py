"""Checks the names the map families give a map in the game's protocol
(port/linux/game/map_families.c, halo_map_families.h): a Halo PC map's
<file>@ce as OpenCE's build-145 names a Custom Edition map, custom_maps\\<file>,
HaloMD's and Halo PC retail's as names no OpenCE client has, and each read
back as this port names it; and where a Halo PC map's file is looked for,
with paths.custom_edition's folder (h:\\) last and none with
game.custom_edition off. The family code is compiled alone with the build
machine's C compiler, with stand-ins for the game's headers."""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent

STUB_CSERIES = """
#include <string.h>
#include <strings.h>
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0
#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))
"""
STUB_WINDOWS = """
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
"""
HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "cseries.h" /* (first, as the game's sources include it: the header uses its types) */
#include "halo_map_families.h"
/* (the settings, port_config.c: game.downloaded_maps names none) */
const char *config_string(const char *name)
{
	return "";
}
int main(int argc, char **argv)
{
	char out[256];
	int index;
	for (index = 2; index < argc; index++)
	{
		short family;
		if (!strcmp(argv[1], "wire"))
		{
			map_family_wire_name(argv[index], out, sizeof(out));
			printf("%s\n", out);
		}
		else
		{
			family = map_family_from_wire_name(argv[index], out, sizeof(out));
			printf("%d %s\n", family, out);
		}
	}
	return 0;
}
"""


@pytest.fixture(scope="module")
def families(tmp_path_factory):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    work = tmp_path_factory.mktemp("families")
    (work / "cseries").mkdir()
    (work / "cseries.h").write_text(STUB_CSERIES)
    (work / "cseries" / "cseries_windows.h").write_text(STUB_WINDOWS)
    (work / "harness.c").write_text(HARNESS)
    # (the header alone: port/linux/include's stdio.h is the game's)
    shutil.copy(ROOT / "port/linux/include/halo_map_families.h", work / "halo_map_families.h")
    binary = work / "families"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Werror", "-I", str(work),
                    str(work / "harness.c"), str(ROOT / "port/linux/game/map_families.c"), "-o", str(binary)],
                   check=True)

    def run(mode, *names):
        output = subprocess.run([str(binary), mode, *names], check=True, capture_output=True, text=True).stdout
        return output.splitlines()
    return run


def test_wire_names(families):
    assert families("wire", "levels\\test\\bloodgulch\\bloodgulch", "infinity@ce", "phoenix3_15@md",
                    "bloodgulch@pc") == [
        "levels\\test\\bloodgulch\\bloodgulch", "custom_maps\\infinity", "maps_md\\phoenix3_15.md",
        "maps_pc\\bloodgulch.pc"]


def test_wire_names_read_back(families):
    assert families("read", "custom_maps\\infinity", "CUSTOM_MAPS\\Infinity", "maps_md\\phoenix3_15.md",
                    "maps_pc\\bloodgulch.pc", "levels\\test\\bloodgulch\\bloodgulch", "infinity@ce",
                    "phoenix3_15@md") == [
        "1 infinity@ce", "1 Infinity@ce", "2 phoenix3_15@md", "3 bloodgulch@pc",
        "0 levels\\test\\bloodgulch\\bloodgulch", "1 infinity@ce", "2 phoenix3_15@md"]


STUB_WINDOWS_FILES = STUB_WINDOWS + r"""
#include <stdio.h>
typedef void *HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define GENERIC_READ 0
#define OPEN_EXISTING 0
#define FILE_ATTRIBUTE_NORMAL 0
typedef struct { char cFileName[260]; } WIN32_FIND_DATAA;
/* (each path looked at, printed: none of them is there) */
static HANDLE CreateFileA(char const *path, int access, int share, void *security, int disposition, int flags,
	void *template_file)
{
	printf("open %s\n", path);
	return INVALID_HANDLE_VALUE;
}
static int ReadFile(HANDLE file, void *buffer, unsigned long size, unsigned long *read, void *overlapped)
{
	return 0;
}
static int CloseHandle(HANDLE handle)
{
	return 1;
}
static HANDLE FindFirstFileA(char const *pattern, WIN32_FIND_DATAA *data)
{
	printf("list %s\n", pattern);
	return INVALID_HANDLE_VALUE;
}
static int FindNextFileA(HANDLE find, WIN32_FIND_DATAA *data)
{
	return 0;
}
"""
FILES_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cseries.h"
#include "halo_map_families.h"
/* (the settings: game.custom_edition from CUSTOM_EDITION, paths.custom_edition
from CUSTOM_EDITION_ROOT, the folder h:\ is) */
const char *config_string(const char *name)
{
	return "";
}
int config_boolean(const char *name)
{
	return !strcmp(name, "game.custom_edition") && getenv("CUSTOM_EDITION") && !strcmp(getenv("CUSTOM_EDITION"), "1");
}
const char *platform_custom_edition_root(void)
{
	return getenv("CUSTOM_EDITION_ROOT") ? getenv("CUSTOM_EDITION_ROOT") : "";
}
char const *cache_files_map_directory(void)
{
	return "d:\\maps\\";
}
static void found(char const *file, void *context)
{
	printf("found %s\n", file);
}
int main(int argc, char **argv)
{
	char path[256];
	if (!strcmp(argv[1], "find"))
		printf("result %d\n", map_family_find(map_family_parse(argv[2], NULL, 0), argv[3], path, sizeof(path)));
	else if (!strcmp(argv[1], "resource"))
		printf("result %d %s\n", map_family_resource(argv[2], path, sizeof(path)), path);
	else
		map_family_list(map_family_parse(argv[2], NULL, 0), found, NULL);
	return 0;
}
"""


@pytest.fixture(scope="module")
def family_files(tmp_path_factory):
    """map_families.c as the builds that play Halo PC maps compile it (HALO_CUSTOM_EDITION), with every file it
    looks at printed and none there"""
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    work = tmp_path_factory.mktemp("family_files")
    (work / "cseries").mkdir()
    (work / "cseries.h").write_text(STUB_CSERIES)
    (work / "cseries" / "cseries_windows.h").write_text(STUB_WINDOWS_FILES)
    (work / "harness.c").write_text(FILES_HARNESS)
    shutil.copy(ROOT / "port/linux/include/halo_map_families.h", work / "halo_map_families.h")
    binary = work / "family_files"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-multichar",
                    "-DHALO_CUSTOM_EDITION", "-I", str(work), str(work / "harness.c"),
                    str(ROOT / "port/linux/game/map_families.c"), "-o", str(binary)], check=True)

    def run(*arguments, enabled=True, root=""):
        environment = dict(os.environ, CUSTOM_EDITION="1" if enabled else "0", CUSTOM_EDITION_ROOT=root)
        output = subprocess.run([str(binary), *arguments], check=True, capture_output=True, text=True,
                                env=environment).stdout
        return output.splitlines()
    return run


def test_custom_edition_folder_looked_in_last(family_files):
    # paths.custom_edition (h:\) after ChupathingyCE's and OpenCE's folders, for maps and resource maps alike
    lines = family_files("find", "x@ce", "infinity", root="/games/halo ce/maps")
    assert lines[:2] == ["open d:\\maps_ce\\infinity.map", "open d:\\maps_ce\\infinity@ce.map"]
    assert "open d:\\custom_maps\\infinity.map" in lines
    assert lines[-3:] == ["open h:\\infinity.map", "open h:\\infinity@ce.map", "result 0"]
    assert family_files("resource", "bitmaps", root="/games/halo ce/maps") == [
        "open d:\\maps_ce\\bitmaps.map", "open d:\\maps\\ce\\bitmaps.map", "open d:\\custom_maps\\bitmaps.map",
        "open h:\\bitmaps.map", "result 0 d:\\maps_ce\\bitmaps.map"]
    assert family_files("list", "x@ce", root="/games/halo ce/maps")[-1] == "list h:\\*.map"


def test_custom_edition_folder_unset(family_files):
    # (no paths.custom_edition: h:\ is never looked in, and HaloMD's maps never were)
    assert not [line for line in family_files("find", "x@ce", "infinity") if "h:\\" in line]
    assert not [line for line in family_files("find", "x@md", "infinity", root="/games/halo ce/maps")
                if "h:\\" in line]


def test_custom_edition_off(family_files):
    # game.custom_edition false: no Halo PC map is looked for or listed
    for family in ("x@ce", "x@md", "x@pc"):
        assert family_files("find", family, "infinity", enabled=False, root="/games/halo ce/maps") == ["result 0"]
        assert family_files("list", family, enabled=False) == []


def test_wire_names_not_files(families):
    # (a folder, a family's suffix or nothing in the file's place is no map
    # of a family: the name stays as it came)
    assert families("read", "custom_maps\\", "custom_maps\\a\\b", "custom_maps\\x@md", "maps_md\\name",
                    "maps_pc\\.pc") == [
        "0 custom_maps\\", "0 custom_maps\\a\\b", "2 custom_maps\\x@md", "0 maps_md\\name", "0 maps_pc\\.pc"]
