/*
TORRENT.C

The BitTorrent client's session (torrent.h, torrent_internal.h): its
thread, which waits on every socket and ticks the torrents; the torrents'
files, pieces and blocks; the metadata (the piece hashes) from the file or
from peers; the rate limits; and the API the game's thread calls.

The peers' protocol is torrent_peer.c, the trackers and web seeds
torrent_tracker.c, the DHT torrent_dht.c.

A torrent's pieces are written to its file as their blocks come, and a
piece is read back and hashed once whole: a bad piece has its blocks asked
for again. The file is opened as a whole once and read at offsets, so a
seeding map is never copied.
*/

#include "torrent_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct torrent_session torrent_session;
pthread_mutex_t torrent_lock = PTHREAD_MUTEX_INITIALIZER;

/* the pieces hashed in one tick while checking a file (the loop stays
responsive: a 300 MB map is checked in a second or two) */
#define CHECK_BYTES_PER_TICK (16UL << 20)
/* a block asked for that has not come after this long is asked again */
#define REQUEST_TIMEOUT (20 * TORRENT_SECOND)
/* how long a peer has a piece before a web seed may take it over (the endgame:
torrent_piece_to_request_whole) */
#define ENDGAME_WAIT (5 * TORRENT_SECOND)
#define SELECT_WAIT_MILLISECONDS 100
#define TOKENS_CAP_SECONDS 2.0

/* ---------- helpers */

unsigned long torrent_now(void)
{
	return GetTickCount();
}

int torrent_elapsed(unsigned long since, unsigned long milliseconds)
{
	return torrent_now() - since >= milliseconds;
}

/* whether a time to come (torrent_now() + a wait) has come: torrent_elapsed
(when, 0) is always true, the difference being unsigned, so every wait
until a time was none (trackers announced to many times a second). 0 is
now: a tracker's or web seed's state zeroed as it starts or stops (after
2^31 ms of uptime the difference from 0 is negative for weeks) */
int torrent_reached(unsigned long when)
{
	return !when || (long)(torrent_now() - when) >= 0;
}

void torrent_log(const char *format, ...)
{
	char text[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	platform_log("torrent: %s", text);
}

unsigned long torrent_network_long(unsigned long value)
{
	return __builtin_bswap32(value);
}

unsigned short torrent_network_short(unsigned short value)
{
	return (unsigned short)((value << 8) | (value >> 8));
}

void torrent_put_long(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)(value >> 24);
	bytes[1] = (unsigned char)(value >> 16);
	bytes[2] = (unsigned char)(value >> 8);
	bytes[3] = (unsigned char)value;
}

unsigned long torrent_get_long(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] << 24 | (unsigned long)bytes[1] << 16 | (unsigned long)bytes[2] << 8 | bytes[3];
}

void torrent_put_short(unsigned char *bytes, unsigned short value)
{
	bytes[0] = (unsigned char)(value >> 8);
	bytes[1] = (unsigned char)value;
}

unsigned short torrent_get_short(const unsigned char *bytes)
{
	return (unsigned short)(bytes[0] << 8 | bytes[1]);
}

const char *torrent_address_text(unsigned long address, unsigned short port, char *text)
{
	unsigned long value = torrent_network_long(address);

	sprintf(text, "%lu.%lu.%lu.%lu:%u", value >> 24, (value >> 16) & 255, (value >> 8) & 255, value & 255,
		torrent_network_short(port));
	return text;
}

int torrent_would_block(void)
{
	int error = posix_socket_last_error();

	return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}

int torrent_open_socket(int type, unsigned short port, unsigned short *bound_port)
{
	struct sockaddr_in address;
	int length = sizeof(address);
	int result = posix_socket(AF_INET, type, 0);

	if (result < 0)
		return -1;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = port;
	if (posix_socket_bind(result, &address, sizeof(address)) < 0 ||
		posix_socket_getsockname(result, &address, &length) < 0 ||
		posix_socket_set_nonblocking(result, 1) < 0)
	{
		posix_socket_close(result);
		return -1;
	}
	if (bound_port)
		*bound_port = address.sin_port;
	return result;
}

int torrent_random(int bound)
{
	unsigned long value;

	posix_random_bytes(&value, sizeof(value));
	return bound > 0 ? (int)(value % (unsigned long)bound) : 0;
}

int torrent_piece_size(const struct torrent *torrent, int piece)
{
	if (piece < 0 || piece >= torrent->piece_count)
		return 0;
	if (piece == torrent->piece_count - 1)
	{
		unsigned long long remainder = torrent->size - (unsigned long long)piece * torrent->piece_length;

		return (int)remainder;
	}
	return (int)torrent->piece_length;
}

int torrent_has_piece(const struct torrent *torrent, int piece)
{
	return piece >= 0 && piece < torrent->piece_count && (torrent->have[piece >> 3] & (0x80 >> (piece & 7))) != 0;
}

static void bit_set(unsigned char *bits, int index)
{
	bits[index >> 3] |= (unsigned char)(0x80 >> (index & 7));
}

static void bit_clear(unsigned char *bits, int index)
{
	bits[index >> 3] &= (unsigned char)~(0x80 >> (index & 7));
}

static int bit_test(const unsigned char *bits, int index);

int torrent_bit_test(const unsigned char *bits, int index)
{
	return bit_test(bits, index);
}

void torrent_bit_set(unsigned char *bits, int index)
{
	bit_set(bits, index);
}

void torrent_bit_clear(unsigned char *bits, int index)
{
	bit_clear(bits, index);
}

static int bit_test(const unsigned char *bits, int index)
{
	return (bits[index >> 3] & (0x80 >> (index & 7))) != 0;
}

int torrent_candidate_add(struct torrent *torrent, unsigned long address, unsigned short port,
	enum torrent_source source)
{
	struct torrent_candidate *candidate;
	int index;
	int oldest = -1;

