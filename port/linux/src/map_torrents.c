/*
MAP_TORRENTS.C

Custom Edition maps over BitTorrent (map_torrents.h), with the client of
torrent.h.

A machine joining a game on a Custom Edition map (custom_maps\<name>) it
lacks, or has another version of, asks here for it before it joins
(ui_widget_port_join_map_fetch in menu_functions.c; and
cache_files_map_present, when the host changes map after the join), with
Custom Edition's bitmaps.map, sounds.map and loc.map when they are missing.
Each is looked up in the index of the maps' torrents (maps.torrent_index:
the file name and, for the map, the header checksum the host sent, which is
the game's map version: custom_edition_map_checksum), and downloaded into
custom_maps\downloads: first, when the index names the community archives'
zips that hold it (HaloNet's, HaloMaps's: maps.archives), out of one of
those (map_archives.c), then checked against its torrent's info hash (added
as a seed, which hashes it); else, or when that fails, over BitTorrent.
A file that arrives whole is moved into custom_maps,
or, for a map of which another version is there, into
custom_maps\versions\<checksum> beside it (custom_edition_cache.h): no file
already there is ever replaced, renamed or changed. Once every file is
there the menus try the join again (map_torrents_take_ready). The menus show
how it goes (map_torrents_fetching, map_torrents_download_progress).

A map is found where the game finds Custom Edition maps
(custom_edition_cache.h): the versions folder's of the version wanted, the
data root's custom_maps folder, then a Halo Custom Edition install's maps
folder (h:\, paths.custom_edition).

The map a machine's network game is on is seeded meanwhile, from where it
is found, when maps.seed allows: "host" (the default) seeds only while
hosting, "all" also while joined, "off" never. The upload limit
(maps.upload_limit) is for the whole client, however many are downloading:
the game's own traffic comes first.

The client is started when first needed, with the port, limits, trackers
and web seeds of the [maps] settings, and stopped with the game.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "map_archives.h"
#include "map_torrents.h"
#include "torrent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif

#define INDEX_FORMAT "# halo map torrents v1"
/* the Custom Edition maps' folders (custom_edition_cache.h's
CUSTOM_EDITION_MAP_DIRECTORY and CUSTOM_EDITION_INSTALL_MAP_DIRECTORY):
downloads go into the first */
#define CUSTOM_MAPS_XBOX_PATH "d:\\custom_maps\\"
#define INSTALL_MAPS_XBOX_PATH "h:\\maps\\"
/* a Custom Edition map's level name (CUSTOM_EDITION_LEVEL_NAME_PREFIX) */
#define LEVEL_NAME_PREFIX "custom_maps\\"
#define DOWNLOADS_FOLDER "downloads"
/* another version of a map than the one of its name there
(CUSTOM_EDITION_VERSIONS_FOLDER): versions\<checksum>\<name>.map */
#define VERSIONS_FOLDER "versions"
#define MAP_FILE_EXTENSION ".map"
#define FETCH_MAXIMUM_FILES 4
#define FAILURE_SHOWN_MILLISECONDS 15000
#define STATUS_INTERVAL 500
#define SEED_CHECK_INTERVAL 5000
#define NAME_SIZE 128

/* ---------- the index */

struct index_entry
{
	/* into torrent_maps_index.names */
	unsigned long name;
	unsigned long checksum;
	unsigned long long size;
	unsigned long piece_length;
	char info_hash[41];
	/* into torrent_maps_index.names: the archives' zips that hold it, as
	the index's columns after the info hash (tab-separated "halonet:<zip>",
	"halomaps:<file id>"; map_archives.h), "" for none */
	unsigned long sources;
};

static struct
{
	int loaded;
	struct index_entry *entries;
	int count;
	char *names;
	unsigned long names_size;
	char path[1024];
} torrent_maps_index;

static int same_name(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char ca = *a, cb = *b;

		if (ca >= 'A' && ca <= 'Z')
			ca = (char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z')
			cb = (char)(cb - 'A' + 'a');
		if (ca != cb)
			return 0;
		if (!ca)
			return 1;
	}
}

static void index_path(char *path, int size)
{
	const char *name = config_string("maps.torrent_index");

	if (name[0] == '/' || name[0] == '\\' || (name[0] && name[1] == ':'))
	{
		snprintf(path, (size_t)size, "%s", name);
		return;
	}
	config_folder(path, (unsigned long)size);
	snprintf(path + strlen(path), (size_t)size - strlen(path), "%s", name);
}

