/* Browser invite-flow mailbox and game-thread menu integration. */

#include "web_online_ui.h"
#include "port_config.h"

#include <emscripten/emscripten.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This browser adapter is compiled as platform code, so it must not include
the game's cseries headers after the host libc headers.  Keep this boundary
to opaque pointers and the game's scalar ABI. */
struct network_game_client;
struct network_game_server;
struct widget_instance;

void ui_widgets_close_all(void);
struct widget_instance *ui_widget_load_by_name_or_tag(
	const char *name,
	long tag_index,
	struct widget_instance *parent,
	short local_player_index,
	long invoking_widget_tag,
	long focused_child_parent_widget_tag,
	short focused_child_index);
void dispose_global_network_game_server(void);
void dispose_global_network_game_client(void);
struct network_game_server *global_network_game_server_get(void);
struct network_game_client *global_network_game_client_get(void);
unsigned char create_global_network_game_client(void);
void network_game_accept_remote_connections(unsigned char accept_remote_connections);
/* halo_map_families.h, halo_ui_map_list.h (ChupathingyCE's map families: a
Custom Edition map is <file>@ce; short as a scalar, wchar_t 16-bit) */
enum { WEB_MAP_FAMILY_CUSTOM_EDITION = 1 };
void map_family_list(short family, void (*found)(char const *file, void *context), void *context);
void ui_map_list_family_name(short family, char const *file, unsigned short *name, long size);
/* ui_widget.h */
unsigned char filesystem_check_thread_is_active(void);
void player_ui_clear_multiplayer_joins(void);
void player_ui_clear_multiplayer_variant(void);
void player_ui_fast_setup_network_server(void);
long player_ui_get_active_player_profile_index(short local_player_index);
void player_ui_get_active_player_profile(short local_player_index, void *profile);
void player_ui_set_active_player_profile(
	short local_player_index,
	long profile_index,
	void *profile);
unsigned char player_ui_configure_network_server_game(
	long multiplayer_level_index,
	long game_mode_index);
unsigned char player_ui_configure_network_server_game_advanced(
	long multiplayer_level_index,
	long game_mode_index,
	long score_to_win,
	long respawn_seconds,
	long lives,
	long health_percent,
	unsigned char infinite_grenades,
	unsigned char shields,
	unsigned char invisible_players,
	unsigned char other_players_on_radar);
void game_connection_set(short connection);
void main_goto_main_menu(void);
short network_game_client_get_state(struct network_game_client *client, short *state_data);
short network_game_client_get_error(struct network_game_client *client);
unsigned char network_game_client_join_first_available_game(void);
unsigned char network_game_client_add_player(struct network_game_client *client, short controller_index);
unsigned char network_game_client_has_local_player(
	struct network_game_client *client,
	short local_player_index);
void platform_log(const char *format, ...);

enum
{
	/* network_game_client_get_state(), kept private by its implementation */
	_network_client_searching = 0,
	_network_client_joining,
	_network_client_pregame,
	_network_client_ingame,
	_network_client_postgame,

	/* Native invite joining also gives up after 90 seconds. */
	WEB_ONLINE_JOIN_TIMEOUT_SECONDS = 90,
	/* Retry slowly enough to avoid duplicate in-game queue entries, but soon
	 * enough to recover when a pregame add crosses the match transition. */
	WEB_ONLINE_PLAYER_RETRY_SECONDS = 1,

	_game_connection_local = 0,
	_game_connection_network_client,

	WEB_ONLINE_REQUEST_COMMAND_MASK = 0xff,
	WEB_ONLINE_REQUEST_MAP_SHIFT = 8,
	WEB_ONLINE_REQUEST_MODE_SHIFT = 16,
	WEB_ONLINE_REQUEST_ADVANCED_BIT = 1 << 24,

	WEB_ONLINE_RULE_INFINITE_GRENADES_BIT = 0,
	WEB_ONLINE_RULE_SHIELDS_BIT,
	WEB_ONLINE_RULE_INVISIBLE_PLAYERS_BIT,
	WEB_ONLINE_RULE_OTHER_PLAYERS_ON_RADAR_BIT,
	WEB_ONLINE_RULE_MASK = 0xf,
};

#define WEB_FALSE ((unsigned char)0)
#define WEB_TRUE 1
#define WEB_NONE (-1)

/* Xbox player profiles use eleven UTF-16 code units plus a terminator.  Keep
 * this small ABI mirror local to the browser adapter: including the game's
 * cseries headers after Emscripten's host headers would corrupt libc types. */
struct web_online_player_profile
{
	unsigned short player_name[WEB_ONLINE_PLAYER_NAME_CHARACTERS + 1];
	short primary_color_index;
	unsigned char remainder[22];
};

