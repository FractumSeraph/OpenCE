/*
TORRENT_PEER.C

The peers of the BitTorrent client (torrent_internal.h): the peer wire
protocol (BEP 3), the extension protocol (BEP 10) with the metadata
exchange (BEP 9) and peers' exchange (BEP 11, received only).

A peer is connected to from its address (a tracker's, the DHT's or
another peer's), or accepted on the listening socket and given to the
torrent its handshake names. Each has a buffer of what it sent not yet
whole, and one of what is to be sent to it. Blocks it asks for are read from
the file as the upload limit lets them go.
*/

#include "torrent_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HANDSHAKE_SIZE 68
#define PROTOCOL_NAME "BitTorrent protocol"
#define CONNECT_TIMEOUT (10 * TORRENT_SECOND)
#define HANDSHAKE_TIMEOUT (20 * TORRENT_SECOND)
#define IDLE_TIMEOUT (2 * TORRENT_MINUTE)
#define KEEP_ALIVE_INTERVAL (90 * TORRENT_SECOND)
#define SNUB_TIMEOUT (30 * TORRENT_SECOND)
#define CHOKE_INTERVAL (10 * TORRENT_SECOND)
#define UNCHOKED_PEERS 4
#define CONNECT_INTERVAL 250
#define RETRY_INTERVAL (60 * TORRENT_SECOND)
#define MAXIMUM_FAILURES 3
#define MAXIMUM_REQUEST_LENGTH (128 * 1024)
#define INCOMING_PENDING 16

/* our ids for the extensions */
#define EXTENSION_HANDSHAKE 0
#define OUR_UT_METADATA 1
#define OUR_UT_PEX 2

enum
{
	_message_choke,
	_message_unchoke,
	_message_interested,
	_message_not_interested,
	_message_have,
	_message_bitfield,
	_message_request,
	_message_piece,
	_message_cancel,
	_message_extended = 20,
};

/* an incoming connection, until its handshake names its torrent */
struct incoming
{
	int used;
	int socket;
	unsigned long address;
	unsigned short port;
	unsigned long started;
	unsigned char in[HANDSHAKE_SIZE];
	int in_size;
};

static struct incoming incomings[INCOMING_PENDING];

static int bit_test(const unsigned char *bits, int index)
{
	return (bits[index >> 3] & (0x80 >> (index & 7))) != 0;
}

static void bit_set(unsigned char *bits, int index)
{
	bits[index >> 3] |= (unsigned char)(0x80 >> (index & 7));
}

/* ---------- sending */

static int out_room(const struct torrent_peer *peer)
{
	return TORRENT_SEND_BUFFER - peer->out_size;
}

static int send_bytes(struct torrent_peer *peer, const void *bytes, int size)
{
	if (size > out_room(peer))
		return 0;
	memcpy(peer->out + peer->out_size, bytes, (size_t)size);
	peer->out_size += size;
	return 1;
}

/* a message of id and payload (NULL: none) */
static int send_message(struct torrent_peer *peer, int id, const void *payload, int size)
{
	unsigned char header[5];

	if (5 + size > out_room(peer))
		return 0;
	torrent_put_long(header, (unsigned long)(size + 1));
	header[4] = (unsigned char)id;
	send_bytes(peer, header, 5);
	if (size)
		send_bytes(peer, payload, size);
	return 1;
}

static void send_keep_alive(struct torrent_peer *peer)
{
	unsigned char zero[4] = { 0, 0, 0, 0 };

	send_bytes(peer, zero, 4);
}

static void send_handshake(struct torrent *torrent, struct torrent_peer *peer)
{
	unsigned char handshake[HANDSHAKE_SIZE];

	memset(handshake, 0, sizeof(handshake));
	handshake[0] = 19;
	memcpy(handshake + 1, PROTOCOL_NAME, 19);
	/* the extension protocol, and the DHT */
	handshake[20 + 5] = 0x10;
	handshake[20 + 7] = torrent_session.dht ? 0x01 : 0;
	memcpy(handshake + 28, torrent->info_hash, SHA1_DIGEST_SIZE);
	memcpy(handshake + 48, torrent_session.peer_id, 20);
	send_bytes(peer, handshake, sizeof(handshake));
	peer->handshake_sent = 1;
}

static void send_extended(struct torrent_peer *peer, int id, const void *payload, int size)
{
	unsigned char header[6];

	if (6 + size > out_room(peer))
		return;
	torrent_put_long(header, (unsigned long)(size + 2));
	header[4] = _message_extended;
	header[5] = (unsigned char)id;
	send_bytes(peer, header, 6);
	send_bytes(peer, payload, size);
}