/* reads the index once: each line a map's file name, header checksum,
size, piece length and info hash, and the archives' zips that hold it,
tab-separated (tools/map_torrents.py) */
static void index_load(void)
{
	char *file;
	size_t file_size = 0;
	char *line;
	char *next;
	int capacity = 0;
	int number = 0;

	if (torrent_maps_index.loaded)
		return;
	torrent_maps_index.loaded = 1;
	index_path(torrent_maps_index.path, sizeof(torrent_maps_index.path));
	file = config_file_read(torrent_maps_index.path, &file_size);
	if (!file)
	{
		platform_log("map torrents: the index %s cannot be read (maps.torrent_index)", torrent_maps_index.path);
		return;
	}
	if (strncmp(file, INDEX_FORMAT, strlen(INDEX_FORMAT)))
	{
		platform_log("map torrents: %s is not a map torrent index (its first line is not \"%s\")",
			torrent_maps_index.path, INDEX_FORMAT);
		free(file);
		return;
	}
	torrent_maps_index.names = malloc(file_size + 1);
	if (!torrent_maps_index.names)
	{
		free(file);
		return;
	}
	for (line = file; line && *line; line = next)
	{
		char *fields[5];
		char *sources;
		int field;
		char *end;
		struct index_entry *entry;
		size_t name_length;

		number++;
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		end = line + strlen(line);
		while (end > line && (end[-1] == '\r' || end[-1] == ' '))
			*--end = 0;
		if (!line[0] || line[0] == '#')
			continue;
		fields[0] = line;
		for (field = 1; field < 5; field++)
		{
			char *tab = strchr(fields[field - 1], '\t');

			if (!tab)
				break;
			*tab = 0;
			fields[field] = tab + 1;
		}
		sources = field == 5 ? strchr(fields[4], '\t') : NULL;
		if (sources)
			*sources++ = 0;
		if (field < 5 || strlen(fields[4]) != 40)
		{
			platform_log("map torrents: %s line %d is not an entry (left out)", torrent_maps_index.path, number);
			continue;
		}
		if (torrent_maps_index.count == capacity)
		{
			struct index_entry *grown;

			capacity = capacity ? capacity * 2 : 1024;
			grown = realloc(torrent_maps_index.entries, (size_t)capacity * sizeof(*grown));
			if (!grown)
				break;
			torrent_maps_index.entries = grown;
		}
		entry = &torrent_maps_index.entries[torrent_maps_index.count];
		name_length = strlen(fields[0]);
		entry->name = torrent_maps_index.names_size;
		memcpy(torrent_maps_index.names + torrent_maps_index.names_size, fields[0], name_length + 1);
		torrent_maps_index.names_size += (unsigned long)name_length + 1;
		entry->checksum = strtoul(fields[1], NULL, 16);
		entry->size = strtoull(fields[2], NULL, 10);
		entry->piece_length = strtoul(fields[3], NULL, 10);
		memcpy(entry->info_hash, fields[4], 41);
		/* (the names and sources together are shorter than the file) */
		entry->sources = torrent_maps_index.names_size;
		name_length = sources ? strlen(sources) : 0;
		memcpy(torrent_maps_index.names + torrent_maps_index.names_size, sources ? sources : "", name_length + 1);
		torrent_maps_index.names_size += (unsigned long)name_length + 1;
		name_length = strlen(torrent_maps_index.names + entry->name);
		if (!name_length || !entry->size || !entry->piece_length)
		{
			platform_log("map torrents: %s line %d has a bad size or piece length (left out)",
				torrent_maps_index.path, number);
			continue;
		}
		torrent_maps_index.count++;
	}
	free(file);
	platform_log("map torrents: %d maps indexed in %s", torrent_maps_index.count, torrent_maps_index.path);
}

/* the entry of the file named, of the checksum (0: any; an entry's 0 is
its own version's, as some maps' headers have, not any) */
static const struct index_entry *index_find(const char *file_name, unsigned long checksum)
{
	int which;

	index_load();
	for (which = 0; which < torrent_maps_index.count; which++)
	{
		const struct index_entry *entry = &torrent_maps_index.entries[which];

		if (same_name(torrent_maps_index.names + entry->name, file_name) && (!checksum || entry->checksum == checksum))
		{
			return entry;
		}
	}
	return NULL;
}

/* the entry of the file named (<name>.map) or, for a version 27 host's
level name (<name>_<8 hexadecimal digits>: those builds named each map for
its hash; custom_edition_cache.c's map_name_unhashed), of the name without
the hash; of the checksum (0: any) */
static const struct index_entry *index_find_file(const char *file_name, unsigned long checksum)
{
	const struct index_entry *entry = index_find(file_name, checksum);
	size_t length = strlen(file_name);
	char base[NAME_SIZE];
	size_t index;

	if (entry || length < 10 + strlen(MAP_FILE_EXTENSION) || length >= sizeof(base))
		return entry;
	length -= strlen(MAP_FILE_EXTENSION);
	if (file_name[length - 9] != '_')
		return NULL;
	for (index = length - 8; index < length; index++)
	{
		char c = file_name[index];

		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			return NULL;
	}
	snprintf(base, sizeof(base), "%.*s" MAP_FILE_EXTENSION, (int)(length - 9), file_name);
	return index_find(base, checksum);
}

