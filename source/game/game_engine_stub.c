/*
GAME_ENGINE_STUB.C

symbols in this file:
000A4710 0010:
	_stub_engine_dispose (0000)
000A4720 0010:
	_stub_engine_initialize_for_new_map (0000)
000A4730 0010:
	_stub_engine_dispose_from_old_map (0000)
000A4740 0010:
	_stub_engine_player_added (0000)
000A4750 0010:
	_stub_engine_game_ending (0000)
000A4760 0010:
	_stub_engine_game_starting (0000)
000A4770 0010:
	_stub_engine_statistics_append (0000)
000A4780 0010:
	_stub_engine_handle_client_message (0000)
000A4790 0010:
	_stub_engine_handle_server_message (0000)
000A47A0 0010:
	_stub_engine_pregame_post_rasterize (0000)
000A47B0 0010:
	_stub_engine_post_rasterize (0000)
000A47C0 0010:
	_stub_engine_update (0000)
000A47D0 0010:
	_stub_engine_allow_pick_up (0000)
000A47E0 0010:
	_stub_engine_player_damaged_player (0000)
000A47F0 0010:
	_stub_engine_player_killed_player (0000)
0025C210 0005:
	??_C@_04GGADAGKI@stub?$AA@ (0000)
002DE6F8 0088:
	_stub_engine (0000)
*/

/* ---------- headers */

#include "cseries/cseries.h"

#ifdef HALO_WEB
/* (web: the stub engine is a struct game_engine whose callbacks carry
their real parameters, game_engine.h: wasm's call_indirect traps on a call
through a pointer whose signature does not match the function's) */
#include "game/game_engine.h"
#endif

/* ---------- constants */

#ifndef HALO_WEB
enum
{
	_game_engine_type_stub = 7,
	NUMBER_OF_STUB_GAME_ENGINE_CALLBACKS = 32,
};
#endif

/* ---------- macros */

/* ---------- structures */

#ifndef HALO_WEB
typedef void (*stub_game_engine_callback)(void);

struct stub_game_engine
{
	char const *name;
	long type;
	stub_game_engine_callback callbacks[NUMBER_OF_STUB_GAME_ENGINE_CALLBACKS];
};
#ifndef HALO_64BIT

typedef char verify_stub_game_engine_size[sizeof(struct stub_game_engine) == 0x88 ? 1 : -1];
#endif
#endif

/* ---------- prototypes */

#ifdef HALO_WEB
static void stub_engine_dispose(void);
static boolean stub_engine_initialize_for_new_map(void);
static void stub_engine_dispose_from_old_map(void);
static void stub_engine_player_added(
	long player_index);
static void stub_engine_game_ending(void);
static void stub_engine_game_starting(void);
static void stub_engine_statistics_append(
	long statistic);
static void stub_engine_handle_client_message(
	void *message);
static void stub_engine_handle_server_message(
	void *message);
static void stub_engine_pregame_post_rasterize(void);
static void stub_engine_post_rasterize(void);
static void stub_engine_update(void);
static boolean stub_engine_allow_pick_up(
	long unit_index,
	long weapon_index);
static void stub_engine_player_damaged_player(
	long damaging_player_index,
	long dead_player_index,
	boolean damage_type);
static void stub_engine_player_killed_player(
	long killing_player_index,
	long killing_object_index,
	long dead_player_index,
	boolean friendly_fire);
#else
static void stub_engine_dispose(void);
static boolean stub_engine_initialize_for_new_map(void);
static void stub_engine_dispose_from_old_map(void);
static void stub_engine_player_added(void);
static void stub_engine_game_ending(void);
static void stub_engine_game_starting(void);
static void stub_engine_statistics_append(void);
static void stub_engine_handle_client_message(void);
static void stub_engine_handle_server_message(void);
static void stub_engine_pregame_post_rasterize(void);
static void stub_engine_post_rasterize(void);
static void stub_engine_update(void);
static boolean stub_engine_allow_pick_up(void);
static void stub_engine_player_damaged_player(void);
static void stub_engine_player_killed_player(void);
#endif

