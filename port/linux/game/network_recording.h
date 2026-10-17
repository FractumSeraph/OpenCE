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

/* one of the game's events as Delta Stats' recorder notes it (game_events.c,
event_log.h's EVENT_LOG_*: a kill with its damage, a medal, an objective, a
pickup, a ride, a spawn, a moderator's action, an anti-cheat's flag): its
players (player indices, NONE for none), its tags' names (NULL for none),
values, bits and positions, as event_log_record has them */
void network_recording_event(long tick, short type, short player_index, short other_index,
	char const *const tags[3], int const values[4], unsigned long bits, float const position[3],
	float const other_position[3]);
/* a chat line the host sent on (chat.c): its kind and scope (delta_wire.h's),
who said it (the game's player list's index, NONE for the server or the
game) and their name, and its text */
void network_recording_chat(byte kind, byte scope, short speaker_index, char const *name, int name_length,
	char const *text, int length);

/* the public copy (network.record_public) being written, or the last one:
its file's name in recordings\public (0 if none), for the carnage
report of its game to be linked to it */
int network_recording_public_name(char *name, int size);

/* a public copy's game's carnage report, once the game list took it: its
address, kept beside the copy as <name>.json for the replay viewer and the
game list to link the two (any thread) */
void network_recording_public_report(char const *name, char const *report_url);

#endif
