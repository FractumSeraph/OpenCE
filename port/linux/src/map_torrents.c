/*
MAP_TORRENTS.C

Custom Edition maps over BitTorrent (map_torrents.h), with the client of
torrent.h.

A machine joining a game on a Custom Edition map it lacks (or has another
version of) asks here for it (ui_widget_port_join_map_fetch in
menu_functions.c, and cache_files_map_present: ChupathingyCE's map
families, halo_map_families.h, find what is there), with Custom Edition's
bitmaps.map, sounds.map and loc.map when they are missing. Each is looked
up in the index of the maps' torrents (maps.torrent_index: the file name
and, for the map, the header checksum the host sent, which is the game
record's map version), and downloaded into maps_ce\downloads; a file that
arrives whole is moved into maps_ce (beside one of its name there, as
<name>_<hash>.map, OpenCE's map sharing's name: a map is never renamed or
replaced, map_families.c finds the version a game plays) and marked
downloaded (map_downloaded_remember: its scripts held to
the tighter rules), and once every file is there the menus try the join
again (map_torrents_take_ready). The menus show how it goes
(map_torrents_fetching). A file that BitTorrent does not give (its download
failed, or brought nothing for ARCHIVE_FALLBACK_MILLISECONDS: no peer has
it, and no web seed answers) is taken instead out of the community
archives' zip that the index names for it, HaloNet's or HaloMaps's
(map_archives.c, maps.archives), checked against the same info hash.

The map a machine's network game is on is seeded meanwhile, from where the
families find it, when maps.seed
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
#include "map_archives.h"
#include "map_list_index.h"
#include "torrent.h"
#ifdef HALO_GAME_BROWSER
#include "browser_http.h"
#endif

/* halo_map_families.h's (the game's boolean is a byte) */
enum { MAP_FAMILY_XBOX, MAP_FAMILY_CUSTOM_EDITION };
short map_family_parse(char const *map, char *file, long size);
unsigned char map_family_find(short family, char const *file, char *path, long size);
unsigned char map_family_resource(char const *name, char *path, long size);
void map_downloaded_remember(char const *map_name);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifndef _WIN32
#include <fcntl.h>
#endif
#ifdef HALO_GAME_BROWSER
#include <pthread.h>
#endif

#ifdef _WIN32
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif

#define INDEX_FORMAT MAP_LIST_INDEX_FORMAT
/* the fetched list as cached (map_list_index.h), in the save root */
#define INDEX_CACHE_NAME "map_torrents.signed"
/* (ChupathingyCE's folder of Custom Edition maps: halo_map_families.h) */
#define CUSTOM_MAPS_XBOX_PATH "d:\\maps_ce\\"
#define DOWNLOADS_FOLDER "downloads"
#define MAP_FILE_EXTENSION ".map"
#define FETCH_MAXIMUM_FILES 4
#define FAILURE_SHOWN_MILLISECONDS 15000
#define STATUS_INTERVAL 500
#define SEED_CHECK_INTERVAL 5000
#define NAME_SIZE 128
/* how long a torrent may bring nothing before the archives are asked */
#define ARCHIVE_FALLBACK_MILLISECONDS 30000

/* ---------- the index */

struct index_entry
{
	/* into torrent_maps_index.names */
	unsigned long name;
	unsigned long checksum;
	unsigned long long size;
	unsigned long piece_length;
	char info_hash[41];
	/* into torrent_maps_index.names: the archives' zips that hold it, as the
	index's columns after the info hash (tab-separated "halonet:<zip>",
	"halomaps:<file id>"; map_archives.h), "" for none; the file's
	BLAKE2b-256 column (blake2b:<hash>) is kept with them, no archive's */
	unsigned long sources;
	/* port: the map sharing hash (the share:<hash> column, which is
	map_share_hash.c's) and the whole-file BLAKE2b's first 32 bits, when the
	index has them: the approved map list (map_torrents_approved) */
	unsigned char share[32];
	int has_share;
	unsigned int blake2b_prefix;
	int has_blake2b;
};

struct torrent_index
{
	int loaded;
	struct index_entry *entries;
	int count;
	char *names;
	unsigned long names_size;
	/* where it was read from, for the log: the file, or the fetched list's
	source */
	char path[1024];
	/* port (this fork): its serial (map_list_index.h; 0 for none) */
	unsigned long long serial;
};

static struct torrent_index torrent_maps_index;

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

/* a column's 64 hex digits after its key ("share:", "blake2b:") in the
sources, as 32 bytes: 1 if there */
static int column_hash(const char *sources, const char *key, unsigned char *hash)
{
	size_t key_length = strlen(key);
	const char *at = sources;

	while (at && *at)
	{
		if (!strncmp(at, key, key_length))
		{
			const char *digits = at + key_length;
			int index;

			for (index = 0; index < 64; index++)
			{
				char c = digits[index];

				if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
					return 0;
			}
			for (index = 0; index < 32; index++)
			{
				char pair[3] = { digits[index * 2], digits[index * 2 + 1], 0 };

				hash[index] = (unsigned char)strtoul(pair, NULL, 16);
			}
			return 1;
		}
		at = strchr(at, '\t');
		if (at)
			at++;
	}
	return 0;
}

