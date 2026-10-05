/* The server browser's public games (Join Game > Server Browser) in a browser.

Native hosts list their public games on internet play's MQTT brokers
(port/linux/src/p2p_lobby.c). A page cannot reach those, and the browser
build runs without p2p.c's thread (HALO_NET_ONLINE is off: no UDP), so:
- the site's server subscribes for the page and relays the listings as they
  came (HaloWeb/server/public-games.mjs; online_client.js polls it while the
  server browser is open and hands each one to web_public_games_heard);
- p2p_lobby.c takes them as from a broker, checks their signatures and
  lists them (web_public_games_update stands in for the p2p thread's pass);
- joining one (p2p_join_invite) hands its halo://join invite to the page,
  which joins it through the native gateway like a pasted link. */

#include "p2p_internal.h"

#include <emscripten/emscripten.h>
#include <string.h>

enum
{
	SLOT_TEXT_SIZE = 2 * P2P_KEY_HASH_SIZE,
	MAXIMUM_PAYLOAD = 512,
	INVITE_BYTES = P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE,
};

/* the page writes a slot's key hash (in hex) and the listing here, then
calls web_public_games_heard: only the page's thread uses it */
static unsigned char heard_buffer[SLOT_TEXT_SIZE + MAXIMUM_PAYLOAD];

EMSCRIPTEN_KEEPALIVE unsigned char *web_public_games_buffer(void)
{
	return heard_buffer;
}

EMSCRIPTEN_KEEPALIVE int web_public_games_buffer_size(void)
{
	return (int)sizeof(heard_buffer);
}

/* whether the server browser is open (and so wants listings) */
EMSCRIPTEN_KEEPALIVE int web_public_games_browsing(void)
{
	return p2p_lobby_browsing();
}

EMSCRIPTEN_KEEPALIVE void web_public_games_heard(int size, int retained)
{
	char slot[SLOT_TEXT_SIZE + 1];

	if (size <= 0 || size > MAXIMUM_PAYLOAD)
		return;
	memcpy(slot, heard_buffer, SLOT_TEXT_SIZE);
	slot[SLOT_TEXT_SIZE] = 0;
	pthread_mutex_lock(&p2p_lock);
	p2p_lobby_slot_heard(slot, heard_buffer + SLOT_TEXT_SIZE, size, retained != 0);
	pthread_mutex_unlock(&p2p_lock);
}

/* the listings heard, their signatures checked (a few each call, as the p2p
thread's pass does), and those not heard for a while dropped */
EMSCRIPTEN_KEEPALIVE void web_public_games_update(void)
{
	pthread_mutex_lock(&p2p_lock);
	p2p_lobby_update(NULL, 0, 0);
	pthread_mutex_unlock(&p2p_lock);
}

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* p2p_join_invite's, in a browser: the invite to the page (its bytes, as
eight numbers: the game's thread cannot hand the page a string it keeps) */
int web_join_invite(const char *text)
{
	static const char prefix[] = "halo://join/";
	unsigned char bytes[INVITE_BYTES];
	unsigned long words[INVITE_BYTES / 4];
	int index;

	if (!text || strncmp(text, prefix, sizeof(prefix) - 1) || strlen(text) != sizeof(prefix) - 1 + 2 * INVITE_BYTES)
		return 0;
	text += sizeof(prefix) - 1;
	for (index = 0; index < INVITE_BYTES; index++)
	{
		int high = hex_digit(text[2 * index]), low = hex_digit(text[2 * index + 1]);

		if (high < 0 || low < 0)
			return 0;
		bytes[index] = (unsigned char)(high << 4 | low);
	}
	for (index = 0; index < INVITE_BYTES / 4; index++)
	{
		words[index] = (unsigned long)bytes[4 * index] << 24 | (unsigned long)bytes[4 * index + 1] << 16 |
			(unsigned long)bytes[4 * index + 2] << 8 | bytes[4 * index + 3];
	}
	MAIN_THREAD_ASYNC_EM_ASM({
		var word = function(value) { return ((value >>> 0) + 0x100000000).toString(16).slice(1); };
		var hex = word($0) + word($1) + word($2) + word($3) + word($4) + word($5) + word($6) + word($7);
		if (typeof window !== "undefined" && window.HaloOnline) window.HaloOnline.join("halo://join/" + hex);
	}, words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7]);
	return 1;
}
