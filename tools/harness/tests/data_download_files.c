/*
DATA_DOWNLOAD_FILES.C (test)

The real download of the site's files (port/linux/src/sdl_platform.c,
data_download) into a data root, the site a folder here and update_download
a copy out of it: Halo MCC's shared sets (mcc_shared/<set>/<name> in
maps.txt) go into maps_mcc/_shared/<set> (mcc_maps/_shared where only that
folder is); a set's file the player has is kept; the multiplayer set's
files are copied from maps_ce when maps.txt's SHA-256 says they are the
same file, and downloaded otherwise; a download whose SHA-256 is not the
list's is refused; names a set does not have are skipped.
test_data_download_files.py takes the types (types.inc) and the code
(under_test.inc).
*/

#include "harness.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_folder(path) _mkdir(path)
#else
#define make_folder(path) mkdir((path), 0755)
#endif

typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define SDL_strncasecmp(a, b, n) strncasecmp((a), (b), (n))
#define SDL_strcasecmp(a, b) strcasecmp((a), (b))
/* (the lock unused: one thread; renamed, as glibc's headers above declare
the real type) */
typedef int harness_mutex;
#define pthread_mutex_t harness_mutex
#define pthread_mutex_lock(lock) ((void)(lock))
#define pthread_mutex_unlock(lock) ((void)(lock))
#define DATA_PATH_SEPARATOR "/"

/* (the game's one SHA-256, which it builds: content_sha256.c, and its
old names, mcc_sha256.h) */
#define HALO_MCC 1
#include "../../../port/linux/game/content_sha256.c"
#define DATA_DOWNLOAD_SHA256 1

/* SDL's path information, from stat */
typedef enum { SDL_PATHTYPE_NONE, SDL_PATHTYPE_FILE, SDL_PATHTYPE_DIRECTORY } SDL_PathType;
typedef struct { SDL_PathType type; } SDL_PathInfo;

static BOOL SDL_GetPathInfo(const char *path, SDL_PathInfo *info)
{
	struct stat status;

	if (stat(path, &status) != 0)
		return FALSE;
	info->type = (status.st_mode & S_IFMT) == S_IFDIR ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_FILE;
	return TRUE;
}

static void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}

/* the site: https://site.test/assets/... is site/assets/...; each file
fetched is counted */
#define SITE_URL "https://site.test/"
static char site[512];
static char fetched[2048];

static int update_make_directory(const char *path)
{
	return make_folder(path) == 0 || errno == EEXIST;
}

static int update_delete_file(const char *path)
{
	return remove(path) == 0;
}

static int update_download(const char *url, const char *path, unsigned long long maximum,
	void (*received)(void *, unsigned long long, unsigned long long), void *context, char *error, int error_size)
{
	char from[1024];
	char buffer[4096];
	size_t read, total = 0;
	FILE *in, *out;

	if (strncmp(url, SITE_URL, strlen(SITE_URL)))
	{
		snprintf(error, (size_t)error_size, "not the site: %s", url);
		return 0;
	}
	snprintf(from, sizeof(from), "%s/%s", site, url + strlen(SITE_URL));
	in = fopen(from, "rb");
	if (!in)
	{
		snprintf(error, (size_t)error_size, "%s answered 404", url);
		return 0;
	}
	out = fopen(path, "wb");
	if (!out)
	{
		fclose(in);
		snprintf(error, (size_t)error_size, "cannot write %s", path);
		return 0;
	}
	while ((read = fread(buffer, 1, sizeof(buffer), in)) > 0 && total + read <= maximum)
	{
		fwrite(buffer, 1, read, out);
		total += read;
		if (received)
			received(context, total, maximum);
	}
	fclose(in);
	fclose(out);
	if (strlen(fetched) + strlen(url) + 2 < sizeof(fetched))
	{
		strcat(fetched, url + strlen(SITE_URL));
		strcat(fetched, "\n");
	}
	return 1;
}

#include "types.inc"
#include "under_test.inc"

