/*
MAP_SHARE_FILES.C

The files map sharing hashes (map_share.h): a map's name made from its
hash, and the hashes of the files this machine shares, worked out on a
thread of their own a file at a time, so that a map of hundreds of
megabytes costs the game no frames.

A file's hash is BLAKE2b's (32 bytes) of a label, its size and its pieces'
hashes (each piece MAP_SHARE_PIECE_BYTES of it, the last shorter, hashed with
BLAKE2b): a client that knows a file's hash, from the host, can check the
pieces' hashes any player sends it, and each piece as it arrives.
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "tag_files/tag_files.h"
#include "bungie_net/common/thread.h"
#include "map_share_maps.h" /* port (this fork): its map families' */
#include "halo_map_families.h"
#include "map_share.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xtl.h>

/* (the C library's allocator, not the game's tracked one (cseries.h): the
hashing thread allocates what the game's thread frees) */
#undef malloc
#undef free
#undef realloc

/* (p2p_crypto.c's) */
void p2p_blake2b_256(const void *data, unsigned long size, unsigned char *digest);
/* (port_config.c's) */
const char *config_string(const char *name);

/* ---------- constants */

enum
{
	/* the files hashed for sharing at once: a game's map and its three
	resource maps, a download's, and the last few games' */
	MAXIMUM_SHARE_FILES = 16,
	/* port (this fork): the hosts' maps whose hashes are known, for their
	level names (map_share_level_name_hash) */
	MAXIMUM_LEVEL_HASHES = 16,
	/* the longest map file name the map lists take (custom_edition_maps.c:
	the level name, custom_maps\ and it, kept whole by the game engine) */
	MAXIMUM_MAP_NAME_LENGTH = CUSTOM_EDITION_MAXIMUM_LEVEL_NAME_LENGTH - (sizeof(CUSTOM_EDITION_LEVEL_NAME_PREFIX) - 1),
	/* the largest file hashed (the combined offset space's resource maps
	are smaller: custom_edition_cache.c) */
	MAXIMUM_FILE_BYTES = 0x7FFFFFFF,
};

/* what a hash is of: these bytes, the file's size (4 bytes, little-endian)
and its pieces' hashes */
static char const file_hash_label[] = "OpenCE map 1";
#define FILE_HASH_LABEL_BYTES 12

enum
{
	_hash_job_none,
	/* a catalog file's (map_share_file_hash) */
	_hash_job_file,
	/* a download's pieces (map_share_pieces_hash) */
	_hash_job_pieces,
};

/* ---------- structures */

/* the hashing thread's work: the file it reads, and what it finds */
struct hash_job
{
	char path[MAP_SHARE_PATH_SIZE];
	unsigned long size;
	long piece_count;
	byte *piece_hashes;
	byte hash[MAP_SHARE_HASH_BYTES];
	/* written by the thread: done (ready or failed) when it has exited */
	volatile long failed;
};

/* port (this fork): a map file's hash, while its size and time stay */
struct level_hash
{
	char path[MAP_SHARE_PATH_SIZE];
	unsigned long size;
	unsigned long modified;
	byte hash[MAP_SHARE_HASH_BYTES];
};

struct map_share_files_globals
{
	struct map_share_file files[MAXIMUM_SHARE_FILES];
	/* the file catalog's next to replace, when full */
	short next_file;

	/* the job running (_hash_job_*), its thread, and whose it is (a
	catalog index, or the pieces' owner) */
	short job_type;
	struct thread_reference *thread;
	struct hash_job job;
	long job_owner;

	/* port (this fork): the hashes of maps hosted, the next to replace */
	struct level_hash level_hashes[MAXIMUM_LEVEL_HASHES];
	short next_level_hash;

	/* the pieces asked for, and what was found for their owner */
	boolean pieces_wanted;
	char pieces_path[MAP_SHARE_PATH_SIZE];
	long pieces_owner;
	boolean pieces_done;
	long pieces_done_owner;
	byte *pieces_hashes;
	long pieces_count;
	unsigned long pieces_size;
};

/* ---------- globals */

static struct map_share_files_globals map_share_files_globals;

/* ---------- names */

