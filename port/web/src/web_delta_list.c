/* Delta List in the browser: ChupathingyCE's game list (halo.milenko.org's
GET /v1/games; their docs/delta.md, "Delta List"), for the in-game Server
Browser.

The list has every public game its site knows of: OpenCE's own (from the
same brokers as the listings this build checks: web_public_games.c) and the
games ChupathingyCE's builds and servers announce to it, with what the
listings do not say: the host's platform, whether it is a dedicated server
(one of ChupathingyCE's own, "official"), whether it speaks Delta, the
score to win and who is playing. The site has no CORS, so the site's server
asks it (server/delta-list.mjs, GET /v1/delta/games); online_client.js
reads that while the Server Browser is open and hands each game here, one
at a time between web_delta_games_begin and _end.

The Server Browser (menu_functions.c, HALO_WEB) shows the listings as ever,
and from here: a game the listings do not have (one only announced to the
site), joined by its invite like any; and for every game, by its invite's
token, the host line, the players line and the score to win. Nothing here
is trusted for anything but showing: a listing's own signed details win,
and joining checks the host as any join does. */

#include "p2p.h"
#include "delta.h"
#include "web_delta_list.h"

#include <emscripten/emscripten.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

enum
{
	MAXIMUM_LISTED = 128,
	INVITE_DIGITS = 64,
	/* (the names of who is playing, UTF-8, each ended by a 0x1E) */
	ROSTER_BYTES = 256,
	TEXT_BYTES = 512,
};

enum
{
	HOSTING_UNKNOWN,
	HOSTING_PLAYER,
	HOSTING_DEDICATED,
	/* one of ChupathingyCE's own dedicated servers */
	HOSTING_OFFICIAL,
};

enum
{
	PROTOCOL_UNKNOWN,
	PROTOCOL_DELTA,
	PROTOCOL_OPENCE,
};

struct listed_game
{
	char invite[INVITE_DIGITS + 1];
	char name[P2P_LISTING_NAME_SIZE + 1];
	char map[P2P_LISTING_MAP_SIZE + 1];
	char gametype[P2P_LISTING_GAMETYPE_SIZE + 1];
	char roster[ROSTER_BYTES];
	int roster_count;
	int engine, players, maximum_players, open, in_progress, teams;
	int version, score_limit;
	/* (enum delta_platform; -1 not said) */
	int platform;
	int hosting, protocol;
};

static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static struct listed_game listed[MAXIMUM_LISTED], coming[MAXIMUM_LISTED];
static int listed_count, coming_count;
/* the page writes a game's invite, name, map, gametype and players (each
ended by a zero; the players' names by a 0x1E each) here before
web_delta_games_add */
static char game_text[TEXT_BYTES];

EMSCRIPTEN_KEEPALIVE char *web_delta_games_buffer(void)
{
	return game_text;
}

EMSCRIPTEN_KEEPALIVE int web_delta_games_buffer_size(void)
{
	return (int)sizeof(game_text);
}

EMSCRIPTEN_KEEPALIVE void web_delta_games_begin(void)
{
	coming_count = 0;
}

/* (the next text of game_text; NULL past its end) */
static const char *next_text(const char **cursor, const char *end)
{
	const char *text = *cursor;

	if (text >= end)
		return NULL;
	while (*cursor < end && **cursor)
		(*cursor)++;
	if (*cursor >= end)
		return NULL;
	(*cursor)++;
	return text;
}

static int is_hex(const char *text, int length)
{
	int index;

	for (index = 0; index < length; index++)
	{
		char c = text[index];

		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			return 0;
	}
	return text[length] == 0;
}

/* printable ASCII alone, as the listings' text is ('?' for the rest) */
static void copy_ascii(char *to, int size, const char *from)
{
	int index = 0;

	for (; *from && index < size - 1; from++)
	{
		unsigned char c = (unsigned char)*from;

		/* (a UTF-8 sequence: one '?') */
		if (c >= 0x80 && c < 0xC0)
			continue;
		to[index++] = c >= 0x20 && c < 0x7F ? (char)c : '?';
	}
	to[index] = 0;
}