/* ---------- the test's files */

static void write_file(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");

	if (!file)
	{
		fprintf(stderr, "cannot write %s\n", path);
		exit(2);
	}
	fputs(text, file);
	fclose(file);
}

/* the file's text ("" when it is not there) */
static const char *read_file(const char *path)
{
	static char text[256];
	FILE *file = fopen(path, "rb");
	size_t read;

	text[0] = 0;
	if (!file)
		return text;
	read = fread(text, 1, sizeof(text) - 1, file);
	text[read] = 0;
	fclose(file);
	return text;
}

static BOOL exists(const char *path)
{
	SDL_PathInfo info;

	return SDL_GetPathInfo(path, &info);
}

static void sha256_text(const char *text, char hex[65])
{
	struct mcc_sha256 hash;
	unsigned char digest[32];
	int index;

	mcc_sha256_begin(&hash);
	mcc_sha256_add(&hash, text, (int)strlen(text));
	mcc_sha256_end(&hash, digest);
	for (index = 0; index < 32; index++)
		sprintf(hex + index * 2, "%02x", digest[index]);
}

/* the files' contents, the same length where the list's sizes must match a copy */
#define SINGLEPLAYER_BITMAPS "mcc singleplayer bitmaps"
#define MULTIPLAYER_BITMAPS "mcc multiplayer bitmaps"
#define MULTIPLAYER_SOUNDS "mcc multiplayer sounds."
#define OTHER_SOUNDS "a custom edition sounds"
#define HALO_PC_BITMAPS "halo pc bitmaps"
#define HALO_PC_SOUNDS "halo pc sounds"

static char root[512];

static void path_of(char *path, size_t size, const char *relative)
{
	snprintf(path, size, "%s/%s", root, relative);
}

/* a site and a data root under base, with maps.txt's MCC lines hashed as
listed (corrupt: the site's sounds.map is not the file its line says) */
static void make_world(const char *base, BOOL corrupt)
{
	char path[1024];
	char list[2048];
	char singleplayer[65], multiplayer[65], sounds[65], pc_bitmaps[65];

	snprintf(site, sizeof(site), "%s/site", base);
	snprintf(root, sizeof(root), "%s/root", base);
	make_folder(base);
	make_folder(site);
	make_folder(root);
	snprintf(path, sizeof(path), "%s/assets", site);
	make_folder(path);
	snprintf(path, sizeof(path), "%s/assets/maps", site);
	make_folder(path);
	snprintf(path, sizeof(path), "%s/assets/custom_maps", site);
	make_folder(path);
	snprintf(path, sizeof(path), "%s/assets/mcc_shared", site);
	make_folder(path);
	snprintf(path, sizeof(path), "%s/assets/mcc_shared/singleplayer", site);
	make_folder(path);
	snprintf(path, sizeof(path), "%s/assets/mcc_shared/multiplayer", site);
	make_folder(path);
	snprintf(path, sizeof(path), "%s/assets/maps/ui.map", site);
	write_file(path, "ui");
	snprintf(path, sizeof(path), "%s/assets/custom_maps/bitmaps.map", site);
	write_file(path, MULTIPLAYER_BITMAPS);
	snprintf(path, sizeof(path), "%s/assets/mcc_shared/singleplayer/bitmaps.map", site);
	write_file(path, SINGLEPLAYER_BITMAPS);
	snprintf(path, sizeof(path), "%s/assets/mcc_shared/multiplayer/bitmaps.map", site);
	write_file(path, MULTIPLAYER_BITMAPS);
	snprintf(path, sizeof(path), "%s/assets/mcc_shared/multiplayer/sounds.map", site);
	write_file(path, corrupt ? "mcc multiplayer sounds!" : MULTIPLAYER_SOUNDS);
	snprintf(path, sizeof(path), "%s/assets/pc_shared", site);
	make_folder(path);
	snprintf(path, sizeof(path), "%s/assets/pc_shared/bitmaps.map", site);
	write_file(path, HALO_PC_BITMAPS);
	snprintf(path, sizeof(path), "%s/assets/pc_shared/sounds.map", site);
	write_file(path, HALO_PC_SOUNDS);
	snprintf(path, sizeof(path), "%s/assets/pc_shared/loc.map", site);
	write_file(path, "loc.!");
	sha256_text(SINGLEPLAYER_BITMAPS, singleplayer);
	sha256_text(MULTIPLAYER_BITMAPS, multiplayer);
	sha256_text(MULTIPLAYER_SOUNDS, sounds);
	sha256_text(HALO_PC_BITMAPS, pc_bitmaps);
	snprintf(list, sizeof(list),
		"ui.map\t2\n"
		"custom_maps/bitmaps.map\t%u\n"
		"mcc_shared/singleplayer/bitmaps.map\t%u\t%s\n"
		"mcc_shared/multiplayer/bitmaps.map\t%u\t%s\n"
		"mcc_shared/multiplayer/sounds.map\t%u\t%s\n"
		"mcc_shared/singleplayer/sounds.map\t5\n"
		"mcc_shared/multiplayer/loc.map\t5\n"
		"mcc_shared/other/bitmaps.map\t5\n"
		"pc_shared/bitmaps.map\t%u\t%s\n"
		"pc_shared/sounds.map\t%u\n"
		"pc_shared/loc.map\t5\n",
		(unsigned)strlen(MULTIPLAYER_BITMAPS),
		(unsigned)strlen(SINGLEPLAYER_BITMAPS), singleplayer,
		(unsigned)strlen(MULTIPLAYER_BITMAPS), multiplayer,
		(unsigned)strlen(MULTIPLAYER_SOUNDS), sounds,
		(unsigned)strlen(HALO_PC_BITMAPS), pc_bitmaps,
		(unsigned)strlen(HALO_PC_SOUNDS));
	snprintf(path, sizeof(path), "%s/assets/maps/maps.txt", site);
	write_file(path, list);
	fetched[0] = 0;
}