	/* (the unroutable left out; a connection to this machine itself is
	ended by its handshake, torrent_peer.c) */
	if (!address || !port || (torrent_network_long(address) >> 24) == 0)
		return 0;
	for (index = 0; index < torrent->candidate_count; index++)
	{
		candidate = &torrent->candidates[index];
		if (candidate->address == address && candidate->port == port)
			return 0;
		if (!candidate->connected && (oldest < 0 || candidate->last_tried < torrent->candidates[oldest].last_tried))
			oldest = index;
	}
	if (torrent->candidate_count < TORRENT_MAXIMUM_CANDIDATES)
		candidate = &torrent->candidates[torrent->candidate_count++];
	else if (oldest >= 0)
		candidate = &torrent->candidates[oldest];
	else
		return 0;
	memset(candidate, 0, sizeof(*candidate));
	candidate->address = address;
	candidate->port = port;
	candidate->source = (unsigned char)source;
	return 1;
}

/* ---------- the file */

static int file_seek(struct torrent *torrent, unsigned long long offset)
{
	posix_ulong low, high;

	return posix_seek(torrent->file, (posix_long)(offset & 0xFFFFFFFFUL), (posix_long)(offset >> 32), SEEK_SET,
		&low, &high) == 0;
}

int torrent_file_read(struct torrent *torrent, unsigned long long offset, void *buffer, int size)
{
	unsigned char *bytes = buffer;

	if (torrent->file < 0 || !file_seek(torrent, offset))
		return 0;
	while (size > 0)
	{
		int count = (int)read(torrent->file, bytes, (unsigned)size);

		if (count <= 0)
			return 0;
		bytes += count;
		size -= count;
	}
	return 1;
}

int torrent_file_write(struct torrent *torrent, unsigned long long offset, const void *buffer, int size)
{
	const unsigned char *bytes = buffer;

	if (torrent->file < 0 || torrent->seeding || !file_seek(torrent, offset))
		return 0;
	while (size > 0)
	{
		int count = (int)write(torrent->file, bytes, (unsigned)size);

		if (count <= 0)
			return 0;
		bytes += count;
		size -= count;
	}
	return 1;
}

static int file_open(struct torrent *torrent, int create, unsigned long long *existing_size)
{
	struct posix_file_information information;

	*existing_size = 0;
	if (posix_stat(torrent->path, &information) == 0 && !(information.flags & _posix_file_is_directory))
		*existing_size = (unsigned long long)information.size_high << 32 | information.size_low;
	else if (!create)
		return 0;
	torrent->file = open(torrent->path, (torrent->seeding ? O_RDONLY : O_RDWR | O_CREAT) | O_CLOEXEC, 0644);
	return torrent->file >= 0;
}

static void file_close(struct torrent *torrent)
{
	if (torrent->file >= 0)
	{
		close(torrent->file);
		torrent->file = -1;
	}
}

/* ---------- the metadata (the info dictionary) */

/* the info dictionary of the torrent's name, size, piece length and
these hashes, as tools/map_torrents.py writes it (its keys in order):
its bytes, malloc'd, and their count */
static unsigned char *info_dictionary_make(const struct torrent *torrent, const unsigned char *hashes, int *size)
{
	struct bencode_writer writer;
	int capacity = 128 + (int)strlen(torrent->name) + torrent->piece_count * SHA1_DIGEST_SIZE;
	unsigned char *bytes = malloc((size_t)capacity);

	if (!bytes)
		return NULL;
	bencode_writer_start(&writer, bytes, capacity);
	bencode_write_dictionary_start(&writer);
	bencode_write_text(&writer, "length");
	bencode_write_integer(&writer, (long long)torrent->size);
	bencode_write_text(&writer, "name");
	bencode_write_text(&writer, torrent->name);
	bencode_write_text(&writer, "piece length");
	bencode_write_integer(&writer, (long long)torrent->piece_length);
	bencode_write_text(&writer, "pieces");
	bencode_write_bytes(&writer, hashes, torrent->piece_count * SHA1_DIGEST_SIZE);
	bencode_write_end(&writer);
	if (writer.overflow)
	{
		free(bytes);
		return NULL;
	}
	*size = writer.size;
	return bytes;
}

/* takes the info dictionary in bytes as the torrent's metadata, if its
hash is the torrent's and it describes the torrent as the index does;
1 on success */
static int metadata_take(struct torrent *torrent, unsigned char *bytes, int size)
{
	unsigned char digest[SHA1_DIGEST_SIZE];
	struct bencode_document *document;
	const unsigned char *pieces;
	int pieces_length;
	int node;
	int ok = 0;

	sha1_bytes(bytes, (unsigned long)size, digest);
	if (memcmp(digest, torrent->info_hash, sizeof(digest)))
	{
		torrent_log("%s: the metadata's hash is not the torrent's (%d bytes)", torrent->name, size);
		return 0;
	}
	document = malloc(sizeof(*document));
	if (!document)
		return 0;
	if (bencode_parse(document, bytes, size) == size && document->nodes[0].type == _bencode_dictionary)
	{
		node = bencode_find(document, 0, "pieces");
		ok = bencode_integer(document, bencode_find(document, 0, "length"), -1) == (long long)torrent->size &&
			bencode_integer(document, bencode_find(document, 0, "piece length"), -1) ==
			(long long)torrent->piece_length &&
			bencode_string(document, node, &pieces, &pieces_length) &&
			pieces_length == torrent->piece_count * SHA1_DIGEST_SIZE &&
			bencode_find(document, 0, "files") < 0;
	}
	free(document);
	if (!ok)
	{
		torrent_log("%s: the metadata does not describe the torrent as the index does", torrent->name);
		return 0;
	}
	free(torrent->metadata);
	torrent->metadata = bytes;
	torrent->metadata_size = size;
	torrent->piece_hashes = (unsigned char *)pieces;
	return 1;
}

int torrent_metadata_piece_to_request(struct torrent *torrent)
{
	int piece;

	if (torrent->state != _torrent_state_metadata || !torrent->metadata_piece_count)
		return -1;
	for (piece = 0; piece < torrent->metadata_piece_count; piece++)
	{
		if (!bit_test(torrent->metadata_have, piece))
			return piece;
	}
	return -1;
}

static void metadata_discard(struct torrent *torrent)
{
	free(torrent->metadata);
	torrent->metadata = NULL;
	torrent->metadata_size = 0;
	torrent->metadata_piece_count = 0;
	torrent->metadata_have_count = 0;
	memset(torrent->metadata_have, 0, sizeof(torrent->metadata_have));
	torrent->piece_hashes = NULL;
}

static void checking_start(struct torrent *torrent);