static void send_extension_handshake(struct torrent *torrent, struct torrent_peer *peer)
{
	unsigned char buffer[256];
	struct bencode_writer writer;

	bencode_writer_start(&writer, buffer, sizeof(buffer));
	bencode_write_dictionary_start(&writer);
	bencode_write_text(&writer, "m");
	bencode_write_dictionary_start(&writer);
	bencode_write_text(&writer, "ut_metadata");
	bencode_write_integer(&writer, OUR_UT_METADATA);
	bencode_write_text(&writer, "ut_pex");
	bencode_write_integer(&writer, OUR_UT_PEX);
	bencode_write_end(&writer);
	if (torrent->piece_hashes && torrent->metadata)
	{
		bencode_write_text(&writer, "metadata_size");
		bencode_write_integer(&writer, torrent->metadata_size);
	}
	bencode_write_text(&writer, "p");
	bencode_write_integer(&writer, torrent_session.port);
	bencode_write_text(&writer, "reqq");
	bencode_write_integer(&writer, TORRENT_MAXIMUM_QUEUED_REQUESTS);
	bencode_write_text(&writer, "v");
	bencode_write_text(&writer, "OpenCE");
	bencode_write_end(&writer);
	if (!writer.overflow)
		send_extended(peer, EXTENSION_HANDSHAKE, buffer, writer.size);
}

static void send_bitfield(struct torrent *torrent, struct torrent_peer *peer)
{
	if (torrent->piece_hashes && torrent->have_count)
		send_message(peer, _message_bitfield, torrent->have, (torrent->piece_count + 7) / 8);
}

static void send_have(struct torrent_peer *peer, int piece)
{
	unsigned char payload[4];

	torrent_put_long(payload, (unsigned long)piece);
	send_message(peer, _message_have, payload, 4);
}

static void send_request(struct torrent_peer *peer, int piece, int begin, int length)
{
	unsigned char payload[12];

	torrent_put_long(payload, (unsigned long)piece);
	torrent_put_long(payload + 4, (unsigned long)begin);
	torrent_put_long(payload + 8, (unsigned long)length);
	send_message(peer, _message_request, payload, 12);
}

static void send_metadata_message(struct torrent_peer *peer, int type, int piece, int total_size,
	const unsigned char *data, int size)
{
	unsigned char buffer[TORRENT_METADATA_PIECE + 128];
	struct bencode_writer writer;

	bencode_writer_start(&writer, buffer, sizeof(buffer));
	bencode_write_dictionary_start(&writer);
	bencode_write_text(&writer, "msg_type");
	bencode_write_integer(&writer, type);
	bencode_write_text(&writer, "piece");
	bencode_write_integer(&writer, piece);
	if (type == 1)
	{
		bencode_write_text(&writer, "total_size");
		bencode_write_integer(&writer, total_size);
	}
	bencode_write_end(&writer);
	if (type == 1)
		bencode_write_raw(&writer, data, size);
	if (!writer.overflow)
		send_extended(peer, peer->ut_metadata, buffer, writer.size);
}

/* ---------- the peers' state */

static void requests_drop(struct torrent *torrent, struct torrent_peer *peer)
{
	int index;

	for (index = 0; index < peer->request_count; index++)
		torrent_block_unrequested(torrent, peer->requests[index].piece, peer->requests[index].begin);
	peer->request_count = 0;
}

void torrent_peer_close(struct torrent *torrent, struct torrent_peer *peer, const char *reason)
{
	char text[32];
	int index;

	if (!peer->used)
		return;
	if (reason)
	{
		torrent_log("%s: peer %s %s", torrent->name, torrent_address_text(peer->address, peer->port, text), reason);
	}
	requests_drop(torrent, peer);
	if (peer->metadata_piece >= 0)
		peer->metadata_piece = -1;
	posix_socket_close(peer->socket);
	for (index = 0; index < torrent->candidate_count; index++)
	{
		struct torrent_candidate *candidate = &torrent->candidates[index];

		if (candidate->address == peer->address && candidate->port == peer->port)
		{
			candidate->connected = 0;
			if (peer->state != _torrent_peer_connected)
				candidate->failures++;
			else
				candidate->failures = 0;
		}
	}
	memset(peer, 0, sizeof(*peer));
	peer->socket = -1;
}

void torrent_peers_close_all(struct torrent *torrent)
{
	int index;

	for (index = 0; index < TORRENT_MAXIMUM_PEERS; index++)
		torrent_peer_close(torrent, &torrent->peers[index], NULL);
}

int torrent_peers_count(const struct torrent *torrent, int *seeds)
{
	int index;
	int count = 0;

	*seeds = 0;
	for (index = 0; index < TORRENT_MAXIMUM_PEERS; index++)
	{
		const struct torrent_peer *peer = &torrent->peers[index];

		if (peer->used && peer->state == _torrent_peer_connected)
		{
			count++;
			if (torrent->piece_count && peer->have_count >= torrent->piece_count)
				(*seeds)++;
		}
	}
	return count;
}