/* ---------- the client */

static struct
{
	int started;
	int failed;
} engine;

static int engine_start(void)
{
	struct torrent_settings settings;
	char error[128];

	if (engine.started)
		return 1;
	if (engine.failed)
		return 0;
	memset(&settings, 0, sizeof(settings));
	settings.port = (int)config_integer("maps.torrent_port");
	settings.upload_limit = config_integer("maps.upload_limit") * 1024;
	settings.download_limit = config_integer("maps.download_limit") * 1024;
	settings.trackers = config_string("maps.trackers");
	settings.web_seeds = config_string("maps.web_seeds");
	settings.maximum_peers = 40;
	if (!torrent_start(&settings, error, sizeof(error)))
	{
		platform_log("map torrents: the client could not start: %s", error);
		engine.failed = 1;
		return 0;
	}
	engine.started = 1;
	return 1;
}

/* ---------- paths */

/* the custom_maps folder (made if missing), with its separator */
static int custom_maps_folder(char *path, int size)
{
	size_t length;

	platform_translate_path(CUSTOM_MAPS_XBOX_PATH, path, (unsigned long)size);
	length = strlen(path);
	while (length && (path[length - 1] == '/' || path[length - 1] == '\\'))
		path[--length] = 0;
	if (!posix_make_directory(path))
	{
		struct posix_file_information information;

		if (posix_stat(path, &information) != 0 || !(information.flags & _posix_file_is_directory))
		{
			platform_log("map torrents: the folder %s cannot be made", path);
			return 0;
		}
	}
	snprintf(path + length, (size_t)size - length, PATH_SEPARATOR);
	return 1;
}

/* the folder named below the one in path (made if missing), with its
separator */
static int folder_below(char *path, int size, const char *name)
{
	size_t length = strlen(path);

	snprintf(path + length, (size_t)size - length, "%s", name);
	if (!posix_make_directory(path))
	{
		struct posix_file_information information;

		if (posix_stat(path, &information) != 0 || !(information.flags & _posix_file_is_directory))
		{
			platform_log("map torrents: the folder %s cannot be made", path);
			return 0;
		}
	}
	length = strlen(path);
	snprintf(path + length, (size_t)size - length, PATH_SEPARATOR);
	return 1;
}

static int downloads_folder(char *path, int size)
{
	return custom_maps_folder(path, size) && folder_below(path, size, DOWNLOADS_FOLDER);
}

/* custom_maps\versions\<checksum> (made if missing), with its separator */
static int version_folder(unsigned long checksum, char *path, int size)
{
	char name[16];

	snprintf(name, sizeof(name), "%08lX", checksum);
	return custom_maps_folder(path, size) && folder_below(path, size, VERSIONS_FOLDER) &&
		folder_below(path, size, name);
}

static int file_present(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && !(information.flags & _posix_file_is_directory);
}

/* the map's file name from its level name: custom_maps\<file> (or the
file's name alone) is <file>.map; "" for any other (the game's own maps) */
static void level_file_name(const char *level_name, char *file_name, int size)
{
	const char *file = level_name;
	size_t prefix = strlen(LEVEL_NAME_PREFIX);

	if (!strncmp(level_name, LEVEL_NAME_PREFIX, prefix) || !strncmp(level_name, "custom_maps/", prefix))
		file = level_name + prefix;
	else if (strpbrk(level_name, "\\/:"))
		file = "";
	/* (a name with no folder, as the folders' files have) */
	if (!file[0] || strpbrk(file, "\\/:") || file[0] == '.' || strlen(file) + strlen(MAP_FILE_EXTENSION) >= NAME_SIZE)
	{
		file_name[0] = 0;
		return;
	}
	snprintf(file_name, (size_t)size, "%s" MAP_FILE_EXTENSION, file);
}

