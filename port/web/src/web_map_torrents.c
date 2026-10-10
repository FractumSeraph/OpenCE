/* Custom Edition maps for the browser build: no BitTorrent (a web page cannot
 * open the TCP and UDP sockets it needs: map_torrents.c and the torrent_*.c
 * client are left out of the build, tools/web_build.py), as the browser has
 * every Custom Edition map the site serves (custom_maps, read from the site
 * as needed). Nothing is seeded.
 *
 * What a fetch does here instead: the Server Browser joining a game on a
 * Custom Edition map (menu_functions.c, lobby_browser_join) has the page
 * fetch that map and the resource maps onto this device first
 * (fetch_path_normalization.js, HaloMapPrefetch), the status line showing
 * how it goes, and joins once they are here. A host gives a joining machine
 * only moments to load the map and add its player (a native host, 15
 * seconds), which reading the map piece by piece from a far site does not
 * fit in, the first time, on a slower connection. */

#include "map_torrents.h"

#include <emscripten/emscripten.h>
#include <stdio.h>
#include <string.h>

static struct
{
	int active;
	/* the map's file name (in custom_maps, without .map) */
	char name[64];
} fetch;

/* the page's prefetch: its state (0 under way, 1 done, 2 not on the site or
failed), and the megabytes done and to do */
static int fetch_progress(int *done_mb, int *total_mb)
{
	int state;

	*done_mb = MAIN_THREAD_EM_ASM_INT({
		var status = globalThis.HaloMapPrefetch ? globalThis.HaloMapPrefetch.status() : null;
		return status ? Math.floor(status.done / 1048576) : 0;
	});
	*total_mb = MAIN_THREAD_EM_ASM_INT({
		var status = globalThis.HaloMapPrefetch ? globalThis.HaloMapPrefetch.status() : null;
		return status ? Math.ceil(status.total / 1048576) : 0;
	});
	state = MAIN_THREAD_EM_ASM_INT({
		var status = globalThis.HaloMapPrefetch ? globalThis.HaloMapPrefetch.status() : null;
		return status ? status.state : 2;
	});
	return state;
}

void map_torrents_initialize(void)
{
}

void map_torrents_dispose(void)
{
}

void map_torrents_poll(void)
{
}

/* level_name: the map's file name; version and files are the torrents' */
int map_torrents_fetch(const char *level_name, unsigned long version, const char *files)
{
	char file[sizeof(fetch.name) + 4];

	(void)version;
	(void)files;
	if (!level_name || !*level_name || strlen(level_name) >= sizeof(fetch.name))
		return 0;
	snprintf(fetch.name, sizeof(fetch.name), "%s", level_name);
	snprintf(file, sizeof(file), "%s.map", level_name);
	if (!MAIN_THREAD_EM_ASM_INT({
			return globalThis.HaloMapPrefetch ? globalThis.HaloMapPrefetch.start(UTF8ToString($0)) : 0;
		}, file))
	{
		return 0;
	}
	fetch.active = 1;
	return 1;
}

int map_torrents_fetching(char *status, int size)
{
	int done_mb, total_mb;

	if (!fetch.active || fetch_progress(&done_mb, &total_mb) != 0)
		return 0;
	if (total_mb > 0)
		snprintf(status, (size_t)size, "Downloading %s: %d MB of %d MB", fetch.name, done_mb, total_mb);
	else
		snprintf(status, (size_t)size, "Getting %s ready...", fetch.name);
	return 1;
}

/* the map fetched (or not on the site: the join goes ahead and the game
says what is missing, as before) */
int map_torrents_take_ready(char *level_name, int size)
{
	int done_mb, total_mb;

	if (!fetch.active || fetch_progress(&done_mb, &total_mb) == 0)
		return 0;
	fetch.active = 0;
	snprintf(level_name, (size_t)size, "%s", fetch.name);
	return 1;
}

void map_torrents_cancel(void)
{
	fetch.active = 0;
}

void map_torrents_playing(const char *level_name, unsigned long version, int hosting)
{
	(void)level_name;
	(void)version;
	(void)hosting;
}