int torrent_metadata_received(struct torrent *torrent, int piece, const unsigned char *data, int size,
	int total_size)
{
	int expected;

	if (torrent->state != _torrent_state_metadata)
		return 0;
	if (total_size <= 0 || total_size > TORRENT_MAXIMUM_METADATA)
		return 0;
	if (!torrent->metadata)
	{
		torrent->metadata = malloc((size_t)total_size);
		if (!torrent->metadata)
			return 0;
		torrent->metadata_size = total_size;
		torrent->metadata_piece_count = (total_size + TORRENT_METADATA_PIECE - 1) / TORRENT_METADATA_PIECE;
	}
	else if (total_size != torrent->metadata_size)
	{
		return 0;
	}
	if (piece < 0 || piece >= torrent->metadata_piece_count || bit_test(torrent->metadata_have, piece))
		return 0;
	expected = piece == torrent->metadata_piece_count - 1 ?
		torrent->metadata_size - piece * TORRENT_METADATA_PIECE : TORRENT_METADATA_PIECE;
	if (size != expected)
		return 0;
	memcpy(torrent->metadata + piece * TORRENT_METADATA_PIECE, data, (size_t)size);
	bit_set(torrent->metadata_have, piece);
	torrent->metadata_have_count++;
	if (torrent->metadata_have_count < torrent->metadata_piece_count)
		return 1;
	/* whole: taken if right, else fetched again */
	{
		unsigned char *bytes = torrent->metadata;
		int bytes_size = torrent->metadata_size;

		torrent->metadata = NULL;
		if (!metadata_take(torrent, bytes, bytes_size))
		{
			free(bytes);
			metadata_discard(torrent);
			return 1;
		}
	}
	torrent_log("%s: the metadata came from peers (%d pieces of %lu bytes)", torrent->name, torrent->piece_count,
		torrent->piece_length);
	/* (a partial file from an earlier run is checked first) */
	checking_start(torrent);
	return 1;
}

/* ---------- checking the file's pieces */

static void checking_start(struct torrent *torrent)
{
	torrent->state = _torrent_state_checking;
	torrent->check_piece = 0;
	torrent->have_count = 0;
	memset(torrent->have, 0, sizeof(torrent->have));
	torrent->active_count = 0;
}

/* the whole file fetched from the web seeds (torrent_web_seeds_tick):
hashed as a seeding torrent's file is, its hashes making the info
dictionary, which must have the info hash (checking_tick) */
void torrent_web_whole_check(struct torrent *torrent)
{
	metadata_discard(torrent);
	torrent_log("%s: the whole file came from the web seeds; checking it against the info hash", torrent->name);
	checking_start(torrent);
}

static void download_complete(struct torrent *torrent)
{
	torrent->state = _torrent_state_seeding;
	torrent->active_count = 0;
	torrent_log("%s: complete", torrent->name);
}

static void checking_tick(struct torrent *torrent)
{
	static unsigned char *buffer;
	unsigned long hashed = 0;
	unsigned char digest[SHA1_DIGEST_SIZE];
	unsigned char *computed = NULL;

	if (!buffer)
	{
		buffer = malloc(TORRENT_MAXIMUM_PIECE_LENGTH);
		if (!buffer)
		{
			torrent_fail(torrent, "out of memory");
			return;
		}
	}
	/* (seeding: the hashes are computed, and must make the info hash) */
	if (!torrent->piece_hashes)
	{
		if (!torrent->metadata)
		{
			torrent->metadata = malloc((size_t)torrent->piece_count * SHA1_DIGEST_SIZE);
			if (!torrent->metadata)
			{
				torrent_fail(torrent, "out of memory");
				return;
			}
			torrent->metadata_size = torrent->piece_count * SHA1_DIGEST_SIZE;
		}
		computed = torrent->metadata;
	}
	while (torrent->check_piece < torrent->piece_count && hashed < CHECK_BYTES_PER_TICK)
	{
		int piece = torrent->check_piece++;
		int size = torrent_piece_size(torrent, piece);

		if (torrent_file_read(torrent, (unsigned long long)piece * torrent->piece_length, buffer, size))
		{
			sha1_bytes(buffer, (unsigned long)size, digest);
			if (computed)
			{
				memcpy(computed + piece * SHA1_DIGEST_SIZE, digest, sizeof(digest));
				bit_set(torrent->have, piece);
				torrent->have_count++;
			}
			else if (!memcmp(digest, torrent->piece_hashes + piece * SHA1_DIGEST_SIZE, sizeof(digest)))
			{
				bit_set(torrent->have, piece);
				torrent->have_count++;
			}
		}
		else if (computed)
		{
			torrent_fail(torrent, "the file could not be read");
			return;
		}
		hashed += (unsigned long)size;
	}
	if (torrent->check_piece < torrent->piece_count)
		return;
	if (computed)
	{
		/* the hashes computed make the info dictionary: its hash must be the
		torrent's, or the file is not the torrent's (another version of the
		map, say) */
		int size;
		unsigned char *info = info_dictionary_make(torrent, computed, &size);

		if (!info)
		{
			torrent_fail(torrent, "out of memory");
			return;
		}
		free(torrent->metadata);
		torrent->metadata = NULL;
		if (!metadata_take(torrent, info, size))
		{
			free(info);
			if (!torrent->seeding)
			{
				/* (the web seeds' file, another version of the map, say: the
				metadata and the pieces looked for from peers, as before) */
				torrent_log("%s: the web seeds' file is not this torrent's; looking to peers", torrent->name);
				metadata_discard(torrent);
				torrent->web_whole = -1;
				torrent->have_count = 0;
				memset(torrent->have, 0, sizeof(torrent->have));
				torrent->state = _torrent_state_metadata;
				return;
			}
			torrent_fail(torrent, "the file is not this torrent's");
			return;
		}
	}
	torrent_log("%s: checked, %d of %d pieces had", torrent->name, torrent->have_count, torrent->piece_count);
	if (torrent->have_count == torrent->piece_count)
	{
		download_complete(torrent);
	}
	else if (torrent->seeding)
	{
		torrent_fail(torrent, "the file is incomplete");
	}
	else
	{
		torrent->state = _torrent_state_downloading;
	}
	torrent_peers_metadata_known(torrent);
}

/* ---------- pieces and blocks */

static struct torrent_active_piece *active_find(struct torrent *torrent, int piece)
{
	int index;

