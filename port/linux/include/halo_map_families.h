/*
HALO_MAP_FAMILIES.H

The families of maps the native builds play (port/linux/game/map_families.c),
each named in playlists, the menus, the game list and Delta by a suffix to
its file's name, and each with a folder of its own beside maps\:

  - the Xbox's own maps, in maps\ (a scenario's name or path, no suffix);
  - Halo PC's Custom Edition maps (cache version 609), played as <name>@ce:
    maps_ce\<name>.map, with Custom Edition's bitmaps.map, sounds.map and
    loc.map beside them;
  - HaloMD's maps (Halo PC retail's cache version 7, the maps of HaloMD's
    mod list), played as <name>@md: maps_md\<name>.map;
  - Halo PC retail's own maps (version 7, from the player's Halo PC disc),
    played as <name>@pc: maps_pc\<name>.map.

A file named for its family (<name>@ce.map) is played from its family's
folder or from maps\ itself. The older places are read too: maps\ce
(ChupathingyCE 0.6 and 0.7.0b: Custom Edition's and HaloMD's maps), md_maps
(HaloMD's) and OpenCE's custom_maps (Custom Edition's), and last the folder
paths.custom_edition names, if any (a Halo Custom Edition install's maps
folder, or a folder of Custom Edition maps). A map past the
Xbox's is found by its family's folders and checked to be a cache file of
its family's version, so a Custom Edition map and a HaloMD map of the same
file name are told apart. Their resource maps (bitmaps.map, sounds.map,
loc.map) are Custom Edition's, for every family past the Xbox's.

The suffixes are this port's. In the game's protocol a Custom Edition map
is named as OpenCE's build-145 names it, custom_maps\<name>, and HaloMD's
and Halo PC retail's maps maps_md\<name>.md and maps_pc\<name>.pc, which no
OpenCE client has (map_family_wire_name); Delta Peer carries each map's
identity between ChupathingyCE machines (docs/delta.md).
*/

#ifndef HALO_MAP_FAMILIES_H
#define HALO_MAP_FAMILIES_H

enum
{
	_map_family_xbox,
	_map_family_custom_edition,
	_map_family_halomd,
	_map_family_halo_pc,
	NUMBER_OF_MAP_FAMILIES
};

/* a map's family, by its name (a scenario's name or path, or <file>@ce,
<file>@md), and its file's name (the path's last part, without the suffix),
in file (size bytes) if file is not NULL */
short map_family_parse(char const *map, char *file, long size);

/* a family's suffix ("@ce", "@md"; "" for the Xbox's) */
char const *map_family_suffix(short family);

/* a family's name as the menus and the server browser show it on a game
("HALO PC", "HALOMD", "HALO PC RETAIL"; "" for the Xbox's) */
char const *map_family_badge(short family);

/* a family's short tag on a listed game's map, the same in every menu set
and on the game list's pages: XBX (the Xbox's, maps\), HCE (Custom
Edition's, maps_ce\, @ce), HMD (HaloMD's, maps_md\, @md), GBX (Halo PC
retail's, Gearbox's, maps_pc\, @pc); what it stands for ("Custom Edition
map"), and its color (0xRRGGBB) */
char const *map_family_tag(short family);
char const *map_family_title(short family);
unsigned long map_family_color(short family);

/* (reserved for MCC's editing kit's maps, not played yet: no family) */
#define MAP_FAMILY_TAG_MCC "MCC"
#define MAP_FAMILY_TITLE_MCC "MCC map"
#define MAP_FAMILY_COLOR_MCC 0x5FD3C8

/* the folder a player puts a family's maps in, as the menus tell them
("maps_ce", "maps_md", "maps_pc"; "maps" for the Xbox's) */
char const *map_family_folder(short family);

/* a family's name in logs and messages ("Custom Edition", "HaloMD",
"Halo PC", "Xbox") */
char const *map_family_kind(short family);

/* a map's name (a scenario's name or path, or <file>@ce, @md, @pc) as the
game's protocol has it (the game's settings, its advertisement): an Xbox
map's as it is, custom_maps\<file>, maps_md\<file>.md, maps_pc\<file>.pc */
void map_family_wire_name(char const *map, char *wire, long size);

