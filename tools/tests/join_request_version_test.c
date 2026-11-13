/* join_request_version_test.c: the join request a dedicated server's own
player sends its game (fs.00253). ChupathingyCE's dedicated server hosts a
game and then joins it itself through 127.0.0.1, a host whose version its
client was not told (network_game_client_joined_version 0xFFFF). Since
network 42 the request's version word (network_version, its last field) lies
past the Xbox's 0x70 bytes; a request cut to the 0x70 reached the host as
version 0, which it refused as too old ("join requires OpenCE network
version"), and the server re-hosted its game every 5 seconds.

Built by tools/test_join_request_version.py with the game's own packet table
and encoder (source/networking/network_messages.c, source/memory/data_*.c)
and network rules (port/linux/game/network_rules.c), as the build compiles
them, plus AddressSanitizer. It makes the request as
network_game_client_send_join_request does, its tail from
network_rules_join_request_tail_size, and reads it as
network_game_server_handle_message_client_join_game_request does: the
version pre-check (network_rules_join_request_refused), the decoder of the
client pregame class, and the version the host compares with its own.
Every `long` of the game is 32 bits: int here. */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char byte;

/* ---------- the game's (network_messages.h, network_rules.h) */

enum
{
	CLIENT_JOIN_GAME_REQUEST = 12,
	PACKET_CLASS_CLIENT_PREGAME = 3,
	/* the Xbox's request (NETWORK_RULES_JOIN_REQUEST_SIZE) */
	XBOX_REQUEST = 0x70,
	/* OpenCE's of 40 and 41: 234 bytes more, no version word */
	REQUEST_40_EXTRA = 234,
	VERSION_UNKNOWN = 0xFFFF,
};

void initialize_network_game_packets(void);
void *create_network_game_message_with_tail(int message_type, const void *message_struct, short message_struct_size,
	short tail_size);
byte decode_network_game_message(void *message_struct, const void *encoded_message, short *encoded_message_size,
	short *packet_type, short *packet_version, int expected_packet_class);
int network_rules_join_request_tail_size(unsigned version, int native);
int network_rules_join_request_refused(unsigned version, uint8_t const *tail, int tail_size);

/* (join_request_version_sizes.c, compiled with network_messages.c: the
request's size and the build's network version) */
extern const int join_request_version_test_size;
extern const int join_request_version_test_network_version;

/* ---------- what the units need of the rest of the game */

char temporary[256];
static byte captured[0x4000];
static unsigned int captured_size;

void *create_message(int type, const void *data, unsigned int data_size, void *buffer, unsigned short buffer_size)
{
	(void)type;
	if (data_size > sizeof(captured) || data_size > buffer_size)
		return NULL;
	memcpy(captured, data, data_size);
	captured_size = data_size;
	return buffer;
}

/* (an assertion is a failure here) */
void display_assert(char *information, char *file, int line, byte fatal)
{
	fprintf(stderr, "assert %s:%d %s%s\n", file, line, information ? information : "", fatal ? " (fatal)" : "");
	exit(1);
}

void release_assert_failed(const char *information, const char *file, int line, byte fatal)
{
	display_assert((char *)information, (char *)file, line, fatal);
}

void system_exit(int code)
{
	exit(code ? code : 1);
}

void error(int priority, const char *format, ...)
{
	va_list arguments;

	(void)priority;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}

unsigned int system_milliseconds(void) { return 1; }
int halo_linux_vsnprintf(char *buffer, size_t count, const char *format, va_list arguments) { return vsnprintf(buffer, count, format, arguments); }
char *csprintf(char *buffer, char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(buffer, sizeof(temporary), format, arguments);
	va_end(arguments);
	return buffer;
}
void *csmemcpy(void *destination, const void *source, unsigned int size) { return memmove(destination, source, size); }
void *csmemset(void *buffer, int c, unsigned int size) { return memset(buffer, c, size); }
char *csstrcpy(char *destination, const char *source) { return strcpy(destination, source); }
char *csstrncpy(char *s1, const char *s2, unsigned int size) { return strncpy(s1, s2, size); }

/* ---------- the test */

static int failures;

#define CHECK(condition, ...) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL: "); \
			printf(__VA_ARGS__); \
			printf("\n"); \
			failures++; \
		} \
	} while (0)

/* the request this build's client makes (network_game_client_send_join_request):
its version word, the struct's last field, the build's network version; the
other fields a pattern, which the decoder must keep */
static byte *new_request(int size)
{
	byte *request = malloc((size_t)size);
	uint16_t version = (uint16_t)join_request_version_test_network_version;
	int i;

	for (i = 0; i < size - 2; i++)
		request[i] = (byte)(0x40 + i);
	memcpy(request + size - 2, &version, sizeof(version));
	return request;
}

/* the request encoded for a host of the version, as the client sends it: the
encoded message's size (its version byte, the fields, its type byte) */
static int encode_for(byte const *request, int size, unsigned host_version)
{
	int native = size - XBOX_REQUEST;
	int tail = network_rules_join_request_tail_size(host_version, native);

	captured_size = 0;
	if (!create_network_game_message_with_tail(CLIENT_JOIN_GAME_REQUEST, request, (short)size, (short)tail))
		return -1;
	return (int)captured_size;
}