/* an index's text (file_size bytes, NUL terminated: its lines are cut up
in place) into index, which was empty: each line a map's file name, header
checksum, size, piece length and info hash, tab-separated
(tools/map_torrents.py); index->path names it in the log. 1 if it is an
index */
static int index_parse(struct torrent_index *index, char *file, size_t file_size)
{
	char *line;
	char *next;
	int capacity = 0;
	int number = 0;

	if (!map_list_index_format(file, file_size))
	{
		platform_log("map torrents: %s is not a map torrent index (its first line is not \"%s\")", index->path,
			INDEX_FORMAT);
		return 0;
	}
	index->serial = map_list_index_serial(file, file_size);
	index->names = malloc(file_size + 1);
	if (!index->names)
		return 0;
	for (line = file; line && *line; line = next)
	{
		char *fields[6];
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
		fields[5] = NULL;
		for (field = 1; field < 5; field++)
		{
			char *tab = strchr(fields[field - 1], '\t');

			if (!tab)
				break;
			*tab = 0;
			fields[field] = tab + 1;
		}
		/* (the archives' zips after the info hash, kept as they are) */
		if (field == 5 && strchr(fields[4], '\t'))
		{
			*strchr(fields[4], '\t') = 0;
			fields[5] = fields[4] + 41;
		}
		if (field < 5 || strlen(fields[4]) != 40)
		{
			platform_log("map torrents: %s line %d is not an entry (left out)", index->path, number);
			continue;
		}
		if (index->count == capacity)
		{
			struct index_entry *grown;

			capacity = capacity ? capacity * 2 : 1024;
			grown = realloc(index->entries, (size_t)capacity * sizeof(*grown));
			if (!grown)
				break;
			index->entries = grown;
		}
		entry = &index->entries[index->count];
		name_length = strlen(fields[0]);
		entry->name = index->names_size;
		memcpy(index->names + index->names_size, fields[0], name_length + 1);
		index->names_size += (unsigned long)name_length + 1;
		entry->checksum = strtoul(fields[1], NULL, 16);
		entry->size = strtoull(fields[2], NULL, 10);
		entry->piece_length = strtoul(fields[3], NULL, 10);
		memcpy(entry->info_hash, fields[4], 41);
		entry->sources = index->names_size;
		if (fields[5])
		{
			size_t sources_length = strlen(fields[5]);

			memcpy(index->names + index->names_size, fields[5], sources_length + 1);
			index->names_size += (unsigned long)sources_length + 1;
		}
		else
		{
			index->names[index->names_size++] = 0;
		}
		{
			unsigned char whole[32];

			entry->has_share = column_hash(index->names + entry->sources, "share:", entry->share);
			entry->has_blake2b = column_hash(index->names + entry->sources, "blake2b:", whole);
			entry->blake2b_prefix = entry->has_blake2b ? (unsigned int)whole[0] << 24 | (unsigned int)whole[1] << 16 |
				(unsigned int)whole[2] << 8 | (unsigned int)whole[3] : 0;
		}
		if (!name_length || !entry->size || !entry->piece_length)
		{
			platform_log("map torrents: %s line %d has a bad size or piece length (left out)",
				index->path, number);
			continue;
		}
		index->count++;
	}
	return 1;
}

static void index_free(struct torrent_index *index)
{
	free(index->entries);
	free(index->names);
	index->entries = NULL;
	index->names = NULL;
	index->count = 0;
	index->names_size = 0;
}

static void index_cache_path(char *path, size_t size)
{
	snprintf(path, size, "%s/%s", platform_save_root(), INDEX_CACHE_NAME);
}

static void index_known(unsigned long long serial, const unsigned char *signature);

/* port (this fork): a signed list (map_list_index.h: the signature's digits, a
line feed, the index), fetched or cached, in place of the one in use when
its serial is newer; checked first when check (the cache's: a fetched one
was checked as it came). 1 when it is the one in use now */
static int index_take_signed(char *text, size_t size, const char *source, int check)
{
	unsigned char signature[MAP_LIST_INDEX_SIGNATURE_SIZE];
	size_t offset;
	struct torrent_index taken;
	unsigned long long serial;
	char *document;
	size_t document_size;

	if (!map_list_index_split(text, size, signature, &offset))
	{
		platform_log("map torrents: the approved map list from %s is not a signed list: not taken", source);
		return 0;
	}
	document = text + offset;
	document_size = size - offset;
	if (check && (!map_list_index_format(document, document_size) ||
		!map_list_index_signature_check(signature, document, document_size)))
	{
		platform_log("map torrents: the approved map list from %s is not signed by the fork's key: not taken", source);
		return 0;
	}
	serial = map_list_index_serial(document, document_size);
	if (serial <= torrent_maps_index.serial)
	{
		/* (the list in use as signed: its signature known, so that the
		published one, the same, is not downloaded again) */
		if (serial == torrent_maps_index.serial)
			index_known(serial, signature);
		else
			platform_log("map torrents: the approved map list from %s (serial %llu) is older than the one in use "
				"(serial %llu): not taken", source, serial, torrent_maps_index.serial);
		return 0;
	}
	memset(&taken, 0, sizeof(taken));
	snprintf(taken.path, sizeof(taken.path), "%s", source);
	/* (the text NUL terminated for the parse: the caller's has room) */
	document[document_size] = 0;
	if (!index_parse(&taken, document, document_size) || !taken.count)
	{
		index_free(&taken);
		return 0;
	}
	platform_log("map torrents: the approved map list from %s is in use: serial %llu, %d maps (was serial %llu, "
		"%d maps)", source, taken.serial, taken.count, torrent_maps_index.serial, torrent_maps_index.count);
	index_free(&torrent_maps_index);
	taken.loaded = 1;
	torrent_maps_index = taken;
	index_known(taken.serial, signature);
	return 1;
}