_Static_assert(sizeof(struct web_online_player_profile) == 0x30,
	"web player profile ABI must remain 0x30 bytes");

/* Command and host options share one atomic word so the game thread can never
observe a new command with map/mode values from a different browser request. */
static atomic_int web_online_requested_request = ATOMIC_VAR_INIT(_web_online_command_none);
static atomic_int web_online_requested_score_to_win = ATOMIC_VAR_INIT(15);
static atomic_int web_online_requested_respawn_seconds = ATOMIC_VAR_INIT(0);
static atomic_int web_online_requested_lives = ATOMIC_VAR_INIT(0);
static atomic_int web_online_requested_health_percent = ATOMIC_VAR_INIT(100);
static atomic_int web_online_requested_rules = ATOMIC_VAR_INIT(0xa);
static atomic_int web_online_requested_player_magnetism = ATOMIC_VAR_INIT(1);
static atomic_int web_online_public_state = ATOMIC_VAR_INIT(_web_online_state_idle);
static atomic_int web_online_public_error = ATOMIC_VAR_INIT(_web_online_error_none);
static atomic_int web_online_transport_state = ATOMIC_VAR_INIT(_web_online_transport_disconnected);
static atomic_uint web_online_customization_sequence = ATOMIC_VAR_INIT(0);
static atomic_int web_online_requested_color = ATOMIC_VAR_INIT(0);
static atomic_int web_online_requested_name[WEB_ONLINE_PLAYER_NAME_CHARACTERS];
/* the Custom Edition multiplayer maps the page offers after the Xbox's
levels, as map indices from _web_online_multiplayer_level_count on
(publish_custom_maps) */
static atomic_int web_online_custom_map_count = ATOMIC_VAR_INIT(0);
static unsigned int web_online_applied_customization_sequence;

static struct
{
	int command;
	int setup;
	int join_attempted;
	int join_completed;
	int player_added;
	int player_request_sent;
	int pregame_screen_loaded;
	int wait_frames;
	int host_map_index;
	int host_mode_index;
	int host_advanced_settings;
	int host_score_to_win;
	int host_respawn_seconds;
	int host_lives;
	int host_health_percent;
	int host_rules;
	float seconds;
	float player_retry_seconds;
	/* a game the game's own menus made (web_online_game_hosting): INTERNET
	or LAN; its listing's timer (update_listing) */
	int from_menus;
	int internet;
	float listing_seconds;
	/* its lobby is open: Server Setup's Start Game (web_online_game_started) */
	int lobby_open;
} web_online;

static void publish_state(int state)
{
	atomic_store_explicit(&web_online_public_state, state, memory_order_release);
}

static void publish_error(int error)
{
	atomic_store_explicit(&web_online_public_error, error, memory_order_release);
}

/* the map indices the page can host: the Xbox's levels, then the Custom
Edition multiplayer maps (up to the request's 8 bits) */
static int host_map_index_valid(int map_index)
{
	int count = _web_online_multiplayer_level_count +
		atomic_load_explicit(&web_online_custom_map_count, memory_order_acquire);

	return map_index >= 0 && map_index < count && map_index <= 0xff;
}

/* The invite link the page made for the room of the game this browser hosts:
the page writes it into web_online_invite_buffer and publishes its length
(the game thread reads it; a link is written once a room) */
enum { INVITE_BYTES = 512 };
static char web_online_invite[INVITE_BYTES];
static atomic_int web_online_invite_length = ATOMIC_VAR_INIT(0);

EMSCRIPTEN_KEEPALIVE char *web_online_invite_buffer(void)
{
	return web_online_invite;
}

EMSCRIPTEN_KEEPALIVE int web_online_invite_buffer_size(void)
{
	return INVITE_BYTES - 1;
}

/* the link's length, once written (0: none) */
EMSCRIPTEN_KEEPALIVE void web_online_invite_set(int length)
{
	if (length < 0 || length >= INVITE_BYTES)
		length = 0;
	web_online_invite[length] = '\0';
	atomic_store_explicit(&web_online_invite_length, length, memory_order_release);
}

int web_online_invite_link(char *link, int size)
{
	int length = atomic_load_explicit(&web_online_invite_length, memory_order_acquire);

	if (!link || size <= 0)
		return 0;
	link[0] = '\0';
	if (!length || !web_online.command)
		return 0;
	snprintf(link, (size_t)size, "%.*s", length, web_online_invite);
	return 1;
}

