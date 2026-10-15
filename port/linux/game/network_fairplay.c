/*
NETWORK_FAIRPLAY.C

Port (this fork): the host's checks of its clients' shots for cheats
(network_fairplay.h).

A client decides what its player's shots hit and reports it; the host
checks the report (network_damage.c's distributed_report_valid): the
weapon, its rate, the shooter's range, the target about where the host had
it. That leaves a client that lies about where it aimed ("silent aim": the
shot goes to a target its player is not looking at) or what was in the way
(a shot through a wall) believed. The host has what it needs to see both:
where each player's eyes were and where they aimed, every tick (their
input, which it plays), and the map's walls. So for each bullet's hit on a
player (a projectile that flies straight: not guided, not falling; on
foot), over the ticks it could have been fired in:

  - Through a wall: no tick from which the shooter's eyes saw the impact
    past the map's structure (its solid, visible surfaces; not glass, not
    fences). Refused when the wall is solid (the line is blocked both ways,
    and from around the eyes too: not a corner the shooter peeked past).
  - Off aim: the shooter never aimed within 15 degrees of the impact;
    refused beyond 45 (no spread or homing goes that far; guided and
    falling projectiles are not checked).
  - A snap: the aim turned faster than 25 degrees a tick in the ticks
    before it came within 2 degrees of the impact (noted, never refused:
    a fast flick is human too).

Every few ticks it also notes, for each player, whether an enemy near them
is behind a wall, and whether they aim at one that is (which a player who
sees through walls does far more than one guessing a corner).

network.fair_play: "refuse" (the default) refuses the hits above and logs
what it finds; "log" only logs; "off" checks nothing. What it finds goes in
fairplay.txt beside bans.txt (a line each, as cheaters.txt's: the player's
machine, its address and hardware id, and what was seen; at most a few a
player a game), a line a player at each game's end, and the log's "fair
play:" lines; sv_suspects lists the game's players and the last game's.
Nothing is banned or kicked by itself: a judgement is the admin's.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_server_manager.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "units/bipeds.h"
#include "units/units.h"
#include "network_distributed.h"
#include "network_fairplay.h"
#include "network_recording.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* the platform layer's (port_config.h, p2p.h) */
const char *config_string(const char *name);
void p2p_hardware_id_sanitize(char *destination, int size, const char *source);

/* ---------- constants */

/* what the host finds, a line each (as CHEATERS_FILE's), beside bans.txt */
#define FAIRPLAY_FILE "d:\\fairplay.txt"

enum
{
	/* the ticks of where players looked from and aimed, kept (a power of
	two: about four seconds, more than a report may be old) */
	FAIRPLAY_HISTORY_TICKS = 128,
	/* ... looked over before the tick a report was made at (the shot's
	flight, at most a second, and the frame drawn behind) */
	FAIRPLAY_MAXIMUM_FLIGHT_TICKS = 30,
	FAIRPLAY_SLACK_TICKS = 6,
	/* the lines of sight tested for one hit, at most (spread over its
	ticks) */
	FAIRPLAY_MAXIMUM_SIGHT_TESTS = 10,
	/* how often each player's aim at hidden enemies is noted, and whether
	an enemy near them is hidden */
	FAIRPLAY_AIM_SAMPLE_TICKS = 3,
	FAIRPLAY_EXPOSURE_SAMPLE_TICKS = 15,
	/* the lines in fairplay.txt for a player in a game, at most */
	FAIRPLAY_MAXIMUM_LINES = 8,
};