/* reads the index once: the build's (maps.torrent_index), then the fetched
list cached, if newer */
static void index_load(void)
{
	char *file;
	size_t file_size = 0;
	char path[1024];

	if (torrent_maps_index.loaded)
		return;
	torrent_maps_index.loaded = 1;
	index_path(torrent_maps_index.path, sizeof(torrent_maps_index.path));
	file = config_file_read(torrent_maps_index.path, &file_size);
	if (!file)
	{
		platform_log("map torrents: the index %s cannot be read (maps.torrent_index)", torrent_maps_index.path);
	}
	else
	{
		if (!index_parse(&torrent_maps_index, file, file_size))
			index_free(&torrent_maps_index);
		free(file);
		platform_log("map torrents: %d maps indexed in %s (serial %llu)", torrent_maps_index.count,
			torrent_maps_index.path, torrent_maps_index.serial);
	}
	index_known(torrent_maps_index.serial, NULL);
	index_cache_path(path, sizeof(path));
	file = config_file_read(path, &file_size);
	if (file)
	{
		index_take_signed(file, file_size, "the cache", 1);
		free(file);
	}
}

/* ---------- the approved map list, fetched (port, this fork) */

/* maps.index_url's lists (map_list_index.h), the newest serial taken: looked at
on a thread of this file's when the game starts and every
INDEX_REFRESH_SECONDS, sooner (INDEX_ASKED_SECONDS at the least) when a map
is turned away as not approved; a list taken is cached (INDEX_CACHE_NAME)
and handed to the game's thread, which puts it in place of the index in
use (index_take_fetched) when no download is under way. A source's
<url>.sig is asked for first: a signature already known (the list in use,
or one seen and found no newer) needs no download. */

#ifdef HALO_GAME_BROWSER

#ifndef INDEX_REFRESH_SECONDS
#define INDEX_REFRESH_SECONDS (60 * 60)
#endif
#ifndef INDEX_RETRY_SECONDS
#define INDEX_RETRY_SECONDS (15 * 60)
#endif
#ifndef INDEX_ASKED_SECONDS
#define INDEX_ASKED_SECONDS (2 * 60)
#endif

static pthread_mutex_t index_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t index_wake = PTHREAD_COND_INITIALIZER;
static struct
{
	/* the newest serial known (in use, or taken and on its way to the
	game's thread), and the last signature seen */
	unsigned long long serial;
	unsigned char signature[MAP_LIST_INDEX_SIGNATURE_SIZE];
	int has_signature;
	/* a signed list for the game's thread (malloc'd, NUL terminated), and
	its source */
	char *taken;
	size_t taken_size;
	char taken_source[160];
	/* a look asked for, and when the last one was */
	int asked;
	time_t last;
	char urls[1024];
	int started;
} index_fetch;

static void index_known(unsigned long long serial, const unsigned char *signature)
{
	pthread_mutex_lock(&index_lock);
	if (serial > index_fetch.serial)
		index_fetch.serial = serial;
	if (signature)
	{
		memcpy(index_fetch.signature, signature, sizeof(index_fetch.signature));
		index_fetch.has_signature = 1;
	}
	pthread_mutex_unlock(&index_lock);
}

/* the signed list written to the cache: a new file beside it, then moved
over it */
static void index_cache_write(const char *text, size_t size)
{
	char path[1024], temporary[1100];
	FILE *file;
	int ok;

	index_cache_path(path, sizeof(path));
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	file = fopen(temporary, "wb");
	if (!file)
		return;
	ok = fwrite(text, 1, size, file) == size;
	ok = fclose(file) == 0 && ok;
	/* (Windows does not rename over a file) */
	if (ok && rename(temporary, path) != 0)
	{
		remove(path);
		ok = rename(temporary, path) == 0;
	}
	if (!ok)
	{
		remove(temporary);
		platform_log("map torrents: the approved map list could not be cached");
	}
}

const char *build_identity_user_agent(void);

