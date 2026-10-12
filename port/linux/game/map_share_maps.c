/*
MAP_SHARE_MAPS.C

OpenCE's map sharing on this fork's map families (map_share_maps.h).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "halo_map_families.h"
#include "map_share_maps.h"
#include "map_share.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	PATH_SIZE = 256,
	LEVEL_NAME_SIZE = 64,
	/* a map file's name, at most (map_families.c's MAP_FAMILY_FILE_LENGTH) */
	FILE_NAME_SIZE = 64,

	/* a Custom Edition cache's header (cache_file_formats.c's offsets):
	where its tags are, and its version */
	CACHE_HEADER_BYTES = 0x68,
	CACHE_HEADER_SIGNATURE = 'head',
	CACHE_HEADER_VERSION_OFFSET = 0x04,
	CACHE_HEADER_TAG_DATA_OFFSET_OFFSET = 0x10,
	CACHE_HEADER_TAG_DATA_SIZE_OFFSET = 0x14,
	CUSTOM_EDITION_CACHE_VERSION = 609,

	/* its tags' index, at the start of the tag data, and its tags'
	instances, where the tag cache's address puts them */
	TAG_INDEX_BYTES = 0x28,
	TAG_INDEX_INSTANCES_OFFSET = 0x00,
	TAG_INDEX_COUNT_OFFSET = 0x0C,
	TAG_INDEX_SIGNATURE_OFFSET = 0x24,
	TAG_INDEX_SIGNATURE = 'tags',
	TAG_INSTANCE_BYTES = 0x20,
	TAG_INSTANCE_GROUP_OFFSET = 0x00,
	/* (set: the tag is held whole by a resource map) */
	TAG_INSTANCE_IN_RESOURCE_MAP_OFFSET = 0x18,
	MAXIMUM_TAG_COUNT = 0x10000,

	BITMAP_GROUP_TAG = 'bitm',
	SOUND_GROUP_TAG = 'snd!',
	FONT_GROUP_TAG = 'font',
	UNICODE_STRING_LIST_GROUP_TAG = 'ustr',
	HUD_MESSAGE_TEXT_GROUP_TAG = 'hmt ',
};

#define CUSTOM_EDITION_TAG_CACHE_ADDRESS 0x40440000UL

/* ---------- private code */

/* the file name of a Custom Edition level's map (custom_maps\<name>:
<name>), into file; FALSE if it is not one */
static boolean level_file(
	char const *level_name,
	char *file)
{
	char map[LEVEL_NAME_SIZE + 8];

	if (!custom_edition_level_name(level_name) ||
		map_family_from_wire_name(level_name, map, sizeof(map)) != _map_family_custom_edition)
	{
		return FALSE;
	}
	map_family_parse(map, file, FILE_NAME_SIZE);
	return file[0] != 0;
}

