/*
MAP_TORRENTS.H

Custom Edition maps over BitTorrent (map_torrents.c): a map a machine
lacks when joining a game on it is downloaded from other players and seed
boxes, and the map a machine's game is on is seeded, through the client of
torrent.h, as the index of the maps' torrents (maps.torrent_index,
port/assets/network/map_torrents.txt) names them.

Only integers and strings cross here: the game's units call these.
*/

#ifndef __HALO_LINUX_MAP_TORRENTS_H
#define __HALO_LINUX_MAP_TORRENTS_H

/* at start-up and shutdown (sdl_platform.c) */
void map_torrents_initialize(void);
void map_torrents_dispose(void);
/* every frame, on the game's thread (sdl_platform.c) */
void map_torrents_poll(void);

/* the files a joining machine lacks for the host's map: level_name the
map's (custom_maps\<name>), version its header checksum as the host sent
it (0: any), files their names ("<name>.map,bitmaps.map"). Starts
downloading them, if the index has them all and maps.torrents is on:
returns 1, and the join is to be tried again once map_torrents_take_ready
says they are there. 0 when nothing was started */
int map_torrents_fetch(const char *level_name, unsigned long version, const char *files);
/* what the download is doing, for the menus' status line: 1 and the text
while one is under way (or just failed), else 0 */
int map_torrents_fetching(char *status, int size);
/* how a download under way goes, for the screen it shows over the menus
(port/linux/game/download_screen.c): 1 while one is, with its share done (0
to 1; 0 while its size is not known yet) and the map's file name, else 0 */
int map_torrents_download_progress(float *fraction, char *name, int size);
/* once, when a download's files are all in place: 1 and the level name
whose join is to be tried again */
int map_torrents_take_ready(char *level_name, int size);
/* stops a download (what came stays, to go on from) */
void map_torrents_cancel(void);

/* the map this machine's network game is on (NULL: none), as the host
(hosting 1) or a client: seeded while the game is on it, if the index has
it and maps.seed allows */
void map_torrents_playing(const char *level_name, unsigned long version, int hosting);

/* port (this fork): the index as the approved map list. No map that is not
in it is downloaded from a host or other players (map sharing,
port/linux/game/map_share.c), nor from the map repositories (map_download.c);
BitTorrent and the archives fetch only the index's maps anyway. The index is
the maps' torrent index (maps.torrent_index), whose info hashes are also the
tracker's whitelist; a map is approved by adding it there
(tools/map_torrents.py). */
#define MAP_APPROVAL_CONTACT "@FractumSeraph"
/* whether a map sharing hash (32 bytes, as a host offers it: the share:<hash>
column, tools/map_share_hash.c) is an approved map's: 1 or 0. A Custom
Edition resource map (bitmaps, sounds, loc; checksum 0 in the index) is
approved by its name (file_name, NULL for none) whatever its hash */
int map_torrents_approved_hash(const char *file_name, const unsigned char *hash);
/* whether a map (its file name, <name>.map) of the host's version (its
header checksum, or from a host of OpenCE's network version 29 on its map
sharing hash's first 32 bits, or its whole-file BLAKE2b's; 0: any version)
is approved: 1 or 0 */
int map_torrents_approved(const char *file_name, unsigned int version);
/* what a player is told when a map is not approved: its file name, its hash
(64 hex digits, or NULL) or else the host's version, and whom to ask */
void map_torrents_unapproved_text(char *text, int size, const char *file_name, const unsigned char *hash,
	unsigned int version);

#endif