static boolean hex_digit(
	char character)
{
	return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
}

boolean map_share_name_hashed(
	char const *name)
{
	long length = (long)csstrlen(name);
	long index;

	if (length < MAP_SHARE_NAME_DIGITS + 2 || name[length - MAP_SHARE_NAME_DIGITS - 1] != '_')
		return FALSE;
	for (index = length - MAP_SHARE_NAME_DIGITS; index < length; index++)
	{
		if (!hex_digit(name[index]))
			return FALSE;
	}
	return TRUE;
}

boolean map_share_name_safe(
	char const *name)
{
	long length = (long)csstrlen(name);
	long index;

	if (!length || name[0] == '.' || name[0] == ' ' || name[length - 1] == '.' || name[length - 1] == ' ')
		return FALSE;
	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)name[index];

		if (character < ' ' || character > '~' || strchr("/\\:*?\"<>|", character))
			return FALSE;
	}
	return TRUE;
}

short map_share_name_base_length(
	char const *name)
{
	short length = (short)csstrlen(name);

	return map_share_name_hashed(name) ? (short)(length - MAP_SHARE_NAME_DIGITS - 1) : length;
}

void map_share_hash_digits(
	byte const *hash,
	char *digits)
{
	static char const hex[] = "0123456789abcdef";
	short index;

	for (index = 0; index < MAP_SHARE_NAME_DIGITS / 2; index++)
	{
		digits[index * 2] = hex[hash[index] >> 4];
		digits[index * 2 + 1] = hex[hash[index] & 15];
	}
	digits[MAP_SHARE_NAME_DIGITS] = 0;
}

/* ---------- hashes */

long map_share_piece_count(
	unsigned long size)
{
	return (long)((size + MAP_SHARE_PIECE_BYTES - 1) / MAP_SHARE_PIECE_BYTES);
}

void map_share_file_hash_of_pieces(
	unsigned long size,
	byte const *piece_hashes,
	long piece_count,
	byte *hash)
{
	unsigned long bytes = FILE_HASH_LABEL_BYTES + 4 + (unsigned long)piece_count * MAP_SHARE_HASH_BYTES;
	byte *data = malloc(bytes);
	short index;

	if (!data)
	{
		csmemset(hash, 0, MAP_SHARE_HASH_BYTES);
		return;
	}
	csmemcpy(data, file_hash_label, FILE_HASH_LABEL_BYTES);
	for (index = 0; index < 4; index++)
		data[FILE_HASH_LABEL_BYTES + index] = (byte)(size >> (index * 8));
	if (piece_count)
		csmemcpy(data + FILE_HASH_LABEL_BYTES + 4, piece_hashes, (unsigned long)piece_count * MAP_SHARE_HASH_BYTES);
	p2p_blake2b_256(data, bytes, hash);
	free(data);
}

/* the hashing thread: the job's file read a piece at a time */
static unsigned long __stdcall hash_job_run(
	void *input)
{
	struct hash_job *job = input;
	byte *piece = malloc(MAP_SHARE_PIECE_BYTES);
	FILE *stream = fopen(job->path, "rb");
	long size = -1;
	long index;

	job->piece_hashes = NULL;
	if (!piece || !stream || fseek(stream, 0, SEEK_END) != 0 || (size = ftell(stream)) < 0 ||
		fseek(stream, 0, SEEK_SET) != 0)
	{
		size = -1;
	}
	if (size >= 0)
	{
		job->size = (unsigned long)size;
		job->piece_count = map_share_piece_count(job->size);
		job->piece_hashes = malloc((unsigned long)MAX(job->piece_count, 1) * MAP_SHARE_HASH_BYTES);
	}
	for (index = 0; job->piece_hashes && index < job->piece_count; index++)
	{
		unsigned long bytes = MIN(MAP_SHARE_PIECE_BYTES, job->size - (unsigned long)index * MAP_SHARE_PIECE_BYTES);

		if (fread(piece, 1, bytes, stream) != bytes)
		{
			free(job->piece_hashes);
			job->piece_hashes = NULL;
			break;
		}
		p2p_blake2b_256(piece, bytes, job->piece_hashes + index * MAP_SHARE_HASH_BYTES);
	}
	if (stream)
		fclose(stream);
	free(piece);
	if (job->piece_hashes)
		map_share_file_hash_of_pieces(job->size, job->piece_hashes, job->piece_count, job->hash);
	else
		job->failed = TRUE;

	return 0;
}