/* The listing of the game this browser hosts from the game's menus, as
JSON, for the page to give the lobby service's list of public games (GET
/v1/public-rooms; online_client.js, sendListing): written by the game thread
(update_listing), its sequence counted up after each change */
enum { LISTING_BYTES = 1024 };
static char web_online_listing[LISTING_BYTES];
static atomic_int web_online_listing_sequence = ATOMIC_VAR_INIT(0);

EMSCRIPTEN_KEEPALIVE char *web_online_listing_buffer(void)
{
	return web_online_listing;
}

EMSCRIPTEN_KEEPALIVE int web_online_listing_changes(void)
{
	return atomic_load_explicit(&web_online_listing_sequence, memory_order_acquire);
}

/* p2p_lobby.c, p2p.c (the browser's) */
void p2p_lobby_web_listing(char *name, char *map, char *gametype, int *open, int *in_progress, int *has_teams,
	int *public, int *has_password);
void p2p_web_game_player_counts(int *count, int *maximum);

/* (text for JSON: quotes and backslashes escaped, control characters out) */
static int json_text(char *out, int size, const char *text)
{
	int length = 0;

	for (; *text && length < size - 3; text++)
	{
		unsigned char c = (unsigned char)*text;

		if (c < 0x20 || c == 0x7f)
			continue;
		if (c == '"' || c == '\\')
			out[length++] = '\\';
		out[length++] = (char)c;
	}
	out[length] = '\0';
	return length;
}

static void update_listing(void)
{
	char name[64], map[64], gametype[64];
	char name_json[160], map_json[160], gametype_json[160];
	char listing[LISTING_BYTES];
	int open, in_progress, has_teams, public, has_password, players, maximum;

	p2p_lobby_web_listing(name, map, gametype, &open, &in_progress, &has_teams, &public, &has_password);
	p2p_web_game_player_counts(&players, &maximum);
	json_text(name_json, sizeof(name_json), name);
	json_text(map_json, sizeof(map_json), map);
	json_text(gametype_json, sizeof(gametype_json), gametype);
	/* (listed: an INTERNET game Server Setup made PUBLIC, without a password,
	which the browsers' rooms cannot ask for, once its lobby is open) */
	snprintf(listing, sizeof(listing),
		"{\"listed\":%s,\"name\":\"%s\",\"map\":\"%s\",\"gametype\":\"%s\",\"players\":%d,\"maximum\":%d,"
		"\"open\":%s,\"inProgress\":%s,\"hasTeams\":%s}",
		web_online.internet && web_online.lobby_open && public && !has_password && map[0] ? "true" : "false",
		name_json, map_json, gametype_json, players < 0 ? 0 : players > 255 ? 255 : players,
		maximum < 0 ? 0 : maximum > 255 ? 255 : maximum,
		open ? "true" : "false", in_progress ? "true" : "false", has_teams ? "true" : "false");
	if (!strcmp(listing, web_online_listing))
		return;
	memcpy(web_online_listing, listing, sizeof(listing));
	atomic_fetch_add_explicit(&web_online_listing_sequence, 1, memory_order_release);
}

/* the Custom Edition maps the page was told of, by their level names
(custom_maps\<name>): the page's map index past the Xbox's levels picks
one, whatever order a later scan of the maps folders lists them in */
enum { MAXIMUM_CUSTOM_MAPS = 0xff + 1 - _web_online_multiplayer_level_count, LEVEL_BYTES = 64, NAME_BYTES = 192 };
static char custom_map_levels[MAXIMUM_CUSTOM_MAPS][LEVEL_BYTES];

/* (source/interface/player_ui.c) */
const char *web_online_custom_map_level(long index)
{
	if (index < 0 || index >= atomic_load_explicit(&web_online_custom_map_count, memory_order_acquire))
		return NULL;
	return custom_map_levels[index];
}

/* Tells the page (online_client.js, haloOnlineCustomMaps) the Custom Edition
multiplayer maps, sorted by name as the game lists them: each one's level
name and display name. Once, with the main menu: the maps are in the
server's custom_maps folder, which a session does not see change. */
/* (the Custom Edition maps the families find, map_family_list: each one's
level name, <file>@ce, and its name as the menus show it) */
static struct
{
	char level[LEVEL_BYTES];
	unsigned short name[NAME_BYTES / 2];
} custom_maps_found[MAXIMUM_CUSTOM_MAPS];
static int custom_maps_found_count;

static void custom_map_found(char const *file, void *context)
{
	(void)context;
	if (custom_maps_found_count >= MAXIMUM_CUSTOM_MAPS || strlen(file) + 4 >= LEVEL_BYTES)
		return;
	snprintf(custom_maps_found[custom_maps_found_count].level, LEVEL_BYTES, "%s@ce", file);
	ui_map_list_family_name(WEB_MAP_FAMILY_CUSTOM_EDITION, file, custom_maps_found[custom_maps_found_count].name,
		NAME_BYTES / 2);
	custom_maps_found_count++;
}

