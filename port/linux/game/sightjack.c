/*
SIGHTJACK.C

Port (this fork): a moderator watching a player through their eyes
(sightjack.h), as Halo PC's admins did with SightJack, to see for
themselves whether someone tracks players through walls or snaps onto them.

A player signed in to a dedicated server's moderation (moderation_screen.c,
Delta Peer) with a role that may warn picks Watch on a player there. Their
camera then looks from that player's eyes along their aim, as the player's
own first-person camera does (first_person_camera_fake), the watched
player's body and what it holds not drawn (the camera is inside its head);
their own player stands still (the director takes no input from them), and
their own first-person weapon and HUD are not drawn (the director's
perspective is the third person's), only a line saying whom they watch. Left and right (the d-pad, the
shoulder buttons, the arrow keys) watch the previous and next player, B (or
Backspace) stops, as does losing the role, the game ending or the player
leaving.

It needs nothing of the server but its moderation: what a client is sent
of the other players (every player, NETCODE.md) is what is drawn. A
dedicated server of this fork sends its moderators' machines every player
every tick (network_distributed.c's full view), as it does a machine whose
players are dead; on another server, players out of a moderator's own
sight come a few times a second. Record it with any screen recorder.
*/

#include "cseries.h"
#include "camera/first_person_camera.h"
#include "game/game.h"
#include "game/players.h"
#include "input/input.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "units/units.h"
#include "sightjack.h"
#ifdef HALO_GAME_BROWSER
#include "../src/delta_moderation.h"
#include "../src/ui_overlay.h"
#endif

#include <stdio.h>
#include <string.h>

void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	COLOR_PANEL = 0x081530D0,
	COLOR_EDGE = 0x2F6DD0FF,
	COLOR_TITLE = 0xFF5A4AFF,
	COLOR_TEXT = 0xE6EEFCFF,
	COLOR_DIM = 0x8FA6C8FF,
	/* an analog button's press, out of 255 */
	PRESS_THRESHOLD = 128,
};

enum
{
	_sightjack_button_previous = 0,
	_sightjack_button_next,
	_sightjack_button_stop,
	NUMBER_OF_SIGHTJACK_BUTTONS
};

/* ---------- globals */

static struct
{
	boolean active;
	/* the player watched (a datum index), and their living unit this frame */
	long player_index;
	long unit_index;
	char name[16];
	boolean held[NUMBER_OF_SIGHTJACK_BUTTONS];
} sightjack = { FALSE, NONE, NONE };

/* ---------- private code */

/* the player's name, as ASCII */
static void sightjack_name(
	struct player_datum const *player,
	char *name,
	long size)
{
	long character;

	for (character = 0; character < (long)NUMBEROF(player->name) && player->name[character] && character < size - 1;
		character++)
	{
		name[character] = player_name_character_ascii(player->name[character]);
	}
	name[character] = 0;
}

/* the next player after the one watched (the previous one: backward) that
is not this machine's, wrapping round; NONE for none */
static long sightjack_next_player(
	boolean backward)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long candidates[64];
	long count = 0;
	long index;
	long at = NONE;

	if (!player_data)
		return NONE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL && count < (long)NUMBEROF(candidates))
	{
		if (player->local_player_index != NONE)
			continue;
		if (iterator.datum_index == sightjack.player_index)
			at = count;
		candidates[count++] = iterator.datum_index;
	}
	if (!count)
		return NONE;
	if (at == NONE)
		return candidates[0];
	index = backward ? (at + count - 1) % count : (at + 1) % count;
	return candidates[index];
}

/* whether the button was pressed since the last frame (held counts once) */
static boolean sightjack_pressed(
	short button,
	boolean down)
{
	boolean pressed = down && !sightjack.held[button];

	sightjack.held[button] = down;
	return pressed;
}

/* ---------- public code */

boolean sightjack_allowed(
	void)
{
#ifdef HALO_GAME_BROWSER
	struct delta_peer_game_moderation moderation;

	delta_peer_game_client_moderation(&moderation);
	return moderation.available && moderation.signed_in && moderation.role >= _delta_moderation_role_moderator &&
		(moderation.permissions & DELTA_MODERATION_WARN) != 0;
#else
	return FALSE;
#endif
}

void sightjack_watch(
	short machine_index)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (!player_data || !sightjack_allowed())
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->local_player_index == NONE && player->network_player_data.machine_index == machine_index)
		{
			sightjack.active = TRUE;
			sightjack.player_index = iterator.datum_index;
			sightjack.unit_index = NONE;
			sightjack_name(player, sightjack.name, sizeof(sightjack.name));
			/* (the button that chose it is not a switch) */
			csmemset(sightjack.held, TRUE, sizeof(sightjack.held));
			platform_log("moderation: watching %s", sightjack.name);
			return;
		}
	}
}

void sightjack_stop(
	void)
{
	if (sightjack.active)
		platform_log("moderation: stopped watching %s", sightjack.name);
	sightjack.active = FALSE;
	sightjack.player_index = NONE;
	sightjack.unit_index = NONE;
}