/* where the file named (<file>.map, or a resource map) is, where the game
finds Custom Edition maps (custom_maps\versions\<checksum> for the version
`checksum` when not 0, custom_maps, then a Custom Edition install's maps
folder): the host's path; 0 if it is in none */
static int map_file_path(const char *file_name, unsigned long checksum, char *path, int size)
{
	static const char *const folders[] = { CUSTOM_MAPS_XBOX_PATH, INSTALL_MAPS_XBOX_PATH };
	char xbox_path[256];
	int folder;

	if (strlen(file_name) <= strlen(MAP_FILE_EXTENSION) || strpbrk(file_name, "\\/:"))
		return 0;
	if (checksum)
	{
		snprintf(xbox_path, sizeof(xbox_path), "%s" VERSIONS_FOLDER "\\%08lX\\%s", CUSTOM_MAPS_XBOX_PATH, checksum,
			file_name);
		platform_translate_path(xbox_path, path, (unsigned long)size);
		if (file_present(path))
			return 1;
	}
	for (folder = 0; folder < (int)(sizeof(folders) / sizeof(folders[0])); folder++)
	{
		/* (a Custom Edition install's folder only when paths.custom_edition names one) */
		if (folder == 1 && !config_string("paths.custom_edition")[0])
			continue;
		snprintf(xbox_path, sizeof(xbox_path), "%s%s", folders[folder], file_name);
		platform_translate_path(xbox_path, path, (unsigned long)size);
		if (file_present(path))
			return 1;
	}
	return 0;
}

/* ---------- downloading */

/* how a file of a fetch is coming */
enum
{
	/* waiting for the archives (one file at a time) */
	_fetch_archive_waiting,
	/* out of an archive's zip (map_archives.c) */
	_fetch_archive,
	/* from the archive, added as a seed: hashed against the info hash */
	_fetch_seed_check,
	/* over BitTorrent */
	_fetch_torrent,
};

static struct
{
	int active;
	char level_name[NAME_SIZE];
	unsigned long version;
	struct
	{
		char name[NAME_SIZE];
		/* the torrent's (when phase is _fetch_seed_check or _fetch_torrent) */
		int handle;
		int done;
		/* the versions folder's checksum it goes into (another version of
		it being in custom_maps); 0 for custom_maps */
		unsigned long version_folder;
		int phase;
		const struct index_entry *entry;
		/* where it is downloaded (custom_maps\downloads) */
		char path[1200];
	} files[FETCH_MAXIMUM_FILES];
	int count;
	unsigned long started;
	int ready;
	char failure[192];
	unsigned long failed_time;
	char status[256];
	unsigned long status_time;
} fetch;

static void fetch_stop(void)
{
	int which;

	for (which = 0; which < fetch.count; which++)
	{
		if (fetch.files[which].done)
			continue;
		if (fetch.files[which].phase == _fetch_seed_check || fetch.files[which].phase == _fetch_torrent)
			torrent_remove(fetch.files[which].handle);
		else if (fetch.files[which].phase == _fetch_archive)
			map_archives_stop();
	}
	fetch.active = 0;
	fetch.count = 0;
}

/* the file downloaded over BitTorrent (seeding: a file from an archive,
hashed against the info hash); 1 if added */
static int fetch_torrent_add(int which, int seeding)
{
	const struct index_entry *entry = fetch.files[which].entry;
	char error[128];
	int handle;

	handle = torrent_add(entry->info_hash, torrent_maps_index.names + entry->name, entry->size, entry->piece_length,
		fetch.files[which].path, seeding, error, sizeof(error));
	if (handle < 0)
	{
		platform_log("map torrents: %s could not be added: %s", fetch.files[which].name, error);
		return 0;
	}
	fetch.files[which].handle = handle;
	fetch.files[which].phase = seeding ? _fetch_seed_check : _fetch_torrent;
	return 1;
}

static void fetch_fail(const char *name, const char *reason)
{
	snprintf(fetch.failure, sizeof(fetch.failure), "Could not download %s: %s", name, reason);
	fetch.failed_time = GetTickCount() ? GetTickCount() : 1;
	platform_log("map torrents: %s", fetch.failure);
	fetch_stop();
}