static int peers_connected_everywhere(void)
{
	int index;
	int count = 0;

	for (index = 0; index < TORRENT_MAXIMUM_TORRENTS; index++)
	{
		int peer;

		if (!torrent_session.torrents[index].used)
			continue;
		for (peer = 0; peer < TORRENT_MAXIMUM_PEERS; peer++)
			count += torrent_session.torrents[index].peers[peer].used;
	}
	return count;
}

static struct torrent_peer *peer_new(struct torrent *torrent)
{
	int index;

	for (index = 0; index < TORRENT_MAXIMUM_PEERS; index++)
	{
		if (!torrent->peers[index].used)
		{
			struct torrent_peer *peer = &torrent->peers[index];

			memset(peer, 0, sizeof(*peer));
			peer->used = 1;
			peer->socket = -1;
			peer->am_choking = 1;
			peer->peer_choking = 1;
			peer->metadata_piece = -1;
			peer->started = peer->last_received = peer->last_sent = torrent_now();
			return peer;
		}
	}
	return NULL;
}

static int peer_known(const struct torrent *torrent, unsigned long address)
{
	int index;

	for (index = 0; index < TORRENT_MAXIMUM_PEERS; index++)
	{
		if (torrent->peers[index].used && torrent->peers[index].address == address)
			return 1;
	}
	return 0;
}

/* a connection to a candidate not tried lately */
static void connect_to_candidates(struct torrent *torrent)
{
	int seeds;
	int connected = torrent_peers_count(torrent, &seeds);
	int index;
	int attempts = 0;

	if (torrent->state == _torrent_state_seeding || torrent->state == _torrent_state_failed ||
		torrent->state == _torrent_state_checking)
	{
		return;
	}
	if (!torrent_elapsed(torrent->last_connect_time, CONNECT_INTERVAL))
		return;
	for (index = 0; index < torrent->candidate_count && attempts < 2; index++)
	{
		struct torrent_candidate *candidate = &torrent->candidates[index];
		struct torrent_peer *peer;
		struct sockaddr_in address;

		if (connected + attempts >= TORRENT_MAXIMUM_PEERS - 4 ||
			peers_connected_everywhere() >= torrent_session.maximum_peers)
		{
			break;
		}
		/* (tried again soon the first time it fails: a host that was still
		checking its file, say; later less often) */
		if (candidate->connected || candidate->failures >= MAXIMUM_FAILURES ||
			(candidate->last_tried && !torrent_elapsed(candidate->last_tried,
			RETRY_INTERVAL * (candidate->failures + 1) / MAXIMUM_FAILURES)) ||
			peer_known(torrent, candidate->address))
		{
			continue;
		}
		candidate->last_tried = torrent_now();
		peer = peer_new(torrent);
		if (!peer)
			break;
		peer->socket = posix_socket(AF_INET, SOCK_STREAM, 0);
		if (peer->socket < 0 || posix_socket_set_nonblocking(peer->socket, 1) < 0)
		{
			if (peer->socket >= 0)
				posix_socket_close(peer->socket);
			memset(peer, 0, sizeof(*peer));
			break;
		}
		posix_socket_set_nodelay(peer->socket);
		memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = candidate->port;
		address.sin_addr.s_addr = candidate->address;
		peer->address = candidate->address;
		peer->port = candidate->port;
		peer->state = _torrent_peer_connecting;
		if (posix_socket_connect(peer->socket, &address, sizeof(address)) < 0 && !torrent_would_block())
		{
			posix_socket_close(peer->socket);
			memset(peer, 0, sizeof(*peer));
			candidate->failures++;
			continue;
		}
		candidate->connected = 1;
		torrent->last_connect_time = torrent_now();
		attempts++;
	}
}

/* ---------- the extension protocol */

static void extension_handshake_received(struct torrent *torrent, struct torrent_peer *peer,
	const unsigned char *payload, int size)
{
	struct bencode_document *document = malloc(sizeof(*document));
	int m;

	(void)torrent;
	if (!document)
		return;
	if (bencode_parse(document, payload, size) > 0 && document->nodes[0].type == _bencode_dictionary)
	{
		m = bencode_find(document, 0, "m");
		peer->ut_metadata = (int)bencode_integer(document, bencode_find(document, m, "ut_metadata"), 0);
		peer->ut_pex = (int)bencode_integer(document, bencode_find(document, m, "ut_pex"), 0);
		peer->metadata_size = (int)bencode_integer(document, bencode_find(document, 0, "metadata_size"), 0);
		if (peer->ut_metadata < 0 || peer->ut_metadata > 255)
			peer->ut_metadata = 0;
		if (peer->ut_pex < 0 || peer->ut_pex > 255)
			peer->ut_pex = 0;
		if (peer->metadata_size < 0 || peer->metadata_size > TORRENT_MAXIMUM_METADATA)
			peer->metadata_size = 0;
	}
	free(document);
}

