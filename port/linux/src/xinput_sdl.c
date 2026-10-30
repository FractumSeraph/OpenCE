/*
XINPUT_SDL.C

Xbox controllers and the debug keyboard for the Linux build.

Port 0 is always connected: it is the keyboard and mouse, merged with the
first SDL gamepad when one is present. Further SDL gamepads take ports 1-3.

In the game the keyboard and mouse are a control scheme of their own
(port/linux/include/halo_keyboard.h): config.toml's [controls] bind each of
the player's actions to up to two keys, mouse buttons or wheel turns
(Settings > Controls Setup changes them), and the game takes the actions
held (halo_keyboard_actions) with the controller's. They press none of its
buttons, but for the pause menu's and the scoreboard's (Start and Back).

In the menus the keys drive the controller, to move about them:
	arrows           D-pad               W A S D          left stick
	space, enter     A                   escape, backspace B
	delete, E        X                   tab              Y
	F1               back                C                black (RB)
(keys held as the game and the menus switch count only once let go of), the
on-screen keyboard takes what is typed, and the mouse is free and drives a
pointer
(port/linux/include/halo_ui_pointer.h, source/interface/ui_widget.c).
The screenshot key (controls.screenshot, F10) saves a PNG, in the menus
too. F11 switches between fullscreen and the window, and F12 releases or
recaptures the mouse, always.

Mouse aim does not go through the right stick: the game's look code asks
halo_linux_mouse_look for the motion since its last call and adds it to the
stick's facing change, so aiming is direct rather than rate based.

The game's debug keyboard exists only for the console. Backquote (which
opens it) always reaches the keystroke queue, everything else only while
the console is open, since the game also polls a few keys directly (escape
returns to the main menu). While the console is open the keyboard does not
drive the controller.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "touch_input.h"
#include "controller_layout.h"
#include "halo_keyboard.h"
#include "../game/network_rules.h"
#include "controller_buttons.h"
#include "controller_remap.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_COUNT 4
#define VK_OEM_3_BACKQUOTE 0xc0

/* ---------- game hooks */

/* main/console.c */
extern unsigned char console_is_active(void);
/* the host's policy on the mouse's aiming assist (network_distributed.c) */
extern unsigned char network_aim_assist_blocked(void);
/* port/linux/game/network_chat.c: a chat line is typed, which takes the
keyboard as the console does */
extern unsigned char network_chat_typing(void);
/* ... and its on-screen keyboard is up, which takes the first controller
and the touch controls' stick and look */
extern unsigned char network_chat_keyboard_open(void);
/* port/linux/game/network_votekick.c: the D-pad's left and right vote now */
extern unsigned char network_vote_pad_open(void);
/* port/linux/game/menu_functions.c: two or more players on this machine
(co-op, or split screen in a network game) */
extern unsigned char pc_menu_split_players(void);
/* port/linux/game/network_test.c: the game's time in seconds while one runs,
else -1 */
extern double network_test_game_seconds(void);

/* ---------- device tables */

XPP_DEVICE_TYPE XDEVICE_TYPE_GAMEPAD_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_MEMORY_UNIT_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE;

struct controller
{
	BOOL open;
	DWORD packet_number;
	XINPUT_GAMEPAD previous;
};

static struct controller controllers[PORT_COUNT];
static struct controller keyboard_device;
static DWORD reported_gamepads = 0;
static BOOL reported_keyboard = FALSE;
/* the 4-way join screen's SPACE (menu_functions.c's join4): the keyboard
plays player 1 alone, and the gamepad that shared port 0 with it takes the
first port no gamepad has (port_gamepad), asked for and in effect */
static BOOL keyboard_alone_wanted, keyboard_alone;
/* the keyboard's SPACE held, as the menus read it (keyboard_gamepad); the
test script's SPACE and Enter (virtual_gamepads_update) */
static BOOL keyboard_join_down, virtual_keyboard_join, virtual_keyboard_done;

/* ---------- mouse */

static pthread_mutex_t mouse_lock = PTHREAD_MUTEX_INITIALIZER;
static float mouse_pending_x, mouse_pending_y;
static unsigned long mouse_polls_unconsumed = 0;
static float mouse_wheel_accumulated = 0.0f;
/* the wheel's notches turned while a chat line is typed, not yet taken
(halo_chat_wheel_steps) */
static int chat_wheel_steps = 0;
/* the wheel's switch (wheel_update): when the wheel last moved, until when
Y is held, and whether a scroll is under way */
static Uint64 wheel_moved_ms = 0;
static Uint64 wheel_press_until_ms = 0;
static BOOL wheel_scrolling = FALSE;
/* the way the scroll under way turns: 1 up (away), -1 down */
static int wheel_direction = 0;
/* when port 0's aim last moved, by the mouse, by the right stick, and by
the touch controls (their swipe, gyroscope or stick: halo_linux_mouse_aiming,
halo_linux_touch_aiming) */
static Uint64 mouse_aimed_ms = 0;
static Uint64 stick_aimed_ms = 0;
/* (and when a touch screen last looked: Android) */
static Uint64 touch_aimed_ms = 0;
/* when the first player last pressed a gamepad's button or moved its
sticks, and a key or mouse button of theirs (halo_gamepad_last_used) */
static Uint64 pad_used_ms = 0;
static Uint64 keyboard_used_ms = 0;

/* the right stick's deflection that counts as aiming with it, clear of a
worn stick's drift */
#define STICK_AIMING_DEFLECTION 8000

/* (input.mouse_vertical_sensitivity, read with it) */
static float vertical_sensitivity = 1.0f;

static float mouse_sensitivity(void)
{
	static float sensitivity;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		sensitivity = (float)config_real("input.mouse_sensitivity");
		if (sensitivity <= 0.0f)
			sensitivity = 1.0f;
		vertical_sensitivity = (float)config_real("input.mouse_vertical_sensitivity");
		if (vertical_sensitivity <= 0.0f)
			vertical_sensitivity = sensitivity;
	}
	return sensitivity;
}

/* radians of yaw and pitch for the mouse motion since the last call; the
game adds these to the facing change of the player on gamepad 0 */
int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch)
{
	/* radians per pixel of relative motion at sensitivity 1 */
	const float scale = 0.0022f;
	static int invert;
	static unsigned long read_at = (unsigned long)-1;
	float x, y;

	*yaw = 0.0f;
	*pitch = 0.0f;
	if (gamepad_index != 0)
		return FALSE;
	if (read_at != config_changes())
	{
		read_at = config_changes();
		invert = config_boolean("input.invert_mouse");
	}
	pthread_mutex_lock(&mouse_lock);
	x = mouse_pending_x;
	y = mouse_pending_y;
	mouse_pending_x = 0.0f;
	mouse_pending_y = 0.0f;
	mouse_polls_unconsumed = 0;
	pthread_mutex_unlock(&mouse_lock);
	if (x == 0.0f && y == 0.0f)
		return FALSE;
	*yaw = -x * scale * mouse_sensitivity();
	*pitch = (invert ? y : -y) * scale * vertical_sensitivity;
	return TRUE;
}

#if defined(HALO_ANDROID) || defined(HALO_WEB)
/* the touch controls moved the player or the aim (halo_linux_touch_aiming) */
static void touch_used(void)
{
	pthread_mutex_lock(&mouse_lock);
	touch_aimed_ms = SDL_GetTicks();
	pthread_mutex_unlock(&mouse_lock);
}
#endif

/* ---------- the text chat's on-screen keyboard (halo_keyboard.h) */

/* Each controller's buttons, before the on-screen keyboard consumes them. */
static unsigned long chat_pad[PORT_COUNT], chat_physical_pad[PORT_COUNT];

static void chat_pad_record(short port, const XINPUT_GAMEPAD *pad)
{
	/* (an analog button pressed half way; the stick most of the way) */
	enum { PRESSED = 0x80, PUSHED = 22000 };
	unsigned long buttons = 0;

	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP) buttons |= 1UL << HALO_CHAT_PAD_UP;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) buttons |= 1UL << HALO_CHAT_PAD_DOWN;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) buttons |= 1UL << HALO_CHAT_PAD_LEFT;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) buttons |= 1UL << HALO_CHAT_PAD_RIGHT;
	if (pad->wButtons & XINPUT_GAMEPAD_START) buttons |= 1UL << HALO_CHAT_PAD_START;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_A] >= PRESSED) buttons |= 1UL << HALO_CHAT_PAD_A;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_B] >= PRESSED) buttons |= 1UL << HALO_CHAT_PAD_B;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_X] >= PRESSED) buttons |= 1UL << HALO_CHAT_PAD_X;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_Y] >= PRESSED) buttons |= 1UL << HALO_CHAT_PAD_Y;
	if (pad->sThumbLY > PUSHED) buttons |= 1UL << HALO_CHAT_PAD_STICK_UP;
	if (pad->sThumbLY < -PUSHED) buttons |= 1UL << HALO_CHAT_PAD_STICK_DOWN;
	if (pad->sThumbLX < -PUSHED) buttons |= 1UL << HALO_CHAT_PAD_STICK_LEFT;
	if (pad->sThumbLX > PUSHED) buttons |= 1UL << HALO_CHAT_PAD_STICK_RIGHT;
	chat_pad[port] = buttons;
}

extern short network_chat_controller(void);
unsigned long halo_chat_pad_buttons_for_port(short port)
{
	return port >= 0 && port < PORT_COUNT ? chat_physical_pad[port] : 0;
}

unsigned long halo_chat_pad_buttons(void)
{
	short port = network_chat_controller();
	return chat_pad[port >= 0 && port < PORT_COUNT ? port : 0];
}

int halo_chat_taps(float *x, float *y)
{
	return touch_input_chat_taps(x, y);
}

int halo_chat_wheel_steps(void)
{
	int steps;

	pthread_mutex_lock(&mouse_lock);
	steps = chat_wheel_steps;
	chat_wheel_steps = 0;
	pthread_mutex_unlock(&mouse_lock);
	return steps;
}

int halo_chat_touch_layout(void)
{
#if defined(HALO_ANDROID)
	return TRUE;
#elif defined(HALO_WEB)
	int used;

	pthread_mutex_lock(&mouse_lock);
	used = touch_aimed_ms != 0;
	pthread_mutex_unlock(&mouse_lock);
	return used;
#else
	return FALSE;
#endif
}

/* the first controller's buttons held as the on-screen keyboard closed (the
B that closed it, the Start or the A that sent the line), which the game
gets none of till each is let go: it would see a new press of it, a pause
menu opened, a melee, a jump (the triggers are analog buttons too) */
static WORD chat_held_buttons[PORT_COUNT];
static unsigned char chat_held_analog[PORT_COUNT];

static void chat_pad_hold_over(short port, XINPUT_GAMEPAD *pad)
{
	int index;

	if (network_chat_controller() == port)
	{
		chat_held_buttons[port] = pad->wButtons;
		chat_held_analog[port] = 0;
		for (index = 0; index < 8; index++)
		{
			if (pad->bAnalogButtons[index])
				chat_held_analog[port] |= (unsigned char)(1 << index);
		}
		memset(pad, 0, sizeof(*pad));
		return;
	}
	chat_held_buttons[port] &= pad->wButtons;
	pad->wButtons &= (WORD)~chat_held_buttons[port];
	for (index = 0; index < 8; index++)
	{
		if (!(chat_held_analog[port] & (1 << index)))
			continue;
		if (pad->bAnalogButtons[index])
			pad->bAnalogButtons[index] = 0;
		else
			chat_held_analog[port] &= (unsigned char)~(1 << index);
	}
}

/* the first controller's D-pad left and right held as a vote took them,
which the game gets none of till each is let go: else a vote's end would
hand it a press (the Fishsticks layout switches grenades with left) */
static WORD vote_held_buttons;

static void vote_pad_hold_over(XINPUT_GAMEPAD *pad)
{
	const WORD sides = XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT;

	if (network_vote_pad_open())
		vote_held_buttons |= (WORD)(pad->wButtons & sides);
	else
		vote_held_buttons &= pad->wButtons;
	if (network_vote_pad_open())
		pad->wButtons &= (WORD)~sides;
	else
		pad->wButtons &= (WORD)~vote_held_buttons;
}