static boolean hash_job_start(
	short type,
	char const *path,
	long owner)
{
	struct map_share_files_globals *globals = &map_share_files_globals;

	csmemset(&globals->job, 0, sizeof(globals->job));
	csstrncpy(globals->job.path, path, sizeof(globals->job.path) - 1);
	if (!create_thread(1, hash_job_run, &globals->job, &globals->thread))
	{
		globals->thread = NULL;
		return FALSE;
	}
	globals->job_type = type;
	globals->job_owner = owner;
	return TRUE;
}

/* the file's size and when it was last written, as the file system says */
static boolean file_stamp(
	char const *path,
	unsigned long *size,
	unsigned long *modified)
{
	WIN32_FILE_ATTRIBUTE_DATA data;

	if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data) || data.nFileSizeHigh)
		return FALSE;
	*size = data.nFileSizeLow;
	*modified = data.ftLastWriteTime.dwLowDateTime ^ data.ftLastWriteTime.dwHighDateTime;
	return TRUE;
}

/* ---------- the catalog */

short map_share_file_hash(
	char const *path,
	short kind)
{
	struct map_share_files_globals *globals = &map_share_files_globals;
	struct map_share_file *file = NULL;
	unsigned long size;
	unsigned long modified;
	short index;

	if (!file_stamp(path, &size, &modified) || size > MAXIMUM_FILE_BYTES)
		return NONE;
	for (index = 0; index < MAXIMUM_SHARE_FILES; index++)
	{
		if (globals->files[index].used && !csstrcasecmp(globals->files[index].path, path))
		{
			file = &globals->files[index];
			break;
		}
	}
	/* (changed since: hashed again) */
	if (file && (file->ready || file->failed) && (file->size != size || file->modified != modified))
	{
		free(file->piece_hashes);
		file->piece_hashes = NULL;
		file->ready = FALSE;
		file->failed = FALSE;
	}
	if (!file)
	{
		/* a free one, else the next in turn not being hashed */
		for (index = 0; index < MAXIMUM_SHARE_FILES && globals->files[index].used; index++)
			;
		if (index == MAXIMUM_SHARE_FILES)
		{
			index = globals->next_file;
			if (globals->job_type == _hash_job_file && globals->job_owner == index)
				index = (short)((index + 1) % MAXIMUM_SHARE_FILES);
			globals->next_file = (short)((index + 1) % MAXIMUM_SHARE_FILES);
			free(globals->files[index].piece_hashes);
		}
		file = &globals->files[index];
		csmemset(file, 0, sizeof(*file));
		file->used = TRUE;
		csstrncpy(file->path, path, sizeof(file->path) - 1);
	}
	file->kind = kind;
	file->size = size;
	file->modified = modified;

	return (short)(file - globals->files);
}

short map_share_file_hashed(
	char const *path,
	short kind,
	unsigned long size,
	byte const *piece_hashes,
	long piece_count,
	byte const *hash)
{
	short index = map_share_file_hash(path, kind);
	struct map_share_file *file = map_share_file_get(index);

	if (!file || file->size != size || piece_count != map_share_piece_count(size))
		return index;
	free(file->piece_hashes);
	file->piece_hashes = malloc((unsigned long)MAX(piece_count, 1) * MAP_SHARE_HASH_BYTES);
	if (!file->piece_hashes)
		return index;
	csmemcpy(file->piece_hashes, piece_hashes, (unsigned long)piece_count * MAP_SHARE_HASH_BYTES);
	file->piece_count = piece_count;
	csmemcpy(file->hash, hash, MAP_SHARE_HASH_BYTES);
	file->ready = TRUE;
	file->failed = FALSE;
	return index;
}

struct map_share_file *map_share_file_get(
	short index)
{
	return index >= 0 && index < MAXIMUM_SHARE_FILES && map_share_files_globals.files[index].used ?
		&map_share_files_globals.files[index] : NULL;
}