/* degrees: off aim, refused, and a snap's turn a tick and how near it ends */
#define FAIRPLAY_OFF_AIM_DEGREES 15.0f
#define FAIRPLAY_REFUSED_AIM_DEGREES 45.0f
#define FAIRPLAY_SNAP_TURN_DEGREES 25.0f
#define FAIRPLAY_SNAP_END_DEGREES 2.0f
/* world units: the nearest impact checked for aim (nearer, the muzzle's
offset from the eyes is a large angle), and for a wall; how far before the
impact (or after the eyes) a wall counts; the offsets around the eyes a
solid wall blocks too; an aim on a hidden enemy (their body's half width,
and a little), how near and far an enemy counts */
#define FAIRPLAY_AIM_MINIMUM_DISTANCE 1.0f
#define FAIRPLAY_WALL_MINIMUM_DISTANCE 2.0f
#define FAIRPLAY_WALL_MARGIN 0.5f
#define FAIRPLAY_EYE_OFFSET 0.25f
#define FAIRPLAY_BODY_RADIUS 0.3f
#define FAIRPLAY_AIM_SLACK_DEGREES 2.0f
#define FAIRPLAY_ENEMY_NEAREST 2.0f
#define FAIRPLAY_ENEMY_FARTHEST 30.0f
/* a suspect: so many refused hits in a game, or so much aim at hidden
enemies (of the seconds one was near, after at least a minute of them) */
#define FAIRPLAY_SUSPECT_REFUSED_HITS 3
#define FAIRPLAY_SUSPECT_HIDDEN_AIM_SHARE 0.25f
#define FAIRPLAY_SUSPECT_HIDDEN_ENEMY_SECONDS 60.0f
/* ... or so many bursts of moves refused (teleports), or seconds flying */
#define FAIRPLAY_SUSPECT_TELEPORTS 3
#define FAIRPLAY_SUSPECT_FLYING_SECONDS 3.0f
/* world units: how little a unit in the air rises and falls for so long, a
hover (gravity takes anything else down) */
#define FAIRPLAY_HOVER_RANGE 0.5f
/* ... and how far the host's own copy of a player moving at once in a tick
is the host moving them (a teleporter), not their game's word: further than
anyone runs, or is thrown, in one */
#define FAIRPLAY_HOST_JUMP 1.0f

enum
{
	/* the ticks after the host moves a player itself (a teleporter, a new
	life, a vehicle got into or out of) its refusals of their game's word
	are not counted: it comes late */
	FAIRPLAY_MOVE_GRACE_TICKS = 2 * TICKS_PER_SECOND,
	/* refusals this close together are one burst */
	FAIRPLAY_MOVE_BURST_TICKS = TICKS_PER_SECOND,
	/* in the air this long, rising and falling less than FAIRPLAY_HOVER_RANGE */
	FAIRPLAY_HOVER_TICKS = 75,
	/* the lines in fairplay.txt for a player's moves in a game, at most */
	FAIRPLAY_MAXIMUM_MOVE_LINES = 6,
};

/* the map's structure as a bullet meets it: its visible faces (not
two-sided ones, fences and grates, nor glass that breaks) */
#define FAIRPLAY_WALL_FLAGS (FLAG(_collision_test_front_facing_surfaces_bit) | \
	FLAG(_collision_test_ignore_two_sided_surfaces_bit) | FLAG(_collision_test_ignore_invisible_surfaces_bit) | \
	FLAG(_collision_test_ignore_breakable_surfaces_bit) | FLAG(_collision_test_structure_bit))

#define FAIRPLAY_DEGREES(radians) ((radians) * 57.2957795f)

enum
{
	_fairplay_off = 0,
	_fairplay_log,
	_fairplay_refuse,
};

/* ---------- types */

struct fairplay_view
{
	/* NONE: no living unit then */
	long time;
	real_point3d eye;
	real_vector3d aim;
	boolean on_foot;
};

struct fairplay_player
{
	/* the player (DATUM_INDEX_NEW of their index and identifier), NONE for
	none */
	long player_id;
	long lines;
	boolean flagged;
	struct network_fairplay_stats stats;
	/* their moves: refusals not counted before (a new life, a ride, a
	teleporter), the unit and whether on foot last tick, the burst of
	refusals going on (until), the time in the air (NONE on the ground) and
	how high and low in it, and the lines written of it */
	long grace_until;
	long last_unit;
	boolean last_on_foot;
	long burst_until;
	long airborne_since;
	real air_low;
	real air_high;
	long hover_noted_until;
	long move_lines;
	/* where the host had their unit last tick (unit last_unit) */
	real_point3d last_position;
};

/* ---------- globals */

static struct fairplay_view fairplay_views[MAXIMUM_TRACKED_PLAYERS][FAIRPLAY_HISTORY_TICKS];
static struct fairplay_player fairplay_players[MAXIMUM_TRACKED_PLAYERS];
static struct network_fairplay_stats fairplay_last_game[MAXIMUM_TRACKED_PLAYERS];
static long fairplay_last_game_count;

/* ---------- private code */

static long fairplay_mode(
	void)
{
	char const *mode = config_string("network.fair_play");

	if (mode && !strcmp(mode, "off"))
		return _fairplay_off;
	if (mode && !strcmp(mode, "log"))
		return _fairplay_log;
	return _fairplay_refuse;
}