/* (by name, letters' case aside) */
static int custom_map_compare(const void *a, const void *b)
{
	const unsigned short *x = ((const unsigned short *)a) + LEVEL_BYTES / 2;
	const unsigned short *y = ((const unsigned short *)b) + LEVEL_BYTES / 2;

	for (; *x && *y; x++, y++)
	{
		unsigned short cx = *x >= 'A' && *x <= 'Z' ? *x + 32 : *x;
		unsigned short cy = *y >= 'A' && *y <= 'Z' ? *y + 32 : *y;

		if (cx != cy)
			return cx < cy ? -1 : 1;
	}
	return *x ? 1 : *y ? -1 : 0;
}

static void publish_custom_maps(void)
{
	static const char *levels[MAXIMUM_CUSTOM_MAPS];
	static char names[MAXIMUM_CUSTOM_MAPS][NAME_BYTES];
	static const char *name_pointers[MAXIMUM_CUSTOM_MAPS];
	int index, listed = 0;

	custom_maps_found_count = 0;
	map_family_list(WEB_MAP_FAMILY_CUSTOM_EDITION, custom_map_found, NULL);
	qsort(custom_maps_found, (size_t)custom_maps_found_count, sizeof(custom_maps_found[0]), custom_map_compare);
	for (index = 0; index < custom_maps_found_count && listed < MAXIMUM_CUSTOM_MAPS; index++)
	{
		const char *level = custom_maps_found[index].level;
		const unsigned short *name = custom_maps_found[index].name;
		char *out = names[listed];
		int length = 0;

		/* (UTF-16 to UTF-8: the names are the files', of the BMP) */
		for (; *name && length < NAME_BYTES - 4; name++)
		{
			unsigned short c = *name;

			if (c < 0x80)
				out[length++] = (char)c;
			else if (c < 0x800)
			{
				out[length++] = (char)(0xc0 | (c >> 6));
				out[length++] = (char)(0x80 | (c & 0x3f));
			}
			else if (c < 0xd800 || c > 0xdfff)
			{
				out[length++] = (char)(0xe0 | (c >> 12));
				out[length++] = (char)(0x80 | ((c >> 6) & 0x3f));
				out[length++] = (char)(0x80 | (c & 0x3f));
			}
		}
		out[length] = '\0';
		strcpy(custom_map_levels[listed], level);
		levels[listed] = custom_map_levels[listed];
		name_pointers[listed] = out;
		listed++;
	}
	MAIN_THREAD_EM_ASM({
		const maps = [];
		for (let index = 0; index < $2; index++)
		{
			maps.push({
				level: UTF8ToString(HEAPU32[($0 >> 2) + index]),
				name: UTF8ToString(HEAPU32[($1 >> 2) + index])
			});
		}
		if (typeof globalThis.haloOnlineCustomMaps === "function")
			globalThis.haloOnlineCustomMaps(maps);
	}, levels, name_pointers, listed);
	atomic_store_explicit(&web_online_custom_map_count, listed, memory_order_release);
	platform_log("web online: %d Custom Edition multiplayer maps to host", listed);
}

static int pack_request(int command, int map_index, int mode_index)
{
	return (command & WEB_ONLINE_REQUEST_COMMAND_MASK) |
		(map_index << WEB_ONLINE_REQUEST_MAP_SHIFT) |
		(mode_index << WEB_ONLINE_REQUEST_MODE_SHIFT);
}

EMSCRIPTEN_KEEPALIVE int platform_web_online_request(int command)
{
	if (command < _web_online_command_host || command > _web_online_command_cancel)
		return 0;
	/* Legacy callers that only request hosting get the safe defaults: Battle
	Creek and Slayer. Join/cancel ignore the packed host fields. */
	atomic_store_explicit(
		&web_online_requested_request,
		pack_request(command, 0, 0),
		memory_order_release);
	return 1;
}

EMSCRIPTEN_KEEPALIVE int platform_web_online_host_configured(
	int map_index,
	int mode_index)
{
	if (!host_map_index_valid(map_index) ||
		mode_index < 0 || mode_index >= _web_online_game_mode_count)
	{
		return 0;
	}
	atomic_store_explicit(
		&web_online_requested_request,
		pack_request(_web_online_command_host, map_index, mode_index),
		memory_order_release);
	return 1;
}

