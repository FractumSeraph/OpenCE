/*
MAP_TORRENTS.C

Custom Edition maps over BitTorrent (map_torrents.h), with the client of
torrent.h.

A machine joining a game on a Custom Edition map it lacks (or has another
version of; or without Custom Edition's bitmaps.map, sounds.map and
loc.map) asks here for the files (ui_widget_port_join, through
custom_edition_cache_files_missing). Each is looked up in the index of the
maps' torrents (maps.torrent_index: the file name and, for the map, the
header checksum the host sent, which is the game record's map version),
and downloaded into custom_maps\downloads; a file that arrives whole is
moved into custom_maps (one of its name there becoming <name>.old), and
once every file is there the menus try the join again
(map_torrents_take_ready). The menus show how it goes
(map_torrents_fetching).

The map a machine's network game is on is seeded meanwhile, from where it
is (custom_maps, or the Custom Edition install's maps), when maps.seed
allows: "host" (the default) seeds only while hosting, "all" also while
joined, "off" never. The upload limit (maps.upload_limit) is for the whole
client, however many are downloading: the game's own traffic comes first.

The client is started when first needed, with the port, limits, trackers,
web seeds and DHT of the [maps] settings, and stopped with the game.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
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
#define CUSTOM_MAPS_XBOX_PATH "d:\\custom_maps\\"
#define INSTALL_MAPS_XBOX_PATH "h:\\maps\\"
#define DOWNLOADS_FOLDER "downloads"
#define MAP_FILE_EXTENSION ".map"
#define FETCH_MAXIMUM_FILES 4
#define FAILURE_SHOWN_MILLISECONDS 15000
#define STATUS_INTERVAL 500
#define SEED_CHECK_INTERVAL 5000
#define NAME_SIZE 128

/* ---------- the index */

struct index_entry
{
	/* into index.names */
	unsigned long name;
	unsigned long checksum;
	unsigned long long size;
	unsigned long piece_length;
	char info_hash[41];
};

static struct
{
	int loaded;
	struct index_entry *entries;
	int count;
	char *names;
	unsigned long names_size;
	char path[1024];
} index;

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
size, piece length and info hash, tab-separated (tools/map_torrents.py) */
static void index_load(void)
{
	char *file;
	size_t file_size = 0;
	char *line;
	char *next;
	int capacity = 0;
	int number = 0;

	if (index.loaded)
		return;
	index.loaded = 1;
	index_path(index.path, sizeof(index.path));
	file = config_file_read(index.path, &file_size);
	if (!file)
	{
		platform_log("map torrents: the index %s cannot be read (maps.torrent_index)", index.path);
		return;
	}
	if (strncmp(file, INDEX_FORMAT, strlen(INDEX_FORMAT)))
	{
		platform_log("map torrents: %s is not a map torrent index (its first line is not \"%s\")", index.path,
			INDEX_FORMAT);
		free(file);
		return;
	}
	index.names = malloc(file_size + 1);
	if (!index.names)
	{
		free(file);
		return;
	}
	for (line = file; line && *line; line = next)
	{
		char *fields[5];
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
		if (field < 5 || strchr(fields[4], '\t') || strlen(fields[4]) != 40)
		{
			platform_log("map torrents: %s line %d is not an entry (left out)", index.path, number);
			continue;
		}
		if (index.count == capacity)
		{
			struct index_entry *grown;

			capacity = capacity ? capacity * 2 : 1024;
			grown = realloc(index.entries, (size_t)capacity * sizeof(*grown));
			if (!grown)
				break;
			index.entries = grown;
		}
		entry = &index.entries[index.count];
		name_length = strlen(fields[0]);
		entry->name = index.names_size;
		memcpy(index.names + index.names_size, fields[0], name_length + 1);
		index.names_size += (unsigned long)name_length + 1;
		entry->checksum = strtoul(fields[1], NULL, 16);
		entry->size = strtoull(fields[2], NULL, 10);
		entry->piece_length = strtoul(fields[3], NULL, 10);
		memcpy(entry->info_hash, fields[4], 41);
		if (!name_length || !entry->size || !entry->piece_length)
		{
			platform_log("map torrents: %s line %d has a bad size or piece length (left out)", index.path, number);
			continue;
		}
		index.count++;
	}
	free(file);
	platform_log("map torrents: %d maps indexed in %s", index.count, index.path);
}