static void metadata_message_received(struct torrent *torrent, struct torrent_peer *peer,
	const unsigned char *payload, int size)
{
	struct bencode_document *document = malloc(sizeof(*document));
	int header_size;
	int type, piece, total_size;

	if (!document)
		return;
	header_size = bencode_parse(document, payload, size);
	if (header_size <= 0 || document->nodes[0].type != _bencode_dictionary)
	{
		free(document);
		return;
	}
	type = (int)bencode_integer(document, bencode_find(document, 0, "msg_type"), -1);
	piece = (int)bencode_integer(document, bencode_find(document, 0, "piece"), -1);
	total_size = (int)bencode_integer(document, bencode_find(document, 0, "total_size"), 0);
	free(document);
	switch (type)
	{
	case 0:
		/* a request: a piece of our metadata, or a rejection */
		/* (the piece counted before it is multiplied: a large one would
		overflow to an offset outside the metadata) */
		if (torrent->metadata && torrent->piece_hashes && piece >= 0 &&
			piece < (torrent->metadata_size + TORRENT_METADATA_PIECE - 1) / TORRENT_METADATA_PIECE &&
			peer->ut_metadata)
		{
			int offset = piece * TORRENT_METADATA_PIECE;
			int length = torrent->metadata_size - offset;

			if (length > TORRENT_METADATA_PIECE)
				length = TORRENT_METADATA_PIECE;
			send_metadata_message(peer, 1, piece, torrent->metadata_size, torrent->metadata + offset, length);
		}
		else if (peer->ut_metadata)
		{
			send_metadata_message(peer, 2, piece, 0, NULL, 0);
		}
		break;
	case 1:
		if (piece == peer->metadata_piece)
			peer->metadata_piece = -1;
		torrent_metadata_received(torrent, piece, payload + header_size, size - header_size, total_size);
		break;
	case 2:
		if (piece == peer->metadata_piece)
			peer->metadata_piece = -1;
		/* (it has none to give: not asked again) */
		peer->ut_metadata = 0;
		break;
	}
}

static void pex_received(struct torrent *torrent, const unsigned char *payload, int size)
{
	struct bencode_document *document = malloc(sizeof(*document));
	const unsigned char *added;
	int added_length;

	if (!document)
		return;
	if (bencode_parse(document, payload, size) > 0 &&
		bencode_string(document, bencode_find(document, 0, "added"), &added, &added_length))
	{
		int index;

		for (index = 0; index + 6 <= added_length && index < 50 * 6; index += 6)
		{
			unsigned long address;
			unsigned short port;

			memcpy(&address, added + index, 4);
			memcpy(&port, added + index + 4, 2);
			torrent_candidate_add(torrent, address, port, _torrent_source_pex);
		}
	}
	free(document);
}

/* ---------- messages received */

static void handshake_done(struct torrent *torrent, struct torrent_peer *peer)
{
	peer->state = _torrent_peer_connected;
	if (!peer->handshake_sent)
		send_handshake(torrent, peer);
	if (peer->extensions)
		send_extension_handshake(torrent, peer);
	send_bitfield(torrent, peer);
}

/* the handshake (68 bytes) at the start of in: 1 if it is right for the
torrent */
static int handshake_check(const struct torrent *torrent, const unsigned char *handshake)
{
	return handshake[0] == 19 && !memcmp(handshake + 1, PROTOCOL_NAME, 19) &&
		!memcmp(handshake + 28, torrent->info_hash, SHA1_DIGEST_SIZE);
}

static void request_received(struct torrent *torrent, struct torrent_peer *peer, const unsigned char *payload)
{
	int piece = (int)torrent_get_long(payload);
	int begin = (int)torrent_get_long(payload + 4);
	int length = (int)torrent_get_long(payload + 8);
	struct torrent_request *request;

	if (peer->am_choking || !torrent_has_piece(torrent, piece) || begin < 0 || length <= 0 ||
		length > MAXIMUM_REQUEST_LENGTH || begin + length > torrent_piece_size(torrent, piece))
	{
		return;
	}
	if (peer->queued_count >= TORRENT_MAXIMUM_QUEUED_REQUESTS)
		return;
	request = &peer->queued[peer->queued_count++];
	request->piece = piece;
	request->begin = begin;
	request->length = length;
	request->time = torrent_now();
}

static void cancel_received(struct torrent_peer *peer, const unsigned char *payload)
{
	int piece = (int)torrent_get_long(payload);
	int begin = (int)torrent_get_long(payload + 4);
	int index;

	for (index = 0; index < peer->queued_count; index++)
	{
		if (peer->queued[index].piece == piece && peer->queued[index].begin == begin)
		{
			memmove(&peer->queued[index], &peer->queued[index + 1],
				(size_t)(peer->queued_count - index - 1) * sizeof(peer->queued[0]));
			peer->queued_count--;
			return;
		}
	}
}

