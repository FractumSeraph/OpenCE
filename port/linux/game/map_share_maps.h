/*
MAP_SHARE_MAPS.H

What OpenCE's map sharing (map_share.c, map_share_files.c) asks of the
Custom Edition maps, on this fork's map families (halo_map_families.h)
rather than OpenCE's own Custom Edition loader (custom_edition_cache.c,
which ChupathingyCE's ce_*.c replaces here). Map sharing names a map as the
game's protocol does (custom_maps\<name>, a level name of OpenCE's), which
this fork's game names <name>@ce (map_family_wire_name, _from_wire_name).

No file of a player's is renamed, moved or replaced here: OpenCE renames
every map of custom_maps <name>_<hash>.map; this fork finds a version by its
checksum or its hash instead (map_family_find_version), and a download of a
version this machine has another of goes beside it as <name>_<hash>.map.
*/

#ifndef __MAP_SHARE_MAPS_H
#define __MAP_SHARE_MAPS_H

/* ---------- constants */

/* where a download goes: the Custom Edition maps' own folder (maps_ce, as
the map torrents' downloads, map_torrents.c) */
#define CUSTOM_EDITION_MAP_DIRECTORY "d:\\maps_ce\\"

/* a Custom Edition map's level name in the game's protocol, and its length
at most (the game engine keeps 63 characters of a level name) */
#define CUSTOM_EDITION_LEVEL_NAME_PREFIX "custom_maps\\"
#define CUSTOM_EDITION_MAXIMUM_LEVEL_NAME_LENGTH 63

/* the resource maps (OpenSauce data_file.hpp's numbers, map_share.h's
files) */
enum resource_map_type
{
	_resource_map_none,
	_resource_map_bitmaps,
	_resource_map_sounds,
	_resource_map_locale,

	NUMBER_OF_RESOURCE_MAP_TYPES
};

/* ---------- prototypes/MAP_SHARE_MAPS.C */

/* a resource map's file name: "bitmaps", "sounds", "loc" */
char const *resource_map_type_describe(enum resource_map_type type);

/* whether a level name is a Custom Edition map's (custom_maps\<name>) */
boolean custom_edition_level_name(char const *level_name);
/* the protocol's level name of the game's map name (<name>@ce:
custom_maps\<name>), into level_name (64 characters) */
void map_share_wire_level_name(char const *map_name, char *level_name);

/* the file of the level's map: the one this game plays (map_family_find:
this machine's own, or the version found for the game), into path (256
characters); FALSE if none */
boolean custom_edition_map_file_path(char const *level_name, char *path);
/* the same of a version (its checksum; 0: any): map_family_find_version */
boolean custom_edition_map_version_file_path(char const *level_name, unsigned long version, char *path);
/* the checksum of the level's map file (custom_edition_map_file_path's) */
unsigned long custom_edition_map_checksum(char const *level_name);
/* a resource map's file (enum resource_map_type), into path (256
characters); FALSE if none */
boolean custom_edition_resource_map_file_path(short type, char *path);
/* the resource maps the level's map needs: a bit (1 << type) for each that
its tags' index says holds some of its tags; 0 if it has no file */
unsigned long custom_edition_map_resource_maps_needed(char const *level_name);
/* whether this machine can play the level, of the host's version
(`checksum`; 0: any), with the resource maps it needs; when not, `message`
says why, for the player */
boolean custom_edition_cache_present(char const *level_name, unsigned long checksum, char *message,
	long message_size);
/* the map lists to be filled again (a map was downloaded) */
void custom_edition_maps_look_again(void);

#endif