/* the entry of the file named, of the checksum (0: any) */
static const struct index_entry *index_find(const char *file_name, unsigned long checksum)
{
	int which;

	index_load();
	for (which = 0; which < index.count; which++)
	{
		const struct index_entry *entry = &index.entries[which];

		if (same_name(index.names + entry->name, file_name) && (!checksum || entry->checksum == checksum ||
			!entry->checksum))
		{
			return entry;
		}
	}
	return NULL;
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
	settings.dht = config_boolean("maps.dht");
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

static int downloads_folder(char *path, int size)
{
	size_t length;

	if (!custom_maps_folder(path, size))
		return 0;
	length = strlen(path);
	snprintf(path + length, (size_t)size - length, DOWNLOADS_FOLDER);
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

static int file_present(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && !(information.flags & _posix_file_is_directory);
}

/* the map's file name from its level name: custom_maps\<name> is
<name>.map */
static void level_file_name(const char *level_name, char *file_name, int size)
{
	const char *name = level_name;
	const char *separator;

	while ((separator = strpbrk(name, "\\/")) != NULL)
		name = separator + 1;
	snprintf(file_name, (size_t)size, "%s" MAP_FILE_EXTENSION, name);
}

/* where the map named is: custom_maps, else the Custom Edition install's
maps; 0 if it is in neither */
static int map_file_path(const char *file_name, char *path, int size)
{
	char xbox_path[256];

	snprintf(xbox_path, sizeof(xbox_path), CUSTOM_MAPS_XBOX_PATH "%s", file_name);
	platform_translate_path(xbox_path, path, (unsigned long)size);
	if (file_present(path))
		return 1;
	if (platform_custom_edition_root()[0])
	{
		snprintf(xbox_path, sizeof(xbox_path), INSTALL_MAPS_XBOX_PATH "%s", file_name);
		platform_translate_path(xbox_path, path, (unsigned long)size);
		if (file_present(path))
			return 1;
	}
	return 0;
}

/* ---------- downloading */

static struct
{
	int active;
	char level_name[NAME_SIZE];
	unsigned long version;
	struct
	{
		char name[NAME_SIZE];
		int handle;
		int done;
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
		if (!fetch.files[which].done)
			torrent_remove(fetch.files[which].handle);
	}
	fetch.active = 0;
	fetch.count = 0;
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
	} wanted[FETCH_MAXIMUM_FILES];
	int which;

	if (!config_boolean("maps.torrents") || !level_name || !files || !files[0])
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
		const char *comma = strchr(text, ',');
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
			wanted[count].entry = index_find(wanted[count].name, checksum);
			if (!wanted[count].entry)
			{
				platform_log("map torrents: %s (checksum %08lX) is not in the index: not downloaded", wanted[count].name,
					checksum);
				return 0;
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
		char path[1200];
		char error[128];
		const struct index_entry *entry = wanted[which].entry;
		int handle;

		snprintf(path, sizeof(path), "%s%s", folder, index.names + entry->name);
		handle = torrent_add(entry->info_hash, index.names + entry->name, entry->size, entry->piece_length, path, 0,
			error, sizeof(error));
		if (handle < 0)
		{
			platform_log("map torrents: %s could not be added: %s", wanted[which].name, error);
			fetch_stop();
			return 0;
		}
		snprintf(fetch.files[which].name, sizeof(fetch.files[which].name), "%s", index.names + entry->name);
		fetch.files[which].handle = handle;
		fetch.count++;
	}
	snprintf(fetch.level_name, sizeof(fetch.level_name), "%s", level_name);
	fetch.version = version;
	fetch.active = 1;
	fetch.started = GetTickCount();
	platform_log("map torrents: downloading %d file(s) for %s: %s", count, level_name, files);
	return 1;
}

/* a file that arrived: into custom_maps, the one of its name there kept
as <name>.old */
static int fetch_file_done(int which)
{
	char folder[1024];
	char destination[1200];
	char old[1200];

	if (!custom_maps_folder(folder, sizeof(folder)))
		return 0;
	snprintf(destination, sizeof(destination), "%s%s", folder, fetch.files[which].name);
	if (file_present(destination))
	{
		snprintf(old, sizeof(old), "%s.old", destination);
		unlink(old);
		if (rename(destination, old) != 0)
		{
			platform_log("map torrents: %s could not be renamed to %s", destination, old);
			return 0;
		}
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
		if (!torrent_status_get(fetch.files[which].handle, &status))
		{
			fetch_fail(fetch.files[which].name, "the client lost it");
			return;
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
		if (!torrent_status_get(fetch.files[which].handle, &status))
			continue;
		if (fetch.count > 1)
			snprintf(which_text, sizeof(which_text), " (%d of %d)", which + 1, fetch.count);
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
		if (strncmp(wanted, "custom_maps\\", 12) && strncmp(wanted, "custom_maps/", 12))
			return;
		level_file_name(wanted, file_name, sizeof(file_name));
		entry = index_find(file_name, version);
		if (!entry)
		{
			platform_log("map torrents: %s (checksum %08lX) is not in the index: not seeded", file_name, version);
			snprintf(seed.failed_level_name, sizeof(seed.failed_level_name), "%s", wanted);
			return;
		}
		if (!map_file_path(file_name, path, sizeof(path)))
		{
			snprintf(seed.failed_level_name, sizeof(seed.failed_level_name), "%s", wanted);
			return;
		}
		if (!engine_start())
			return;
		handle = torrent_add(entry->info_hash, index.names + entry->name, entry->size, entry->piece_length, path, 1,
			error, sizeof(error));
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