int halo_gamepad_last_used(void)
{
	Uint64 pad, keyboard;

	pthread_mutex_lock(&mouse_lock);
	pad = pad_used_ms > stick_aimed_ms ? pad_used_ms : stick_aimed_ms;
	keyboard = keyboard_used_ms > mouse_aimed_ms ? keyboard_used_ms : mouse_aimed_ms;
	pthread_mutex_unlock(&mouse_lock);
	return pad != 0 && pad > keyboard;
}

/* the touch controls' stick as the player's movement (input_abstraction.c's
keyboard_controls_update), forward and strafe -1..1, whatever the
profile's sticks do; nonzero while it is pushed. Nothing but on Android and
the web (port/web/site/touch.js). */
int halo_linux_touch_move(short controller_index, float *forward, float *strafe)
{
	*forward = 0.0f;
	*strafe = 0.0f;
	if (controller_index != 0)
		return FALSE;
#if defined(HALO_ANDROID) || defined(HALO_WEB)
	if (!touch_input_move(forward, strafe) || network_chat_controller() == 0)
	{
		*forward = 0.0f;
		*strafe = 0.0f;
		return FALSE;
	}
	touch_used();
	return TRUE;
#else
	return FALSE;
#endif
}

/* the touch controls' swipe and gyroscope since the last call, in radians at
the mouse's rate per pixel (the view applies its own sensitivity), each
apart: player_control.c inverts the swipe as the profile inverts the stick,
not the gyroscope, which turns as the phone does, and makes them the
stick's rate before the magnetism. Nothing but on Android and the web. */
int halo_linux_touch_look(short gamepad_index, float *yaw, float *pitch, float *gyro_yaw, float *gyro_pitch)
{
	*yaw = 0.0f;
	*pitch = 0.0f;
	*gyro_yaw = 0.0f;
	*gyro_pitch = 0.0f;
	if (gamepad_index != 0)
		return FALSE;
#if defined(HALO_ANDROID) || defined(HALO_WEB)
	touch_input_look(0.0022f, yaw, pitch, gyro_yaw, gyro_pitch);
	/* (none while the chat's on-screen keyboard is up: what was swiped is
	dropped, not saved for after) */
	if (network_chat_keyboard_open() && network_chat_controller() == 0)
	{
		*yaw = 0.0f;
		*pitch = 0.0f;
		*gyro_yaw = 0.0f;
		*gyro_pitch = 0.0f;
	}
	if (*yaw == 0.0f && *pitch == 0.0f && *gyro_yaw == 0.0f && *gyro_pitch == 0.0f)
		return FALSE;
	touch_used();
	return TRUE;
#else
	return FALSE;
#endif
}

/* whether port 0's player aims with the touch controls without their aim
assist (input.touch_aim_assist false): no magnetism, as for the mouse
(player_control.c), until a stick moves the aim again */
int halo_linux_touch_aiming(short gamepad_index)
{
#if defined(HALO_ANDROID) || defined(HALO_WEB)
	int aiming;

	if (gamepad_index != 0 || touch_input_aim_assist())
		return FALSE;
	pthread_mutex_lock(&mouse_lock);
	aiming = touch_aimed_ms != 0 && touch_aimed_ms >= stick_aimed_ms;
	pthread_mutex_unlock(&mouse_lock);
	return aiming;
#else
	(void)gamepad_index;
	return FALSE;
#endif
}

/* whether the player on the gamepad aims with the mouse (it moved after the
right stick last did) and input.mouse_aim_assist is off: then the view's
magnetism leaves them be (player_control.c); the bullets' autoaim stays */
int halo_linux_mouse_aiming(short gamepad_index)
{
	static int aim_assist;
	static unsigned long read_at = (unsigned long)-1;
	int aiming;

	if (gamepad_index != 0)
		return FALSE;
	if (read_at != config_changes())
	{
		read_at = config_changes();
		aim_assist = config_boolean("input.mouse_aim_assist");
	}
	/* the host's policy (network.block_mouse_aim_assist, network_distributed.c)
	turns the magnetism off for whoever aims with the mouse, whatever their
	own setting: the mouse aims unless the right stick (or a touch) did last,
	and no stick aiming at all is the mouse's */
	if (network_aim_assist_blocked())
	{
		pthread_mutex_lock(&mouse_lock);
		aiming = network_rules_mouse_is_aim_device(mouse_aimed_ms, stick_aimed_ms, touch_aimed_ms);
		pthread_mutex_unlock(&mouse_lock);
		return aiming;
	}
	if (aim_assist)
		return FALSE;
	pthread_mutex_lock(&mouse_lock);
	aiming = mouse_aimed_ms != 0 && mouse_aimed_ms >= stick_aimed_ms;
	pthread_mutex_unlock(&mouse_lock);
	return aiming;
}

#ifdef HALO_BROWSER
#include <emscripten/emscripten.h>

/* Touch aiming (port/web/assets/touch/touch-controls.js): a finger dragged
across the screen turns the view exactly as the mouse does, in pixels of
relative motion (mouse_poll's accumulation, and it counts as aiming with the
mouse for halo_linux_mouse_aiming). Called on the page's main thread. */
EMSCRIPTEN_KEEPALIVE void platform_web_touch_look(float dx, float dy)
{
	pthread_mutex_lock(&mouse_lock);
	mouse_pending_x += dx;
	mouse_pending_y += dy;
	if (dx != 0.0f || dy != 0.0f)
		mouse_aimed_ms = SDL_GetTicks();
	pthread_mutex_unlock(&mouse_lock);
}

/* Text chat from the touch controls' text box (touch-controls.js), for a
phone with no keyboard: the page puts the line's characters one by one, then
says it to everyone or the team; port/linux/game/chat.c takes it on the
game's thread (platform_chat_take_line) and says it as a typed line. Only
printable ASCII is kept, as the keyboard types. Called on the page's main
thread. */
enum
{
	WEB_CHAT_LINE_SIZE = 81
};

static pthread_mutex_t web_chat_lock = PTHREAD_MUTEX_INITIALIZER;
static char web_chat_typed[WEB_CHAT_LINE_SIZE];
static int web_chat_typed_length;
static char web_chat_line[WEB_CHAT_LINE_SIZE];
static int web_chat_team;
static BOOL web_chat_pending;

EMSCRIPTEN_KEEPALIVE void platform_web_chat_put(int character)
{
	pthread_mutex_lock(&web_chat_lock);
	if (character >= ' ' && character <= '~' && web_chat_typed_length < WEB_CHAT_LINE_SIZE - 1)
		web_chat_typed[web_chat_typed_length++] = (char)character;
	pthread_mutex_unlock(&web_chat_lock);
}

EMSCRIPTEN_KEEPALIVE void platform_web_chat_say(int team)
{
	pthread_mutex_lock(&web_chat_lock);
	memcpy(web_chat_line, web_chat_typed, (size_t)web_chat_typed_length);
	web_chat_line[web_chat_typed_length] = 0;
	web_chat_team = team != 0;
	web_chat_pending = web_chat_typed_length > 0;
	web_chat_typed_length = 0;
	pthread_mutex_unlock(&web_chat_lock);
}

int platform_chat_take_line(int *team, char *text, int size)
{
	int taken;

	if (size <= 0)
		return 0;
	pthread_mutex_lock(&web_chat_lock);
	taken = web_chat_pending;
	if (taken)
	{
		snprintf(text, (size_t)size, "%s", web_chat_line);
		*team = web_chat_team;
		web_chat_pending = FALSE;
	}
	pthread_mutex_unlock(&web_chat_lock);
	return taken;
}
#endif
/* the device the player last used (Controls Setup's page, and the button
prompts: platform_prompt_scheme): 0 the keyboard (or mouse), 1 an Xbox-like
pad, 2 a PlayStation pad, 3 a Nintendo pad */
static int last_input_scheme = 1;
static BOOL last_input_seen = FALSE;
/* (the kind of pad used last, for prompts that never show keys) */
static int last_pad_scheme = 1;

int platform_input_scheme(void)
{
	/* (before any input: a pad if one is connected, else the keyboard; on
	Android the controller's, whose buttons its touch controls are drawn
	as) */
	if (!last_input_seen)
	{
#ifdef HALO_ANDROID
		return 1;
#else
		return SDL_HasGamepad() ? 1 : 0;
#endif
	}
	return last_input_scheme;
}

/* the device every button prompt is drawn for: the menus' (the Chupathingy
menus' and the code-drawn screens', ui_overlay.c; the Xbox and Gearbox
menus' pictures, menu_tags.c) and the HUD's (halo_keyboard_prompts). The
device used last, but with input.keyboard_prompts set to false (in
config.toml alone: the menus no longer have it) never the keyboard: the
controller's buttons always, as that setting's false was */
int platform_prompt_scheme(void)
{
	int scheme = platform_input_scheme();

	if (scheme == 0 && !config_boolean("input.keyboard_prompts"))
		return last_pad_scheme;
	return scheme;
}

static int scheme_of(SDL_Gamepad *gamepad)
{
	switch (SDL_GetGamepadType(gamepad))
	{
	case SDL_GAMEPAD_TYPE_PS3:
	case SDL_GAMEPAD_TYPE_PS4:
	case SDL_GAMEPAD_TYPE_PS5:
		return 2;
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
		return 3;
	default:
		return 1;
	}
}

void platform_input_scheme_used(void *gamepad)
{
	if (gamepad)
	{
		last_pad_scheme = scheme_of((SDL_Gamepad *)gamepad);
		last_input_scheme = last_pad_scheme;
	}
	else
	{
		last_input_scheme = 0;
	}
	last_input_seen = TRUE;
}

/* collects the motion the game has not asked for yet; motion that nobody
consumes for a few polls (menus, cutscenes) is dropped so it cannot jerk
the view later */
static void mouse_poll(const struct platform_input_state *input)
{
	pthread_mutex_lock(&mouse_lock);
	if (++mouse_polls_unconsumed > 4)
	{
		mouse_pending_x = 0.0f;
		mouse_pending_y = 0.0f;
	}
	if (!input->mouse_released)
	{
		int button;

		mouse_pending_x += input->mouse_dx;
		mouse_pending_y += input->mouse_dy;
		if (input->mouse_dx != 0.0f || input->mouse_dy != 0.0f)
			mouse_aimed_ms = SDL_GetTicks();
		/* (the mouse used, not nudged: the prompts show the keyboard's) */
		for (button = 1; button < PLATFORM_MOUSE_BUTTON_COUNT && !input->mouse_buttons[button]; button++)
			;
		if (button < PLATFORM_MOUSE_BUTTON_COUNT || input->mouse_wheel != 0.0f ||
			fabsf(input->mouse_dx) + fabsf(input->mouse_dy) > 8.0f)
		{
			last_input_scheme = 0;
			last_input_seen = TRUE;
		}
		mouse_wheel_accumulated += input->mouse_wheel;
		if (input->mouse_wheel != 0.0f)
			wheel_moved_ms = SDL_GetTicks();
	}
	pthread_mutex_unlock(&mouse_lock);
}

/* ---------- keyboard and mouse as a controller */

static BYTE analog(BOOL down)
{
	return down ? 0xff : 0x00;
}

/* the game's on-screen keyboard is up (platform_text_typing): the keys type
into it (XInputDebugGetKeystroke passes them to the game), but for the
arrows, which move about it, enter (Done, once let go of since it came up)
and escape (cancel) */
static BOOL text_typing;
static BOOL text_typing_enter_armed;
static BOOL text_typing_enter_blocked;
/* (the on-screen keyboard's, and a menu's text field's: menu_functions.c) */
static BOOL text_typing_keyboard, text_typing_field;

static void text_typing_update(void)
{
	BOOL typing = text_typing_keyboard || text_typing_field;

	if (typing && !text_typing)
		text_typing_enter_armed = FALSE;
	if (!typing && text_typing)
		text_typing_enter_blocked = TRUE;
	text_typing = typing;
}

void platform_text_typing(int typing)
{
	text_typing_keyboard = typing != 0;
	text_typing_update();
#ifdef HALO_ANDROID
	/* (on a touchscreen: Android's on-screen keyboard as well, sdl_platform.c) */
	platform_screen_keyboard(text_typing_keyboard, FALSE);
#endif
}

void platform_text_field(int typing, int password)
{
	text_typing_field = typing != 0;
	text_typing_update();
	/* (with no keyboard: Steam's on-screen one, or Android's, sdl_platform.c) */
	platform_screen_keyboard(text_typing_field, typing && password);
}