int map_torrents_fetch(const char *level_name, unsigned long version, const char *files)
{
	char map_file[NAME_SIZE];
	char folder[1024];
	const char *text = files;
	int count = 0;
	struct
	{
		char name[NAME_SIZE];
		const struct index_entry *entry;
		unsigned long version_folder;
	} wanted[FETCH_MAXIMUM_FILES];
	int which;

	char computed[256];

	/* (none while Custom Edition maps cannot be played: game.custom_edition) */
	if (!config_boolean("maps.torrents") || !config_boolean("game.custom_edition") || !level_name)
		return 0;
	if (!files)
	{
		/* (asked for a map alone: it, and Custom Edition's resource maps
		that this machine lacks) */
		static const char *const resources[] = { "bitmaps.map", "sounds.map", "loc.map" };
		char path[1024];
		int resource;

		level_file_name(level_name, map_file, sizeof(map_file));
		if (!map_file[0])
			return 0;
		snprintf(computed, sizeof(computed), "%s", map_file);
		for (resource = 0; resource < (int)(sizeof(resources) / sizeof(resources[0])); resource++)
		{
			size_t used = strlen(computed);

			if (!map_file_path(resources[resource], 0, path, sizeof(path)))
				snprintf(computed + used, sizeof(computed) - used, "\t%s", resources[resource]);
		}
		files = computed;
		text = files;
	}
	if (!files[0])
		return 0;
	if (fetch.active)
	{
		if (same_name(fetch.level_name, level_name) && fetch.version == version)
			return 1;
		platform_log("map torrents: the download of %s's files gives way to %s's", fetch.level_name, level_name);
		fetch_stop();
	}
	level_file_name(level_name, map_file, sizeof(map_file));
	while (*text)
	{
		/* (tab-separated: some maps' names have commas, none a tab) */
		const char *comma = strchr(text, '\t');
		int length = comma ? (int)(comma - text) : (int)strlen(text);
		unsigned long checksum;

		if (length > 0 && count < FETCH_MAXIMUM_FILES)
		{
			if (length >= NAME_SIZE)
				length = NAME_SIZE - 1;
			memcpy(wanted[count].name, text, (size_t)length);
			wanted[count].name[length] = 0;
			/* (the map is the host's version; a resource map any) */
			checksum = same_name(wanted[count].name, map_file) ? version : 0;
			wanted[count].entry = index_find_file(wanted[count].name, checksum);
			if (!wanted[count].entry)
			{
				platform_log("map torrents: %s (checksum %08lX) is not in the index: not downloaded", wanted[count].name,
					checksum);
				return 0;
			}
			/* (a map of which another version is here goes beside it, in
			the versions folder, that one left as it is: the host's version
			is in neither, custom_edition_cache_files_missing found) */
			wanted[count].version_folder = 0;
			if (checksum)
			{
				char path[1024];

				/* (by the index's name: a version 27 host's has a hash after it) */
				if (map_file_path(torrent_maps_index.names + wanted[count].entry->name, 0, path, sizeof(path)))
					wanted[count].version_folder = checksum;
			}
			count++;
		}
		text = comma ? comma + 1 : text + length;
	}
	if (!count || !engine_start() || !downloads_folder(folder, sizeof(folder)))
		return 0;
	memset(&fetch, 0, sizeof(fetch));
	for (which = 0; which < count; which++)
	{
		const struct index_entry *entry = wanted[which].entry;

		snprintf(fetch.files[which].path, sizeof(fetch.files[which].path), "%s%s", folder,
			torrent_maps_index.names + entry->name);
		snprintf(fetch.files[which].name, sizeof(fetch.files[which].name), "%s", torrent_maps_index.names + entry->name);
		fetch.files[which].entry = entry;
		fetch.files[which].version_folder = wanted[which].version_folder;
		fetch.count++;
		/* (an archive's zip first, when the index names one of
		maps.archives; then BitTorrent) */
		if (map_archives_available(torrent_maps_index.names + entry->sources))
		{
			fetch.files[which].phase = _fetch_archive_waiting;
		}
		else if (!fetch_torrent_add(which, 0))
		{
			fetch_stop();
			return 0;
		}
	}
	snprintf(fetch.level_name, sizeof(fetch.level_name), "%s", level_name);
	fetch.version = version;
	fetch.active = 1;
	fetch.started = GetTickCount();
	platform_log("map torrents: downloading %d file(s) for %s: %s", count, level_name, files);
	return 1;
}

/* a file that arrived: into custom_maps, or its versions folder; never in
place of a file there */
static int fetch_file_done(int which)
{
	char folder[1024];
	char destination[1200];

	if (fetch.files[which].version_folder ?
		!version_folder(fetch.files[which].version_folder, folder, sizeof(folder)) :
		!custom_maps_folder(folder, sizeof(folder)))
	{
		return 0;
	}
	snprintf(destination, sizeof(destination), "%s%s", folder, fetch.files[which].name);
	if (file_present(destination))
	{
		platform_log("map torrents: %s is already there: left as it is", destination);
		return 0;
	}
	if (!torrent_move(fetch.files[which].handle, destination))
	{
		platform_log("map torrents: %s could not be moved into %s", fetch.files[which].name, folder);
		return 0;
	}
	torrent_remove(fetch.files[which].handle);
	fetch.files[which].done = 1;
	platform_log("map torrents: %s is in %s", fetch.files[which].name, folder);
	return 1;
}

/* set when a fetch is done: seed_poll tries again a level it could not
seed (its map not there yet, say) */
static int seed_failed_forget;

