/*
MAP_FILE_HASH.H

A map file's whole-file hash prefix (map_archive_map_hash), as a hashed
name ends in (map_archive.h), read from the file and kept for the run
(map_file_hash.c): what finds a hashed name's map by its base name
(port/linux/game/map_families.c) and checks a downloaded one
(map_download.c).

Plain types only: the game's units (64-bit builds rewrite their long) and
the platform's both call it.
*/

#ifndef MAP_FILE_HASH_H
#define MAP_FILE_HASH_H

/* the hash prefix of the file at path (a game path, d:\...): 1 with it in
*prefix, else 0 (no such file, or not read whole). A file is read once for
its path, size and modification time; any thread may ask */
int map_file_hash_prefix(const char *path, unsigned int *prefix);

#endif