/* the document at url, at most MAP_LIST_INDEX_MAXIMUM_SIZE bytes, after room
for the signature's line (offset bytes) before it, NUL terminated: malloc'd,
or NULL and why in error */
static char *index_download(const char *url, size_t offset, size_t *size, char *error, int error_size)
{
	int status = 0, length = -1, download, count;
	size_t used = 0, capacity;
	char *buffer, *grown;

	download = posix_browser_download_open(url, build_identity_user_agent(), &status, &length, error, error_size);
	if (!download)
	{
		if (status)
			snprintf(error, (size_t)error_size, "HTTP %d", status);
		return NULL;
	}
	if (length > 0 && (unsigned long)length > MAP_LIST_INDEX_MAXIMUM_SIZE)
	{
		posix_browser_download_close(download);
		snprintf(error, (size_t)error_size, "it is %d bytes, more than a list may be", length);
		return NULL;
	}
	capacity = offset + (length > 0 ? (size_t)length : 2 * 1024 * 1024) + 1;
	buffer = malloc(capacity);
	while (buffer)
	{
		if (capacity - offset - used - 1 < 65536)
		{
			if (used >= MAP_LIST_INDEX_MAXIMUM_SIZE)
			{
				snprintf(error, (size_t)error_size, "it is more than a list may be");
				break;
			}
			grown = realloc(buffer, capacity * 2);
			if (!grown)
			{
				snprintf(error, (size_t)error_size, "out of memory");
				break;
			}
			buffer = grown;
			capacity *= 2;
		}
		count = posix_browser_download_read(download, buffer + offset + used, 65536);
		if (count < 0)
		{
			snprintf(error, (size_t)error_size, "the download broke off");
			break;
		}
		if (!count)
		{
			posix_browser_download_close(download);
			buffer[offset + used] = 0;
			*size = used;
			return buffer;
		}
		used += (size_t)count;
	}
	if (!buffer)
		snprintf(error, (size_t)error_size, "out of memory");
	posix_browser_download_close(download);
	free(buffer);
	return NULL;
}

/* one source's list: 1 when it answered (its list known, taken if newer),
0 when not; *absent when it has none (HTTP 404) */
static int index_fetch_from(const char *url, int *absent)
{
	static const char digits[] = "0123456789abcdef";
	char signature_url[1100], answer[300], error[256];
	unsigned char signature[MAP_LIST_INDEX_SIGNATURE_SIZE];
	size_t line = MAP_LIST_INDEX_SIGNATURE_SIZE * 2 + 1, size = 0, index;
	unsigned long long serial;
	char *text;
	int status, known, newer, same;

	*absent = 0;
	snprintf(signature_url, sizeof(signature_url), "%s.sig", url);
	status = posix_browser_request_as(signature_url, NULL, NULL, build_identity_user_agent(), answer,
		(int)sizeof(answer), error, (int)sizeof(error));
	if (status == 404)
	{
		*absent = 1;
		return 0;
	}
	if (status != 200)
	{
		platform_log("map torrents: no approved map list from %s (%s)", url, status ? "the server refused" : error);
		return 0;
	}
	if (!map_list_index_signature_parse(answer, strlen(answer), signature))
	{
		platform_log("map torrents: %s is not a signature: the approved map list there is not taken", signature_url);
		return 0;
	}
	pthread_mutex_lock(&index_lock);
	known = index_fetch.has_signature && !memcmp(index_fetch.signature, signature, sizeof(signature));
	pthread_mutex_unlock(&index_lock);
	if (known)
		return 1;
	text = index_download(url, line, &size, error, (int)sizeof(error));
	if (!text)
	{
		platform_log("map torrents: no approved map list from %s (%s)", url, error);
		return 0;
	}
	if (!map_list_index_format(text + line, size) || !map_list_index_signature_check(signature, text + line, size))
	{
		platform_log("map torrents: the approved map list from %s is not signed by the fork's key: not taken", url);
		free(text);
		return 0;
	}
	serial = map_list_index_serial(text + line, size);
	/* (the cache's form: the signature's digits and a line feed first) */
	for (index = 0; index < MAP_LIST_INDEX_SIGNATURE_SIZE; index++)
	{
		text[index * 2] = digits[signature[index] >> 4];
		text[index * 2 + 1] = digits[signature[index] & 15];
	}
	text[line - 1] = '\n';
	pthread_mutex_lock(&index_lock);
	newer = serial > index_fetch.serial;
	same = serial == index_fetch.serial;
	memcpy(index_fetch.signature, signature, sizeof(signature));
	index_fetch.has_signature = 1;
	pthread_mutex_unlock(&index_lock);
	if (!newer)
	{
		/* (the one in use, as signed: cached, so that the next start knows
		its signature) */
		if (same)
			index_cache_write(text, line + size);
		else
			platform_log("map torrents: the approved map list from %s (serial %llu) is older than this machine's",
				url, serial);
		free(text);
		return 1;
	}
	index_cache_write(text, line + size);
	pthread_mutex_lock(&index_lock);
	index_fetch.serial = serial;
	free(index_fetch.taken);
	index_fetch.taken = text;
	index_fetch.taken_size = line + size;
	snprintf(index_fetch.taken_source, sizeof(index_fetch.taken_source), "%s", url);
	pthread_mutex_unlock(&index_lock);
	platform_log("map torrents: a newer approved map list from %s (serial %llu)", url, serial);
	return 1;
}

/* the sources in maps.index_url's order, until one answers: 1 if one did */
static int index_refresh(void)
{
	/* (none published anywhere: said once a run) */
	static int absence_said;
	char *url, *comma;
	int absent, any_absent = 0, answered = 0;

	for (url = index_fetch.urls; url && *url && !answered; url = comma)
	{
		comma = strchr(url, ',');
		if (comma)
			*comma = 0;
		while (*url == ' ')
			url++;
		if (*url)
		{
			answered = index_fetch_from(url, &absent);
			any_absent |= absent;
		}
		if (comma)
			*comma++ = ',';
	}
	if (!answered && any_absent && !absence_said)
	{
		platform_log("map torrents: no approved map list is published at %s: the built-in one is used",
			index_fetch.urls);
		absence_said = 1;
	}
	return answered;
}