static void typing_gamepad(const struct platform_input_state *input, XINPUT_GAMEPAD *pad)
{
	const unsigned char *k = input->keys;
	BOOL enter = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER];

	if (k[SDL_SCANCODE_UP]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_UP;
	if (k[SDL_SCANCODE_DOWN]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
	if (k[SDL_SCANCODE_LEFT]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
	if (k[SDL_SCANCODE_RIGHT]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
	if (!enter)
		text_typing_enter_armed = TRUE;
	else if (text_typing_enter_armed)
		pad->wButtons |= XINPUT_GAMEPAD_START;
	pad->bAnalogButtons[XINPUT_GAMEPAD_B] |= analog(k[SDL_SCANCODE_ESCAPE]);
}

static void keyboard_gamepad(const struct platform_input_state *input, XINPUT_GAMEPAD *pad)
{
	const unsigned char *k = input->keys;
	BOOL mouse = !input->mouse_released;
	const unsigned char *m = input->mouse_buttons;
	int x = 0, y = 0;
	BOOL enter = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER];

	/* The Enter that finished typing must be released before it can
	   activate the menu underneath the keyboard. */
	if (!enter)
		text_typing_enter_blocked = FALSE;

	{
		int scancode;

		/* (not a system shortcut: Command held, as a screenshot's; and the
		modifiers alone do not count) */
		for (scancode = SDL_SCANCODE_A; scancode < SDL_SCANCODE_LCTRL && !k[SDL_SCANCODE_LGUI] &&
			!k[SDL_SCANCODE_RGUI]; scancode++)
		{
			if (k[scancode])
			{
				last_input_scheme = 0;
				last_input_seen = TRUE;
				break;
			}
		}
	}
	if (text_typing)
	{
		typing_gamepad(input, pad);
		return;
	}

	if (k[SDL_SCANCODE_D]) x++;
	if (k[SDL_SCANCODE_A]) x--;
	if (k[SDL_SCANCODE_W]) y++;
	if (k[SDL_SCANCODE_S]) y--;
	if (x || y)
	{
		/* full deflection, diagonals on the unit circle */
		float length = (x && y) ? 0.70710678f : 1.0f;

		pad->sThumbLX = (SHORT)(x * 32767 * length);
		pad->sThumbLY = (SHORT)(y * 32767 * length);
	}

	if (k[SDL_SCANCODE_UP]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_UP;
	if (k[SDL_SCANCODE_DOWN]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
	if (k[SDL_SCANCODE_LEFT]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
	if (k[SDL_SCANCODE_RIGHT]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
	if (k[SDL_SCANCODE_F1]) pad->wButtons |= XINPUT_GAMEPAD_BACK;

	/* (escape backs out, as backspace does: the pause menu's B resumes the
	game, the main menu's asks to quit; Start would choose, as A does) */
	keyboard_join_down = k[SDL_SCANCODE_SPACE] || virtual_keyboard_join;
	pad->bAnalogButtons[XINPUT_GAMEPAD_A] |= analog(keyboard_join_down ||
		(enter && !text_typing_enter_blocked) || virtual_keyboard_done);
	pad->bAnalogButtons[XINPUT_GAMEPAD_B] |= analog(k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_BACKSPACE] ||
		(mouse && m[SDL_BUTTON_X1]));
	#if defined(HALO_ANDROID) && !defined(HALO_BROWSER)
	/* the system back key (gesture or button) backs out of menus */
	pad->bAnalogButtons[XINPUT_GAMEPAD_B] |= analog(k[SDL_SCANCODE_AC_BACK]);
#endif
	pad->bAnalogButtons[XINPUT_GAMEPAD_X] |= analog(k[SDL_SCANCODE_DELETE] || k[SDL_SCANCODE_E]);
	pad->bAnalogButtons[XINPUT_GAMEPAD_Y] |= analog(k[SDL_SCANCODE_TAB]);
	/* (Online Games' Link Profile: browser_screen.c) */
	pad->bAnalogButtons[XINPUT_GAMEPAD_BLACK] |= analog(k[SDL_SCANCODE_C]);
	/* (and Q, the left bumper's: the game type editor's pages, with C) */
	pad->bAnalogButtons[XINPUT_GAMEPAD_WHITE] |= analog(k[SDL_SCANCODE_Q]);
}

static BOOL chat_typing;

/* the keys held when the game, the menus or the console switch, or text
chat's line or log opens or closes, count as up until let go of: the escape
that opens the pause menu does not also back out of it, nor the one that
closes it (or the console, or the chat's line) pause the game again, nor the
enter that sends a line press anything */
static void keys_held_over_switch(struct platform_input_state *input)
{
	static unsigned char held[SDL_SCANCODE_COUNT];
	static int context = -1;
	int next_context = (input->menus != FALSE) | (text_typing ? 2 : 0) | (console_is_active() ? 4 : 0) |
		(network_chat_typing() ? 8 : 0) | (chat_typing ? 16 : 0);
	int scancode;
	int now = (input->menus != FALSE) | (chat_typing ? 2 : 0);

	if (context != next_context)
	{
		context = next_context;
		memcpy(held, input->keys, sizeof(held));
	}
	for (scancode = 0; scancode < SDL_SCANCODE_COUNT; scancode++)
	{
		if (!input->keys[scancode])
			held[scancode] = 0;
		else if (held[scancode])
			input->keys[scancode] = 0;
	}
}

/* ---------- the keyboard and mouse's own controls */

#define MAXIMUM_BINDINGS 2

static const char *const binding_settings[NUMBER_OF_HALO_KEYBOARD_ACTIONS] =
{
	"controls.move_forward", "controls.move_backward", "controls.strafe_left", "controls.strafe_right",
	"controls.jump", "controls.crouch", "controls.fire", "controls.throw_grenade", "controls.melee",
	"controls.reload", "controls.zoom", "controls.switch_weapon", "controls.switch_grenade", "controls.action",
	"controls.flashlight", "controls.scoreboard", "controls.pause",
	/* (OpenCE's: the screenshot key, and voice chat's push to talk, which the
	game itself does not read: halo_push_to_talk_held; then text chat's:
	chat.c reads them; then OpenCE's text chat's, network_chat.c's; and the votes',
	network_votekick.c's) */
	"controls.screenshot", "controls.push_to_talk", "controls.chat_all", "controls.chat_team",
	"controls.chat_log", "controls.chat", "controls.team_chat", "controls.vote_yes", "controls.vote_no",
};

static const struct
{
	const char *name;
	int input;
} named_inputs[] =
{
	/* (SDL's names are "," and "Keypad ,", which a list of bindings, split
	at commas, cannot hold) */
	{ "Comma", SDL_SCANCODE_COMMA },
	{ "Keypad Comma", SDL_SCANCODE_KP_COMMA },
	{ "Mouse Left", INPUT_MOUSE + SDL_BUTTON_LEFT },
	{ "Mouse Right", INPUT_MOUSE + SDL_BUTTON_RIGHT },
	{ "Mouse Middle", INPUT_MOUSE + SDL_BUTTON_MIDDLE },
	{ "Mouse 4", INPUT_MOUSE + SDL_BUTTON_X1 },
	{ "Mouse 5", INPUT_MOUSE + SDL_BUTTON_X2 },
	{ "Wheel", INPUT_WHEEL },
	{ "Wheel Up", INPUT_WHEEL_UP },
	{ "Wheel Down", INPUT_WHEEL_DOWN },
};

/* the bindings, read again when config.toml changes; -1 for none */
static int bindings[NUMBER_OF_HALO_KEYBOARD_ACTIONS][MAXIMUM_BINDINGS];
static unsigned long bindings_read_at = (unsigned long)-1;
static unsigned long keyboard_actions_held;

/* (the C library's case: SDL's string functions are not the Android
guest's) */
static int same_name(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		if ((*a >= 'a' && *a <= 'z' ? *a - 32 : *a) != (*b >= 'a' && *b <= 'z' ? *b - 32 : *b))
			return FALSE;
	}
	return *a == *b;
}

int halo_input_from_name(const char *name)
{
	SDL_Scancode scancode;
	size_t index;

	for (index = 0; index < sizeof(named_inputs) / sizeof(named_inputs[0]); index++)
	{
		if (same_name(name, named_inputs[index].name))
			return named_inputs[index].input;
	}
	/* (the other mouse buttons, as halo_input_name names them) */
	if (!strncmp(name, "Mouse ", 6) && name[6] >= '1' && name[6] <= '9' && !name[7] &&
		name[6] - '0' < PLATFORM_MOUSE_BUTTON_COUNT)
	{
		return INPUT_MOUSE + (name[6] - '0');
	}
	scancode = SDL_GetScancodeFromName(name);
	return scancode != SDL_SCANCODE_UNKNOWN ? (int)scancode : -1;
}

void halo_input_name(int input, char *name, size_t size)
{
	size_t index;

	for (index = 0; index < sizeof(named_inputs) / sizeof(named_inputs[0]); index++)
	{
		if (named_inputs[index].input == input)
		{
			snprintf(name, size, "%s", named_inputs[index].name);
			return;
		}
	}
	if (input >= 0 && input < SDL_SCANCODE_COUNT && *SDL_GetScancodeName((SDL_Scancode)input))
		snprintf(name, size, "%s", SDL_GetScancodeName((SDL_Scancode)input));
	else if (input >= INPUT_MOUSE && input < INPUT_WHEEL)
		snprintf(name, size, "Mouse %d", input - INPUT_MOUSE);
	else
		snprintf(name, size, "%s", "");
}

/* a binding's name as the keyboard's layout labels the key: config.toml
names a key by where it sits (its scancode, named as on a US keyboard), so
WASD stays under the fingers on any layout, as Halo PC's DirectInput keys
did, but the menus show what is printed on it: the zoom key "Z" (the key
right of the left Shift) shows as Y on a German keyboard. A key with no
character of its own (Space, Left Shift, the keypad's) keeps its name, as
does one whose character the menus' font has not (beyond Latin-1). */
void halo_input_shown_name(const char *binding, char *shown, size_t size)
{
	int input = halo_input_from_name(binding);
	SDL_Keycode key;

	snprintf(shown, size, "%s", binding);
#ifdef HALO_ANDROID
	/* (the Android guest's SDL has no keyboard layouts to ask: the US
	names, which match a hardware keyboard's usual labels there) */
	(void)input;
	(void)key;
	return;
#else
	if (input < 0 || input >= SDL_SCANCODE_COUNT)
		return;
	/* (as in key events: the French number row shows its numbers, and a
	non-Latin layout's letters the US ones) */
	key = SDL_GetKeyFromScancode((SDL_Scancode)input, SDL_KMOD_NONE, true);
	if (key == ',')
		snprintf(shown, size, "%s", "Comma");
	else if (key > ' ' && key < 0x7f)
		snprintf(shown, size, "%c", key >= 'a' && key <= 'z' ? (char)(key - 'a' + 'A') : (char)key);
	else if (key >= 0xa1 && key <= 0xff && size >= 2)
	{
		/* (Latin-1, a byte the menus take as its character; the small
		letters made capitals but for sharp s and y with diaeresis) */
		if (key >= 0xe0 && key <= 0xfe && key != 0xf7)
			key -= 0x20;
		shown[0] = (char)key;
		shown[1] = 0;
	}
#endif
}

/* the key of an action, as the menus show it (halo_input_shown_name): its
first binding; 0 for an action with none */
int halo_keyboard_action_shown(int action, char *shown, unsigned long size)
{
	char binding[64];
	const char *text;
	size_t length;

	if (action < 0 || action >= NUMBER_OF_HALO_KEYBOARD_ACTIONS || !size)
		return 0;
	text = config_string(binding_settings[action]);
	text += strspn(text, " ");
	length = strcspn(text, ",");
	while (length && text[length - 1] == ' ')
		length--;
	if (!length || length >= sizeof(binding))
		return 0;
	memcpy(binding, text, length);
	binding[length] = 0;
	halo_input_shown_name(binding, shown, size);
	return shown[0] != 0;
}

/* the game's button prompts show the keyboard's keys (Halo PC's) in place
of the controller's buttons while the keyboard or mouse was used last (or,
before any input, on the desktop with no controller connected):
platform_prompt_scheme's. Android's touch controls are drawn as the
controller's buttons, and touches leave the prompts the controller's; a
keyboard or mouse used there shows its keys too. */
int halo_keyboard_prompts(void)
{
	return platform_prompt_scheme() == 0;
}

static void bindings_read(void)
{
	int action;

	if (bindings_read_at == config_changes())
		return;
	bindings_read_at = config_changes();
	for (action = 0; action < NUMBER_OF_HALO_KEYBOARD_ACTIONS; action++)
	{
		const char *text = config_string(binding_settings[action]);
		int slot;

		for (slot = 0; slot < MAXIMUM_BINDINGS; slot++)
		{
			char name[64];
			size_t length;

			bindings[action][slot] = -1;
			while (*text == ' ' || *text == ',')
				text++;
			length = strcspn(text, ",");
			if (!length)
				continue;
			snprintf(name, sizeof(name), "%.*s", (int)length, text);
			while (*name && name[strlen(name) - 1] == ' ')
				name[strlen(name) - 1] = 0;
			bindings[action][slot] = halo_input_from_name(name);
			if (bindings[action][slot] < 0)
				platform_log("controls: %s has no key or button named \"%s\"", binding_settings[action], name);
			text += length;
		}
	}
}

static BOOL input_held(const struct platform_input_state *input, int code)
{
	BOOL wheel = SDL_GetTicks() < wheel_press_until_ms;

	if (code < 0)
		return FALSE;
	if (code < SDL_SCANCODE_COUNT)
		return input->keys[code] != 0;
	if (code < INPUT_WHEEL)
		return !input->mouse_released && input->mouse_buttons[code - INPUT_MOUSE];
	if (code == INPUT_WHEEL)
		return wheel;
	return wheel && wheel_direction == (code == INPUT_WHEEL_UP ? 1 : -1);
}

/* the actions whose bindings are held (the game's, and the screenshot key's
in the menus too) */
static unsigned long keyboard_bound_actions(const struct platform_input_state *input)
{
	unsigned long held = 0;
	int action, slot;

	bindings_read();
	for (action = 0; action < NUMBER_OF_HALO_KEYBOARD_ACTIONS; action++)
	{
		for (slot = 0; slot < MAXIMUM_BINDINGS; slot++)
		{
			if (input_held(input, bindings[action][slot]))
				held |= 1UL << action;
		}
	}
	return held;
}

/* one capture per press of the screenshot key, however long it is held
(written at the next presentation: d3d8_gl.c) */
static void keyboard_screenshot(unsigned long held)
{
	static BOOL was_down;
	BOOL down = (held & (1UL << HALO_KEYBOARD_SCREENSHOT)) != 0;

	if (down && !was_down)
		platform_screenshot_request();
	was_down = down;
}

/* in the game: the actions held, and the controller's Start and Back for
the pause menu and the scoreboard */
static void keyboard_controls(unsigned long held, XINPUT_GAMEPAD *pad)
{
	if (held & (1UL << HALO_KEYBOARD_PAUSE))
		pad->wButtons |= XINPUT_GAMEPAD_START;
	if (held & (1UL << HALO_KEYBOARD_SCOREBOARD))
		pad->wButtons |= XINPUT_GAMEPAD_BACK;
	keyboard_actions_held = held;
}

/* text chat's typing (platform_chat_typing: chat_typing, above), and escape
pressed while it types (platform_chat_escape) */
static BOOL chat_escape_held, chat_escape_pressed;

void platform_chat_typing(int typing)
{
	chat_typing = typing != 0;
	chat_escape_pressed = FALSE;
}

int platform_chat_escape(void)
{
	BOOL pressed = chat_escape_pressed;

	chat_escape_pressed = FALSE;
	return pressed;
}

/* (escape, seen as the key itself rather than as a keystroke) */
static void chat_escape_update(const struct platform_input_state *input)
{
	BOOL held = input->keys[SDL_SCANCODE_ESCAPE] != 0;

	if (chat_typing && held && !chat_escape_held)
		chat_escape_pressed = TRUE;
	chat_escape_held = held;
}

unsigned long halo_keyboard_actions(short controller_index)
{
	return controller_index == 0 ? keyboard_actions_held : 0;
}

int halo_push_to_talk_held(void)
{
	struct platform_input_state input;
	int slot;

	/* (the window losing the focus lets every key go: sdl_platform.c) */
	bindings_read();
	platform_input_read(&input, FALSE);
	for (slot = 0; slot < MAXIMUM_BINDINGS; slot++)
	{
		if (input_held(&input, bindings[HALO_KEYBOARD_PUSH_TO_TALK][slot]))
			return 1;
	}
	return controller_remap_talk();
}

/* A scroll of the wheel switches weapons once: it holds Y for WHEEL_PRESS_MS
once the wheel has turned a notch, and the scroll lasts until the wheel has
been still for WHEEL_SCROLL_GAP_MS. One notch often arrives as several events
over a few tens of milliseconds (high-resolution and smooth-scrolling
wheels), and one flick turns several notches; switching for each would bring
the same weapon straight back. Timed in milliseconds, not polls: polls come
once a frame, at the display's refresh rate. */
#define WHEEL_PRESS_MS 50
#define WHEEL_SCROLL_GAP_MS 200
/* how far the wheel turns before it switches: a notch. A browser's notch is
pixels, which SDL counts as a notch per 100, and some send fewer for one
(about half), so in the web build half of SDL's notch is one */
#ifdef HALO_WEB
#define WHEEL_NOTCH 0.5f
#else
#define WHEEL_NOTCH 1.0f
#endif

/* debug.test_input "bot:<seed>": a scripted player for the automated
network tests (port/linux/game/network_test.c), different for each seed:
it walks and strafes in circles, turns, fires every few seconds, jumps now
and then and throws a grenade every seven seconds; "look:<seed>" stands
still, only turning and looking up and down (where remote players aim and
whether they stand) */
static int test_input_holding_action;
static double test_input_holding_action_since;

/* the scripted player's clock: the game's time while one runs (the main
menu's too), so that it plays alike at any speed and tick for tick
(debug.fixed_step), else real time */
static double test_input_seconds(void)
{
	double seconds = network_test_game_seconds();

	return seconds >= 0.0 ? seconds : (double)SDL_GetTicks() / 1000.0;
}

/* the automated tests (port/linux/game/network_test.c): the scripted player
stands still, holding the action button (X: picking up, swapping weapons)
after a second */
void test_input_hold_action(int hold)
{
	if (hold && !test_input_holding_action)
		test_input_holding_action_since = test_input_seconds();
	test_input_holding_action = hold;
}

/* ... and the melee button (B) held, the player standing still */
static int test_input_holding_melee;

void test_input_hold_melee(int hold)
{
	test_input_holding_melee = hold;
}

/* ... and the trigger (right), the player standing still: a flag or ball
in hand is thrown down */
static int test_input_holding_fire;

void test_input_hold_fire(int hold)
{
	test_input_holding_fire = hold;
}

/* debug.test_input "menu:<buttons>": the buttons pressed one a second, from
the first poll, for testing the menus: a, b, x, y, lb, rb (white and black),
up, down, left, right, start, back, key:<a key's name> (a key of the
keyboard, as SDL names it: key:C; key:W+C holds both), the mouse's (move,
click, rclick, wheel, below), shot, or wait (none), separated by spaces or
commas */
static char test_input_menu[512];
static Uint64 test_input_menu_since;

/* the button or key pressed now (NULL for none) */
static const char *test_input_menu_token(size_t *length)
{
	Uint64 elapsed = SDL_GetTicks() - test_input_menu_since;
	Uint64 step = elapsed / 1000;
	const char *token = test_input_menu;

	/* (pressed for the first 150 ms of its second) */
	if (!test_input_menu[0] || elapsed % 1000 >= 150)
		return NULL;
	for (;;)
	{
		token += strspn(token, " ,");
		*length = strcspn(token, " ,");
		if (!*length)
			return NULL;
		if (!step)
			return token;
		step--;
		token += *length;
	}
}

#ifndef HALO_ANDROID
BOOL halo_ui_point_to_window(short x, short y, float *window_x, float *window_y);
void halo_test_screenshot(void);

/* the mouse's tokens of debug.test_input "menu:": move:<x>:<y>,
click:<x>:<y>, rclick:<x>:<y> and wheel:<notches> (positive away from the
user), at a point of the menus' 640x480 (the middle 640 of a wide screen);
a click moves there first and clicks 300 ms later. And shot saves the
window to debug.screenshot_directory. Each acts once in its second. */
static void test_input_menu_pointer(void)
{
	static Uint64 step_done = (Uint64)-1;
	static Uint64 click_step = (Uint64)-1;
	Uint64 elapsed;
	Uint64 step;
	const char *token = test_input_menu;
	size_t length;
	char text[64];
	int x = 0, y = 0, wheel = 0;
	float window_x, window_y;

	if (!test_input_menu[0])
		return;
	elapsed = SDL_GetTicks() - test_input_menu_since;
	step = elapsed / 1000;
	for (;;)
	{
		token += strspn(token, " ,");
		length = strcspn(token, " ,");
		if (!length)
			return;
		if (!step)
			break;
		step--;
		token += length;
	}
	step = elapsed / 1000;
	if (length >= sizeof(text))
		return;
	memcpy(text, token, length);
	text[length] = 0;
	if (!strcmp(text, "shot"))
	{
		if (step_done != step)
			halo_test_screenshot();
		step_done = step;
		return;
	}
	if (sscanf(text, "wheel:%d", &wheel) == 1)
	{
		if (step_done != step)
			platform_test_pointer_event(0.0f, 0.0f, 0, wheel);
		step_done = step;
		return;
	}
	if (sscanf(text, "move:%d:%d", &x, &y) != 2 && sscanf(text, "click:%d:%d", &x, &y) != 2 &&
		sscanf(text, "rclick:%d:%d", &x, &y) != 2)
	{
		return;
	}
	if (!halo_ui_point_to_window((short)x, (short)y, &window_x, &window_y))
		return;
	if (step_done != step)
	{
		step_done = step;
		platform_test_pointer_event(window_x, window_y, 0, 0);
		platform_log("test input: %s at window %.1f,%.1f", text, window_x, window_y);
	}
	if (text[0] != 'm' && click_step != step && elapsed % 1000 >= 300)
	{
		click_step = step;
		platform_test_pointer_event(window_x, window_y, text[0] == 'r' ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT, 0);
	}
}
#endif

/* (a key goes in with the keyboard's, as if typed, before the keys drive
the controller; key:W+C holds both) */
static void test_input_menu_keys(struct platform_input_state *input)
{
	size_t length;
	const char *token;
	char names[64];
	char *name, *next;

#ifndef HALO_ANDROID
	test_input_menu_pointer();
#endif
	token = test_input_menu_token(&length);
	if (!token || length <= 4 || length - 4 >= sizeof(names) || strncmp(token, "key:", 4))
		return;
	memcpy(names, token + 4, length - 4);
	names[length - 4] = 0;
	for (name = names; name; name = next)
	{
		SDL_Scancode scancode;

		next = strchr(name, '+');
		if (next)
			*next++ = 0;
		scancode = SDL_GetScancodeFromName(name);
		if (scancode != SDL_SCANCODE_UNKNOWN)
			input->keys[scancode] = 1;
	}
}

static void test_input_menu_gamepad(XINPUT_GAMEPAD *pad)
{
	static const struct
	{
		const char *name;
		int analog;
		WORD digital;
	} buttons[] =
	{
		{ "a", XINPUT_GAMEPAD_A, 0 },
		{ "b", XINPUT_GAMEPAD_B, 0 },
		{ "x", XINPUT_GAMEPAD_X, 0 },
		{ "y", XINPUT_GAMEPAD_Y, 0 },
		{ "lb", XINPUT_GAMEPAD_WHITE, 0 },
		{ "rb", XINPUT_GAMEPAD_BLACK, 0 },
		{ "up", -1, XINPUT_GAMEPAD_DPAD_UP },
		{ "down", -1, XINPUT_GAMEPAD_DPAD_DOWN },
		{ "left", -1, XINPUT_GAMEPAD_DPAD_LEFT },
		{ "right", -1, XINPUT_GAMEPAD_DPAD_RIGHT },
		{ "start", -1, XINPUT_GAMEPAD_START },
		{ "back", -1, XINPUT_GAMEPAD_BACK },
	};
	size_t length;
	const char *token = test_input_menu_token(&length);
	unsigned int index;

	if (!token)
		return;
	for (index = 0; index < sizeof(buttons) / sizeof(buttons[0]); index++)
	{
		if (strlen(buttons[index].name) == length && !strncmp(token, buttons[index].name, length))
		{
			if (buttons[index].analog >= 0)
				pad->bAnalogButtons[buttons[index].analog] = 255;
			else
				pad->wButtons |= buttons[index].digital;
			return;
		}
	}
}

/* the automated tests (port/linux/game/network_test.c): the controller's
buttons (halo_chat_pad_button bits) held for the reads to come, as a player
types on the text chat's on-screen keyboard; the scripted player does
nothing else while that keyboard is up */
static unsigned long test_input_pressed;

void test_input_press(unsigned long buttons)
{
	test_input_pressed = buttons;
}

#if !defined(HALO_ANDROID) && !defined(HALO_CONSOLE_NO_TEST_GAMEPADS)
/* debug.test_gamepads: that many virtual gamepads (SDL's), attached at the
first poll, for tests of more than one controller (split screen). The menu
script's pad<n>:<button> (pad2:a) presses a button of virtual gamepad n (from
1) through SDL, as a real one would, for its 150 ms: a, b, x, y, lb, rb, up,
down, left, right, start, back, a stick pushed all the way (ls_up, rs_left
and so on) or a trigger pulled (lt, rt); pad<n>:unplug and pad<n>:plug take
it out and put it back (as a new controller, as SDL sees a real one) */
static int test_virtual_pad_count;
static SDL_JoystickID test_virtual_pads[PORT_COUNT];

static void test_virtual_pad_plug(int index)
{
	static char names[PORT_COUNT][32];
	SDL_VirtualJoystickDesc desc;

	SDL_INIT_INTERFACE(&desc);
	snprintf(names[index], sizeof(names[index]), "Test Gamepad %d", index + 1);
	desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
	desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
	desc.name = names[index];
	test_virtual_pads[index] = SDL_AttachVirtualJoystick(&desc);
	if (!test_virtual_pads[index])
	{
		platform_log("test gamepads: %s", SDL_GetError());
		return;
	}
	/* (opened now, not at its event: this poll already sees it) */
	SDL_OpenGamepad(test_virtual_pads[index]);
	platform_log("test gamepads: %d plugged in", index + 1);
}

static void test_virtual_pads_attach(void)
{
	static int attached;
	int count, index;

	if (attached)
		return;
	attached = 1;
	count = config_integer("debug.test_gamepads");
	if (count > PORT_COUNT)
		count = PORT_COUNT;
	/* (a test's window need not have the focus) */
	if (count > 0)
		SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	for (index = 0; index < count; index++)
		test_virtual_pad_plug(index);
	test_virtual_pad_count = count > 0 ? count : 0;
}

/* the menu script's pad<n>:<button> now pressed on its virtual pad, every
other button of the virtual pads let go */
static void test_virtual_pads_update(void)
{
	static const struct
	{
		const char *name;
		SDL_GamepadButton button;
	} buttons[] =
	{
		{ "a", SDL_GAMEPAD_BUTTON_SOUTH },
		{ "b", SDL_GAMEPAD_BUTTON_EAST },
		{ "x", SDL_GAMEPAD_BUTTON_WEST },
		{ "y", SDL_GAMEPAD_BUTTON_NORTH },
		{ "lb", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },
		{ "rb", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
		{ "up", SDL_GAMEPAD_BUTTON_DPAD_UP },
		{ "down", SDL_GAMEPAD_BUTTON_DPAD_DOWN },
		{ "left", SDL_GAMEPAD_BUTTON_DPAD_LEFT },
		{ "right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
		{ "start", SDL_GAMEPAD_BUTTON_START },
		{ "back", SDL_GAMEPAD_BUTTON_BACK },
	};
	static const struct
	{
		const char *name;
		SDL_GamepadAxis axis;
		Sint16 value;
	} sticks[] =
	{
		{ "ls_up", SDL_GAMEPAD_AXIS_LEFTY, -32767 },
		{ "ls_down", SDL_GAMEPAD_AXIS_LEFTY, 32767 },
		{ "ls_left", SDL_GAMEPAD_AXIS_LEFTX, -32767 },
		{ "ls_right", SDL_GAMEPAD_AXIS_LEFTX, 32767 },
		{ "rs_up", SDL_GAMEPAD_AXIS_RIGHTY, -32767 },
		{ "rs_down", SDL_GAMEPAD_AXIS_RIGHTY, 32767 },
		{ "rs_left", SDL_GAMEPAD_AXIS_RIGHTX, -32767 },
		{ "rs_right", SDL_GAMEPAD_AXIS_RIGHTX, 32767 },
		{ "rt", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767 },
		{ "lt", SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767 },
	};
	size_t length = 0;
	const char *token;
	const char *action = "";
	size_t action_length = 0;
	int pad = 0, index;
	SDL_GamepadButton pressed = SDL_GAMEPAD_BUTTON_INVALID;
	int stick = -1;

	test_virtual_pads_attach();
	if (!test_virtual_pad_count)
		return;
	token = test_input_menu_token(&length);
	if (token && length > 5 && !strncmp(token, "pad", 3) && token[3] >= '1' &&
		token[3] < '1' + test_virtual_pad_count && token[4] == ':')
	{
		pad = token[3] - '0';
		action = token + 5;
		action_length = length - 5;
		for (index = 0; index < (int)(sizeof(buttons) / sizeof(buttons[0])); index++)
		{
			if (strlen(buttons[index].name) == action_length && !strncmp(action, buttons[index].name, action_length))
				pressed = buttons[index].button;
		}
		for (index = 0; index < (int)(sizeof(sticks) / sizeof(sticks[0])); index++)
		{
			if (strlen(sticks[index].name) == action_length && !strncmp(action, sticks[index].name, action_length))
				stick = index;
		}
		if (action_length == 6 && !strncmp(action, "unplug", 6) && test_virtual_pads[pad - 1])
		{
			SDL_DetachVirtualJoystick(test_virtual_pads[pad - 1]);
			test_virtual_pads[pad - 1] = 0;
			platform_log("test gamepads: %d unplugged", pad);
		}
		else if (action_length == 4 && !strncmp(action, "plug", 4) && !test_virtual_pads[pad - 1])
			test_virtual_pad_plug(pad - 1);
	}
	for (index = 0; index < test_virtual_pad_count; index++)
	{
		SDL_Joystick *joystick = test_virtual_pads[index] ? SDL_GetJoystickFromID(test_virtual_pads[index]) : NULL;
		int button;

		if (!joystick)
			continue;
		for (button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; button++)
		{
			SDL_SetJoystickVirtualButton(joystick, button,
				index + 1 == pad && button == (int)pressed);
		}
		for (button = 0; button < SDL_GAMEPAD_AXIS_COUNT; button++)
		{
			SDL_SetJoystickVirtualAxis(joystick, button,
				index + 1 == pad && stick >= 0 && (int)sticks[stick].axis == button ? sticks[stick].value :
				(button == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || button == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? -32768 : 0));
		}
	}
	/* (in force for this poll's read) */
	SDL_UpdateJoysticks();
}
#endif

static void test_input_gamepad(XINPUT_GAMEPAD *pad)
{
	static const WORD digital[] =
	{
		XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
	};
	static const int analog[] = { XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y };
	static int checked;
	static int seed = -1;
	static int looking;
	double t;
	int index;

	for (index = 0; index < 4; index++)
	{
		if (test_input_pressed & (1UL << (HALO_CHAT_PAD_UP + index)))
			pad->wButtons |= digital[index];
		if (test_input_pressed & (1UL << (HALO_CHAT_PAD_A + index)))
			pad->bAnalogButtons[analog[index]] = 255;
	}
	if (test_input_pressed & (1UL << HALO_CHAT_PAD_START))
		pad->wButtons |= XINPUT_GAMEPAD_START;
	if (test_input_pressed & (1UL << HALO_CHAT_PAD_STICK_UP))
		pad->sThumbLY = 32000;
	if (test_input_pressed & (1UL << HALO_CHAT_PAD_STICK_DOWN))
		pad->sThumbLY = -32000;
	if (test_input_pressed & (1UL << HALO_CHAT_PAD_STICK_LEFT))
		pad->sThumbLX = -32000;
	if (test_input_pressed & (1UL << HALO_CHAT_PAD_STICK_RIGHT))
		pad->sThumbLX = 32000;
	if (network_chat_keyboard_open() && network_chat_controller() == 0)
		return;

	if (!checked)
	{
		const char *setting = config_string("debug.test_input");

		checked = 1;
		if (!strncmp(setting, "menu:", 5))
		{
			snprintf(test_input_menu, sizeof(test_input_menu), "%s", setting + 5);
			test_input_menu_since = SDL_GetTicks();
		}
		else if (!strncmp(setting, "bot:", 4))
			seed = atoi(setting + 4);
		else if (!strcmp(setting, "bot"))
			seed = 0;
		else if (!strncmp(setting, "look:", 5))
		{
			seed = atoi(setting + 5);
			looking = 1;
		}
	}
	if (test_input_menu[0])
	{
		test_input_menu_gamepad(pad);
		return;
	}
	if (test_input_holding_melee)
	{
		pad->bAnalogButtons[XINPUT_GAMEPAD_B] = 255;
		return;
	}
	if (test_input_holding_fire)
	{
		pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
		return;
	}
	if (seed < 0)
		return;
	if (test_input_holding_action)
	{
		/* (standing still, the button held from a second on) */
		if (test_input_seconds() - test_input_holding_action_since >= 1.0)
			pad->bAnalogButtons[XINPUT_GAMEPAD_X] = 255;
		return;
	}
	t = test_input_seconds() + seed * 1.7;
	if (looking)
	{
		pad->sThumbRX = (SHORT)(sin(t * 0.5) * 14000.0);
		pad->sThumbRY = (SHORT)(sin(t * 0.3) * 32000.0);
		return;
	}
	pad->sThumbLY = (SHORT)(sin(t * 0.9) * 32000.0);
	pad->sThumbLX = (SHORT)(cos(t * 0.6 + seed) * 20000.0);
	pad->sThumbRX = (SHORT)(sin(t * 0.4) * 14000.0);
	if (fmod(t, 3.0) < 0.3)
		pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
	if (fmod(t, 5.0) < 0.1)
		pad->bAnalogButtons[XINPUT_GAMEPAD_A] = 255;
	if (fmod(t, 7.0) < 0.2)
		pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = 255;
}

static void wheel_update(void)
{
	Uint64 now = SDL_GetTicks();

	pthread_mutex_lock(&mouse_lock);
	/* (a chat line typed: the wheel scrolls its lines, network_chat.c) */
	if (network_chat_typing())
	{
		while (mouse_wheel_accumulated >= WHEEL_NOTCH)
		{
			chat_wheel_steps++;
			mouse_wheel_accumulated -= WHEEL_NOTCH;
		}
		while (mouse_wheel_accumulated <= -WHEEL_NOTCH)
		{
			chat_wheel_steps--;
			mouse_wheel_accumulated += WHEEL_NOTCH;
		}
		wheel_scrolling = FALSE;
		pthread_mutex_unlock(&mouse_lock);
		return;
	}
	if (!wheel_scrolling)
	{
		if (fabsf(mouse_wheel_accumulated) >= WHEEL_NOTCH)
		{
			wheel_scrolling = TRUE;
			wheel_direction = mouse_wheel_accumulated > 0.0f ? 1 : -1;
			wheel_press_until_ms = now + WHEEL_PRESS_MS;
		}
	}
	else if (now >= wheel_press_until_ms && now - wheel_moved_ms >= WHEEL_SCROLL_GAP_MS)
	{
		wheel_scrolling = FALSE;
		mouse_wheel_accumulated = 0.0f;
	}
	pthread_mutex_unlock(&mouse_lock);
}

/* ---------- gamepad ports (tools/test_split_screen_input.py compiles this
part on its own, from here to the next part) */

/* the gamepads' ports (their SDL ids, 0 for none): while two or more people
play here (pc_menu_split_players), a controller keeps its port when another
is plugged in or out, so that no player takes over another's view, and the
port of one unplugged is kept for the next controller plugged in; else the
ports follow the order the controllers were connected in */
static SDL_JoystickID port_gamepad_ids[PORT_COUNT];
/* the ports kept for controllers unplugged while people play */
static BOOL port_gamepad_kept[PORT_COUNT];

/* each listed gamepad's port (port_of; -1 for none), the gamepads listed in
connection order; returns the number of ports in use: one past the last with
a gamepad, or kept for one */
static int gamepad_ports_assign(const SDL_JoystickID *listed, int count, BOOL keep, int *port_of)
{
	BOOL taken[PORT_COUNT] = { FALSE };
	int index, port, ports = 0;

	for (index = 0; index < count; index++)
		port_of[index] = -1;
	if (!keep)
	{
		/* (one person: the ports in connection order, none kept) */
		for (port = 0; port < PORT_COUNT; port++)
		{
			port_gamepad_ids[port] = port < count ? listed[port] : 0;
			port_gamepad_kept[port] = FALSE;
			if (port < count)
				port_of[port] = port;
		}
		return count < PORT_COUNT ? count : PORT_COUNT;
	}
	/* the controllers still connected keep their ports; an unplugged one's
	port is kept for the next */
	for (port = 0; port < PORT_COUNT; port++)
	{
		if (!port_gamepad_ids[port])
			continue;
		for (index = 0; index < count && !taken[port]; index++)
		{
			if (port_of[index] < 0 && listed[index] == port_gamepad_ids[port])
			{
				port_of[index] = port;
				taken[port] = TRUE;
			}
		}
		if (!taken[port])
		{
			port_gamepad_ids[port] = 0;
			port_gamepad_kept[port] = TRUE;
		}
	}
	/* a controller plugged in takes a kept port first, then a free one */
	for (index = 0; index < count; index++)
	{
		int free_port = -1;

		if (port_of[index] >= 0)
			continue;
		for (port = 0; port < PORT_COUNT && free_port < 0; port++)
		{
			if (!taken[port] && port_gamepad_kept[port])
				free_port = port;
		}
		for (port = 0; port < PORT_COUNT && free_port < 0; port++)
		{
			if (!taken[port])
				free_port = port;
		}
		if (free_port < 0)
			break;
		port_gamepad_ids[free_port] = listed[index];
		port_gamepad_kept[free_port] = FALSE;
		port_of[index] = free_port;
		taken[free_port] = TRUE;
	}
	for (port = 0; port < PORT_COUNT; port++)
	{
		if (taken[port] || port_gamepad_kept[port])
			ports = port + 1;
	}
	return ports;
}

/* ---------- SDL gamepads */

/* the SDL gamepads by port (NULL for a port with none: gamepad_ports_assign);
returns the number of ports in use */
static int sdl_gamepads(SDL_Gamepad *gamepads[PORT_COUNT])
{
	SDL_JoystickID *ids;
	SDL_JoystickID listed_ids[PORT_COUNT];
	SDL_Gamepad *listed[PORT_COUNT];
	int port_of[PORT_COUNT];
	int count = 0, index, found = 0, ports;

	memset(gamepads, 0, sizeof(SDL_Gamepad *) * PORT_COUNT);
	ids = SDL_GetGamepads(&count);
#if defined(HALO_ANDROID) && !defined(HALO_BROWSER)
	{
		/* Android can list input devices with a few gamepad buttons (the
		emulator's keyboard, some phones' key devices) as generic gamepads:
		recognised controllers take the first ports */
		int pass;

		for (pass = 0; pass < 2 && ids; pass++)
		{
			for (index = 0; index < count && found < PORT_COUNT; index++)
			{
				SDL_Gamepad *gamepad = SDL_GetGamepadFromID(ids[index]);
				SDL_GamepadType type;
				BOOL recognised;

				if (!gamepad)
					continue;
				type = SDL_GetGamepadType(gamepad);
				recognised = type != SDL_GAMEPAD_TYPE_UNKNOWN && type != SDL_GAMEPAD_TYPE_STANDARD;
				if (recognised == (pass == 0))
				{
					listed_ids[found] = ids[index];
					listed[found++] = gamepad;
				}
			}
		}
	}
#else
	for (index = 0; ids && index < count && found < PORT_COUNT; index++)
	{
		SDL_Gamepad *gamepad = SDL_GetGamepadFromID(ids[index]);

		if (gamepad)
		{
			listed_ids[found] = ids[index];
			listed[found++] = gamepad;
		}
	}
#endif
	SDL_free(ids);
	ports = gamepad_ports_assign(listed_ids, found, pc_menu_split_players() != 0, port_of);
	for (index = 0; index < found; index++)
	{
		if (port_of[index] >= 0)
			gamepads[port_of[index]] = listed[index];
	}
	return ports;
}

/* the gamepad last used (its SDL id; 0 none yet), which the Controls screen
names first (Android has no SDL_GetGamepadID: the first) */
#ifndef HALO_ANDROID
static SDL_JoystickID last_used_gamepad;
#endif

int platform_gamepad_count(void)
{
	SDL_Gamepad *gamepads[PORT_COUNT];
	int ports = sdl_gamepads(gamepads), port, count = 0;

	for (port = 0; port < ports; port++)
	{
		if (gamepads[port])
			count++;
	}
	return count;
}

/* a gamepad's name (as it names itself, else its kind's) and its kind
(controller_layout.h's CONTROLLER_KIND_*): index -1 is the one last used,
else the first; -1 if there is none */
int platform_gamepad_describe(int index, char *name, size_t size)
{
	SDL_Gamepad *gamepads[PORT_COUNT];
	SDL_Gamepad *gamepad = NULL;
	int ports = sdl_gamepads(gamepads), count = 0, type, kind, each;
	const char *own;

	if (size)
		name[0] = 0;
	/* (the connected ones in port order, none between) */
	for (each = 0; each < ports; each++)
	{
		if (gamepads[each])
			gamepads[count++] = gamepads[each];
	}
	if (index < 0)
	{
#ifndef HALO_ANDROID
		for (each = 0; each < count && !gamepad; each++)
		{
			if (last_used_gamepad && SDL_GetGamepadID(gamepads[each]) == last_used_gamepad)
				gamepad = gamepads[each];
		}
#else
		(void)each;
#endif
		index = 0;
	}
	if (!gamepad && index < count)
		gamepad = gamepads[index];
	if (!gamepad)
		return -1;
	switch (SDL_GetGamepadType(gamepad))
	{
	case SDL_GAMEPAD_TYPE_XBOX360: type = 1; break;
	case SDL_GAMEPAD_TYPE_XBOXONE: type = 2; break;
	case SDL_GAMEPAD_TYPE_PS3: type = 3; break;
	case SDL_GAMEPAD_TYPE_PS4: type = 4; break;
	case SDL_GAMEPAD_TYPE_PS5: type = 5; break;
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: type = 6; break;
	default: type = 0; break;
	}
#ifdef HALO_ANDROID
	/* (the guest reaches SDL only through host_imports.list, which has no
	SDL_GetGamepadName or SDL_GetGamepadID: the kind's name, the first pad) */
	own = NULL;
#else
	own = SDL_GetGamepadName(gamepad);
#endif
	kind = controller_layout_kind(type, own);
	if (size)
		snprintf(name, size, "%s", own && *own ? own : controller_layout_kind_title(kind));
	return kind;
}

/* whether one gamepad is port 1's (port_gamepad) */
static BOOL lone_gamepad_split;
/* pc_menu_split_players, as the game's thread last read it (XInputGetState):
port_gamepad also runs on the input thread's rumble (XInputSetState), which
must not read the game's state while the game's thread has it */
static BOOL split_players;

/* no button of the gamepad held, its sticks and triggers at rest */
static BOOL gamepad_idle(SDL_Gamepad *gamepad)
{
	int index;

	for (index = 0; index < SDL_GAMEPAD_BUTTON_COUNT; index++)
	{
		if (SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)index))
			return FALSE;
	}
	for (index = 0; index < SDL_GAMEPAD_AXIS_COUNT; index++)
	{
		if (abs(SDL_GetGamepadAxis(gamepad, (SDL_GamepadAxis)index)) > 8000)
			return FALSE;
	}
	return TRUE;
}

/* the gamepad of a port: the first shares port 0 with the keyboard, but for
two or more players with one gamepad (co-op, split screen), port 1 has it
(the keyboard's player is 1, the gamepad's 2). It changes port only at rest:
a button held across the change would be pressed again on the other port
(the B that leaves a profile screen leaving the game as player 1's) */
static SDL_Gamepad *port_gamepad(SDL_Gamepad *gamepads[PORT_COUNT], int count, int port)
{
	if (count == 1 && gamepads[0])
	{
		BOOL split = split_players;

		if (split != lone_gamepad_split && gamepad_idle(gamepads[0]))
			lone_gamepad_split = split;
		if (lone_gamepad_split)
			return port == 1 ? gamepads[0] : NULL;
	}
	/* (back to one player: the keyboard shares port 0 again) */
	if (!split_players)
		keyboard_alone_wanted = FALSE;
	if (keyboard_alone != keyboard_alone_wanted && count > 1 && gamepad_idle(gamepads[0]))
		keyboard_alone = keyboard_alone_wanted;
	if (keyboard_alone && count > 1 && count < PORT_COUNT)
		return port == 0 ? NULL : port == count ? gamepads[0] : port < count ? gamepads[port] : NULL;
	return port < count ? gamepads[port] : NULL;
}

unsigned char halo_keyboard_join_held(void)
{
	return keyboard_join_down || virtual_keyboard_join;
}



void halo_keyboard_alone_set(unsigned char alone)
{
	keyboard_alone_wanted = alone != 0;
}

/* whether port 0 (player 1) is the keyboard's and a gamepad's */
unsigned char halo_keyboard_shares_gamepad(void)
{
	SDL_Gamepad *gamepads[PORT_COUNT];
	int count = sdl_gamepads(gamepads);

	return port_gamepad(gamepads, count, 0) != NULL;
}

int halo_gamepad_count(void)
{
	SDL_Gamepad *gamepads[PORT_COUNT];

	return sdl_gamepads(gamepads);
}

static SHORT stick(Sint16 value, BOOL flip)
{
	int result = flip ? -(int)value - 1 : value;

	if (result < -32768) result = -32768;
	if (result > 32767) result = 32767;
	return (SHORT)result;
}

static void merge_button(XINPUT_GAMEPAD *pad, int analog_index, BOOL down)
{
	if (down)
		pad->bAnalogButtons[analog_index] = 0xff;
}

#ifdef HALO_SWITCH_CONSOLE
/* ---------- gyro aiming (the Switch)

The controller's gyroscope turns the view as well as the stick, each local
player from their own pad: the host reads the sensor (port/switch/host/
host_sdl2.c, host_sdl_gamepad_gyro) and gives the turn since the last call
in radians, which player_control.c adds to the view as it adds the mouse's.
Whether it aims is input.gyro_aim (true always, zoom only while zoomed in:
halo_linux_gyro_zoom_only), and its horizontal and vertical sensitivity and
invert are its own, apart from the stick's. */
void host_sdl_gamepad_gyro(unsigned int gamepad, float *yaw, float *pitch);

static struct
{
	int enabled;
	int zoom_only;
	int invert_horizontal;
	int invert_vertical;
	float sensitivity_horizontal;
	float sensitivity_vertical;
	unsigned long read_at;
} gyro = { 0, 0, 0, 0, 1.0f, 1.0f, (unsigned long)-1 };

/* (read again when the settings change) */
static void gyro_settings_read(void)
{
	const char *aim;

	if (gyro.read_at == config_changes())
		return;
	gyro.read_at = config_changes();
	aim = config_string("input.gyro_aim");
	gyro.zoom_only = !strcmp(aim, "zoom");
	gyro.enabled = gyro.zoom_only || !strcmp(aim, "true");
	gyro.invert_horizontal = config_boolean("input.gyro_invert_horizontal");
	gyro.invert_vertical = config_boolean("input.gyro_invert_vertical");
	gyro.sensitivity_horizontal = (float)config_real("input.gyro_sensitivity_horizontal");
	gyro.sensitivity_vertical = (float)config_real("input.gyro_sensitivity_vertical");
}

/* whether the gyroscope aims only while the player is zoomed in
(input.gyro_aim = "zoom"): player_control.c drops its turn otherwise */
int halo_linux_gyro_zoom_only(void)
{
	gyro_settings_read();
	return gyro.zoom_only;
}

/* radians of yaw and pitch the pad on port gamepad_index turned since the
last call, with the settings' sensitivity and invert; FALSE for nothing */
int halo_linux_gyro_look(short gamepad_index, float *yaw, float *pitch)
{
	SDL_Gamepad *gamepads[PORT_COUNT];
	SDL_Gamepad *gamepad;
	int count;
	float turn_yaw, turn_pitch;

	*yaw = 0.0f;
	*pitch = 0.0f;
	gyro_settings_read();
	if (!gyro.enabled || gamepad_index < 0)
		return FALSE;
	count = sdl_gamepads(gamepads);
	gamepad = port_gamepad(gamepads, count, gamepad_index);
	if (!gamepad)
		return FALSE;
	host_sdl_gamepad_gyro((unsigned int)(uintptr_t)gamepad, &turn_yaw, &turn_pitch);
	*yaw = (gyro.invert_horizontal ? -turn_yaw : turn_yaw) * gyro.sensitivity_horizontal;
	*pitch = (gyro.invert_vertical ? -turn_pitch : turn_pitch) * gyro.sensitivity_vertical;
	return *yaw != 0.0f || *pitch != 0.0f;
}
#endif

static void sdl_gamepad_state(SDL_Gamepad *gamepad, XINPUT_GAMEPAD *pad)
{
	static const struct
	{
		SDL_GamepadButton button;
		WORD mask;
	} digital[] =
	{
		{ SDL_GAMEPAD_BUTTON_DPAD_UP, XINPUT_GAMEPAD_DPAD_UP },
		{ SDL_GAMEPAD_BUTTON_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN },
		{ SDL_GAMEPAD_BUTTON_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT },
		{ SDL_GAMEPAD_BUTTON_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT },
		{ SDL_GAMEPAD_BUTTON_START, XINPUT_GAMEPAD_START },
		{ SDL_GAMEPAD_BUTTON_BACK, XINPUT_GAMEPAD_BACK },
		{ SDL_GAMEPAD_BUTTON_LEFT_STICK, XINPUT_GAMEPAD_LEFT_THUMB },
		{ SDL_GAMEPAD_BUTTON_RIGHT_STICK, XINPUT_GAMEPAD_RIGHT_THUMB },
	};
	int index;
	int left_trigger, right_trigger;
	SHORT value;

	for (index = 0; index < (int)(sizeof(digital) / sizeof(digital[0])); index++)
	{
		if (SDL_GetGamepadButton(gamepad, digital[index].button))
			pad->wButtons |= digital[index].mask;
	}
	merge_button(pad, XINPUT_GAMEPAD_A, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH));
	merge_button(pad, XINPUT_GAMEPAD_B, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST));
	merge_button(pad, XINPUT_GAMEPAD_X, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST));
	merge_button(pad, XINPUT_GAMEPAD_Y, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH));
	/* the Duke's white and black buttons sit where later pads have shoulders */
	merge_button(pad, XINPUT_GAMEPAD_WHITE, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
	merge_button(pad, XINPUT_GAMEPAD_BLACK, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));

	left_trigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) * 255 / 32767;
	right_trigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) * 255 / 32767;
	if (left_trigger > pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER])
		pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = (BYTE)left_trigger;
	if (right_trigger > pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER])
		pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = (BYTE)right_trigger;

	/* (a button pressed, a trigger pulled or a stick pushed: this pad) */
	{
		int button;
		BOOL used = left_trigger > 64 || right_trigger > 64 ||
			abs(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX)) > 16000 ||
			abs(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY)) > 16000;

		for (button = 0; button < SDL_GAMEPAD_BUTTON_COUNT && !used; button++)
			used = SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)button);
		if (used)
		{
			last_input_scheme = scheme_of(gamepad);
			last_pad_scheme = last_input_scheme;
			last_input_seen = TRUE;
#ifndef HALO_ANDROID
			last_used_gamepad = SDL_GetGamepadID(gamepad);
#endif
		}
	}
	/* a stick only overrides the keyboard when it is pushed further */
	value = stick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX), FALSE);
	if (abs(value) > abs(pad->sThumbLX)) pad->sThumbLX = value;
	value = stick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY), TRUE);
	if (abs(value) > abs(pad->sThumbLY)) pad->sThumbLY = value;
	value = stick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX), FALSE);
	if (abs(value) > abs(pad->sThumbRX)) pad->sThumbRX = value;
	value = stick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY), TRUE);
	if (abs(value) > abs(pad->sThumbRY)) pad->sThumbRY = value;
}

