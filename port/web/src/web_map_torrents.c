/* Custom Edition maps over BitTorrent for the browser build: there are
 * none. A web page cannot open the TCP and UDP sockets BitTorrent needs
 * (map_torrents.c and the torrent_*.c client are left out of the build:
 * tools/web_build.py), and the browser has every Custom Edition map the
 * site serves (custom_maps, read from the server as needed). The game then
 * behaves as with maps.torrents off: a missing map is said to be missing,
 * nothing is downloaded and nothing is seeded. */

#include "map_torrents.h"

void map_torrents_initialize(void)
{
}

void map_torrents_dispose(void)
{
}

void map_torrents_poll(void)
{
}

int map_torrents_fetch(const char *level_name, unsigned long version, const char *files)
{
	(void)level_name;
	(void)version;
	(void)files;
	return 0;
}

int map_torrents_fetching(char *status, int size)
{
	(void)status;
	(void)size;
	return 0;
}

int map_torrents_take_ready(char *level_name, int size)
{
	(void)level_name;
	(void)size;
	return 0;
}

void map_torrents_cancel(void)
{
}

void map_torrents_playing(const char *level_name, unsigned long version, int hosting)
{
	(void)level_name;
	(void)version;
	(void)hosting;
}
