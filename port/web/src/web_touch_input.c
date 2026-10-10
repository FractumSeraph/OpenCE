/* The browser build's stand-ins for the Android touchscreen
(port/linux/src/touch_input.c, OpenCE's bridge to the Android host's touch
overlay, which reads the host's events and is left out of the web build:
WEB_EXCLUDED_PLATFORM_SOURCES in tools/web_build.py). The page has touch
controls of its own (port/web/assets/touch), which press keys and move the
mouse; so these do nothing, as on a desktop with no touchscreen. And the updater's version,
which the window's title asks for (updater.c, also left out). */

#include "platform.h"
#include "touch_input.h"

void touch_input_event(unsigned int type, const SDL_TouchFingerEvent *finger)
{
	(void)type;
	(void)finger;
}

void touch_input_cancel(void)
{
}

void touch_input_menu_set_active(int active)
{
	(void)active;
}

void touch_input_menu_read(struct platform_ui_pointer *pointer)
{
	(void)pointer;
}

void touch_input_gamepad(XINPUT_GAMEPAD *pad)
{
	(void)pad;
}

void touch_input_controls(XINPUT_GAMEPAD *pad, int menus)
{
	(void)pad;
	(void)menus;
}

int touch_input_move(float *forward, float *strafe)
{
	*forward = 0.0f;
	*strafe = 0.0f;
	return 0;
}

void touch_input_look(float scale, float *yaw, float *pitch, float *gyro_yaw, float *gyro_pitch)
{
	(void)scale;
	(void)yaw;
	(void)pitch;
	(void)gyro_yaw;
	(void)gyro_pitch;
}

int touch_input_aim_assist(void)
{
	return 0;
}

void touch_input_rumble(unsigned int left, unsigned int right)
{
	(void)left;
	(void)right;
}

const char *updater_version(void)
{
	return "web";
}
