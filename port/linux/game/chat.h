/*
CHAT.H

Text chat between Chupathingy machines (chat.c; docs/delta.md, Chat).
*/

#ifndef __CHAT_H
#define __CHAT_H

/* each frame, from the main loop (main.c): the host's routing, the lines
heard, the keys */
void chat_update(void);

/* over the HUD of a local player (hud.c): the recent lines, the line
being typed, the log */
void chat_draw(short local_player_index);

/* the host's (a dedicated server's sv_say, its notices): a server line to
every Chupathingy machine of the game, and this machine's own player; 0 if
no machine could take it */
int chat_server_say(char const *text);

/* a note of this machine's own in the chat (as its refusals are): what
came of the player's ask to change teams (team_change.c) */
void chat_note(char const *text);

/* the host's mode (enum delta_chat_mode) and whether it filters blocked
words: a dedicated server's settings (a player's host reads network.chat
and network.chat_filter) */
void chat_host_set(int mode, int filter);
int chat_host_mode(void);
int chat_host_filter(void);

/* (a dedicated server's) the seconds a player's chat pauses once they say
too much too fast (0: none; 5 unless set), and the welcome each Chupathingy
player is sent as they join (NULL or empty: none) */
void chat_host_set_cooldown(int seconds);
int chat_host_cooldown(void);
void chat_host_set_welcome(char const *text);
char const *chat_host_welcome(void);

/* (the dedicated server's) whether a machine's player is signed in as a
moderator with a role here: its lines are marked as a moderator's */
void chat_set_moderator_check(int (*check)(int machine_index));

/* port (this fork): OpenCE's text chat (network_chat.c), carried in this one
(chat.c, "OpenCE's text chat, bridged"). Players are the player table's
absolute indices, channels network_chat.h's. */
/* a line of OpenCE's chat this machine hears, shown here */
void chat_bridge_heard(short player_index, short channel, char const *text);
/* a notice of OpenCE's chat (its host's), shown here */
void chat_bridge_notice(char const *text);
/* (the host) a line network_chat.c took from an OpenCE machine: sent on to
this chat's machines and shown here (not back to OpenCE's); FALSE where this
chat's host settings refuse it (its filter, its mode), sent to none */
int chat_bridge_host_route(short player_index, short channel, char const *text);
/* whether this machine's host speaks this chat (Delta's): a client then
leaves the OpenCE copies of the host's lines, which it has here already */
int chat_bridge_host_speaks_delta(void);

#endif