static void piece_received(struct torrent *torrent, struct torrent_peer *peer, const unsigned char *payload,
	int size)
{
	int piece = (int)torrent_get_long(payload);
	int begin = (int)torrent_get_long(payload + 4);
	int length = size - 8;
	int index;

	for (index = 0; index < peer->request_count; index++)
	{
		if (peer->requests[index].piece == piece && peer->requests[index].begin == begin)
		{
			memmove(&peer->requests[index], &peer->requests[index + 1],
				(size_t)(peer->request_count - index - 1) * sizeof(peer->requests[0]));
			peer->request_count--;
			break;
		}
	}
	peer->downloaded += (unsigned long long)length;
	peer->snubbed = 0;
	torrent_downloaded(torrent, length);
	torrent_block_received(torrent, piece, begin, payload + 8, length);
}

static void message_received(struct torrent *torrent, struct torrent_peer *peer, int id,
	const unsigned char *payload, int size)
{
	switch (id)
	{
	case _message_choke:
		peer->peer_choking = 1;
		requests_drop(torrent, peer);
		break;
	case _message_unchoke:
		peer->peer_choking = 0;
		break;
	case _message_interested:
		peer->peer_interested = 1;
		break;
	case _message_not_interested:
		peer->peer_interested = 0;
		peer->queued_count = 0;
		break;
	case _message_have:
		if (size == 4)
		{
			int piece = (int)torrent_get_long(payload);

			if (piece >= 0 && piece < TORRENT_MAXIMUM_PIECES && !bit_test(peer->bitfield, piece))
			{
				bit_set(peer->bitfield, piece);
				peer->have_count++;
			}
		}
		break;
	case _message_bitfield:
		if (size <= (int)sizeof(peer->bitfield))
		{
			int piece;

			memset(peer->bitfield, 0, sizeof(peer->bitfield));
			memcpy(peer->bitfield, payload, (size_t)size);
			peer->have_count = 0;
			for (piece = 0; piece < size * 8; piece++)
				peer->have_count += bit_test(peer->bitfield, piece);
		}
		break;
	case _message_request:
		if (size == 12)
			request_received(torrent, peer, payload);
		break;
	case _message_piece:
		if (size > 8)
			piece_received(torrent, peer, payload, size);
		break;
	case _message_cancel:
		if (size == 12)
			cancel_received(peer, payload);
		break;
	case _message_extended:
		if (size >= 1 && peer->extensions)
		{
			if (payload[0] == EXTENSION_HANDSHAKE)
				extension_handshake_received(torrent, peer, payload + 1, size - 1);
			else if (payload[0] == OUR_UT_METADATA)
				metadata_message_received(torrent, peer, payload + 1, size - 1);
			else if (payload[0] == OUR_UT_PEX)
				pex_received(torrent, payload + 1, size - 1);
		}
		break;
	default:
		break;
	}
}

/* what came: handshake, then messages; 0 if the peer is to be closed */
static int input_parse(struct torrent *torrent, struct torrent_peer *peer)
{
	for (;;)
	{
		if (peer->state == _torrent_peer_handshake)
		{
			if (peer->in_size < HANDSHAKE_SIZE)
				return 1;
			if (!handshake_check(torrent, peer->in))
				return 0;
			/* (this machine itself, reached by its own address) */
			if (!memcmp(peer->in + 48, torrent_session.peer_id, 20))
				return 0;
			peer->extensions = (peer->in[20 + 5] & 0x10) != 0;
			memcpy(peer->id, peer->in + 48, 20);
			memmove(peer->in, peer->in + HANDSHAKE_SIZE, (size_t)(peer->in_size - HANDSHAKE_SIZE));
			peer->in_size -= HANDSHAKE_SIZE;
			handshake_done(torrent, peer);
			continue;
		}
		if (peer->in_size < 4)
			return 1;
		{
			unsigned long length = torrent_get_long(peer->in);

			if (length == 0)
			{
				/* (a keep-alive) */
				memmove(peer->in, peer->in + 4, (size_t)(peer->in_size - 4));
				peer->in_size -= 4;
				continue;
			}
			if (length > TORRENT_RECEIVE_BUFFER - 4)
				return 0;
			if (peer->in_size < 4 + (int)length)
				return 1;
			message_received(torrent, peer, peer->in[4], peer->in + 5, (int)length - 1);
			if (!peer->used)
				return 1;
			memmove(peer->in, peer->in + 4 + length, (size_t)(peer->in_size - 4 - (int)length));
			peer->in_size -= 4 + (int)length;
		}
	}
}

/* ---------- each tick */