EMSCRIPTEN_KEEPALIVE int platform_web_online_host_advanced_configured(
	int map_index,
	int mode_index,
	int score_to_win,
	int respawn_seconds,
	int lives,
	int health_percent,
	int rules)
{
	if (!host_map_index_valid(map_index) ||
		mode_index < 0 || mode_index >= _web_online_game_mode_count ||
		score_to_win < 1 || score_to_win > 1000 ||
		respawn_seconds < 0 || respawn_seconds > 30 ||
		lives < 0 || lives > 99 ||
		health_percent < 25 || health_percent > 400 ||
		(rules & ~WEB_ONLINE_RULE_MASK))
	{
		return 0;
	}

	/* Publish the rules first, then release the request containing the advanced
	bit. The game thread acquires that request before reading this mailbox. */
	atomic_store_explicit(&web_online_requested_score_to_win, score_to_win, memory_order_relaxed);
	atomic_store_explicit(&web_online_requested_respawn_seconds, respawn_seconds, memory_order_relaxed);
	atomic_store_explicit(&web_online_requested_lives, lives, memory_order_relaxed);
	atomic_store_explicit(&web_online_requested_health_percent, health_percent, memory_order_relaxed);
	atomic_store_explicit(&web_online_requested_rules, rules, memory_order_relaxed);
	atomic_store_explicit(
		&web_online_requested_request,
		pack_request(_web_online_command_host, map_index, mode_index) |
			WEB_ONLINE_REQUEST_ADVANCED_BIT,
		memory_order_release);
	return 1;
}

EMSCRIPTEN_KEEPALIVE void platform_web_set_player_magnetism_enabled(int enabled)
{
	atomic_store_explicit(
		&web_online_requested_player_magnetism,
		!!enabled,
		memory_order_release);
}

EMSCRIPTEN_KEEPALIVE int platform_web_online_set_player_customization(
	int color_index,
	int name0,
	int name1,
	int name2,
	int name3,
	int name4,
	int name5,
	int name6,
	int name7,
	int name8,
	int name9,
	int name10)
{
	int i;
	int name_length = 0;
	int name[WEB_ONLINE_PLAYER_NAME_CHARACTERS] =
	{
		name0, name1, name2, name3, name4, name5,
		name6, name7, name8, name9, name10,
	};

	if (color_index < 0 || color_index >= WEB_ONLINE_PLAYER_COLOR_COUNT)
		return 0;

	/* Browser names intentionally use Halo's portable ASCII subset.  Reject a
	 * malformed caller instead of letting control codes reach menu rendering. */
	for (i = 0; i < WEB_ONLINE_PLAYER_NAME_CHARACTERS; i++)
	{
		if (!name[i])
			break;
		if (name[i] < 0x20 || name[i] > 0x7e)
			return 0;
		name_length++;
	}
	if (!name_length)
		return 0;

	/* Odd versions are writes in progress; even versions are complete. */
	atomic_fetch_add_explicit(
		&web_online_customization_sequence,
		1,
		memory_order_acq_rel);
	atomic_store_explicit(
		&web_online_requested_color,
		color_index,
		memory_order_relaxed);
	for (i = 0; i < WEB_ONLINE_PLAYER_NAME_CHARACTERS; i++)
	{
		atomic_store_explicit(
			&web_online_requested_name[i],
			i < name_length ? name[i] : 0,
			memory_order_relaxed);
	}
	/* Publishing the even version last commits the mailbox transaction. */
	atomic_fetch_add_explicit(
		&web_online_customization_sequence,
		1,
		memory_order_release);
	return 1;
}

EMSCRIPTEN_KEEPALIVE int platform_web_online_get_state(void)
{
	return atomic_load_explicit(&web_online_public_state, memory_order_acquire);
}

EMSCRIPTEN_KEEPALIVE int platform_web_online_get_error(void)
{
	return atomic_load_explicit(&web_online_public_error, memory_order_acquire);
}

EMSCRIPTEN_KEEPALIVE void platform_web_online_set_transport_state(int state)
{
	if (state < _web_online_transport_disconnected || state > _web_online_transport_failed)
		return;
	atomic_store_explicit(&web_online_transport_state, state, memory_order_release);
}

EMSCRIPTEN_KEEPALIVE int platform_web_online_get_transport_state(void)
{
	return atomic_load_explicit(&web_online_transport_state, memory_order_acquire);
}

EMSCRIPTEN_KEEPALIVE int platform_web_online_get_client_state(void)
{
	struct network_game_client *client = global_network_game_client_get();

	return client ? network_game_client_get_state(client, NULL) : WEB_NONE;
}