/* ---------- globals */

#ifdef HALO_WEB
struct game_engine stub_engine =
{
	"stub",
	game_engine_stub,
	stub_engine_dispose,
	stub_engine_initialize_for_new_map,
	stub_engine_dispose_from_old_map,
	stub_engine_player_added,
	stub_engine_game_ending,
	stub_engine_game_starting,
	stub_engine_statistics_append,
	stub_engine_handle_client_message,
	stub_engine_handle_server_message,
	stub_engine_pregame_post_rasterize,
	stub_engine_post_rasterize,
	NULL,
	NULL,
	NULL,
	NULL,
	stub_engine_update,
	NULL,
	NULL,
	NULL,
	NULL,
	stub_engine_allow_pick_up,
	stub_engine_player_damaged_player,
	stub_engine_player_killed_player,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};
#else
struct stub_game_engine stub_engine =
{
	"stub",
	_game_engine_type_stub,
	{
		stub_engine_dispose,
		(stub_game_engine_callback) stub_engine_initialize_for_new_map,
		stub_engine_dispose_from_old_map,
		stub_engine_player_added,
		stub_engine_game_ending,
		stub_engine_game_starting,
		stub_engine_statistics_append,
		stub_engine_handle_client_message,
		stub_engine_handle_server_message,
		stub_engine_pregame_post_rasterize,
		stub_engine_post_rasterize,
		NULL,
		NULL,
		NULL,
		NULL,
		stub_engine_update,
		NULL,
		NULL,
		NULL,
		NULL,
		(stub_game_engine_callback) stub_engine_allow_pick_up,
		stub_engine_player_damaged_player,
		stub_engine_player_killed_player,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
	},
};
#endif

/* ---------- public code */

static void stub_engine_dispose(void)
{
}

static boolean stub_engine_initialize_for_new_map(void)
{
	return TRUE;
}

static void stub_engine_dispose_from_old_map(void)
{
}

#ifdef HALO_WEB
static void stub_engine_player_added(
	long player_index)
{
	(void)player_index;
}
#else
static void stub_engine_player_added(void)
{
}
#endif

static void stub_engine_game_ending(void)
{
}

static void stub_engine_game_starting(void)
{
}

#ifdef HALO_WEB
static void stub_engine_statistics_append(
	long statistic)
{
	(void)statistic;
}

static void stub_engine_handle_client_message(
	void *message)
{
	(void)message;
}

static void stub_engine_handle_server_message(
	void *message)
{
	(void)message;
}
#else
static void stub_engine_statistics_append(void)
{
}

static void stub_engine_handle_client_message(void)
{
}

static void stub_engine_handle_server_message(void)
{
}
#endif

static void stub_engine_pregame_post_rasterize(void)
{
}

static void stub_engine_post_rasterize(void)
{
}

static void stub_engine_update(void)
{
}

#ifdef HALO_WEB
static boolean stub_engine_allow_pick_up(
	long unit_index,
	long weapon_index)
{
	(void)unit_index;
	(void)weapon_index;

	return TRUE;
}

static void stub_engine_player_damaged_player(
	long damaging_player_index,
	long dead_player_index,
	boolean damage_type)
{
	(void)damaging_player_index;
	(void)dead_player_index;
	(void)damage_type;
}

static void stub_engine_player_killed_player(
	long killing_player_index,
	long killing_object_index,
	long dead_player_index,
	boolean friendly_fire)
{
	(void)killing_player_index;
	(void)killing_object_index;
	(void)dead_player_index;
	(void)friendly_fire;
}
#else
static boolean stub_engine_allow_pick_up(void)
{
	return TRUE;
}

static void stub_engine_player_damaged_player(void)
{
}

static void stub_engine_player_killed_player(void)
{
}
#endif

/* ---------- private code */