static BOOL download(int wanted, char *error, size_t error_size)
{
	static struct data_extraction extraction;
	BOOL succeeded;

	memset(&extraction, 0, sizeof(extraction));
	snprintf(extraction.url, sizeof(extraction.url), SITE_URL "assets/maps/");
	snprintf(extraction.destination, sizeof(extraction.destination), "%s", root);
	extraction.wanted = wanted;
	succeeded = data_download(&extraction);
	snprintf(error, error_size, "%s", extraction.error);
	return succeeded;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 2 ? argv[1] : "";
	char const *base = argc > 2 ? argv[2] : "";
	char path[1024];
	char error[512];

	/* MCC's shared files alone: the sets into maps_mcc/_shared, maps_ce's
	same bitmaps.map copied, its other sounds.map not, nothing else fetched */
	if (!strcmp(case_name, "mcc"))
	{
		make_world(base, FALSE);
		path_of(path, sizeof(path), "maps_ce");
		make_folder(path);
		path_of(path, sizeof(path), "maps_ce/bitmaps.map");
		write_file(path, MULTIPLAYER_BITMAPS);
		path_of(path, sizeof(path), "maps_ce/sounds.map");
		write_file(path, OTHER_SOUNDS);
		CHECK(strlen(OTHER_SOUNDS) == strlen(MULTIPLAYER_SOUNDS), "the test's sounds differ in size");
		CHECK(download(DATA_MAPS_MCC_SHARED, error, sizeof(error)), "failed: %s", error);
		path_of(path, sizeof(path), "maps_mcc/_shared/singleplayer/bitmaps.map");
		CHECK(!strcmp(read_file(path), SINGLEPLAYER_BITMAPS), "singleplayer bitmaps.map: '%s'", read_file(path));
		path_of(path, sizeof(path), "maps_mcc/_shared/multiplayer/bitmaps.map");
		CHECK(!strcmp(read_file(path), MULTIPLAYER_BITMAPS), "multiplayer bitmaps.map: '%s'", read_file(path));
		path_of(path, sizeof(path), "maps_mcc/_shared/multiplayer/sounds.map");
		CHECK(!strcmp(read_file(path), MULTIPLAYER_SOUNDS), "multiplayer sounds.map: '%s'", read_file(path));
		CHECK(!strstr(fetched, "mcc_shared/multiplayer/bitmaps.map"), "maps_ce's copy downloaded again:\n%s", fetched);
		CHECK(strstr(fetched, "mcc_shared/multiplayer/sounds.map") != NULL, "sounds.map not downloaded:\n%s", fetched);
		CHECK(!strstr(fetched, "ui.map") && !strstr(fetched, "custom_maps/"), "other files fetched:\n%s", fetched);
		path_of(path, sizeof(path), "maps_mcc/_shared/singleplayer/sounds.map");
		CHECK(!exists(path), "a name the singleplayer set has not was taken");
		path_of(path, sizeof(path), "maps_mcc/_shared/multiplayer/loc.map");
		CHECK(!exists(path), "a name the multiplayer set has not was taken");
		path_of(path, sizeof(path), "maps_mcc/_shared/multiplayer/bitmaps.map.partial");
		CHECK(!exists(path), "a partial file was left");
		path_of(path, sizeof(path), "maps_ce/sounds.map");
		CHECK(!strcmp(read_file(path), OTHER_SOUNDS), "maps_ce's sounds.map changed");
		return 0;
	}
	/* only mcc_maps there: the sets go into mcc_maps/_shared */
	if (!strcmp(case_name, "fallback-folder"))
	{
		make_world(base, FALSE);
		path_of(path, sizeof(path), "mcc_maps");
		make_folder(path);
		CHECK(download(DATA_MAPS_MCC_SHARED, error, sizeof(error)), "failed: %s", error);
		path_of(path, sizeof(path), "mcc_maps/_shared/multiplayer/sounds.map");
		CHECK(!strcmp(read_file(path), MULTIPLAYER_SOUNDS), "not in mcc_maps/_shared");
		path_of(path, sizeof(path), "maps_mcc");
		CHECK(!exists(path), "maps_mcc made beside mcc_maps");
		return 0;
	}
	/* a set's file the player has is kept, whatever it holds */
	if (!strcmp(case_name, "kept"))
	{
		make_world(base, FALSE);
		path_of(path, sizeof(path), "maps_mcc");
		make_folder(path);
		path_of(path, sizeof(path), "maps_mcc/_shared");
		make_folder(path);
		path_of(path, sizeof(path), "maps_mcc/_shared/multiplayer");
		make_folder(path);
		path_of(path, sizeof(path), "maps_mcc/_shared/multiplayer/sounds.map");
		write_file(path, "the player's own");
		CHECK(download(DATA_MAPS_MCC_SHARED, error, sizeof(error)), "failed: %s", error);
		CHECK(!strcmp(read_file(path), "the player's own"), "the player's sounds.map replaced");
		CHECK(!strstr(fetched, "sounds.map"), "the player's sounds.map downloaded:\n%s", fetched);
		return 0;
	}
	/* a download that is not the file its line says is refused */
	if (!strcmp(case_name, "corrupt"))
	{
		make_world(base, TRUE);
		CHECK(!download(DATA_MAPS_MCC_SHARED, error, sizeof(error)), "a corrupt sounds.map was taken");
		CHECK(strstr(error, "sounds.map") != NULL, "error: %s", error);
		path_of(path, sizeof(path), "maps_mcc/_shared/multiplayer/sounds.map");
		CHECK(!exists(path), "the corrupt sounds.map is in place");
		path_of(path, sizeof(path), "maps_mcc/_shared/multiplayer/sounds.map.partial");
		CHECK(!exists(path), "its partial file was left");
		return 0;
	}
	/* Everything: the maps folder, Custom Edition's and MCC's (maps_ce's
	bitmaps.map, downloaded first, then copied for the multiplayer set) */
	if (!strcmp(case_name, "everything"))
	{
		make_world(base, FALSE);
		CHECK(download(DATA_MAPS_ALL, error, sizeof(error)), "failed: %s", error);
		path_of(path, sizeof(path), "maps/ui.map");
		CHECK(!strcmp(read_file(path), "ui"), "ui.map");
		path_of(path, sizeof(path), "maps_ce/bitmaps.map");
		CHECK(!strcmp(read_file(path), MULTIPLAYER_BITMAPS), "maps_ce/bitmaps.map");
		path_of(path, sizeof(path), "maps_mcc/_shared/singleplayer/bitmaps.map");
		CHECK(!strcmp(read_file(path), SINGLEPLAYER_BITMAPS), "singleplayer bitmaps.map");
		path_of(path, sizeof(path), "maps_mcc/_shared/multiplayer/bitmaps.map");
		CHECK(!strcmp(read_file(path), MULTIPLAYER_BITMAPS), "multiplayer bitmaps.map");
		CHECK(!strstr(fetched, "mcc_shared/multiplayer/bitmaps.map"), "maps_ce's copy downloaded again:\n%s", fetched);
		path_of(path, sizeof(path), "maps_pc/sounds.map");
		CHECK(!strcmp(read_file(path), HALO_PC_SOUNDS), "maps_pc/sounds.map");
		return 0;
	}
	/* Multiplayer only: not MCC's, nor Halo PC's */
	if (!strcmp(case_name, "multiplayer"))
	{
		make_world(base, FALSE);
		CHECK(download(DATA_MAPS_MULTIPLAYER, error, sizeof(error)), "failed: %s", error);
		CHECK(!strstr(fetched, "mcc_shared"), "MCC's fetched:\n%s", fetched);
		CHECK(!strstr(fetched, "pc_shared"), "Halo PC's fetched:\n%s", fetched);
		path_of(path, sizeof(path), "maps_mcc");
		CHECK(!exists(path), "maps_mcc made");
		return 0;
	}
	/* each start's shared files: Custom Edition's, MCC's and Halo PC's,
	never a map of the maps folder, never a name a set has not, and a
	player's own copy kept */
	if (!strcmp(case_name, "shared"))
	{
		make_world(base, FALSE);
		path_of(path, sizeof(path), "maps_pc");
		make_folder(path);
		path_of(path, sizeof(path), "maps_pc/sounds.map");
		write_file(path, "the player's own");
		CHECK(download(DATA_MAPS_SHARED_FILES, error, sizeof(error)), "failed: %s", error);
		CHECK(!strstr(fetched, "maps/ui.map"), "a map fetched:\n%s", fetched);
		path_of(path, sizeof(path), "maps/ui.map");
		CHECK(!exists(path), "ui.map downloaded");
		path_of(path, sizeof(path), "maps_ce/bitmaps.map");
		CHECK(!strcmp(read_file(path), MULTIPLAYER_BITMAPS), "maps_ce/bitmaps.map: '%s'", read_file(path));
		path_of(path, sizeof(path), "maps_mcc/_shared/singleplayer/bitmaps.map");
		CHECK(!strcmp(read_file(path), SINGLEPLAYER_BITMAPS), "singleplayer bitmaps.map: '%s'", read_file(path));
		path_of(path, sizeof(path), "maps_pc/bitmaps.map");
		CHECK(!strcmp(read_file(path), HALO_PC_BITMAPS), "maps_pc/bitmaps.map: '%s'", read_file(path));
		path_of(path, sizeof(path), "maps_pc/sounds.map");
		CHECK(!strcmp(read_file(path), "the player's own"), "the player's maps_pc/sounds.map replaced");
		CHECK(!strstr(fetched, "pc_shared/sounds.map"), "the player's sounds.map downloaded:\n%s", fetched);
		path_of(path, sizeof(path), "maps_pc/loc.map");
		CHECK(!exists(path), "a name Halo PC's shared files have not was taken");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