static void *index_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		int answered = index_refresh();
		time_t next;

		pthread_mutex_lock(&index_lock);
		index_fetch.last = time(NULL);
		index_fetch.asked = 0;
		next = index_fetch.last + (answered ? INDEX_REFRESH_SECONDS : INDEX_RETRY_SECONDS);
		for (;;)
		{
			struct timespec deadline;
			time_t now = time(NULL), until = next;

			if (index_fetch.asked)
			{
				time_t soonest = index_fetch.last + INDEX_ASKED_SECONDS;

				if (soonest <= now)
					break;
				if (soonest < until)
					until = soonest;
			}
			if (until <= now)
				break;
			deadline.tv_sec = until;
			deadline.tv_nsec = 0;
			pthread_cond_timedwait(&index_wake, &index_lock, &deadline);
		}
		pthread_mutex_unlock(&index_lock);
	}
	return NULL;
}

static void index_fetch_start(void)
{
	pthread_t thread;
	const char *urls = config_string("maps.index_url");

	if (index_fetch.started || !urls[0])
		return;
	index_fetch.started = 1;
	snprintf(index_fetch.urls, sizeof(index_fetch.urls), "%s", urls);
	if (pthread_create(&thread, NULL, index_thread, NULL) == 0)
		pthread_detach(thread);
	else
		platform_log("map torrents: could not start the approved map list's thread");
}

/* a map turned away as not approved: the sources looked at again soon, in
case it was approved since */
static void index_fetch_ask(void)
{
	if (!index_fetch.started)
		return;
	pthread_mutex_lock(&index_lock);
	index_fetch.asked = 1;
	pthread_cond_signal(&index_wake);
	pthread_mutex_unlock(&index_lock);
}

/* (the game's thread) a list the thread took, in place of the one in use;
not while a download holds the index's entries */
static void index_take_fetched(int downloading)
{
	char *text;
	size_t size;
	char source[160];

	if (downloading || !index_fetch.started)
		return;
	pthread_mutex_lock(&index_lock);
	text = index_fetch.taken;
	size = index_fetch.taken_size;
	snprintf(source, sizeof(source), "%s", index_fetch.taken_source);
	index_fetch.taken = NULL;
	pthread_mutex_unlock(&index_lock);
	if (!text)
		return;
	index_load();
	index_take_signed(text, size, source, 0);
	free(text);
}

#else

static void index_known(unsigned long long serial, const unsigned char *signature)
{
	(void)serial;
	(void)signature;
}

static void index_fetch_start(void)
{
}

static void index_fetch_ask(void)
{
}

static void index_take_fetched(int downloading)
{
	(void)downloading;
}

#endif

/* the entry of the file named, of the checksum (0: any) */
static const struct index_entry *index_find(const char *file_name, unsigned long checksum)
{
	int which;

	index_load();
	for (which = 0; which < torrent_maps_index.count; which++)
	{
		const struct index_entry *entry = &torrent_maps_index.entries[which];

		if (same_name(torrent_maps_index.names + entry->name, file_name) && (!checksum || entry->checksum == checksum ||
			!entry->checksum))
		{
			return entry;
		}
	}
	return NULL;
}

/* ---------- the approved map list (port) */

/* the name's own file name: its folders gone, ".map" ended with */
static void approval_file_name(const char *name, char *file, int size)
{
	const char *base = name ? name : "";
	const char *slash;

	while ((slash = strpbrk(base, "\\/")) != NULL)
		base = slash + 1;
	snprintf(file, (size_t)size, "%s", base);
	if (strlen(file) < 4 || !same_name(file + strlen(file) - 4, MAP_FILE_EXTENSION))
		snprintf(file + strlen(file), (size_t)size - strlen(file), "%s", MAP_FILE_EXTENSION);
}

int map_torrents_approved_hash(const char *file_name, const unsigned char *hash)
{
	char file[NAME_SIZE];
	int which;

	index_load();
	if (file_name)
	{
		approval_file_name(file_name, file, sizeof(file));
		for (which = 0; which < torrent_maps_index.count; which++)
		{
			const struct index_entry *entry = &torrent_maps_index.entries[which];

			if (!entry->checksum && same_name(torrent_maps_index.names + entry->name, file))
				return 1;
		}
	}
	for (which = 0; hash && which < torrent_maps_index.count; which++)
	{
		const struct index_entry *entry = &torrent_maps_index.entries[which];

		if (entry->has_share && !memcmp(entry->share, hash, sizeof(entry->share)))
			return 1;
	}
	return 0;
}

int map_torrents_approved(const char *file_name, unsigned int version)
{
	char file[NAME_SIZE];
	int which;

	index_load();
	approval_file_name(file_name, file, sizeof(file));
	for (which = 0; which < torrent_maps_index.count; which++)
	{
		const struct index_entry *entry = &torrent_maps_index.entries[which];
		unsigned int share_prefix = entry->has_share ? (unsigned int)entry->share[0] << 24 |
			(unsigned int)entry->share[1] << 16 | (unsigned int)entry->share[2] << 8 | (unsigned int)entry->share[3] : 0;

		if (!same_name(torrent_maps_index.names + entry->name, file))
			continue;
		if (!version || !entry->checksum || entry->checksum == version || (entry->has_share && share_prefix == version) ||
			(entry->has_blake2b && entry->blake2b_prefix == version))
		{
			return 1;
		}
	}
	return 0;
}

