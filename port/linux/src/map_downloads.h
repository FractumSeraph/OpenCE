/*
MAP_DOWNLOADS.H

Custom Edition maps downloaded from where the community keeps them
(map_downloads.c): a map a machine lacks when joining a game whose host does
not send it (map sharing, port/linux/game/map_share.c: a host of another
build, or one that does not share) comes from HaloNet's or HaloMaps's
archives or a web seed, as the index of the maps
(maps.index, port/assets/network/map_index.txt) names it, checked against
the index's hash.

Only integers and strings cross here: the game's units call these.
*/

#ifndef __HALO_LINUX_MAP_DOWNLOADS_H
#define __HALO_LINUX_MAP_DOWNLOADS_H

/* every frame, on the game's thread, a window or not (main.c's loop) */
void map_downloads_poll(void);

/* the files a joining machine lacks for the host's map: level_name the
map's (custom_maps\<name>, or custom_maps\<name>_<hash>), version its
header checksum as the host sent it (0: any), files their names,
tab-separated ("<name>.map\tbitmaps.map"; custom_edition_cache_files_missing).
Starts downloading them, if the index has them all and maps.downloads is on:
returns 1, and the join is to be tried again once map_downloads_take_ready
says they are there. 0 when nothing was started */
int map_downloads_fetch(const char *level_name, unsigned long version, const char *files);
/* what the download is doing, for the menus' status line: 1 and the text
while one is under way (or just failed), else 0 */
int map_downloads_fetching(char *status, int size);
/* how a download under way goes, for the screen it shows over the menus
(port/linux/game/download_screen.c): 1 while one is, with its share done (0
to 1; 0 while its size is not known yet) and the map's name (its file's,
without .map), else 0 */
int map_downloads_progress(float *fraction, char *name, int size);
/* once, when a download's files are all in place: 1 and the level name
whose join is to be tried again */
int map_downloads_take_ready(char *level_name, int size);
/* stops a download */
void map_downloads_cancel(void);

#endif
