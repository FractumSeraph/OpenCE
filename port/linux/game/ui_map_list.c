/*
UI_MAP_LIST.C

The menus' list of multiplayer maps, filled by the port rather than fixed in
the game (ui_widget_event_handler_functions.c's multiplayer level list): the
Xbox's thirteen maps first, as they were, then the Custom Edition maps
(Halo PC's, played as <name>@ce), each named with [CE], then HaloMD's maps
(Halo PC retail's, played as <name>@md), each named with [MD]: each family
in its folders (halo_map_families.h), in the order of their names.

A row past the Xbox's has no string or bitmap frame of its own in ui.map:
its text comes from here, by a string list index of
UI_MAP_LIST_STRING_BASE and up (ui_widget.c asks ui_map_list_text when a
text box has one), and its picture by a bitmap frame of
UI_MAP_LIST_PICTURE_BASE and up (ui_widget.c asks ui_map_list_picture). Its
name is two lines in the map list's narrow boxes (the name, then [CE] or
[MD]), and one in the lobby's.

Those are Halo PC's own, read from its ui.map in maps_ce as Halo PC shows
them: the names of its map list (ui\shell\main_menu\mp_map_list), their
descriptions (...\mp_map_select\map_data) and their pictures
(ui\shell\bitmaps\mp_map_grafix, its pixels in maps_ce\bitmaps.map), each
the map's by Halo PC's order of its maps (ce_maps); a map of another's
making is named by its file, with Halo PC's picture for an unknown level.
Without Halo PC's ui.map, the names are ce_maps' and an Xbox map's picture
stands in. A HaloMD map is named as HaloMD's mod list names it (by its
file's name: halomd_map_names.h, written by tools/halomd_map_names.py), or
by its file's name made readable, with Halo PC's picture for an unknown
level.

The server browser (browser_screen.c) names a listed game's Custom Edition
or HaloMD map by the same (ui_map_list_family_name), on every build: the
builds without such maps (HALO_CUSTOM_EDITION) still name one. On those with
them it has Halo PC's picture of the map (ui_map_list_family_picture) and
asks whether the map is in its family's folders (ui_map_list_family_present).

The families' campaign maps (a solo scenario's, which the multiplayer list
leaves out) are listed apart, as OpenCE lists them (MrBruh's ce41b41d): the
PC menus' map lists show them as CUSTOM SINGLEPLAYER, played as the
campaign's levels are, alone or hosted as network co-op (menu_functions.c),
and the multiplayer rows past the Xbox's as CUSTOM MULTIPLAYER. A campaign
row's strings have string list indices of UI_MAP_LIST_CAMPAIGN_STRING_BASE
and up, its name the file's made readable; its picture is Halo PC's for an
unknown level.

Beside a map, as OpenCE has them (30cd6367), <name>.bmp is its picture and
<name>.txt its description, in place of Halo PC's or the file's own: the
picture's middle in the menus' picture shape (bmp_files.c, which checks the
file as untrusted input), read when it is first drawn and let go when the
list is filled anew, and the description line by line (the Xbox's are lines
of about 20 characters). A row with a picture of its own has a bitmap frame
of UI_MAP_LIST_ROW_PICTURE_BASE (or UI_MAP_LIST_CAMPAIGN_PICTURE_BASE) and
up. Not in the browser build, whose maps are on the site and each file read
is fetched (its page shows the pictures itself: online_client.js).
*/

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"

#include <stdio.h>
#include <string.h>

#include "halo_map_families.h"
#include "halo_ui_map_list.h"
#include "halomd_map_names.h"

/* Halo PC's multiplayer maps, in the order of its map list (its strings and
pictures), and an Xbox map of the same kind whose picture stands in
without Halo PC's ui.map */
static struct
{
	char const *file;
	wchar_t const *name;
	short xbox_picture_index;
} const ce_maps[] =
{
	{ "beavercreek", L"Battle Creek", 0 },
	{ "sidewinder", L"Sidewinder", 1 },
	{ "damnation", L"Damnation", 2 },
	{ "ratrace", L"Rat Race", 3 },
	{ "prisoner", L"Prisoner", 4 },
	{ "hangemhigh", L"Hang 'Em High", 5 },
	{ "chillout", L"Chill Out", 6 },
	{ "carousel", L"Derelict", 7 },
	{ "boardingaction", L"Boarding Action", 8 },
	{ "bloodgulch", L"Blood Gulch", 9 },
	{ "wizard", L"Wizard", 10 },
	{ "putput", L"Chiron TL34", 11 },
	{ "longest", L"Longest", 12 },
	{ "icefields", L"Ice Fields", 1 },
	{ "deathisland", L"Death Island", 1 },
	{ "dangercanyon", L"Danger Canyon", 9 },
	{ "infinity", L"Infinity", 9 },
	{ "timberland", L"Timberland", 9 },
	{ "gephyrophobia", L"Gephyrophobia", 2 },
};

/* a map file's place in ce_maps (by its name, any case), or NONE */
static long ce_map_index(
	char const *file)
{
	long index;

	for (index = 0; index < (long)NUMBEROF(ce_maps); index++)
	{
		char const *a = file;
		char const *b = ce_maps[index].file;

		while (*a && (*a | 0x20) == *b)
		{
			a++;
			b++;
		}
		if (!*a && !*b)
			return index;
	}
	return NONE;
}

/* a HaloMD map's name, by its file's name: as HaloMD's mod list names it,
or another version's of the same map (<map>_<version>), or NULL */
static wchar_t const *halomd_map_name(
	char const *file)
{
	size_t stem_length = strlen(file);
	long index;

	for (index = 0; index < (long)NUMBEROF(halomd_map_names); index++)
	{
		if (!_stricmp(file, halomd_map_names[index].file))
			return halomd_map_names[index].name;
	}
	/* (another version's: the name without its _<version>) */
	while (stem_length && file[stem_length - 1] >= '0' && file[stem_length - 1] <= '9')
		stem_length--;
	if (!stem_length || stem_length == strlen(file) || file[stem_length - 1] != '_')
		return NULL;
	for (index = 0; index < (long)NUMBEROF(halomd_map_names); index++)
	{
		char const *other = halomd_map_names[index].file;
		size_t other_length = strlen(other);

		if (other_length > stem_length && !_strnicmp(file, other, stem_length) &&
			strspn(other + stem_length, "0123456789") == other_length - stem_length)
		{
			return halomd_map_names[index].name;
		}
	}
	return NULL;
}