static void apply_requested_player_customization(void)
{
	int i;
	unsigned int sequence;
	unsigned int committed_sequence;
	struct web_online_player_profile profile;

	sequence = atomic_load_explicit(
		&web_online_customization_sequence,
		memory_order_acquire);
	if ((sequence & 1) || sequence == web_online_applied_customization_sequence)
		return;

	player_ui_get_active_player_profile(0, &profile);
	for (i = 0; i < WEB_ONLINE_PLAYER_NAME_CHARACTERS; i++)
	{
		profile.player_name[i] = (unsigned short)atomic_load_explicit(
			&web_online_requested_name[i],
			memory_order_relaxed);
	}
	profile.player_name[WEB_ONLINE_PLAYER_NAME_CHARACTERS] = 0;
	profile.primary_color_index = (short)atomic_load_explicit(
		&web_online_requested_color,
		memory_order_relaxed);
	committed_sequence = atomic_load_explicit(
		&web_online_customization_sequence,
		memory_order_acquire);
	if (sequence != committed_sequence)
		return;
	player_ui_set_active_player_profile(
		0,
		player_ui_get_active_player_profile_index(0),
		&profile);
	web_online_applied_customization_sequence = sequence;
}

static void clear_multiplayer_joins_and_restore_customization(void)
{
	/* Halo's stock clear also replaces every active player profile with an
	 * unnamed, colour-less default.  Browser hosting/joining deliberately
	 * bypasses the profile picker, so restore the identity the browser posted
	 * before network_game_client_add_player() serializes that profile.  Without
	 * this, the server sees an empty name/NONE colour and assigns random values
	 * such as "Howard" instead. */
	player_ui_clear_multiplayer_joins();
	web_online_applied_customization_sequence = 0;
	apply_requested_player_customization();
}

static void reset_owned_game(void)
{
	/* This mirrors the stock network-game cancel handlers.  Closing the old
	widgets first prevents them from observing the disposed client/server on
	the remainder of this frame. */
	ui_widgets_close_all();
	dispose_global_network_game_server();
	dispose_global_network_game_client();
	network_game_accept_remote_connections(WEB_FALSE);
	player_ui_clear_multiplayer_joins();
	player_ui_clear_multiplayer_variant();
	game_connection_set(_game_connection_local);
	main_goto_main_menu();
}

static void clear_session(void)
{
	memset(&web_online, 0, sizeof(web_online));
	/* (its room's invite goes with it) */
	atomic_store_explicit(&web_online_invite_length, 0, memory_order_release);
}

static void fail_session(int error)
{
	platform_log("web online: session failed (%d)", error);
	if (web_online.setup)
		reset_owned_game();
	clear_session();
	publish_error(error);
	publish_state(_web_online_state_error);
}

static void begin_request(
	int command,
	int map_index,
	int mode_index,
	int advanced_settings)
{
	platform_log("web online: request %s", command == _web_online_command_host ? "host" : "join");
	if (web_online.command || web_online.setup)
	{
		reset_owned_game();
		clear_session();
		/* main_menu_load() runs near the beginning of the next frame. */
		web_online.wait_frames = 1;
	}
	web_online.command = command;
	web_online.host_map_index = map_index;
	web_online.host_mode_index = mode_index;
	web_online.host_advanced_settings = advanced_settings;
	if (advanced_settings)
	{
		web_online.host_score_to_win = atomic_load_explicit(
			&web_online_requested_score_to_win, memory_order_relaxed);
		web_online.host_respawn_seconds = atomic_load_explicit(
			&web_online_requested_respawn_seconds, memory_order_relaxed);
		web_online.host_lives = atomic_load_explicit(
			&web_online_requested_lives, memory_order_relaxed);
		web_online.host_health_percent = atomic_load_explicit(
			&web_online_requested_health_percent, memory_order_relaxed);
		web_online.host_rules = atomic_load_explicit(
			&web_online_requested_rules, memory_order_relaxed);
	}
	publish_error(_web_online_error_none);
	publish_state(_web_online_state_waiting_for_main_menu);
}

static void cancel_request(void)
{
	if (web_online.command || web_online.setup)
		reset_owned_game();
	clear_session();
	publish_error(_web_online_error_none);
	publish_state(_web_online_state_idle);
}