static real fairplay_dot(
	real_vector3d const *a,
	real_vector3d const *b)
{
	return a->i * b->i + a->j * b->j + a->k * b->k;
}

/* the angle between the aim (one long) and the direction (any length, not
0), in degrees */
static real fairplay_angle(
	real_vector3d const *aim,
	real_vector3d const *direction)
{
	real length = magnitude3d(direction);
	real cosine;

	if (!(length > 0.0f))
		return 0.0f;
	cosine = fairplay_dot(aim, direction) / length;
	cosine = PIN(cosine, -1.0f, 1.0f);
	return FAIRPLAY_DEGREES((real)acos(cosine));
}

/* whether the map's structure is in the way from the one point to the
other: met further than the margin from both */
static boolean fairplay_blocked(
	real_point3d const *from,
	real_point3d const *to)
{
	struct collision_result collision;
	real_vector3d vector;
	real length;
	real at;

	vector_from_points3d(from, to, &vector);
	length = magnitude3d(&vector);
	if (!(length > 2.0f * FAIRPLAY_WALL_MARGIN))
		return FALSE;
	if (!collision_test_vector(FAIRPLAY_WALL_FLAGS, from, &vector, NONE, &collision))
		return FALSE;
	at = collision.t * length;
	return at > FAIRPLAY_WALL_MARGIN && at < length - FAIRPLAY_WALL_MARGIN;
}

/* whether a solid wall is between the eyes and the point: blocked both
ways, and from around the eyes (above, below, either side) too */
static boolean fairplay_solid_wall(
	real_point3d const *eye,
	real_point3d const *point)
{
	real_vector3d forward;
	real_vector3d side;
	short offset;

	if (!fairplay_blocked(eye, point) || !fairplay_blocked(point, eye))
		return FALSE;
	vector_from_points3d(eye, point, &forward);
	if (normalize3d(cross_product3d(global_up3d, &forward, &side)) == 0.0f)
		side = *global_left3d;
	for (offset = 0; offset < 4; offset++)
	{
		real_point3d from = *eye;

		switch (offset)
		{
		case 0: point_from_line3d(eye, &side, FAIRPLAY_EYE_OFFSET, &from); break;
		case 1: point_from_line3d(eye, &side, -FAIRPLAY_EYE_OFFSET, &from); break;
		case 2: from.z += FAIRPLAY_EYE_OFFSET; break;
		default: from.z -= FAIRPLAY_EYE_OFFSET; break;
		}
		if (!fairplay_blocked(&from, point))
			return FALSE;
	}
	return TRUE;
}

/* the player's state, set out for a new player in the slot */
static struct fairplay_player *fairplay_player_get(
	short player_index)
{
	struct player_datum *player = distributed_player(player_index);
	struct fairplay_player *fairplay = &fairplay_players[player_index];
	long player_id;

	if (!player)
		return NULL;
	player_id = DATUM_INDEX_NEW(player_index, player->identifier);
	if (fairplay->player_id != player_id)
	{
		short character;

		csmemset(fairplay, 0, sizeof(*fairplay));
		fairplay->player_id = player_id;
		fairplay->last_unit = NONE;
		fairplay->airborne_since = NONE;
		for (character = 0; character < (short)NUMBEROF(player->name) && player->name[character] &&
			character < (short)sizeof(fairplay->stats.name) - 1; character++)
		{
			fairplay->stats.name[character] = player_name_character_ascii(player->name[character]);
		}
		fairplay->stats.name[character] = 0;
	}
	fairplay->stats.machine_index = distributed_player_machine(player_index);
	if (!fairplay->stats.hardware_id[0] && fairplay->stats.machine_index != NONE)
	{
		char const *hardware_id = network_game_server_machine_hardware_id(fairplay->stats.machine_index);

		if (hardware_id)
			p2p_hardware_id_sanitize(fairplay->stats.hardware_id, 33, hardware_id);
		distributed_machine_address_text(fairplay->stats.machine_index, fairplay->stats.address,
			sizeof(fairplay->stats.address));
	}
	return fairplay;
}

/* whether the player's findings make them one to look into */
static void fairplay_judge(
	short player_index,
	struct fairplay_player *fairplay)
{
	struct network_fairplay_stats *stats = &fairplay->stats;
	boolean hidden_aim = stats->hidden_enemy_seconds >= FAIRPLAY_SUSPECT_HIDDEN_ENEMY_SECONDS &&
		stats->hidden_aim_seconds >= FAIRPLAY_SUSPECT_HIDDEN_AIM_SHARE * stats->hidden_enemy_seconds;