/* the map a name in the game's protocol names, as this port names it
(<file>@ce for custom_maps\<file>, and so on; any other name as it is,
<file>@ce of an older ChupathingyCE host among them), into map; its family */
short map_family_from_wire_name(char const *wire, char *map, long size);

/* whether a map (as scenario_tags_load names it: <file>, <file>@ce, ...)
came from a download, whose scripts are held to tighter rules (hs.c):
marked by the downloader, or named by game.downloaded_maps */
boolean map_is_downloaded(char const *map_name);
void map_downloaded_mark(char const *map_name);
/* ... and kept so in maps_ce/downloaded_maps.txt, for the next runs (the
map downloader's maps: port/linux/src/map_download.c) */
void map_downloaded_remember(char const *map_name);

#ifdef HALO_CUSTOM_EDITION
/* a family's cache version: 609 for Custom Edition's, 7 for HaloMD's and
Halo PC retail's */
long map_family_cache_version(short family);

/* whether the families past the Xbox's are played (game.custom_edition,
OpenCE's setting; on unless turned off): when not, none of their maps is
found or listed */
boolean map_families_enabled(void);

/* whether Halo PC maps can be played at all: their tag cache (platform.h)
was mapped at start-up. When not, why, in words for the player (why may be
NULL) */
boolean map_families_playable(char *why, long size);

/* the path of a Custom Edition resource map (bitmaps, sounds, loc; or
Halo PC's ui), in maps_ce or its older places: FALSE if there is none (path
then the place it goes) */
boolean map_family_resource(char const *name, char *path, long size);

/* the file of a map of a family past the Xbox's (its file's name without
the suffix), as cache_files_windows.c opens it: the path (for CreateFileA)
of the first of the family's places that is a cache file of the family's
version (the network game's version of its map first:
map_family_set_game_version); FALSE if there is none */
boolean map_family_find(short family, char const *file, char *path, long size);
/* the same of a version (as a network game's settings name it: the
checksum in its header for a host of network version 28 or before, the first
32 bits of its hash for one of 29, OpenCE's build-206, either taken:
map_family_version_matches; 0: any): this machine's own file of the name, or another
version's beside it (<file>_<hash>.map, OpenCE's map sharing's name), or for
a name with a hash (<base>_<8 hex digits>) its base's file of that version.
The file found is the name's for the rest of the game (map_family_find
finds it), until map_family_aliases_forget. No file is renamed. */
boolean map_family_find_version(short family, char const *file, unsigned long version, char *path, long size);
/* a Custom Edition name's file from now on (map_family_find's): for the
game (lasting FALSE: forgotten by map_family_aliases_forget, when this
machine leaves a network game), or for good (a host's map of another
version, played by its name alone: map_share_level_name_choose) */
void map_family_alias(char const *file, char const *path, boolean lasting);
void map_family_aliases_forget(void);
/* a Custom Edition cache file's checksum (its version for a host of network
version 28 or before); 0 if it is not one */
unsigned long map_family_file_checksum(char const *path);
/* ... the first 32 bits of its hash (its version for a host of 29, OpenCE's
build-206: map_share.h's map_share_hash_version, never 0); 0 if it cannot be
read */
unsigned long map_family_file_hash_version(char const *path);
/* whether the file is of the version a network game's settings name (0:
any), its checksum or its hash's */
boolean map_family_version_matches(char const *path, unsigned long version);

/* the map a network game is on and the version of it the host plays (0 for
any, which forgets it): map_family_find gives the file of that version, if
there is one, so that the map loaded is the host's */
void map_family_set_game_version(char const *map, unsigned long version);
/* ... read back: the host's version of this map (0: none, or another map) */
unsigned long map_family_game_version(char const *map);

/* every multiplayer map of a family past the Xbox's, in its folders: each
one's file's name (without the suffix, each once), passed to found */
void map_family_list(short family, void (*found)(char const *file, void *context), void *context);

/* the same of its campaign maps (a solo scenario's: OpenCE's CUSTOM
SINGLEPLAYER, played alone or hosted as network co-op) */
void map_family_list_campaigns(short family, void (*found)(char const *file, void *context), void *context);

/* whether a map (<file>@ce, @md, @pc) is a campaign map in its family's
folders: its file's header says its scenario is a solo one */
boolean map_family_campaign(char const *map);
#endif

#endif