/* a peer's requests served, as the upload limit lets them */
static void serve_requests(struct torrent *torrent, struct torrent_peer *peer)
{
	static unsigned char *buffer;

	if (!buffer)
		buffer = malloc(MAXIMUM_REQUEST_LENGTH + 13);
	while (peer->queued_count && buffer)
	{
		struct torrent_request *request = &peer->queued[0];
		int length = request->length;

		if (length + 13 > out_room(peer) || torrent_upload_allowance() < length)
			return;
		if (!torrent_has_piece(torrent, request->piece) ||
			!torrent_file_read(torrent, (unsigned long long)request->piece * torrent->piece_length +
			(unsigned long long)request->begin, buffer + 8, length))
		{
			memmove(&peer->queued[0], &peer->queued[1], (size_t)(peer->queued_count - 1) * sizeof(peer->queued[0]));
			peer->queued_count--;
			continue;
		}
		torrent_put_long(buffer, (unsigned long)request->piece);
		torrent_put_long(buffer + 4, (unsigned long)request->begin);
		send_message(peer, _message_piece, buffer, length + 8);
		peer->uploaded += (unsigned long long)length;
		torrent_uploaded(torrent, length);
		memmove(&peer->queued[0], &peer->queued[1], (size_t)(peer->queued_count - 1) * sizeof(peer->queued[0]));
		peer->queued_count--;
	}
}

/* blocks asked of a peer that lets us: the pipeline as deep as it is fast */
static void make_requests(struct torrent *torrent, struct torrent_peer *peer)
{
	int depth;
	unsigned long seconds;

	if (peer->peer_choking || !peer->am_interested || torrent->state != _torrent_state_downloading)
		return;
	if (torrent_download_allowance() < TORRENT_BLOCK_SIZE)
		return;
	seconds = (torrent_now() - peer->started) / TORRENT_SECOND;
	depth = 8 + (int)(peer->downloaded / (seconds ? seconds : 1) / 65536);
	if (depth > TORRENT_MAXIMUM_PIPELINE)
		depth = TORRENT_MAXIMUM_PIPELINE;
	if (peer->snubbed)
		depth = 1;
	while (peer->request_count < depth)
	{
		int piece, begin, length;
		struct torrent_request *request;

		if (!torrent_block_to_request(torrent, peer->bitfield, &piece, &begin, &length))
			return;
		if (12 + 5 > out_room(peer))
		{
			torrent_block_unrequested(torrent, piece, begin);
			return;
		}
		send_request(peer, piece, begin, length);
		request = &peer->requests[peer->request_count++];
		request->piece = piece;
		request->begin = begin;
		request->length = length;
		request->time = torrent_now();
	}
}

static void request_metadata(struct torrent *torrent, struct torrent_peer *peer)
{
	int piece;

	if (torrent->state != _torrent_state_metadata || !peer->ut_metadata || !peer->metadata_size)
		return;
	if (peer->metadata_piece >= 0)
	{
		if (torrent_elapsed(peer->metadata_request_time, TORRENT_METADATA_REQUEST_TIMEOUT))
			peer->metadata_piece = -1;
		return;
	}
	/* (the size the peer told starts the fetch) */
	if (!torrent_metadata_begin(torrent, peer->metadata_size))
		return;
	piece = torrent_metadata_piece_to_request(torrent);
	if (piece < 0)
		return;
	send_metadata_message(peer, 0, piece, 0, NULL, 0);
	peer->metadata_piece = piece;
	peer->metadata_request_time = torrent_now();
}

/* interest: the peer has a piece we lack */
static void update_interest(struct torrent *torrent, struct torrent_peer *peer)
{
	int interested = torrent->state == _torrent_state_downloading && torrent_wants_from(torrent, peer->bitfield);

	if (interested != peer->am_interested)
	{
		peer->am_interested = interested;
		send_message(peer, interested ? _message_interested : _message_not_interested, NULL, 0);
		if (!interested)
			requests_drop(torrent, peer);
	}
}

/* those we let download: up to UNCHOKED_PEERS of the interested, the ones
unchoked longest giving way in turn */
static void update_choking(struct torrent *torrent)
{
	int index;
	int unchoked = 0;

	if (!torrent_elapsed(torrent->choke_time, CHOKE_INTERVAL))
		return;
	torrent->choke_time = torrent_now();
	for (index = 0; index < TORRENT_MAXIMUM_PEERS; index++)
	{
		struct torrent_peer *peer = &torrent->peers[index];

		if (peer->used && peer->state == _torrent_peer_connected && !peer->am_choking)
		{
			if (!peer->peer_interested || torrent_elapsed(peer->unchoked_time, 3 * CHOKE_INTERVAL))
			{
				peer->am_choking = 1;
				peer->queued_count = 0;
				send_message(peer, _message_choke, NULL, 0);
			}
			else
			{
				unchoked++;
			}
		}
	}
	for (index = 0; index < TORRENT_MAXIMUM_PEERS && unchoked < UNCHOKED_PEERS; index++)
	{
		struct torrent_peer *peer = &torrent->peers[index];

		if (peer->used && peer->state == _torrent_peer_connected && peer->am_choking && peer->peer_interested &&
			torrent->have_count)
		{
			peer->am_choking = 0;
			peer->unchoked_time = torrent_now();
			send_message(peer, _message_unchoke, NULL, 0);
			unchoked++;
		}
	}
}