	(void)player_index;
	stats->suspect = stats->refused_wall_hits + stats->refused_aim_hits >= FAIRPLAY_SUSPECT_REFUSED_HITS ||
		hidden_aim || stats->teleport_attempts >= FAIRPLAY_SUSPECT_TELEPORTS ||
		stats->flying_seconds >= FAIRPLAY_SUSPECT_FLYING_SECONDS;
	if (stats->suspect && !fairplay->flagged)
	{
		char reason[128];

		fairplay->flagged = TRUE;
		snprintf(reason, sizeof(reason), "fair play: suspect (%ld refused hits, hidden aim %.0f%%, "
			"%ld teleports, %.0fs flying)", stats->refused_wall_hits + stats->refused_aim_hits,
			stats->hidden_enemy_seconds > 0.0f ? 100.0f * stats->hidden_aim_seconds / stats->hidden_enemy_seconds : 0.0f,
			stats->teleport_attempts, stats->flying_seconds);
		error(_error_log, "%s: %s", reason, stats->name);
		if (stats->machine_index != NONE)
			distributed_log_player(FAIRPLAY_FILE, stats->machine_index, reason);
	}
}

/* a line about the player's hit in fairplay.txt and the log, while they
have lines left this game */
static void fairplay_note(
	struct fairplay_player *fairplay,
	char const *reason)
{
	if (fairplay->lines >= FAIRPLAY_MAXIMUM_LINES || fairplay->stats.machine_index == NONE)
		return;
	fairplay->lines++;
	error(_error_silent, "%s: %s", reason, fairplay->stats.name);
	distributed_log_player(FAIRPLAY_FILE, fairplay->stats.machine_index, reason);
}

/* the player (index) whose living unit, or what it rides, the object is;
NONE for none */
static short fairplay_object_player(
	long object_index)
{
	short player_index;

	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		struct player_datum *player = distributed_player(player_index);
		long unit_index = player ? distributed_living_unit(player) : NONE;

		if (unit_index == NONE)
			continue;
		if (unit_index == object_index || object_get(unit_index)->object.parent_object_index == object_index)
			return player_index;
	}
	return NONE;
}

/* whether the two players are enemies (anyone in a game without teams) */
static boolean fairplay_enemies(
	struct player_datum const *a,
	struct player_datum const *b)
{
	return !game_engine_has_teams() || a->team_index != b->team_index;
}

/* each player's aim at hidden enemies, and whether one near them is hidden
(the ESP's tell: aim held on players the walls hide) */
static void fairplay_note_hidden_aim(
	void)
{
	long now = game_time_get();
	boolean aim_sample = now % FAIRPLAY_AIM_SAMPLE_TICKS == 0;
	boolean exposure_sample = now % FAIRPLAY_EXPOSURE_SAMPLE_TICKS == 0;
	short player_index;

	if (!aim_sample && !exposure_sample)
		return;
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		struct fairplay_view const *view = &fairplay_views[player_index][now & (FAIRPLAY_HISTORY_TICKS - 1)];
		struct player_datum *player = distributed_player(player_index);
		struct fairplay_player *fairplay;
		boolean aimed_hidden = FALSE;
		boolean hidden_near = FALSE;
		short other_index;

		if (!player || view->time != now || !view->on_foot || distributed_player_is_local(player_index))
			continue;
		fairplay = fairplay_player_get(player_index);
		for (other_index = 0; other_index < MAXIMUM_TRACKED_PLAYERS; other_index++)
		{
			struct player_datum *other = other_index != player_index ? distributed_player(other_index) : NULL;
			long other_unit = other ? distributed_living_unit(other) : NONE;
			real_point3d center;
			real_vector3d direction;
			real distance;
			boolean aimed;

			if (other_unit == NONE || !fairplay_enemies(player, other))
				continue;
			center = object_get(other_unit)->object.bounding_sphere_center;
			vector_from_points3d(&view->eye, &center, &direction);
			distance = magnitude3d(&direction);
			if (!(distance >= FAIRPLAY_ENEMY_NEAREST && distance <= FAIRPLAY_ENEMY_FARTHEST))
				continue;
			aimed = aim_sample && fairplay_angle(&view->aim, &direction) <=
				FAIRPLAY_DEGREES((real)atan(FAIRPLAY_BODY_RADIUS / distance)) + FAIRPLAY_AIM_SLACK_DEGREES;
			/* (a line of sight only where it counts: aimed at, or the
			exposure's sample while none hidden is found yet) */
			if (!aimed && !(exposure_sample && !hidden_near))
				continue;
			if (!fairplay_blocked(&view->eye, &center))
				continue;
			hidden_near = TRUE;
			if (aimed)
				aimed_hidden = TRUE;
		}
		if (aimed_hidden)
			fairplay->stats.hidden_aim_seconds += (real)FAIRPLAY_AIM_SAMPLE_TICKS / TICKS_PER_SECOND;
		if (exposure_sample && hidden_near)
		{
			fairplay->stats.hidden_enemy_seconds += (real)FAIRPLAY_EXPOSURE_SAMPLE_TICKS / TICKS_PER_SECOND;
			fairplay_judge(player_index, fairplay);
		}
	}
}