/* ---------- virtual gamepads (tests)

debug.virtual_gamepads attaches that many SDL virtual gamepads (1 to 4),
which the game takes for gamepads plugged in, after any real ones; and
debug.virtual_gamepad_script drives them: entries "<seconds> <pad> <what>"
separated by semicolons, the seconds from when the network lobby first
opens (the game loads at its own pace before), the pad 1 to 4 in the order
attached (0: the keyboard, whose a is SPACE and start Enter), and what one
of a, b, x, y, start, back, up, down, left, right, lb, rb, lt or rt (pressed
for 150 ms and until the game has read it), "walk" (the sticks moving and
turning from then on, each pad its own way) or "still". For testing split
screen without four controllers. */

#define VIRTUAL_PRESS_MS 150

static SDL_Joystick *virtual_pads[PORT_COUNT];
static int virtual_pad_count = -1;
static Uint64 virtual_pads_since;
static BOOL virtual_pad_walking[PORT_COUNT];
/* how many polls each of the script's first entries has been pressed for
(a press lasts until 8 polls, two frames or so, have seen it, however slow
the frames) */
#define VIRTUAL_ENTRIES 64
static int virtual_entry_polls[VIRTUAL_ENTRIES];

/* (menu_functions.c: the network lobby opening) the script's clock starts,
the first time */
void halo_virtual_gamepads_start(void)
{
	if (!virtual_pads_since && virtual_pad_count > 0)
		virtual_pads_since = SDL_GetTicks();
}