static void fetch_poll(void)
{
	int which;
	int done = 0;

	if (!fetch.active)
		return;
	for (which = 0; which < fetch.count; which++)
	{
		struct torrent_status status;

		if (fetch.files[which].done)
		{
			done++;
			continue;
		}
		if (fetch.files[which].phase == _fetch_archive_waiting)
		{
			const struct index_entry *entry = fetch.files[which].entry;
			unsigned long long received, total;
			char archive[32];
			int other;

			/* (one at a time: the next file waits while another's fetch is
			under way or its result not yet taken) */
			for (other = 0; other < fetch.count && fetch.files[other].phase != _fetch_archive; other++)
				;
			if (other < fetch.count ||
				map_archives_poll(&received, &total, archive, sizeof(archive)) == _map_archives_running)
			{
				continue;
			}
			map_archives_stop();
			if (map_archives_start(fetch.files[which].name, entry->size, torrent_maps_index.names + entry->sources,
				fetch.files[which].path))
			{
				fetch.files[which].phase = _fetch_archive;
			}
			else if (!fetch_torrent_add(which, 0))
			{
				fetch_fail(fetch.files[which].name, "the client could not take it");
				return;
			}
			continue;
		}
		if (fetch.files[which].phase == _fetch_archive)
		{
			unsigned long long received, total;
			char archive[32];
			enum map_archives_state state = map_archives_poll(&received, &total, archive, sizeof(archive));

			if (state == _map_archives_running)
				continue;
			map_archives_stop();
			/* (what an archive gave is hashed as a seed's file is; what it did
			not give comes over BitTorrent) */
			if (!fetch_torrent_add(which, state == _map_archives_done) &&
				(state != _map_archives_done || !fetch_torrent_add(which, 0)))
			{
				fetch_fail(fetch.files[which].name, "the client could not take it");
				return;
			}
			continue;
		}
		if (!torrent_status_get(fetch.files[which].handle, &status))
		{
			fetch_fail(fetch.files[which].name, "the client lost it");
			return;
		}
		if (status.state == _torrent_state_failed && fetch.files[which].phase == _fetch_seed_check)
		{
			/* (an archive's file that is not the torrent's: downloaded over
			BitTorrent instead, in its place) */
			platform_log("map torrents: %s from the archive is not the index's (%s): downloading it over BitTorrent",
				fetch.files[which].name, status.detail);
			torrent_remove(fetch.files[which].handle);
			unlink(fetch.files[which].path);
			if (!fetch_torrent_add(which, 0))
			{
				fetch_fail(fetch.files[which].name, "the client could not take it");
				return;
			}
			continue;
		}
		if (status.state == _torrent_state_failed)
		{
			fetch_fail(fetch.files[which].name, status.detail);
			return;
		}
		if (status.state == _torrent_state_seeding)
		{
			if (!fetch_file_done(which))
			{
				fetch_fail(fetch.files[which].name, "it could not be put in place");
				return;
			}
			done++;
		}
	}
	if (done == fetch.count)
	{
		/* (a map not found to seed before it came: tried again) */
		seed_failed_forget = 1;
		platform_log("map torrents: every file of %s is there (%lu s)", fetch.level_name,
			(GetTickCount() - fetch.started) / 1000);
		fetch.active = 0;
		fetch.ready = 1;
	}
}

static void size_text(unsigned long long bytes, char *text, int size)
{
	if (bytes >= 100ULL << 20)
		snprintf(text, (size_t)size, "%llu MB", bytes >> 20);
	else if (bytes >= 1ULL << 20)
		snprintf(text, (size_t)size, "%.1f MB", (double)bytes / (double)(1 << 20));
	else
		snprintf(text, (size_t)size, "%llu KB", bytes >> 10);
}