long sightjack_watched_unit(
	short local_player_index)
{
	return sightjack.active && local_player_index == 0 ? sightjack.unit_index : NONE;
}

boolean sightjack_object_hidden(
	long object_index)
{
	struct object_datum *object;

	if (!sightjack.active || sightjack.unit_index == NONE || object_index == NONE)
		return FALSE;
	if (object_index == sightjack.unit_index)
		return TRUE;
	object = (struct object_datum *)object_try_and_get(object_index);
	return object && object->object.parent_object_index == sightjack.unit_index;
}

void sightjack_camera_update(
	void *camera,
	void *controls,
	void *result)
{
	(void)camera;
	(void)controls;
	/* (the first-person camera's command, at the start of the result) */
	if (sightjack.unit_index != NONE && object_try_and_get(sightjack.unit_index))
		first_person_camera_fake(sightjack.unit_index, (struct camera_command *)result);
}

void sightjack_update(
	boolean menu_open)
{
	struct player_datum *player;
	struct gamepad_state const *gamepad;
	boolean down[NUMBER_OF_SIGHTJACK_BUTTONS];
	short button;

	if (!sightjack.active)
		return;
	if (!game_in_progress() || !player_data || !sightjack_allowed())
	{
		sightjack_stop();
		return;
	}
	player = (struct player_datum *)datum_try_and_get(player_data, sightjack.player_index);
	/* (the one watched left: the next, if anyone is left) */
	if (!player)
	{
		sightjack.player_index = sightjack_next_player(FALSE);
		player = sightjack.player_index != NONE ?
			(struct player_datum *)datum_try_and_get(player_data, sightjack.player_index) : NULL;
		if (!player)
		{
			sightjack_stop();
			return;
		}
		sightjack_name(player, sightjack.name, sizeof(sightjack.name));
	}
	sightjack.unit_index = player->unit_index;
	if (menu_open)
		return;
	gamepad = input_has_gamepad(0) ? input_get_gamepad_state(0) : NULL;
	down[_sightjack_button_previous] = input_key_is_down(_key_left_arrow) ||
		(gamepad && (gamepad->buttons[_gamepad_binary_button_dpad_left] ||
			gamepad->analog_buttons[_gamepad_analog_button_white] >= PRESS_THRESHOLD));
	down[_sightjack_button_next] = input_key_is_down(_key_right_arrow) ||
		(gamepad && (gamepad->buttons[_gamepad_binary_button_dpad_right] ||
			gamepad->analog_buttons[_gamepad_analog_button_black] >= PRESS_THRESHOLD));
	down[_sightjack_button_stop] = input_key_is_down(_key_backspace) ||
		(gamepad && gamepad->analog_buttons[_gamepad_analog_button_b] >= PRESS_THRESHOLD);
	for (button = 0; button < NUMBER_OF_SIGHTJACK_BUTTONS; button++)
	{
		if (!sightjack_pressed(button, down[button]))
			continue;
		if (button == _sightjack_button_stop)
		{
			sightjack_stop();
			return;
		}
		sightjack.player_index = sightjack_next_player(button == _sightjack_button_previous);
		player = sightjack.player_index != NONE ?
			(struct player_datum *)datum_try_and_get(player_data, sightjack.player_index) : NULL;
		if (!player)
		{
			sightjack_stop();
			return;
		}
		sightjack_name(player, sightjack.name, sizeof(sightjack.name));
		sightjack.unit_index = player->unit_index;
		platform_log("moderation: watching %s", sightjack.name);
	}
}

void sightjack_render(
	void)
{
#ifdef HALO_GAME_BROWSER
	char text[64];
	float x;

	if (!sightjack.active || !ui_overlay_available())
		return;
	ui_overlay_rect(170, 8, 300, 38, 6, COLOR_PANEL);
	ui_overlay_outline(170, 8, 300, 38, 6, 1.0f, COLOR_EDGE);
	snprintf(text, sizeof(text), "WATCHING  %s%s", sightjack.name, sightjack.unit_index == NONE ? "  (dead)" : "");
	ui_overlay_text(UI_FONT_BOLD, 13.0f, 320, 12, UI_ALIGN_CENTER, COLOR_TITLE, text);
	x = 214;
	x += ui_overlay_button(UI_BUTTON_DPAD_LEFT, 11.0f, x, 29, 0xFFFFFFFF) + 1.0f;
	x += ui_overlay_button(UI_BUTTON_DPAD_RIGHT, 11.0f, x, 29, 0xFFFFFFFF) + 3.0f;
	x += ui_overlay_text(UI_FONT_REGULAR, 10.0f, x, 29, UI_ALIGN_LEFT, COLOR_DIM, "other players") + 14.0f;
	x += ui_overlay_button(UI_BUTTON_B, 11.0f, x, 29, 0xFFFFFFFF) + 3.0f;
	ui_overlay_text(UI_FONT_REGULAR, 10.0f, x, 29, UI_ALIGN_LEFT, COLOR_DIM, "stop (Backspace)");
#endif
}