/* the player's ping, for a line about their moves (a laggy link has some
refused, a cheat many) */
static long fairplay_ping(
	short player_index)
{
	long ping = distributed_player_ping(player_index);

	return ping > 0 ? ping : 0;
}

/* a new life, a ride got into or out of: the next refusals not counted; and
in the air neither rising nor falling for long (flying), on foot */
static void fairplay_note_moves(
	short player_index,
	struct fairplay_player *fairplay,
	long unit_index,
	boolean on_foot)
{
	struct biped_datum *biped = (struct biped_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_biped);
	long now = game_time_get();
	real z = object_get(unit_index)->object.position.z;
	boolean airborne;

	if (unit_index != fairplay->last_unit || on_foot != fairplay->last_on_foot)
	{
		fairplay->grace_until = now + FAIRPLAY_MOVE_GRACE_TICKS;
		fairplay->airborne_since = NONE;
	}
	fairplay->last_unit = unit_index;
	fairplay->last_on_foot = on_foot;
	fairplay->last_position = object_get(unit_index)->object.position;
	airborne = on_foot && biped && TEST_FLAG(biped->biped.flags, _biped_airborne_bit);
	if (!airborne || now < fairplay->grace_until)
	{
		fairplay->airborne_since = NONE;
		return;
	}
	if (fairplay->airborne_since == NONE)
	{
		fairplay->airborne_since = now;
		fairplay->air_low = fairplay->air_high = z;
		return;
	}
	fairplay->air_low = MIN(fairplay->air_low, z);
	fairplay->air_high = MAX(fairplay->air_high, z);
	if (now - fairplay->airborne_since < FAIRPLAY_HOVER_TICKS || fairplay->air_high - fairplay->air_low >= FAIRPLAY_HOVER_RANGE)
		return;
	fairplay->stats.flying_seconds += 1.0f / TICKS_PER_SECOND;
	if (now >= fairplay->hover_noted_until)
	{
		char reason[96];

		fairplay->hover_noted_until = now + 5 * TICKS_PER_SECOND;
		network_recording_move(player_index, NETWORK_FAIRPLAY_MOVE_HOVER, fairplay->air_high - fairplay->air_low);
		if (fairplay->move_lines < FAIRPLAY_MAXIMUM_MOVE_LINES)
		{
			fairplay->move_lines++;
			snprintf(reason, sizeof(reason), "fair play: hovering, %.1fs in the air within %.1f (ping %ld ms)",
				(real)(now - fairplay->airborne_since) / TICKS_PER_SECOND, fairplay->air_high - fairplay->air_low,
				fairplay_ping(player_index));
			fairplay_note(fairplay, reason);
		}
	}
	fairplay_judge(player_index, fairplay);
}

