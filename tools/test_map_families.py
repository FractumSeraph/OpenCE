"""Checks the names the map families give a map in the game's protocol
(port/linux/game/map_families.c, halo_map_families.h): a Halo PC map's
<file>@ce as OpenCE's build-145 names a Custom Edition map, custom_maps\\<file>,
HaloMD's, Halo PC retail's and a map pack's (<file>@<pack>,
maps_<pack>\\<file>.<pack>) as names no OpenCE client has, and each read
back as this port names it; the file an Xbox-format map is read from; and
where a Halo PC map's file is looked for, with paths.custom_edition's folder
(h:\\) last and none with game.custom_edition off; and the campaign maps (a
solo scenario's) listed apart from the multiplayer ones, from folders of the
test's own. The family code is compiled alone with the build machine's C
compiler, with stand-ins for the game's headers."""
import os
import json
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
# (the file calls' stand-ins are each test's own: STUB_WINDOWS_FILES and
# STUB_WINDOWS_REAL below, which take the place of ChupathingyCE's macros)
STUB_WINDOWS = """
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
char const *config_string(char const *name);
"""
HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cseries.h" /* (first, as the game's sources include it: the header uses its types) */
#include "halo_map_families.h"
/* (the settings, port_config.c: game.downloaded_maps names none) */
char const *config_string(char const *name) { (void)name; return ""; }
char const *cache_files_map_directory(void) { return "d:\\maps\\"; }
static char const *level_at(long index, void *context)
{
	return ((char const *const *)context)[index];
}
int main(int argc, char **argv)
{
	char out[256];
	int index;
	if (!strcmp(argv[1], "unknown"))
	{
		/* (the map, version, has_platform, ce_present, mcc_found) */
		printf("%d\n", map_family_opence_unknown(argv[2], atol(argv[3]), (boolean)atoi(argv[4]),
			(boolean)atoi(argv[5]), (boolean)atoi(argv[6])));
		return 0;
	}
	if (!strcmp(argv[1], "missing"))
	{
		/* (stock, downloadable, opence_unknown) */
		printf("%d\n", map_family_missing_kind((boolean)atoi(argv[2]), (boolean)atoi(argv[3]),
			(boolean)atoi(argv[4])));
		return 0;
	}
	if (!strcmp(argv[1], "gives_up"))
	{
		/* (asked_host, advertised family, present, stock, downloadable) */
		printf("%d\n", map_family_asked_host_gives_up((boolean)atoi(argv[2]), (short)atoi(argv[3]),
			(boolean)atoi(argv[4]), (boolean)atoi(argv[5]), (boolean)atoi(argv[6])));
		return 0;
	}
	if (!strcmp(argv[1], "mcc_listed"))
	{
		/* (version, Custom Edition map here 0/1, the listed map, then this
		machine's MCC levels) */
		if (map_family_listed_mcc(argv[4], atol(argv[2]), (boolean)atoi(argv[3]), level_at, argc - 5, argv + 5,
			out, sizeof(out)))
			printf("%s\n", out);
		else
			printf("-\n");
		return 0;
	}
	for (index = 2; index < argc; index++)
	{
		short family;
		if (!strcmp(argv[1], "listed"))
		{
			family = map_family_parse_listed(argv[index], out, sizeof(out));
			printf("%d %s\n", family, out);
		}
		else if (!strcmp(argv[1], "path"))
		{
			char pack[16];
			int ok = map_family_xbox_path(argv[index], out, sizeof(out));
			family = map_family_parse(argv[index], NULL, 0);
			map_family_pack_id(argv[index], pack, sizeof(pack));
			printf("%d %d %s %s\n", family, ok, out, pack[0] ? pack : "-");
		}
		else if (!strcmp(argv[1], "mcc"))
		{
			/* (argv[index]: the header's version, in decimal) */
			unsigned long header[2] = { 'head', strtoul(argv[index], NULL, 10) };
			printf("%d\n", map_family_header_is_mcc(header));
		}
		else if (!strcmp(argv[1], "wire"))
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
    (work / "cseries" / "cseries_windows.h").write_text(STUB_WINDOWS_FILES)
    (work / "harness.c").write_text(HARNESS + LINK_STUBS)
    # (the header alone: port/linux/include's stdio.h is the game's)
    shutil.copy(ROOT / "port/linux/include/halo_map_families.h", work / "halo_map_families.h")
    shutil.copy(ROOT / "port/linux/include/halo_join_reasons.h", work / "halo_join_reasons.h")
    binary = work / "families"
    # (the file stubs this build leaves unused: its Custom Edition folders' lists)
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Werror", "-Wno-unused-function", "-I", str(work),
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
#define FILE_SHARE_READ 0
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
#define GENERIC_WRITE 0
#define OPEN_ALWAYS 0
#define FILE_END 2
/* (ChupathingyCE's maps_ce/downloaded_maps.txt, written by
map_downloaded_remember: not here) */
static unsigned long SetFilePointer(HANDLE file, long distance, long *high, int method)
{
	return 0;
}
static int WriteFile(HANDLE file, void const *buffer, unsigned long size, unsigned long *written, void *overlapped)
{
	return 0;
}
/* (ChupathingyCE's resource map checks, map_family_resources_ready: no file
is here to measure) */
#define FILE_BEGIN 0
#define INVALID_FILE_SIZE 0xffffffffUL
#define INVALID_SET_FILE_POINTER 0xffffffffUL
static unsigned long GetFileSize(HANDLE file, unsigned long *high)
{
	return INVALID_FILE_SIZE;
}
"""
LINK_STUBS = r"""
/* (ChupathingyCE's hashed names and versions, port/linux/src/map_archive.c
and map_file_hash.c, which map_families.c calls: no hashes here, the
fork's own hash-named versions are what is tested) */
int map_archive_hashed_name(const char *name, char *base, int base_size, unsigned int *prefix)
{
	return 0;
}
int map_file_hash_prefix(const char *path, unsigned int *prefix)
{
	return 0;
}
int map_file_hash_version(const char *path, unsigned int *version)
{
	return 0;
}
unsigned int map_archive_header_checksum(const unsigned char *header)
{
	return (unsigned int)header[0x64] | (unsigned int)header[0x65] << 8 | (unsigned int)header[0x66] << 16 |
		(unsigned int)header[0x67] << 24;
}
/* (map_archive.c's: a version is the checksum or the hash's, 0 any) */
int map_archive_version_matches(unsigned int version, unsigned int checksum, unsigned int hash_version)
{
	return !version || version == checksum || (hash_version && version == hash_version);
}
/* (map_archive.c's Halo PC maps and data files: none here, so that no
Custom Edition name falls back to a Halo PC file) */
int map_archive_stock_name(const char *name)
{
	return 0;
}
/* (map_archive.c's: a hashed name's base, any other name as it is) */
void map_archive_display_name(const char *name, char *display, int size)
{
	if (size > 0 && !map_archive_hashed_name(name, display, size, NULL))
		snprintf(display, (size_t)size, "%s", name ? name : "");
}
void platform_log(char const *format, ...)
{
}
/* (port/linux/src/xbox_memory.c: the tag cache mapped, as a machine that
plays them has it: map_families_playable) */
int platform_ce_tag_cache_ready = 1;
char platform_ce_tag_cache_error[160];
/* (xbox_files.c's: a path as the host names it, for ChupathingyCE's resource
map checks' log, map_family_resources_ready: as it is here) */
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size)
{
	snprintf(host_path, (size_t)host_path_size, "%s", xbox_path);
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
    (work / "harness.c").write_text(FILES_HARNESS + LINK_STUBS)
    shutil.copy(ROOT / "port/linux/include/halo_map_families.h", work / "halo_map_families.h")
    shutil.copy(ROOT / "port/linux/include/halo_join_reasons.h", work / "halo_join_reasons.h")
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



STUB_WINDOWS_REAL = STUB_WINDOWS + r"""
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* (the Xbox's drives a folder of the test's each: d:\maps_ce\a.map is
$DRIVES/d/maps_ce/a.map) */
typedef struct { FILE *file; DIR *folder; } *HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)0)
#define GENERIC_READ 0
#define FILE_SHARE_READ 0
#define OPEN_EXISTING 0
#define FILE_ATTRIBUTE_NORMAL 0
typedef struct { char cFileName[260]; } WIN32_FIND_DATAA;
static void host_path(char const *path, char *host, size_t size)
{
	char *cursor;

	snprintf(host, size, "%s/%c/%s", getenv("DRIVES"), path[0], path[0] && path[1] == ':' ? path + 3 : path);
	for (cursor = host; *cursor; cursor++)
		if (*cursor == '\\')
			*cursor = '/';
}
static HANDLE CreateFileA(char const *path, int access, int share, void *security, int disposition, int flags,
	void *template_file)
{
	char host[512];
	FILE *file;
	HANDLE handle;

	host_path(path, host, sizeof(host));
	if (!(file = fopen(host, "rb")))
		return INVALID_HANDLE_VALUE;
	handle = calloc(1, sizeof(*handle));
	handle->file = file;
	return handle;
}
static int ReadFile(HANDLE handle, void *buffer, unsigned long size, unsigned long *read, void *overlapped)
{
	*read = (unsigned long)fread(buffer, 1, size, handle->file);
	return 1;
}
static int CloseHandle(HANDLE handle)
{
	if (handle->file)
		fclose(handle->file);
	if (handle->folder)
		closedir(handle->folder);
	free(handle);
	return 1;
}
static int FindNextFileA(HANDLE handle, WIN32_FIND_DATAA *data)
{
	struct dirent *entry;

	while ((entry = readdir(handle->folder)) != NULL)
	{
		if (entry->d_name[0] == '.')
			continue;
		snprintf(data->cFileName, sizeof(data->cFileName), "%s", entry->d_name);
		return 1;
	}
	return 0;
}
/* (every file of the folder: the families' code picks the .map files) */
static HANDLE FindFirstFileA(char const *pattern, WIN32_FIND_DATAA *data)
{
	char host[512];
	char *star;
	DIR *folder;
	HANDLE handle;

	host_path(pattern, host, sizeof(host));
	if ((star = strrchr(host, '/')) != NULL)
		*star = 0;
	if (!(folder = opendir(host)))
		return INVALID_HANDLE_VALUE;
	handle = calloc(1, sizeof(*handle));
	handle->folder = folder;
	if (!FindNextFileA(handle, data))
	{
		CloseHandle(handle);
		return INVALID_HANDLE_VALUE;
	}
	return handle;
}
#define GENERIC_WRITE 0
#define OPEN_ALWAYS 0
#define FILE_END 2
/* (ChupathingyCE's maps_ce/downloaded_maps.txt, written by
map_downloaded_remember: not here) */
static unsigned long SetFilePointer(HANDLE file, long distance, long *high, int method)
{
	return 0;
}
static int WriteFile(HANDLE file, void const *buffer, unsigned long size, unsigned long *written, void *overlapped)
{
	return 0;
}
/* (ChupathingyCE's resource map checks, map_family_resources_ready: a
file's size, as it is in the test's folder) */
#define FILE_BEGIN 0
#define INVALID_FILE_SIZE 0xffffffffUL
#define INVALID_SET_FILE_POINTER 0xffffffffUL
static unsigned long GetFileSize(HANDLE handle, unsigned long *high)
{
	long at, size;

	if (!handle->file || (at = ftell(handle->file)) < 0 || fseek(handle->file, 0, SEEK_END))
		return INVALID_FILE_SIZE;
	size = ftell(handle->file);
	fseek(handle->file, at, SEEK_SET);
	if (high)
		*high = 0;
	return size < 0 ? INVALID_FILE_SIZE : (unsigned long)size;
}
"""
REAL_HARNESS = FILES_HARNESS.replace("""	else if (!strcmp(argv[1], "resource"))""", """	else if (!strcmp(argv[1], "campaigns"))
		map_family_list_campaigns(map_family_parse(argv[2], NULL, 0), found, NULL);
	else if (!strcmp(argv[1], "campaign"))
		printf("result %d\\n", map_family_campaign(argv[2]));
	else if (!strcmp(argv[1], "resource"))""")


@pytest.fixture(scope="module")
def family_folders(tmp_path_factory):
    """map_families.c as the builds that play Halo PC maps compile it, on folders of the test's own: the Xbox's
    drives d:\\ and h:\\ under DRIVES"""
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc or os.name == "nt":
        pytest.skip("no C compiler with dirent.h")
    work = tmp_path_factory.mktemp("family_folders")
    (work / "cseries").mkdir()
    (work / "cseries.h").write_text(STUB_CSERIES)
    (work / "cseries" / "cseries_windows.h").write_text(STUB_WINDOWS_REAL)
    (work / "harness.c").write_text(REAL_HARNESS + LINK_STUBS)
    shutil.copy(ROOT / "port/linux/include/halo_map_families.h", work / "halo_map_families.h")
    shutil.copy(ROOT / "port/linux/include/halo_join_reasons.h", work / "halo_join_reasons.h")
    binary = work / "family_folders"
    # (32-bit, as the game is: a cache file's header is read as longs of 4 bytes, as the 64-bit builds have them
    # too, tools/lp64_build.py)
    (work / "empty.c").write_text("#include <stdio.h>\nint main(void)\n{\n\treturn 0;\n}\n")
    if subprocess.run([cc, "-m32", str(work / "empty.c"), "-o", str(work / "empty")], capture_output=True).returncode:
        pytest.skip("no 32-bit C compiler and library")
    subprocess.run([cc, "-m32", "-std=gnu99", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-multichar",
                    "-Wno-unused-parameter", "-DHALO_CUSTOM_EDITION", "-I", str(work), str(work / "harness.c"),
                    str(ROOT / "port/linux/game/map_families.c"), "-o", str(binary)], check=True)
    drives = work / "drives"

    def put(folder, name, version, scenario_type):
        """a cache file's header: 'head', its version, ..., its scenario's type at 0x60"""
        path = drives / "d" / folder / name
        path.parent.mkdir(parents=True, exist_ok=True)
        header = bytearray(0x800)
        header[0:4] = (0x68656164).to_bytes(4, "little")
        header[4:8] = version.to_bytes(4, "little")
        header[0x60:0x62] = scenario_type.to_bytes(2, "little")
        path.write_bytes(bytes(header))

    put("maps_ce", "a50_custom.map", 609, 0)
    put("maps_ce", "bigass_v3.map", 609, 1)
    put("maps_ce", "Zanzibar_Campaign.map", 609, 0)
    put("maps_ce", "ui.map", 609, 2)
    put("maps_md", "md_campaign.map", 7, 0)
    put("maps_md", "md_multiplayer.map", 7, 1)
    # (Halo PC retail's version in the Custom Edition folder: not a Custom Edition map)
    put("maps_ce", "retail_campaign.map", 7, 0)

    def run(*arguments, enabled=True):
        environment = dict(os.environ, CUSTOM_EDITION="1" if enabled else "0", CUSTOM_EDITION_ROOT="",
                           DRIVES=str(drives))
        output = subprocess.run([str(binary), *arguments], check=True, capture_output=True, text=True,
                                env=environment).stdout
        return output.splitlines()
    return run


def test_campaign_maps_listed_apart(family_folders):
    # a solo scenario's maps are the campaign maps (CUSTOM SINGLEPLAYER), apart from the multiplayer ones; a user
    # interface's are neither, and a map of another family's version is not the family's
    assert sorted(family_folders("campaigns", "x@ce")) == ["found Zanzibar_Campaign", "found a50_custom"]
    assert family_folders("list", "x@ce") == ["found bigass_v3"]
    # (HaloMD's maps were first played from maps_ce too: halomd_places)
    assert sorted(family_folders("campaigns", "x@md")) == ["found md_campaign", "found retail_campaign"]
    assert family_folders("list", "x@md") == ["found md_multiplayer"]
    assert family_folders("campaigns", "x@ce", enabled=False) == []


def test_campaign_map_by_name(family_folders):
    # (the co-op host's check: ui_widget_port_cooperative_level_choose)
    assert family_folders("campaign", "a50_custom@ce") == ["result 1"]
    assert family_folders("campaign", "bigass_v3@ce") == ["result 0"]
    assert family_folders("campaign", "md_campaign@md") == ["result 1"]
    assert family_folders("campaign", "retail_campaign@ce") == ["result 0"]
    assert family_folders("campaign", "missing@ce") == ["result 0"]
    assert family_folders("campaign", "a50") == ["result 0"]
    assert family_folders("campaign", "a50_custom@ce", enabled=False) == ["result 0"]


def test_listed_games_by_bare_name(families):
    # (OpenCE's hosts announce every map by its file name alone: the Xbox's
    # maps stay the Xbox's, any other is a Halo PC map, as custom_maps\<name>
    # is in the game's protocol)
    assert families("listed", "bloodgulch", "deathisland", "Icefields", "hugeass", "a30",
                    "levels\\test\\hangemhigh\\hangemhigh", "deathisland@ce", "x@md", "bloodgulch@pc", "") == [
        "0 bloodgulch", "1 deathisland", "1 Icefields", "1 hugeass", "0 a30",
        "0 hangemhigh", "1 deathisland", "2 x", "3 bloodgulch", "0 "]


def test_pack_names(families):
    # (a map pack's map: <file>@<pack>, its pack 1 to 8 letters and digits,
    # in its own folder; the other families' suffixes and the Xbox's language
    # folders are no pack's)
    assert families("path", "bloodgulch@cex", "Bloodgulch@CEX", "bloodgulch@xyz", "bloodgulch", "ui",
                    "levels\\test\\bloodgulch\\bloodgulch", "x@md", "x@de", "x@toolongname", "@cex", "a@b@cex") == [
        "4 1 d:\\maps_cex\\bloodgulch.map cex", "4 1 d:\\maps_cex\\Bloodgulch.map cex",
        "4 1 d:\\maps_xyz\\bloodgulch.map xyz", "0 1 d:\\maps\\bloodgulch.map -", "0 1 d:\\maps\\ui.map -",
        "0 1 d:\\maps\\levels\\test\\bloodgulch\\bloodgulch.map -", "2 1 d:\\maps\\x@md.map -",
        "0 1 d:\\maps\\x@de.map -", "0 1 d:\\maps\\x@toolongname.map -", "0 1 d:\\maps\\@cex.map -",
        "4 1 d:\\maps_cex\\a@b.map cex"]


def test_pack_wire_names(families):
    # (never the retail name: a build before packs reads maps_cex\\<file>.cex
    # as a map it doesn't have)
    assert families("wire", "bloodgulch@cex", "putput@xyz") == ["maps_cex\\bloodgulch.cex", "maps_xyz\\putput.xyz"]
    assert families("read", "maps_cex\\bloodgulch.cex", "MAPS_CEX\\Wizard.CEX", "maps_xyz\\putput.xyz",
                    "maps_cex\\bloodgulch.xyz", "maps_cex\\bloodgulch", "maps_de\\x.de", "maps_cex\\a\\b.cex",
                    "maps_cex\\.cex") == [
        "4 bloodgulch@cex", "4 Wizard@cex", "4 putput@xyz", "0 maps_cex\\bloodgulch.xyz", "0 maps_cex\\bloodgulch",
        "0 maps_de\\x.de", "0 maps_cex\\a\\b.cex", "0 maps_cex\\.cex"]


def test_pack_listed(families):
    assert families("listed", "bloodgulch@cex", "longest@xyz") == ["4 bloodgulch", "4 longest"]


def test_mcc_header_detected(families):
    # (cache version 13 is MCC's; Custom Edition's 609, Halo PC's 7 and the
    # Xbox's 5 are not)
    assert families("mcc", "13", "609", "7", "5", "0") == ["1", "0", "0", "0", "0"]


LEVELS = ["mcc_maps\\2900197929\\desert", "mcc_maps\\2900306344\\portent", "mcc_maps\\my_pack\\Portent",
          "mcc_maps\\3002260635\\crashsite"]


def test_opence_mcc_listing(families):
    # (OpenCE build-344's listing of a game on mcc_maps\2900306344\portent
    # names its map "portent", at network version 43:
    # tools/tests/opence_mcc/listing-344.json; the first local level with
    # that file name, the Workshop item's here)
    assert families("mcc_listed", "43", "0", "portent", *LEVELS) == ["mcc_maps\\2900306344\\portent"]
    assert families("mcc_listed", "43", "0", "PORTENT", *LEVELS) == ["mcc_maps\\2900306344\\portent"]
    assert families("mcc_listed", "42", "0", "crashsite", *LEVELS) == ["mcc_maps\\3002260635\\crashsite"]


def test_opence_mcc_listing_unchanged(families):
    # (what stays as it was: a host before MCC maps (41), a Custom Edition
    # map this machine has by the name, a map no MCC level here has, the
    # Xbox's maps, our own hosts' full names and other families' names)
    assert families("mcc_listed", "41", "0", "portent", *LEVELS) == ["-"]
    assert families("mcc_listed", "43", "1", "portent", *LEVELS) == ["-"]
    assert families("mcc_listed", "43", "0", "cairo", *LEVELS) == ["-"]
    assert families("mcc_listed", "43", "0", "bloodgulch", "mcc_maps\\1\\bloodgulch") == ["-"]
    assert families("mcc_listed", "43", "0", "mcc_maps\\2900306344\\portent", *LEVELS) == ["-"]
    assert families("mcc_listed", "43", "0", "portent@ce", *LEVELS) == ["-"]
    assert families("mcc_listed", "43", "0", "", *LEVELS) == ["-"]
    # (a level not under mcc_maps, or with no file part, is never taken)
    assert families("mcc_listed", "43", "0", "portent", "maps\\portent", "mcc_maps\\portent\\") == ["-"]


def read_listing(raw):
    # (p2p_lobby.c's listing_read, the fields up to the gametype)
    at = 5
    version = raw[3] << 8 | raw[4]
    at += 1 + 8 + 32 + 16 + 3
    texts = []
    for _ in range(3):
        length = raw[at]
        texts.append(raw[at + 1:at + 1 + length].decode())
        at += 1 + length
    return version, texts


def test_opence_mcc_listing_recorded(families):
    # (what OpenCE build-344 listed, recorded: the MCC game's map by its file
    # alone, at 43, and that read as this machine's level of it)
    fixture = json.loads((ROOT / "tools/tests/opence_mcc/listing-344.json").read_text())
    listing = next(entry for entry in fixture["listings"] if entry["fields"]["engine_type"] == 2)
    version, (name, listed_map, gametype) = read_listing(bytes.fromhex(listing["listing_hex"]))
    assert (version, name, listed_map) == (43, "Host", "portent")
    assert families("mcc_listed", str(version), "0", listed_map, *LEVELS) == [fixture["settings"]["map_name"]]
    # (the same host's lobby on an Xbox map stays the Xbox's)
    lobby = next(entry for entry in fixture["listings"] if entry["fields"]["engine_type"] == 0)
    version, (_, listed_map, _) = read_listing(bytes.fromhex(lobby["listing_hex"]))
    assert families("mcc_listed", str(version), "0", listed_map, *LEVELS) == ["-"]


# (map_family_missing_kind's answers, halo_map_families.h's order)
STOCK, DOWNLOAD, ASK_HOST, BLOCKED = "0", "1", "2", "3"


def test_opence_unknown_names(families):
    # (OpenCE's bare name, 42 on, of a map this machine has neither as an MCC
    # nor as a Custom Edition map: unknown; anything else is not)
    assert families("unknown", "cairo", "43", "0", "0", "0") == ["1"]
    assert families("unknown", "cairo", "42", "0", "0", "0") == ["1"]
    assert families("unknown", "cairo", "41", "0", "0", "0") == ["0"]
    assert families("unknown", "cairo", "43", "1", "0", "0") == ["0"]
    assert families("unknown", "cairo", "43", "0", "1", "0") == ["0"]
    assert families("unknown", "cairo", "43", "0", "0", "1") == ["0"]
    assert families("unknown", "bloodgulch", "43", "0", "0", "0") == ["0"]
    assert families("unknown", "mcc_maps\\1\\cairo", "43", "0", "0", "0") == ["0"]
    assert families("unknown", "cairo@ce", "43", "0", "0", "0") == ["0"]


def test_plain_custom_edition_games_on_opence_43(families):
    # (a Custom Edition game on an OpenCE 42+ host, its map missing here:
    # Halo PC's own map and a map that may be downloaded are said and done
    # as they always were, before the unknown name's "join to find out")
    assert families("missing", "1", "0", "1") == [STOCK]
    assert families("missing", "1", "1", "1") == [STOCK]
    assert families("missing", "0", "1", "1") == [DOWNLOAD]
    # (one that can't be downloaded: only then joined to ask the host)
    assert families("missing", "0", "0", "1") == [ASK_HOST]
    # (any other game, as before: an older host, our own, a map not bare)
    assert families("missing", "0", "0", "0") == [BLOCKED]
    assert families("missing", "0", "1", "0") == [DOWNLOAD]
    assert families("missing", "1", "0", "0") == [STOCK]


def test_asked_host_gives_up_only_that_join(families):
    # (the host names a Custom Edition map (1) that can't be had: the join let
    # through only to ask is given up; a stock or downloadable one is offered)
    assert families("gives_up", "1", "1", "0", "0", "0") == ["1"]
    assert families("gives_up", "1", "1", "0", "1", "0") == ["0"]
    assert families("gives_up", "1", "1", "0", "0", "1") == ["0"]
    assert families("gives_up", "1", "1", "1", "0", "0") == ["0"]
    # (an MCC map, mcc_maps\..., is an Xbox-format name: joined, the content
    # fingerprint decides)
    assert families("gives_up", "1", "0", "0", "0", "0") == ["0"]
    # (every other join, invites and system link included, goes on as it did)
    for family in ("0", "1", "2", "3", "4"):
        for present in ("0", "1"):
            assert families("gives_up", "0", family, present, "0", "0") == ["0"]