/* a map file's name made readable, as the game list's web pages make it:
its underscores, dots and dashes spaces, each word begun with a capital
(hugeass_v2: Hugeass V2) */
static void ce_map_tidy_name(
	char const *file,
	wchar_t *name,
	long size)
{
	long length = 0;
	boolean space = FALSE;

	for (; *file && length < size - 1; file++)
	{
		unsigned char character = (unsigned char)*file;

		if (character == '_' || character == '.' || character == '-' || character == ' ' || character == '\t')
		{
			space = length > 0;
			continue;
		}
		if (space)
		{
			if (length >= size - 2)
				break;
			name[length++] = ' ';
			space = FALSE;
		}
		if ((length == 0 || name[length - 1] == ' ') && character >= 'a' && character <= 'z')
			character = (unsigned char)(character - 'a' + 'A');
		name[length++] = (wchar_t)character;
	}
	name[length] = 0;
}

#ifdef HALO_CUSTOM_EDITION

#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "rasterizer/rasterizer_swizzle.h"
#include "tag_files/tag_groups.h"
#include "bmp_files.h"

#include <xtl.h>
#include <stdlib.h>

/* ---------- constants */

enum
{
	XBOX_MAP_COUNT = 13,
	/* (the Xbox's thirteen, then the Custom Edition and HaloMD maps found,
	up to this many rows in all) */
	MAXIMUM_MAP_LIST = 256,
	MAP_NAME_LENGTH = 64,
	DISPLAY_NAME_LENGTH = 48,
	DESCRIPTION_LENGTH = 160,
	/* text boxes' string list indices from here are this list's: the row
	times four, plus its string's kind */
	UI_MAP_LIST_STRING_BASE = 0x4000,
	UI_MAP_LIST_STRINGS_PER_ROW = 4,
	/* bitmap frames from here are Halo PC's map pictures */
	UI_MAP_LIST_PICTURE_BASE = 0x4000,

	/* Halo PC's ui.map: its cache file's version, the address its tags are
	read at, and its map list's entry for an unknown level */
	CE_CACHE_VERSION = 609,
	CE_TAGS_ADDRESS = 0x40440000,
	CE_UNKNOWN_LEVEL = 19,
	MAXIMUM_CE_STRINGS = 24,
	MAXIMUM_CE_PICTURES = 24,
	/* its bitmaps' pixels kept in bitmaps.map */
	CE_BITMAP_EXTERNAL_FLAG = 0x100,
	/* (the bitmaps' type of a 2D texture, as the game's) */
	CE_BITMAP_TYPE_2D = 0,
	/* the Xbox's map pictures' (ui.map's mp_map_grafix) unknown level
	(ui_widget_game_data_input_functions.c) */
	XBOX_UNKNOWN_LEVEL_PICTURE = 13,

	/* the campaign maps found, at most; their string list indices from
	here, as the multiplayer rows' (row times four, plus the kind) */
	MAXIMUM_CAMPAIGN_LIST = 256,
	UI_MAP_LIST_CAMPAIGN_STRING_BASE = 0x5000,
	/* bitmap frames from here are rows' own pictures (<name>.bmp): the
	multiplayer rows', then the campaign rows' (below ui_widget.c's 0x7000) */
	UI_MAP_LIST_ROW_PICTURE_BASE = 0x5800,
	UI_MAP_LIST_CAMPAIGN_PICTURE_BASE = 0x5C00,
	MAP_PATH_LENGTH = 256,

	/* a picture beside a map: the file read whole up to this size (a 4K
	screenshot is 25 MB as a bmp file), made a texture of this size (about
	twice the menus' picture) from the middle of it in the menus' picture
	shape (140 by 114 of their 640 by 480) */
	MAXIMUM_PICTURE_FILE_SIZE = 0x4000000,
	PICTURE_TEXTURE_SIZE = 256,
	PICTURE_SHAPE_WIDTH = 140,
	PICTURE_SHAPE_HEIGHT = 114,
	/* (bitmaps.c's 32-bit color with alpha) */
	PICTURE_BITMAP_FORMAT = 11,
	/* a description beside a map: the bytes read of it */
	MAXIMUM_DESCRIPTION_FILE_SIZE = 1024,
};

typedef char verify_ui_map_list_string_bases[
	UI_MAP_LIST_STRING_BASE + MAXIMUM_MAP_LIST * UI_MAP_LIST_STRINGS_PER_ROW <= UI_MAP_LIST_CAMPAIGN_STRING_BASE &&
	UI_MAP_LIST_CAMPAIGN_STRING_BASE + MAXIMUM_CAMPAIGN_LIST * UI_MAP_LIST_STRINGS_PER_ROW <= 0x7000 ? 1 : -1];
typedef char verify_ui_map_list_picture_bases[
	UI_MAP_LIST_PICTURE_BASE + MAXIMUM_CE_PICTURES <= UI_MAP_LIST_ROW_PICTURE_BASE &&
	UI_MAP_LIST_ROW_PICTURE_BASE + MAXIMUM_MAP_LIST <= UI_MAP_LIST_CAMPAIGN_PICTURE_BASE &&
	UI_MAP_LIST_CAMPAIGN_PICTURE_BASE + MAXIMUM_CAMPAIGN_LIST <= 0x7000 ? 1 : -1];

/* ---------- structures */