static unsigned long little_long(
	byte const *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

static boolean file_read(
	HANDLE file,
	unsigned long offset,
	unsigned long size,
	void *buffer)
{
	unsigned long read = 0;

	return SetFilePointer(file, (LONG)offset, NULL, FILE_BEGIN) == offset &&
		ReadFile(file, buffer, size, &read, NULL) && read == size;
}

/* ---------- public code */

char const *resource_map_type_describe(
	enum resource_map_type type)
{
	switch (type)
	{
	case _resource_map_bitmaps:
		return "bitmaps";
	case _resource_map_sounds:
		return "sounds";
	case _resource_map_locale:
		return "loc";
	default:
		return "";
	}
}

boolean custom_edition_level_name(
	char const *level_name)
{
	size_t length = strlen(CUSTOM_EDITION_LEVEL_NAME_PREFIX);

	return level_name && !_strnicmp(level_name, CUSTOM_EDITION_LEVEL_NAME_PREFIX, length) && level_name[length];
}

void map_share_wire_level_name(
	char const *map_name,
	char *level_name)
{
	map_family_wire_name(map_name ? map_name : "", level_name, LEVEL_NAME_SIZE);
}

boolean custom_edition_map_file_path(
	char const *level_name,
	char *path)
{
	char file[FILE_NAME_SIZE];

	return level_file(level_name, file) && map_family_find(_map_family_custom_edition, file, path, PATH_SIZE);
}

boolean custom_edition_map_version_file_path(
	char const *level_name,
	unsigned long version,
	char *path)
{
	char file[FILE_NAME_SIZE];

	return level_file(level_name, file) &&
		map_family_find_version(_map_family_custom_edition, file, version, path, PATH_SIZE);
}

unsigned long custom_edition_map_checksum(
	char const *level_name)
{
	char path[PATH_SIZE];

	return custom_edition_map_file_path(level_name, path) ? map_family_file_checksum(path) : 0;
}

boolean custom_edition_resource_map_file_path(
	short type,
	char *path)
{
	if (type <= _resource_map_none || type >= NUMBER_OF_RESOURCE_MAP_TYPES)
		return FALSE;
	return map_family_resource(resource_map_type_describe((enum resource_map_type)type), path, PATH_SIZE);
}

unsigned long custom_edition_map_resource_maps_needed(
	char const *level_name)
{
	char path[PATH_SIZE];
	byte header[CACHE_HEADER_BYTES];
	byte tag_index[TAG_INDEX_BYTES];
	byte *instances = NULL;
	unsigned long types = 0;
	unsigned long tag_data_offset, tag_data_size, instances_address, instances_offset;
	long tag_count, index;
	HANDLE file;

	if (!custom_edition_map_file_path(level_name, path))
		return 0;
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	/* (every size and offset checked: a file is anyone's) */
	if (!file_read(file, 0, sizeof(header), header) || little_long(header) != CACHE_HEADER_SIGNATURE ||
		little_long(header + CACHE_HEADER_VERSION_OFFSET) != CUSTOM_EDITION_CACHE_VERSION)
	{
		goto done;
	}
	tag_data_offset = little_long(header + CACHE_HEADER_TAG_DATA_OFFSET_OFFSET);
	tag_data_size = little_long(header + CACHE_HEADER_TAG_DATA_SIZE_OFFSET);
	if (tag_data_size < TAG_INDEX_BYTES || !file_read(file, tag_data_offset, sizeof(tag_index), tag_index) ||
		little_long(tag_index + TAG_INDEX_SIGNATURE_OFFSET) != TAG_INDEX_SIGNATURE)
	{
		goto done;
	}
	tag_count = (long)little_long(tag_index + TAG_INDEX_COUNT_OFFSET);
	instances_address = little_long(tag_index + TAG_INDEX_INSTANCES_OFFSET);
	if (tag_count <= 0 || tag_count > MAXIMUM_TAG_COUNT || instances_address < CUSTOM_EDITION_TAG_CACHE_ADDRESS)
		goto done;
	instances_offset = instances_address - CUSTOM_EDITION_TAG_CACHE_ADDRESS;
	if (instances_offset > tag_data_size || (unsigned long)tag_count * TAG_INSTANCE_BYTES > tag_data_size - instances_offset)
		goto done;
	instances = (byte *)malloc((size_t)tag_count * TAG_INSTANCE_BYTES);
	if (!instances ||
		!file_read(file, tag_data_offset + instances_offset, (unsigned long)tag_count * TAG_INSTANCE_BYTES, instances))
	{
		goto done;
	}
	for (index = 0; index < tag_count; index++)
	{
		byte const *instance = instances + index * TAG_INSTANCE_BYTES;

		if (!little_long(instance + TAG_INSTANCE_IN_RESOURCE_MAP_OFFSET))
			continue;
		switch (little_long(instance + TAG_INSTANCE_GROUP_OFFSET))
		{
		case BITMAP_GROUP_TAG:
			types |= 1u << _resource_map_bitmaps;
			break;
		case SOUND_GROUP_TAG:
			types |= 1u << _resource_map_sounds;
			break;
		case FONT_GROUP_TAG:
		case UNICODE_STRING_LIST_GROUP_TAG:
		case HUD_MESSAGE_TEXT_GROUP_TAG:
			types |= 1u << _resource_map_locale;
			break;
		}
	}

done:
	free(instances);
	CloseHandle(file);
	return types;
}

boolean custom_edition_cache_present(
	char const *level_name,
	unsigned long checksum,
	char *message,
	long message_size)
{
	char file[FILE_NAME_SIZE];
	char path[PATH_SIZE];
	unsigned long needed;
	short type;

	if (!level_file(level_name, file))
	{
		snprintf(message, (size_t)message_size, "The map %.63s is not a Custom Edition map.", level_name);
		return FALSE;
	}
	if (!map_families_enabled())
	{
		snprintf(message, (size_t)message_size,
			"The map %.63s is a Custom Edition map, and Halo PC maps are turned off (game.custom_edition in config.toml).",
			file);
		return FALSE;
	}
	if (!map_family_find_version(_map_family_custom_edition, file, checksum, path, sizeof(path)))
	{
		if (map_family_find(_map_family_custom_edition, file, path, sizeof(path)))
		{
			snprintf(message, (size_t)message_size,
				"Your Custom Edition map %.63s is another version than the host's. Copy the host's %.63s.map into maps_ce.",
				file, file);
		}
		else
		{
			snprintf(message, (size_t)message_size,
				"You don't have the Custom Edition map %.63s. If you have it, copy %.63s.map into maps_ce.", file, file);
		}
		return FALSE;
	}
	needed = custom_edition_map_resource_maps_needed(level_name);
	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		if ((needed & (1u << type)) && !custom_edition_resource_map_file_path(type, path))
		{
			snprintf(message, (size_t)message_size, "The map %.63s needs Custom Edition's %s.map in maps_ce.", file,
				resource_map_type_describe((enum resource_map_type)type));
			return FALSE;
		}
	}
	return TRUE;
}

boolean custom_edition_map_present(
	char const *map_name,
	unsigned long version,
	char *message,
	long message_size)
{
	char level_name[LEVEL_NAME_SIZE];

	map_share_wire_level_name(map_name, level_name);
	return custom_edition_cache_present(level_name, version, message, message_size);
}

void custom_edition_maps_look_again(
	void)
{
	/* (nothing kept: the menus' map lists look in the folders each time they
	are filled, ui_map_list_refresh) */
}