/* the game's findings kept as the last game's, and a line in fairplay.txt
for each player with a hit through a wall, off aim or after a snap, or aim
on a hidden enemy */
static void fairplay_end_game(
	void)
{
	short player_index;

	fairplay_last_game_count = 0;
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		struct fairplay_player const *fairplay = &fairplay_players[player_index];
		struct network_fairplay_stats const *stats = &fairplay->stats;
		char reason[96];

		if (fairplay->player_id == NONE || fairplay->player_id == 0)
			continue;
		fairplay_last_game[fairplay_last_game_count++] = *stats;
		/* (the line only for a player with anything to look at) */
		if (!stats->wall_hits && !stats->off_aim_hits && !stats->snap_hits && !(stats->hidden_aim_seconds > 0.0f) &&
			!stats->teleport_attempts && !(stats->flying_seconds > 0.0f))
		{
			continue;
		}
		snprintf(reason, sizeof(reason), "fair play game:%s hits %ld wall %ld/%ld aim %ld/%ld snap %ld hs %ld/%ld hidden %.0f/%.0fs tp %ld fly %.0fs",
			stats->suspect ? " SUSPECT" : "", stats->checked_hits, stats->refused_wall_hits, stats->wall_hits,
			stats->refused_aim_hits, stats->off_aim_hits, stats->snap_hits, stats->headshot_kills, stats->kills,
			stats->hidden_aim_seconds, stats->hidden_enemy_seconds, stats->teleport_attempts, stats->flying_seconds);
		if (stats->machine_index != NONE)
			distributed_log_player(FAIRPLAY_FILE, stats->machine_index, reason);
	}
}

/* ---------- public code */

void network_fairplay_new_game(
	void)
{
	short player_index;
	short tick;

	fairplay_end_game();
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		csmemset(&fairplay_players[player_index], 0, sizeof(fairplay_players[player_index]));
		fairplay_players[player_index].player_id = NONE;
		for (tick = 0; tick < FAIRPLAY_HISTORY_TICKS; tick++)
			fairplay_views[player_index][tick].time = NONE;
	}
}

void network_fairplay_host_tick(
	void)
{
	long now = game_time_get();
	short player_index;

	if (fairplay_mode() == _fairplay_off)
		return;
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		struct fairplay_view *view = &fairplay_views[player_index][now & (FAIRPLAY_HISTORY_TICKS - 1)];
		struct player_datum *player = distributed_player(player_index);
		long unit_index = player ? distributed_living_unit(player) : NONE;
		struct unit_datum *unit;
		struct fairplay_player *fairplay;

		view->time = NONE;
		if (unit_index == NONE)
			continue;
		unit = unit_get(unit_index);
		fairplay = fairplay_player_get(player_index);
		unit_get_camera_position(unit_index, &view->eye);
		view->aim = unit->unit.aiming_vector;
		if (normalize3d(&view->aim) == 0.0f)
			continue;
		view->on_foot = unit->object.parent_object_index == NONE;
		view->time = now;
		if (fairplay)
			fairplay_note_moves(player_index, fairplay, unit_index, view->on_foot);
	}
	fairplay_note_hidden_aim();
}