	for (index = 0; index < torrent->active_count; index++)
	{
		if (torrent->active[index].piece == piece)
			return &torrent->active[index];
	}
	return NULL;
}

static struct torrent_active_piece *active_add(struct torrent *torrent, int piece)
{
	struct torrent_active_piece *active;

	if (torrent->active_count >= TORRENT_MAXIMUM_ACTIVE_PIECES)
		return NULL;
	active = &torrent->active[torrent->active_count++];
	memset(active, 0, sizeof(*active));
	active->piece = piece;
	active->block_count = (torrent_piece_size(torrent, piece) + TORRENT_BLOCK_SIZE - 1) / TORRENT_BLOCK_SIZE;
	active->started = torrent_now();
	return active;
}

static void active_remove(struct torrent *torrent, struct torrent_active_piece *active)
{
	int index = (int)(active - torrent->active);

	memmove(&torrent->active[index], &torrent->active[index + 1],
		(size_t)(torrent->active_count - index - 1) * sizeof(*active));
	torrent->active_count--;
}

static int block_length(const struct torrent *torrent, int piece, int block)
{
	int size = torrent_piece_size(torrent, piece);
	int remaining = size - block * TORRENT_BLOCK_SIZE;

	return remaining < TORRENT_BLOCK_SIZE ? remaining : TORRENT_BLOCK_SIZE;
}

/* a piece just whole: read back and hashed; had if right, else its blocks
to be asked for again */
static void piece_complete(struct torrent *torrent, struct torrent_active_piece *active)
{
	static unsigned char *buffer;
	unsigned char digest[SHA1_DIGEST_SIZE];
	int piece = active->piece;
	int size = torrent_piece_size(torrent, piece);

	if (!buffer)
		buffer = malloc(TORRENT_MAXIMUM_PIECE_LENGTH);
	if (!buffer || !torrent_file_read(torrent, (unsigned long long)piece * torrent->piece_length, buffer, size))
	{
		torrent_fail(torrent, "the file could not be read back");
		return;
	}
	sha1_bytes(buffer, (unsigned long)size, digest);
	active_remove(torrent, active);
	if (memcmp(digest, torrent->piece_hashes + piece * SHA1_DIGEST_SIZE, sizeof(digest)))
	{
		torrent_log("%s: piece %d failed its hash; asking again", torrent->name, piece);
		return;
	}
	bit_set(torrent->have, piece);
	torrent->have_count++;
	torrent_peers_have(torrent, piece);
	if (torrent->have_count == torrent->piece_count)
		download_complete(torrent);
}

int torrent_block_received(struct torrent *torrent, int piece, int begin, const unsigned char *data, int length)
{
	struct torrent_active_piece *active;
	int block;

	if (torrent->state != _torrent_state_downloading || torrent_has_piece(torrent, piece))
		return 0;
	if (begin < 0 || (begin % TORRENT_BLOCK_SIZE) != 0)
		return 0;
	block = begin / TORRENT_BLOCK_SIZE;
	if (length != block_length(torrent, piece, block))
		return 0;
	active = active_find(torrent, piece);
	if (!active || bit_test(active->received, block))
		return 0;
	if (!torrent_file_write(torrent, (unsigned long long)piece * torrent->piece_length + (unsigned long long)begin,
		data, length))
	{
		torrent_fail(torrent, "the file could not be written");
		return 0;
	}
	bit_set(active->received, block);
	active->received_count++;
	if (active->received_count == active->block_count)
		piece_complete(torrent, active);
	return 1;
}

/* the blocks of an active piece not yet received nor requested, in order;
a block requested long ago is open again */
static int active_block_to_request(struct torrent *torrent, struct torrent_active_piece *active, int *begin,
	int *length)
{
	int block;

	for (block = 0; block < active->block_count; block++)
	{
		if (!bit_test(active->received, block) && !bit_test(active->requested, block))
		{
			bit_set(active->requested, block);
			*begin = block * TORRENT_BLOCK_SIZE;
			*length = block_length(torrent, active->piece, block);
			return 1;
		}
	}
	return 0;
}

int torrent_block_to_request(struct torrent *torrent, const unsigned char *bitfield, int *piece, int *begin,
	int *length)
{
	int index;
	int start;
	int count;

	if (torrent->state != _torrent_state_downloading || !torrent->piece_hashes)
		return 0;
	/* a piece under way the peer has first */
	for (index = 0; index < torrent->active_count; index++)
	{
		struct torrent_active_piece *active = &torrent->active[index];

		if ((!bitfield || bit_test(bitfield, active->piece)) && active_block_to_request(torrent, active, begin, length))
		{
			*piece = active->piece;
			return 1;
		}
	}
	/* else a new piece, from a random start (peers take different ones) */
	if (torrent->active_count >= TORRENT_MAXIMUM_ACTIVE_PIECES)
		return 0;
	start = torrent_random(torrent->piece_count);
	for (count = 0; count < torrent->piece_count; count++)
	{
		int candidate = (start + count) % torrent->piece_count;

		if (!torrent_has_piece(torrent, candidate) && (!bitfield || bit_test(bitfield, candidate)) &&
			!active_find(torrent, candidate))
		{
			struct torrent_active_piece *active = active_add(torrent, candidate);

			if (active && active_block_to_request(torrent, active, begin, length))
			{
				*piece = candidate;
				return 1;
			}
			return 0;
		}
	}
	return 0;
}

void torrent_block_unrequested(struct torrent *torrent, int piece, int begin)
{
	struct torrent_active_piece *active = active_find(torrent, piece);

	if (active && begin >= 0 && begin / TORRENT_BLOCK_SIZE < active->block_count)
		bit_clear(active->requested, begin / TORRENT_BLOCK_SIZE);
}

