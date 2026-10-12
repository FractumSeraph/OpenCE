/*
MAP_SHARE_DECLINE.C

Version 27's map sharing (builds 189 and 190: port/linux/NETCODE.md), which
these builds replaced with downloads before the join
(port/linux/src/map_torrents.c), but whose version they still speak. A
machine of those builds that joins this host without the game's Custom
Edition map asks the host for it (_request, distributed message 83), and is
answered as a version 27 host that does not share maps answers it (_offer,
84, "off"), so that it tells its player at once and leaves, rather than
waiting 30 seconds for an answer. Its other map sharing messages (85, 86:
the sources, the progress; an offer, which no host of these builds asks
for) are dropped, as a message of a type these builds do not know is
(network_distributed.c). The numbers 83 to 86 are version 27's: not to be
given to other messages (network_distributed.h).
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "network_distributed.h"

/* network_server_message_handler.c's, cseries_windows.c's */
boolean network_distributed_server_send_to_machine_reliably(long machine_index, void *message, word size);
unsigned long system_milliseconds(void);

/* ---------- constants */

enum
{
	/* version 27's map sharing messages (its network_distributed.h) */
	_map_share_request = 83,
	_map_share_offer = 84,
	_map_share_sources = 85,
	_map_share_progress = 86,

	/* its offer's results: _offer_off, the host does not share maps */
	_offer_off = 1,

	/* a level name as its messages carry it */
	LEVEL_NAME_BYTES = 64,

	/* a machine's requests answered once in this long, at most (any other
	machine sends untrusted messages as fast as it likes) */
	ANSWER_INTERVAL = 5000,
};

/* ---------- globals */

/* when each machine was last answered (0: never) */
static unsigned long map_share_decline_answered[HALO_PORT_MAXIMUM_NETWORK_MACHINES];

/* ---------- structures */

/* version 27's request and offer, as its map_share.c lays them out */
struct map_share_request
{
	char level_name[LEVEL_NAME_BYTES];
	unsigned long version;
	byte wanted;
	byte pad[3];
};

struct map_share_offer
{
	char level_name[LEVEL_NAME_BYTES];
	byte result;
	byte file_count;
	byte flags;
	byte pad;
};

typedef char map_share_request_size_assert[sizeof(struct map_share_request) == 72 ? 1 : -1];
typedef char map_share_offer_size_assert[sizeof(struct map_share_offer) == 68 ? 1 : -1];

/* ---------- public code */

/* whether the message is one of version 27's map sharing messages */
boolean map_share_decline_handles(
	word const *message,
	word size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	csmemcpy(&header, message, sizeof(header));

	return header.type >= _map_share_request && header.type <= _map_share_progress;
}

/* a version 27 map sharing message from the machine (over its connection,
to this host): a request declined, anything else dropped */
void map_share_decline_handle(
	long machine_index,
	word const *message,
	word size)
{
	struct distributed_message_header header;
	struct map_share_request request;
	struct
	{
		struct distributed_message_header header;
		struct map_share_offer offer;
	} answer;

	if (size != sizeof(header) + sizeof(request))
		return;
	csmemcpy(&header, message, sizeof(header));
	if (header.type != _map_share_request)
		return;
	csmemcpy(&request, (byte const *)message + sizeof(header), sizeof(request));
	if (!memchr(request.level_name, 0, LEVEL_NAME_BYTES) || !request.level_name[0] || machine_index < 0 ||
		machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES)
	{
		return;
	}
	/* (once in ANSWER_INTERVAL: a machine's player is told at once, and a
	flood of requests costs nothing) */
	if (map_share_decline_answered[machine_index] &&
		system_milliseconds() - map_share_decline_answered[machine_index] < ANSWER_INTERVAL)
	{
		return;
	}
	map_share_decline_answered[machine_index] = system_milliseconds() | 1;
	/* (as version 27's map_share_fill_header fills it) */
	csmemset(&answer, 0, sizeof(answer));
	build_message_header(&answer.header.header, (word)sizeof(answer), 2, 0);
	answer.header.type = _map_share_offer;
	answer.header.count = 1;
	answer.header.game_time = game_in_progress() ? game_time_get() : 0;
	csmemcpy(answer.offer.level_name, request.level_name, LEVEL_NAME_BYTES);
	answer.offer.result = _offer_off;
	network_distributed_server_send_to_machine_reliably(machine_index, &answer, (word)sizeof(answer));
	/* (the name as the machine sent it, its unprintable characters as ?) */
	{
		char name[LEVEL_NAME_BYTES];
		short index;

		for (index = 0; index < LEVEL_NAME_BYTES - 1 && request.level_name[index]; index++)
		{
			char c = request.level_name[index];

			name[index] = c >= 0x20 && c < 0x7F ? c : '?';
		}
		name[index] = 0;
		error(_error_silent, "map sharing: machine #%ld asks for %s (a version 27 build): declined, as these builds "
			"do not share maps (its player downloads it before joining with this build)", machine_index, name);
	}

	return;
}