boolean network_fairplay_hit(
	short player_index,
	long target_object_index,
	real_point3d const *impact,
	long host_time,
	real flight_ticks,
	boolean bent)
{
	long mode = fairplay_mode();
	long now = game_time_get();
	long earliest;
	long time;
	long views = 0;
	long tests = 0;
	long stride;
	short victim_index;
	struct fairplay_player *fairplay;
	struct fairplay_view const *latest = NULL;
	struct fairplay_view const *best = NULL;
	long best_time = NONE;
	real best_angle = 180.0f;
	real distance = 0.0f;
	boolean seen = FALSE;
	boolean wall;
	boolean solid = FALSE;
	boolean off_aim;
	boolean refused_aim;
	boolean snap = FALSE;
	char reason[96];

	if (mode == _fairplay_off || player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS)
		return TRUE;
	victim_index = fairplay_object_player(target_object_index);
	if (victim_index == NONE || victim_index == player_index || !(fairplay = fairplay_player_get(player_index)))
		return TRUE;
	/* the ticks it could have been fired in: its flight before the report's
	tick, and up to now (where the host has the shooter by the time their
	report comes) */
	if (!(flight_ticks >= 0.0f) || flight_ticks > (real)FAIRPLAY_MAXIMUM_FLIGHT_TICKS)
		flight_ticks = (real)FAIRPLAY_MAXIMUM_FLIGHT_TICKS;
	earliest = host_time - (long)flight_ticks - FAIRPLAY_SLACK_TICKS;
	if (earliest < now - FAIRPLAY_HISTORY_TICKS + 1)
		earliest = now - FAIRPLAY_HISTORY_TICKS + 1;
	for (time = earliest; time <= now; time++)
	{
		struct fairplay_view const *view = &fairplay_views[player_index][time & (FAIRPLAY_HISTORY_TICKS - 1)];
		real_vector3d direction;
		real angle;

		if (view->time != time)
			continue;
		/* (from a vehicle: a turret's aim is not the eyes', nor its muzzle) */
		if (!view->on_foot)
			return TRUE;
		vector_from_points3d(&view->eye, impact, &direction);
		angle = fairplay_angle(&view->aim, &direction);
		if (angle < best_angle)
		{
			best_angle = angle;
			best = view;
			best_time = time;
		}
		latest = view;
		views++;
	}
	if (!views)
		return TRUE;
	distance = (real)sqrt((impact->x - latest->eye.x) * (impact->x - latest->eye.x) +
		(impact->y - latest->eye.y) * (impact->y - latest->eye.y) +
		(impact->z - latest->eye.z) * (impact->z - latest->eye.z));
	/* through a wall: no tick's eyes saw the impact (tested at a stride
	over the ticks, the latest first, until one did) */
	stride = MAX(1, views / FAIRPLAY_MAXIMUM_SIGHT_TESTS);
	if (distance < FAIRPLAY_WALL_MINIMUM_DISTANCE || !fairplay_blocked(&latest->eye, impact))
		seen = TRUE;
	for (time = now, tests = 1; !seen && time >= earliest && tests < FAIRPLAY_MAXIMUM_SIGHT_TESTS; time -= stride)
	{
		struct fairplay_view const *view = &fairplay_views[player_index][time & (FAIRPLAY_HISTORY_TICKS - 1)];

		if (view->time != time || view == latest)
			continue;
		tests++;
		if (!fairplay_blocked(&view->eye, impact))
			seen = TRUE;
	}
	wall = !seen;
	if (wall)
		solid = fairplay_solid_wall(&latest->eye, impact);
	/* off aim: never near it (a guided or falling shot's path is not the
	aim's, nor a near one's) */
	off_aim = !bent && distance >= FAIRPLAY_AIM_MINIMUM_DISTANCE && best_angle > FAIRPLAY_OFF_AIM_DEGREES;
	refused_aim = off_aim && best_angle > FAIRPLAY_REFUSED_AIM_DEGREES;
	/* a snap: a fast turn in the ticks before the aim came on it */
	if (!bent && best && best_angle <= FAIRPLAY_SNAP_END_DEGREES)
	{
		long back;

		for (back = best_time; back > best_time - FAIRPLAY_SLACK_TICKS && back > earliest; back--)
		{
			struct fairplay_view const *view = &fairplay_views[player_index][back & (FAIRPLAY_HISTORY_TICKS - 1)];
			struct fairplay_view const *before = &fairplay_views[player_index][(back - 1) & (FAIRPLAY_HISTORY_TICKS - 1)];

			if (view->time != back || before->time != back - 1)
				break;
			if (fairplay_angle(&before->aim, &view->aim) >= FAIRPLAY_SNAP_TURN_DEGREES)
			{
				snap = TRUE;
				break;
			}
		}
	}

	fairplay->stats.checked_hits++;
	if (wall)
		fairplay->stats.wall_hits++;
	if (off_aim)
		fairplay->stats.off_aim_hits++;
	if (snap)
		fairplay->stats.snap_hits++;
	if (mode == _fairplay_refuse && wall && solid)
		fairplay->stats.refused_wall_hits++;
	if (mode == _fairplay_refuse && refused_aim)
		fairplay->stats.refused_aim_hits++;
	if (wall || off_aim || snap)
	{
		struct fairplay_player const *victim = &fairplay_players[victim_index];

		snprintf(reason, sizeof(reason), "fair play: %s%s%s on %.23s, %.1f away, %.0f deg off aim%s",
			wall ? (solid ? "through solid wall" : "through wall") : "",
			wall && (off_aim || snap) ? ", " : "",
			off_aim ? "off aim" : snap ? "snap" : "", victim->stats.name, distance, best_angle,
			mode == _fairplay_refuse && ((wall && solid) || refused_aim) ? " (refused)" : "");
		fairplay_note(fairplay, reason);
	}
	fairplay_judge(player_index, fairplay);
	/* (in the game's recording, network_recording.c) */
	network_recording_hit(player_index, victim_index, impact,
		(byte)((wall ? NETWORK_RECORDING_HIT_WALL : 0) | (wall && solid ? NETWORK_RECORDING_HIT_SOLID_WALL : 0) |
			(off_aim ? NETWORK_RECORDING_HIT_OFF_AIM : 0) | (snap ? NETWORK_RECORDING_HIT_SNAP : 0) |
			(mode == _fairplay_refuse && ((wall && solid) || refused_aim) ? NETWORK_RECORDING_HIT_REFUSED : 0)),
		best_angle, distance);
	return !(mode == _fairplay_refuse && ((wall && solid) || refused_aim));
}