static void virtual_gamepads_attach(void)
{
#if defined(HALO_ANDROID) || defined(HALO_CONSOLE_NO_TEST_GAMEPADS)
	/* (the Android host's SDL has no virtual joysticks for the guest: the
	automated test's gamepads are the desktop's; nor a console's, as
	debug.test_gamepads: halo_port_features.h) */
	virtual_pad_count = 0;
#else
	long wanted = config_integer("debug.virtual_gamepads");
	int index;

	virtual_pad_count = 0;
	if (wanted <= 0)
		return;
	/* (the test runs while other windows have the focus) */
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if (wanted > PORT_COUNT)
		wanted = PORT_COUNT;
	for (index = 0; index < wanted; index++)
	{
		/* (SDL keeps the name's pointer) */
		static char names[PORT_COUNT][32];
		SDL_VirtualJoystickDesc desc;
		SDL_JoystickID id;
		char *name = names[index];

		SDL_INIT_INTERFACE(&desc);
		snprintf(name, sizeof(names[index]), "OpenCE virtual gamepad %d", index + 1);
		desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
		desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
		desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
		desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
		desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
		desc.name = name;
		id = SDL_AttachVirtualJoystick(&desc);
		if (!id || !(virtual_pads[virtual_pad_count] = SDL_OpenJoystick(id)))
		{
			platform_log("virtual gamepads: %s", SDL_GetError());
			break;
		}
		virtual_pad_count++;
	}
	platform_log("virtual gamepads: %d attached", virtual_pad_count);
#endif
}