int torrent_piece_to_request_whole(struct torrent *torrent, int *piece)
{
	int start;
	int count;

	if (torrent->state != _torrent_state_downloading || !torrent->piece_hashes)
		return 0;
	/* (from the end: the peers' random starts are as likely anywhere, and a
	web seed is fastest at the pieces no peer is on) */
	start = torrent_random(torrent->piece_count);
	for (count = 0; count < torrent->piece_count && torrent->active_count < TORRENT_MAXIMUM_ACTIVE_PIECES; count++)
	{
		int candidate = (start + torrent->piece_count - count) % torrent->piece_count;

		if (!torrent_has_piece(torrent, candidate) && !active_find(torrent, candidate))
		{
			struct torrent_active_piece *active = active_add(torrent, candidate);
			int block;

			if (!active)
				return 0;
			for (block = 0; block < active->block_count; block++)
				bit_set(active->requested, block);
			active->web_seed = 1;
			*piece = candidate;
			return 1;
		}
	}
	/* (the endgame: every piece left is a peer's. A busy or gone peer held
	the last of them for ever, the web seed taking only pieces no peer was
	on: it takes the one waited on longest, once a peer has had it a while;
	blocks that come twice are taken once. Never one another web seed
	connection is fetching: that one was handed on every ENDGAME_WAIT) */
	{
		struct torrent_active_piece *oldest = NULL;
		int index;
		int block;

		for (index = 0; index < torrent->active_count; index++)
		{
			struct torrent_active_piece *active = &torrent->active[index];

			if (active->received_count < active->block_count && !active->web_seed &&
				torrent_elapsed(active->started, ENDGAME_WAIT) &&
				(!oldest || (long)(active->started - oldest->started) < 0))
			{
				oldest = active;
			}
		}
		if (!oldest)
			return 0;
		for (block = 0; block < oldest->block_count; block++)
			bit_set(oldest->requested, block);
		oldest->web_seed = 1;
		oldest->started = torrent_now();
		*piece = oldest->piece;
		return 1;
	}
}

void torrent_piece_unrequested(struct torrent *torrent, int piece)
{
	struct torrent_active_piece *active = active_find(torrent, piece);
	int block;

	if (!active)
		return;
	active->web_seed = 0;
	for (block = 0; block < active->block_count; block++)
	{
		if (!bit_test(active->received, block))
			bit_clear(active->requested, block);
	}
}

int torrent_wants_from(const struct torrent *torrent, const unsigned char *bitfield)
{
	int piece;

	if (torrent->state != _torrent_state_downloading && torrent->state != _torrent_state_metadata)
		return 0;
	if (torrent->state == _torrent_state_metadata)
		return 1;
	for (piece = 0; piece < torrent->piece_count; piece++)
	{
		if (!torrent_has_piece(torrent, piece) && bit_test(bitfield, piece))
			return 1;
	}
	return 0;
}

/* blocks requested long ago, open to asking again (the peer that was asked
may have gone quiet) */
static void active_pieces_tick(struct torrent *torrent)
{
	int index;

	for (index = 0; index < torrent->active_count; index++)
	{
		struct torrent_active_piece *active = &torrent->active[index];

		if (torrent_elapsed(active->started, REQUEST_TIMEOUT))
		{
			int block;

			for (block = 0; block < active->block_count; block++)
			{
				if (!bit_test(active->received, block))
					bit_clear(active->requested, block);
			}
			active->started = torrent_now();
		}
	}
}

/* ---------- rates and limits */

static void rates_tick(struct torrent *torrent)
{
	unsigned long second = torrent_now() / TORRENT_SECOND;

	while (torrent->rate_second != second)
	{
		int slot;

		torrent->rate_second = torrent->rate_second + 1 <= second ? torrent->rate_second + 1 : second;
		slot = (int)(torrent->rate_second % TORRENT_RATE_SECONDS);
		torrent->download_by_second[slot] = 0;
		torrent->upload_by_second[slot] = 0;
	}
}

static long rate_of(const long *by_second, unsigned long second)
{
	long total = 0;
	int index;

	/* (the seconds before this one, which is still filling) */
	for (index = 1; index < TORRENT_RATE_SECONDS; index++)
		total += by_second[(second + TORRENT_RATE_SECONDS - (unsigned long)index) % TORRENT_RATE_SECONDS];
	return total / (TORRENT_RATE_SECONDS - 1);
}

void torrent_uploaded(struct torrent *torrent, long bytes)
{
	torrent->uploaded += (unsigned long long)bytes;
	torrent->upload_by_second[torrent->rate_second % TORRENT_RATE_SECONDS] += bytes;
	torrent_session.upload_tokens -= bytes;
}

void torrent_downloaded(struct torrent *torrent, long bytes)
{
	torrent->downloaded += (unsigned long long)bytes;
	torrent->download_by_second[torrent->rate_second % TORRENT_RATE_SECONDS] += bytes;
	torrent_session.download_tokens -= bytes;
}

static void tokens_tick(void)
{
	unsigned long now = torrent_now();
	double seconds = (double)(now - torrent_session.tokens_time) / 1000.0;

	torrent_session.tokens_time = now;
	if (seconds < 0.0 || seconds > TOKENS_CAP_SECONDS)
		seconds = TOKENS_CAP_SECONDS;
	torrent_session.upload_tokens += seconds * (double)torrent_session.upload_limit;
	torrent_session.download_tokens += seconds * (double)torrent_session.download_limit;
	if (torrent_session.upload_tokens > TOKENS_CAP_SECONDS * (double)torrent_session.upload_limit)
		torrent_session.upload_tokens = TOKENS_CAP_SECONDS * (double)torrent_session.upload_limit;
	if (torrent_session.download_tokens > TOKENS_CAP_SECONDS * (double)torrent_session.download_limit)
		torrent_session.download_tokens = TOKENS_CAP_SECONDS * (double)torrent_session.download_limit;
}

long torrent_upload_allowance(void)
{
	if (!torrent_session.upload_limit)
		return 1L << 30;
	return torrent_session.upload_tokens > 0.0 ? (long)torrent_session.upload_tokens : 0;
}

long torrent_download_allowance(void)
{
	if (!torrent_session.download_limit)
		return 1L << 30;
	return torrent_session.download_tokens > 0.0 ? (long)torrent_session.download_tokens : 0;
}

/* ---------- torrents */

void torrent_fail(struct torrent *torrent, const char *reason)
{
	if (torrent->state == _torrent_state_failed)
		return;
	torrent_log("%s: failed: %s", torrent->name, reason);
	snprintf(torrent->failure, sizeof(torrent->failure), "%s", reason);
	torrent->state = _torrent_state_failed;
	torrent_peers_close_all(torrent);
	torrent_trackers_stop(torrent);
	file_close(torrent);
}

