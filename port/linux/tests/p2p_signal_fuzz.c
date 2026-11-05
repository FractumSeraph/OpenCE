/*
P2P_SIGNAL_FUZZ.C

libFuzzer's entry for what the signalling brokers send a machine
(port/linux/src/p2p_signal.c, included whole to reach its readers;
tools/test_host_fuzz.py builds it). The first byte of an input says how the
rest arrives: 0, as the bytes of a broker's connection (MQTT 5 or 3.1.1,
packets cut anywhere); 1, 2 and 3, as a message published on the host's
topic, the prober's reply topic and the joiner's, with the sealing taken
off (a message nobody opened is dropped before it is read, which no input
can pass: the harness opens anything, and takes any proof for right, so
that the readers behind them are reached).

Port (this fork): its p2p_signal.c is OpenCE's, whose joiner asks the
invite's host and its game's other players (signalling.joins, the host
joins[0]), and whose offers carry each machine's WebRTC; a joiner's answers
are those to joins[0]'s request.
*/

#define p2p_open fuzz_open
#define p2p_equal fuzz_equal

#include "../src/p2p_signal.c"

#include <stdint.h>

#undef p2p_open
#undef p2p_equal

static unsigned long clock_now = 5000000;

unsigned long p2p_now(void)
{
	return clock_now;
}

int fuzz_open(const unsigned char *key, const unsigned char *sealed, int size, unsigned char *plaintext)
{
	(void)key;
	if (size < P2P_SEAL_OVERHEAD)
		return -1;
	memcpy(plaintext, sealed + P2P_NONCE_SIZE, (size_t)(size - P2P_SEAL_OVERHEAD));
	return size - P2P_SEAL_OVERHEAD;
}

int fuzz_equal(const void *first, const void *second, int size)
{
	(void)first;
	(void)second;
	(void)size;
	return 1;
}

void posix_random_bytes(void *buffer, unsigned long size)
{
	memset(buffer, 0x21, size);
}

void platform_log(const char *format, ...)
{
	(void)format;
}

int posix_socket_last_error(void)
{
	return WSAEWOULDBLOCK;
}

int posix_socket(int family, int type, int protocol)
{
	(void)family;
	(void)type;
	(void)protocol;
	return 3;
}

int posix_socket_close(int socket)
{
	(void)socket;
	return 0;
}

int posix_socket_connect(int socket, const void *address, int address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return 0;
}

int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	(void)socket;
	(void)buffer;
	(void)flags;
	return length;
}

int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	(void)socket;
	(void)buffer;
	(void)length;
	(void)flags;
	return -1;
}

int posix_socket_set_nonblocking(int socket, int nonblocking)
{
	(void)socket;
	(void)nonblocking;
	return 0;
}

char *config_file_read(const char *path, size_t *size)
{
	(void)path;
	(void)size;
	return NULL;
}

const char *config_string(const char *name)
{
	(void)name;
	return "";
}

const unsigned char *p2p_identifier(void)
{
	static const unsigned char identifier[P2P_IDENTIFIER_SIZE] = { 9 };

	return identifier;
}

void config_folder(char *path, size_t size)
{
	(void)size;
	path[0] = 0;
}

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < size; index++)
	{
		text[2 * index] = digits[bytes[index] >> 4];
		text[2 * index + 1] = digits[bytes[index] & 15];
	}
	text[2 * size] = 0;
}

void p2p_identifier_for(const unsigned char *public_key, unsigned char *identifier)
{
	memcpy(identifier, public_key, P2P_IDENTIFIER_SIZE);
}

void p2p_identifier_from_hash(const unsigned char *hash, unsigned char *identifier)
{
	memcpy(identifier, hash, P2P_IDENTIFIER_SIZE);
}

void p2p_key_hash(const unsigned char *public_key, unsigned char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(public_key, P2P_KEY_SIZE, digest);
	memcpy(hash, digest, P2P_KEY_HASH_SIZE);
}

const unsigned char *p2p_public_key(void)
{
	static const unsigned char key[P2P_KEY_SIZE] = { 9 };

	return key;
}

int p2p_shared_secret(const unsigned char *public_key, unsigned char *shared)
{
	memcpy(shared, public_key, P2P_KEY_SIZE);
	return public_key[0] != 0xFF;
}

int p2p_local_candidates(struct p2p_candidate *candidates, int maximum_count)
{
	int index;

	for (index = 0; index < 2 && index < maximum_count; index++)
	{
		candidates[index].address = 0x0100007F + (unsigned long)index;
		candidates[index].port = 0x1234;
	}
	return index;
}

int p2p_peer_offered(const unsigned char *identifier, const unsigned char *public_key, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count, const struct p2p_webrtc *webrtc, int offered)
{
	(void)identifier;
	(void)public_key;
	(void)secret;
	(void)candidates;
	(void)count;
	(void)webrtc;
	return offered != _p2p_offered_probe;
}

int p2p_peer_turned_away(const unsigned char *identifier, int is_host)
{
	(void)is_host;
	return identifier[1] == 0xEE;
}

int p2p_peer_reoffered(const unsigned char *identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count, const struct p2p_webrtc *webrtc)
{
	(void)identifier;
	(void)secret;
	(void)candidates;
	(void)webrtc;
	return count >= 0;
}

/* (port, this fork: each offer carries this machine's WebRTC, a native
build's: its fingerprint and the addresses p2p_local_candidates gives) */
int p2p_webrtc_ready(void)
{
	return 1;
}