static int virtual_button(const char *what, size_t length)
{
	static const struct
	{
		const char *name;
		int button;
	} buttons[] =
	{
		{ "a", SDL_GAMEPAD_BUTTON_SOUTH }, { "b", SDL_GAMEPAD_BUTTON_EAST },
		{ "x", SDL_GAMEPAD_BUTTON_WEST }, { "y", SDL_GAMEPAD_BUTTON_NORTH },
		{ "start", SDL_GAMEPAD_BUTTON_START }, { "back", SDL_GAMEPAD_BUTTON_BACK },
		{ "up", SDL_GAMEPAD_BUTTON_DPAD_UP }, { "down", SDL_GAMEPAD_BUTTON_DPAD_DOWN },
		{ "left", SDL_GAMEPAD_BUTTON_DPAD_LEFT }, { "right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
		{ "lb", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER }, { "rb", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
	};
	unsigned int index;

	for (index = 0; index < sizeof(buttons) / sizeof(buttons[0]); index++)
	{
		if (strlen(buttons[index].name) == length && !strncmp(what, buttons[index].name, length))
			return buttons[index].button;
	}
	return -1;
}

/* the script's state at this moment, set on the pads */
static void virtual_gamepads_update(void)
{
#if !defined(HALO_ANDROID) && !defined(HALO_CONSOLE_NO_TEST_GAMEPADS)
	BOOL pressed[PORT_COUNT][SDL_GAMEPAD_BUTTON_COUNT];
	int triggers[PORT_COUNT][2];
	BOOL keyboard_pressed = FALSE, keyboard_enter = FALSE;
	const char *entry;
	int entry_index;
	double now;
	int pad, index;

	if (virtual_pad_count < 0)
		virtual_gamepads_attach();
	if (!virtual_pad_count)
		return;
	entry = config_string("debug.virtual_gamepad_script");
	/* (nothing pressed until the lobby opens: halo_virtual_gamepads_start) */
	now = virtual_pads_since ? (double)(SDL_GetTicks() - virtual_pads_since) / 1000.0 : -1.0;
	memset(pressed, 0, sizeof(pressed));
	memset(triggers, 0, sizeof(triggers));
	for (entry_index = 0; *entry; entry_index++)
	{
		double at = 0.0;
		int number = 0, consumed = 0;
		char what[16];
		BOOL held;

		entry += strspn(entry, " ;");
		if (!*entry)
			break;
		held = FALSE;
		if (sscanf(entry, "%lf %d %15[a-z]%n", &at, &number, what, &consumed) == 3)
		{
			held = now >= at && entry_index < VIRTUAL_ENTRIES &&
				(now < at + VIRTUAL_PRESS_MS / 1000.0 || virtual_entry_polls[entry_index] < 8);
			if (held && strcmp(what, "walk") && strcmp(what, "still"))
				virtual_entry_polls[entry_index]++;
		}
		if (sscanf(entry, "%lf %d %15[a-z]%n", &at, &number, what, &consumed) == 3 && number == 0 && held)
		{
			/* (the keyboard: a is SPACE, start Enter) */
			if (!strcmp(what, "a"))
				keyboard_pressed = TRUE;
			else if (!strcmp(what, "start"))
				keyboard_enter = TRUE;
		}
		else if (sscanf(entry, "%lf %d %15[a-z]%n", &at, &number, what, &consumed) == 3 && number >= 1 &&
			number <= virtual_pad_count && now >= at)
		{
			int button = virtual_button(what, strlen(what));

			pad = number - 1;
			if (!strcmp(what, "walk"))
				virtual_pad_walking[pad] = TRUE;
			else if (!strcmp(what, "still"))
				virtual_pad_walking[pad] = FALSE;
			else if (held)
			{
				if (button >= 0)
					pressed[pad][button] = TRUE;
				else if (!strcmp(what, "lt"))
					triggers[pad][0] = 32767;
				else if (!strcmp(what, "rt"))
					triggers[pad][1] = 32767;
			}
		}
		entry += strcspn(entry, ";");
	}
	virtual_keyboard_join = keyboard_pressed;
	virtual_keyboard_done = keyboard_enter;
	for (pad = 0; pad < virtual_pad_count; pad++)
	{
		double t = now + pad * 2.3;
		BOOL walking = virtual_pad_walking[pad];

		for (index = 0; index < SDL_GAMEPAD_BUTTON_COUNT; index++)
			SDL_SetJoystickVirtualButton(virtual_pads[pad], index, pressed[pad][index]);
		SDL_SetJoystickVirtualAxis(virtual_pads[pad], SDL_GAMEPAD_AXIS_LEFTX,
			walking ? (Sint16)(cos(t * 0.6 + pad) * 20000.0) : 0);
		SDL_SetJoystickVirtualAxis(virtual_pads[pad], SDL_GAMEPAD_AXIS_LEFTY,
			walking ? (Sint16)(sin(t * 0.9) * 30000.0) : 0);
		SDL_SetJoystickVirtualAxis(virtual_pads[pad], SDL_GAMEPAD_AXIS_RIGHTX,
			walking ? (Sint16)(sin(t * 0.4 + pad) * 16000.0) : 0);
		SDL_SetJoystickVirtualAxis(virtual_pads[pad], SDL_GAMEPAD_AXIS_RIGHTY, 0);
		/* (a virtual gamepad's trigger reads the axis's whole range: at rest
		its least) */
		SDL_SetJoystickVirtualAxis(virtual_pads[pad], SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
			triggers[pad][0] ? (Sint16)triggers[pad][0] : -32768);
		SDL_SetJoystickVirtualAxis(virtual_pads[pad], SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
			triggers[pad][1] ? (Sint16)triggers[pad][1] : -32768);
	}
#endif
}

/* ---------- XAPI */

VOID WINAPI XInitDevices(DWORD preallocation_type_count, PXDEVICE_PREALLOC_TYPE preallocation_types)
{
	(void)preallocation_type_count;
	(void)preallocation_types;
	platform_sdl_initialize();
}

static DWORD connected_gamepads(void)
{
	SDL_Gamepad *gamepads[PORT_COUNT];
	int count = sdl_gamepads(gamepads);
	DWORD mask = XDEVICE_PORT0_MASK;
	int port;

	/* the first pad shares port 0 with the keyboard (port_gamepad) */
	for (port = 0; port < PORT_COUNT; port++)
		controller_device_update((short)port, port_gamepad(gamepads, count, port));
	for (port = 1; port < PORT_COUNT; port++)
	{
		if (port_gamepad(gamepads, count, port))
			mask |= 1UL << port;
	}
	return mask;
}

BOOL WINAPI XGetDeviceChanges(PXPP_DEVICE_TYPE device_type, PDWORD insertions, PDWORD removals)
{
	*insertions = 0;
	*removals = 0;
	if (device_type == XDEVICE_TYPE_GAMEPAD)
	{
		DWORD connected = connected_gamepads();

		*insertions = connected & ~reported_gamepads;
		*removals = reported_gamepads & ~connected;
		reported_gamepads = connected;
	}
	else if (device_type == XDEVICE_TYPE_DEBUG_KEYBOARD)
	{
		if (!reported_keyboard)
		{
			*insertions = 1;
			reported_keyboard = TRUE;
		}
	}
	return *insertions || *removals;
}

HANDLE WINAPI XInputOpen(PXPP_DEVICE_TYPE device_type, DWORD port, DWORD slot,
	PXINPUT_POLLING_PARAMETERS polling_parameters)
{
	(void)slot;
	(void)polling_parameters;
	if (device_type == XDEVICE_TYPE_GAMEPAD && port < PORT_COUNT)
	{
		memset(&controllers[port], 0, sizeof(controllers[port]));
		controllers[port].open = TRUE;
		return (HANDLE)&controllers[port];
	}
	if (device_type == XDEVICE_TYPE_DEBUG_KEYBOARD && port == 0)
	{
		keyboard_device.open = TRUE;
		return (HANDLE)&keyboard_device;
	}
	SetLastError(ERROR_DEVICE_NOT_CONNECTED);
	return NULL;
}

VOID WINAPI XInputClose(HANDLE device)
{
	struct controller *controller = (struct controller *)device;

	if (controller)
		controller->open = FALSE;
}

static int controller_port(HANDLE device)
{
	int port;

	for (port = 0; port < PORT_COUNT; port++)
	{
		if (device == (HANDLE)&controllers[port] && controllers[port].open)
			return port;
	}
	return -1;
}

/* debug camera (director.c): the middle button, R and F, and the wheel's
notches since the director last asked */
static BOOL debug_cam_middle = FALSE;
static BOOL debug_cam_up = FALSE;
static BOOL debug_cam_down = FALSE;
static float debug_cam_wheel = 0.0f;

static void debug_camera_update(const struct platform_input_state *input, BOOL blocked)
{
	pthread_mutex_lock(&mouse_lock);
	debug_cam_middle = !blocked && !input->mouse_released &&
		input->mouse_buttons[SDL_BUTTON_MIDDLE] != 0;
	debug_cam_up = !blocked && input->keys[SDL_SCANCODE_R] != 0;
	debug_cam_down = !blocked && input->keys[SDL_SCANCODE_F] != 0;
	if (debug_cam_middle)
		debug_cam_wheel += input->mouse_wheel;
	else
		debug_cam_wheel = 0.0f;
	pthread_mutex_unlock(&mouse_lock);
}

/* read by director.c (halo_linux_source_fixups.h); the wheel's notches are
returned once, then cleared */
int halo_linux_debug_camera_input(int *middle, int *up, int *down, float *wheel)
{
	pthread_mutex_lock(&mouse_lock);
	*middle = debug_cam_middle;
	*up = debug_cam_up;
	*down = debug_cam_down;
	*wheel = debug_cam_wheel;
	debug_cam_wheel = 0.0f;
	pthread_mutex_unlock(&mouse_lock);
	return TRUE;
}

/* reads a controller's state; for port 0 the keyboard, mouse, debug input
and touchscreen are merged into the first gamepad's; runs on the game's main
thread (touch_input_gamepad relies on it); returns ERROR_SUCCESS or
ERROR_DEVICE_NOT_CONNECTED for an unknown port */
DWORD WINAPI XInputGetState(HANDLE device, PXINPUT_STATE state)
{
	int port = controller_port(device);
	SDL_Gamepad *gamepads[PORT_COUNT];
	int count;
	XINPUT_GAMEPAD physical;
	int captured;

	memset(state, 0, sizeof(*state));
	if (port < 0)
		return ERROR_DEVICE_NOT_CONNECTED;
#if !defined(HALO_ANDROID) && !defined(HALO_CONSOLE_NO_TEST_GAMEPADS)
	test_virtual_pads_update();
#endif
	/* (and OpenCE's: debug.virtual_gamepads, pressed by
	debug.virtual_gamepad_script) */
	virtual_gamepads_update();
	platform_pump_events();
	split_players = pc_menu_split_players() != 0;
	count = sdl_gamepads(gamepads);
	controller_device_update((short)port, port_gamepad(gamepads, count, port));
	/* Keep physical remapping separate from the keyboard and touch controls. */
	memset(&physical, 0, sizeof(physical));
	if (port_gamepad(gamepads, count, port))
		sdl_gamepad_state(port_gamepad(gamepads, count, port), &physical);
	/* Keep the opening chat press so even B can open it without immediately closing it. */
	chat_pad_record((short)port, &physical);
	chat_physical_pad[port] = chat_pad[port];
	captured = controller_remap_input((short)port, &physical, port_gamepad(gamepads, count, port) != NULL);
	if (port == 0)
	{
		struct platform_input_state input;
		unsigned long held;
		BOOL console_active;

		platform_input_read(&input, TRUE);
		mouse_poll(&input);
		wheel_update();
		keyboard_actions_held = 0;
		keys_held_over_switch(&input);
		test_input_menu_keys(&input);
		chat_escape_update(&input);
		/* (nothing bound while the console or a chat line, either text
		chat's, is being typed) */
		console_active = console_is_active() != 0 || chat_typing || network_chat_typing();
		debug_camera_update(&input, console_active);
		held = console_active ? 0 : keyboard_bound_actions(&input);
		/* (what the player last used: a gamepad's button or stick, or a key
		or mouse button bound to an action) */
		{
			enum { MOVED = 8000 };
			int index;
			int used = physical.wButtons != 0 || abs(physical.sThumbLX) > MOVED || abs(physical.sThumbLY) > MOVED ||
				abs(physical.sThumbRX) > MOVED || abs(physical.sThumbRY) > MOVED;

			for (index = 0; index < 8 && !used; index++)
				used = physical.bAnalogButtons[index] > 0x40;
			pthread_mutex_lock(&mouse_lock);
			if (used)
				pad_used_ms = SDL_GetTicks();
			if (held)
				keyboard_used_ms = SDL_GetTicks();
			pthread_mutex_unlock(&mouse_lock);
		}
		keyboard_screenshot(held);
		if (!console_active)
		{
			if (input.menus)
				keyboard_gamepad(&input, &state->Gamepad);
			else
				keyboard_controls(held, &state->Gamepad);
		}
		if (port_gamepad(gamepads, count, 0))
			controller_remap_merge(&physical, &state->Gamepad);
		test_input_gamepad(&state->Gamepad);
		touch_input_gamepad(&state->Gamepad);
#if defined(HALO_ANDROID) || defined(HALO_WEB)
		touch_input_controls(&state->Gamepad, input.menus);
#endif
		if (abs(state->Gamepad.sThumbRX) > STICK_AIMING_DEFLECTION ||
			abs(state->Gamepad.sThumbRY) > STICK_AIMING_DEFLECTION)
		{
			pthread_mutex_lock(&mouse_lock);
			stick_aimed_ms = SDL_GetTicks();
			pthread_mutex_unlock(&mouse_lock);
		}
	}
	else if (port_gamepad(gamepads, count, port))
	{
		state->Gamepad = physical;
	}
	/* Record the owner's navigation before suppressing their gameplay input. */
	chat_pad_record((short)port, &state->Gamepad);
	chat_pad_hold_over((short)port, &state->Gamepad);
	/* (and a vote running takes the first controller's D-pad left and
	right: network_votekick.c) */
	if (port == 0)
		vote_pad_hold_over(&state->Gamepad);
	if (captured) memset(&state->Gamepad, 0, sizeof(state->Gamepad));

	if (memcmp(&state->Gamepad, &controllers[port].previous, sizeof(state->Gamepad)))
	{
		controllers[port].packet_number++;
		controllers[port].previous = state->Gamepad;
	}
	state->dwPacketNumber = controllers[port].packet_number;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputSetState(HANDLE device, PXINPUT_FEEDBACK feedback)
{
	int port = controller_port(device);
	SDL_Gamepad *gamepads[PORT_COUNT];
	int count;

	if (!feedback)
		return ERROR_INVALID_PARAMETER;
	feedback->Header.dwStatus = ERROR_SUCCESS;
	if (port < 0)
		return ERROR_DEVICE_NOT_CONNECTED;
#if defined(HALO_ANDROID) || defined(HALO_WEB)
	/* the phone vibrates for the touch controls' player (in a browser, where
	it can) */
	if (port == 0)
		touch_input_rumble(feedback->Rumble.wLeftMotorSpeed, feedback->Rumble.wRightMotorSpeed);
#endif
	count = sdl_gamepads(gamepads);
	if (port_gamepad(gamepads, count, port))
	{
		/* the game refreshes the motors every frame; rumble a little longer
		than that so they do not stutter */
		SDL_RumbleGamepad(port_gamepad(gamepads, count, port), feedback->Rumble.wLeftMotorSpeed,
			feedback->Rumble.wRightMotorSpeed, 100);
	}
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugInitKeyboardQueue(PXINPUT_DEBUG_KEYQUEUE_PARAMETERS parameters)
{
	(void)parameters;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugGetKeystroke(PXINPUT_DEBUG_KEYSTROKE keystroke)
{
	struct platform_keystroke next;

	memset(keystroke, 0, sizeof(*keystroke));
	while (platform_next_keystroke(&next))
	{
		BOOL key_up = (next.flags & XINPUT_DEBUG_KEYSTROKE_FLAG_KEYUP) != 0;

		/* key ups always pass, so no key is left latched down; while typing,
		escape does not (it cancels, as B: the game's own escape leaves the
		menus, main.c); text chat's line and log take every key, escape
		included (chat.c: it closes them), as OpenCE's text chat's line does
		(network_chat.c) */
		if (key_up || next.virtual_key == VK_OEM_3_BACKQUOTE || console_is_active() || chat_typing ||
			network_chat_typing() ||
			(text_typing && next.virtual_key != 0x1B /* escape */))
		{
			keystroke->VirtualKey = next.virtual_key;
			keystroke->Ascii = next.ascii;
			keystroke->Flags = next.flags;
			return ERROR_SUCCESS;
		}
	}
	return ERROR_HANDLE_EOF;
}