static void torrent_tick(struct torrent *torrent)
{
	rates_tick(torrent);
	switch (torrent->state)
	{
	case _torrent_state_checking:
		checking_tick(torrent);
		break;
	case _torrent_state_metadata:
	case _torrent_state_downloading:
		active_pieces_tick(torrent);
		torrent_trackers_tick(torrent);
		if (torrent_session.dht && torrent_reached(torrent->dht_next_lookup))
			torrent_dht_lookup(torrent);
		torrent_web_seeds_tick(torrent);
		torrent_peers_tick(torrent);
		break;
	case _torrent_state_seeding:
		torrent_trackers_tick(torrent);
		if (torrent_session.dht && torrent_reached(torrent->dht_next_lookup))
			torrent_dht_lookup(torrent);
		torrent_peers_tick(torrent);
		break;
	case _torrent_state_failed:
		break;
	}
}

static void torrent_free(struct torrent *torrent)
{
	torrent_peers_close_all(torrent);
	torrent_trackers_stop(torrent);
	file_close(torrent);
	free(torrent->metadata);
	memset(torrent, 0, sizeof(*torrent));
	torrent->file = -1;
}

/* ---------- the thread */

#define MAXIMUM_SOCKETS (TORRENT_MAXIMUM_TORRENTS * (TORRENT_MAXIMUM_PEERS + TORRENT_MAXIMUM_TRACKERS + \
	TORRENT_MAXIMUM_WEB_SEEDS) + 4)

static void accept_connections(void)
{
	for (;;)
	{
		struct sockaddr_in address;
		int length = sizeof(address);
		int socket = posix_socket_accept(torrent_session.listen_socket, &address, &length);

		if (socket < 0)
			return;
		posix_socket_set_nonblocking(socket, 1);
		posix_socket_set_nodelay(socket);
		torrent_peer_accept(socket, address.sin_addr.s_addr, address.sin_port);
	}
}

static void udp_receive(void)
{
	static unsigned char packet[4096];

	for (;;)
	{
		struct sockaddr_in from;
		int length = sizeof(from);
		int size = posix_socket_recvfrom(torrent_session.udp_socket, packet, sizeof(packet), 0, &from, &length);

		if (size <= 0)
			return;
		/* (a DHT message is a bencoded dictionary; a tracker's answer starts
		with its action, a small number) */
		if (packet[0] == 'd')
			torrent_dht_received(packet, size, from.sin_addr.s_addr, from.sin_port);
		else
			torrent_tracker_udp_received(packet, size, from.sin_addr.s_addr, from.sin_port);
	}
}

static void *torrent_thread(void *context)
{
	static int read[MAXIMUM_SOCKETS], write[MAXIMUM_SOCKETS];
	static int asked_read[MAXIMUM_SOCKETS], asked_write[MAXIMUM_SOCKETS];

	(void)context;
	pthread_mutex_lock(&torrent_lock);
	torrent_session.thread_running = 1;
	while (!torrent_session.stop_requested)
	{
		int read_count = 0, write_count = 0, error_count = 0;
		int index;

		tokens_tick();
		for (index = 0; index < TORRENT_MAXIMUM_TORRENTS; index++)
		{
			if (torrent_session.torrents[index].used)
				torrent_tick(&torrent_session.torrents[index]);
		}
		if (torrent_session.dht)
			torrent_dht_tick();

		read[read_count++] = torrent_session.listen_socket;
		read[read_count++] = torrent_session.udp_socket;
		torrent_peers_sockets(read, &read_count, write, &write_count, MAXIMUM_SOCKETS);
		torrent_http_sockets(read, &read_count, write, &write_count, MAXIMUM_SOCKETS);
		memcpy(asked_read, read, sizeof(int) * (size_t)read_count);
		memcpy(asked_write, write, sizeof(int) * (size_t)write_count);

		pthread_mutex_unlock(&torrent_lock);
		if (posix_socket_select(read, &read_count, write, &write_count, NULL, &error_count, 0,
			SELECT_WAIT_MILLISECONDS * 1000, 0) < 0)
		{
			read_count = write_count = 0;
		}
		pthread_mutex_lock(&torrent_lock);
		if (torrent_session.stop_requested)
			break;
		for (index = 0; index < read_count; index++)
		{
			if (read[index] == torrent_session.listen_socket)
				accept_connections();
			else if (read[index] == torrent_session.udp_socket)
				udp_receive();
			else if (!torrent_peer_socket_ready(read[index], 0))
				torrent_http_socket_ready(read[index], 0);
		}
		for (index = 0; index < write_count; index++)
		{
			if (!torrent_peer_socket_ready(write[index], 1))
				torrent_http_socket_ready(write[index], 1);
		}
	}
	torrent_session.thread_running = 0;
	pthread_cond_broadcast(&torrent_session.stopped);
	pthread_mutex_unlock(&torrent_lock);
	return NULL;
}

/* ---------- the API */