static void fetch_status_update(void)
{
	int which;
	unsigned long now = GetTickCount();

	if (!fetch.active)
		return;
	if (fetch.status[0] && now - fetch.status_time < STATUS_INTERVAL)
		return;
	fetch.status_time = now;
	for (which = 0; which < fetch.count; which++)
	{
		struct torrent_status status;
		char which_text[24] = "";
		char have[32], total[32], rate[32];

		if (fetch.files[which].done)
			continue;
		if (fetch.count > 1)
			snprintf(which_text, sizeof(which_text), " (%d of %d)", which + 1, fetch.count);
		if (fetch.files[which].phase == _fetch_archive_waiting)
			continue;
		if (fetch.files[which].phase == _fetch_archive)
		{
			unsigned long long received, all;
			char archive[32];

			map_archives_poll(&received, &all, archive, sizeof(archive));
			size_text(received, have, sizeof(have));
			size_text(all, total, sizeof(total));
			if (all)
			{
				snprintf(fetch.status, sizeof(fetch.status), "Downloading %s%s from %s: %s of %s",
					fetch.files[which].name, which_text, archive, have, total);
			}
			else
			{
				snprintf(fetch.status, sizeof(fetch.status), "Downloading %s%s from %s", fetch.files[which].name,
					which_text, archive);
			}
			return;
		}
		if (!torrent_status_get(fetch.files[which].handle, &status))
			continue;
		switch (status.state)
		{
		case _torrent_state_metadata:
			snprintf(fetch.status, sizeof(fetch.status), "Downloading %s%s: %s", fetch.files[which].name, which_text,
				status.detail);
			break;
		case _torrent_state_checking:
			snprintf(fetch.status, sizeof(fetch.status), "Checking %s%s", fetch.files[which].name, which_text);
			break;
		case _torrent_state_downloading:
			size_text(status.have_bytes, have, sizeof(have));
			size_text(status.total_bytes, total, sizeof(total));
			size_text((unsigned long long)status.download_rate, rate, sizeof(rate));
			snprintf(fetch.status, sizeof(fetch.status), "Downloading %s%s: %s of %s (%s/s, %d peers)",
				fetch.files[which].name, which_text, have, total, rate, status.peers_connected);
			break;
		default:
			snprintf(fetch.status, sizeof(fetch.status), "Downloading %s%s", fetch.files[which].name, which_text);
			break;
		}
		return;
	}
}

int map_torrents_fetching(char *status, int size)
{
	if (fetch.active)
	{
		fetch_status_update();
		snprintf(status, (size_t)size, "%s", fetch.status[0] ? fetch.status : "Downloading...");
		return 1;
	}
	if (fetch.failed_time && GetTickCount() - fetch.failed_time < FAILURE_SHOWN_MILLISECONDS)
	{
		snprintf(status, (size_t)size, "%s", fetch.failure);
		return 1;
	}
	return 0;
}

int map_torrents_download_progress(float *fraction, char *name, int size)
{
	float done = 0.0f;
	int which;

	if (!fetch.active)
		return 0;
	/* (each file an equal share: the size of one not begun is not known) */
	for (which = 0; which < fetch.count; which++)
	{
		struct torrent_status status;

		if (fetch.files[which].done)
		{
			done += 1.0f;
		}
		else if (fetch.files[which].phase == _fetch_archive)
		{
			unsigned long long received, total;
			char archive[32];

			/* (the zip's share) */
			map_archives_poll(&received, &total, archive, sizeof(archive));
			if (total)
				done += (float)((double)received / (double)total);
		}
		else if (fetch.files[which].phase != _fetch_archive_waiting &&
			torrent_status_get(fetch.files[which].handle, &status) && status.total_bytes)
		{
			done += (float)((double)status.have_bytes / (double)status.total_bytes);
		}
	}
	*fraction = fetch.count ? done / (float)fetch.count : 0.0f;
	/* (the map's name: its file's, the fetch's first, without .map) */
	snprintf(name, (size_t)size, "%.*s", (int)(strlen(fetch.files[0].name) > strlen(MAP_FILE_EXTENSION) ?
		strlen(fetch.files[0].name) - strlen(MAP_FILE_EXTENSION) : strlen(fetch.files[0].name)), fetch.files[0].name);
	return 1;
}

int map_torrents_take_ready(char *level_name, int size)
{
	if (!fetch.ready)
		return 0;
	fetch.ready = 0;
	snprintf(level_name, (size_t)size, "%s", fetch.level_name);
	return 1;
}

void map_torrents_cancel(void)
{
	if (fetch.active)
	{
		platform_log("map torrents: the download of %s's files is cancelled (what came stays, for next time)",
			fetch.level_name);
		fetch_stop();
	}
	fetch.ready = 0;
}

/* ---------- seeding */

static struct
{
	char level_name[NAME_SIZE];
	unsigned long version;
} playing[2];

static struct
{
	int active;
	int handle;
	char level_name[NAME_SIZE];
	char failed_level_name[NAME_SIZE];
	int changed;
	unsigned long checked_time;
} seed;

void map_torrents_playing(const char *level_name, unsigned long version, int hosting)
{
	int slot = hosting ? 0 : 1;

	if (!level_name || !level_name[0])
	{
		if (playing[slot].level_name[0])
			seed.changed = 1;
		playing[slot].level_name[0] = 0;
		playing[slot].version = 0;
		return;
	}
	if (same_name(playing[slot].level_name, level_name) && playing[slot].version == version)
		return;
	snprintf(playing[slot].level_name, sizeof(playing[slot].level_name), "%s", level_name);
	playing[slot].version = version;
	seed.changed = 1;
	/* (another level: one that failed before is tried again) */
	seed.failed_level_name[0] = 0;
}

