/*
NETWORK_RECORDING.H

Port (this fork): the host's recordings of its games (network_recording.c,
network.record_games), for its admins to watch again in a browser
(port/web/assets/replay).
*/

#ifndef __NETWORK_RECORDING_H
#define __NETWORK_RECORDING_H

/* ---------- constants */

/* a hit's verdict (network_fairplay.c's): what was seen of it */
enum
{
	NETWORK_RECORDING_HIT_WALL = 0x01,
	NETWORK_RECORDING_HIT_SOLID_WALL = 0x02,
	NETWORK_RECORDING_HIT_OFF_AIM = 0x04,
	NETWORK_RECORDING_HIT_SNAP = 0x08,
	NETWORK_RECORDING_HIT_REFUSED = 0x10,
};

/* ---------- prototypes */

/* a game begins (network_damage_new_game): the last one's recording ends */
void network_recording_new_game(void);
/* (the host, each tick, after the fair play checks note where each player
looks from) */
void network_recording_host_tick(void);
/* a bullet's hit on a player the fair play checks looked at: its shooter and
target (player indices), where it hit, the verdict, how far off the
shooter's aim it was (degrees) and how far it went */
void network_recording_hit(short shooter_index, short victim_index, real_point3d const *impact, byte verdict,
	real angle, real distance);
/* a player killed another (or themself) */
void network_recording_kill(short killer_index, short victim_index, boolean headshot);
/* the host refused where a player's game said they were, or saw them hover
(network_fairplay.h's NETWORK_FAIRPLAY_MOVE_*), and by how much */
void network_recording_move(short player_index, byte kind, real amount);

#endif