struct ui_map_entry
{
	char map_name[MAP_NAME_LENGTH];
	wchar_t display_name[DISPLAY_NAME_LENGTH];
	wchar_t lobby_name[DISPLAY_NAME_LENGTH];
	wchar_t description[DESCRIPTION_LENGTH];
	/* its row among the Xbox's (their strings and bitmap frames), or NONE */
	short xbox_index;
	short picture_index;
	/* Halo PC's or the Xbox's picture of it, for a row whose own picture
	cannot be shown */
	short stock_picture_index;
	/* its file (map_family_find's), beside which its picture and
	description are; its own picture, once read (NULL if it cannot be shown) */
	char path[MAP_PATH_LENGTH];
	boolean picture_read;
	struct bitmap_data *picture;
};

/* ---------- prototypes */

char const *cache_files_map_directory(void);
long bitmap_format_to_d3d_format(short format, word flags);

/* ---------- globals */

static struct ui_map_entry ui_map_list[MAXIMUM_MAP_LIST];
static char *ui_map_list_names_array[MAXIMUM_MAP_LIST];
static long ui_map_list_count_value;
/* the campaign maps found */
static struct ui_map_entry ui_campaign_list[MAXIMUM_CAMPAIGN_LIST];
static long ui_campaign_list_count;

/* what Halo PC's ui.map has of its map list, read once */
static struct
{
	boolean read;
	long name_count;
	wchar_t names[MAXIMUM_CE_STRINGS][DISPLAY_NAME_LENGTH];
	long description_count;
	wchar_t descriptions[MAXIMUM_CE_STRINGS][DESCRIPTION_LENGTH];
	long picture_count;
	struct bitmap_data pictures[MAXIMUM_CE_PICTURES];
} ce_ui;

/* Halo PC's ui.map's tags, while they are read (ce_ui_read) */
static byte *ce_tags;
static unsigned long ce_tags_size;

/* ---------- private code */

/* (the game's wide characters are 16 bits: not the C library's) */
static void wide_copy(
	wchar_t *destination,
	long size,
	wchar_t const *source)
{
	long index;

	for (index = 0; index < size - 1 && source[index]; index++)
		destination[index] = source[index];
	destination[index] = 0;
}

static void wide_append(
	wchar_t *destination,
	long size,
	wchar_t const *source)
{
	long length = 0;

	while (length < size - 1 && destination[length])
		length++;
	wide_copy(destination + length, size - length, source);
}

