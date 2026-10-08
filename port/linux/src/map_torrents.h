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
/* once, when a download's files are all in place: 1 and the level name
whose join is to be tried again */
int map_torrents_take_ready(char *level_name, int size);
/* stops a download (what came stays, to go on from) */
void map_torrents_cancel(void);

/* the map this machine's network game is on (NULL: none), as the host
(hosting 1) or a client: seeded while the game is on it, if the index has
it and maps.seed allows */
void map_torrents_playing(const char *level_name, unsigned long version, int hosting);

#endif