static int clamp(int value, int minimum, int maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

EMSCRIPTEN_KEEPALIVE void web_delta_games_add(int engine, int players, int maximum_players, int open, int in_progress,
	int teams, int version, int score_limit, int platform, int hosting, int protocol, int roster_count)
{
	const char *cursor = game_text, *end = game_text + sizeof(game_text);
	const char *invite = next_text(&cursor, end), *name = next_text(&cursor, end);
	const char *map = next_text(&cursor, end), *gametype = next_text(&cursor, end);
	const char *roster = next_text(&cursor, end);
	struct listed_game *game;
	const char *leaf;

	if (!invite || !name || !map || !gametype || !roster || strlen(invite) != INVITE_DIGITS ||
		!is_hex(invite, INVITE_DIGITS) || coming_count >= MAXIMUM_LISTED)
	{
		return;
	}
	game = &coming[coming_count++];
	memset(game, 0, sizeof(*game));
	snprintf(game->invite, sizeof(game->invite), "%s", invite);
	copy_ascii(game->name, sizeof(game->name), name);
	/* (an announced game's map is its scenario's path: its name is the last
	part, as the listings have it) */
	leaf = strrchr(map, '\\');
	copy_ascii(game->map, sizeof(game->map), leaf ? leaf + 1 : map);
	copy_ascii(game->gametype, sizeof(game->gametype), gametype);
	snprintf(game->roster, sizeof(game->roster), "%s", roster);
	game->roster_count = clamp(roster_count, 0, 255);
	game->engine = clamp(engine, 0, 5);
	game->players = clamp(players, 0, 255);
	game->maximum_players = clamp(maximum_players, 0, 255);
	game->open = open != 0;
	game->in_progress = in_progress != 0;
	game->teams = teams != 0;
	game->version = clamp(version, 0, 65535);
	game->score_limit = clamp(score_limit, 0, 32767);
	game->platform = platform >= 0 && platform < NUMBER_OF_DELTA_PLATFORMS ? platform : -1;
	game->hosting = clamp(hosting, HOSTING_UNKNOWN, HOSTING_OFFICIAL);
	game->protocol = clamp(protocol, PROTOCOL_UNKNOWN, PROTOCOL_OPENCE);
}

EMSCRIPTEN_KEEPALIVE void web_delta_games_end(void)
{
	pthread_mutex_lock(&list_lock);
	memcpy(listed, coming, sizeof(listed[0]) * (size_t)coming_count);
	listed_count = coming_count;
	pthread_mutex_unlock(&list_lock);
}

/* ---------- the Server Browser's side */

/* whether an invite (a link: halo://join/<digits>, or the list's digits)
and the list's digits are the same game's: their token, the last 32 */
static int same_token(const char *invite, const char *digits)
{
	size_t invite_length = strlen(invite), digits_length = strlen(digits);

	return invite_length >= 32 && digits_length >= 32 &&
		!strncasecmp(invite + invite_length - 32, digits + digits_length - 32, 32);
}

/* (with list_lock) the list's game an invite is, or NULL */
static const struct listed_game *find(const char *invite)
{
	int index;

	if (!invite || !*invite)
		return NULL;
	for (index = 0; index < listed_count; index++)
	{
		if (same_token(invite, listed[index].invite))
			return &listed[index];
	}
	return NULL;
}

int web_delta_list_add_games(struct p2p_listing *games, int count, int maximum)
{
	/* (a gametype's name as a listing names it, when the list says only its
	engine) */
	static const char *const engines[] = { "Game", "CTF", "Slayer", "Oddball", "King", "Race" };
	int index;

	pthread_mutex_lock(&list_lock);
	for (index = 0; index < listed_count && count < maximum; index++)
	{
		const struct listed_game *game = &listed[index];
		struct p2p_listing *shown;
		int other;

		if (game->version < delta_legacy_minimum() || game->version > delta_legacy_maximum())
			continue;
		for (other = 0; other < count; other++)
		{
			if (same_token(games[other].invite, game->invite))
				break;
		}
		if (other < count)
			continue;
		shown = &games[count++];
		memset(shown, 0, sizeof(*shown));
		snprintf(shown->invite, sizeof(shown->invite), "halo://join/%s", game->invite);
		/* (the host's identifier: its invite's first 6 bytes, made a locally
		administered address, as p2p.c makes it) */
		for (other = 0; other < (int)sizeof(shown->identifier); other++)
		{
			unsigned int value = 0;

			sscanf(game->invite + 2 * other, "%2x", &value);
			shown->identifier[other] = (unsigned char)value;
		}
		shown->identifier[0] = (unsigned char)((shown->identifier[0] & 0xFC) | 0x02);
		snprintf(shown->name, sizeof(shown->name), "%s", game->name);
		snprintf(shown->map, sizeof(shown->map), "%s", game->map);
		if (game->gametype[0])
			snprintf(shown->gametype, sizeof(shown->gametype), "%s", game->gametype);
		else
		{
			snprintf(shown->gametype, sizeof(shown->gametype), "%s%s", game->teams && game->engine != 1 ? "Team " : "",
				engines[game->engine]);
		}
		shown->player_count = (unsigned char)game->players;
		shown->maximum_player_count = (unsigned char)game->maximum_players;
		shown->engine_type = (unsigned char)game->engine;
		shown->open = (unsigned char)game->open;
		shown->in_progress = (unsigned char)game->in_progress;
		shown->has_teams = (unsigned char)game->teams;
		shown->ping = -1;
	}
	pthread_mutex_unlock(&list_lock);
	return count;
}

/* ASCII text appended to text (UTF-16) */
static void append_ascii(unsigned short *text, int size, const char *ascii)
{
	int used = 0;

	while (used < size - 1 && text[used])
		used++;
	for (; *ascii && used < size - 1; ascii++)
		text[used++] = (unsigned char)*ascii;
	text[used] = 0;
}

/* UTF-8 text (as far as end, or a 0x1E) appended to text (UTF-16); the
bytes read */
static int append_utf8(unsigned short *text, int size, const char *utf8)
{
	const unsigned char *at = (const unsigned char *)utf8;
	int used = 0;

	while (used < size - 1 && text[used])
		used++;
	while (*at && *at != 0x1E)
	{
		unsigned long code = *at++;
		int more = code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : code >= 0xC0 ? 1 : 0;

		if (more)
			code &= 0x3F >> more;
		for (; more && (*at & 0xC0) == 0x80; more--)
			code = code << 6 | (*at++ & 0x3F);
		if (code < 0x20 || code > 0xFFFF || (code >= 0xD800 && code < 0xE000))
			code = '?';
		if (used < size - 1)
			text[used++] = (unsigned short)code;
	}
	text[used] = 0;
	return (int)((const char *)at - utf8);
}

/* the length of text (UTF-16) */
static int text_length(const unsigned short *text)
{
	int length = 0;

	while (text[length])
		length++;
	return length;
}

void web_delta_list_detail_text(const char *invite, unsigned short *text, int size, int characters)
{
	static const char *const platforms[] =
	{
		"", "WINDOWS", "MAC", "LINUX", "ANDROID", "STEAM DECK", "XBOX", "XBOX 360", "WII U", "SWITCH",
	};
	const struct listed_game *game;
	const char *platform;
	const char *name;
	char host[96];
	int dedicated, shown = 0;

	if (size <= 0)
		return;
	text[0] = 0;
	if (characters > size - 1)
		characters = size - 1;
	pthread_mutex_lock(&list_lock);
	game = find(invite);
	if (!game)
	{
		pthread_mutex_unlock(&list_lock);
		return;
	}
	/* the host: "DEDICATED, LINUX, DELTA", "WINDOWS HOST, DELTA", "OPENCE" */
	dedicated = game->hosting == HOSTING_DEDICATED || game->hosting == HOSTING_OFFICIAL;
	platform = game->platform > 0 && game->platform < (int)(sizeof(platforms) / sizeof(*platforms)) ?
		platforms[game->platform] : "";
	snprintf(host, sizeof(host), "%s%s%s%s%s%s%s",
		dedicated ? (game->hosting == HOSTING_OFFICIAL ? "OFFICIAL SERVER" : "DEDICATED") : "",
		dedicated && platform[0] ? ", " : "", platform, !dedicated && platform[0] ? " HOST" : "",
		(dedicated || platform[0]) && game->protocol ? ", " : "",
		game->protocol == PROTOCOL_DELTA ? "DELTA" : "", game->protocol == PROTOCOL_OPENCE ? "OPENCE" : "");
	append_ascii(text, size, host);
	/* who is playing, as many as fit (room kept for "+N more") */
	for (name = game->roster; game->roster_count && *name; shown++)
	{
		unsigned short one[32];
		int length;

		one[0] = 0;
		append_ascii(one, (int)(sizeof(one) / sizeof(*one)), shown ? ", " : host[0] ? ": " : "");
		name += append_utf8(one, (int)(sizeof(one) / sizeof(*one)), name);
		if (*name == 0x1E)
			name++;
		length = text_length(text);
		if (length + text_length(one) + (shown + 1 < game->roster_count ? 9 : 0) > characters)
			break;
		memcpy(text + length, one, sizeof(*one) * (size_t)(text_length(one) + 1));
	}
	if (shown < game->roster_count)
	{
		char more[24];

		snprintf(more, sizeof(more), "%s+%d more", shown ? " " : host[0] ? ": " : "", game->roster_count - shown);
		append_ascii(text, size, more);
	}
	pthread_mutex_unlock(&list_lock);
}

int web_delta_list_score_limit(const char *invite)
{
	const struct listed_game *game;
	int score_limit;

	pthread_mutex_lock(&list_lock);
	game = find(invite);
	score_limit = game ? game->score_limit : 0;
	pthread_mutex_unlock(&list_lock);
	return score_limit;
}

int web_delta_list_dedicated(const char *invite)
{
	const struct listed_game *game;
	int dedicated;

	pthread_mutex_lock(&list_lock);
	game = find(invite);
	dedicated = game && (game->hosting == HOSTING_DEDICATED || game->hosting == HOSTING_OFFICIAL);
	pthread_mutex_unlock(&list_lock);
	return dedicated;
}