/* the map to seed now: the hosted game's, or the joined one's when
maps.seed is "all"; NULL for none */
static const char *seed_wanted(unsigned long *version)
{
	const char *policy = config_string("maps.seed");
	int all = !strcmp(policy, "all");

	if (!config_boolean("maps.torrents") || !strcmp(policy, "off"))
		return NULL;
	if (playing[0].level_name[0])
	{
		*version = playing[0].version;
		return playing[0].level_name;
	}
	if (all && playing[1].level_name[0])
	{
		*version = playing[1].version;
		return playing[1].level_name;
	}
	return NULL;
}

static void seed_stop(void)
{
	if (seed.active)
	{
		platform_log("map torrents: no longer seeding %s", seed.level_name);
		torrent_remove(seed.handle);
	}
	seed.active = 0;
	seed.level_name[0] = 0;
}

static void seed_poll(void)
{
	unsigned long version = 0;
	const char *wanted;
	unsigned long now = GetTickCount();

	if (seed_failed_forget)
	{
		seed_failed_forget = 0;
		seed.failed_level_name[0] = 0;
		seed.changed = 1;
	}
	if (!seed.changed && now - seed.checked_time < SEED_CHECK_INTERVAL)
		return;
	seed.checked_time = now;
	seed.changed = 0;
	wanted = seed_wanted(&version);
	if (seed.active)
	{
		struct torrent_status status;

		if (!wanted || !same_name(seed.level_name, wanted))
		{
			seed_stop();
		}
		else if (!torrent_status_get(seed.handle, &status) || status.state == _torrent_state_failed)
		{
			platform_log("map torrents: %s is not seeded: %s", seed.level_name, status.detail);
			snprintf(seed.failed_level_name, sizeof(seed.failed_level_name), "%s", seed.level_name);
			seed_stop();
			return;
		}
	}
	if (wanted && !seed.active && !same_name(seed.failed_level_name, wanted))
	{
		char file_name[NAME_SIZE];
		char path[1200];
		char error[128];
		const struct index_entry *entry;
		int handle;

		/* (only a Custom Edition map: the game's own are on every disc) */
		if (strncmp(wanted, LEVEL_NAME_PREFIX, strlen(LEVEL_NAME_PREFIX)) &&
			strncmp(wanted, "custom_maps/", strlen(LEVEL_NAME_PREFIX)))
		{
			return;
		}
		level_file_name(wanted, file_name, sizeof(file_name));
		entry = index_find_file(file_name, version);
		if (!entry)
		{
			platform_log("map torrents: %s (checksum %08lX) is not in the index: not seeded", file_name, version);
			snprintf(seed.failed_level_name, sizeof(seed.failed_level_name), "%s", wanted);
			return;
		}
		/* (the level's file, or the index's name's: a version 27 host's level
		name has a hash after it) */
		if (!map_file_path(file_name, version, path, sizeof(path)) &&
			!map_file_path(torrent_maps_index.names + entry->name, version, path, sizeof(path)))
		{
			snprintf(seed.failed_level_name, sizeof(seed.failed_level_name), "%s", wanted);
			return;
		}
		if (!engine_start())
			return;
		handle = torrent_add(entry->info_hash, torrent_maps_index.names + entry->name, entry->size, entry->piece_length,
			path, 1, error, sizeof(error));
		if (handle < 0)
		{
			platform_log("map torrents: %s is not seeded: %s", file_name, error);
			snprintf(seed.failed_level_name, sizeof(seed.failed_level_name), "%s", wanted);
			return;
		}
		seed.active = 1;
		seed.handle = handle;
		snprintf(seed.level_name, sizeof(seed.level_name), "%s", wanted);
		platform_log("map torrents: seeding %s from %s", file_name, path);
	}
}

/* ---------- the game's */

void map_torrents_initialize(void)
{
	memset(&fetch, 0, sizeof(fetch));
	memset(&seed, 0, sizeof(seed));
	memset(playing, 0, sizeof(playing));
	/* (the game ends with exit(): the client's thread stopped then, its
	torrents' files left as they are) */
	atexit(map_torrents_dispose);
}

void map_torrents_poll(void)
{
	static int initialized;

	/* (the first frame's: main.c's loop calls this every frame, a window or
	not, debug.null_renderer's too) */
	if (!initialized)
	{
		initialized = 1;
		map_torrents_initialize();
	}
	fetch_poll();
	seed_poll();
}

void map_torrents_dispose(void)
{
	map_torrents_cancel();
	seed_stop();
	if (engine.started)
	{
		torrent_stop();
		engine.started = 0;
	}
}