static int hex_digit(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

static int info_hash_parse(const char *hex, unsigned char *info_hash)
{
	int index;

	for (index = 0; index < SHA1_DIGEST_SIZE; index++)
	{
		int high = hex_digit(hex[index * 2]);
		int low = high >= 0 ? hex_digit(hex[index * 2 + 1]) : -1;

		if (low < 0)
			return 0;
		info_hash[index] = (unsigned char)(high << 4 | low);
	}
	return hex[2 * SHA1_DIGEST_SIZE] == 0;
}

int torrent_start(const struct torrent_settings *settings, char *error, int error_size)
{
	int index;
	unsigned short port;
	int tries;

	pthread_mutex_lock(&torrent_lock);
	if (torrent_session.running)
	{
		pthread_mutex_unlock(&torrent_lock);
		return 1;
	}
	memset(&torrent_session, 0, sizeof(torrent_session));
	torrent_session.listen_socket = torrent_session.udp_socket = -1;
	for (index = 0; index < TORRENT_MAXIMUM_TORRENTS; index++)
		torrent_session.torrents[index].file = -1;
	torrent_session.dht = settings->dht;
	torrent_session.maximum_peers = settings->maximum_peers > 0 ? settings->maximum_peers : 40;
	torrent_session.upload_limit = settings->upload_limit > 0 ? settings->upload_limit : 0;
	torrent_session.download_limit = settings->download_limit > 0 ? settings->download_limit : 0;
	torrent_session.tokens_time = torrent_now();
	snprintf(torrent_session.trackers_text, sizeof(torrent_session.trackers_text), "%s",
		settings->trackers ? settings->trackers : "");
	snprintf(torrent_session.web_seeds_text, sizeof(torrent_session.web_seeds_text), "%s",
		settings->web_seeds ? settings->web_seeds : "");
	/* the id: this client's name, then random */
	memcpy(torrent_session.peer_id, "-OC0001-", 8);
	posix_random_bytes(torrent_session.peer_id + 8, sizeof(torrent_session.peer_id) - 8);
	/* the TCP and UDP ports the same number: the DHT announces the TCP one
	as the UDP one it is heard from (implied_port) */
	for (tries = 0; tries < 8; tries++)
	{
		unsigned short wanted = (unsigned short)(settings->port > 0 && settings->port < 65536 ?
			settings->port + tries : 0);
		unsigned short bound = 0;

		torrent_session.listen_socket = torrent_open_socket(SOCK_STREAM, torrent_network_short(wanted), &bound);
		if (torrent_session.listen_socket < 0)
			continue;
		torrent_session.udp_socket = torrent_open_socket(SOCK_DGRAM, bound, NULL);
		if (torrent_session.udp_socket >= 0 && posix_socket_listen(torrent_session.listen_socket, 8) == 0)
		{
			port = bound;
			break;
		}
		posix_socket_close(torrent_session.listen_socket);
		if (torrent_session.udp_socket >= 0)
			posix_socket_close(torrent_session.udp_socket);
		torrent_session.listen_socket = torrent_session.udp_socket = -1;
		if (!wanted)
			break;
	}
	if (torrent_session.listen_socket < 0)
	{
		snprintf(error, error_size, "no port could be listened on");
		pthread_mutex_unlock(&torrent_lock);
		return 0;
	}
	torrent_session.port = torrent_network_short(port);
	torrent_trackers_configure();
	if (torrent_session.dht)
		torrent_dht_start();
	torrent_session.running = 1;
	pthread_cond_init(&torrent_session.stopped, NULL);
	if (pthread_create(&torrent_session.thread, NULL, torrent_thread, NULL) != 0)
	{
		torrent_session.running = 0;
		posix_socket_close(torrent_session.listen_socket);
		posix_socket_close(torrent_session.udp_socket);
		torrent_session.listen_socket = torrent_session.udp_socket = -1;
		snprintf(error, error_size, "the thread could not be started");
		pthread_mutex_unlock(&torrent_lock);
		return 0;
	}
	torrent_log("listening on port %d (DHT %s, %d trackers, %d web seeds, up %ld down %ld bytes/s)",
		torrent_session.port, torrent_session.dht ? "on" : "off", torrent_session.tracker_count,
		torrent_session.web_seed_count, torrent_session.upload_limit, torrent_session.download_limit);
	pthread_mutex_unlock(&torrent_lock);
	return 1;
}

void torrent_stop(void)
{
	int index;

	pthread_mutex_lock(&torrent_lock);
	if (!torrent_session.running)
	{
		pthread_mutex_unlock(&torrent_lock);
		return;
	}
	torrent_session.stop_requested = 1;
	/* (the thread ends its wait within SELECT_WAIT_MILLISECONDS) */
	while (torrent_session.thread_running)
		pthread_cond_wait(&torrent_session.stopped, &torrent_lock);
	pthread_detach(torrent_session.thread);
	pthread_cond_destroy(&torrent_session.stopped);
	for (index = 0; index < TORRENT_MAXIMUM_TORRENTS; index++)
	{
		if (torrent_session.torrents[index].used)
			torrent_free(&torrent_session.torrents[index]);
	}
	if (torrent_session.dht)
		torrent_dht_stop();
	posix_socket_close(torrent_session.listen_socket);
	posix_socket_close(torrent_session.udp_socket);
	torrent_session.listen_socket = torrent_session.udp_socket = -1;
	torrent_session.running = 0;
	torrent_session.stop_requested = 0;
	pthread_mutex_unlock(&torrent_lock);
}

int torrent_add(const char *info_hash, const char *name, unsigned long long size, unsigned long piece_length,
	const char *path, int seeding, char *error, int error_size)
{
	struct torrent *torrent = NULL;
	unsigned long long existing_size;
	int index;

	if (!info_hash_parse(info_hash, (unsigned char[SHA1_DIGEST_SIZE]){ 0 }))
	{
		snprintf(error, error_size, "a bad info hash");
		return -1;
	}
	if (!name[0] || strlen(name) >= 255 || !size || size > (unsigned long long)TORRENT_MAXIMUM_PIECES * piece_length ||
		piece_length < TORRENT_MINIMUM_PIECE_LENGTH || piece_length > TORRENT_MAXIMUM_PIECE_LENGTH ||
		(piece_length & (piece_length - 1)))
	{
		snprintf(error, error_size, "a bad name, size or piece length");
		return -1;
	}
	pthread_mutex_lock(&torrent_lock);
	if (!torrent_session.running)
	{
		pthread_mutex_unlock(&torrent_lock);
		snprintf(error, error_size, "the client is not running");
		return -1;
	}
	for (index = 0; index < TORRENT_MAXIMUM_TORRENTS; index++)
	{
		if (!torrent_session.torrents[index].used)
		{
			torrent = &torrent_session.torrents[index];
			break;
		}
	}
	if (!torrent)
	{
		pthread_mutex_unlock(&torrent_lock);
		snprintf(error, error_size, "too many torrents");
		return -1;
	}
	memset(torrent, 0, sizeof(*torrent));
	torrent->used = 1;
	torrent->file = -1;
	info_hash_parse(info_hash, torrent->info_hash);
	for (index = 0; index < SHA1_DIGEST_SIZE; index++)
		sprintf(torrent->info_hash_hex + index * 2, "%02x", torrent->info_hash[index]);
	snprintf(torrent->name, sizeof(torrent->name), "%s", name);
	torrent->size = size;
	torrent->piece_length = piece_length;
	torrent->piece_count = (int)((size + piece_length - 1) / piece_length);
	snprintf(torrent->path, sizeof(torrent->path), "%s", path);
	torrent->seeding = seeding;
	torrent->added_time = torrent_now();
	torrent->rate_second = torrent->added_time / TORRENT_SECOND;
	torrent->dht_next_lookup = torrent->added_time;
	/* (the trackers' and web seeds' states as they start: no sockets, no
	piece under way) */
	torrent_trackers_stop(torrent);
	if (!file_open(torrent, !seeding, &existing_size))
	{
		torrent->used = 0;
		pthread_mutex_unlock(&torrent_lock);
		snprintf(error, error_size, "the file cannot be opened");
		return -1;
	}
	if (seeding)
	{
		if (existing_size != size)
		{
			file_close(torrent);
			torrent->used = 0;
			pthread_mutex_unlock(&torrent_lock);
			snprintf(error, error_size, "the file is not the torrent's size");
			return -1;
		}
		/* (its hashes computed, which must make the info hash) */
		checking_start(torrent);
	}
	else
	{
		/* (a partial file is checked once the hashes are known, whatever
		its length: pieces are written where they fall, so it is as long
		as the last one that came; one longer than the torrent is cut) */
		if (existing_size > size)
		{
			posix_truncate(torrent->file, (posix_ulong)(size & 0xFFFFFFFFUL), (posix_ulong)(size >> 32));
		}
		torrent->state = _torrent_state_metadata;
	}
	torrent_log("%s: added (%s, %llu bytes, %d pieces of %lu) at %s", torrent->name, seeding ? "seeding" :
		"downloading", size, torrent->piece_count, piece_length, path);
	pthread_mutex_unlock(&torrent_lock);
	return (int)(torrent - torrent_session.torrents);
}

static struct torrent *torrent_of(int handle)
{
	if (handle < 0 || handle >= TORRENT_MAXIMUM_TORRENTS || !torrent_session.torrents[handle].used)
		return NULL;
	return &torrent_session.torrents[handle];
}

void torrent_remove(int handle)
{
	struct torrent *torrent;

	pthread_mutex_lock(&torrent_lock);
	torrent = torrent_of(handle);
	if (torrent)
	{
		torrent_log("%s: removed", torrent->name);
		torrent_free(torrent);
	}
	pthread_mutex_unlock(&torrent_lock);
}

int torrent_move(int handle, const char *path)
{
	struct torrent *torrent;
	int result = 0;

	pthread_mutex_lock(&torrent_lock);
	torrent = torrent_of(handle);
	if (torrent && torrent->state == _torrent_state_seeding)
	{
		unsigned long long existing_size;

		file_close(torrent);
		if (rename(torrent->path, path) == 0)
		{
			snprintf(torrent->path, sizeof(torrent->path), "%s", path);
			torrent->seeding = 1;
			result = 1;
		}
		if (!file_open(torrent, 0, &existing_size))
			torrent_fail(torrent, "the file could not be opened again after moving it");
	}
	pthread_mutex_unlock(&torrent_lock);
	return result;
}

int torrent_status_get(int handle, struct torrent_status *status)
{
	struct torrent *torrent;
	int seeds = 0;

	memset(status, 0, sizeof(*status));
	pthread_mutex_lock(&torrent_lock);
	torrent = torrent_of(handle);
	if (!torrent)
	{
		pthread_mutex_unlock(&torrent_lock);
		return 0;
	}
	status->state = torrent->state;
	status->total_bytes = torrent->size;
	if (torrent->state == _torrent_state_seeding)
		status->have_bytes = torrent->size;
	else if (torrent->piece_hashes)
	{
		int piece;

		for (piece = 0; piece < torrent->piece_count; piece++)
		{
			if (torrent_has_piece(torrent, piece))
				status->have_bytes += (unsigned long long)torrent_piece_size(torrent, piece);
		}
	}
	else if (torrent->web_whole > 0)
	{
		int piece;

		for (piece = 0; piece < torrent->piece_count; piece++)
		{
			if (bit_test(torrent->web_whole_have, piece))
				status->have_bytes += (unsigned long long)torrent_piece_size(torrent, piece);
		}
	}
	status->uploaded_bytes = torrent->uploaded;
	status->peers_connected = torrent_peers_count(torrent, &seeds);
	status->seeds_connected = seeds;
	status->peers_known = torrent->candidate_count;
	status->download_rate = rate_of(torrent->download_by_second, torrent->rate_second);
	status->upload_rate = rate_of(torrent->upload_by_second, torrent->rate_second);
	switch (torrent->state)
	{
	case _torrent_state_metadata:
		if (torrent->web_whole > 0)
			snprintf(status->detail, sizeof(status->detail), "from the web seeds (no peer yet): %d of %d pieces",
				torrent->web_whole_count, torrent->piece_count);
		else
			snprintf(status->detail, sizeof(status->detail), "looking for peers: %d connected, %d known, DHT %d nodes",
				status->peers_connected, status->peers_known, torrent_dht_node_count());
		break;
	case _torrent_state_checking:
		snprintf(status->detail, sizeof(status->detail), "checking the file: %d of %d pieces", torrent->check_piece,
			torrent->piece_count);
		break;
	case _torrent_state_downloading:
	case _torrent_state_seeding:
		snprintf(status->detail, sizeof(status->detail), "%d peers (%d seeds), %d known, DHT %d nodes",
			status->peers_connected, seeds, status->peers_known, torrent_dht_node_count());
		break;
	case _torrent_state_failed:
		snprintf(status->detail, sizeof(status->detail), "%s", torrent->failure);
		break;
	}
	pthread_mutex_unlock(&torrent_lock);
	return 1;
}

void torrent_add_peer(int handle, unsigned long address, unsigned short port)
{
	struct torrent *torrent;

	pthread_mutex_lock(&torrent_lock);
	torrent = torrent_of(handle);
	if (torrent)
		torrent_candidate_add(torrent, address, port, _torrent_source_tracker);
	pthread_mutex_unlock(&torrent_lock);
}

void torrent_set_limits(long upload_limit, long download_limit)
{
	pthread_mutex_lock(&torrent_lock);
	torrent_session.upload_limit = upload_limit > 0 ? upload_limit : 0;
	torrent_session.download_limit = download_limit > 0 ? download_limit : 0;
	pthread_mutex_unlock(&torrent_lock);
}

int torrent_port(void)
{
	int port;

	pthread_mutex_lock(&torrent_lock);
	port = torrent_session.running ? torrent_session.port : 0;
	pthread_mutex_unlock(&torrent_lock);
	return port;
}
