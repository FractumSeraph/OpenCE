/*
NETWORK_FAIRPLAY.H

Port (this fork): the host's checks of its clients' shots for cheats
(network_fairplay.c): a hit through a wall, or far from where its shooter
aimed, refused (network.fair_play), the shooter's aim watched for snaps and
for following enemies through walls, and what it found kept for the
server's admins (sv_suspects, fairplay.txt).
*/

#ifndef __NETWORK_FAIRPLAY_H
#define __NETWORK_FAIRPLAY_H

/* ---------- types */

/* what the host found of a player this game (or the last), for sv_suspects */
struct network_fairplay_stats
{
	/* (ASCII, as sv_players has it) */
	char name[24];
	long machine_index;
	/* their machine's hardware id (sv_players') and address, "" for none */
	char hardware_id[40];
	char address[32];
	/* the bullets' hits on players checked, and of them: through a wall
	(and refused: through solid wall), far from the shooter's aim (and
	refused: very far), and on target right after a snap of the aim */
	long checked_hits;
	long wall_hits;
	long refused_wall_hits;
	long off_aim_hits;
	long refused_aim_hits;
	long snap_hits;
	/* their kills of players, and with a headshot */
	long kills;
	long headshot_kills;
	/* seconds an enemy near them was behind a wall, and of those, seconds
	they aimed at one (an ESP's tell) */
	real hidden_enemy_seconds;
	real hidden_aim_seconds;
	/* the host refusing where their game said they were (network_distributed.c):
	bursts of moves too far or too fast (teleports), and seconds too high
	or hovering (flying); never after the host moved them (a teleporter, a
	new life, a vehicle) */
	long teleport_attempts;
	real flying_seconds;
	/* the host's judgement: enough refused hits, or aim on hidden enemies,
	to look into */
	boolean suspect;
};

/* ---------- constants */

/* why the host refused a player's position (network_fairplay_refused_move) */
enum
{
	NETWORK_FAIRPLAY_MOVE_FAR = 1,
	NETWORK_FAIRPLAY_MOVE_FAST,
	NETWORK_FAIRPLAY_MOVE_HIGH,
	/* (the host's own: in the air, neither rising nor falling) */
	NETWORK_FAIRPLAY_MOVE_HOVER,
};

/* ---------- prototypes */

void network_fairplay_new_game(void);
/* (the host, each tick: where each player looks from and aims) */
void network_fairplay_host_tick(void);
/* (the host) a client's hit at a point (a bullet's: no explosion, melee or
collision), otherwise valid, on the object (a player's unit or vehicle, or
anything), made at the host's tick host_time, of a projectile that flies
for up to flight_ticks and goes straight unless bent (guided, or falling):
FALSE to refuse it */
boolean network_fairplay_hit(short player_index, long target_object_index, real_point3d const *impact,
	long host_time, real flight_ticks, boolean bent);
/* (damage.c, through game_events_damage) a player's damage to another's
unit, its headshot and whether it killed */
void network_fairplay_damage(long owner_player_index, long victim_player_index, boolean headshot, boolean killing);
/* (the host, network_distributed.c's distributed_take_prediction) where a
player's game said they were, refused: too far from where the host has
them, too fast since the last taken, or too high above the ground; the
amount (world units) */
void network_fairplay_refused_move(short player_index, short kind, real amount);
/* (the host) the host moved the player's unit itself (a teleporter): their
game's word on it is late for a while */
void network_fairplay_moved_by_host(short player_index);
/* the players' findings: of this game (last FALSE) or the one before; the
entry at the index, FALSE past the last */
boolean network_fairplay_entry(boolean last, long index, struct network_fairplay_stats *stats);

#endif