static boolean file_read(
	HANDLE file,
	unsigned long offset,
	void *buffer,
	unsigned long size)
{
	unsigned long bytes_read = 0;

	if (SetFilePointer(file, (long)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return FALSE;
	return ReadFile(file, buffer, size, &bytes_read, NULL) && bytes_read == size;
}

/* an address of Halo PC's ui.map's tags made a pointer to size bytes of
them, or NULL outside them */
static void *ce_tags_pointer(
	unsigned long address,
	unsigned long size)
{
	if (address < CE_TAGS_ADDRESS || address - CE_TAGS_ADDRESS > ce_tags_size ||
		size > ce_tags_size - (address - CE_TAGS_ADDRESS))
	{
		return NULL;
	}
	return ce_tags + (address - CE_TAGS_ADDRESS);
}

/* the data of the tag of a group and name (size bytes of it in the tags), or NULL */
static byte *ce_tag_find(
	unsigned long group_tag,
	char const *name,
	unsigned long size)
{
	unsigned long *header = ce_tags_pointer(CE_TAGS_ADDRESS, 0x10);
	unsigned long *instances;
	unsigned long index;

	if (!header)
		return NULL;
	/* (its instances' address, ..., their count: no more than its tags
hold, so that the size does not overflow) */
	if (header[3] > ce_tags_size / 0x20)
		return NULL;
	instances = ce_tags_pointer(header[0], header[3] * 0x20);
	if (!instances)
		return NULL;
	for (index = 0; index < header[3]; index++)
	{
		/* (an instance: its group, ..., its name, its data) */
		unsigned long *instance = instances + index * 8;
		char const *instance_name = ce_tags_pointer(instance[4], strlen(name) + 1);

		if (instance[0] == group_tag && instance_name && !memcmp(instance_name, name, strlen(name) + 1))
			return ce_tags_pointer(instance[5], size);
	}
	return NULL;
}

/* a unicode string list's strings, each up to length characters: how many */
static long ce_string_list_read(
	char const *name,
	wchar_t *strings,
	long length,
	long maximum_count)
{
	unsigned long *block = (unsigned long *)ce_tag_find('ustr', name, 0x0c);
	unsigned long *references;
	long count, index;

	if (!block)
		return 0;
	count = MIN((long)block[0], maximum_count);
	references = ce_tags_pointer(block[1], count * 0x14);
	if (!references)
		return 0;
	for (index = 0; index < count; index++)
	{
		/* (a tag data: its size, ..., its address) */
		unsigned long size = references[index * 5];
		unsigned short const *text = ce_tags_pointer(references[index * 5 + 3], size);
		wchar_t *string = strings + index * length;
		long character;

		for (character = 0; text && character < length - 1 && character < (long)(size / 2) && text[character];
			character++)
		{
			string[character] = text[character];
		}
		string[character] = 0;
	}
	return count;
}

/* the map list's pictures: each made a bitmap of its own, its pixels and its
Direct3D texture in contiguous memory, as the texture cache would make it */
static void ce_pictures_read(
	HANDLE ui_file)
{
	byte *group = ce_tag_find('bitm', "ui\\shell\\bitmaps\\mp_map_grafix", 0x6c);
	HANDLE bitmaps_file = INVALID_HANDLE_VALUE;
	unsigned long *block;
	byte *elements;
	long count, index;

	if (!group)
		return;
	/* (its bitmaps block) */
	block = (unsigned long *)(group + 0x60);
	count = MIN((long)block[0], MAXIMUM_CE_PICTURES);
	elements = ce_tags_pointer(block[1], count * 0x30);
	if (!elements)
		return;
	for (index = 0; index < count; index++)
	{
		byte *element = elements + index * 0x30;
		struct bitmap_data *bitmap = &ce_ui.pictures[index];
		unsigned short flags = *(unsigned short *)(element + 0xe);
		unsigned long pixels_offset = *(unsigned long *)(element + 0x18);
		unsigned long pixels_size = *(unsigned long *)(element + 0x1c);
		unsigned long allocated_size;
		HANDLE file = ui_file;
		D3DBaseTexture *texture;
		void *pixels;

		memset(bitmap, 0, sizeof(*bitmap));
		bitmap->signature = *(unsigned long *)element;
		bitmap->width = *(short *)(element + 0x4);
		bitmap->height = *(short *)(element + 0x6);
		bitmap->depth = *(short *)(element + 0x8);
		bitmap->type = *(short *)(element + 0xa);
		bitmap->format = *(short *)(element + 0xc);
		/* (Halo PC's own flags dropped: not cached, its pixels here) */
		bitmap->flags = flags & 0x3f;
		bitmap->mipmap_count = *(short *)(element + 0x14);
		bitmap->pixels_size = pixels_size;
		bitmap->tag_index = NONE;
		bitmap->cache_block_index = NONE;
		/* (a 2D bitmap the game draws, of a format with a Direct3D one:
		bitmap_format_to_d3d_format asserts there is) */
		if (bitmap->type != CE_BITMAP_TYPE_2D || bitmap->width <= 0 || bitmap->height <= 0 ||
			pixels_size == 0 || pixels_size > 0x100000 || !bitmap_verify(bitmap, FALSE) ||
			bitmap->format == 4 || bitmap->format == 5 || bitmap->format == 7 || bitmap->format == 12 ||
			bitmap->format == 13 || (bitmap->flags & 0x10))
		{
			break;
		}
		/* (as much as the renderer reads of its size and format, as the
		texture cache gives it: the pixels the file has, the rest zero) */
		allocated_size = MAX(pixels_size, (unsigned long)rasterizer_xbox_bitmap_get_pixel_data_size(bitmap));
		if (allocated_size > 0x400000)
			break;
		if (flags & CE_BITMAP_EXTERNAL_FLAG)
		{
			if (bitmaps_file == INVALID_HANDLE_VALUE)
			{
				char path[512];

				map_family_resource("bitmaps", path, sizeof(path));
				bitmaps_file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				if (bitmaps_file == INVALID_HANDLE_VALUE)
					break;
			}
			file = bitmaps_file;
		}
		pixels = XPhysicalAlloc(allocated_size, -1, 0, PAGE_READWRITE);
		texture = XPhysicalAlloc(sizeof(D3DBaseTexture), -1, 0, PAGE_READWRITE);
		if (pixels)
			memset(pixels, 0, allocated_size);
		if (!pixels || !texture || !file_read(file, pixels_offset, pixels, pixels_size))
		{
			if (pixels)
				XPhysicalFree(pixels);
			if (texture)
				XPhysicalFree(texture);
			break;
		}
		memset(texture, 0, sizeof(*texture));
		/* (texture_cache_initialize_hardware_format's, laid out as Halo PC
		lays out its pixels) */
		texture->Common = D3DCOMMON_TYPE_TEXTURE | 1 | D3DCOMMON_PORT_PC_LAYOUT;
		texture->Format =
			(floor_log2(bitmap->height) << D3DFORMAT_VSIZE_SHIFT) |
			(floor_log2(bitmap->width) << D3DFORMAT_USIZE_SHIFT) |
			(bitmap_format_to_d3d_format(bitmap->format, bitmap->flags) << D3DFORMAT_FORMAT_SHIFT) |
			(2 << D3DFORMAT_DIMENSION_SHIFT) |
			((rasterizer_xbox_bitmap_get_max_mipmap_count(bitmap) + 1) << D3DFORMAT_MIPMAP_SHIFT) |
			D3DFORMAT_BORDERSOURCE_COLOR |
			D3DFORMAT_DMACHANNEL_A;
		IDirect3DBaseTexture8_Register(texture, pixels);
		bitmap->base_address = xbox_address(pixels);
		bitmap->hardware_format = xbox_address(texture);
		ce_ui.picture_count = index + 1;
	}
	if (bitmaps_file != INVALID_HANDLE_VALUE)
		CloseHandle(bitmaps_file);
}

/* what Halo PC's ui.map (maps_ce\ui.map) has of its map list, read the first
time it is asked for */
static void ce_ui_read(
	void)
{
	char path[512];
	unsigned long header[6];
	HANDLE file;

	if (ce_ui.read)
		return;
	ce_ui.read = TRUE;
	map_family_resource("ui", path, sizeof(path));
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return;
	/* ('head', its version, its size, ..., its tags' offset and size) */
	if (file_read(file, 0, header, sizeof(header)) && header[0] == 'head' && header[1] == CE_CACHE_VERSION &&
		header[5] >= 0x10 && header[5] <= 0x4000000)
	{
		ce_tags = malloc(header[5]);
		ce_tags_size = header[5];
		if (ce_tags && file_read(file, header[4], ce_tags, header[5]))
		{
			ce_ui.name_count = ce_string_list_read("ui\\shell\\main_menu\\mp_map_list", ce_ui.names[0],
				DISPLAY_NAME_LENGTH, MAXIMUM_CE_STRINGS);
			ce_ui.description_count = ce_string_list_read(
				"ui\\shell\\main_menu\\multiplayer_type_select\\mp_map_select\\map_data", ce_ui.descriptions[0],
				DESCRIPTION_LENGTH, MAXIMUM_CE_STRINGS);
			ce_pictures_read(file);
		}
		free(ce_tags);
		ce_tags = NULL;
		ce_tags_size = 0;
	}
	CloseHandle(file);
}

/* a row added to the multiplayer maps' list, or the campaign maps': NULL
if it is full. Its picture is picture_index's until one of its own is found */
static struct ui_map_entry *entry_add(
	boolean campaign,
	char const *map_name,
	wchar_t const *display_name,
	wchar_t const *lobby_name,
	wchar_t const *description,
	short xbox_index,
	short picture_index)
{
	struct ui_map_entry *entry;

	if (campaign)
	{
		if (ui_campaign_list_count >= MAXIMUM_CAMPAIGN_LIST)
			return NULL;
		entry = &ui_campaign_list[ui_campaign_list_count++];
	}
	else
	{
		if (ui_map_list_count_value >= MAXIMUM_MAP_LIST)
			return NULL;
		entry = &ui_map_list[ui_map_list_count_value];
		ui_map_list_names_array[ui_map_list_count_value] = entry->map_name;
		ui_map_list_count_value++;
	}
	/* (its picture let go already: entries_forget) */
	memset(entry, 0, sizeof(*entry));
	snprintf(entry->map_name, sizeof(entry->map_name), "%s", map_name);
	wide_copy(entry->display_name, DISPLAY_NAME_LENGTH, display_name);
	wide_copy(entry->lobby_name, DISPLAY_NAME_LENGTH, lobby_name);
	wide_copy(entry->description, DESCRIPTION_LENGTH, description);
	entry->xbox_index = xbox_index;
	entry->picture_index = picture_index;
	entry->stock_picture_index = picture_index;
	return entry;
}

static void add_entry(
	char const *map_name,
	wchar_t const *display_name,
	wchar_t const *lobby_name,
	wchar_t const *description,
	short xbox_index,
	short picture_index)
{
	entry_add(FALSE, map_name, display_name, lobby_name, description, xbox_index, picture_index);
}

/* the rows' own pictures let go (the list is filled anew) */
static void entries_forget(
	struct ui_map_entry *entries,
	long count)
{
	long index;

	for (index = 0; index < count; index++)
	{
		if (entries[index].picture)
			bitmap_delete(entries[index].picture);
		entries[index].picture = NULL;
		entries[index].picture_read = FALSE;
	}
}

#ifndef HALO_WEB
/* the path of a file beside a map (its file's, with another extension in
place of .map): FALSE if it has none, or it does not fit */
static boolean beside_path(
	char const *map_path,
	char const *extension,
	char *path,
	long size)
{
	size_t length = strlen(map_path);

	if (length < 4 || _stricmp(map_path + length - 4, ".map") || length - 4 + strlen(extension) >= (size_t)size)
		return FALSE;
	snprintf(path, (size_t)size, "%.*s%s", (int)(length - 4), map_path, extension);
	return TRUE;
}

/* up to size bytes of a file (how many: NONE if there is no file), and its
length */
static long beside_file_read(
	char const *path,
	void *buffer,
	unsigned long size,
	unsigned long *file_size)
{
	HANDLE file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	unsigned long bytes_read = 0;

	if (file == INVALID_HANDLE_VALUE)
		return NONE;
	*file_size = GetFileSize(file, NULL);
	if (!ReadFile(file, buffer, size, &bytes_read, NULL) || bytes_read > size)
		bytes_read = 0;
	CloseHandle(file);
	return (long)bytes_read;
}

/* <name>.txt beside a map, its description (size characters at most): its
lines ended as the menus' strings end them (\r\n), its tabs spaces, and a
character past ASCII a '?' (once, for a UTF-8 character's first byte; a
UTF-8 byte order mark left out). FALSE if there is none, or nothing in it */
static boolean beside_description_read(
	char const *map_path,
	wchar_t *description,
	long size)
{
	char path[MAP_PATH_LENGTH + 8];
	byte text[MAXIMUM_DESCRIPTION_FILE_SIZE];
	unsigned long file_size = 0;
	long text_size, index = 0, length = 0;

	if (!beside_path(map_path, ".txt", path, sizeof(path)))
		return FALSE;
	text_size = beside_file_read(path, text, sizeof(text), &file_size);
	if (text_size <= 0)
		return FALSE;
	if (text_size >= 3 && text[0] == 0xEF && text[1] == 0xBB && text[2] == 0xBF)
		index = 3;
	for (; index < text_size && length < size - 1; index++)
	{
		byte character = text[index];

		if (character == '\n')
		{
			if (length + 2 > size - 1)
				break;
			description[length++] = '\r';
			description[length++] = '\n';
		}
		else if (character == '\t')
			description[length++] = ' ';
		else if (character >= ' ' && character < 0x7F)
			description[length++] = character;
		else if (character >= 0xC0)
			description[length++] = '?';
	}
	while (length > 0 &&
		(description[length - 1] == ' ' || description[length - 1] == '\r' || description[length - 1] == '\n'))
	{
		length--;
	}
	description[length] = 0;
	return length > 0;
}

/* whether a map has <name>.bmp beside it, of a size it is read at (whether
bmp_files.c reads it is found when it is first drawn) */
static boolean beside_picture_present(
	char const *map_path)
{
	char path[MAP_PATH_LENGTH + 8];
	byte signature[2];
	unsigned long file_size = 0;

	return beside_path(map_path, ".bmp", path, sizeof(path)) &&
		beside_file_read(path, signature, sizeof(signature), &file_size) == sizeof(signature) &&
		signature[0] == 'B' && signature[1] == 'M' && file_size <= MAXIMUM_PICTURE_FILE_SIZE;
}

/* <name>.bmp beside a map made a texture: the middle of it in the menus'
picture shape (bmp_files.c). NULL if it cannot be shown (logged) */
static struct bitmap_data *beside_picture_read(
	char const *map_path)
{
	char path[MAP_PATH_LENGTH + 8];
	struct bmp_file_picture picture;
	enum bmp_file_status status;
	struct bitmap_data *bitmap = NULL;
	unsigned long file_size = 0, bytes_read = 0;
	uint8_t *bytes = NULL;
	HANDLE file;

	if (!beside_path(map_path, ".bmp", path, sizeof(path)))
		return NULL;
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return NULL;
	file_size = GetFileSize(file, NULL);
	/* (a byte more, so that an empty file is not a failed allocation) */
	if (file_size <= MAXIMUM_PICTURE_FILE_SIZE)
		bytes = malloc(file_size + 1);
	if (bytes && (!ReadFile(file, bytes, file_size, &bytes_read, NULL) || bytes_read != file_size))
	{
		free(bytes);
		bytes = NULL;
	}
	CloseHandle(file);
	if (!bytes)
	{
		error(_error_silent, "the menus' map list: the picture %s could not be read", path);
		return NULL;
	}
	status = bmp_file_open(bytes, (uint32_t)file_size, &picture);
	if (status == _bmp_file_status_ok)
	{
		bitmap = bitmap_2d_new(PICTURE_TEXTURE_SIZE, PICTURE_TEXTURE_SIZE, 0, PICTURE_BITMAP_FORMAT);
		if (bitmap && bitmap->base_address)
		{
			bmp_file_fit(bytes, &picture, PICTURE_SHAPE_WIDTH, PICTURE_SHAPE_HEIGHT, PICTURE_TEXTURE_SIZE,
				PICTURE_TEXTURE_SIZE, XBOX_POINTER(uint32_t, bitmap->base_address));
			bitmap_rebuild(bitmap);
		}
		if (bitmap && !bitmap->hardware_format)
		{
			bitmap_delete(bitmap);
			bitmap = NULL;
		}
	}
	else
	{
		error(_error_silent, "the menus' map list: the picture %s cannot be shown: %s", path,
			bmp_file_status_describe(status));
	}
	free(bytes);
	return bitmap;
}
#endif

/* a picture of ours or the Xbox's (a bitmap frame of ui.map's map pictures),
for a row whose own cannot be shown */
static struct bitmap_data *stock_picture(
	short frame_index)
{
	long pictures;

	if (frame_index >= UI_MAP_LIST_PICTURE_BASE)
	{
		return frame_index - UI_MAP_LIST_PICTURE_BASE < ce_ui.picture_count ?
			&ce_ui.pictures[frame_index - UI_MAP_LIST_PICTURE_BASE] : NULL;
	}
	pictures = tag_loaded('bitm', "ui\\shell\\bitmaps\\mp_map_grafix");
	return pictures != NONE ? bitmap_group_get_bitmap_from_sequence(pictures, 0, frame_index) : NULL;
}

/* a row's own picture, read the first time it is drawn; Halo PC's or the
Xbox's in its place from then on if it cannot be shown */
static struct bitmap_data *entry_picture(
	struct ui_map_entry *entry)
{
#ifndef HALO_WEB
	if (!entry->picture_read)
	{
		entry->picture_read = TRUE;
		entry->picture = beside_picture_read(entry->path);
		if (!entry->picture)
			entry->picture_index = entry->stock_picture_index;
	}
#endif
	return entry->picture ? entry->picture : stock_picture(entry->stock_picture_index);
}

/* a Custom Edition, HaloMD or Halo PC map's row: its file's name (without
its suffix or .map), in the multiplayer maps or the campaign maps. A map of
Halo PC's is named, described and pictured as Halo PC has it, another by
its file's name; a campaign map by its file's name made readable, with Halo
PC's picture of an unknown level. Its own picture and description beside
it come first */
static void add_pc_entry(
	short family,
	char const *file,
	boolean campaign)
{
	char map_name[MAP_NAME_LENGTH];
	wchar_t name[DISPLAY_NAME_LENGTH];
	wchar_t display_name[DISPLAY_NAME_LENGTH];
	wchar_t lobby_name[DISPLAY_NAME_LENGTH];
	wchar_t own_description[DESCRIPTION_LENGTH];
	wchar_t const *description = campaign ? L"A Halo Custom\r\nEdition campaign\r\nmap" : L"A Halo Custom\r\nEdition map";
	wchar_t const *mark = L"CE";
	struct ui_map_entry *entry;
	long ce_index = CE_UNKNOWN_LEVEL;
	short picture_index = campaign ? XBOX_UNKNOWN_LEVEL_PICTURE : 9;
	short known;

	name[0] = 0;
	if (family == _map_family_halomd)
	{
		wchar_t const *known_name = campaign ? NULL : halomd_map_name(file);

		description = campaign ? L"A HaloMD campaign\r\nmap" : L"A HaloMD map";
		mark = L"MD";
		if (known_name)
			wide_copy(name, DISPLAY_NAME_LENGTH - 7, known_name);
	}
	else if (family == _map_family_halo_pc)
	{
		/* (Halo PC retail's maps are the stock ones Custom Edition has
		too, named and pictured as those) */
		description = campaign ? L"A Halo PC campaign\r\nmap" : L"A Halo PC map";
		mark = L"PC";
	}
	for (known = 0; !campaign && (family == _map_family_custom_edition || family == _map_family_halo_pc) &&
		known < (short)NUMBEROF(ce_maps); known++)
	{
		if (!_stricmp(file, ce_maps[known].file))
		{
			ce_index = known;
			picture_index = ce_maps[known].xbox_picture_index;
			wide_copy(name, DISPLAY_NAME_LENGTH, ce_maps[known].name);
			if (known < ce_ui.name_count && ce_ui.names[known][0])
				wide_copy(name, DISPLAY_NAME_LENGTH, ce_ui.names[known]);
			if (known < ce_ui.description_count)
				description = ce_ui.descriptions[known];
			break;
		}
	}
	if (!name[0] && campaign)
	{
		/* (a campaign map: its file's name made readable, as OpenCE names
		it) */
		ce_map_tidy_name(file, name, DISPLAY_NAME_LENGTH - 7);
	}
	if (!name[0])
	{
		/* (a map of another's making: its file's name) */
		long character;

		for (character = 0; file[character] && character < DISPLAY_NAME_LENGTH - 7; character++)
			name[character] = (wchar_t)(unsigned char)file[character];
		name[character] = 0;
	}
	if (ce_index < ce_ui.picture_count)
		picture_index = (short)(UI_MAP_LIST_PICTURE_BASE + ce_index);
	wide_copy(lobby_name, DISPLAY_NAME_LENGTH, name);
	wide_append(lobby_name, DISPLAY_NAME_LENGTH, L" [");
	wide_append(lobby_name, DISPLAY_NAME_LENGTH, mark);
	wide_append(lobby_name, DISPLAY_NAME_LENGTH, L"]");
	wide_copy(display_name, DISPLAY_NAME_LENGTH, name);
	wide_append(display_name, DISPLAY_NAME_LENGTH, L"\r\n[");
	wide_append(display_name, DISPLAY_NAME_LENGTH, mark);
	wide_append(display_name, DISPLAY_NAME_LENGTH, L"]");
	snprintf(map_name, sizeof(map_name), "%s%s", file, map_family_suffix(family));
	entry = entry_add(campaign, map_name, display_name, lobby_name, description, NONE, picture_index);
#ifndef HALO_WEB
	/* (its own description and picture, beside it) */
	if (!entry || !map_family_find(family, file, entry->path, sizeof(entry->path)))
		return;
	if (beside_description_read(entry->path, own_description, DESCRIPTION_LENGTH))
		wide_copy(entry->description, DESCRIPTION_LENGTH, own_description);
	if (beside_picture_present(entry->path))
	{
		entry->picture_index = (short)((campaign ? UI_MAP_LIST_CAMPAIGN_PICTURE_BASE : UI_MAP_LIST_ROW_PICTURE_BASE) +
			(campaign ? ui_campaign_list_count : ui_map_list_count_value) - 1);
	}
#else
	(void)entry;
	(void)own_description;
#endif
}

/* the files found of a family's maps, while the list is filled */
struct found_files
{
	char names[MAXIMUM_MAP_LIST][MAP_NAME_LENGTH];
	long count;
};

static void file_found(
	char const *file,
	void *context)
{
	struct found_files *found = context;

	if (found->count < MAXIMUM_MAP_LIST && strlen(file) < MAP_NAME_LENGTH - 3)
		snprintf(found->names[found->count++], MAP_NAME_LENGTH, "%s", file);
}

/* ---------- public code */

/* the list anew: the Xbox's maps (the game's thirteen names, in its order),
then the Custom Edition maps found, then HaloMD's, then Halo PC retail's;
and apart from them each family's campaign maps */
void ui_map_list_refresh(
	char *const *xbox_maps)
{
	static struct found_files found;
	/* (the counts last logged, to log each change once) */
	static long logged_counts[NUMBER_OF_MAP_FAMILIES] = { -1, -1, -1, -1 };
	static long logged_campaign_count = -1;
	long counts[NUMBER_OF_MAP_FAMILIES] = { XBOX_MAP_COUNT, 0, 0, 0 };
	short index, family;

	entries_forget(ui_map_list, ui_map_list_count_value);
	entries_forget(ui_campaign_list, ui_campaign_list_count);
	ui_map_list_count_value = 0;
	ui_campaign_list_count = 0;
	for (index = 0; index < XBOX_MAP_COUNT; index++)
		add_entry(xbox_maps[index], L"", L"", L"", index, index);
	for (family = _map_family_custom_edition; family < NUMBER_OF_MAP_FAMILIES; family++)
	{
		extern int platform_ce_tag_cache_ready;
		long file;

		/* (none of them would load without their tag cache, which the
		platform layer could not map: port/linux/src/xbox_memory.c) */
		if (!platform_ce_tag_cache_ready)
			break;
		found.count = 0;
		map_family_list(family, file_found, &found);
		if (found.count)
			ce_ui_read();
		/* (in the order of their names, as the list shows them) */
		qsort(found.names, found.count, MAP_NAME_LENGTH, (int (*)(void const *, void const *))_stricmp);
		for (file = 0; file < found.count; file++)
			add_pc_entry(family, found.names[file], FALSE);
		counts[family] = found.count;
	}
	for (family = _map_family_custom_edition; family < NUMBER_OF_MAP_FAMILIES; family++)
	{
		extern int platform_ce_tag_cache_ready;
		long file;

		if (!platform_ce_tag_cache_ready)
			break;
		found.count = 0;
		map_family_list_campaigns(family, file_found, &found);
		if (found.count)
			ce_ui_read();
		qsort(found.names, found.count, MAP_NAME_LENGTH, (int (*)(void const *, void const *))_stricmp);
		for (file = 0; file < found.count; file++)
			add_pc_entry(family, found.names[file], TRUE);
	}
	if (memcmp(counts, logged_counts, sizeof(counts)) || logged_campaign_count != ui_campaign_list_count)
	{
		memcpy(logged_counts, counts, sizeof(counts));
		logged_campaign_count = ui_campaign_list_count;
		error(_error_silent, "the menus' map list: %ld Custom Edition maps, %ld HaloMD maps, %ld Halo PC maps; "
			"%ld campaign maps of theirs", counts[_map_family_custom_edition], counts[_map_family_halomd],
			counts[_map_family_halo_pc], ui_campaign_list_count);
	}
}

long ui_map_list_count(
	void)
{
	return ui_map_list_count_value;
}

/* the rows' map names, for the list widget's items */
char **ui_map_list_names(
	void)
{
	return ui_map_list_names_array;
}

/* the row of a map's name (as the list or a game has it), or NONE */
long ui_map_list_find(
	char const *map_name)
{
	long row;

	if (!map_name)
		return NONE;
	for (row = 0; row < ui_map_list_count_value; row++)
	{
		if (!_stricmp(map_name, ui_map_list[row].map_name))
			return row;
	}
	return NONE;
}

/* a row's string list index for one of its strings (_ui_map_list_string_*):
the Xbox's own, or this list's */
short ui_map_list_string_index(
	long row,
	short kind)
{
	if (row < 0 || row >= ui_map_list_count_value)
		return 0;
	if (ui_map_list[row].xbox_index != NONE)
		return ui_map_list[row].xbox_index;
	return (short)(UI_MAP_LIST_STRING_BASE + row * UI_MAP_LIST_STRINGS_PER_ROW + kind);
}

/* a row's bitmap frame: an Xbox map's, or Halo PC's picture of it, or its
own */
short ui_map_list_picture_index(
	long row)
{
	if (row < 0 || row >= ui_map_list_count_value)
		return 0;
	return ui_map_list[row].picture_index;
}

long ui_map_list_campaign_count(
	void)
{
	return ui_campaign_list_count;
}

char const *ui_map_list_campaign_name(
	long index)
{
	return index >= 0 && index < ui_campaign_list_count ? ui_campaign_list[index].map_name : NULL;
}

long ui_map_list_campaign_find(
	char const *map_name)
{
	long index;

	for (index = 0; map_name && index < ui_campaign_list_count; index++)
	{
		if (!_stricmp(map_name, ui_campaign_list[index].map_name))
			return index;
	}
	return NONE;
}

short ui_map_list_campaign_string_index(
	long index,
	short kind)
{
	if (index < 0 || index >= ui_campaign_list_count)
		return 0;
	return (short)(UI_MAP_LIST_CAMPAIGN_STRING_BASE + index * UI_MAP_LIST_STRINGS_PER_ROW + kind);
}

short ui_map_list_campaign_picture_index(
	long index)
{
	if (index < 0 || index >= ui_campaign_list_count)
		return XBOX_UNKNOWN_LEVEL_PICTURE;
	return ui_campaign_list[index].picture_index;
}

/* the bitmap of a frame of this list's (Halo PC's pictures from
UI_MAP_LIST_PICTURE_BASE, the rows' own from UI_MAP_LIST_ROW_PICTURE_BASE
and UI_MAP_LIST_CAMPAIGN_PICTURE_BASE), or NULL for any other */
struct bitmap_data *ui_map_list_picture(
	short frame_index)
{
	if (frame_index >= UI_MAP_LIST_CAMPAIGN_PICTURE_BASE &&
		frame_index - UI_MAP_LIST_CAMPAIGN_PICTURE_BASE < ui_campaign_list_count)
	{
		return entry_picture(&ui_campaign_list[frame_index - UI_MAP_LIST_CAMPAIGN_PICTURE_BASE]);
	}
	if (frame_index >= UI_MAP_LIST_ROW_PICTURE_BASE && frame_index < UI_MAP_LIST_CAMPAIGN_PICTURE_BASE &&
		frame_index - UI_MAP_LIST_ROW_PICTURE_BASE < ui_map_list_count_value)
	{
		return entry_picture(&ui_map_list[frame_index - UI_MAP_LIST_ROW_PICTURE_BASE]);
	}
	if (frame_index < UI_MAP_LIST_PICTURE_BASE || frame_index - UI_MAP_LIST_PICTURE_BASE >= ce_ui.picture_count)
		return NULL;
	return &ce_ui.pictures[frame_index - UI_MAP_LIST_PICTURE_BASE];
}

/* the text of a string list index of this list's (UI_MAP_LIST_STRING_BASE
and up, the campaign maps' from UI_MAP_LIST_CAMPAIGN_STRING_BASE), or NULL
for any other */
wchar_t const *ui_map_list_text(
	short string_list_index)
{
	struct ui_map_entry *entry;
	long row;

	if (string_list_index < UI_MAP_LIST_STRING_BASE)
		return NULL;
	if (string_list_index >= UI_MAP_LIST_CAMPAIGN_STRING_BASE)
	{
		row = (string_list_index - UI_MAP_LIST_CAMPAIGN_STRING_BASE) / UI_MAP_LIST_STRINGS_PER_ROW;
		if (row >= ui_campaign_list_count)
			return NULL;
		entry = &ui_campaign_list[row];
	}
	else
	{
		row = (string_list_index - UI_MAP_LIST_STRING_BASE) / UI_MAP_LIST_STRINGS_PER_ROW;
		if (row >= ui_map_list_count_value)
			return NULL;
		entry = &ui_map_list[row];
	}
	switch ((string_list_index - UI_MAP_LIST_STRING_BASE) % UI_MAP_LIST_STRINGS_PER_ROW)
	{
	case _ui_map_list_string_description:
		return entry->description;
	case _ui_map_list_string_lobby_name:
		return entry->lobby_name;
	default:
		return entry->display_name;
	}
}

/* Halo PC's picture of a Custom Edition or HaloMD map, by its file's name:
its own of Halo PC's maps, its unknown level's for another's (and for every
HaloMD map); NULL without Halo PC's ui.map */
struct bitmap_data *ui_map_list_family_picture(
	short family,
	char const *file)
{
	long index = family == _map_family_custom_edition || family == _map_family_halo_pc ? ce_map_index(file) : NONE;

	ce_ui_read();
	if (index == NONE)
		index = CE_UNKNOWN_LEVEL;
	return index < ce_ui.picture_count ? &ce_ui.pictures[index] : NULL;
}

/* whether a family's folders have a map of this file's name, of its
family's version (where the game loads one from: cache_files_windows.c) */
boolean ui_map_list_family_present(
	short family,
	char const *file)
{
	char path[512];

	return family != _map_family_xbox && map_family_find(family, file, path, sizeof(path));
}

#endif

/* a Custom Edition or HaloMD map's name, by its file's name: Halo PC's own
for its maps (as its ui.map has it, on the builds with Custom Edition maps),
HaloMD's mod list's for HaloMD's, else the file's name made readable */
void ui_map_list_family_name(
	short family,
	char const *file,
	wchar_t *name,
	long size)
{
	long index = family == _map_family_custom_edition || family == _map_family_halo_pc ? ce_map_index(file) : NONE;
	wchar_t const *known = family == _map_family_halomd ? halomd_map_name(file) : NULL;
	long length;

	if (index == NONE && !known)
	{
		ce_map_tidy_name(file, name, size);
		return;
	}
	if (index == NONE)
	{
		for (length = 0; length < size - 1 && known[length]; length++)
			name[length] = known[length];
		name[length] = 0;
		return;
	}
	known = ce_maps[index].name;
#ifdef HALO_CUSTOM_EDITION
	ce_ui_read();
	if (index < ce_ui.name_count && ce_ui.names[index][0])
		known = ce_ui.names[index];
#endif
	for (length = 0; length < size - 1 && known[length]; length++)
		name[length] = known[length];
	name[length] = 0;
}