void network_fairplay_damage(
	long owner_player_index,
	long victim_player_index,
	boolean headshot,
	boolean killing)
{
	short owner;
	struct fairplay_player *fairplay;

	/* (in the game's recording, network_recording.c, whatever the checks) */
	if (killing && owner_player_index != NONE && victim_player_index != NONE)
	{
		network_recording_kill((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(owner_player_index),
			(short)DATUM_INDEX_TO_ABSOLUTE_INDEX(victim_player_index), headshot);
	}
	if (!killing || owner_player_index == NONE || victim_player_index == NONE ||
		owner_player_index == victim_player_index || fairplay_mode() == _fairplay_off)
	{
		return;
	}
	owner = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(owner_player_index);
	if (owner < 0 || owner >= MAXIMUM_TRACKED_PLAYERS || !(fairplay = fairplay_player_get(owner)))
		return;
	fairplay->stats.kills++;
	if (headshot)
		fairplay->stats.headshot_kills++;
}

void network_fairplay_refused_move(
	short player_index,
	short kind,
	real amount)
{
	struct fairplay_player *fairplay;
	long now = game_time_get();
	boolean new_burst;

	if (fairplay_mode() == _fairplay_off || player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS ||
		distributed_player_is_local(player_index) || !(fairplay = fairplay_player_get(player_index)) ||
		now < fairplay->grace_until)
	{
		return;
	}
	/* (the host moved them itself since its last tick, a teleporter: their
	game's word is late, and the next few with it) */
	{
		long unit_index = distributed_living_unit(distributed_player(player_index));

		if (unit_index != NONE && unit_index == fairplay->last_unit)
		{
			real_point3d const *at = &object_get(unit_index)->object.position;
			real dx = at->x - fairplay->last_position.x;
			real dy = at->y - fairplay->last_position.y;
			real dz = at->z - fairplay->last_position.z;

			if (dx * dx + dy * dy + dz * dz > FAIRPLAY_HOST_JUMP * FAIRPLAY_HOST_JUMP)
			{
				fairplay->grace_until = now + FAIRPLAY_MOVE_GRACE_TICKS;
				fairplay->last_position = *at;
				return;
			}
		}
	}
	new_burst = now > fairplay->burst_until;
	fairplay->burst_until = now + FAIRPLAY_MOVE_BURST_TICKS;
	/* (too high: each refusal a tick of flying; too far or too fast: a
	burst of them one teleport) */
	if (kind == NETWORK_FAIRPLAY_MOVE_HIGH)
		fairplay->stats.flying_seconds += 1.0f / TICKS_PER_SECOND;
	else if (new_burst)
		fairplay->stats.teleport_attempts++;
	if (new_burst)
	{
		char reason[96];

		network_recording_move(player_index, (byte)kind, amount);
		if (fairplay->move_lines < FAIRPLAY_MAXIMUM_MOVE_LINES)
		{
			fairplay->move_lines++;
			snprintf(reason, sizeof(reason), "fair play: move refused, %s %.1f (ping %ld ms)",
				kind == NETWORK_FAIRPLAY_MOVE_FAR ? "too far from the host's," :
				kind == NETWORK_FAIRPLAY_MOVE_FAST ? "too fast, moved" : "too high (flying),", amount,
				fairplay_ping(player_index));
			fairplay_note(fairplay, reason);
		}
	}
	fairplay_judge(player_index, fairplay);
}

void network_fairplay_moved_by_host(
	short player_index)
{
	struct fairplay_player *fairplay;

	if (player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS || !(fairplay = fairplay_player_get(player_index)))
		return;
	fairplay->grace_until = game_time_get() + FAIRPLAY_MOVE_GRACE_TICKS;
	fairplay->airborne_since = NONE;
}

boolean network_fairplay_entry(
	boolean last,
	long index,
	struct network_fairplay_stats *stats)
{
	long player_index;
	long found = 0;

	if (last)
	{
		if (index < 0 || index >= fairplay_last_game_count)
			return FALSE;
		*stats = fairplay_last_game[index];
		return TRUE;
	}
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		if (fairplay_players[player_index].player_id == NONE || fairplay_players[player_index].player_id == 0)
			continue;
		if (found++ == index)
		{
			*stats = fairplay_players[player_index].stats;
			return TRUE;
		}
	}
	return FALSE;
}