static void peer_tick(struct torrent *torrent, struct torrent_peer *peer)
{
	switch (peer->state)
	{
	case _torrent_peer_connecting:
		if (torrent_elapsed(peer->started, CONNECT_TIMEOUT))
			torrent_peer_close(torrent, peer, NULL);
		return;
	case _torrent_peer_handshake:
		if (torrent_elapsed(peer->started, HANDSHAKE_TIMEOUT))
			torrent_peer_close(torrent, peer, NULL);
		return;
	case _torrent_peer_connected:
		break;
	}
	if (torrent_elapsed(peer->last_received, IDLE_TIMEOUT))
	{
		torrent_peer_close(torrent, peer, "went quiet");
		return;
	}
	if (torrent_elapsed(peer->last_sent, KEEP_ALIVE_INTERVAL))
	{
		send_keep_alive(peer);
		peer->last_sent = torrent_now();
	}
	/* (snubbed: a request of it has had no answer for a while) */
	if (peer->request_count && torrent_elapsed(peer->requests[0].time, SNUB_TIMEOUT))
	{
		peer->snubbed = 1;
		requests_drop(torrent, peer);
	}
	update_interest(torrent, peer);
	request_metadata(torrent, peer);
	make_requests(torrent, peer);
	serve_requests(torrent, peer);
}

void torrent_peers_tick(struct torrent *torrent)
{
	int index;

	/* (an incoming connection whose handshake never came) */
	for (index = 0; index < INCOMING_PENDING; index++)
	{
		if (incomings[index].used && torrent_elapsed(incomings[index].started, HANDSHAKE_TIMEOUT))
		{
			posix_socket_close(incomings[index].socket);
			incomings[index].used = 0;
		}
	}
	connect_to_candidates(torrent);
	for (index = 0; index < TORRENT_MAXIMUM_PEERS; index++)
	{
		if (torrent->peers[index].used)
			peer_tick(torrent, &torrent->peers[index]);
	}
	update_choking(torrent);
}

void torrent_peers_have(struct torrent *torrent, int piece)
{
	int index;

	for (index = 0; index < TORRENT_MAXIMUM_PEERS; index++)
	{
		struct torrent_peer *peer = &torrent->peers[index];

		if (peer->used && peer->state == _torrent_peer_connected)
			send_have(peer, piece);
	}
}

void torrent_peers_metadata_known(struct torrent *torrent)
{
	int index;

	for (index = 0; index < TORRENT_MAXIMUM_PEERS; index++)
	{
		struct torrent_peer *peer = &torrent->peers[index];
		int piece;

		if (!peer->used)
			continue;
		peer->have_count = 0;
		for (piece = 0; piece < torrent->piece_count; piece++)
			peer->have_count += bit_test(peer->bitfield, piece);
		/* (the pieces a resumed file had: told, as the bitfield was not) */
		if (peer->state == _torrent_peer_connected)
		{
			for (piece = 0; piece < torrent->piece_count; piece++)
			{
				if (torrent_has_piece(torrent, piece))
					send_have(peer, piece);
			}
		}
	}
}

/* ---------- sockets */

void torrent_peers_sockets(int *read, int *read_count, int *write, int *write_count, int maximum)
{
	int torrent_index;

	for (torrent_index = 0; torrent_index < TORRENT_MAXIMUM_TORRENTS; torrent_index++)
	{
		struct torrent *torrent = &torrent_session.torrents[torrent_index];
		int index;

		if (!torrent->used)
			continue;
		for (index = 0; index < TORRENT_MAXIMUM_PEERS; index++)
		{
			struct torrent_peer *peer = &torrent->peers[index];

			if (!peer->used || peer->socket < 0)
				continue;
			if (*read_count < maximum)
				read[(*read_count)++] = peer->socket;
			if ((peer->state == _torrent_peer_connecting || peer->out_size) && *write_count < maximum)
				write[(*write_count)++] = peer->socket;
		}
	}
	for (torrent_index = 0; torrent_index < INCOMING_PENDING; torrent_index++)
	{
		if (incomings[torrent_index].used && *read_count < maximum)
			read[(*read_count)++] = incomings[torrent_index].socket;
	}
}

static void peer_flush(struct torrent *torrent, struct torrent_peer *peer)
{
	while (peer->out_size)
	{
		int sent = posix_socket_send(peer->socket, peer->out, peer->out_size, 0);

		if (sent < 0)
		{
			if (!torrent_would_block())
				torrent_peer_close(torrent, peer, NULL);
			return;
		}
		if (sent == 0)
			return;
		memmove(peer->out, peer->out + sent, (size_t)(peer->out_size - sent));
		peer->out_size -= sent;
		peer->last_sent = torrent_now();
	}
}