short map_share_file_find(
	byte const *hash)
{
	short index;

	for (index = 0; index < MAXIMUM_SHARE_FILES; index++)
	{
		struct map_share_file const *file = &map_share_files_globals.files[index];

		if (file->used && file->ready && !csmemcmp(file->hash, hash, MAP_SHARE_HASH_BYTES))
			return index;
	}
	return NONE;
}

/* ---------- a download's pieces */

boolean map_share_pieces_hash(
	char const *path,
	long owner)
{
	struct map_share_files_globals *globals = &map_share_files_globals;

	if (globals->pieces_wanted || (globals->job_type == _hash_job_pieces))
		return globals->pieces_owner == owner;
	globals->pieces_wanted = TRUE;
	globals->pieces_owner = owner;
	csstrncpy(globals->pieces_path, path, sizeof(globals->pieces_path) - 1);
	globals->pieces_path[sizeof(globals->pieces_path) - 1] = 0;
	return TRUE;
}

boolean map_share_pieces_hashed(
	long owner,
	byte **piece_hashes,
	long *piece_count,
	unsigned long *size)
{
	struct map_share_files_globals *globals = &map_share_files_globals;

	if (!globals->pieces_done || globals->pieces_done_owner != owner)
		return FALSE;
	*piece_hashes = globals->pieces_hashes;
	*piece_count = globals->pieces_count;
	*size = globals->pieces_size;
	globals->pieces_done = FALSE;
	globals->pieces_hashes = NULL;
	return TRUE;
}

/* ---------- a host's level name

port (this fork): OpenCE renames every map of custom_maps <name>_<hash>.map
(its map lists name them), so that a host's level name says its version.
This fork never renames a file: a host whose network.map_share_names is
"hash" names its level <name>_<hash> (custom_maps\<name>_<hash> in the
protocol), hashing the map where it is, and the name finds the file from
then on (map_family_alias). "plain" leaves the name as it is: clients that
play maps by their names alone (ChupathingyCE's, OpenCE's before build-189)
play it, and OpenCE's since then that have it, but these do not download a
map named so (map_share.c). */

/* the hash of a map file, from what is known of it or hashed now (a map's
hash takes a fraction of a second) */
static boolean level_hash_find(
	char const *path,
	byte *hash)
{
	struct map_share_files_globals *globals = &map_share_files_globals;
	struct level_hash *entry;
	struct hash_job job;
	unsigned long size, modified;
	short index;

	if (!file_stamp(path, &size, &modified))
		return FALSE;
	for (index = 0; index < MAXIMUM_LEVEL_HASHES; index++)
	{
		entry = &globals->level_hashes[index];
		if (entry->path[0] && !csstrcasecmp(entry->path, path) && entry->size == size && entry->modified == modified)
		{
			csmemcpy(hash, entry->hash, MAP_SHARE_HASH_BYTES);
			return TRUE;
		}
	}
	csmemset(&job, 0, sizeof(job));
	csstrncpy(job.path, path, sizeof(job.path) - 1);
	hash_job_run(&job);
	free(job.piece_hashes);
	if (job.failed || job.size != size)
		return FALSE;
	entry = &globals->level_hashes[globals->next_level_hash];
	globals->next_level_hash = (short)((globals->next_level_hash + 1) % MAXIMUM_LEVEL_HASHES);
	csstrncpy(entry->path, path, sizeof(entry->path) - 1);
	entry->path[sizeof(entry->path) - 1] = 0;
	entry->size = size;
	entry->modified = modified;
	csmemcpy(entry->hash, job.hash, MAP_SHARE_HASH_BYTES);
	csmemcpy(hash, job.hash, MAP_SHARE_HASH_BYTES);
	return TRUE;
}

boolean map_share_file_digits(
	char const *path,
	char *digits)
{
	byte hash[MAP_SHARE_HASH_BYTES];

	if (!level_hash_find(path, hash))
		return FALSE;
	map_share_hash_digits(hash, digits);
	return TRUE;
}