int p2p_webrtc_describe(const unsigned char *identifier, int proven, struct p2p_webrtc *local,
	struct p2p_candidate *candidates, int maximum_count)
{
	(void)identifier;
	(void)proven;
	memset(local, 0, sizeof(*local));
	local->kind = _p2p_webrtc_native;
	memset(local->fingerprint, 0x3C, sizeof(local->fingerprint));
	return p2p_local_candidates(candidates, maximum_count);
}

void p2p_webrtc_new_request(void)
{
}

unsigned long p2p_resolve(const char *host)
{
	(void)host;
	return 0;
}

int p2p_probe_request_heard(const unsigned char *message, int size, unsigned char *answer, unsigned char *reply)
{
	unsigned char id[P2P_PROBE_ID_SIZE], key[P2P_PROBE_KEY_SIZE];
	struct p2p_probe_address addresses[P2P_PROBE_MAXIMUM_ADDRESSES];
	int count;

	if (!p2p_probe_get_request(message, size, id, reply, addresses, &count))
		return 0;
	memset(key, 0x77, sizeof(key));
	memset(answer, 0, P2P_PROBE_MAXIMUM_MESSAGE);
	return p2p_probe_put_answer(answer, id, key, addresses, count);
}

int p2p_probe_asking_tokens(unsigned char (*tokens)[P2P_TOKEN_SIZE], int maximum)
{
	int index;

	for (index = 0; index < 2 && index < maximum; index++)
		memset(tokens[index], index, P2P_TOKEN_SIZE);
	return index;
}

void p2p_probe_answer_heard(const unsigned char *token, const unsigned char *message, int size)
{
	unsigned char id[P2P_PROBE_ID_SIZE], key[P2P_PROBE_KEY_SIZE];
	struct p2p_probe_address addresses[P2P_PROBE_MAXIMUM_ADDRESSES];
	int count, signed_size;

	(void)token;
	p2p_probe_get_answer(message, size, id, key, addresses, &count, &signed_size);
}

void p2p_lobby_slot_heard(const char *hash_text, const unsigned char *payload, int size, int retained)
{
	(void)hash_text;
	(void)payload;
	(void)size;
	(void)retained;
}

void p2p_lobby_query_heard(void)
{
}

void p2p_lobby_slot_topic(const unsigned char *key_hash, char *topic, int size)
{
	(void)key_hash;
	snprintf(topic, (size_t)size, "lobby");
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static struct broker *broker;
	unsigned char *payload;
	int mode, rounds;

	if (size < 2)
		return 0;
	mode = data[0] & 3;
	memset(&signalling, 0, sizeof(signalling));
	signalling.started = 1;
	signalling.broker_count = 1;
	broker = &signalling.brokers[0];
	broker->socket = 3;
	broker->protocol = (data[0] & 4) ? 5 : 4;
	broker->state = _broker_ready;
	broker->publish_tokens = PUBLISH_BURST;
	signalling.hosting = 1;
	memset(signalling.host_key, 3, sizeof(signalling.host_key));
	memset(signalling.nonce_key, 4, sizeof(signalling.nonce_key));
	signalling.has_nonce_key = 1;
	strcpy(signalling.host_topic, "H");
	strcpy(signalling.probe_topic, "P");
	signalling.joins[0].active = 1;
	strcpy(signalling.join_topic, "J");
	memset(signalling.join_key, 5, sizeof(signalling.join_key));
	signalling.joins[0].has_base = 0;
	signalling.key_work.left = MAXIMUM_KEY_WORK;
	signalling.unproven_answers.left = MAXIMUM_UNPROVEN_ANSWERS;
	signalling.key_work.time = signalling.unproven_answers.time = clock_now;
	clock_now = 5000000;

	if (mode == 0)
	{
		size_t at = 1, piece = data[1] ? data[1] : 1;

		while (at < size)
		{
			size_t take = size - at < piece ? size - at : piece;

			if (broker->input_size + (int)take > BUFFER_SIZE)
				break;
			memcpy(broker->input + broker->input_size, data + at, take);
			broker->input_size += (int)take;
			at += take;
			broker_parse(broker);
			if (broker->socket < 0)
				break;
		}
		return 0;
	}
	payload = malloc(P2P_SEAL_OVERHEAD + size);
	if (!payload)
		return 0;
	memset(payload, 0, P2P_SEAL_OVERHEAD);
	memcpy(payload + P2P_SEAL_OVERHEAD, data + 1, size - 1);
	if (mode == 3 && size > 1 + 2 + P2P_KEY_SIZE + NONCE_SIZE)
	{
		/* (the answer to the request the joiner made, and from the host it asked) */
		memcpy(signalling.joins[0].nonce, data + 1 + 2 + P2P_KEY_SIZE, NONCE_SIZE);
		p2p_key_hash(data + 1 + 2, signalling.joins[0].host_hash);
		memcpy(signalling.joins[0].host, data + 1 + 2, P2P_IDENTIFIER_SIZE);
	}
	for (rounds = 0; rounds < 2; rounds++)
	{
		publish_received(broker, mode == 1 ? "H" : mode == 2 ? "P" : "J", payload,
			(int)(P2P_SEAL_OVERHEAD + size - 1), 0);
		clock_now += 2000;
		if (broker->socket < 0)
			break;
	}
	free(payload);
	return 0;
}
