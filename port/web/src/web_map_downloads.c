/* Custom Edition maps downloaded from the community's archives
 * (port/linux/src/map_downloads.h) for the web build: there are none. The
 * page fetches what it plays itself, and has no HTTPS download of the
 * platform layer's (update.h), so map_downloads.c and map_archives*.c are
 * left out of the build (tools/web_build.py, WEB_EXCLUDE). These stand in for
 * the game's calls: nothing is downloaded, as with maps.downloads off. */

#include "map_downloads.h"

void map_downloads_poll(void)
{
}

int map_downloads_fetch(const char *level_name, unsigned long version, const char *files)
{
	(void)level_name;
	(void)version;
	(void)files;
	return 0;
}

int map_downloads_fetching(char *status, int size)
{
	(void)status;
	(void)size;
	return 0;
}

int map_downloads_progress(float *fraction, char *name, int size)
{
	(void)fraction;
	(void)name;
	(void)size;
	return 0;
}

int map_downloads_take_ready(char *level_name, int size)
{
	(void)level_name;
	(void)size;
	return 0;
}

void map_downloads_cancel(void)
{
}