void map_share_level_name_hash(
	char *level_name,
	long size)
{
	char file[MAP_SHARE_PATH_SIZE];
	char path[MAP_SHARE_PATH_SIZE];
	char hashed[MAP_SHARE_PATH_SIZE];
	char digits[MAP_SHARE_NAME_DIGITS + 1];
	byte hash[MAP_SHARE_HASH_BYTES];
	char const *names = config_string("network.map_share_names");

	/* (the game's name, <name>@ce: a Custom Edition map's, not named for
	its hash yet) */
	if (!level_name || map_family_parse(level_name, file, sizeof(file)) != _map_family_custom_edition ||
		map_share_name_hashed(file) || !names || _stricmp(names, "hash"))
	{
		return;
	}
	if (!map_family_find(_map_family_custom_edition, file, path, sizeof(path)))
		return;
	/* (the name and its hash kept whole by the game engine) */
	if (csstrlen(file) + 1 + MAP_SHARE_NAME_DIGITS > MAXIMUM_MAP_NAME_LENGTH)
	{
		error(_error_silent, "map sharing: %s is named as it is: too long a name for its hash too", file);
		return;
	}
	if (!level_hash_find(path, hash))
	{
		error(_error_silent, "map sharing: '%s' could not be read to name it for its hash", path);
		return;
	}
	map_share_hash_digits(hash, digits);
	csprintf(hashed, "%s_%s", file, digits);
	if (csstrlen(hashed) + csstrlen(map_family_suffix(_map_family_custom_edition)) >= (unsigned long)size)
		return;
	map_family_alias(hashed, path, TRUE);
	csprintf(level_name, "%s%s", hashed, map_family_suffix(_map_family_custom_edition));
	error(_error_silent, "map sharing: the game's map is %s (%s, not renamed)", level_name, path);
}

/* ---------- the hashing thread's work */

void map_share_files_update(
	void)
{
	struct map_share_files_globals *globals = &map_share_files_globals;
	short index;

	if (globals->thread)
	{
		if (!thread_has_exited(globals->thread))
			return;
		dispose_thread(globals->thread);
		globals->thread = NULL;
		switch (globals->job_type)
		{
		case _hash_job_file:
		{
			struct map_share_file *file = map_share_file_get((short)globals->job_owner);

			/* (replaced meanwhile: the catalog's file is another's now) */
			if (file && !csstrcasecmp(file->path, globals->job.path))
			{
				/* (changed meanwhile: hashed again) */
				if (globals->job.failed || globals->job.size != file->size)
				{
					file->failed = globals->job.failed;
					free(globals->job.piece_hashes);
				}
				else
				{
					file->piece_count = globals->job.piece_count;
					file->piece_hashes = globals->job.piece_hashes;
					csmemcpy(file->hash, globals->job.hash, MAP_SHARE_HASH_BYTES);
					file->ready = TRUE;
				}
			}
			else
			{
				free(globals->job.piece_hashes);
			}
			break;
		}
		case _hash_job_pieces:
			free(globals->pieces_hashes);
			globals->pieces_done = TRUE;
			globals->pieces_done_owner = globals->job_owner;
			globals->pieces_hashes = globals->job.failed ? NULL : globals->job.piece_hashes;
			globals->pieces_count = globals->job.failed ? 0 : globals->job.piece_count;
			globals->pieces_size = globals->job.size;
			break;
		}
		globals->job.piece_hashes = NULL;
		globals->job_type = _hash_job_none;
	}

	/* the next: a download's pieces, then a file to share */
	if (globals->pieces_wanted)
	{
		globals->pieces_wanted = FALSE;
		if (!hash_job_start(_hash_job_pieces, globals->pieces_path, globals->pieces_owner))
		{
			globals->pieces_done = TRUE;
			globals->pieces_done_owner = globals->pieces_owner;
			globals->pieces_hashes = NULL;
			globals->pieces_count = 0;
		}
		return;
	}
	for (index = 0; index < MAXIMUM_SHARE_FILES; index++)
	{
		struct map_share_file *file = &globals->files[index];

		if (file->used && !file->ready && !file->failed)
		{
			if (!hash_job_start(_hash_job_file, file->path, index))
				file->failed = TRUE;
			return;
		}
	}
}