static void setup_host(void)
{
	platform_log("web online: opening host lobby");
	clear_multiplayer_joins_and_restore_customization();
	player_ui_clear_multiplayer_variant();
	publish_state(_web_online_state_host_starting);
	player_ui_fast_setup_network_server();
	web_online.setup = WEB_TRUE;
	if (!global_network_game_server_get() || !global_network_game_client_get())
	{
		fail_session(_web_online_error_host_setup_failed);
		return;
	}
	if (web_online.host_advanced_settings ?
		!player_ui_configure_network_server_game_advanced(
			web_online.host_map_index,
			web_online.host_mode_index,
			web_online.host_score_to_win,
			web_online.host_respawn_seconds,
			web_online.host_lives,
			web_online.host_health_percent,
			(web_online.host_rules & (1 << WEB_ONLINE_RULE_INFINITE_GRENADES_BIT)) != 0,
			(web_online.host_rules & (1 << WEB_ONLINE_RULE_SHIELDS_BIT)) != 0,
			(web_online.host_rules & (1 << WEB_ONLINE_RULE_INVISIBLE_PLAYERS_BIT)) != 0,
			(web_online.host_rules & (1 << WEB_ONLINE_RULE_OTHER_PLAYERS_ON_RADAR_BIT)) != 0) :
		!player_ui_configure_network_server_game(
			web_online.host_map_index,
			web_online.host_mode_index))
	{
		fail_session(_web_online_error_host_setup_failed);
		return;
	}
	platform_log("web online: host lobby ready");
	publish_state(_web_online_state_hosting);
}

void web_online_game_hosting(int internet)
{
	platform_log("web online: the game's menus made a %s game: opening a room for it", internet ? "INTERNET" : "LAN");
	/* (this server is the session's: update_host ends it, and the room, when
	the player backs out; the menus already have the player in) */
	clear_session();
	web_online.command = _web_online_command_host;
	web_online.setup = WEB_TRUE;
	web_online.player_added = WEB_TRUE;
	web_online.from_menus = WEB_TRUE;
	web_online.internet = internet ? WEB_TRUE : WEB_FALSE;
	/* (another game's listing is not this one's) */
	web_online_listing[0] = '\0';
	update_listing();
	publish_error(_web_online_error_none);
	publish_state(_web_online_state_hosting);
	MAIN_THREAD_ASYNC_EM_ASM({
		if (globalThis.HaloOnline && typeof globalThis.HaloOnline.hostFromGame === "function")
			globalThis.HaloOnline.hostFromGame({ internet: !!$0 });
	}, internet);
}

void web_online_game_started(void)
{
	if (!web_online.from_menus)
		return;
	web_online.lobby_open = WEB_TRUE;
	update_listing();
}

static void setup_join(void)
{
	platform_log("web online: opening join client");
	dispose_global_network_game_client();
	dispose_global_network_game_server();
	network_game_accept_remote_connections(WEB_FALSE);
	clear_multiplayer_joins_and_restore_customization();
	player_ui_clear_multiplayer_variant();
	web_online.setup = WEB_TRUE;
	if (!create_global_network_game_client())
	{
		fail_session(_web_online_error_client_setup_failed);
		return;
	}
	game_connection_set(_game_connection_network_client);
	publish_state(_web_online_state_join_searching);
}

static void add_primary_player_when_ready(
	struct network_game_client *client,
	float seconds)
{
	if (!client || web_online.player_added)
		return;
	if (network_game_client_has_local_player(client, 0))
	{
		web_online.player_added = WEB_TRUE;
		platform_log("web online: primary player confirmed");
		return;
	}

	web_online.player_retry_seconds += seconds;
	if (!web_online.player_request_sent ||
		web_online.player_retry_seconds >= WEB_ONLINE_PLAYER_RETRY_SECONDS)
	{
		if (network_game_client_add_player(client, 0))
		{
			platform_log("web online: %s primary player",
				web_online.player_request_sent ? "retrying" : "requesting");
			web_online.player_request_sent = WEB_TRUE;
		}
		web_online.player_retry_seconds = 0.0f;
	}
}

static void update_host(float seconds)
{
	struct network_game_client *client = global_network_game_client_get();

	if (!global_network_game_server_get() || !client)
	{
		/* Backing out through Halo's own UI ends the browser room cleanly. */
		clear_session();
		publish_error(_web_online_error_none);
		publish_state(_web_online_state_idle);
		return;
	}
	web_online.seconds += seconds;
	if (web_online.seconds >= 0.5f)
		add_primary_player_when_ready(client, seconds);
	/* (a game of the menus' own: its listing, now and then) */
	if (web_online.from_menus && (web_online.listing_seconds += seconds) >= 1.0f)
	{
		web_online.listing_seconds = 0.0f;
		update_listing();
	}
	publish_state(_web_online_state_hosting);
}

static int load_join_pregame_screen(void)
{
	ui_widgets_close_all();
	return ui_widget_load_by_name_or_tag(
		"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen",
		WEB_NONE, NULL, WEB_NONE, WEB_NONE, WEB_NONE, WEB_NONE) != NULL;
}

