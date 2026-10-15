/*
SIGHTJACK.H

Port (this fork): a moderator watching a player through their eyes
(sightjack.c), on a dedicated server whose moderation they signed in to.
*/

#ifndef __SIGHTJACK_H
#define __SIGHTJACK_H

/* ---------- prototypes */

/* whether this machine's player may watch others: signed in to the
server's moderation with a role */
boolean sightjack_allowed(void);
/* starts watching the machine's player (moderation_screen.c's Watch) */
void sightjack_watch(short machine_index);
void sightjack_stop(void);
/* the unit the local player watches (director.c), NONE when not watching
(or the one watched has no living unit) */
long sightjack_watched_unit(short local_player_index);
/* whether the object is drawn not at all: the watched unit and what it
holds, from inside whose head the camera looks (render_objects.c) */
boolean sightjack_object_hidden(long object_index);
/* the director's camera while watching (director_camera_update_proc) */
void sightjack_camera_update(void *camera, void *controls, void *result);
/* each frame (moderation_screen_update): the buttons that switch players
and stop; and over the game, who is watched (moderation_screen_render) */
void sightjack_update(boolean menu_open);
void sightjack_render(void);

#endif