/* what the host makes of an encoded request
(network_game_server_handle_message_client_join_game_request): a request of
the Xbox's size or OpenCE 40's padded with zeros before its type byte; one
its version pre-check refuses dropped (-2); else decoded, and the network
version it read (-1: did not decode) */
static int host_reads(byte const *encoded, int encoded_size, int size, byte *decoded)
{
	byte *padded = malloc((size_t)size + 2);
	short message_size = (short)encoded_size;
	short packet_type = CLIENT_JOIN_GAME_REQUEST;
	short packet_version = 1;
	uint16_t version;
	int result;

	if (encoded_size == 1 + XBOX_REQUEST + 1 || encoded_size == 1 + XBOX_REQUEST + REQUEST_40_EXTRA + 1)
	{
		memcpy(padded, encoded, (size_t)encoded_size - 1);
		memset(padded + encoded_size - 1, 0, (size_t)(size + 2 - encoded_size));
		padded[size + 1] = encoded[encoded_size - 1];
		message_size = (short)(size + 2);
	}
	else
	{
		memcpy(padded, encoded, (size_t)(encoded_size < size + 2 ? encoded_size : size + 2));
	}
	if (message_size >= 2 + XBOX_REQUEST &&
		network_rules_join_request_refused((unsigned)join_request_version_test_network_version,
			padded + 1 + XBOX_REQUEST, message_size - 2 - XBOX_REQUEST))
	{
		free(padded);
		return -2;
	}
	memset(decoded, 0xA5, (size_t)size);
	if (!decode_network_game_message(decoded, padded, &message_size, &packet_type, &packet_version,
		PACKET_CLASS_CLIENT_PREGAME) || packet_type != CLIENT_JOIN_GAME_REQUEST)
	{
		result = -1;
	}
	else
	{
		memcpy(&version, decoded + size - 2, sizeof(version));
		result = version;
	}
	free(padded);
	return result;
}

int main(void)
{
	int size = join_request_version_test_size;
	byte *request;
	byte *decoded;
	byte *encoded;
	int encoded_size;
	int version;

	initialize_network_game_packets();
	/* (the request of 42 on: the Xbox's 0x70, a pass of 170, a proof of 64,
	the version word) */
	CHECK(size == XBOX_REQUEST + 170 + 64 + 2, "the join request is %d bytes, not 0x70 + 236", size);
	request = new_request(size);
	decoded = malloc((size_t)size);
	encoded = malloc(sizeof(captured));

	/* the dedicated server's own player, its host's version not known: the
	whole request, which the host takes as its own version */
	encoded_size = encode_for(request, size, VERSION_UNKNOWN);
	CHECK(encoded_size == 1 + size + 1,
		"to a host of a version not known (0xFFFF) the join request encodes to %d bytes, not the whole %d "
		"(cut, the network_version is lost: the server refuses its own player, fs.00253)", encoded_size, 1 + size + 1);
	if (encoded_size > 0)
	{
		memcpy(encoded, captured, (size_t)encoded_size);
		version = host_reads(encoded, encoded_size, size, decoded);
		CHECK(version == join_request_version_test_network_version,
			"the host reads the request of its own player as network version %d, not %d "
			"(\"join requires OpenCE network version\")", version, join_request_version_test_network_version);
		CHECK(memcmp(decoded, request, (size_t)size) == 0, "the decoded request is not the request sent");
	}

	/* a host of this build's own version: the whole request too */
	encoded_size = encode_for(request, size, (unsigned)join_request_version_test_network_version);
	CHECK(encoded_size == 1 + size + 1, "to a host of %d the join request encodes to %d bytes, not %d",
		join_request_version_test_network_version, encoded_size, 1 + size + 1);
	if (encoded_size > 0)
	{
		memcpy(encoded, captured, (size_t)encoded_size);
		version = host_reads(encoded, encoded_size, size, decoded);
		CHECK(version == join_request_version_test_network_version, "a host of %d reads the request as version %d",
			join_request_version_test_network_version, version);
	}

	/* (the control: the request cut to the Xbox's 0x70 for a host of 39,
	which such a host reads; a host of this build reads it as version 0 and
	refuses it, or drops it at its pre-check where the layout it announces
	has a version word: the bug above, which the checks above must see) */
	encoded_size = encode_for(request, size, 39);
	CHECK(encoded_size == 1 + XBOX_REQUEST + 1, "to a host of 39 the join request encodes to %d bytes, not %d",
		encoded_size, 1 + XBOX_REQUEST + 1);
	if (encoded_size > 0)
	{
		memcpy(encoded, captured, (size_t)encoded_size);
		version = host_reads(encoded, encoded_size, size, decoded);
		CHECK(version == 0 || version == -2,
			"a request cut to 0x70 reads as version %d, not 0 or dropped: the checks above prove nothing", version);
	}

	free(request);
	free(decoded);
	free(encoded);
	if (failures)
		return 1;
	printf("join_request_version_test: ok (network version %d, request %d bytes)\n",
		join_request_version_test_network_version, size);
	return 0;
}