static void peer_ready(struct torrent *torrent, struct torrent_peer *peer, int writable)
{
	if (peer->state == _torrent_peer_connecting)
	{
		/* (connected, or failed: told by what a send does) */
		int error = 0;
		int length = sizeof(error);

		if (posix_socket_getsockopt(peer->socket, SOL_SOCKET, TORRENT_SO_ERROR, &error, &length) < 0 || error)
		{
			torrent_peer_close(torrent, peer, NULL);
			return;
		}
		peer->state = _torrent_peer_handshake;
		peer->started = torrent_now();
		send_handshake(torrent, peer);
		peer_flush(torrent, peer);
		return;
	}
	if (writable)
	{
		peer_flush(torrent, peer);
		return;
	}
	for (;;)
	{
		int room = TORRENT_RECEIVE_BUFFER - peer->in_size;
		int received;

		if (!room)
			break;
		received = posix_socket_recv(peer->socket, peer->in + peer->in_size, room, 0);
		if (received < 0)
		{
			if (!torrent_would_block())
				torrent_peer_close(torrent, peer, NULL);
			break;
		}
		if (received == 0)
		{
			torrent_peer_close(torrent, peer, NULL);
			return;
		}
		peer->in_size += received;
		peer->last_received = torrent_now();
		if (!input_parse(torrent, peer))
		{
			torrent_peer_close(torrent, peer, "sent something wrong");
			return;
		}
		if (!peer->used)
			return;
		if (received < room)
			break;
	}
	peer_flush(torrent, peer);
}

/* an incoming connection's handshake, which names its torrent */
static void incoming_ready(struct incoming *incoming)
{
	int received = posix_socket_recv(incoming->socket, incoming->in + incoming->in_size,
		HANDSHAKE_SIZE - incoming->in_size, 0);
	int index;

	if (received < 0 && torrent_would_block())
		return;
	if (received <= 0)
	{
		posix_socket_close(incoming->socket);
		incoming->used = 0;
		return;
	}
	incoming->in_size += received;
	if (incoming->in_size < HANDSHAKE_SIZE)
		return;
	for (index = 0; index < TORRENT_MAXIMUM_TORRENTS; index++)
	{
		struct torrent *torrent = &torrent_session.torrents[index];
		struct torrent_peer *peer;

		/* (one still checking its file takes the peer: its pieces are
		told as haves once checked, torrent_peers_metadata_known) */
		if (!torrent->used || torrent->state == _torrent_state_failed || !handshake_check(torrent, incoming->in))
			continue;
		peer = peers_connected_everywhere() < torrent_session.maximum_peers ? peer_new(torrent) : NULL;
		if (!peer)
			break;
		peer->socket = incoming->socket;
		peer->address = incoming->address;
		peer->port = incoming->port;
		peer->incoming = 1;
		peer->state = _torrent_peer_handshake;
		memcpy(peer->in, incoming->in, HANDSHAKE_SIZE);
		peer->in_size = HANDSHAKE_SIZE;
		incoming->used = 0;
		torrent_candidate_add(torrent, incoming->address, incoming->port, _torrent_source_incoming);
		if (!input_parse(torrent, peer))
			torrent_peer_close(torrent, peer, NULL);
		else
			peer_flush(torrent, peer);
		return;
	}
	posix_socket_close(incoming->socket);
	incoming->used = 0;
}

void torrent_peer_accept(int socket, unsigned long address, unsigned short port)
{
	int index;
	int oldest = 0;

	for (index = 0; index < INCOMING_PENDING; index++)
	{
		if (!incomings[index].used)
			break;
		if (incomings[index].started < incomings[oldest].started)
			oldest = index;
	}
	if (index == INCOMING_PENDING)
	{
		index = oldest;
		posix_socket_close(incomings[index].socket);
	}
	memset(&incomings[index], 0, sizeof(incomings[index]));
	incomings[index].used = 1;
	incomings[index].socket = socket;
	incomings[index].address = address;
	incomings[index].port = port;
	incomings[index].started = torrent_now();
}

int torrent_peer_socket_ready(int socket, int writable)
{
	int torrent_index;

	for (torrent_index = 0; torrent_index < TORRENT_MAXIMUM_TORRENTS; torrent_index++)
	{
		struct torrent *torrent = &torrent_session.torrents[torrent_index];
		int index;

		if (!torrent->used)
			continue;
		for (index = 0; index < TORRENT_MAXIMUM_PEERS; index++)
		{
			struct torrent_peer *peer = &torrent->peers[index];

			if (peer->used && peer->socket == socket)
			{
				peer_ready(torrent, peer, writable);
				return 1;
			}
		}
	}
	for (torrent_index = 0; torrent_index < INCOMING_PENDING; torrent_index++)
	{
		if (incomings[torrent_index].used && incomings[torrent_index].socket == socket)
		{
			if (!writable)
				incoming_ready(&incomings[torrent_index]);
			return 1;
		}
	}
	return 0;
}