void map_torrents_unapproved_text(char *text, int size, const char *file_name, const unsigned char *hash,
	unsigned int version)
{
	char file[NAME_SIZE];
	char digits[65];
	int index;

	approval_file_name(file_name, file, sizeof(file));
	if (hash)
	{
		for (index = 0; index < 32; index++)
			snprintf(digits + index * 2, 3, "%02x", hash[index]);
	}
	else
	{
		snprintf(digits, sizeof(digits), "%08x (its first digits)", version);
	}
	/* (why, what and whom to ask, in the five lines of about 30 characters
	the game's error dialog shows (ui_widget.c): the hash's first 8 digits,
	the whole hash in the log) */
	snprintf(text, (size_t)size, "To prevent malware, only pre-approved maps are shared over p2p. Ask "
		MAP_APPROVAL_CONTACT " to approve %s (%.8s).", file, digits);
	platform_log("map approval: %s (%s) is not in the maps' torrent index, the approved map list: not downloaded",
		file, digits);
	index_fetch_ask();
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

/* the maps_ce folder (made if missing), with its separator */
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

/* the map's file name from its name: <file>@ce (ChupathingyCE's), a level's
path (custom_maps\<file>, OpenCE's protocol) or the file's name alone is
<file>.map; "" for a map of another family (the Xbox's, HaloMD's) */
static void level_file_name(const char *level_name, char *file_name, int size)
{
	char file[NAME_SIZE];
	short family = map_family_parse(level_name, file, sizeof(file));

	if (family == MAP_FAMILY_XBOX && !strpbrk(level_name, "\\/"))
		family = MAP_FAMILY_CUSTOM_EDITION;
	if (family == MAP_FAMILY_XBOX && !strncmp(level_name, "custom_maps", 11))
		family = MAP_FAMILY_CUSTOM_EDITION;
	if (family != MAP_FAMILY_CUSTOM_EDITION || !file[0])
	{
		file_name[0] = 0;
		return;
	}
	snprintf(file_name, (size_t)size, "%s" MAP_FILE_EXTENSION, file);
}

/* where the file named (<file>.map, or a resource map) is, where the map
families find it (maps_ce, OpenCE's custom_maps, maps\ce...): the host's
path; 0 if it is nowhere */
/* a Custom Edition map file's header checksum (at 0x64, as the index holds
it); 0 if it cannot be read */
static unsigned long file_header_checksum(const char *path)
{
	unsigned char header[0x68];
	FILE *file = fopen(path, "rb");
	size_t read;

	if (!file)
		return 0;
	read = fread(header, 1, sizeof(header), file);
	fclose(file);
	if (read != sizeof(header))
		return 0;
	return (unsigned long)header[0x64] | (unsigned long)header[0x65] << 8 | (unsigned long)header[0x66] << 16 |
		(unsigned long)header[0x67] << 24;
}

static int map_file_path(const char *file_name, char *path, int size)
{
	char xbox_path[256];
	char file[NAME_SIZE];
	size_t length = strlen(file_name);
	int found;

	if (length <= strlen(MAP_FILE_EXTENSION) || length >= sizeof(file))
		return 0;
	if (same_name(file_name, "bitmaps.map") || same_name(file_name, "sounds.map") || same_name(file_name, "loc.map"))
	{
		snprintf(file, sizeof(file), "%.*s", (int)(length - strlen(MAP_FILE_EXTENSION)), file_name);
		found = map_family_resource(file, xbox_path, sizeof(xbox_path));
	}
	else
	{
		snprintf(file, sizeof(file), "%.*s", (int)(length - strlen(MAP_FILE_EXTENSION)), file_name);
		found = map_family_find(MAP_FAMILY_CUSTOM_EDITION, file, xbox_path, sizeof(xbox_path));
	}
	if (!found)
		return 0;
	platform_translate_path(xbox_path, path, (unsigned long)size);
	return file_present(path);
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
		const struct index_entry *entry;
		/* where it is downloaded to (maps_ce\downloads), the bytes the
		torrent had when they last grew, and when; and whether it is the
		archives' to fetch now (the torrent removed: 1 waiting for the
		archives' fetch, 2 under way) */
		char path[1200];
		unsigned long long progress_bytes;
		unsigned long progress_time;
		int archive;
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
		if (fetch.files[which].archive)
		{
			if (fetch.files[which].archive == 2)
				map_archives_stop();
		}
		else
		{
			torrent_remove(fetch.files[which].handle);
		}
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

	char computed[256];

	if (!config_boolean("maps.torrents") || !level_name)
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

			if (!map_file_path(resources[resource], path, sizeof(path)))
				snprintf(computed + used, sizeof(computed) - used, ",%s", resources[resource]);
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
			if (!wanted[count].entry && checksum)
			{
				char path[1024];

				/* A host whose version is not one of the index's (a build
				that tells another checksum, or none of these): the map of
				that name, when there is none here; one here is that host's
				other version, which downloading cannot change. */
				if (map_file_path(wanted[count].name, path, sizeof(path)))
				{
					platform_log("map torrents: %s (checksum %08lX) is not in the index, and %s is here: not downloaded",
						wanted[count].name, checksum, wanted[count].name);
					return 0;
				}
				wanted[count].entry = index_find(wanted[count].name, 0);
				if (wanted[count].entry)
				{
					platform_log("map torrents: %s (checksum %08lX) is not in the index: downloading the index's %s "
						"(checksum %08lX) by its name", wanted[count].name, checksum, wanted[count].name,
						wanted[count].entry->checksum);
				}
			}
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

		snprintf(path, sizeof(path), "%s%s", folder, torrent_maps_index.names + entry->name);
		handle = torrent_add(entry->info_hash, torrent_maps_index.names + entry->name, entry->size, entry->piece_length,
			path, 0, error, sizeof(error));
		if (handle < 0)
		{
			platform_log("map torrents: %s could not be added: %s", wanted[which].name, error);
			fetch_stop();
			return 0;
		}
		snprintf(fetch.files[which].name, sizeof(fetch.files[which].name), "%s", torrent_maps_index.names + entry->name);
		snprintf(fetch.files[which].path, sizeof(fetch.files[which].path), "%s", path);
		fetch.files[which].handle = handle;
		fetch.files[which].entry = entry;
		fetch.files[which].progress_time = GetTickCount();
		fetch.count++;
	}
	snprintf(fetch.level_name, sizeof(fetch.level_name), "%s", level_name);
	fetch.version = version;
	fetch.active = 1;
	fetch.started = GetTickCount();
	platform_log("map torrents: downloading %d file(s) for %s: %s", count, level_name, files);
	return 1;
}