static void update_join(float seconds)
{
	struct network_game_client *client = global_network_game_client_get();
	short client_state;

	web_online.seconds += seconds;
	if (web_online.seconds >= WEB_ONLINE_JOIN_TIMEOUT_SECONDS && !web_online.join_completed)
	{
		fail_session(_web_online_error_join_timed_out);
		return;
	}
	if (!client)
	{
		/* Halo's Back action disposes the client before the browser receives a
		 * cancel command. Treat that local UI action as a clean room exit. */
		clear_session();
		publish_error(_web_online_error_none);
		publish_state(_web_online_state_idle);
		return;
	}
	if (network_game_client_get_error(client) != 0)
	{
		fail_session(_web_online_error_join_failed);
		return;
	}

	client_state = network_game_client_get_state(client, NULL);
	if (!web_online.join_attempted && client_state == _network_client_searching)
	{
		if (network_game_client_join_first_available_game())
		{
			web_online.join_attempted = WEB_TRUE;
			if (!load_join_pregame_screen())
			{
				fail_session(_web_online_error_pregame_screen_failed);
				return;
			}
			web_online.pregame_screen_loaded = WEB_TRUE;
			publish_state(_web_online_state_join_connecting);
		}
		else
		{
			publish_state(_web_online_state_join_searching);
		}
		return;
	}

	if (client_state == _network_client_joining)
	{
		publish_state(_web_online_state_join_connecting);
		return;
	}
	if (client_state == _network_client_pregame ||
		client_state == _network_client_ingame ||
		client_state == _network_client_postgame)
	{
		add_primary_player_when_ready(client, seconds);
		if (!web_online.player_added)
		{
			publish_state(_web_online_state_join_connecting);
			return;
		}
		web_online.join_completed = WEB_TRUE;
		if (!web_online.pregame_screen_loaded && client_state == _network_client_pregame)
		{
			if (!load_join_pregame_screen())
			{
				fail_session(_web_online_error_pregame_screen_failed);
				return;
			}
			web_online.pregame_screen_loaded = WEB_TRUE;
		}
		publish_state(_web_online_state_joined);
		return;
	}

	/* A rejected or disconnected join returns the client to searching. */
	if (web_online.join_attempted && client_state == _network_client_searching)
		fail_session(_web_online_error_join_failed);
}

void web_online_ui_update(int main_menu_loaded, float seconds)
{
	static int applied_magnetism = -1;
	static int custom_maps_published;
	int requested_magnetism;
	int request = atomic_exchange_explicit(
		&web_online_requested_request,
		_web_online_command_none,
		memory_order_acq_rel);
	int command = request & WEB_ONLINE_REQUEST_COMMAND_MASK;
	int map_index = (request >> WEB_ONLINE_REQUEST_MAP_SHIFT) & 0xff;
	int mode_index = (request >> WEB_ONLINE_REQUEST_MODE_SHIFT) & 0xff;
	int advanced_settings = (request & WEB_ONLINE_REQUEST_ADVANCED_BIT) != 0;

	/* The page's "aim magnetism" choice. OpenCE lets the view's magnetism
	 * step aside only while the mouse (or a touch drag) aims, unless
	 * input.mouse_aim_assist is on (xinput_sdl.c, halo_linux_mouse_aiming);
	 * the game-wide player_magnetism_flag is the host's rule and stays as
	 * the game sets it (cheats_network_client_enforce puts it back). */
	requested_magnetism = atomic_load_explicit(
		&web_online_requested_player_magnetism,
		memory_order_acquire);
	if (requested_magnetism != applied_magnetism)
	{
		applied_magnetism = requested_magnetism;
		config_write_boolean("input.mouse_aim_assist", requested_magnetism);
	}

	/* (once the start-up's saved game enumeration is over: it walks folders
	with the same find_files state as the maps folders' scan, on its own
	thread, and a scan beside it stops short, listing only some maps) */
	if (main_menu_loaded && !custom_maps_published && !filesystem_check_thread_is_active())
	{
		custom_maps_published = 1;
		publish_custom_maps();
	}

	/* Browser calls only publish atomics.  Apply the selected identity here,
	 * before host/join can build its network_player from the active profile. */
	apply_requested_player_customization();

	if (command == _web_online_command_cancel)
	{
		cancel_request();
		return;
	}
	if (command == _web_online_command_host || command == _web_online_command_join)
		begin_request(command, map_index, mode_index, advanced_settings);

	if (!web_online.command)
		return;
	if (web_online.wait_frames > 0)
	{
		web_online.wait_frames--;
		return;
	}
	if (!web_online.setup)
	{
		if (!main_menu_loaded)
		{
			publish_state(_web_online_state_waiting_for_main_menu);
			return;
		}
		if (web_online.command == _web_online_command_host)
			setup_host();
		else
			setup_join();
		return;
	}

	if (web_online.command == _web_online_command_host)
		update_host(seconds);
	else
		update_join(seconds);
}