/* a file moved to a name no file has: never in place of one (POSIX's
rename would replace it, so the name is taken first; Windows's fails) */
static int move_to_new_name(const char *from, const char *to)
{
#ifndef _WIN32
	int taken = open(to, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);

	if (taken < 0)
		return 0;
	close(taken);
	if (rename(from, to) != 0)
	{
		unlink(to);
		return 0;
	}
	return 1;
#else
	return rename(from, to) == 0;
#endif
}

static int fetch_file_done(int which);

/* a file the archives are to give: their fetch started when no other is
under way, and the file put in place once it came (map_archives.c checks it
against the index's info hash); 0 when the download failed */
static int archive_poll(int which)
{
	const struct index_entry *entry = fetch.files[which].entry;
	unsigned long long received, total;
	char archive[64];
	enum map_archives_state state;

	if (fetch.files[which].archive == 1)
	{
		/* (one fetch at a time: another file's, or one stopped still ending) */
		if (map_archives_poll(&received, &total, archive, sizeof(archive)) == _map_archives_running)
			return 1;
		map_archives_stop();
		if (!map_archives_start(fetch.files[which].name, entry->size, entry->piece_length, entry->info_hash,
			torrent_maps_index.names + entry->sources, fetch.files[which].path))
		{
			fetch_fail(fetch.files[which].name, "nothing came over BitTorrent, and the archives could not be asked");
			return 0;
		}
		fetch.files[which].archive = 2;
		return 1;
	}
	state = map_archives_poll(&received, &total, archive, sizeof(archive));
	if (state == _map_archives_running)
		return 1;
	map_archives_stop();
	if (state != _map_archives_done)
	{
		fetch_fail(fetch.files[which].name, "neither BitTorrent nor the map archives gave it (the log says why)");
		return 0;
	}
	if (!fetch_file_done(which))
	{
		fetch_fail(fetch.files[which].name, "it could not be put in place");
		return 0;
	}
	return 1;
}

/* a file that arrived: into maps_ce; beside one of its name there (another
version), as <name>_<hash>.map, the one there never touched */
static int fetch_file_done(int which)
{
	/* (map_share_files.c's) */
	unsigned char map_share_file_digits(const char *path, char *digits);
	char folder[1024];
	char destination[1200];
	const char *name = fetch.files[which].name;
	size_t length = strlen(name);

	if (!custom_maps_folder(folder, sizeof(folder)))
		return 0;
	snprintf(destination, sizeof(destination), "%s%s", folder, name);
	if (file_present(destination))
	{
		char download[1200];
		char digits[16];

		/* (the download's own hash, from where it is: maps_ce\downloads) */
		snprintf(download, sizeof(download), "%s%s\\%s", CUSTOM_MAPS_XBOX_PATH, DOWNLOADS_FOLDER, name);
		if (length <= strlen(MAP_FILE_EXTENSION) || !map_share_file_digits(download, digits))
		{
			platform_log("map torrents: %s is there already, and the download could not be hashed to go beside it",
				destination);
			return 0;
		}
		snprintf(destination, sizeof(destination), "%s%.*s_%s%s", folder, (int)(length - strlen(MAP_FILE_EXTENSION)),
			name, digits, MAP_FILE_EXTENSION);
		/* (that version there already: the download is it) */
		if (file_present(destination))
		{
			if (fetch.files[which].archive)
				unlink(fetch.files[which].path);
			else
				torrent_remove(fetch.files[which].handle);
			fetch.files[which].done = 1;
			platform_log("map torrents: %s is there already", destination);
			return 1;
		}
	}
	if (fetch.files[which].archive ? !move_to_new_name(fetch.files[which].path, destination) :
		!torrent_move(fetch.files[which].handle, destination))
	{
		platform_log("map torrents: %s could not be moved into %s", fetch.files[which].name, folder);
		return 0;
	}
	if (!fetch.files[which].archive)
		torrent_remove(fetch.files[which].handle);
	fetch.files[which].done = 1;
	platform_log("map torrents: %s is in %s", fetch.files[which].name, folder);
	/* (a map, not a resource map: a downloaded one, whose scripts are held to
	the tighter rules: map_downloaded_remember, ChupathingyCE's hs.c, kept in
	maps_ce/downloaded_maps.txt for the next runs too) */
	if (!same_name(fetch.files[which].name, "bitmaps.map") && !same_name(fetch.files[which].name, "sounds.map") &&
		!same_name(fetch.files[which].name, "loc.map"))
	{
		char marked[NAME_SIZE + 4];
		size_t length = strlen(fetch.files[which].name);

		if (length > strlen(MAP_FILE_EXTENSION))
		{
			snprintf(marked, sizeof(marked), "%.*s@ce", (int)(length - strlen(MAP_FILE_EXTENSION)),
				fetch.files[which].name);
			map_downloaded_remember(marked);
		}
	}
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
		if (fetch.files[which].archive)
		{
			if (!archive_poll(which))
				return;
			if (fetch.files[which].done)
				done++;
			continue;
		}
		if (!torrent_status_get(fetch.files[which].handle, &status))
		{
			fetch_fail(fetch.files[which].name, "the client lost it");
			return;
		}
		if (status.have_bytes != fetch.files[which].progress_bytes)
		{
			fetch.files[which].progress_bytes = status.have_bytes;
			fetch.files[which].progress_time = GetTickCount();
		}
		/* (BitTorrent does not give it: the archives instead, if they have it) */
		if ((status.state == _torrent_state_failed ||
			(status.state != _torrent_state_seeding && status.state != _torrent_state_checking &&
			GetTickCount() - fetch.files[which].progress_time >= ARCHIVE_FALLBACK_MILLISECONDS)) &&
			map_archives_available(torrent_maps_index.names + fetch.files[which].entry->sources))
		{
			platform_log("map torrents: %s: %s; from the archives instead", fetch.files[which].name,
				status.state == _torrent_state_failed ? status.detail : "nothing came over BitTorrent");
			torrent_remove(fetch.files[which].handle);
			fetch.files[which].archive = 1;
			continue;
		}
		if (status.state == _torrent_state_failed)
		{
			fetch_fail(fetch.files[which].name, status.detail);
			return;
		}
		/* (nor do the archives, for want of one that has it, or any:
		the download fails, so that the next source is asked, as
		ChupathingyCE's map downloader asks its repositories then,
		map_download.c) */
		if (status.state != _torrent_state_seeding && status.state != _torrent_state_checking &&
			GetTickCount() - fetch.files[which].progress_time >= ARCHIVE_FALLBACK_MILLISECONDS)
		{
			fetch_fail(fetch.files[which].name, "nothing came over BitTorrent, and no map archive has it");
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
		if (fetch.count > 1)
			snprintf(which_text, sizeof(which_text), " (%d of %d)", which + 1, fetch.count);
		if (fetch.files[which].archive)
		{
			unsigned long long received, bytes;
			char archive[64];

			if (fetch.files[which].archive == 2 &&
				map_archives_poll(&received, &bytes, archive, sizeof(archive)) == _map_archives_running && bytes)
			{
				size_text(received, have, sizeof(have));
				size_text(bytes, total, sizeof(total));
				snprintf(fetch.status, sizeof(fetch.status), "Downloading %s%s from %s: %s of %s",
					fetch.files[which].name, which_text, archive, have, total);
			}
			else
			{
				snprintf(fetch.status, sizeof(fetch.status), "Downloading %s%s from the map archives",
					fetch.files[which].name, which_text);
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
		else if (fetch.files[which].archive)
		{
			unsigned long long received, bytes;
			char archive[64];

			if (fetch.files[which].archive == 2 &&
				map_archives_poll(&received, &bytes, archive, sizeof(archive)) == _map_archives_running && bytes)
			{
				done += (float)((double)received / (double)bytes);
			}
		}
		else if (torrent_status_get(fetch.files[which].handle, &status) && status.total_bytes)
		{
			done += (float)((double)status.have_bytes / (double)status.total_bytes);
		}
	}
	*fraction = fetch.count ? done / (float)fetch.count : 0.0f;
	snprintf(name, (size_t)size, "%s", fetch.level_name);
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
		if (!map_file_path(file_name, path, sizeof(path)))
		{
			snprintf(seed.failed_level_name, sizeof(seed.failed_level_name), "%s", wanted);
			return;
		}
		/* (the game's version is the header checksum for a game of network
		version 28 and before, the start of the map's hash for one of 29,
		which the index does not hold: the file's own checksum then) */
		entry = index_find(file_name, version);
		if (!entry)
			entry = index_find(file_name, file_header_checksum(path));
		if (!entry)
		{
			platform_log("map torrents: %s (version %08lX) is not in the index: not seeded", file_name, version);
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
	/* port (this fork): the approved map list read now, and the published
	one looked for (maps.index_url) */
	index_load();
	index_fetch_start();
}

void map_torrents_poll(void)
{
	fetch_poll();
	seed_poll();
	index_take_fetched(fetch.active);
}

void map_torrents_index_poll(void)
{
	index_load();
	index_fetch_start();
	index_take_fetched(fetch.active);
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
