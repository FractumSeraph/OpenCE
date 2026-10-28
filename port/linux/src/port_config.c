/*
PORT_CONFIG.C

The native ports' settings (port_config.h), parsed with tomlc17
(port/third_party/tomlc17). Every setting is in the table below with its
type, default, the HALO_* environment variable that overrides it and the
comment written into a new file. The file is read once, on the first
question; unknown keys and values of the wrong type are reported in the log
and the defaults used instead. A new file has every setting commented out at
its default, so that a later version's default reaches it; a setting is
written as a value only when the player chooses one (config_write). Once
the file exists only those lines, the settings a newer version adds and an
older file's layout (config_version) are changed in it, so that the player's
edits and comments stay.

The original Xbox builds this file too (warthog-xbox's port/xbox/build.py, HALO_XBOX_CONSOLE):
its D:\config.toml beside default.xbe has the settings marked for it, read
and written with the C runtime's files in place of SDL's; it has no
environment, and a setting not marked for it reads as empty, 0 or false, as
it did before the console had the file.
*/

#include "platform.h"
#include "port_config.h"
#include "halo_port_limits.h"
#include "posix.h"
#if !defined(HALO_ANDROID) && !defined(HALO_WEB)
/* (the command line: --config, --server, --probe...) */
#include "posix_server.h"
#endif
#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
#include "packaged_install.h"
#endif

#include <time.h>
#include "tomlc17.h"

#ifndef HALO_XBOX_CONSOLE
#include <SDL3/SDL.h>
#endif
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>

#ifdef HALO_SWITCH_CONSOLE
/* the guest's write is not on the card until the host commits it
(port/switch/host/host_main.c) */
void host_storage_commit(void);
#endif
#endif

#ifdef HALO_XBOX_CONSOLE
/* (the console's C runtime's) */
#define SDL_strncasecmp _strnicmp
#endif

/* ---------- the settings */

enum config_type
{
	_config_boolean,
	_config_integer,
	_config_real,
	_config_string,
};

/* how the setting's environment variable sets it */
enum config_environment
{
	/* the variable's text is the value ("0", "false", "no" and "off" are
	false for a boolean) */
	_environment_value,
	/* the variable being set makes it true */
	_environment_set_is_true,
	/* the variable being set makes it false */
	_environment_set_is_false,
};

/* the builds a setting means something in, and is written for */
enum
{
	_platform_desktop = 1,
	_platform_android = 2,
	_platform_all = _platform_desktop | _platform_android,
	/* (of the desktop builds, only Windows) */
	_platform_windows = 4,
	/* (the web build, which has the desktop's settings, and these) */
	_platform_web = 8,
	/* the original Xbox (port/xbox): not in _platform_all, each of its
	settings marked for it */
	_platform_xbox = 16,
	/* (a dedicated server's, of the Linux and Windows builds: written into
	config.toml only in server mode; OpenCE's 16, the Xbox's here) */
	_platform_server = 32,
};

struct config_setting
{
	const char *name;
	enum config_type type;
	/* as it is written in the file */
	const char *default_value;
	/* NULL: none, for a setting the Android app reads from the file itself,
	or one only the console has */
	const char *environment;
	enum config_environment environment_style;
	unsigned platforms;
	const char *comment;
};

/* macOS starts in a window, as its games do */
#ifdef __APPLE__
#define DEFAULT_FULLSCREEN "false"
#else
#define DEFAULT_FULLSCREEN "true"
#endif

/* macOS's audio cuts out with SDL's 512-frame buffer when the mixer is late
(a 10.6 ms budget at 48 kHz); 2048 frames (43 ms) rides that out. On
Android each callback is handed to a thread that can run the guest
(port/android/host/host_sdl.c): 512 left it too little time and the menus'
music broke up, which 1024 (21 ms) does not. Elsewhere 512 keeps the
latency low. */
#ifdef __APPLE__
#define DEFAULT_AUDIO_BUFFER_FRAMES "2048"
#elif defined(HALO_ANDROID) && !defined(HALO_BROWSER)
#define DEFAULT_AUDIO_BUFFER_FRAMES "1024"
#else
#define DEFAULT_AUDIO_BUFFER_FRAMES "512"
#endif

/* crouching's keys (in a browser C alone: a page cannot keep Ctrl+W, Ctrl+S or
Ctrl+D, which moving and crouching would press, from the browser) */
#ifdef HALO_WEB
#define CROUCH_KEYS "\"C\""
#else
#define CROUCH_KEYS "\"Left Ctrl, C\""
#endif

/* The 3D view's anti-aliasing and the objects' shadow maps: the sharpest
picture the builds' computers draw comfortably, with the look kept
(multisampling smooths only the edges of what is drawn; a larger shadow map
only the steps along a shadow's edge, its blur widened to keep it as soft).
The 64-bit desktop builds take 2 samples a pixel (on an M4 at 1920x1080,
0.4 ms a frame more; 4 samples cost three times that, which a large display
multiplies) and 256 (about nothing); the 32-bit builds, for older
computers, and Android, whose picture is 480 lines, 256 and no
anti-aliasing. The server draws nothing: the Xbox's. */
#if defined(HALO_SERVER)
#define DEFAULT_ANTI_ALIASING "\"off\""
#define DEFAULT_SHADOW_RESOLUTION "128"
#elif defined(HALO_64BIT) && !defined(HALO_ANDROID)
#define DEFAULT_ANTI_ALIASING "\"msaa2x\""
#define DEFAULT_SHADOW_RESOLUTION "256"
#else
#define DEFAULT_ANTI_ALIASING "\"off\""
#define DEFAULT_SHADOW_RESOLUTION "256"
#endif

static const struct config_setting config_settings[] =
{
	{ "display.fullscreen", _config_boolean, DEFAULT_FULLSCREEN, "HALO_FULLSCREEN", _environment_value, _platform_desktop,
		"When display.mode is empty: true starts borderless, false in a window.\n"
		"F11 switches." },
	{ "display.mode", _config_string, "\"\"", "HALO_DISPLAY_MODE", _environment_value, _platform_desktop,
		"\"fullscreen\", \"borderless\" or \"windowed\". Empty: display.fullscreen's.\n"
		"F11 switches to the window and back." },
	{ "display.resolution", _config_string, "\"native\"", "HALO_RESOLUTION", _environment_value, _platform_desktop,
		"What fullscreen and borderless draw at: \"native\" or \"<width>x<height>\"\n"
		"(\"1920x1080\"), 640x480 or more." },
	{ "display.resolution_scaling", _config_string, "\"native\"", "HALO_RESOLUTION_SCALING", _environment_value,
		_platform_desktop,
		"\"native\" draws at the window's resolution; \"original\" draws 640x480\n"
		"and scales it up." },
	{ "display.window_size", _config_string, "\"\"", "HALO_WINDOW_SIZE", _environment_value, _platform_desktop,
		"The window's size, \"<width>x<height>\" (\"1920x1080\"), 640x480 or more.\n"
		"Empty: display.window_scale's." },
	{ "display.window_scale", _config_integer, "2", "HALO_WINDOW_SCALE", _environment_value, _platform_desktop,
		"When display.window_size is empty: the window's size as a multiple of\n"
		"640x480." },
	{ "display.screen_width", _config_integer, "0", "HALO_SCREEN_WIDTH", _environment_value, _platform_android,
		"The picture's width in columns: 0 matches the display, 640 is 4:3." },
	{ "display.round_screen", _config_string, "\"auto\"", "HALO_ROUND_SCREEN", _environment_value, _platform_android,
		"A round display (a watch's): \"auto\" (as the system says), \"on\" or \"off\".\n"
		"On one, the HUD is laid out inside the circle." },
#if defined(HALO_ANDROID) && !defined(HALO_SWITCH_CONSOLE)
	/* (the Switch's own is below; port/linux/src/d3d8_gl.c) */
	{ "display.resolution_limit", _config_string, "\"480\"", "HALO_RESOLUTION_LIMIT", _environment_value,
		_platform_android,
		"The lines the game draws at: \"480\" (the game's own), \"720\", \"900\",\n"
		"\"1080\" (or any number from 240) or \"native\" (the display's), no more\n"
		"than the display shows (a watch's 438x438 shows the picture on 328),\n"
		"which the picture is scaled to. More is sharper and uses more battery.\n"
		"Changes at once." },
#endif
	{ "display.texture_decode", _config_string, "\"auto\"", "HALO_TEXTURE_DECODE", _environment_value,
		_platform_android,
		"Where compressed textures are unpacked: \"gpu\", \"cpu\" (more memory) or\n"
		"\"auto\" (\"cpu\" on Adreno graphics, \"gpu\" on others)." },
#ifdef HALO_SWITCH_CONSOLE
	/* the Switch's (port/switch): read by the deko3d renderer in the guest
	(port/switch/guest/d3d8_dk.c) and by the host from config.toml itself */
	/* (720 by default: a handheld console's own lines, 1280x720) */
	{ "display.resolution_limit", _config_string, "\"720\"", "HALO_RESOLUTION_LIMIT", _environment_value,
		_platform_android,
		"The lines the game draws at: \"480\" (the game's own), \"720\", \"900\" or\n"
		"\"1080\" (or any number between), no more than the console shows (1280x720\n"
		"in handheld, 1920x1080 docked), which the picture is scaled up to. Changes\n"
		"at once." },
	{ "display.big_sprite_limit", _config_integer, "8", "HALO_BIG_SPRITE_LIMIT", _environment_value, _platform_android,
		"The most sprites covering more than half the view drawn in a frame (the\n"
		"Xbox's 11): each is a layer over most of the screen, and the Halo level's\n"
		"falls have 11 sheets of mist. Changes at once." },
	{ "display.dynamic_resolution", _config_boolean, "false", "HALO_DYNAMIC_RESOLUTION", _environment_value,
		_platform_android,
		"While the GPU would hold the game under 55 frames a second (for half a\n"
		"second), draw one step lower (1080 to 900, 900 to 720, 720 to 480: never\n"
		"more), and go back once the frame would fit in 90% of a 60 Hz frame for\n"
		"a second. Changes at once." },
#endif
#ifdef HALO_BGFX
	{ "display.renderer", _config_string, "\"gl\"", "HALO_RENDERER", _environment_value, _platform_desktop,
		"What draws the game: \"gl\", OpenGL; \"bgfx\", the bgfx test backend\n"
		"(Metal on macOS), a work in progress. Read at start-up." },
	{ "display.crt_filter", _config_boolean, "false", "HALO_CRT_FILTER", _environment_set_is_true, _platform_desktop,
		"A CRT look over the picture (scanlines, curvature, phosphor mask); the\n"
		"bgfx renderer only, for now." },
#endif
	{ "display.vsync", _config_boolean, "true", "HALO_NO_VSYNC", _environment_set_is_false, _platform_all,
		"Wait for the display between frames." },
	{ "display.max_fps", _config_integer, "0", "HALO_MAX_FPS", _environment_value, _platform_all,
		"With vsync off, the frame rate limit: 0 for twice the display's rate,\n"
		"-1 for none (can hang some Intel graphics, and heats a phone)." },
	{ "display.frames_in_flight", _config_integer, "3", "HALO_FRAMES_IN_FLIGHT", _environment_value,
		_platform_desktop,
		"The most frames the GPU has queued (1 to 8): a frame waits for the GPU\n"
		"to finish the one this many before it, so that frames come evenly when\n"
		"the GPU is the slower; 0 leaves it to the driver." },
	{ "display.anti_aliasing", _config_string, DEFAULT_ANTI_ALIASING, "HALO_ANTI_ALIASING", _environment_value,
		_platform_all,
		"Smooth jagged edges: \"off\", \"fxaa\", \"smaa\", \"ssaa2x\", \"msaa2x\",\n"
		"\"msaa4x\" or \"msaa8x\". Android has \"off\", \"fxaa\", \"msaa2x\" and \"msaa4x\".\n"
		"Default: \"msaa2x\" on 64-bit computers, \"off\" on 32-bit ones and Android." },
	{ "display.interpolation", _config_boolean, "true", "HALO_INTERPOLATION", _environment_value, _platform_all,
		"Draw a frame for every display refresh, blending between the game's 30\n"
		"ticks a second." },
	{ "display.direct_camera", _config_boolean, "true", "HALO_DIRECT_CAMERA", _environment_value, _platform_all | _platform_xbox,
		"In first person, turn the view the same frame the mouse moves." },
	{ "display.fov", _config_real, "0.0", "HALO_FOV", _environment_value, _platform_all,
		"The first-person view's field of view on foot, in degrees across at\n"
		"16:9 (20 to 150); 0 keeps the stock view. Vehicles, cinematics and\n"
		"scripted cameras keep their own." },
	{ "display.viewmodel_fov", _config_real, "0.0", "HALO_VIEWMODEL_FOV", _environment_value, _platform_all,
		"The first-person weapon's and hands' field of view, in degrees across\n"
		"at 16:9 (20 to 150); 0 keeps the weapon's stock view, also when\n"
		"display.fov widens the world." },
	{ "display.viewmodel_visible", _config_boolean, "true", "HALO_VIEWMODEL_VISIBLE", _environment_value, _platform_all,
		"Draw the first-person weapon, hands and what is attached to them.\n"
		"Off, they are not drawn; firing, animation, sound and lights go on." },
	{ "display.viewmodel_shield", _config_boolean, "false", "HALO_VIEWMODEL_SHIELD", _environment_value, _platform_all,
		"The energy shield's flare on the first-person arms too, as on the body\n"
		"(the stock game draws it on the body only)." },
	{ "display.first_person_legs", _config_boolean, "false", "HALO_FIRST_PERSON_LEGS", _environment_value, _platform_all,
		"Your own legs in first person, seen looking down: your body as others\n"
		"see it, drawn from the waist down, moving as it moves." },
	{ "display.shield_glow", _config_boolean, "false", "HALO_SHIELD_GLOW", _environment_value, _platform_all,
		"Energy shields light what is around them as they flare, as plasma\n"
		"does, in the shield's own color. In a network game, the host's\n"
		"network.shield_effects decides, unless it is \"players\" (each\n"
		"player's own)." },
	{ "display.shield_glow_intensity", _config_string, "\"default\"", "HALO_SHIELD_GLOW_INTENSITY", _environment_value, _platform_all,
		"How far the shield glow reaches: \"default\" or \"light_show\" (three\n"
		"times as far, and at full strength from a third of the flare).\n"
		"In a network game, the host's network.shield_effects decides, unless\n"
		"it is \"players\" (each player's own)." },
	{ "display.shield_color", _config_string, "\"default\"", "HALO_SHIELD_COLOR", _environment_value, _platform_all,
		"Your energy shield's color, as this machine draws it: \"default\" (the\n"
		"shield's own) or a multiplayer armor color: white, black, red, blue,\n"
		"gray, yellow, green, pink, purple, cyan, cobalt, orange, teal, sage,\n"
		"brown, tan, maroon or salmon." },
	{ "display.overshield_effect", _config_boolean, "false", "HALO_OVERSHIELD_EFFECT", _environment_value, _platform_all,
		"The overshield as this machine draws it: its flare rising up the body as\n"
		"it charges (2 seconds, 3 from a shield that was down) and staying lit,\n"
		"its energy slower, in its own color (the powerup's), on the first-person\n"
		"arms too with viewmodel_shield; a hit on it plays what the projectile\n"
		"plays on an Elite's shield. Only what is drawn and heard changes.\n"
		"In a network game, the host's network.shield_effects decides, unless\n"
		"it is \"players\" (each player's own)." },

	{ "display.far_clip_distance", _config_real, "1024.0", "HALO_FAR_CLIP_DISTANCE", _environment_value, _platform_all,
		"How far the world is drawn, up to 10000. 1024: the Xbox's." },
	{ "display.match_timer", _config_boolean, "false", "HALO_MATCH_TIMER", _environment_value, _platform_all | _platform_xbox,
		"In a multiplayer game with a time limit, show the time left at the top\n"
		"of the screen." },
	{ "display.passenger_third_person", _config_boolean, "false", "HALO_PASSENGER_THIRD_PERSON", _environment_value, _platform_all,
		"Third-person Warthog and Scorpion passengers. Weapon zoom is disabled." },
	{ "display.high_res_hud", _config_boolean, "true", "HALO_HIGH_RES_HUD", _environment_value, _platform_all,
		"Draw the HUD from the high-res assets." },
	{ "display.hud_button_style", _config_string, "\"classic\"", "HALO_HUD_BUTTON_STYLE", _environment_value, _platform_all,
		"High-res gameplay controller buttons: classic uses plain Xbox faces;\n"
		"gems uses shaded Xbox faces; menu_gems uses the menu's button art.\n"
		"Requires display.high_res_hud." },
	{ "display.high_res_text", _config_boolean, "true", "HALO_HIGH_RES_TEXT", _environment_value, _platform_all,
		"Draw the menus' and HUD's text and titles with high-res fonts." },
	{ "display.high_res_font", _config_string, "\"\"", "HALO_HIGH_RES_FONT", _environment_value, _platform_all,
		"A font file (.ttf or .otf) for the high-res text and Online Games. A\n"
		"name alone is a file in data/fonts beside the maps folder. Empty: the\n"
		"built-in fonts." },
	{ "display.high_res_font_large", _config_string, "\"\"", "HALO_HIGH_RES_FONT_LARGE", _environment_value,
		_platform_all,
		"A font file for the large text only, as display.high_res_font. Empty:\n"
		"display.high_res_font's." },
	{ "display.version_watermark", _config_boolean, "false", "HALO_VERSION_WATERMARK", _environment_value,
		_platform_all | _platform_xbox,
		"Show the build's version in the bottom right corner while playing." },
	{ "display.shadow_resolution", _config_integer, DEFAULT_SHADOW_RESOLUTION, "HALO_SHADOW_RESOLUTION",
		_environment_value, _platform_all,
		"Shadow size in pixels: 128, 256, 512 or 1024. 128: the Xbox's." },
	{ "display.anisotropic_filtering", _config_integer, "0", "HALO_ANISOTROPIC_FILTERING", _environment_value,
		_platform_all,
		"Sharper textures at a slant: 0 (off, the Xbox's), 2, 4, 8 or 16." },
	{ "display.dxt1_astc", _config_boolean, "true", "HALO_DXT1_ASTC", _environment_value, _platform_android,
		"On a GPU without DXT textures (Mali), give it the DXT1 textures as ASTC,\n"
		"which holds them nearly as they are in a quarter of the memory of the\n"
		"32-bit texels they are otherwise decoded to; false decodes them." },
/* (ChupathingyCE's particle and light limits, among the display settings;
0.8.0h's own merge had put the light limits inside debug.gpu_fault_test's
entry, which ChupathingyCE fixed in e3bd5a15) */
#ifdef HALO_SWITCH_CONSOLE
	{ "display.particle_limit", _config_integer, "2048", "HALO_PARTICLE_LIMIT", _environment_value, _platform_all,
#else
	{ "display.particle_limit", _config_integer, "4096", "HALO_PARTICLE_LIMIT", _environment_value, _platform_all,
#endif
		"The most particles (sparks, smoke, casings, debris) alive at once; past\n"
		"it the oldest goes to make room for each new one. The Xbox's limit was\n"
		"1024. 0 for the port's own 8192 (a 64-player firefight fills them, and\n"
		"every one costs the processor and the GPU: 2048 on the Switch, 4096\n"
		"elsewhere). Changes at once." },
#ifdef HALO_CONSOLE_PARTICLE_TICKS
	{ "display.particle_ticks", _config_boolean, "true", "HALO_PARTICLE_TICKS", _environment_value, _platform_all,
#else
	{ "display.particle_ticks", _config_boolean, "false", "HALO_PARTICLE_TICKS", _environment_value, _platform_all,
#endif
		"Move the particles that are not attached to an object (sparks, casings,\n"
		"debris) once a 30 Hz tick, as on the Xbox, and draw them between their\n"
		"ticks: two in three of their collision tests spared at 60 frames a\n"
		"second, more at 120. false moves them at every frame drawn, smooth at\n"
		"high refresh rates: the desktop's default; the consoles' is true.\n"
		"Takes effect at the next map." },
#ifdef HALO_CONSOLE_LIGHT_LIMITS
	{ "display.dynamic_light_limit", _config_integer, "8", "HALO_DYNAMIC_LIGHT_LIMIT", _environment_value, _platform_all,
#else
	{ "display.dynamic_light_limit", _config_integer, "0", "HALO_DYNAMIC_LIGHT_LIMIT", _environment_value, _platform_all,
#endif
		"The most dynamic lights (muzzle flashes, plasma, explosions) that light\n"
		"the level at once: the biggest on the screen keep their light on the\n"
		"walls and floor, the rest light only the objects near them. 0 for no\n"
		"limit, as on the Xbox (each redraws the level it reaches twice: a\n"
		"firefight of many is the most of a busy frame's GPU time): the\n"
		"desktop's default; the consoles' is 8. Changes at once." },
#ifdef HALO_CONSOLE_LIGHT_LIMITS
	{ "display.specular_light_limit", _config_integer, "2", "HALO_SPECULAR_LIGHT_LIMIT", _environment_value, _platform_all,
#else
	{ "display.specular_light_limit", _config_integer, "0", "HALO_SPECULAR_LIGHT_LIMIT", _environment_value, _platform_all,
#endif
		"Of those, the most that add their specular highlights (the shine on\n"
		"metal and wet floors): the biggest on the screen, the first-person\n"
		"weapon's first. The others still light the level, without the shine.\n"
		"0 for no limit, as on the Xbox (a near light's highlights cover most\n"
		"of the screen: the costliest pass of a firefight): the desktop's\n"
		"default; the consoles' is 2. Changes at once." },
	{ "display.full_model_detail", _config_boolean, "false", "HALO_FULL_MODEL_DETAIL", _environment_value,
		_platform_all,
		"Draw every model at full detail at any distance." },
#ifdef HALO_BROWSER
	/* (the browser build's online play, the page's rooms and the native
	gateway, is wired into the PC screens: Create Game and the Server Browser) */
	{ "display.menus", _config_string, "\"pc\"", "HALO_MENUS", _environment_value, _platform_all,
		"The menus: \"xbox\" for the Xbox's (with Online Games), \"pc\" for the\n"
		"PC version's screens and Server Browser, rebuilt from port/assets/menus\n"
		"(a menus folder here for your own) with this port's fonts and redrawn\n"
		"pictures, not Halo PC's files. Read when the game starts: restart it\n"
		"after a change. Not all of the PC screens are wired yet." },
#else
	{ "display.menus", _config_string, "\"chupathingy\"", "HALO_MENUS", _environment_value, _platform_all,
		"The menus: \"chupathingy\", \"xbox\" or \"gearbox\". Your own go in a\n"
		"menus folder beside config.toml. Takes effect after a restart." },
#endif
	{ "display.menu_language", _config_string, "\"maps\"", "HALO_MENU_LANGUAGE", _environment_value, _platform_all,
		"The PC menus' language: \"maps\" for the language of the Custom Edition\n"
		"whose loc.map is in custom_maps (as that edition shows them; English\n"
		"without one), \"english\" for English. The port's own lines are English." },
	{ "display.player_names", _config_string, "\"all\"", "HALO_PLAYER_NAMES", _environment_value, _platform_all,
		"In multiplayer, whose names are drawn above their heads: \"all\",\n"
		"\"allies\", \"enemies\" or \"none\"." },
	{ "display.player_name_scale", _config_real, "1.0", "HALO_PLAYER_NAME_SCALE", _environment_value, _platform_all,
		"The size of player names, 0.25 to 4." },
	{ "display.text_chat", _config_boolean, "true", "HALO_TEXT_CHAT", _environment_value, _platform_all,
		"Show the text chat of network games: false hides its lines and keeps\n"
		"its keys (and a controller's D-pad Up) from opening it, as for purists\n"
		"and streamers." },
	{ "display.chat_style", _config_string, "\"colors\"", "HALO_CHAT_STYLE", _environment_value, _platform_all,
		"The text chat's colours: \"colors\" (names in their team's colour, the\n"
		"text a pale blue) or \"white\" (all white, as Custom Edition)." },
	{ "display.follow_camera", _config_string, "\"long_waits\"", "HALO_FOLLOW_CAMERA", _environment_value,
		_platform_all | _platform_xbox,
		"After a death, follow a living player's view: \"long_waits\" (only\n"
		"during a long respawn wait), \"always\" or \"off\". LB and RB, or Q and E,\n"
		"switch players." },
	{ "display.follow_camera_after", _config_real, "5.0", "HALO_FOLLOW_CAMERA_AFTER", _environment_value,
		_platform_all | _platform_xbox,
		"For \"long_waits\": the seconds of respawn wait left that start the\n"
		"follow camera." },
	{ "display.powerup_timers", _config_boolean, "false", "HALO_POWERUP_TIMERS", _environment_value, _platform_all | _platform_xbox,
		"Show the seconds left on your active camouflage, overshield and full\n"
		"spectrum vision." },
	{ "display.loading_screen", _config_boolean, "true", "HALO_LOADING_SCREEN", _environment_value,
		_platform_all,
		"Keep the original loading screen up for a while when a level loads,\n"
		"as loading from the Xbox's disc did, for the times below; false loads\n"
		"every level as fast as it can." },
	{ "display.loading_screen_campaign_seconds", _config_real, "0.0", "HALO_LOADING_SCREEN_CAMPAIGN_SECONDS",
		_environment_value, _platform_all,
		"The least seconds the loading screen stays up when a campaign level\n"
		"loads on this machine alone, split screen co-op too (up to 30); 0 for\n"
		"no wait. A longer load shows for as long as it takes." },
	{ "display.loading_screen_multiplayer_seconds", _config_real, "0.0", "HALO_LOADING_SCREEN_MULTIPLAYER_SECONDS",
		_environment_value, _platform_all,
		"The same for every other game: split screen multiplayer, system link,\n"
		"online and network co-op, up to 5: the network waits while the\n"
		"loading screen is held." },
	{ "display.scoreboard_team_layout", _config_string, "\"teams\"", "HALO_SCOREBOARD_TEAM_LAYOUT", _environment_value,
		_platform_all | _platform_xbox,
		"A team game's scoreboard: \"teams\" (a column for each team) or \"score\"\n"
		"(everyone in order of score)." },
	{ "display.scoreboard_background", _config_boolean, "true", "HALO_SCOREBOARD_BACKGROUND", _environment_value,
		_platform_all | _platform_xbox,
		"Draw a panel behind the multiplayer scoreboard." },
	{ "display.scoreboard_platforms", _config_string, "\"auto\"", "HALO_SCOREBOARD_PLATFORMS", _environment_value,
		_platform_all,
		"Show each Chupathingy player's platform logo on the scoreboard: \"auto\"\n"
		"(Chupathingy menus only), \"on\" or \"off\"." },
	{ "display.delta_profile", _config_boolean, "true", "HALO_DELTA_PROFILE", _environment_value, _platform_all,
		"Show Delta Profile in the menus and Online Games." },
	{ "display.profile_names", _config_boolean, "true", "HALO_PROFILE_NAMES", _environment_value, _platform_all,
		"Show players by their shared Delta profile name in Delta games." },
	{ "display.show_quit_players", _config_boolean, "true", "HALO_SHOW_QUIT_PLAYERS", _environment_value,
		_platform_all | _platform_xbox,
		"Keep players who quit on the multiplayer scoreboard." },
	{ "display.scoreboard_background_color", _config_string, "\"16, 16, 16, 150\"", "HALO_SCOREBOARD_BACKGROUND_COLOR",
		_environment_value, _platform_all | _platform_xbox,
		"The scoreboard panel's colour: \"red, green, blue, alpha\", each 0 to 255." },
	{ "display.pause_change_teams", _config_boolean, "true", "HALO_PAUSE_CHANGE_TEAMS", _environment_value,
		_platform_all,
		"Show CHANGE TEAMS in the pause menu of a team game." },
#ifndef HALO_CONSOLE_NO_CONTROLS
	{ "display.pause_controls", _config_boolean, "true", "HALO_PAUSE_CONTROLS", _environment_value, _platform_all,
		"Show CONTROLS in the Xbox and Gearbox menus' pause menu." },
#endif
	{ "display.server_messages", _config_boolean, "true", "HALO_SERVER_MESSAGES", _environment_value, _platform_all | _platform_xbox,
		"Show the banner for a Delta server's warnings and notices." },
	{ "display.server_message_text_color", _config_string, "\"\"", "HALO_SERVER_MESSAGE_TEXT_COLOR",
		_environment_value, _platform_all,
		"The banner's text colour, \"#RRGGBB\" or \"#RRGGBBAA\". Empty: the default." },
	{ "display.server_message_border_color", _config_string, "\"\"", "HALO_SERVER_MESSAGE_BORDER_COLOR",
		_environment_value, _platform_all,
		"The banner's outline colour, \"#RRGGBB\" or \"#RRGGBBAA\". Empty: the\n"
		"default." },
	{ "display.server_message_background_color", _config_string, "\"\"", "HALO_SERVER_MESSAGE_BACKGROUND_COLOR",
		_environment_value, _platform_all,
		"The banner's background colour, \"#RRGGBB\" or \"#RRGGBBAA\". Empty: the\n"
		"default." },
	{ "display.latency_meter", _config_boolean, "false", "HALO_LATENCY_METER", _environment_value, _platform_all,
		"In a network game, your round trip to the host in milliseconds at the\n"
		"screen's top right, with bars (the host: its slowest player's), and\n"
		"\"Connection problem\" when the host has sent nothing for two seconds;\n"
		"the scoreboard's pings in the same colours, as this machine times them." },
	{ "display.shader_cache", _config_boolean, "true", "HALO_SHADER_CACHE", _environment_value, _platform_desktop,
		"Keep the GPU programs the game links under the save root (shader_cache),\n"
		"and load them from there next time instead of linking them again, which\n"
		"stalls a frame on some drivers; false links each every run." },
	{ "display.per_pixel_lighting", _config_boolean, "false", "HALO_PER_PIXEL_LIGHTING", _environment_value,
		_platform_all,
		"Light models for each pixel instead of each vertex." },
	{ "display.structure_culling", _config_integer, "31", "HALO_STRUCTURE_CULLING", _environment_value,
		_platform_all,
		"Which tests skip the level's triangles the camera cannot see, as bits\n"
		"added together: 1 outside the view, 2 groups facing away, 4 triangles\n"
		"facing away, 8 smaller groups, 16 nearer rooms first; 0 walks them as\n"
		"the Xbox does. The console's structure_culling changes it until the\n"
		"game quits." },
	{ "display.model_detail", _config_string, "\"stock\"", "HALO_MODEL_DETAIL", _environment_value, _platform_all,
		"Which of their levels of detail models are drawn at: \"stock\" chooses by\n"
		"their size on the Xbox's 480 lines, at any resolution; \"native\" by their\n"
		"size on the display's, so that more detail is kept at a distance at high\n"
		"resolutions; \"highest\" draws the models \"native\" does, each at its most\n"
		"detailed level." },
	{ "display.hdr", _config_boolean, "false", "HALO_HDR", _environment_value, _platform_all,
		"Draw the 3D view into a floating-point back buffer, which keeps what\n"
		"the game's additive effects (explosions, plasma, lights, the sun's\n"
		"glare) add past white, and tone map it before the HUD is drawn:\n"
		"display.hdr_tone_mapping brings it back to the display's range\n"
		"instead of clipping it. false draws into 8 bits a channel, as the\n"
		"Xbox does." },
	{ "display.hdr_exposure", _config_real, "1.0", "HALO_HDR_EXPOSURE", _environment_value, _platform_all,
		"With display.hdr, the 3D view's brightness in linear light before it\n"
		"is tone mapped, 0.25 to 4.0." },
	{ "display.hdr_tone_mapping", _config_string, "\"neutral\"", "HALO_HDR_TONE_MAPPING", _environment_value,
		_platform_all,
		"With display.hdr, the curve that brings what is past white into the\n"
		"display's range: \"neutral\" (the picture as drawn, with only its\n"
		"brightest parts rolled off), \"aces\" (filmic, more contrast) or\n"
		"\"reinhard\" (softer, and darker throughout)." },
	{ "display.hdr_output", _config_boolean, "false", "HALO_HDR_OUTPUT", _environment_value, _platform_desktop,
		"With display.hdr, ask for a floating-point window (scRGB), so that an\n"
		"HDR display shows what is past white brighter than the desktop's\n"
		"white rather than tone mapped down to it; the operating system's HDR\n"
		"must be on. Taken up when the game starts." },

	{ "audio.enabled", _config_boolean, "true", "HALO_NO_AUDIO", _environment_set_is_false, _platform_all,
		"Play sound." },
	{ "audio.volume", _config_real, "1.0", "HALO_VOLUME", _environment_value, _platform_all,
		"The volume of everything, 0.0 to 1.0." },
	{ "audio.music_volume", _config_real, "1.0", "HALO_MUSIC_VOLUME", _environment_value, _platform_all | _platform_xbox,
		"The music's volume, 0.0 to 1.0." },
	{ "audio.effects_volume", _config_real, "1.0", "HALO_EFFECTS_VOLUME", _environment_value, _platform_all | _platform_xbox,
		"The volume of effects and speech, 0.0 to 1.0." },
	{ "audio.buffer_frames", _config_integer, DEFAULT_AUDIO_BUFFER_FRAMES, "HALO_AUDIO_BUFFER_FRAMES", _environment_value,
		_platform_all,
		"The audio buffer in sample frames, 64 to 8192. Raise it if the sound\n"
		"cuts out; lower it for less delay." },
	{ "audio.channels", _config_string, "\"auto\"", "HALO_AUDIO_CHANNELS", _environment_value, _platform_desktop,
		"\"stereo\", \"5.1\" or \"7.1\" (surround, as the Xbox's Dolby Digital),\n"
		"or \"auto\": 5.1 or 7.1 when the sound device has 6 or 8 channels, else\n"
		"stereo. Read when the game starts." },
	{ "audio.doppler", _config_boolean, "true", "HALO_AUDIO_DOPPLER", _environment_value, _platform_all,
		"Shift the pitch of moving sounds (a Warthog or a projectile going past),\n"
		"as the Xbox did; false keeps them at their pitch." },
	{ "audio.hrtf", _config_string, "\"off\"", "HALO_AUDIO_HRTF", _environment_value, _platform_all,
		"Binaural 3D sound for headphones (HRTF): \"off\", \"on\", or \"auto\" for\n"
		"on when the output's name says it is headphones. Stereo output only." },

	{ "audio.reverb", _config_boolean, "true", "HALO_REVERB", _environment_value, _platform_all,
		"Echo sounds to match the place you are in." },
	{ "audio.resampler", _config_string, "\"sinc\"", "HALO_AUDIO_RESAMPLER", _environment_value, _platform_all,
		"How sounds are converted to the output's rate: \"sinc\" with a\n"
		"band-limited low pass; \"linear\" between neighbouring samples,\n"
		"brighter for the aliasing it lets through." },
	{ "audio.voice_chat", _config_string, "\"push_to_talk\"", "HALO_VOICE_CHAT", _environment_value, _platform_all,
		"Talking in network games' voice chat: \"push_to_talk\" (while\n"
		"controls.push_to_talk is held; the microphone opens the first time),\n"
		"\"open_mic\" (whenever the microphone hears speech), or \"off\". Others'\n"
		"voices play whatever this is (audio.voice_volume 0 silences them)." },
	{ "audio.voice_volume", _config_real, "1.0", "HALO_VOICE_VOLUME", _environment_value, _platform_all,
		"The volume of the other players' voices (0 to 2)." },
	{ "audio.output_device", _config_string, "\"default\"", "HALO_AUDIO_OUTPUT_DEVICE", _environment_value,
		_platform_desktop,
		"Where the game's sound and the voices play: a device's name as Settings >\n"
		"Audio lists it, or \"default\" for the system's (also if it is not found)." },
	{ "audio.input_device", _config_string, "\"default\"", "HALO_AUDIO_INPUT_DEVICE", _environment_value,
		_platform_desktop,
		"The microphone voice chat listens to: a device's name as Settings >\n"
		"Audio lists it, or \"default\" for the system's (also if it is not found)." },
	{ "audio.resampling", _config_string, "\"sinc\"", "HALO_AUDIO_RESAMPLING", _environment_value, _platform_all,
		"How sounds are converted to 48 kHz: \"sinc\" or \"linear\".\n"
		"audio.resampler (Sound Processing) is the same choice: either at\n"
		"\"linear\" resamples linearly." },

	{ "input.touch_controls", _config_string, "\"on\"", "HALO_TOUCH_CONTROLS", _environment_value, _platform_android | _platform_web,
		"On-screen touch controls: \"on\", \"auto\" (only with no controller\n"
		"connected) or \"off\"." },
	{ "input.touch_aim_assist", _config_boolean, "true", "HALO_TOUCH_AIM_ASSIST", _environment_value, _platform_android | _platform_web,
		"The touch controls' swipe aiming gets a controller's aim assist: the\n"
		"aim slows over a target and follows a moving one. false: none, as a\n"
		"mouse (the bullets' own autoaim stays)." },
	{ "input.mouse_sensitivity", _config_real, "1.0", "HALO_MOUSE_SENSITIVITY", _environment_value, _platform_all,
		"How fast the view turns with the mouse." },
	{ "input.playstation_kernel_driver", _config_boolean, "false", "HALO_PLAYSTATION_KERNEL_DRIVER",
		_environment_value, _platform_desktop,
		"Linux: a PlayStation controller (DualSense, DualShock 4) through the\n"
		"kernel's driver of it (hid-playstation, hid-sony), which needs no setup;\n"
		"false: through SDL's own, which needs Steam's udev rules to open it. Read\n"
		"at start-up." },
	{ "input.invert_mouse", _config_boolean, "false", "HALO_MOUSE_INVERT", _environment_set_is_true, _platform_all,
		"Moving the mouse forward looks down." },
	{ "input.keyboard_prompts", _config_boolean, "true", "HALO_KEYBOARD_PROMPTS", _environment_value, _platform_desktop,
		"Show keys in button prompts while using the keyboard and mouse." },
	{ "input.mouse_aim_assist", _config_boolean, "false", "HALO_MOUSE_AIM_ASSIST", _environment_value, _platform_all,
		"Aim assist for the mouse, as with a controller." },
	{ "input.mouse_vertical_sensitivity", _config_real, "0.0", "HALO_MOUSE_VERTICAL_SENSITIVITY", _environment_value,
		_platform_all,
		"How fast the view turns up and down with the mouse; 0 for the same as\n"
		"input.mouse_sensitivity." },
#ifdef HALO_SWITCH_CONSOLE
	/* gyro aiming (port/linux/src/xinput_sdl.c): the controller's gyroscope,
	each local player from their own (here with the other input settings, so
	that config.toml's input section is one) */
	{ "input.gyro_aim", _config_string, "\"false\"", "HALO_GYRO_AIM", _environment_value, _platform_android,
		"Aim with the controller's gyroscope, each local player from their own (a\n"
		"Pro Controller, the console held in the hands, or one Joy-Con; of two,\n"
		"the right one): \"true\" always, \"zoom\" only while zoomed in (a scope),\n"
		"\"false\" never. Gamepads, Gyro Aim, or here. Changes at once." },
	{ "input.gyro_sensitivity_horizontal", _config_real, "1.0", "HALO_GYRO_SENSITIVITY_HORIZONTAL",
		_environment_value, _platform_android,
		"How far the view turns left and right for the gyroscope, when\n"
		"input.gyro_aim is on. 1 is the sensor's own turn. 0.25 to 10." },
	{ "input.gyro_sensitivity_vertical", _config_real, "1.0", "HALO_GYRO_SENSITIVITY_VERTICAL",
		_environment_value, _platform_android,
		"How far the view turns up and down for the gyroscope, when\n"
		"input.gyro_aim is on. 1 is the sensor's own turn. 0.25 to 10." },
	{ "input.gyro_invert_horizontal", _config_boolean, "false", "HALO_GYRO_INVERT_HORIZONTAL",
		_environment_value, _platform_android,
		"Invert the gyroscope's yaw. false: turning the controller right looks\n"
		"right. true: turning it right looks left. The stick's invert is its own." },
	{ "input.gyro_invert_vertical", _config_boolean, "false", "HALO_GYRO_INVERT_VERTICAL",
		_environment_value, _platform_android,
		"Invert the gyroscope's pitch. false: tilting the controller up looks up.\n"
		"true: tilting it up looks down. The stick's invert is its own." },
#endif

	/* the keyboard and mouse's own controls (port/linux/src/xinput_sdl.c) */
	{ "chat.enabled", _config_boolean, "true", "HALO_CHAT", _environment_value, _platform_all | _platform_xbox,
		"Text chat with Chupathingy players. T talks to everyone, Y to your team." },
	{ "chat.show", _config_string, "\"all\"", "HALO_CHAT_SHOW", _environment_value, _platform_all | _platform_xbox,
		"Whose chat lines show: \"all\", \"team\" or \"off\"." },
	{ "chat.filter", _config_string, "\"mask\"", "HALO_CHAT_FILTER", _environment_value, _platform_all | _platform_xbox,
		"Blocked words in chat: \"mask\" stars them out, \"hide\" hides the line,\n"
		"\"show\" shows it." },
	{ "chat.filter_file", _config_string, "\"chat_filter.txt\"", "HALO_CHAT_FILTER_FILE", _environment_value,
		_platform_all,
		"Your own blocked words, one per line, in the save folder. \"whole <word>\"\n"
		"blocks only the whole word. Lines starting with # are comments." },
	{ "chat.streamer_mode", _config_boolean, "false", "HALO_CHAT_STREAMER_MODE", _environment_value, _platform_all | _platform_xbox,
		"Show other players in chat by team name, and always star out blocked\n"
		"words." },
	{ "chat.quick_messages", _config_string,
		"\"Nice shot|Need backup|On my way|Thanks|Good game|Sorry\"", "HALO_CHAT_QUICK_MESSAGES",
		_environment_value, _platform_all | _platform_xbox,
		"Controller quick messages, separated by |. D-pad up picks one, D-pad\n"
		"down sends it. Empty: none." },
	{ "controls.move_forward", _config_string, "\"W\"", "HALO_KEY_MOVE_FORWARD", _environment_value, _platform_all,
		"Keyboard and mouse controls (Settings > Controls Setup): up to two\n"
		"each, separated by a comma. Keys by their US keyboard names (\"W\",\n"
		"\"Space\", \"Left Ctrl\", \"F1\"). Mouse: \"Mouse Left\", \"Mouse Right\",\n"
		"\"Mouse Middle\", \"Mouse 4\", \"Mouse 5\", \"Wheel\", \"Wheel Up\", \"Wheel Down\".\n"
		"Empty: none. Move forward:" },
	{ "controls.move_backward", _config_string, "\"S\"", "HALO_KEY_MOVE_BACKWARD", _environment_value, _platform_all,
		"Move backward." },
	{ "controls.strafe_left", _config_string, "\"A\"", "HALO_KEY_STRAFE_LEFT", _environment_value, _platform_all,
		"Move left." },
	{ "controls.strafe_right", _config_string, "\"D\"", "HALO_KEY_STRAFE_RIGHT", _environment_value, _platform_all,
		"Move right." },
	{ "controls.jump", _config_string, "\"Space\"", "HALO_KEY_JUMP", _environment_value, _platform_all,
		"Jump and skip cutscenes." },
#ifdef HALO_BROWSER
	/* (in a browser, Ctrl makes Ctrl+W and the like a slip of the finger
	away from closing the tab) */
	{ "controls.crouch", _config_string, "\"C\"", "HALO_KEY_CROUCH", _environment_value, _platform_all,
		"Crouch." },
#else
	{ "controls.crouch", _config_string, CROUCH_KEYS, "HALO_KEY_CROUCH", _environment_value, _platform_all,
		"Crouch." },
#endif
	{ "controls.fire", _config_string, "\"Mouse Left\"", "HALO_KEY_FIRE", _environment_value, _platform_all,
		"Fire." },
	{ "controls.throw_grenade", _config_string, "\"Mouse Right, G\"", "HALO_KEY_THROW_GRENADE", _environment_value,
		_platform_all,
		"Throw grenade." },
	{ "controls.melee", _config_string, "\"F, Mouse 4\"", "HALO_KEY_MELEE", _environment_value, _platform_all,
		"Melee attack." },
	{ "controls.reload", _config_string, "\"R\"", "HALO_KEY_RELOAD", _environment_value, _platform_all,
		"Reload." },
	{ "controls.zoom", _config_string, "\"Z, Mouse Middle\"", "HALO_KEY_ZOOM", _environment_value, _platform_all,
		"Zoom." },
	{ "controls.switch_weapon", _config_string, "\"Wheel, 1\"", "HALO_KEY_SWITCH_WEAPON", _environment_value,
		_platform_all,
		"Switch weapon." },
	{ "controls.switch_grenade", _config_string, "\"X\"", "HALO_KEY_SWITCH_GRENADE", _environment_value, _platform_all,
		"Switch grenade." },
	{ "controls.action", _config_string, "\"E\"", "HALO_KEY_ACTION", _environment_value, _platform_all,
		"Action: pick up, swap weapons, enter and exit vehicles, press switches." },
	{ "controls.flashlight", _config_string, "\"Q\"", "HALO_KEY_FLASHLIGHT", _environment_value, _platform_all,
		"Flashlight." },
	{ "controls.scoreboard", _config_string, "\"Tab\"", "HALO_KEY_SCOREBOARD", _environment_value, _platform_all,
		"Show scores." },
	{ "controls.pause", _config_string, "\"Escape\"", "HALO_KEY_PAUSE", _environment_value, _platform_all,
		"Pause menu." },
	{ "controls.screenshot", _config_string, "\"F10\"", "HALO_KEY_SCREENSHOT", _environment_value, _platform_all,
		"Save a PNG screenshot beside maps/ (press once per capture)." },
	{ "controls.push_to_talk", _config_string, "\"V\"", "HALO_KEY_PUSH_TO_TALK", _environment_value, _platform_all,
		"Voice chat: talk while it is held (audio.voice_chat \"push_to_talk\")." },
	{ "controls.chat_all", _config_string, "\"T\"", "HALO_KEY_CHAT_ALL", _environment_value, _platform_all,
		"Chat to everyone." },
	{ "controls.chat_team", _config_string, "\"Y\"", "HALO_KEY_CHAT_TEAM", _environment_value, _platform_all,
		"Chat to your team." },
	{ "controls.chat_log", _config_string, "\"H\"", "HALO_KEY_CHAT_LOG", _environment_value, _platform_all,
		"Chat history and player list (M mutes a player)." },
	/* (port, this fork: OpenCE's text chat's keys, none by default:
	ChupathingyCE's chat, controls.chat_all and chat_team, is the one played,
	and OpenCE players' lines reach it) */
	{ "controls.chat", _config_string, "\"\"", "HALO_KEY_CHAT", _environment_value, _platform_all,
		"OpenCE's text chat: type a line to everyone (Enter sends it, Escape lets\n"
		"it go). None by default: controls.chat_all is the game's chat, which\n"
		"OpenCE players' lines reach." },
	{ "controls.team_chat", _config_string, "\"\"", "HALO_KEY_TEAM_CHAT", _environment_value, _platform_all,
		"OpenCE's text chat: type a line to your team. None by default\n"
		"(controls.chat_team)." },
#ifndef HALO_CONSOLE_NO_CONTROLS
	{ "controls.controller_layout", _config_string, "\"\"", "HALO_CONTROLLER_LAYOUT", _environment_value,
		_platform_all,
		"The Custom button layout, set on the Controls screen: \"action=button\"\n"
		"pairs separated by commas (\"jump=B,melee=RB\"). Empty: the default." },
#endif

	{ "game.enhanced_animations", _config_boolean, "true", "HALO_ENHANCED_ANIMATIONS", _environment_value, _platform_all,
		"The player bipeds' grenade throws keep their legs moving (crouched,\n"
		"in the air and in a vehicle's seat too), riders' hands leave the grips\n"
		"to throw and reload, and a player turns with the aim while throwing;\n"
		"false: the original animations, which freeze the legs and stand a\n"
		"rider up." },
	{ "game.console_log", _config_string, "\"none\"", "HALO_CONSOLE_LOG", _environment_value, _platform_all | _platform_xbox,
		"What the console shows on screen: \"important\", \"all\" or \"none\".\n"
		"debug.txt has every line." },

	{ "game.language", _config_string, "\"\"", "HALO_LANGUAGE", _environment_value, _platform_all,
		"The game's language: \"ja\", \"de\", \"fr\", \"es\" or \"it\"; empty for English." },

	{ "game.unlock_all_levels", _config_boolean, "false", "HALO_UNLOCK_ALL_LEVELS", _environment_value, _platform_all | _platform_xbox,
		"New profiles start with every campaign level unlocked." },

	{ "game.start_with_one_player", _config_boolean, "false", "HALO_START_WITH_ONE_PLAYER", _environment_value,
		_platform_all | _platform_xbox,
		"Let a game you host start with only you. Others can join in progress." },

	{ "game.downloaded_maps", _config_string, "\"\"", "HALO_DOWNLOADED_MAPS", _environment_value, _platform_all,
		"Maps also treated as downloaded (a map the game downloads is marked so\n"
		"by itself), separated by commas (bloodgulch, hugeass@ce), or \"*\" for\n"
		"all. Their scripts can't change your settings or other players' games." },

	{ "game.pack_menus", _config_string, "\"\"", "HALO_PACK_MENUS", _environment_value, _platform_all | _platform_xbox,
		"Play with a map pack's own menus, by the pack's folder name: \"cex\" uses\n"
		"maps_cex/ui.map (Halo CE+'s) in place of the game's; \"\" (the default)\n"
		"keeps the game's. Takes effect after a restart." },

	{ "game.bomb_spawns", _config_string, "\"\"", "HALO_BOMB_SPAWNS", _environment_value, _platform_all,
		"Where a Bomb or Grifball game you host puts the bomb on a map, for maps\n"
		"whose spot halfway between the goals plays badly: \"<map> <x> <y> [<z>]\"\n"
		"(the map as a playlist names it: bloodgulch, timberland@ce), entries\n"
		"separated by commas. The bomb rests on the first ground under the point\n"
		"(from a little over its height, when given). A map not named has the\n"
		"game's own spot: Blood Gulch's halfway between its teleporters' exits,\n"
		"any other map's halfway between the goals." },
	{ "game.bomb_arm_time", _config_real, "1.0", "HALO_BOMB_ARM_TIME", _environment_value, _platform_all,
		"In a Bomb or Grifball game you host, the seconds the bomb arms once it\n"
		"is planted in the enemy goal (up to 10): a defender who reaches it in\n"
		"that time saves it (it resets, no score); else it goes off and scores.\n"
		"0: it scores the moment it is planted." },

	{ "game.bomb_open_pitch_speed", _config_integer, "0", "HALO_BOMB_OPEN_PITCH_SPEED", _environment_value,
		_platform_all,
		"Everyone's speed in a Grifball: Open Pitch game you host, in percent\n"
		"(50 to 200; the carrier's 115% on top stops at 200). 0: the game\n"
		"type's own, 175." },

	{ "game.bomb_open_pitch_carrier_health", _config_integer, "0", "HALO_BOMB_OPEN_PITCH_CARRIER_HEALTH",
		_environment_value, _platform_all,
		"The bomb carrier's health in a Grifball: Open Pitch game you host, in\n"
		"percent of everyone's (10 to 400). 0: the game type's own, 400 (with\n"
		"the resistance's 400, it lives through one melee blow, not two)." },
	{ "game.bomb_open_pitch_carrier_resistance", _config_integer, "0",
		"HALO_BOMB_OPEN_PITCH_CARRIER_RESISTANCE", _environment_value, _platform_all,
		"The bomb carrier's damage resistance in a Grifball: Open Pitch game you\n"
		"host, in percent (25 to 400; 150 takes two thirds of the damage). 0: the\n"
		"game type's own, 400." },
	{ "game.bomb_open_pitch_carrier_shields", _config_string, "\"none\"", "HALO_BOMB_OPEN_PITCH_CARRIER_SHIELDS",
		_environment_value, _platform_all,
		"The bomb carrier's shields in a Grifball: Open Pitch game you host:\n"
		"\"none\" (everyone's: none), \"normal\" or \"overshield\"." },
	{ "game.bomb_open_pitch_fumble", _config_boolean, "true", "HALO_BOMB_OPEN_PITCH_FUMBLE", _environment_value,
		_platform_all,
		"true: in a Grifball: Open Pitch game you host, a melee blow that doesn't\n"
		"kill the bomb carrier knocks the bomb out of their hands." },

	{ "game.move_old_map_folders", _config_string, "\"ask\"", "HALO_MOVE_OLD_MAP_FOLDERS", _environment_value,
		_platform_all,
		"Move maps from the old maps/ce and md_maps folders to maps_ce, maps_md\n"
		"and maps_pc: \"ask\", \"yes\" or \"no\"." },
	{ "game.custom_edition", _config_boolean, "true", "HALO_CUSTOM_EDITION", _environment_value, _platform_all,
		"Play Halo PC maps (not those that need OpenSauce): Custom Edition's in\n"
		"maps_ce (with Custom Edition's bitmaps.map, sounds.map and loc.map),\n"
		"OpenCE's custom_maps or paths.custom_edition, HaloMD's in maps_md and\n"
		"Halo PC's in maps_pc. Their tags are checked as the game's own maps'\n"
		"are before they run. false: none of them is listed, played or\n"
		"downloaded (OpenCE's setting)." },

	{ "game.defaults_answered", _config_integer, "0", "HALO_DEFAULTS_ANSWERED", _environment_value, _platform_all,
		"Set by the game after it asks about changed defaults. Do not edit." },
	{ "bots.count", _config_integer, "0", "HALO_BOTS", _environment_value, _platform_all,
		"Computer players (bots) in a local (split screen) Slayer game: 0 to 15\n"
		"join its lobby with the players, as far as the game has room. Not in\n"
		"system link or internet games. 0: none, as in the stock game." },
	{ "bots.skill", _config_string, "\"normal\"", "HALO_BOT_SKILL", _environment_value, _platform_all,
		"How well the bots play: \"easy\", \"normal\", \"heroic\" or \"legendary\"\n"
		"(how soon they react, how well they aim, how far they see)." },
	{ "bots.teams", _config_string, "\"even\"", "HALO_BOT_TEAMS", _environment_value, _platform_all,
		"In team games: \"even\" puts the bots on both teams, evening them with\n"
		"the players; \"against\" puts them all on the team the players are not on." },

	{ "paths.data", _config_string, "\"\"", "HALO_DATA_ROOT", _environment_value, _platform_desktop,
		"The folder holding the maps folder; empty for the working folder and\n"
		"its assets folder. Use single quotes for Windows paths: 'C:\\Games\\Halo'." },
	{ "paths.saves", _config_string, "\"\"", "HALO_SAVE_ROOT", _environment_value, _platform_desktop,
		"Where saved games and profiles go; empty for the usual place\n"
		"(~/.local/share/fractum-halo, or %APPDATA%\\FractumHalo on Windows)." },
	{ "paths.custom_edition", _config_string, "\"\"", "HALO_CUSTOM_EDITION_ROOT", _environment_value, _platform_desktop,
		"A folder of Custom Edition maps, or a Halo Custom Edition install\n"
		"(its maps folder), looked in after maps_ce and custom_maps for\n"
		"Custom Edition maps and their bitmaps.map, sounds.map and loc.map\n"
		"(game.custom_edition); empty for none (OpenCE's setting)." },
	{ "paths.logs", _config_string, "\"\"", "HALO_LOG_ROOT", _environment_value, _platform_desktop,
		"Where logs, crash reports, bans.txt and cheaters.txt go; empty for\n"
		"logs/ beside maps (bans.txt and cheaters.txt then in the data folder).\n"
		"Lets the data folder be read-only (a dedicated server's maps mounted\n"
		"so)." },
	{ "paths.cache", _config_string, "\"\"", "HALO_CACHE_ROOT", _environment_value, _platform_desktop,
		"Where the game keeps its cache; empty for cache/ in the save folder." },
	{ "paths.custom_maps", _config_string, "\"\"", "HALO_CUSTOM_MAPS_ROOT", _environment_value, _platform_desktop,
		"The custom_maps folder (Custom Edition maps, and their bitmaps.map,\n"
		"sounds.map and loc.map); empty for the one in the data folder." },

	{ "data.download_url", _config_string, "\"https://halo.fractumseraph.net/assets/maps/\"", "HALO_DATA_DOWNLOAD_URL",
		_environment_value, _platform_desktop,
		"Where the first start offers to download the maps folder from, when\n"
		"there is none: an https:// folder holding its files and maps.txt (the\n"
		"files' names and sizes, a line each; services/selfhost's\n"
		"assets/maps/). Empty offers only an Xbox disc image." },
	{ "network.address", _config_string, "\"\"", "HALO_NET_ADDRESS", _environment_value, _platform_all,
		"This machine's IPv4 address for system link; empty chooses one." },
	{ "network.broadcast", _config_string, "\"\"", "HALO_NET_BROADCAST", _environment_value, _platform_all,
		"IPv4 addresses for system link announcements, separated by commas\n"
		"(for VPNs); empty for the local network." },
	{ "network.online", _config_boolean, "true", "HALO_NET_ONLINE", _environment_value, _platform_all | _platform_xbox,
		"Host and join games over the internet by invite links. false: local\n"
		"network only." },
	{ "network.lasting_invite", _config_boolean, "false", "HALO_NET_LASTING_INVITE", _environment_value,
		_platform_all,
		"Hosting on the internet: keep the same invite link from one run of the\n"
		"game to the next (a dedicated server's, to post somewhere), its key and\n"
		"token kept in lasting_invite.key in the save folder, which only you can\n"
		"read. Anyone with the link can always use it; a new password or going\n"
		"private still makes a new one, which is then kept. Copy that file to\n"
		"one machine alone." },
	{ "network.shared_bans", _config_string, "\"\"", "HALO_NET_SHARED_BANS", _environment_value, _platform_all,
		"Hosting: ban lists other hosts publish, taken as this host's own bans:\n"
		"their https:// addresses, with commas between them (FractumSeraph's\n"
		"servers' is https://halovps.fractumseraph.net/bans/shared.txt). Fetched\n"
		"at start and every 10 minutes into shared_bans.txt beside bans.txt; a\n"
		"machine whose hardware id (or address) is in one is refused. Name only\n"
		"lists whose publishers you trust. Empty: none." },
	{ "network.fair_play", _config_string, "\"refuse\"", "HALO_NET_FAIR_PLAY", _environment_value, _platform_all,
		"Hosting: check players' shots for cheats. \"refuse\" refuses a bullet's\n"
		"hit through a solid wall or far from where its shooter aimed (silent\n"
		"aim), and logs those, near misses of the checks, fast snaps of the aim\n"
		"and aim held on enemies behind walls (wallhacks) in fairplay.txt beside\n"
		"bans.txt; \"log\" only logs; \"off\" checks nothing. A dedicated\n"
		"server lists what it found with sv_suspects. Nothing is banned by\n"
		"itself." },
	{ "network.record_games", _config_boolean, "false", "HALO_NET_RECORD_GAMES", _environment_value, _platform_all,
		"Hosting: record every game to recordings\\ in the data folder (each\n"
		"player's position, aim, health and weapon every tick, their names,\n"
		"addresses and hardware ids, the fair play checks' verdicts, the kills and\n"
		"the map's walls), about 40 MB an hour, to watch again through any\n"
		"player's eyes at halo.fractumseraph.net/watch/. Keep the folder\n"
		"private." },
	{ "network.recordings_megabytes", _config_integer, "2048", "HALO_NET_RECORDINGS_MEGABYTES", _environment_value,
		_platform_all,
		"How many megabytes of recordings (network.record_games) to keep: the\n"
		"oldest go first. 0 keeps them all." },
	{ "network.record_public", _config_boolean, "false", "HALO_NET_RECORD_PUBLIC", _environment_value, _platform_all,
		"Hosting, with network.record_games: also write each game's public copy\n"
		"to recordings\\public, for anyone to watch, live or again: no player's\n"
		"address or hardware id and no fair play verdicts, and the game as it is\n"
		"played held back network.record_public_delay seconds, so that no one\n"
		"watching can tell its players where the others are. recordings\\public\\\n"
		"live.json says which game is being written. Serve that folder alone." },
	{ "network.record_public_delay", _config_integer, "30", "HALO_NET_RECORD_PUBLIC_DELAY", _environment_value,
		_platform_all,
		"How many seconds the public copy (network.record_public) is held back\n"
		"while its game is played." },
	{ "network.record_public_megabytes", _config_integer, "2048", "HALO_NET_RECORD_PUBLIC_MEGABYTES",
		_environment_value, _platform_all,
		"How many megabytes of public copies (network.record_public) to keep:\n"
		"the oldest go first. 0 keeps them all." },
	{ "network.join_from_clipboard", _config_string, "\"never\"", "HALO_NET_JOIN_FROM_CLIPBOARD", _environment_value,
		_platform_all,
		"An invite link on the clipboard when the game comes to the front:\n"
		"\"ask\", \"always\" (join it) or \"never\"." },
	{ "network.copy_to_clipboard", _config_boolean, "false", "HALO_NET_COPY_TO_CLIPBOARD", _environment_value,
		_platform_all,
		"Put a game's invite link on the clipboard when you start to host it.\n"
		"false: the log has it, and the Gearbox menus' Server Settings copy it." },
	{ "network.map_downloads", _config_string, "\"ask\"", "HALO_NET_MAP_DOWNLOADS", _environment_value,
		_platform_all,
		"A Custom Edition map a game you join needs and you don't have: \"ask\"\n"
		"offers to download it, \"always\" downloads it without asking, \"never\"\n"
		"only says which map is missing. It comes from the host if the host\n"
		"shares it (network.map_share), else over BitTorrent (other players, the\n"
		"trackers and web seeds: maps.torrents), else from the HaloNet and\n"
		"HaloMaps archives (maps.archives), else from the community map\n"
		"repositories (maps.halonet.net and its mirrors, as Chimera downloads\n"
		"from). A downloaded map is checked (against the index's info hash, or\n"
		"its size, CRC-32, header, name and the host's version) before it goes\n"
		"in maps_ce." },
	{ "network.playlist_downloads", _config_string, "\"ask\"", "HALO_NET_PLAYLIST_DOWNLOADS", _environment_value,
		_platform_all,
		"Offer a playlist's missing maps from its verified map set when you join\n"
		"one of its games: \"ask\", \"always\" or \"never\"." },
	{ "network.tunnel_port", _config_integer, "0", "HALO_NET_TUNNEL_PORT", _environment_value, _platform_all | _platform_xbox,
		"The UDP port for internet play; 0 picks one." },
	{ "network.client_timeout_seconds", _config_integer, "15", "HALO_NET_CLIENT_TIMEOUT_SECONDS", _environment_value,
		_platform_all,
		"Seconds a host waits for a machine it hears nothing from before it\n"
		"drops it (5 to 300; the stock game's 15). Longer keeps players whose\n"
		"connection stalls (a browser tab in the background) at the cost of\n"
		"frozen players for longer when one really goes." },
	{ "network.allow_upnp", _config_boolean, "true", "HALO_NET_ALLOW_UPNP", _environment_value, _platform_all,
		"Ask the router to forward internet play's port (UPnP)." },
	{ "network.protocol", _config_string, "\"auto\"", "HALO_NET_PROTOCOL", _environment_value, _platform_all,
		"Talk to other Chupathingy machines: \"auto\" (Delta where it can,\n"
		"OpenCE otherwise), \"opence\" (OpenCE only) or \"delta\" (as \"auto\" for\n"
		"now)." },
	{ "network.chat", _config_string, "\"on\"", "HALO_NET_CHAT", _environment_value, _platform_all,
		"Text chat in games you host: \"on\", \"team\" or \"off\"." },
	{ "network.team_change", _config_string, "\"balanced\"", "HALO_NET_TEAM_CHANGE", _environment_value,
		_platform_all,
		"Team changes in team games you host: \"balanced\", \"allow\" or \"off\"." },
	{ "network.team_change_margin", _config_integer, "1", "HALO_NET_TEAM_CHANGE_MARGIN", _environment_value,
		_platform_all,
		"For \"balanced\": how many more players one team may have, 0 to 16." },
	{ "network.chat_filter", _config_boolean, "true", "HALO_NET_CHAT_FILTER", _environment_value, _platform_all,
		"In games you host, block chat lines with blocked words." },
	{ "network.chat_legacy", _config_boolean, "true", "HALO_NET_CHAT_LEGACY", _environment_value, _platform_all,
		"In games you host, let OpenCE players chat too (over their own\n"
		"text chat, bridged), while network.chat is on." },
	{ "network.share_profile", _config_boolean, "false", "HALO_NET_SHARE_PROFILE", _environment_value,
		_platform_all,
		"Show your Chupathingy player ID to other Chupathingy players over\n"
		"Delta." },
	{ "network.platform_limits", _config_string, "\"on\"", "HALO_NET_PLATFORM_LIMITS", _environment_value,
		_platform_all,
		"Delta hosts keep games to this platform's player limit: \"on\" or \"off\"." },
	{ "network.host_platform_limits", _config_boolean, "true", "HALO_NET_HOST_PLATFORM_LIMITS", _environment_value,
		_platform_all,
		"Games you host keep to their Delta players' platform limits. false is\n"
		"for testing." },
	{ "network.public_lobby", _config_boolean, "true", "HALO_NET_PUBLIC_LOBBY", _environment_value, _platform_all,
		"List public games in the Server Browser and show others' games there." },
	{ "network.ping_games", _config_boolean, "true", "HALO_NET_PING_GAMES", _environment_value, _platform_all,
		"Ping the hosts listed in Online Games. false shows estimated pings\n"
		"only." },
	{ "network.host_public", _config_boolean, "true", "HALO_NET_HOST_PUBLIC", _environment_value, _platform_all,
		"New internet games start as PUBLIC (true) or PRIVATE (false)." },
	{ "network.voice_lobby", _config_boolean, "true", "HALO_NET_VOICE_LOBBY", _environment_value, _platform_all,
		"Hosting: voice chat in the lobby, before and after a game, where every\n"
		"player hears every other." },
	{ "network.voice_mode", _config_string, "\"team_global_enemy_proximity\"", "HALO_NET_VOICE_MODE",
		_environment_value, _platform_all,
		"Hosting: voice chat in a game: \"off\"; \"team_proximity\" (teammates\n"
		"near); \"team_enemy_proximity\" (anyone near); \"team_global\" (all\n"
		"teammates); or \"team_global_enemy_proximity\" (all teammates, and\n"
		"enemies near). A game without teams has only enemies; co-op only\n"
		"teammates." },
	{ "network.voice_kbps", _config_integer, "24", "HALO_NET_VOICE_KBPS", _environment_value, _platform_all,
		"Hosting: the voices' quality, in the lobby and in a game, in kilobits a\n"
		"second (8 to 64)." },
	{ "network.voice_proximity", _config_real, "15.0", "HALO_NET_VOICE_PROXIMITY", _environment_value,
		_platform_all,
		"Hosting: how near (world units: 1 is about 3 metres) a player must be\n"
		"to be heard by proximity voice chat (5 to 100)." },
	{ "network.text_chat", _config_string, "\"on\"", "HALO_NET_TEXT_CHAT", _environment_value, _platform_all,
		"Hosting: the text chat: \"on\" (links in lines are refused), \"links\"\n"
		"(links allowed) or \"off\", in either case. Always off in a game a\n"
		"player hosts that is listed in the server browser (a public internet\n"
		"game); a dedicated server's follows this setting, listed or not." },
	{ "network.votekick", _config_boolean, "true", "HALO_NET_VOTEKICK", _environment_value, _platform_all,
		"Hosting: let the players vote to kick a player (the scoreboard's\n"
		"right-click, or the console's votekick). More than half of the players\n"
		"must vote, counted once per address." },
	{ "network.votekick_minutes", _config_integer, "5", "HALO_NET_VOTEKICK_MINUTES", _environment_value,
		_platform_all,
		"Hosting: the minutes a player must have played on this server to start\n"
		"a vote to kick (0 to 60); to vote, 2 minutes or this, the less." },
	{ "network.votekick_ban_minutes", _config_integer, "30", "HALO_NET_VOTEKICK_BAN_MINUTES", _environment_value,
		_platform_all,
		"Hosting: the minutes a player kicked by a vote cannot join again (1 to\n"
		"1440)." },
	{ "network.map_share", _config_boolean, "true", "HALO_NET_MAP_SHARE", _environment_value, _platform_all,
		"Hosting: send a player who joins without the game's Custom Edition map\n"
		"the map (from maps_ce, custom_maps...), and the resource maps it needs\n"
		"that the player lacks (bitmaps.map, sounds.map, loc.map). The game's\n"
		"other players help (network.map_seed)." },
	{ "network.map_share_upload", _config_integer, "1024", "HALO_NET_MAP_SHARE_UPLOAD", _environment_value,
		_platform_all,
		"Hosting: the most kilobytes a second the host sends of maps (16 to\n"
		"20480: 20 MB/s), so that the game's own traffic keeps the rest of the\n"
		"upload." },
	{ "network.map_seed", _config_boolean, "true", "HALO_NET_MAP_SEED", _environment_value, _platform_all,
		"Playing in another's game: help send its map to players who join\n"
		"without it, as a torrent's peers do (the pieces this machine has, at\n"
		"most network.map_seed_upload)." },
	{ "network.map_seed_upload", _config_integer, "256", "HALO_NET_MAP_SEED_UPLOAD", _environment_value,
		_platform_all,
		"Playing in another's game: the most kilobytes a second this machine\n"
		"sends of maps to other players (16 to 10240: 10 MB/s)." },
	{ "network.map_download", _config_boolean, "true", "HALO_NET_MAP_DOWNLOAD", _environment_value, _platform_all,
		"Joining a game on a Custom Edition map this machine does not have:\n"
		"download it (and the resource maps it needs) from the game's players\n"
		"into custom_maps, if its host shares it (network.map_share); false\n"
		"leaves the game, saying which map is missing." },
	{ "network.coop_public", _config_boolean, "false", "HALO_NET_COOP_PUBLIC", _environment_value, _platform_all,
		"New online co-op games start as PUBLIC (true) or PRIVATE (false)." },
	{ "network.coop_friendly_fire", _config_string, "\"on\"", "HALO_NET_COOP_FRIENDLY_FIRE", _environment_value,
		_platform_all,
		"Friendly fire in online co-op: \"off\", \"on\", \"shields_only\" or\n"
		"\"explosives_only\" (FRIENDLY FIRE in co-op's Server Setup > Co-op\n"
		"Options writes its choice here). Their AI allies they hurt as\n"
		"network.coop_ally_friendly_fire says." },
	{ "network.coop_ally_friendly_fire", _config_boolean, "true", "HALO_NET_COOP_ALLY_FRIENDLY_FIRE",
		_environment_value, _platform_all,
		"Whether the players of an online co-op game hurt their AI allies (the\n"
		"Marines, Captain Keyes, the Sentinels while they are allies), as in the\n"
		"campaign; false, the players' weapons, grenades and vehicles do them\n"
		"no harm. ALLY FRIENDLY FIRE in co-op's Server Setup > Co-op Options\n"
		"writes its choice here." },
	{ "network.block_mouse_aim_assist", _config_boolean, "false", "HALO_NET_BLOCK_MOUSE_AIM_ASSIST", _environment_value,
		_platform_all,
		"Turn off mouse aim assist for everyone in games you host." },
	{ "network.shield_effects", _config_string, "\"stock\"", "HALO_NET_SHIELD_EFFECTS", _environment_value, _platform_all,
		"Host: the shield effects every player of a network game sees, whatever\n"
		"their own: \"stock\", \"glow\", \"light_show\", \"overshield\",\n"
		"\"glow_overshield\" or \"light_show_overshield\" (display.shield_glow,\n"
		"its light_show intensity, display.overshield_effect); \"players\": not\n"
		"synced, each player sees their own settings'. SHIELD EFFECTS in Server\n"
		"Setup > Player Options writes its choice here." },
	{ "network.fire_origins", _config_boolean, "true", "HALO_NET_FIRE_ORIGINS", _environment_value, _platform_all,
		"Host: whether a reported hit the shooter's camera had no line of sight\n"
		"to may still count from where their machine says the shot left, when\n"
		"that is not the camera (a VR build's hand: past a corner the camera is\n"
		"behind), no further than a player leaning out and an arm, and in front\n"
		"of the eye. false: from the camera and vehicles' guns only." },
	{ "network.coop_own_friendly_fire", _config_boolean, "true", "HALO_NET_COOP_OWN_FRIENDLY_FIRE",
		_environment_value, _platform_all,
		"Whether this machine's players hurt, and are hurt by, the other players\n"
		"of an online co-op game: two players hurt each other only if both have\n"
		"it true. FRIENDLY FIRE in that game's pause menu writes its choice\n"
		"here. A player away from the controls for over 70 seconds is hurt by\n"
		"every player, whatever either chose." },
	{ "network.coop_player_collisions", _config_boolean, "true", "HALO_NET_COOP_PLAYER_COLLISIONS", _environment_value,
		_platform_all,
		"Players bump into each other in online co-op." },
	{ "network.coop_enemies_mode", _config_string, "\"per_player\"", "HALO_NET_COOP_ENEMIES_MODE", _environment_value,
		_platform_all,
		"Extra enemies in online co-op: \"none\", \"per_player\"\n"
		"(network.coop_enemies) or \"multiplier\"\n"
		"(network.coop_enemies_multiplier)." },
	{ "network.coop_pickups", _config_string, "\"none\"", "HALO_NET_COOP_PICKUPS", _environment_value, _platform_all,
		"Extra campaign weapons, grenades, health kits, overshields and camouflage.\n"
		"Modes: \"none\", \"per_player\" (coop_enemies percent per extra player),\n"
		"or \"multiplier\" (coop_enemies_multiplier times as many), up to 8 times\n"
		"the original pickups. Only safe nearby places receive copies." },
	{ "network.coop_vehicles", _config_string, "\"none\"", "HALO_NET_COOP_VEHICLES", _environment_value, _platform_all,
		"Extra campaign vehicles: \"none\", \"per_player\" or \"multiplier\".\n"
		"After a player clears its unloaded starting place by both bounding\n"
		"radii plus 15 percent, make one more there, up to\n"
		"floor((players - 1) * coop_enemies * 2 / 300) extras, or floor((coop_enemies_multiplier - 1) * 2 / 3) in static mode.\n"
		"The host alone creates them, and waits while the starting place is blocked." },
	{ "network.coop_enemies", _config_integer, "50", "HALO_NET_COOP_ENEMIES", _environment_value, _platform_all,
		"Extra enemies for each co-op player past the first, in percent, 25\n"
		"to 200." },
	{ "network.coop_enemies_multiplier", _config_integer, "2", "HALO_NET_COOP_ENEMIES_MULTIPLIER", _environment_value,
		_platform_all,
		"How many times larger each co-op enemy squad is, 2 to 32. Extra\n"
		"pickups stop at 8 times; extra vehicles grow by two thirds of one less\n"
		"than this, rounded down." },
	{ "network.brokers_file", _config_string, "\"brokers.txt\"",
		"HALO_NET_BROKERS_FILE", _environment_value, _platform_all | _platform_xbox,
		"The file of MQTT brokers that connect invites, one host:port per line,\n"
		"up to 4. Updates replace brokers.txt, so keep your own under another\n"
		"name." },
	{ "network.public_address", _config_string, "\"\"", "HALO_NET_PUBLIC_ADDRESS", _environment_value,
		_platform_desktop,
		"Hosting: the internet address joiners reach this machine at, as\n"
		"host[:port] (the port: the one forwarded to network.tunnel_port), for a\n"
		"server behind a NAT with one forwarded port; offered first, its name\n"
		"looked up again every 5 minutes. Empty: what STUN and UPnP find." },
	{ "network.listings", _config_string, "\"both\"", "HALO_NET_LISTINGS", _environment_value, _platform_all,
		"What the server browser lists: \"both\" (dedicated servers from the\n"
		"master servers first, then the games players host, from the brokers),\n"
		"\"masters\" (the master servers' alone) or \"brokers\" (the brokers'\n"
		"alone)." },
	{ "network.masters_file", _config_string, "\"masters.txt\"", "HALO_NET_MASTERS_FILE", _environment_value,
		_platform_all,
		"The file of the master servers the server browser asks for dedicated\n"
		"servers, beside this file unless a full path: an https:// URL and its\n"
		"key (ed25519:<hex>) on each line, up to 4. None named (no file, or\n"
		"comments alone): the official master, built in." },
	{ "network.browser_source", _config_string, "\"all\"", "HALO_NET_BROWSER_SOURCE", _environment_value,
		_platform_all,
		"The server browser's games: \"all\" (ALL GAMES: the dedicated\n"
		"servers first, then the games players host), \"dedicated\" (the\n"
		"master servers' dedicated servers) or \"players\" (P2P GAMES: games\n"
		"players host). Its top-left tab changes it." },
	{ "network.browser_show_empty", _config_boolean, "true", "HALO_NET_BROWSER_SHOW_EMPTY", _environment_value,
		_platform_all,
		"The server browser lists games with no player (its Filters screen)." },
	{ "network.browser_show_full", _config_boolean, "true", "HALO_NET_BROWSER_SHOW_FULL", _environment_value,
		_platform_all,
		"The server browser lists full games (its Filters screen)." },
	{ "network.browser_maps", _config_string, "\"yes\"", "HALO_NET_BROWSER_MAPS", _environment_value, _platform_all,
		"The server browser lists games on maps this machine lacks: \"yes\", \"no\",\n"
		"or \"downloadable\" (those of a Custom Edition map its host shares)." },
	{ "network.browser_gametype", _config_string, "\"any\"", "HALO_NET_BROWSER_GAMETYPE", _environment_value,
		_platform_all,
		"The server browser lists games of one kind only: \"any\", \"ctf\",\n"
		"\"slayer\", \"oddball\", \"king\" or \"race\" (its Filters screen)." },
	{ "network.browser_sort", _config_string, "\"servers\"", "HALO_NET_BROWSER_SORT", _environment_value,
		_platform_all,
		"The server browser's order: \"servers\" (dedicated servers first, then by\n"
		"players), \"players\", \"name\", \"map\", \"type\" or \"locked\" (a password's\n"
		"first); \"-\" before one reverses it (\"-players\"). A column title sorts by\n"
		"it, and again the other way." },
	{ "network.stun_servers", _config_string, "\"stun.l.google.com:19302,stun.cloudflare.com:3478\"",
		"HALO_NET_STUN", _environment_value, _platform_all | _platform_xbox,
		"STUN servers that find this machine's internet address, separated by\n"
		"commas (host:port)." },
	{ "network.legacy_table", _config_string, "\"\"", "HALO_LEGACY_TABLE", _environment_value, _platform_all,
		"Testing only: an unsigned legacy table file (docs/delta.md) that sets\n"
		"the OpenCE network versions. Empty for none." },
#ifdef HALO_GAME_BROWSER
	{ "network.browser_url", _config_string, "\"https://stats.chupathingy.org\"", "HALO_NET_BROWSER", _environment_value,
		_platform_all,
		"The game list server; empty for none." },
	{ "network.list_hosted_games", _config_boolean, "true", "HALO_NET_LIST_GAMES", _environment_value, _platform_all,
		"List public games you host without a password on the game list, and\n"
		"send their carnage reports." },
	{ "network.report_hosted_games", _config_boolean, "true", "HALO_NET_REPORT_HOSTED", _environment_value,
		_platform_all,
		"Send the stats of games you host to the game list." },
	{ "network.report_joined_games", _config_boolean, "true", "HALO_NET_REPORT_GAMES", _environment_value,
		_platform_all,
		"Send the scores of internet games you join to the game list." },
	{ "network.report_events", _config_boolean, "true", "HALO_NET_REPORT_EVENTS", _environment_value, _platform_all,
		"Delta Stats: record the games you host for the site's match pages." },
	{ "network.events_token", _config_string, "\"\"", "HALO_EVENTS_TOKEN", _environment_value, _platform_all,
		"A dedicated server's Delta Stats token, from the game list's operator." },
	{ "network.events_positions", _config_integer, "2", "HALO_EVENTS_POSITIONS", _environment_value, _platform_all,
		"Delta Stats: seconds between position samples, 1 to 60; 0 for none." },
	{ "network.events_limit", _config_integer, "40000", "HALO_EVENTS_LIMIT", _environment_value, _platform_all,
		"Delta Stats: the most events kept for a game, 64 to 200000." },
	{ "network.events_part_minutes", _config_integer, "30", "HALO_EVENTS_PART_MINUTES", _environment_value,
		_platform_all,
		"Delta Stats: send a long game in parts every this many minutes; 0\n"
		"sends it only at the end." },
	{ "network.events_folder", _config_string, "\"\"", "HALO_EVENTS_FOLDER", _environment_value, _platform_all,
		"Delta Stats: a folder to keep a copy of each batch sent; empty for\n"
		"none." },
#endif

	/* the original Xbox's own (port/xbox/src/xbox_config.c), once empty
	files beside default.xbe (D:\memory_64.txt and the rest) */
	{ "console.bypass_security", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"Start the network in its open mode (XNet without its security), which\n"
		"internet play, the game list and play with the PC builds need; false\n"
		"keeps System Link's security, Xboxes only. tools/xbox_package.py\n"
		"writes true unless --secure. Read when the game starts." },
	{ "console.simulate_64mb", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"Deprecated, ignored: a devkit gets 64 MB from the XBE's own flag now\n"
		"(tools/xbox_package.py --devkit-64mb)." },
	{ "console.memory_class", _config_string, "\"auto\"", NULL, _environment_value, _platform_xbox,
		"The console's memory class. \"auto\": the console's own; a console of\n"
		"128 MB or more (the common upgrade, a devkit) gets the extras: larger\n"
		"texture and sound caches, render buffers and network queues. \"64\":\n"
		"a stock Xbox's sizes on any console, to test the 64 MB baseline or if\n"
		"an extra misbehaves. Nothing of the game itself changes with it." },
	{ "console.large_caches", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"Deprecated, ignored: a console of 128 MB or more has the larger caches\n"
		"on its own now (console.memory_class)." },
	{ "console.debug_geometry", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"For development: the debug build's debug geometry (lines and triangles\n"
		"drawn by debug commands), whose buffers take 1440 KB. Off, as the retail\n"
		"game had none; a stock 64 MB console may not have the room." },
	{ "console.log_flush", _config_string, "\"buffered\"", NULL, _environment_value, _platform_xbox,
		"When debug.txt is written. \"buffered\": the game's lines wait in memory\n"
		"and are written together, once a second, when 8 KB are waiting, and\n"
		"at once for an assert, an exit or a crash. \"every_line\": each line is\n"
		"written as it comes (slow, a file opened for each), for a crash hunt\n"
		"where even the last line before the console stops must be there." },
	{ "console.trace", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"Detailed traces in debug.txt, for reporting a join that fails: the main\n"
		"loop each second, every hop of an internet join, the games advertised.\n"
		"Other hosts appear only as tags, never by their addresses." },
	{ "console.perf_log", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"For finding what makes a scene slow: once a second, a line in debug.txt\n"
		"with the frame time (average and worst), the time each part of the frame\n"
		"took (game ticks, the render's parts, sound, network, present, the wait\n"
		"for the screen), the draw calls and objects drawn, and a line when the\n"
		"player zooms in or out. Read when the game starts." },
	{ "console.perf_gpu_sync", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"With console.perf_log: wait for the graphics chip to finish at each\n"
		"timed part of the render, so each part's time is what the chip spent\n"
		"drawing it, not only the processor's share. Slows the game down; for\n"
		"finding which part is slow on the chip. Read when the game starts." },
	{ "console.join", _config_string, "\"\"", NULL, _environment_value, _platform_xbox,
		"For testing joins without the game list: a host's LAN address\n"
		"(\"192.168.1.20\"), which System Link joins directly, or an invite\n"
		"(\"halo://join/...\"), which Y in ONLINE GAMES joins; empty for none." },
	{ "console.delta", _config_string, "\"auto\"", NULL, _environment_value, _platform_xbox,
		"Delta Peer: when joining a game whose host offers it, tell the host\n"
		"this is an Xbox (its platform, memory and nothing else), so the stats\n"
		"site, the scoreboard and the server show Xbox. \"auto\": on; \"off\" for\n"
		"the legacy protocol alone. The game is the same either way." },
	{ "console.game_list_server", _config_string, "\"\"", NULL, _environment_value, _platform_xbox,
		"Where to get the game list (\"host\" or \"a.b.c.d\", \":port\" optional)\n"
		"when warthog.milenko.org does not resolve; empty for none." },

#ifndef HALO_CONSOLE_NO_DISCORD
	/* (the project's Discord application, "Chupathingy") */
	{ "discord.application_id", _config_string, "\"1560397272166957136\"", "HALO_DISCORD_APPLICATION",
		_environment_value, _platform_desktop,
		"The Discord application the game shows as; empty for none." },
	{ "discord.presence", _config_string, "\"full\"", "HALO_DISCORD_PRESENCE", _environment_value,
		_platform_desktop,
		"What Discord shows: \"full\", \"minimal\" or \"off\"." },
	{ "discord.show_server_name", _config_boolean, "true", "HALO_DISCORD_SHOW_SERVER_NAME", _environment_value,
		_platform_desktop,
		"Show the network game's name on Discord." },
	{ "discord.show_join", _config_boolean, "true", "HALO_DISCORD_SHOW_JOIN", _environment_value,
		_platform_desktop,
		"Show a Join game button on Discord while you host a public game." },
#endif

	{ "maps.torrents", _config_boolean, "true", "HALO_MAP_TORRENTS", _environment_value, _platform_all,
		"Download a Custom Edition map this machine lacks when joining a game\n"
		"on it, from other players and seed boxes (BitTorrent), when the index\n"
		"of the maps' torrents (torrent_index) has it; false never downloads." },
	{ "maps.download_screen", _config_boolean, "true", "HALO_MAP_DOWNLOAD_SCREEN", _environment_value,
		_platform_all,
		"While a map downloads before its game is joined, show Halo's loading\n"
		"screen over the menus, its light sweeping across as the map comes (B\n"
		"stops the download); false leaves the menus up, their status line\n"
		"saying how the download goes." },
	{ "maps.seed", _config_string, "\"host\"", "HALO_MAP_SEED", _environment_value, _platform_all,
		"Seed the Custom Edition map this machine's network game is on, for\n"
		"the players joining it: \"host\" while hosting, \"all\" also while\n"
		"joined, \"off\" never." },
	{ "maps.upload_limit", _config_integer, "512", "HALO_MAP_UPLOAD_LIMIT", _environment_value, _platform_all,
		"The most the seeding sends, in KB a second, however many players are\n"
		"downloading, so that the game's own traffic comes first; 0 for no\n"
		"limit." },
	{ "maps.download_limit", _config_integer, "0", "HALO_MAP_DOWNLOAD_LIMIT", _environment_value, _platform_all,
		"The most a map download takes, in KB a second; 0 for no limit." },
	{ "maps.torrent_port", _config_integer, "0", "HALO_MAP_TORRENT_PORT", _environment_value, _platform_all,
		"The TCP port other players' downloads connect to, and the UDP port\n"
		"the DHT uses; 0 picks one. A fixed one can be forwarded on the\n"
		"router, so that players behind other routers can reach this machine." },
	{ "maps.dht", _config_boolean, "true", "HALO_MAP_DHT", _environment_value, _platform_all,
		"Find a map's peers through the DHT (the mainline one, no server of\n"
		"this project's); false finds them only through the trackers." },
	{ "maps.trackers", _config_string,
		"\"udp://halovps.fractumseraph.net:6969/announce,http://halovps.fractumseraph.net:6969/announce\"",
		"HALO_MAP_TRACKERS", _environment_value, _platform_all,
		"Comma-separated trackers every map's torrent is announced to\n"
		"(udp://host:port/announce or http://host/announce); empty for none.\n"
		"The default is the map archive's own (docs/map_torrents.md)." },
	{ "maps.web_seeds", _config_string, "\"http://halomaps.fractumseraph.net/maps/\"", "HALO_MAP_WEB_SEEDS",
		_environment_value, _platform_all,
		"Comma-separated http:// URLs of folders holding the maps' files under\n"
		"their names, which a download also takes pieces from; empty for none.\n"
		"The default is the map archive's own (docs/map_torrents.md)." },
	{ "maps.archives", _config_string, "\"halonet,halomaps\"", "HALO_MAP_ARCHIVES", _environment_value,
		_platform_desktop,
		"The community's map archives a map is taken from when BitTorrent does\n"
		"not give it, out of the zip the index names for it, in this order:\n"
		"halonet (maps.halonet.net), halomaps (halomaps.org); empty for none\n"
		"(docs/map_torrents.md)." },
	{ "maps.torrent_index", _config_string, "\"map_torrents.txt\"", "HALO_MAP_TORRENT_INDEX", _environment_value,
		_platform_all,
		"The index of the maps' torrents (tools/map_torrents.py), beside this\n"
		"file unless a full path: port/assets/network/map_torrents.txt, which\n"
		"the builds put there." },
#ifndef HALO_CONSOLE_NO_UPDATER
	{ "update.auto", _config_boolean, "true", "HALO_UPDATE_AUTO", _environment_value, _platform_all,
		"Check for a new version when the game starts." },
#endif
	{ "crash_reports.upload", _config_string, "\"yes\"", "HALO_CRASH_REPORTS", _environment_value, _platform_desktop,
		"Send a report of each crash to the developers, through network.browser_url\n"
		"(port/linux/src/crash_report.h: the build, where it crashed, the end of the\n"
		"log without IP addresses, and on Windows a minidump): \"yes\" sends them\n"
		"(this fork's default), \"no\" never does, \"ask\" asks after the next crash\n"
		"and writes the answer here." },
	/* (a dedicated server's: halo --server, port/linux/src/server_mode.c;
	written into the file only by one) */
	{ "server.enabled", _config_boolean, "false", "HALO_SERVER", _environment_set_is_true, _platform_server,
		"Run as a dedicated server (as halo --server does): host games with no\n"
		"window, sound or player of its own, one after another from\n"
		"server.rotation (docs/dedicated_server.md)." },
	{ "server.name", _config_string, "\"OpenCE Server\"", "HALO_SERVER_NAME", _environment_value, _platform_server,
		"The game's name in the lobby and the server browser: 15 characters of\n"
		"printable ASCII." },
	{ "server.max_players", _config_integer, "16", "HALO_SERVER_MAX_PLAYERS", _environment_value, _platform_server,
		"The most players (2 to 127)." },
	{ "server.min_players", _config_integer, "1", "HALO_SERVER_MIN_PLAYERS", _environment_value, _platform_server,
		"Players in the lobby before it counts down to a game (1 to 16)." },
	{ "server.password", _config_string, "\"\"", "HALO_SERVER_PASSWORD", _environment_value, _platform_server,
		"The password the server browser asks for (the invite link still joins);\n"
		"empty for none. Never logged." },
	{ "server.public", _config_boolean, "true", "HALO_SERVER_PUBLIC", _environment_value, _platform_server,
		"List the server in everyone's server browser (on the master servers,\n"
		"with network.online); false: joined by its invite link and on the LAN." },
	{ "server.rotation", _config_string, "\"bloodgulch:ctf, sidewinder:team_slayer, prisoner:slayer\"",
		"HALO_SERVER_ROTATION", _environment_value, _platform_server,
		"The games played one after another: map[:gametype[+rule=value...]],\n"
		"separated by commas, 64 at most. A map is a multiplayer map's name\n"
		"(bloodgulch), custom_maps\\<name> or a level's path; a gametype a\n"
		"built-in's name (slayer, team_slayer, ctf...), an OpenCE gametype's\n"
		"(swat...), a gametypes/<name>.toml or file=<saved gametype>." },
	{ "server.rotation_fallback", _config_boolean, "false", "HALO_SERVER_ROTATION_FALLBACK", _environment_value,
		_platform_server,
		"With no playable rotation entry, play bloodgulch:slayer rather than\n"
		"stop (exit code 3)." },
	{ "server.lobby_seconds", _config_integer, "15", "HALO_SERVER_LOBBY_SECONDS", _environment_value,
		_platform_server,
		"Seconds the lobby counts down once min_players are in (0 to 300; 0\n"
		"starts at once)." },
	{ "server.postgame_seconds", _config_integer, "12", "HALO_SERVER_POSTGAME_SECONDS", _environment_value,
		_platform_server,
		"Seconds the scores show after a game (0 to 300)." },
	{ "server.empty_seconds", _config_integer, "30", "HALO_SERVER_EMPTY_SECONDS", _environment_value,
		_platform_server,
		"Seconds a game nobody is in goes on before it ends (0: never). A game\n"
		"whose last player leaves ends at once, as the game ends it." },
	{ "server.idle_minutes", _config_integer, "0", "HALO_SERVER_IDLE_MINUTES", _environment_value, _platform_server,
		"Minutes a game goes on with no score before it ends (0: never)." },
	{ "server.team_start", _config_string, "\"one\"", "HALO_SERVER_TEAM_START", _environment_value, _platform_server,
		"A team game: \"one\" starts with one team's players (the next joiners\n"
		"balance), \"both\" waits for a player on each team, \"skip\" skips team\n"
		"games while one player waits." },
	{ "server.motd", _config_string, "\"\"", "HALO_SERVER_MOTD", _environment_value, _platform_server,
		"A notice each machine is shown as it joins (127 characters); empty for\n"
		"none." },
	{ "server.frame_rate", _config_integer, "60", "HALO_SERVER_FRAME_RATE", _environment_value, _platform_server,
		"Frames a second (30 to 120); the game's ticks stay 30 a second. A\n"
		"server with no other machine runs 10." },
	{ "server.console", _config_boolean, "true", "HALO_SERVER_CONSOLE", _environment_value, _platform_server,
		"Take commands from the standard input." },
	{ "server.console_exit_on_eof", _config_boolean, "false", "HALO_SERVER_CONSOLE_EXIT_ON_EOF", _environment_value,
		_platform_server,
		"Stop when the standard input ends; false goes on (a panel may close\n"
		"it)." },
	{ "server.init_file", _config_string, "\"init.txt\"", "HALO_SERVER_INIT_FILE", _environment_value,
		_platform_server,
		"Commands run once at the start, one a line: in the data folder unless a\n"
		"full path; empty for none." },
	{ "server.web_port", _config_integer, "8080", "HALO_SERVER_WEB_PORT", _environment_value, _platform_server,
		"The TCP port of the web page, its API and the remote console (RCON);\n"
		"0 for none." },
	{ "server.web_bind", _config_string, "\"127.0.0.1\"", "HALO_SERVER_WEB_BIND", _environment_value,
		_platform_server,
		"The address the web port listens on. Any but 127.0.0.1 needs\n"
		"web_password (behind a reverse proxy with TLS)." },
	{ "server.web_password", _config_string, "\"\"", "HALO_SERVER_WEB_PASSWORD", _environment_value,
		_platform_server,
		"The web page's admin password (8 characters or more). Empty: one is\n"
		"made at the first start, printed once and written here." },
	{ "server.web_owner_password", _config_string, "\"\"", "HALO_SERVER_WEB_OWNER_PASSWORD", _environment_value,
		_platform_server,
		"A password with the owner role (quit, sv_restart, sv_stop, the owner's\n"
		"settings, the game's console), for a supervisor on this machine: it\n"
		"signs in from 127.0.0.0/8 alone. 8 characters or more; empty for none." },
	{ "server.web_moderator_password", _config_string, "\"\"", "HALO_SERVER_WEB_MODERATOR_PASSWORD",
		_environment_value, _platform_server,
		"A second password, with the moderator role (kick, mute, say, the next\n"
		"map, bans of a day at most); empty for none." },
	{ "server.web_viewer_password", _config_string, "\"\"", "HALO_SERVER_WEB_VIEWER_PASSWORD", _environment_value,
		_platform_server,
		"A password with the viewer role (the status, the players, the log: no\n"
		"changes); empty for none." },
	{ "server.web_ui", _config_boolean, "true", "HALO_SERVER_WEB_UI", _environment_value, _platform_server,
		"Serve the admin page on the web port; false: its API and RCON only." },
	{ "server.web_trusted_proxy", _config_string, "\"\"", "HALO_SERVER_WEB_TRUSTED_PROXY", _environment_value,
		_platform_server,
		"The address of a reverse proxy in front of the web port, whose\n"
		"X-Forwarded-For (its last entry, the proxy's), -Host and -Proto are\n"
		"believed; empty for none." },
	{ "server.metrics", _config_boolean, "true", "HALO_SERVER_METRICS", _environment_value, _platform_server,
		"Serve /metrics (Prometheus) on the web port without a password." },
	{ "server.log_format", _config_string, "\"text\"", "HALO_SERVER_LOG_FORMAT", _environment_value,
		_platform_server,
		"The standard output's lines: \"text\" or \"json\"." },
	{ "server.log_addresses", _config_boolean, "false", "HALO_SERVER_LOG_ADDRESSES", _environment_value,
		_platform_server,
		"Players' internet addresses and hardware ids in the log, the events and\n"
		"the API for every role (the owner's console has them always)." },
	{ "server.log_debug_echo", _config_boolean, "false", "HALO_SERVER_LOG_DEBUG_ECHO", _environment_value,
		_platform_server,
		"Also write debug.txt's lines to the standard error." },
	{ "server.status_file", _config_string, "\"\"", "HALO_SERVER_STATUS_FILE", _environment_value,
		_platform_server,
		"A file the status (as /api/status) is written to every 2 seconds, for a\n"
		"panel without HTTP; empty for none." },
	{ "server.auto_update", _config_string, "\"off\"", "HALO_SERVER_AUTO_UPDATE", _environment_value,
		_platform_server,
		"\"off\"; \"check\" logs a newer release; \"install\" installs it once the\n"
		"server has been empty for 10 minutes, and stops (exit code 5) to be\n"
		"started again." },
	{ "server.upnp", _config_boolean, "false", "HALO_SERVER_UPNP", _environment_value, _platform_server,
		"Ask the router (UPnP) to forward the tunnel's port (network.allow_upnp,\n"
		"for a server at home)." },
	{ "server.coop_difficulty", _config_string, "\"normal\"", "HALO_SERVER_COOP_DIFFICULTY", _environment_value,
		_platform_server,
		"Co-op rotation entries' (<level>:coop) difficulty: \"easy\", \"normal\",\n"
		"\"hard\" or \"impossible\"." },
	{ "server.list_on_brokers", _config_boolean, "false", "HALO_SERVER_LIST_ON_BROKERS", _environment_value,
		_platform_server,
		"Also list the server among the games players host (through the\n"
		"signalling brokers), for server browsers with no master server." },
	{ "server.master_publish", _config_boolean, "true", "HALO_SERVER_MASTER_PUBLISH", _environment_value,
		_platform_server,
		"Publish a public server to the official master servers, signed with\n"
		"the publisher key (publisher_key_file)." },
	{ "server.masters_file", _config_string, "\"masters.txt\"", "HALO_SERVER_MASTERS_FILE", _environment_value,
		_platform_server,
		"The master servers published to, beside this file unless a full path:\n"
		"an https:// URL and its key (ed25519:<hex>) on each line. None named:\n"
		"the official master, built in." },
	{ "server.publisher_key_file", _config_string, "\"publisher.key\"", "HALO_SERVER_PUBLISHER_KEY_FILE",
		_environment_value, _platform_server,
		"The operator's Ed25519 key for the master servers (halo --server\n"
		"--keygen makes it), beside this file unless a full path.\n"
		"HALO_SERVER_PUBLISHER_KEY holds the key itself for containers." },
	{ "server.instance", _config_string, "\"\"", "HALO_SERVER_INSTANCE", _environment_value, _platform_server,
		"A short name telling this server from the operator's others on the\n"
		"master servers (letters, digits, - and _); empty: from server.name." },
	{ "server.region", _config_string, "\"\"", "HALO_SERVER_REGION", _environment_value, _platform_server,
		"A region shown in the server browser (eu, na-east...): 16 characters;\n"
		"empty for none." },
	{ "server.chat_filter", _config_string, "\"off\"", "HALO_SERVER_CHAT_FILTER", _environment_value, _platform_server,
		"The words the server keeps out of its chat and its players' names\n"
		"(streamer mode's list, by chat_filter_slurs, _swearing and _insults):\n"
		"\"off\"; \"censor\", a line's words shown as stars; \"block\", a line\n"
		"with them refused (its sender is told why) and a player whose name has\n"
		"them refused at the door. Links are network.text_chat's." },
	{ "server.chat_filter_slurs", _config_string, "\"mild\"", "HALO_SERVER_CHAT_FILTER_SLURS", _environment_value,
		_platform_server,
		"With server.chat_filter, the slurs (race, sexuality, gender, disability)\n"
		"kept out: \"off\", \"severe\" (the worst), \"strong\" or \"mild\" (all)." },
	{ "server.chat_filter_swearing", _config_string, "\"off\"", "HALO_SERVER_CHAT_FILTER_SWEARING",
		_environment_value, _platform_server,
		"With server.chat_filter, the swearing and sexual words kept out: \"off\",\n"
		"\"severe\", \"strong\" or \"mild\" (all)." },
	{ "server.chat_filter_insults", _config_string, "\"off\"", "HALO_SERVER_CHAT_FILTER_INSULTS",
		_environment_value, _platform_server,
		"With server.chat_filter, the insults that are not slurs kept out: \"off\",\n"
		"\"severe\", \"strong\" or \"mild\" (all)." },
	{ "server.master_publish_address", _config_boolean, "false", "HALO_SERVER_MASTER_PUBLISH_ADDRESS",
		_environment_value, _platform_server,
		"Let the master servers give this server's address to players; false:\n"
		"only their reachability probe uses it." },
	{ "streamer.enabled", _config_boolean, "false", "HALO_STREAMER_MODE", _environment_value, _platform_all,
		"Hide offensive words in names and text from other players, maps and mods\n"
		"as stars, at the levels below (Settings > Online > Streamer Mode)." },
	{ "streamer.slurs", _config_string, "\"mild\"", "HALO_STREAMER_SLURS", _environment_value, _platform_all,
		"With streamer.enabled, the slurs (race, sexuality, gender, disability)\n"
		"hidden: \"off\", \"severe\", \"strong\" (and severe) or \"mild\" (all)." },
	{ "streamer.swearing", _config_string, "\"strong\"", "HALO_STREAMER_SWEARING", _environment_value, _platform_all,
		"With streamer.enabled, the swearing and sexual words hidden: \"off\",\n"
		"\"severe\", \"strong\" (and severe) or \"mild\" (all)." },
	{ "streamer.insults", _config_string, "\"off\"", "HALO_STREAMER_INSULTS", _environment_value, _platform_all,
		"With streamer.enabled, the insults that are not slurs hidden: \"off\",\n"
		"\"severe\", \"strong\" (and severe) or \"mild\" (all)." },

	{ "debug.network_test", _config_string, "\"\"", "HALO_NETWORK_TEST", _environment_value, _platform_all,
		"Automated system link tests: \"host:<map>\", \"local:<map>\" (split\n"
		"screen), \"join\", \"browse\" (the server browser's fullest game) or\n"
		"\"log\" (logs the game that runs); empty for none." },
	{ "debug.network_test_start", _config_real, "15.0", "HALO_NETWORK_TEST_START", _environment_value, _platform_all,
		"Seconds after hosting that an automated test game starts." },
	{ "debug.network_test_kill", _config_real, "0.0", "HALO_NETWORK_TEST_KILL", _environment_value,
		_platform_all,
		"Every this many seconds an automated test host kills its last player; 0 never." },
	{ "debug.network_test_team_change", _config_real, "0.0", "HALO_NETWORK_TEST_TEAM_CHANGE", _environment_value,
		_platform_all,
		"Every this many seconds a test player changes teams; 0 never." },
	{ "debug.network_test_pause", _config_real, "0.0", "HALO_NETWORK_TEST_PAUSE", _environment_value, _platform_all,
		"Seconds into a test game to open the pause menu; 0 never." },
	{ "debug.network_test_moderate", _config_string, "\"\"", "HALO_NETWORK_TEST_MODERATE", _environment_value,
		_platform_all,
		"A test host's Moderation action on its first other player (\"Kick\", \"Ban forever\"); empty for none." },
	{ "debug.network_test_moderate_time", _config_real, "20.0", "HALO_NETWORK_TEST_MODERATE_TIME", _environment_value,
		_platform_all,
		"Seconds into a test game that the host's Moderation action is taken." },
	{ "debug.network_test_local_players", _config_integer, "1", "HALO_NETWORK_TEST_LOCAL_PLAYERS", _environment_value,
		_platform_all,
		"A test machine's players, 1 to 4." },
	{ "debug.network_test_score", _config_integer, "0", "HALO_NETWORK_TEST_SCORE", _environment_value, _platform_all,
		"The score a test game plays to; 0 for the game type's own." },
	{ "debug.network_test_aim", _config_boolean, "false", "HALO_NETWORK_TEST_AIM", _environment_set_is_true,
		_platform_all,
		"Each automated test player aims at the next player (where\n"
		"debug.network_test_shoot's hits land) and holds the trigger: aimed hits,\n"
		"which the host's fair play checks (network.fair_play) must deal." },
	{ "debug.network_test_cheat", _config_string, "\"\"", "HALO_NETWORK_TEST_CHEAT", _environment_value, _platform_all,
		"The automated test player moves as a cheat's game would: \"teleport\" (6\n"
		"world units ahead every 5 seconds) or \"fly\" (3 up, hovering, 4 seconds\n"
		"of every 10), for the host's fair play checks to refuse and count; empty\n"
		"for none." },
	{ "debug.network_test_watch", _config_boolean, "false", "HALO_NETWORK_TEST_WATCH", _environment_set_is_true,
		_platform_all,
		"The automated test player signs in to a dedicated server's moderation\n"
		"and, once the server gives its key a role, watches the first other\n"
		"player (the Moderation screen's Watch)." },
	{ "debug.network_test_shoot", _config_real, "0.0", "HALO_NETWORK_TEST_SHOOT", _environment_value, _platform_all,
		"Every this many seconds each test player hits the next; 0 never." },
	{ "debug.network_test_vehicle", _config_real, "0.0", "HALO_NETWORK_TEST_VEHICLE", _environment_value, _platform_all,
		"Seconds into a test game to seat the last player in a vehicle; 0\n"
		"never." },
	{ "debug.network_test_pickup", _config_real, "0.0", "HALO_NETWORK_TEST_PICKUP", _environment_value, _platform_all,
		"Seconds into a test game to stand the last player on a weapon; 0\n"
		"never." },
	{ "debug.network_test_bomb", _config_real, "0.0", "HALO_NETWORK_TEST_BOMB", _environment_value, _platform_all,
		"In an automated test Bomb game (host:<map>:grifball), this many seconds\n"
		"in (and again each time that long after) the last player stands on the\n"
		"bomb, and three seconds later in the goal its team attacks; 0 never." },
	{ "debug.network_test_bomb_in_goal", _config_boolean, "false", "HALO_NETWORK_TEST_BOMB_IN_GOAL",
		_environment_value, _platform_all,
		"With debug.network_test_bomb: the host lays the bomb in the goal the\n"
		"last player's team attacks before standing the player on it, as a bomb\n"
		"thrown down in the goal and picked up again." },
	{ "debug.network_test_bomb_throw", _config_boolean, "false", "HALO_NETWORK_TEST_BOMB_THROW",
		_environment_value, _platform_all,
		"With debug.network_test_bomb: the joining machine's player holds the\n"
		"trigger through each try's first four seconds, picking the bomb up\n"
		"with it down, throwing it down and picking it up again." },
	{ "debug.network_test_bomb_keep", _config_boolean, "false", "HALO_NETWORK_TEST_BOMB_KEEP", _environment_value,
		_platform_all,
		"With debug.network_test_bomb: the carrier is not moved into the goal (it\n"
		"keeps the bomb: debug.network_test_melee's blows strike a carrier)." },
	{ "debug.network_test_bomb_save", _config_boolean, "false", "HALO_NETWORK_TEST_BOMB_SAVE", _environment_value,
		_platform_all,
		"With debug.network_test_bomb: the host stands a player of the goal's\n"
		"team on the bomb planted there as it arms (game.bomb_arm_time), which\n"
		"saves it." },
	{ "debug.network_test_fall", _config_real, "0.0", "HALO_NETWORK_TEST_FALL", _environment_value, _platform_all,
		"In an automated test game, each time this many seconds pass the last\n"
		"player is lifted 10 world units, for a fall whose landing kills; 0\n"
		"never." },
	{ "debug.network_test_melee", _config_real, "0.0", "HALO_NETWORK_TEST_MELEE", _environment_value, _platform_all,
		"Every this many seconds an automated test host brings its last player in\n"
		"front of its first, and a player strikes the other with the melee\n"
		"button: the host's player, then the joining machine's, in turn; 0 never." },
	{ "debug.network_test_chat", _config_real, "0.0", "HALO_NETWORK_TEST_CHAT", _environment_value, _platform_all,
		"This many seconds into an automated test game each machine's player\n"
		"says a chat line to everyone, then to the team and to the vehicle (the\n"
		"host seats the last two players in one first), then the host mutes and\n"
		"unmutes a player, refuses a link and turns the chat off, and the\n"
		"clients type on the on-screen keyboard (port/linux/NETCODE.md); 0 never." },
	{ "debug.network_test_name", _config_string, "\"\"", "HALO_NETWORK_TEST_NAME", _environment_value, _platform_all,
		"The name this machine's player takes in an automated test game (ASCII);\n"
		"empty keeps its profile's." },
	{ "debug.network_test_pickup_weapon", _config_string, "\"\"", "HALO_NETWORK_TEST_PICKUP_WEAPON", _environment_value,
		_platform_all,
		"The weapon for network_test_pickup, by part of its tag name; empty\n"
		"for any." },
	{ "debug.network_test_cues", _config_string, "\"\"", "HALO_NETWORK_TEST_CUES", _environment_value, _platform_all,
		"Console lines an automated test runs at a game second, from the first tick\n"
		"every player of debug.network_test_cue_players has a unit:\n"
		"\"<second> <line>|<second> <line>...\" (the host's, or a game of one machine;\n"
		"not on the web)." },
	{ "debug.network_test_cue_players", _config_integer, "1", "HALO_NETWORK_TEST_CUE_PLAYERS", _environment_value,
		_platform_all,
		"The players debug.network_test_cues waits for." },
	{ "debug.network_test_public", _config_boolean, "false", "HALO_NETWORK_TEST_PUBLIC", _environment_value,
		_platform_all,
		"The network test's host lists its game in the server browser, as Create\n"
		"Game > Internet's PUBLIC does (debug.network_test \"browse\" joins it)." },
	{ "debug.network_test_password", _config_string, "\"\"", "HALO_NETWORK_TEST_PASSWORD", _environment_value,
		_platform_all,
		"With debug.network_test: the host's game's password (as Server Setup's\n"
		"PASSWORD), or the password a joining machine types when its invite asks\n"
		"for one; empty for none." },
	{ "debug.network_test_invite_file", _config_string, "\"\"", "HALO_NETWORK_TEST_INVITE_FILE", _environment_value,
		_platform_all,
		"With debug.network_test's host: a file the host writes its invite link\n"
		"to once it has one, for a joining machine's command line; empty for none." },
	{ "debug.telnet_console", _config_boolean, "false", "HALO_TELNET_CONSOLE", _environment_set_is_true, _platform_all,
		"A console on 127.0.0.1 (telnet_console_port), with no password." },
	{ "debug.telnet_console_port", _config_integer, "2323", "HALO_TELNET_CONSOLE_PORT", _environment_value,
		_platform_all,
		"The telnet console's port." },
	{ "debug.log_addresses", _config_boolean, "false", "HALO_LOG_ADDRESSES", _environment_set_is_true, _platform_all,
		"Write full internet addresses in debug.txt and the console. Do not post\n"
		"a debug.txt written with this on." },
	{ "debug.touch_targets", _config_boolean, "false", "HALO_TOUCH_TARGETS", _environment_set_is_true, _platform_all,
		"Outline the menus' tap targets and log each tap." },
	{ "debug.solo_game", _config_boolean, "false", "HALO_SOLO_GAME", _environment_set_is_true, _platform_all,
		"Old name for game.start_with_one_player." },
	{ "debug.network_latency", _config_real, "0.0", "HALO_NETWORK_LATENCY", _environment_value, _platform_all,
		"Milliseconds to delay everything received; 0 none." },
	{ "debug.network_loss", _config_real, "0.0", "HALO_NETWORK_LOSS", _environment_value, _platform_all,
		"Percent of received datagrams to drop; 0 none." },
	{ "debug.network_corrupt", _config_real, "0.0", "HALO_NETWORK_CORRUPT", _environment_value, _platform_all,
		"Percent of received datagrams to damage; 0 none." },
	{ "debug.network_corrupt_stream", _config_real, "0.0", "HALO_NETWORK_CORRUPT_STREAM", _environment_value,
		_platform_all,
		"Percent of stream reads to damage; 0 none." },
	{ "debug.network_corrupt_after", _config_real, "0.0", "HALO_NETWORK_CORRUPT_AFTER", _environment_value,
		_platform_all,
		"Seconds after the start before anything is damaged." },
	{ "debug.test_input", _config_string, "\"\"", "HALO_TEST_INPUT", _environment_value, _platform_all,
		"Scripted input on controller 1: \"bot:<seed>\", \"look:<seed>\" or\n"
		"\"menu:<buttons>\" (menu buttons one a second: a, b, x, y, up, down,\n"
		"left, right, start, back or wait); empty for none." },
	{ "debug.bot_trace", _config_integer, "0", "HALO_BOT_TRACE", _environment_value, _platform_all,
		"Log each bot's state (bots.count) every this many seconds: where it is\n"
		"and goes, its path, target and weapon; 0 never." },
	{ "debug.determinism", _config_string, "\"\"", "HALO_DETERMINISM", _environment_value, _platform_all,
		"The input-replay check (tools/test_determinism.py): \"record:<file>\"\n"
		"writes each tick's player actions to the file, \"replay:<file>\" plays\n"
		"them back in place of the controllers'; empty for none." },
	{ "debug.determinism_hashes", _config_string, "\"\"", "HALO_DETERMINISM_HASHES", _environment_value,
		_platform_all,
		"With debug.determinism: a file for a line of hashes of the game's state\n"
		"after each tick; empty for none." },
	{ "debug.determinism_ticks", _config_integer, "0", "HALO_DETERMINISM_TICKS", _environment_value, _platform_all,
		"With debug.determinism: quit after this many game ticks; 0 never." },
	{ "debug.determinism_seed", _config_integer, "1", "HALO_DETERMINISM_SEED", _environment_value, _platform_all,
		"With debug.determinism: the games' random seed (a replay takes its\n"
		"recording's)." },
	{ "debug.test_gamepads", _config_integer, "0", "HALO_TEST_GAMEPADS", _environment_value, _platform_desktop,
		"Virtual gamepads (up to 4) for tests of split screen: debug.test_input's\n"
		"\"menu:\" script presses their buttons and sticks as pad<n>:<button>, and\n"
		"pad<n>:unplug and pad<n>:plug take one out and put it back. 0 none." },
#ifndef HALO_CONSOLE_NO_UPDATER
	{ "debug.update_answer", _config_string, "\"\"", "HALO_UPDATE_ANSWER", _environment_value, _platform_desktop,
		"The answer to the update question, for tests: \"yes\", \"no\" or \"never\";\n"
		"empty asks." },
#endif
	{ "debug.chat_test", _config_string, "\"\"", "HALO_CHAT_TEST", _environment_value, _platform_all,
		"Scripted chat for tests: steps of\n"
		"\"<seconds>:<all|team|server|mute|unmute|localmute|log|close|type>[:<text>]\"\n"
		"separated by ;. Empty: none." },
	{ "debug.fixed_step", _config_boolean, "false", "HALO_FIXED_STEP", _environment_set_is_true, _platform_all,
		"A game of this machine alone runs one tick a frame, whatever time has\n"
		"passed, so that it plays the same every time (tests); game_speed has no\n"
		"effect." },
	{ "debug.audio_probe", _config_string, "\"\"", "HALO_AUDIO_PROBE", _environment_value, _platform_all | _platform_xbox,
		"A probe file (port/linux/game/audio_probe.c), named as the game names\n"
		"its files (d:\\ is the game's folder: on the desktop, the data folder):\n"
		"the game loads its map, plays its sounds at its spots, records what it\n"
		"plays beside the file (to compare with tools/audio_probe_compare.py),\n"
		"and quits. Empty: none." },
	{ "debug.audio_capture_seconds", _config_real, "0.0", "HALO_AUDIO_CAPTURE_SECONDS", _environment_value,
		_platform_all | _platform_xbox,
		"Record this many seconds of the sound of each game played (solo,\n"
		"system link or online), with a log of the player's position and every\n"
		"sound started, to d:\\audio_probe\\capture-<map>-<n>-* (that folder must\n"
		"be there; port/linux/game/audio_probe.c); 0 none." },
	{ "debug.exit_after", _config_real, "0.0", "HALO_EXIT_AFTER", _environment_value, _platform_all,
		"Quit this many seconds after the window opens (without a window, after\n"
		"the first frame); 0 never." },
	{ "debug.hidden_window", _config_boolean, "false", "HALO_HIDDEN_WINDOW", _environment_set_is_true, _platform_desktop,
		"Keep the window hidden (and never fullscreen)." },
	{ "debug.voice_test", _config_boolean, "false", "HALO_VOICE_TEST", _environment_set_is_true, _platform_all,
		"Voice chat's automated tests: a tone instead of the microphone, and\n"
		"each voice heard logged once a second." },
	{ "debug.null_renderer", _config_boolean, "false", "HALO_NULL_RENDERER", _environment_set_is_true, _platform_all,
		"Run without a window, drawing nothing." },
	{ "debug.gl_debug", _config_boolean, "false", "HALO_GL_DEBUG", _environment_set_is_true, _platform_all,
		"Report OpenGL errors in the log." },
	{ "debug.menu_open", _config_string, "\"\"", "HALO_MENU_OPEN", _environment_value, _platform_all,
		"Start on this menu screen instead of the main menu; empty for the\n"
		"main menu." },
	{ "debug.gpu_flush_draws", _config_integer, "-1", "HALO_GPU_FLUSH_DRAWS", _environment_value, _platform_desktop,
		"Flush the GPU every this many draws: -1 for every 3 on Intel with Mesa,\n"
		"0 never." },
	{ "debug.gpu_stats", _config_boolean, "false", "HALO_GPU_STATS", _environment_set_is_true, _platform_all,
		"Log the renderer's draw counts once a second." },
	{ "debug.gpu_trace_frame", _config_integer, "-1", "HALO_GPU_TRACE", _environment_value, _platform_all,
		"Log every draw of this frame; -1 none." },
	{ "debug.gpu_trace_constants", _config_boolean, "false", "HALO_GPU_TRACE_CONSTANTS", _environment_set_is_true, _platform_all,
		"With gpu_trace_frame, also the vertex shader constants." },
	{ "debug.gpu_skip_vertex_shaders", _config_string, "\"\"", "HALO_GPU_SKIP_VS", _environment_value, _platform_all,
		"Comma-separated ids of vertex shaders not to draw with." },
	{ "debug.gpu_dump_shaders", _config_string, "\"\"", "HALO_GPU_DUMP_SHADERS", _environment_value, _platform_all,
		"A folder to write the generated GLSL to; empty none." },
	{ "debug.gpu_debug_expression", _config_string, "\"\"", "HALO_GPU_DEBUG_EXPR", _environment_value, _platform_all,
		"A GLSL expression every pixel shader shows instead of its result." },
	{ "debug.gpu_debug_texture0", _config_boolean, "false", "HALO_GPU_DEBUG_T0", _environment_set_is_true, _platform_all,
		"Pixel shaders show their first texture." },
	{ "debug.gpu_debug_flat", _config_boolean, "false", "HALO_GPU_DEBUG_FLAT", _environment_set_is_true, _platform_all,
		"Pixel shaders show their vertex colour." },
	{ "debug.screenshot_directory", _config_string, "\"\"", "HALO_SCREENSHOT_DIR", _environment_value, _platform_all,
		"A folder to save frames to (with screenshot_every); empty none." },
	{ "debug.screenshot_every", _config_integer, "0", "HALO_SCREENSHOT_EVERY", _environment_value, _platform_all,
		"Save every this many frames to screenshot_directory; 0 none." },
	{ "debug.screenshot_window", _config_boolean, "false", "HALO_SCREENSHOT_WINDOW", _environment_set_is_true,
		_platform_all,
		"With screenshot_every, save the window as shown instead of the game's\n"
		"frame." },
	{ "debug.texture_dump_directory", _config_string, "\"\"", "HALO_TEXTURE_DUMP", _environment_value, _platform_all,
		"A folder to write every texture to as it is uploaded; empty none." },
#ifndef HALO_CONSOLE_NO_DISCORD
	{ "debug.discord_log", _config_boolean, "false", "HALO_DISCORD_LOG", _environment_set_is_true,
		_platform_desktop,
		"Log each activity sent to Discord." },
#endif
	{ "debug.texture_log", _config_boolean, "false", "HALO_TEXTURE_LOG", _environment_set_is_true, _platform_all,
		"Log texture uploads." },
	{ "debug.texture_no_cache", _config_boolean, "false", "HALO_TEXTURE_NO_CACHE", _environment_set_is_true, _platform_all,
		"Upload textures again every time they are used." },
	{ "debug.sample_seconds", _config_real, "0.0", "HALO_SAMPLE", _environment_value, _platform_android,
		"Log where every game thread is this often, in seconds; 0 never." },
	{ "debug.memory_watch", _config_boolean, "true", NULL, _environment_value, _platform_android,
		"Notice the game's writes to cached textures and vertices by page\n"
		"protection; false compares page contents once a frame instead, which is\n"
		"slower. Under ARM translation (the x86 emulator) the app always compares\n"
		"contents. Read by the app from the file (port/android/host/host_main.c)." },
#ifdef HALO_PROFILE
	{ "debug.profile_record", _config_boolean, "false", "HALO_PROFILE_RECORD", _environment_value, _platform_all,
		"Record a profile with no command (configure.py --profile builds): from\n"
		"when profile_record_when says until profile_stop, a map change or the\n"
		"end, into numbered part files in the data folder's profiles folder\n"
		"(tools/net_report.py reads it)." },
	{ "debug.profile_record_when", _config_string, "\"start\"", "HALO_PROFILE_RECORD_WHEN", _environment_value,
		_platform_all,
		"\"start\": from the first frame until the first map change; \"game\":\n"
		"each game that is not the main menu, a recording each, until its map\n"
		"goes." },
	{ "debug.profile_memory", _config_integer, "256", "HALO_PROFILE_MEMORY", _environment_value, _platform_all,
		"Megabytes a recording keeps in memory, 4 to 1024: two halves, each\n"
		"written out as a part when it fills." },
#endif
#ifdef HALO_SWITCH_CONSOLE
	/* read by the Switch host (port/switch/host/host_main.c, host_dk.c) */
	{ "debug.profiler", _config_boolean, "false", "HALO_PROFILER", _environment_set_is_true, _platform_android,
		"Profile the game: sample where its threads are (debug.profile_hz times a\n"
		"second) and write the counts to the profile folder every 20 s, for\n"
		"tools/switch_profile.py. Takes effect the next start." },
	{ "debug.profile_hz", _config_integer, "500", "HALO_PROFILE_HZ", _environment_value, _platform_android,
		"The profiler's samples a second, when debug.profiler is true." },
	{ "debug.texture_verify_compressed", _config_boolean, "false", "HALO_TEXTURE_VERIFY_COMPRESSED",
		_environment_set_is_true, _platform_android,
		"Read each DXT-compressed texture back out of its image after it is written,\n"
		"and log where it differs from the game's bytes (port/switch/host/host_dk.c).\n"
		"Slow: it waits for the GPU each time. Takes effect the next start." },
	/* (the Switch's debug and performance settings, read as
	display.resolution_limit is: here, so that the debug section stays
	together in config.toml) */
	{ "debug.resolution_uncapped", _config_boolean, "false", "HALO_RESOLUTION_UNCAPPED", _environment_value,
		_platform_android,
		"Draw at display.resolution_limit's lines even past what the console\n"
		"shows, scaled down to it: a Switch Lite (720p) drawing 1080p, to test\n"
		"a docked console's GPU load. Changes at once." },
	{ "debug.log_files", _config_boolean, "true", "HALO_LOG_FILES", _environment_value, _platform_android,
		"Write the logs to files: the game's debug.txt (beside the data) and the\n"
		"program's halo.log. false writes neither, which spares the card a write\n"
		"for each line; nothing is kept to say what went wrong. halo.log takes it\n"
		"the next start." },
	{ "debug.mic_probe", _config_boolean, "false", "HALO_MIC_PROBE", _environment_set_is_true, _platform_android,
		"For two minutes from the start, log what the console's microphone input\n"
		"hears (port/switch/host/host_mic_probe.c): its inputs, the format it gives\n"
		"and the sound's level each second. Takes effect the next start." },
	{ "debug.gpu_trace", _config_boolean, "false", "HALO_GPU_TRACE_FAULTS", _environment_set_is_true, _platform_android,
		"Keep a record of the GPU's last commands, and after a GPU fault write it and\n"
		"the console's account of the fault to halo.log (port/switch/host/host_dk.c).\n"
		"It costs the GPU some time. Takes effect the next start." },
	{ "debug.gpu_fault_test", _config_boolean, "false", "HALO_GPU_FAULT_TEST", _environment_set_is_true,
		_platform_android,
		"Make the GPU fault on purpose at the 1200th frame, to see gpu_trace's record.\n"
		"Takes effect the next start." },
	{ "debug.gpu_profile", _config_boolean, "false", "HALO_GPU_PROFILE", _environment_set_is_true, _platform_android,
		"Every 300 frames, log the GPU's time by part of the frame (the sky, the\n"
		"objects, the fog...) and what a frame asks of it. Takes effect the next start." },
	{ "debug.gpu_capture", _config_boolean, "false", "HALO_GPU_CAPTURE", _environment_set_is_true, _platform_android,
		"With gpu_profile, time one frame of every 300 draw by draw, and log the GPU's\n"
		"time by shader, target and kind of command. Takes effect the next start." },
	{ "debug.gpu_experiment", _config_string, "\"\"", "HALO_GPU_EXPERIMENT", _environment_value, _platform_android,
		"Measurements of what a change could save, comma-separated:\n"
		"\"no_compression\" (render targets without hardware compression),\n"
		"\"no_target_barriers\" (no wait before a render target change; may draw\n"
		"wrong), \"lod_bias\" (textures sampled three levels smaller: blurry).\n"
		"Empty for none. Takes effect the next start." },
	{ "debug.gpu_shader_ranges", _config_boolean, "true", "HALO_GPU_SHADER_RANGES", _environment_value,
		_platform_android,
		"Leave out of the pixel shaders the clamps their values cannot need\n"
		"(port/switch/guest/nv2a_psh_dk.c); false writes them all, as before, to\n"
		"compare the two. Takes effect the next start." },
	{ "debug.gpu_texture_clip", _config_string, "\"on\"", "HALO_GPU_TEXTURE_CLIP", _environment_value,
		_platform_android,
		"Shadows cast on the level drawn only over their map: \"on\", \"off\", or\n"
		"\"wide\" (clipped by limits nothing reaches, to tell the clip's own effect).\n"
		"Takes effect the next start." },
	{ "debug.original_zoom", _config_boolean, "false", "HALO_ORIGINAL_ZOOM", _environment_value, _platform_android,
		"Draw the zoom's blur as the game does, in passes over the whole screen,\n"
		"rather than on a small copy blended over it once (about 3.7 ms less of\n"
		"the GPU's frame at 720p). Takes effect the next start." },
	{ "debug.gpu_blend_discard", _config_boolean, "true", "HALO_GPU_BLEND_DISCARD", _environment_value,
		_platform_android,
		"Let a blended draw's pixels go where they would leave the screen as it was\n"
		"(the empty parts of particles and glows); false blends them all in, as\n"
		"before, to compare. Takes effect the next start." },
	{ "debug.render_thread", _config_boolean, "true", "HALO_RENDER_THREAD", _environment_value, _platform_android,
		"Turn the game's drawing commands into the GPU's on a thread of their own,\n"
		"on another core, while the game goes on with its next frame; false runs\n"
		"them on the game's thread, as before. Takes effect the next start." },
	{ "debug.gpu_draw_scissor", _config_boolean, "true", "HALO_GPU_DRAW_SCISSOR", _environment_value,
		_platform_android,
		"Rasterize an object's shadow and a dynamic light's pass only inside the\n"
		"rectangle of the screen they can change; false rasterizes their whole\n"
		"triangles, as before, to compare. Takes effect the next start." },
	{ "debug.original_blur", _config_boolean, "false", "HALO_ORIGINAL_BLUR", _environment_value, _platform_android,
		"Draw a blur screen effect (cutscenes) as the game does, in passes over the\n"
		"whole screen, rather than its passes on a small copy of it. Takes effect\n"
		"the next start." },
	{ "debug.gpu_glsl_folder", _config_string, "\"\"", "HALO_GPU_GLSL_FOLDER", _environment_value, _platform_android,
		"A folder to write each shader the game draws with to, as GLSL named by its\n"
		"hash (the names gpu_capture logs); the folder must exist. Empty for none." },
	{ "performance.cpu_clock", _config_string, "\"1785\"", "HALO_CPU_CLOCK", _environment_value, _platform_android,
		"The CPU's clock: \"1785\" (1.7 GHz, the most Nintendo's own games use),\n"
		"\"1020\" (1 GHz, the console's handheld rate: cooler, and the battery\n"
		"lasts longer) or \"stock\" (left as the console has it). Changes at once." },
#endif
};

#define NUMBER_OF_CONFIG_SETTINGS (sizeof(config_settings) / sizeof(config_settings[0]))

#if defined(HALO_ANDROID) && !defined(HALO_BROWSER)
#define CONFIG_BUILD_PLATFORM _platform_android
#elif defined(HALO_WEB)
#define CONFIG_BUILD_PLATFORM (_platform_desktop | _platform_web)
#elif defined(HALO_XBOX_CONSOLE)
#define CONFIG_BUILD_PLATFORM _platform_xbox
#elif defined(_WIN32)
#define CONFIG_BUILD_PLATFORM (_platform_desktop | _platform_windows)
#else
#define CONFIG_BUILD_PLATFORM _platform_desktop
#endif

/* a dedicated server (server.enabled, --server, HALO_SERVER: decided as the
file is read): its settings are written into the file too */
static int config_server_mode;
/* a run that only answers its command line (--version...): nothing written */
static int config_answer_only;
/* halo --probe [<invite>]: a list site's look at a game (network_test.c's
"probe"), headless (port, this fork: unused where the command line is not
read, as on Android) */
__attribute__((unused)) static int config_probe_mode;
#define CONFIG_PLATFORM (CONFIG_BUILD_PLATFORM | (config_server_mode ? _platform_server : 0))

struct config_value
{
	int boolean;
	long integer;
	double real;
	char *string;
};

static struct config_value config_values[NUMBER_OF_CONFIG_SETTINGS];
/* whether the file or the environment gives the setting a value */
static int config_given[NUMBER_OF_CONFIG_SETTINGS];
/* the settings an existing file would see change with this version's
defaults, until the player is asked (config_find_new_defaults): 1 for one
that follows the default (commented out, or not there), 2 for one that an
older version wrote as a value beside the commented one; and the old
default each had, as written */
static int config_pending[NUMBER_OF_CONFIG_SETTINGS];
static const char *config_pending_old[NUMBER_OF_CONFIG_SETTINGS];
static int config_loaded = 0;
#ifndef HALO_CONSOLE_NO_UPDATER
/* update.auto as the file or the environment gave it, in a packaged
install, where update.auto itself is off (config_load) */
static int packaged_update_check = 1;
#endif
static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- the command line */

#if !defined(HALO_ANDROID) && !defined(HALO_WEB)
/* whether the command line has the argument name ("--server"), and with
value, the one after it (or after "=": --config=<path>) */
static int config_argument(const char *name, char *value, size_t size)
{
	char text[1024];
	size_t length = strlen(name);
	int index;

	for (index = 1; posix_command_line_argument(index, text, (posix_ulong)sizeof(text)); index++)
	{
		if (!strcmp(text, name))
			return !value || posix_command_line_argument(index + 1, value, (posix_ulong)size);
		if (value && !strncmp(text, name, length) && text[length] == '=')
		{
			snprintf(value, size, "%s", text + length + 1);
			return 1;
		}
	}
	return 0;
}
#endif

/* ---------- the file */

int platform_packaged(void)
{
#if defined(HALO_ANDROID) || defined(HALO_XBOX_CONSOLE)
	return 0;
#else
	static int packaged = -1;

	if (packaged < 0)
	{
		char path[1100];
		SDL_PathInfo information;

		packaged = packaged_install_marked(getenv("HALO_PACKAGED"),
			packaged_install_file_path(path, sizeof(path), SDL_GetBasePath()) &&
			SDL_GetPathInfo(path, &information) && information.type == SDL_PATHTYPE_FILE);
	}
	return packaged;
#endif
}

#if defined(__linux__) && !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
/* whether folder has maps/ or config.toml, in any case */
static int folder_has_setup(const char *folder)
{
	char found[256];

	return posix_find_entry_case_insensitive(folder, "maps", found, sizeof(found)) ||
		posix_find_entry_case_insensitive(folder, "config.toml", found, sizeof(found));
}
#endif

int platform_app_folder(char *path, unsigned long size)
{
#if defined(__APPLE__) && !defined(HALO_XBOX_CONSOLE)
	const char *base = SDL_GetBasePath();
	const char *home = getenv("HOME");

	if (!base || !strstr(base, ".app/Contents/") || !home || !*home)
		return 0;
	snprintf(path, (size_t)size, "%s/Library/Application Support/ChupathingyCE", home);
	SDL_CreateDirectory(path);
	return 1;
#elif defined(__linux__) && !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
	/* an AppImage (which sets APPIMAGE) runs from its own file, mounted
	read-only, and a packaged install (packaged_install.h) from a folder
	its player cannot write: their data and settings go in the save root's
	usual place (save_folder.c), or a test's HALO_SAVE_ROOT. So do those of
	a game in any folder it cannot write, unless the folder has its own
	maps/ or config.toml (a setup made there by hand, kept as it is) */
	const char *appimage = getenv("APPIMAGE");
	const char *save_root = getenv("HALO_SAVE_ROOT");
	const char *data_home = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	const char *base = SDL_GetBasePath();
	int writable = !base || !*base || access(base, W_OK) == 0;

	if (!packaged_install_files_in_home(appimage && *appimage, platform_packaged(), writable,
			writable || folder_has_setup(base)))
	{
		return 0;
	}
	if (save_root && *save_root)
		snprintf(path, (size_t)size, "%s", save_root);
	else if (data_home && *data_home)
		snprintf(path, (size_t)size, "%s/chupathingyce", data_home);
	else if (home && *home)
		snprintf(path, (size_t)size, "%s/.local/share/chupathingyce", home);
	else
		return 0;
	SDL_CreateDirectory(path);
	return 1;
#else
	(void)path;
	(void)size;
	return 0;
#endif
}

static void config_path(char *path, size_t size)
{
#ifdef HALO_BROWSER
	snprintf(path, size, "/storage/config.toml");
	return;
#endif
#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
	char folder[1024];
#endif
#ifdef __APPLE__
	const char *save_root = getenv("HALO_SAVE_ROOT");

	/* (a copy given a save root of its own, a test's, keeps its settings
	there too, not in the player's) */
	if (save_root && *save_root)
	{
		snprintf(path, size, "%s/config.toml", save_root);
		return;
	}
#endif
#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
	/* (the application's folder: not the application, which is signed, or
	the AppImage, which cannot be written) */
	if (platform_app_folder(folder, sizeof(folder)))
	{
		snprintf(path, size, "%s/config.toml", folder);
		return;
	}
#endif
#if defined(HALO_XBOX_CONSOLE)
	/* beside default.xbe */
	snprintf(path, size, "d:\\config.toml");
#elif defined(HALO_ANDROID) || defined(HALO_WEB)
	/* the data folder, which the app names (port/android/host/host_main.c;
	in OpenCE's web build, port/web/src/web_main.c) */
	const char *root = getenv("HALO_DATA_ROOT");

	snprintf(path, size, "%s/config.toml", root && *root ? root : ".");
#else
	/* HALO_CONFIG_FILE or --config <path> (a dedicated server's instance's
	own), else the executable's folder's, with its separator */
	const char *file = getenv("HALO_CONFIG_FILE");
	const char *base;

	if (file && *file)
	{
		snprintf(path, size, "%s", file);
		return;
	}
	if (config_argument("--config", path, size) && *path)
		return;
	base = SDL_GetBasePath();
	snprintf(path, size, "%sconfig.toml", base ? base : "");
#endif
}

/* the whole file, NUL terminated, or NULL; free() it */
static char *config_read_file(const char *path, size_t *size)
{
#if defined(HALO_ARM64_GUEST) || defined(HALO_XBOX_CONSOLE)
	FILE *file = fopen(path, "rb");
	char *text = NULL;
	long length;

	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) >= 0 && fseek(file, 0, SEEK_SET) == 0)
	{
		text = malloc((size_t)length + 1);
		if (text && fread(text, 1, (size_t)length, file) == (size_t)length)
		{
			text[length] = 0;
			*size = (size_t)length;
		}
		else
		{
			free(text);
			text = NULL;
		}
	}
	fclose(file);
	return text;
#else
	/* SDL's, for UTF-8 paths on Windows */
	void *data = SDL_LoadFile(path, size);
	char *text;

	if (!data)
		return NULL;
	text = malloc(*size + 1);
	if (text)
	{
		memcpy(text, data, *size);
		text[*size] = 0;
	}
	SDL_free(data);
	return text;
#endif
}

static int config_write_file(const char *path, const char *text)
{
#if defined(HALO_ARM64_GUEST) || defined(HALO_XBOX_CONSOLE)
	FILE *file = fopen(path, "wb");
	int written;

	if (!file)
		return 0;
	written = fwrite(text, 1, strlen(text), file) == strlen(text);
#ifdef HALO_SWITCH_CONSOLE
	/* leaving the game is _exit, which does not flush the card; without a
	commit here a setting is kept for this run and gone on the next */
	if (written)
		fsync(fileno(file));
	written = fclose(file) == 0 && written;
	if (written)
		host_storage_commit();
	return written;
#else
	return fclose(file) == 0 && written;
#endif
#elif !defined(HALO_ANDROID) && !defined(HALO_WEB)
	/* (a dedicated server's holds its passwords: its user's to read alone) */
	if (!SDL_SaveFile(path, text, strlen(text)))
		return 0;
	if (config_server_mode)
		posix_server_private_file(path);
	return 1;
#else
	return SDL_SaveFile(path, text, strlen(text));
#endif
}

/* the file's layout: 2 writes a setting at its default commented out (1,
before config_version, wrote every value, which kept a default a later
version changed) */
#define CONFIG_VERSION 2
/* the defaults the game has asked about (game.defaults_answered): the ones
of v0.8.0 */
#define CONFIG_DEFAULTS_GENERATION 1
#define CONFIG_VERSION_LINE "config_version = 2\n"
#define CONFIG_VERSION_LINES "# The layout of this file, for the game: leave it.\n" CONFIG_VERSION_LINE
#define CONFIG_VERSION_TEXT "\n" CONFIG_VERSION_LINES

struct config_text
{
	char *buffer;
	size_t length, capacity;
};

static void config_append(struct config_text *text, const char *string)
{
	size_t length = strlen(string);

	if (text->length + length + 1 > text->capacity)
	{
		size_t capacity = (text->capacity ? text->capacity : 4096) * 2 + length;
		char *buffer = realloc(text->buffer, capacity);

		if (!buffer)
			return;
		text->buffer = buffer;
		text->capacity = capacity;
	}
	memcpy(text->buffer + text->length, string, length + 1);
	text->length += length;
}

/* the first length characters of text, as a string of their own */
static char *config_copy(const char *text, size_t length)
{
	char *copy = malloc(length + 1);

	if (copy)
	{
		memcpy(copy, text, length);
		copy[length] = 0;
	}
	return copy;
}

/* the line's key, if it is "key = ..." (after spaces), in key */
static int config_line_key(const char *line, const char *end, const char *key)
{
	size_t length = strlen(key);

	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	if ((size_t)(end - line) <= length || strncmp(line, key, length) != 0)
		return 0;
	line += length;
	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	return line < end && *line == '=';
}

/* the section the line opens, if it is "[section]" (after spaces) */
static int config_line_section(const char *line, const char *end, char *section, size_t size)
{
	const char *close;

	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	if (line >= end || *line != '[')
		return 0;
	close = memchr(line, ']', (size_t)(end - line));
	if (!close || (size_t)(close - line - 1) >= size)
		return 0;
	memcpy(section, line + 1, (size_t)(close - line - 1));
	section[close - line - 1] = 0;
	return 1;
}

/* the line's key, if it is a setting written at its default, "# key = ..."
(config_append_setting) */
static int config_line_commented_key(const char *line, const char *end, const char *key)
{
	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	return line < end && *line == '#' && config_line_key(line + 1, end, key);
}

/* one setting as the file holds it: its comment, and its key at the
default, commented out, so that it follows the default of whichever version
reads the file until the player chooses a value (config_write) */
static void config_append_setting(struct config_text *text, const struct config_setting *setting)
{
	const char *dot = strchr(setting->name, '.');
	const char *line;
	char buffer[256];

	config_append(text, "\n");
	for (line = setting->comment; *line;)
	{
		size_t length = strcspn(line, "\n");

		snprintf(buffer, sizeof(buffer), "# %.*s\n", (int)length, line);
		config_append(text, buffer);
		line += length;
		if (*line)
			line++;
	}
#if (!defined(HALO_ANDROID) || defined(HALO_BROWSER)) && !defined(HALO_XBOX_CONSOLE)
	/* (Android apps and the console have no environment to set) */
	switch (setting->environment_style)
	{
	case _environment_value:
		snprintf(buffer, sizeof(buffer), "# (for one run: %s=<value>)\n", setting->environment);
		break;
	case _environment_set_is_true:
		snprintf(buffer, sizeof(buffer), "# (for one run: %s=1 makes it true)\n", setting->environment);
		break;
	case _environment_set_is_false:
		snprintf(buffer, sizeof(buffer), "# (for one run: %s=1 makes it false)\n", setting->environment);
		break;
	}
	config_append(text, buffer);
#endif
	snprintf(buffer, sizeof(buffer), "# %s = %s\n", dot + 1, setting->default_value);
	config_append(text, buffer);
}

/* the file with every setting of this build at its default */
static char *config_default_text(void)
{
	struct config_text text = { NULL, 0, 0 };
	char section[32] = "";
	size_t index;

#if defined(HALO_XBOX_CONSOLE)
	config_append(&text,
		"# Halo settings\n"
		"#\n"
		"# The game writes this file beside default.xbe with the defaults when it\n"
		"# is missing. A setting at its default is commented out (\"# key =\n"
		"# value\") and follows the default of the version that runs: take out\n"
		"# the \"# \" and change the value to choose another. A line starting with\n"
		"# # does nothing. The game reads this file when it starts: restart it\n"
		"# after an edit. Delete the file to go back to the defaults.\n"
		CONFIG_VERSION_TEXT);
#elif defined(HALO_ANDROID) && !defined(HALO_BROWSER)
	config_append(&text,
		"# Halo settings\n"
		"#\n"
		"# The game writes this file with the defaults when it is missing. A\n"
		"# setting at its default is commented out (\"# key = value\") and follows\n"
		"# the default of the version that runs: take out the \"# \" and change the\n"
		"# value to choose another. A line starting with # does nothing. The game\n"
		"# reads this file when it starts: restart it after an edit. Delete the\n"
		"# file to go back to the defaults.\n"
		CONFIG_VERSION_TEXT);
#else
	config_append(&text,
		"# Halo settings\n"
		"#\n"
		"# The game writes this file with the defaults when it is missing. A\n"
		"# setting at its default is commented out (\"# key = value\") and follows\n"
		"# the default of the version that runs: take out the \"# \" and change the\n"
		"# value to choose another. A line starting with # does nothing. The game\n"
		"# reads this file when it starts: restart it after an edit. Delete the\n"
		"# file to go back to the defaults.\n"
		"# Each setting can also be set for one run with the environment variable\n"
		"# named with it, which wins over this file.\n"
		CONFIG_VERSION_TEXT);
#endif
	/* each section once, where its first setting is, with all of its
	settings: TOML refuses a section twice, and the whole file would go
	unread (config_join_sections) */
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		const char *dot = strchr(setting->name, '.');
		size_t length, other;
		char buffer[64];

		if (!(setting->platforms & CONFIG_PLATFORM) || !dot)
			continue;
		length = (size_t)(dot - setting->name);
		for (other = 0; other < index; other++)
		{
			if ((config_settings[other].platforms & CONFIG_PLATFORM) &&
				!strncmp(config_settings[other].name, setting->name, length + 1))
				break;
		}
		if (other < index)
			continue;
		snprintf(section, sizeof(section), "%.*s", (int)length, setting->name);
		snprintf(buffer, sizeof(buffer), "\n[%s]\n", section);
		config_append(&text, buffer);
		for (other = index; other < NUMBER_OF_CONFIG_SETTINGS; other++)
		{
			if ((config_settings[other].platforms & CONFIG_PLATFORM) &&
				!strncmp(config_settings[other].name, setting->name, length + 1))
				config_append_setting(&text, &config_settings[other]);
		}
	}
	return text.buffer;
}

/* whether text has the setting's line in its section: as a value, or (with
commented) also commented out at its default */
static int config_text_has_key(const char *text, const char *name, int commented)
{
	const char *dot = strchr(name, '.');
	char section[64], current[64] = "";
	const char *line;

	snprintf(section, sizeof(section), "%.*s", (int)(dot - name), name);
	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");

		if (!config_line_section(line, end, current, sizeof(current)) && !strcmp(current, section) &&
			(config_line_key(line, end, dot + 1) || (commented && config_line_commented_key(line, end, dot + 1))))
		{
			return 1;
		}
		line = *end ? end + 1 : end;
	}
	return 0;
}

/* the settings of this build that text (the file, parsed as table) lacks,
neither a value nor commented out at a default, added to it in their
sections (commented out), keeping the rest as it is: a newer version's
settings appear in an older file. Returns the new text, or NULL if nothing
was missing */
static char *config_add_missing(const char *text, toml_datum_t table)
{
	char *result = NULL;
	size_t index;

	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		const char *dot = strchr(setting->name, '.');
		const char *current = result ? result : text;
		struct config_text block = { NULL, 0, 0 };
		struct config_text updated = { NULL, 0, 0 };
		char header[40];
		const char *line;
		const char *insert = NULL;

		if (!(setting->platforms & CONFIG_PLATFORM) || !dot || toml_seek(table, setting->name).type != TOML_UNKNOWN ||
			config_pending[index] || config_text_has_key(current, setting->name, 1))
			continue;
		snprintf(header, sizeof(header), "[%.*s]", (int)(dot - setting->name), setting->name);
		/* the end of the section's last line that is not blank */
		for (line = current; *line; )
		{
			const char *start = line;
			size_t length = strcspn(line, "\n");

			while (*start == ' ' || *start == '\t')
				start++;
			if (insert && *start == '[')
				break;
			if (!insert && !strncmp(start, header, strlen(header)))
				insert = line + length;
			else if (insert && start < line + length && *start != '\r')
				insert = line + length;
			line += length;
			if (*line)
				line++;
		}
		if (insert)
		{
			if (*insert)
				insert++;
			config_append_setting(&block, setting);
		}
		else
		{
			/* no such section: a new one at the end */
			insert = current + strlen(current);
			config_append(&block, current[0] && insert[-1] != '\n' ? "\n\n" : "\n");
			config_append(&block, header);
			config_append(&block, "\n");
			config_append_setting(&block, setting);
		}
		if (!block.buffer)
			continue;
		{
			char *before = config_copy(current, (size_t)(insert - current));

			if (before)
				config_append(&updated, before);
			free(before);
		}
		if (insert > current && insert[-1] != '\n')
			config_append(&updated, "\n");
		config_append(&updated, block.buffer);
		config_append(&updated, insert);
		free(block.buffer);
		if (updated.buffer)
		{
			free(result);
			result = updated.buffer;
			platform_log("settings: added %s (new in this version) at its default", setting->name);
		}
	}
	return result;
}

static int config_line_section(const char *line, const char *end, char *section, size_t size);

/* text from start to end, appended */
static void config_append_range(struct config_text *text, const char *start, const char *end)
{
	char *copy = config_copy(start, (size_t)(end - start));

	if (copy)
		config_append(text, copy);
	free(copy);
}

/* a file with a section twice, which TOML refuses (the defaults of the
versions from 2026-10-10 had a second [display] after [game]): each
section's later parts moved to the end of its first, the rest kept as it
is. Returns the new text, or NULL if no section is there twice */
static char *config_join_sections(const char *text)
{
	enum { PARTS_MAXIMUM = 64 };
	struct
	{
		char name[64];
		const char *start, *end;
	} parts[PARTS_MAXIMUM];
	struct config_text joined = { NULL, 0, 0 };
	const char *line;
	const char *first_part = NULL;
	int count = 0, repeated = 0, index, other;

	/* the parts, each from its section's line to the next one's */
	for (line = text; *line; )
	{
		size_t length = strcspn(line, "\n");
		char name[64];

		if (config_line_section(line, line + length, name, sizeof(name)))
		{
			if (count == PARTS_MAXIMUM)
				return NULL;
			if (count)
				parts[count - 1].end = line;
			else
				first_part = line;
			for (other = 0; other < count; other++)
				repeated |= !strcmp(parts[other].name, name);
			strcpy(parts[count].name, name);
			parts[count].start = line;
			count++;
		}
		line += length;
		if (*line)
			line++;
	}
	if (!repeated)
		return NULL;
	parts[count - 1].end = line;

	config_append_range(&joined, text, first_part);
	for (index = 0; index < count; index++)
	{
		for (other = 0; other < index && strcmp(parts[other].name, parts[index].name); other++)
			;
		if (other < index)
			continue;
		config_append_range(&joined, parts[index].start, parts[index].end);
		for (other = index + 1; other < count; other++)
		{
			const char *body;

			if (strcmp(parts[other].name, parts[index].name))
				continue;
			/* (its settings, without its section's line) */
			body = memchr(parts[other].start, '\n', (size_t)(parts[other].end - parts[other].start));
			if (!body)
				continue;
			if (joined.length && joined.buffer[joined.length - 1] != '\n')
				config_append(&joined, "\n");
			config_append_range(&joined, body + 1, parts[other].end);
		}
	}
	return joined.buffer;
}

/* ---------- values */

static int config_text_is_false(const char *text)
{
	char lower[8];
	size_t index;

	for (index = 0; index + 1 < sizeof(lower) && text[index]; index++)
		lower[index] = (char)tolower((unsigned char)text[index]);
	lower[index] = 0;
	return !strcmp(lower, "0") || !strcmp(lower, "false") || !strcmp(lower, "no") || !strcmp(lower, "off");
}

#ifndef HALO_XBOX_CONSOLE
/* whether an environment variable that only has to be set (a
_environment_set_is_*) is: one empty or false is not */
static int config_environment_set(const char *text)
{
	return text[0] && !config_text_is_false(text);
}
#endif

static void config_set_from_text(struct config_value *value, enum config_type type, const char *text)
{
	switch (type)
	{
	case _config_boolean:
		value->boolean = !config_text_is_false(text);
		break;
	case _config_integer:
		value->integer = strtol(text, NULL, 10);
		break;
	case _config_real:
		value->real = strtod(text, NULL);
		break;
	case _config_string:
		/* (the old string is kept, not freed: config_string's callers hold
		its pointer, and Settings writes few, seldom) */
		value->string = strdup(text);
		break;
	}
}

static int config_was_boolean(const char *name);

/* the value in the file, if it is there and of the setting's type */
static void config_set_from_file(struct config_value *value, const struct config_setting *setting,
	toml_datum_t table)
{
	toml_datum_t datum = toml_seek(table, setting->name);
	int wrong_type = 0;

	if (datum.type == TOML_UNKNOWN)
		return;
	switch (setting->type)
	{
	case _config_boolean:
		if (datum.type == TOML_BOOLEAN)
			value->boolean = datum.u.boolean;
		else
			wrong_type = 1;
		break;
	case _config_integer:
		if (datum.type == TOML_INT64)
			value->integer = (long)datum.u.int64;
		else
			wrong_type = 1;
		break;
	case _config_real:
		if (datum.type == TOML_FP64)
			value->real = datum.u.fp64;
		else if (datum.type == TOML_INT64)
			value->real = (double)datum.u.int64;
		else
			wrong_type = 1;
		break;
	case _config_string:
		if (datum.type == TOML_STRING)
		{
			free(value->string);
			value->string = strdup(datum.u.s);
		}
		/* (true or false, as an older version had it: config_old_words) */
		else if (datum.type == TOML_BOOLEAN && config_was_boolean(setting->name))
		{
			free(value->string);
			value->string = strdup(datum.u.boolean ? "true" : "false");
		}
		else
		{
			wrong_type = 1;
		}
		break;
	}
	if (wrong_type)
	{
		static const char *const expected[] = { "true or false", "a whole number", "a number", "a quoted string" };

		platform_log("config.toml line %d: %s should be %s; using %s", datum.lineno, setting->name,
			expected[setting->type], setting->default_value);
	}
}

/* how many times a setting has been written (config_write): readers that
keep a setting watch this, to read it again */
static volatile unsigned long config_change_count;

/* the settings the game asked for last, by where their names are: most reads
pass the same string literal each time (the scoreboard and the HUD ask each
frame), and a search of the table compares the name with the 200 others
first. A slot is only a guess, taken when the name at the index it gives is
the one asked for, so two threads that fill a slot at once, or a name in a
buffer that later holds another, only cost a search. */
enum
{
	CONFIG_INDEX_GUESSES = 64
};
static long config_index_guesses[CONFIG_INDEX_GUESSES];

static long config_setting_index(const char *name)
{
	size_t index;
	long *guess = &config_index_guesses[((size_t)name >> 2) % CONFIG_INDEX_GUESSES];
	long guessed = *guess;

	/* (the slot holds the index plus one: 0 is no guess) */
	if (guessed > 0 && guessed <= (long)NUMBER_OF_CONFIG_SETTINGS && !strcmp(config_settings[guessed - 1].name, name))
		return guessed - 1;
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		if (!strcmp(config_settings[index].name, name))
		{
			*guess = (long)index + 1;
			return (long)index;
		}
	}
	return -1;
}

/* a key in the file that is no setting, likely misspelt or in the wrong
section: said in the log, with the section of a setting of that name */
static void config_report_unknown_key(int line, const char *name)
{
	const char *key = strrchr(name, '.');
	const char *found = NULL;
	size_t index, count = 0;

	key = key ? key + 1 : name;
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const char *dot = strchr(config_settings[index].name, '.');

		if (dot && !strcmp(dot + 1, key))
		{
			found = config_settings[index].name;
			count++;
		}
	}
	/* (only a name one setting has: the section is then sure) */
	if (count == 1)
	{
		platform_log("config.toml line %d: unknown setting %s, ignored (%s goes under [%.*s])", line, name, key,
			(int)(strchr(found, '.') - found), found);
	}
	else
	{
		platform_log("config.toml line %d: unknown setting %s, ignored", line, name);
	}
}

/* keys in the file that are no setting, likely misspelt */
static void config_report_unknown_keys(toml_datum_t table)
{
	int section_index;

	for (section_index = 0; section_index < table.u.tab.size; section_index++)
	{
		toml_datum_t section = table.u.tab.value[section_index];
		int key_index;

		if (!strcmp(table.u.tab.key[section_index], "config_version"))
			continue;
		if (section.type != TOML_TABLE)
		{
			config_report_unknown_key(section.lineno, table.u.tab.key[section_index]);
			continue;
		}
		for (key_index = 0; key_index < section.u.tab.size; key_index++)
		{
			char name[128];

			snprintf(name, sizeof(name), "%s.%s", table.u.tab.key[section_index], section.u.tab.key[key_index]);
			if (config_setting_index(name) < 0)
				config_report_unknown_key(section.u.tab.value[key_index].lineno, name);
		}
	}
}

/* ---------- older files */

/* the settings whose default an earlier version had otherwise, with that
default as it was written */
static const struct
{
	const char *name;
	const char *default_value;
	/* whether a file of the current layout holding it is moved to the
	current default too (the value was never a choice a player made) */
	int follow;
	/* for an existing file that has the setting at the old default (or not
	set): the change in words, to ask about it (config_find_new_defaults) */
	const char *change;
} config_old_defaults[] =
{
	/* upstream's, which ChupathingyCE kept until v0.5.2b */
	{ "display.menus", "\"pc\"", 0, NULL },
	/* the Xbox's menus, the default until v0.8.0 (a file that names them
	keeps them) */
#ifndef HALO_BROWSER
	/* (port, this fork: not the browser's, whose default is the PC menus) */
	{ "display.menus", "\"xbox\"", 0,
		"The Chupathingy menus" },
#endif
	/* the Xbox's, until the per-platform defaults of v0.8.0 (a file that
	names them keeps them) */
	{ "display.anti_aliasing", "\"off\"", 0, "Smoother edges (2x multisampling)" },
	{ "display.shadow_resolution", "128", 0, "Sharper shadows (256)" },
	/* asking to join a copied invite, the default until v0.8.0c: a file
	that names "ask" keeps it, one that left it commented out follows
	"never" without a question */
	{ "network.join_from_clipboard", "\"ask\"", 0, NULL },
	/* the community site's first address, the default until stats.chupathingy.org
	(the same server, so a file that holds it follows without a question) */
	{ "network.browser_url", "\"https://halo.milenko.org\"", 1, NULL },
#if defined(HALO_ANDROID) && !defined(HALO_BROWSER)
	/* (port, this fork: not the browser's, which keeps 512) */
	{ "audio.buffer_frames", "512", 0, "A larger audio buffer (1024)" },
#endif
#ifndef HALO_CONSOLE_NO_DISCORD
	/* OpenCE's Discord application, which ChupathingyCE kept until v0.7.5c
	(Discord showed "OpenCE" as being played) */
	{ "discord.application_id", "\"1553978809840050229\"", 1, NULL },
#endif
#ifdef HALO_BROWSER
	/* the browser's internet play, which its page turned off until 2026-10-18
	(its machinery is still off: p2p_native_online): never a player's choice,
	shown again as the default */
	{ "network.online", "false", 1, NULL },
#endif
};

#define NUMBER_OF_CONFIG_OLD_DEFAULTS (sizeof(config_old_defaults) / sizeof(config_old_defaults[0]))

/* the settings an earlier version had under another name, whose value the
player chose goes on under the new one (config_forward_renamed) */
static const struct
{
	const char *old_name;
	const char *name;
} config_renamed[] =
{
	/* the loading screen's one least time, until v0.8.0b: a campaign
	level's now (a multiplayer game's starts at its own default) */
	{ "display.loading_screen_min_seconds", "display.loading_screen_campaign_seconds" },
};

#define NUMBER_OF_CONFIG_RENAMED (sizeof(config_renamed) / sizeof(config_renamed[0]))

/* whether the file's value is the default written as text */
static int config_datum_is(toml_datum_t datum, enum config_type type, const char *text)
{
	struct config_value value = { 0, 0, 0.0, NULL };
	size_t length = strlen(text);

	switch (type)
	{
	case _config_boolean:
		config_set_from_text(&value, type, text);
		return datum.type == TOML_BOOLEAN && !datum.u.boolean == !value.boolean;
	case _config_integer:
		config_set_from_text(&value, type, text);
		return datum.type == TOML_INT64 && datum.u.int64 == value.integer;
	case _config_real:
		config_set_from_text(&value, type, text);
		return (datum.type == TOML_FP64 && datum.u.fp64 == value.real) ||
			(datum.type == TOML_INT64 && (double)datum.u.int64 == value.real);
	case _config_string:
		/* (a TOML basic string without escapes) */
		return datum.type == TOML_STRING && length >= 2 && strlen(datum.u.s) == length - 2 &&
			!strncmp(datum.u.s, text + 1, length - 2);
	}
	return 0;
}

/* a setting's line the player changed but left commented out, "# key =
value" with a value other than the default written there: it does nothing,
which the log says (the file as the game writes it, every line at its
default, says nothing). A setting given a value elsewhere in the file is
left alone */
static void config_report_commented(const char *text, toml_datum_t table)
{
	char section[64] = "";
	const char *line;
	int line_number = 1;

	for (line = text; *line; line_number++)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;
		size_t index;

		for (index = 0; !config_line_section(line, end, section, sizeof(section)) &&
			index < NUMBER_OF_CONFIG_SETTINGS; index++)
		{
			const struct config_setting *setting = &config_settings[index];
			const char *dot = strchr(setting->name, '.');
			const char *value;
			char *probe;
			size_t probe_size;
			toml_result_t parsed;
			int same;

			if (!(setting->platforms & CONFIG_PLATFORM) || !dot ||
				strncmp(section, setting->name, (size_t)(dot - setting->name)) ||
				section[dot - setting->name] != 0 || !config_line_commented_key(line, end, dot + 1))
			{
				continue;
			}
			if (toml_seek(table, setting->name).type != TOML_UNKNOWN || config_pending[index])
				break;
			/* (the value read as TOML: 1.0 is 1.00) */
			value = (const char *)memchr(line, '=', (size_t)(end - line)) + 1;
			probe_size = (size_t)(end - value) + 8;
			probe = malloc(probe_size);
			if (!probe)
				break;
			snprintf(probe, probe_size, "v = %.*s\n", (int)(end - value), value);
			parsed = toml_parse(probe, (int)strlen(probe));
			same = parsed.ok && config_datum_is(toml_seek(parsed.toptab, "v"), setting->type, setting->default_value);
			toml_free(parsed);
			free(probe);
			if (!same)
			{
				platform_log("config.toml line %d: %s is commented out, so it does nothing; remove the '# ' to use it",
					line_number, setting->name);
			}
			break;
		}
		line = next;
	}
}

/* a file from before config_version (text, parsed as table) wrote every
setting at the default of the version that wrote it, so that a default
changed since never reached it: its lines holding this version's default
are commented out at it, as a new file has them, and config_version
written. Any other value stays, being the player's, even an older
version's default (the player may have chosen it: display.menus = "gearbox"),
which the log says. Returns the new text, or NULL if the file has its
version */
static char *config_update_layout(const char *text, toml_datum_t table)
{
	toml_datum_t version = toml_seek(table, "config_version");
	int lines[NUMBER_OF_CONFIG_SETTINGS];
	struct config_text out = { NULL, 0, 0 };
	int line_number = 1, defaults = 0, version_written;
	const char *line;
	size_t index;

	if (version.type == TOML_INT64 && version.u.int64 >= CONFIG_VERSION)
		return NULL;
	/* (an older version's line is changed where it is) */
	version_written = version.type != TOML_UNKNOWN;
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		toml_datum_t datum = toml_seek(table, setting->name);
		size_t old;

		lines[index] = 0;
		if (datum.type == TOML_UNKNOWN)
			continue;
		if (config_datum_is(datum, setting->type, setting->default_value))
		{
			lines[index] = datum.lineno;
			continue;
		}
		for (old = 0; old < NUMBER_OF_CONFIG_OLD_DEFAULTS; old++)
		{
			if (!strcmp(config_old_defaults[old].name, setting->name) &&
				config_datum_is(datum, setting->type, config_old_defaults[old].default_value))
			{
				platform_log("settings: %s = %s is kept (an older version's default; this version's is %s, which "
					"commenting the line out follows)", setting->name,
					config_old_defaults[old].default_value, setting->default_value);
			}
		}
	}
	for (line = text; *line; line_number++)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;
		char section[64];
		long setting_index = -1;

		for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
		{
			/* (only the line "key = value" in the section: not a dotted key
			or an inline table, which hold more) */
			if (lines[index] == line_number &&
				config_line_key(line, end, strchr(config_settings[index].name, '.') + 1))
			{
				setting_index = (long)index;
			}
		}
		if (version.type != TOML_UNKNOWN && version.lineno == line_number &&
			config_line_key(line, end, "config_version"))
		{
			config_append(&out, CONFIG_VERSION_LINE);
		}
		else if (setting_index >= 0)
		{
			const struct config_setting *setting = &config_settings[setting_index];
			char buffer[256];

			snprintf(buffer, sizeof(buffer), "# %s = %s\n", strchr(setting->name, '.') + 1, setting->default_value);
			config_append(&out, buffer);
			defaults++;
		}
		else
		{
			char *copy;

			/* (the version goes before the first section, as TOML has it,
			after the blank line that is there) */
			if (!version_written && config_line_section(line, end, section, sizeof(section)))
			{
				config_append(&out, CONFIG_VERSION_LINES "\n");
				version_written = 1;
			}
			copy = config_copy(line, (size_t)(next - line));
			if (copy)
				config_append(&out, copy);
			free(copy);
		}
		line = next;
	}
	if (!version_written)
	{
		if (out.length && out.buffer[out.length - 1] != '\n')
			config_append(&out, "\n");
		config_append(&out, CONFIG_VERSION_TEXT);
	}
	platform_log("settings: config.toml updated (config_version %d): %d lines at this version's default commented "
		"out, to follow it; every other value is kept", CONFIG_VERSION, defaults);
	return out.buffer;
}

/* whether the value is the setting's default, or one an earlier version had */
static int config_datum_is_a_default(toml_datum_t datum, const struct config_setting *setting)
{
	size_t old;

	if (config_datum_is(datum, setting->type, setting->default_value))
		return 1;
	for (old = 0; old < NUMBER_OF_CONFIG_OLD_DEFAULTS; old++)
	{
		if (!strcmp(config_old_defaults[old].name, setting->name) &&
			config_datum_is(datum, setting->type, config_old_defaults[old].default_value))
		{
			return 1;
		}
	}
	return 0;
}

/* whether text has the setting commented out at a default in its section,
"# key = ..." */
static int config_text_has_commented_key(const char *text, const char *name)
{
	const char *dot = strchr(name, '.');
	char section[64], current[64] = "";
	const char *line;

	snprintf(section, sizeof(section), "%.*s", (int)(dot - name), name);
	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");

		if (!config_line_section(line, end, current, sizeof(current)) && !strcmp(current, section) &&
			config_line_commented_key(line, end, dot + 1))
		{
			return 1;
		}
		line = *end ? end + 1 : end;
	}
	return 0;
}

/* whether the line (from its '#') is a setting commented out, "# key = ..." */
static int config_line_is_commented_setting(const char *line)
{
	const char *start;

	line++;
	while (*line == ' ' || *line == '\t')
		line++;
	start = line;
	while ((*line >= 'a' && *line <= 'z') || (*line >= '0' && *line <= '9') || *line == '_')
		line++;
	if (line == start)
		return 0;
	while (*line == ' ' || *line == '\t')
		line++;
	return *line == '=';
}

/* a version from before config_version, run on a file of this layout, sees
the settings commented out at their default as missing, and adds each as a
value at its own default (with its comment, after a blank line) at the end
of its section: the file then holds the setting twice, "# key = default"
from this layout and "key = value" from the older version, and the value
pins the older version's default. Such a value that is a default, this
version's or an older one, is taken out with its comment, so that the
setting follows this version's default again; a value that is no default
stays, being the player's. Returns the new text, or NULL if there was none */
static char *config_drop_added_defaults(const char *text, toml_datum_t table)
{
	toml_datum_t version = toml_seek(table, "config_version");
	int lines[NUMBER_OF_CONFIG_SETTINGS];
	struct config_text out = { NULL, 0, 0 };
	int line_number = 1, dropped = 0, any = 0;
	const char *line;
	size_t index;

	if (version.type != TOML_INT64 || version.u.int64 < 2)
		return NULL;
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		toml_datum_t datum = toml_seek(table, setting->name);

		lines[index] = 0;
		if (datum.type != TOML_UNKNOWN && config_datum_is_a_default(datum, setting) &&
			config_text_has_commented_key(text, setting->name))
		{
			lines[index] = datum.lineno;
			any = 1;
		}
	}
	if (!any)
		return NULL;
	for (line = text; *line; line_number++)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;
		int drop = 0;

		for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS && !drop; index++)
		{
			drop = lines[index] == line_number &&
				config_line_key(line, end, strchr(config_settings[index].name, '.') + 1);
		}
		if (drop)
		{
			/* (its comment above it, and the blank line before that) */
			while (out.length)
			{
				size_t start = out.length - 1;
				const char *first;

				while (start > 0 && out.buffer[start - 1] != '\n')
					start--;
				for (first = out.buffer + start; *first == ' ' || *first == '\t'; first++)
				{
				}
				/* (not another setting's line, "# key = value") */
				if (*first != '#' || config_line_is_commented_setting(first))
					break;
				out.length = start;
				out.buffer[start] = 0;
			}
			if (out.length >= 2 && out.buffer[out.length - 1] == '\n' && out.buffer[out.length - 2] == '\n')
				out.buffer[--out.length] = 0;
			dropped++;
		}
		else
		{
			char *copy = config_copy(line, (size_t)(next - line));

			if (copy)
				config_append(&out, copy);
			free(copy);
		}
		line = next;
	}
	platform_log("settings: config.toml held %d settings twice, commented out and as a default an older version "
		"added; those values are taken out, to follow this version's defaults", dropped);
	return out.buffer;
}

/* a default's text without its quotes, as config_write takes it */
static void config_unquote(const char *text, char *out, size_t size)
{
	size_t length = strlen(text);

	if (length >= 2 && text[0] == '"')
	{
		text++;
		length -= 2;
	}
	if (length >= size)
		length = size - 1;
	memcpy(out, text, length);
	out[length] = 0;
}

#ifndef HALO_SERVER
/* whether the setting is commented out in text ("# key = ...") at the value
(a default as written): 1 if so, 0 if at another, -1 if it is not there */
static int config_commented_value_is(const char *text, const struct config_setting *setting, const char *value_text)
{
	const char *dot = strchr(setting->name, '.');
	char section[64], current[64] = "";
	const char *line;

	snprintf(section, sizeof(section), "%.*s", (int)(dot - setting->name), setting->name);
	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");

		if (!config_line_section(line, end, current, sizeof(current)) && !strcmp(current, section) &&
			config_line_commented_key(line, end, dot + 1))
		{
			const char *value = (const char *)memchr(line, '=', (size_t)(end - line)) + 1;
			size_t probe_size = (size_t)(end - value) + 8;
			char *probe = malloc(probe_size);
			toml_result_t parsed;
			int same;

			if (!probe)
				return 0;
			snprintf(probe, probe_size, "v = %.*s\n", (int)(end - value), value);
			parsed = toml_parse(probe, (int)strlen(probe));
			same = parsed.ok && config_datum_is(toml_seek(parsed.toptab, "v"), setting->type, value_text);
			toml_free(parsed);
			free(probe);
			return same;
		}
		line = *end ? end + 1 : end;
	}
	return -1;
}

#endif

/* An existing file (text, parsed as table) whose settings this version's
defaults would change: those at an older default that follow the default (at
the old one, commented out, or not there at all, the file being from before
the setting), and those an older version wrote as a value beside the
commented one (config_drop_added_defaults). They are listed in
config_pending, to be asked about once (config_new_defaults_pending); the
file is left as it is, and the settings keep their old values, until the
player answers. A file that was answered (game.defaults_answered) has none.
The dedicated server keeps its own defaults and is never asked. */
static void config_find_new_defaults(const char *text, toml_datum_t table)
{
#ifndef HALO_SERVER
	toml_datum_t version = toml_seek(table, "config_version");
	toml_datum_t answered = toml_seek(table, "game.defaults_answered");
	size_t old;

	if (answered.type == TOML_INT64 && answered.u.int64 >= CONFIG_DEFAULTS_GENERATION)
		return;
	for (old = 0; old < NUMBER_OF_CONFIG_OLD_DEFAULTS; old++)
	{
		long index = config_old_defaults[old].change ? config_setting_index(config_old_defaults[old].name) : -1;
		const struct config_setting *setting;
		toml_datum_t datum;

		if (index < 0)
			continue;
		setting = &config_settings[index];
		if (!(setting->platforms & CONFIG_PLATFORM) || !strcmp(config_old_defaults[old].default_value, setting->default_value))
			continue;
		datum = toml_seek(table, setting->name);
		if (datum.type != TOML_UNKNOWN)
		{
			if (version.type == TOML_INT64 && version.u.int64 >= 2 &&
				config_datum_is(datum, setting->type, config_old_defaults[old].default_value) &&
				config_text_has_commented_key(text, setting->name))
			{
				config_pending[index] = 2;
			}
		}
		else if (config_commented_value_is(text, setting, config_old_defaults[old].default_value) != 0)
		{
			config_pending[index] = 1;
		}
		if (config_pending[index])
			config_pending_old[index] = config_old_defaults[old].default_value;
	}
#else
	(void)text;
	(void)table;
#endif
}

/* whether the line is blank */
static int config_line_blank(const char *line, const char *end)
{
	while (line < end && (*line == ' ' || *line == '\t' || *line == '\r'))
		line++;
	return line == end;
}

/* whether the line is a comment */
static int config_line_comment(const char *line, const char *end)
{
	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	return line < end && *line == '#';
}

/* a file that holds, for a setting whose old default is followed, that
default as a value: the setting's lines are taken out of it (the value, and
a copy commented out at that default with its help text of that day, each
with the comment above it and one blank line), and config_add_missing then
writes it again as this version has it, commented out at the current
default, so that it follows. Returns the new text, or NULL if there was
nothing to change */
static char *config_follow_old_defaults(const char *text, toml_datum_t table)
{
	char *result = NULL;
	size_t old;

	for (old = 0; old < NUMBER_OF_CONFIG_OLD_DEFAULTS; old++)
	{
		const char *source = result ? result : text;
		const struct config_setting *setting;
		struct config_text out = { NULL, 0, 0 };
		toml_datum_t datum;
		const char *dot, *line;
		const char **starts;
		size_t count = 0, capacity = 0, line_index, found = 0;
		unsigned char *drop;
		char section[64], current[64] = "";
		long index = config_old_defaults[old].follow ? config_setting_index(config_old_defaults[old].name) : -1;

		if (index < 0)
			continue;
		setting = &config_settings[index];
		datum = toml_seek(table, setting->name);
		if (!config_datum_is(datum, setting->type, config_old_defaults[old].default_value) ||
			config_datum_is(datum, setting->type, setting->default_value))
			continue;
		dot = strchr(setting->name, '.');
		snprintf(section, sizeof(section), "%.*s", (int)(dot - setting->name), setting->name);
		for (line = source; *line; line += strcspn(line, "\n") + (line[strcspn(line, "\n")] ? 1 : 0))
			capacity++;
		starts = malloc((capacity + 1) * sizeof(*starts));
		drop = calloc(capacity + 1, 1);
		if (!starts || !drop)
		{
			free(starts);
			free(drop);
			continue;
		}
		for (line = source; *line; line += strcspn(line, "\n") + (line[strcspn(line, "\n")] ? 1 : 0))
			starts[count++] = line;
		starts[count] = line;
		for (line_index = 0; line_index < count; line_index++)
		{
			const char *start = starts[line_index];
			const char *end = start + strcspn(start, "\n");

			if (config_line_section(start, end, current, sizeof(current)))
				continue;
			if (!strcmp(current, section) &&
				(config_line_key(start, end, dot + 1) || config_line_commented_key(start, end, dot + 1)))
			{
				size_t back = line_index;

				drop[line_index] = 1;
				found++;
				while (back > 0 && config_line_comment(starts[back - 1], starts[back - 1] + strcspn(starts[back - 1], "\n")))
					drop[--back] = 1;
				if (back > 0 && config_line_blank(starts[back - 1], starts[back - 1] + strcspn(starts[back - 1], "\n")))
					drop[back - 1] = 1;
			}
		}
		if (found)
		{
			for (line_index = 0; line_index < count; line_index++)
			{
				if (!drop[line_index])
				{
					char *copy = config_copy(starts[line_index], (size_t)(starts[line_index + 1] - starts[line_index]));

					if (copy)
						config_append(&out, copy);
					free(copy);
				}
			}
			if (out.buffer)
			{
				free(result);
				result = out.buffer;
				platform_log("settings: %s = %s was an older version's default; it follows this version's (%s) now",
					setting->name, config_old_defaults[old].default_value, setting->default_value);
			}
		}
		free(starts);
		free(drop);
	}
	return result;
}

/* a file that holds a setting under its older name (config_renamed): that
name's lines are taken out of it (its value, and its line commented out at
the default with its help text of that day, each with the comment above it
and one blank line), and a value the player chose there goes in under the
new name where the old one was, with the new setting's help text, as
config_write writes a choice; a value already given under the new name is
the one kept. A setting not chosen (only commented out) is left to
config_add_missing, which writes it at its default. Returns the new text,
or NULL if there was nothing to change */
static char *config_forward_renamed(const char *text, toml_datum_t table)
{
	char *result = NULL;
	size_t renamed;

	for (renamed = 0; renamed < NUMBER_OF_CONFIG_RENAMED; renamed++)
	{
		const char *source = result ? result : text;
		const char *old_name = config_renamed[renamed].old_name;
		const char *old_dot = strchr(old_name, '.');
		long index = config_setting_index(config_renamed[renamed].name);
		const struct config_setting *setting;
		struct config_text out = { NULL, 0, 0 };
		struct config_text block = { NULL, 0, 0 };
		const char *line;
		const char **starts;
		unsigned char *drop;
		size_t count = 0, capacity = 0, line_index, value_line = (size_t)-1, found = 0;
		char section[64], current[64] = "", value[256] = "";
		int forward;

		if (index < 0 || !old_dot)
			continue;
		setting = &config_settings[index];
		forward = toml_seek(table, old_name).type != TOML_UNKNOWN &&
			toml_seek(table, setting->name).type == TOML_UNKNOWN;
		snprintf(section, sizeof(section), "%.*s", (int)(old_dot - old_name), old_name);
		for (line = source; *line; line += strcspn(line, "\n") + (line[strcspn(line, "\n")] ? 1 : 0))
			capacity++;
		starts = malloc((capacity + 1) * sizeof(*starts));
		drop = calloc(capacity + 1, 1);
		if (!starts || !drop)
		{
			free(starts);
			free(drop);
			continue;
		}
		for (line = source; *line; line += strcspn(line, "\n") + (line[strcspn(line, "\n")] ? 1 : 0))
			starts[count++] = line;
		starts[count] = line;
		for (line_index = 0; line_index < count; line_index++)
		{
			const char *start = starts[line_index];
			const char *end = start + strcspn(start, "\n");
			int is_value;

			if (config_line_section(start, end, current, sizeof(current)))
				continue;
			is_value = config_line_key(start, end, old_dot + 1);
			if (strcmp(current, section) || (!is_value && !config_line_commented_key(start, end, old_dot + 1)))
				continue;
			{
				size_t back = line_index;

				drop[line_index] = 1;
				found++;
				while (back > 0 && config_line_comment(starts[back - 1], starts[back - 1] + strcspn(starts[back - 1], "\n")))
					drop[--back] = 1;
				if (back > 0 && config_line_blank(starts[back - 1], starts[back - 1] + strcspn(starts[back - 1], "\n")))
					drop[back - 1] = 1;
			}
			/* (the value as the player wrote it) */
			if (is_value && value_line == (size_t)-1)
			{
				const char *text_start = (const char *)memchr(start, '=', (size_t)(end - start)) + 1;
				const char *text_end = end;

				while (text_start < text_end && (*text_start == ' ' || *text_start == '\t'))
					text_start++;
				while (text_end > text_start && (text_end[-1] == ' ' || text_end[-1] == '\t' || text_end[-1] == '\r'))
					text_end--;
				if ((size_t)(text_end - text_start) < sizeof(value))
				{
					memcpy(value, text_start, (size_t)(text_end - text_start));
					value[text_end - text_start] = 0;
					value_line = line_index;
				}
			}
		}
		if (forward && value_line != (size_t)-1)
		{
			char buffer[320];
			size_t last;

			/* the new setting's lines, its key taken out of its comment */
			config_append_setting(&block, setting);
			if (block.buffer)
			{
				for (last = block.length - 1; last > 0 && block.buffer[last - 1] != '\n'; last--)
					;
				block.buffer[last] = 0;
				block.length = last;
				snprintf(buffer, sizeof(buffer), "%s = %s\n", strchr(setting->name, '.') + 1, value);
				config_append(&block, buffer);
			}
		}
		if (found)
		{
			for (line_index = 0; line_index < count; line_index++)
			{
				if (line_index == value_line && block.buffer)
					config_append(&out, block.buffer);
				if (!drop[line_index])
				{
					char *copy = config_copy(starts[line_index], (size_t)(starts[line_index + 1] - starts[line_index]));

					if (copy)
						config_append(&out, copy);
					free(copy);
				}
			}
			if (out.buffer)
			{
				free(result);
				result = out.buffer;
				if (block.buffer)
					platform_log("settings: %s = %s goes on as %s", old_name, value, setting->name);
				else
					platform_log("settings: %s (an older version's) taken out of config.toml", old_name);
			}
		}
		free(block.buffer);
		free(starts);
		free(drop);
	}
	return result;
}

/* ---------- words */

/* the settings that take one of a few words: one in any case is taken (as
written here), and another is said once in the log */
static const struct
{
	const char *name;
	/* NULL ended; "" where empty is one (display.shield_color's are 19) */
	const char *words[24];
	/* what the game does with another, for the log (NULL: what reads the
	setting says it) */
	const char *otherwise;
} config_words[] =
{
	{ "display.mode", { "", "fullscreen", "borderless", "windowed", NULL }, "display.fullscreen's" },
	{ "display.resolution_scaling", { "native", "original", NULL }, "\"native\"" },
	{ "display.anti_aliasing", { "off", "fxaa", "smaa", "ssaa2x", "msaa2x", "msaa4x", "msaa8x", NULL }, NULL },
	{ "display.menus", { "xbox", "gearbox", "pc", "chupathingy", NULL }, NULL },
	{ "display.player_names", { "all", "allies", "allys", "enemies", "none", NULL }, "\"all\"" },
	{ "display.scoreboard_team_layout", { "teams", "score", NULL }, "\"teams\"" },
	{ "display.scoreboard_platforms", { "auto", "on", "off", NULL }, "\"auto\"" },
	{ "display.follow_camera", { "long_waits", "always", "off", NULL }, "\"long_waits\"" },
	{ "display.texture_decode", { "auto", "gpu", "cpu", NULL }, "\"auto\"" },
	{ "audio.resampling", { "sinc", "linear", NULL }, "\"sinc\"" },
	{ "input.touch_controls", { "on", "auto", "off", NULL }, "\"on\"" },
	{ "game.console_log", { "important", "all", "none", NULL }, "\"important\"" },
	{ "game.move_old_map_folders", { "ask", "yes", "no", NULL }, "\"ask\"" },
	{ "network.protocol", { "auto", "opence", "delta", NULL }, NULL },
	{ "network.map_downloads", { "ask", "always", "never", NULL }, "\"ask\"" },
	{ "network.fair_play", { "refuse", "log", "off", NULL }, "\"refuse\"" },
	{ "network.playlist_downloads", { "ask", "always", "never", NULL }, "\"ask\"" },
	{ "network.join_from_clipboard", { "ask", "always", "never", NULL }, "\"never\"" },
	{ "network.team_change", { "balanced", "allow", "off", NULL }, "\"balanced\"" },
#ifndef HALO_CONSOLE_NO_DISCORD
	{ "discord.presence", { "full", "minimal", "off", NULL }, "\"full\"" },
#endif
	{ "network.platform_limits", { "on", "off", NULL }, "\"on\"" },
	{ "console.delta", { "auto", "on", "off", NULL }, "\"auto\"" },
	{ "network.coop_friendly_fire", { "off", "on", "shields_only", "explosives_only", NULL }, NULL },
	{ "network.shield_effects", { "stock", "glow", "light_show", "overshield", "glow_overshield",
		"light_show_overshield", "players", NULL }, NULL },
	{ "network.coop_enemies_mode", { "none", "per_player", "multiplier", NULL }, "\"none\"" },
	{ "crash_reports.upload", { "ask", "yes", "no", NULL }, "\"yes\"" },
	{ "display.hud_button_style", { "classic", "gems", "menu_gems", NULL }, NULL },
	{ "display.chat_style", { "colors", "white", NULL }, NULL },
	{ "display.shield_glow_intensity", { "default", "light_show", NULL }, NULL },
	{ "display.shield_color", { "default", "white", "black", "red", "blue", "gray", "yellow", "green", "pink",
		"purple", "cyan", "cobalt", "orange", "teal", "sage", "brown", "tan", "maroon", "salmon", NULL }, NULL },
	{ "audio.resampler", { "sinc", "linear", NULL }, NULL },
	{ "audio.voice_chat", { "push_to_talk", "open_mic", "off", NULL }, NULL },
	{ "game.language", { "", "ja", "de", "fr", "es", "it", NULL }, NULL },
	{ "network.text_chat", { "on", "links", "off", NULL }, NULL },
	{ "server.chat_filter", { "off", "censor", "block", NULL }, NULL },
	{ "server.chat_filter_slurs", { "off", "severe", "strong", "mild", NULL }, NULL },
	{ "server.chat_filter_swearing", { "off", "severe", "strong", "mild", NULL }, NULL },
	{ "server.chat_filter_insults", { "off", "severe", "strong", "mild", NULL }, NULL },
	{ "network.voice_mode", { "off", "team_proximity", "team_enemy_proximity", "team_global",
		"team_global_enemy_proximity", NULL }, NULL },
	{ "streamer.slurs", { "off", "severe", "strong", "mild", NULL }, NULL },
	{ "streamer.swearing", { "off", "severe", "strong", "mild", NULL }, NULL },
	{ "streamer.insults", { "off", "severe", "strong", "mild", NULL }, NULL },
};

#define NUMBER_OF_CONFIG_WORDS (sizeof(config_words) / sizeof(config_words[0]))

/* an older version's value of a word setting, read as the word that took
its place (a setting that was true or false: the file's true and false are
taken as these words' "true" and "false": config_set_from_file) */
static const struct
{
	const char *name;
	const char *old_word;
	const char *word;
} config_old_words[] =
{
	{ "network.join_from_clipboard", "true", "always" },
	{ "network.join_from_clipboard", "false", "never" },
};

#define NUMBER_OF_CONFIG_OLD_WORDS (sizeof(config_old_words) / sizeof(config_old_words[0]))

/* whether the setting once was true or false, which the file may still say */
static int config_was_boolean(const char *name)
{
	size_t index;

	for (index = 0; index < NUMBER_OF_CONFIG_OLD_WORDS; index++)
	{
		if (!strcmp(config_old_words[index].name, name))
			return 1;
	}
	return 0;
}

/* the word settings' values as their readers compare them (lowercase:
"Borderless" is "borderless"), and the log told of one that is none of
its words */
static void config_check_words(void)
{
	size_t word_index;

	for (word_index = 0; word_index < NUMBER_OF_CONFIG_WORDS; word_index++)
	{
		long index = config_setting_index(config_words[word_index].name);
		const char *const *words = config_words[word_index].words;
		struct config_value *value;
		char list[160] = "";
		size_t word;

		if (index < 0 || !(config_settings[index].platforms & CONFIG_PLATFORM))
			continue;
		value = &config_values[index];
		for (word = 0; word < NUMBER_OF_CONFIG_OLD_WORDS; word++)
		{
			if (!strcmp(config_old_words[word].name, config_words[word_index].name) &&
				!SDL_strncasecmp(value->string, config_old_words[word].old_word, strlen(config_old_words[word].old_word) + 1))
			{
				free(value->string);
				value->string = strdup(config_old_words[word].word);
				break;
			}
		}
		for (word = 0; words[word] && SDL_strncasecmp(value->string, words[word], strlen(words[word]) + 1); word++)
		{
		}
		if (words[word])
		{
			if (strcmp(value->string, words[word]))
			{
				free(value->string);
				value->string = strdup(words[word]);
			}
			continue;
		}
		if (!config_words[word_index].otherwise)
			continue;
		/* ("a", "b" or "c", and "or empty" for "") */
		for (word = 0; words[word]; word++)
		{
			size_t length = strlen(list);

			if (!words[word][0])
				continue;
			snprintf(list + length, sizeof(list) - length, "%s\"%s\"",
				!length ? "" : !words[word + 1] && words[0][0] ? " or " : ", ", words[word]);
		}
		if (!words[0][0])
			snprintf(list + strlen(list), sizeof(list) - strlen(list), " or empty");
		platform_log("settings: %s = \"%s\" is not %s; using %s", config_words[word_index].name, value->string, list,
			config_words[word_index].otherwise);
	}
}

/* the most lines of a file that cannot be read that are skipped to read
the rest */
#define CONFIG_MAXIMUM_SKIPPED_LINES 16

/* the line a parse error is on ("(line 12) ..."), or 0 */
static int config_error_line(const char *message)
{
	const char *at = strstr(message, "(line ");
	int line = 0;

	if (!at || sscanf(at, "(line %d)", &line) != 1)
		return 0;
	return line;
}

/* the line of text that cannot be read, said in the log with what likely
makes it so, and emptied (its number kept for the lines after it): returns
whether it was there */
static int config_skip_line(char *text, int line_number, const char *message)
{
	char *line = text, *end, *value, *value_end;
	const char *reason = strchr(message, ')');
	char key[64] = "";
	char hint[160] = "";
	int number;

	for (number = 1; number < line_number && *line; number++)
		line += strcspn(line, "\n") + (line[strcspn(line, "\n")] ? 1 : 0);
	if (number != line_number || !*line)
		return 0;
	end = line + strcspn(line, "\n");
	reason = reason ? reason + 1 : message;
	while (*reason == ' ')
		reason++;
	/* "key = value": the value as written, to the end of the line or a
	comment */
	value = memchr(line, '=', (size_t)(end - line));
	if (value)
	{
		char *key_start = line, *key_end = value;

		while (key_start < key_end && (*key_start == ' ' || *key_start == '\t'))
			key_start++;
		while (key_end > key_start && (key_end[-1] == ' ' || key_end[-1] == '\t'))
			key_end--;
		snprintf(key, sizeof(key), "%.*s", (int)(key_end - key_start), key_start);
		value++;
		while (value < end && (*value == ' ' || *value == '\t'))
			value++;
		value_end = value + strcspn(value, "#\n");
		if (value_end > end)
			value_end = end;
		while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t' || value_end[-1] == '\r'))
			value_end--;
		if ((value_end - value == 4 && !SDL_strncasecmp(value, "true", 4)) ||
			(value_end - value == 5 && !SDL_strncasecmp(value, "false", 5)))
		{
			snprintf(hint, sizeof(hint), ": true and false are lowercase, %s = %.*s", key, (int)(value_end - value),
				value_end - value == 4 ? "true" : "false");
		}
		else if (value < value_end && isalpha((unsigned char)*value))
		{
			snprintf(hint, sizeof(hint), ": text needs quotes, %s = \"%.*s\"", key, (int)(value_end - value), value);
		}
	}
	platform_log("config.toml line %d cannot be read (%s), so it is skipped%s", line_number, reason, hint);
	memset(line, ' ', (size_t)(end - line));
	return 1;
}

#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
/* a config.toml the game never reads, likely edited in its place: said in
the log, with the one it reads (path) */
static void config_report_unread(const char *path)
{
	char other[1100];
#ifdef __APPLE__
	/* beside the application, or in it (the game's is in Application
	Support) */
	const char *base = SDL_GetBasePath();
	const char *bundle = base ? strstr(base, ".app/Contents/") : NULL;

	if (bundle)
	{
		const char *folder_end = bundle;

		while (folder_end > base && folder_end[-1] != '/')
			folder_end--;
		snprintf(other, sizeof(other), "%sconfig.toml", base);
		if (SDL_GetPathInfo(other, NULL))
			platform_log("settings: %s is not read; the game's settings are %s", other, path);
		snprintf(other, sizeof(other), "%.*sconfig.toml", (int)(folder_end - base), base);
		if (SDL_GetPathInfo(other, NULL))
			platform_log("settings: %s is not read; the game's settings are %s", other, path);
	}
#elif !defined(_WIN32)
	/* in the folder the game was started from, when that is not the
	executable's (the game's is beside the executable) */
	char folder[1024];

	if (getcwd(folder, sizeof(folder)))
	{
		snprintf(other, sizeof(other), "%s/config.toml", folder);
		if (strcmp(other, path) && SDL_GetPathInfo(other, NULL))
		{
			SDL_PathInfo mine, theirs;

			/* (the same file by another name is the one read) */
			if (!SDL_GetPathInfo(path, &mine) || !SDL_GetPathInfo(other, &theirs) ||
				mine.size != theirs.size || mine.modify_time != theirs.modify_time)
			{
				platform_log("settings: %s is not read; the game's settings are %s", other, path);
			}
		}
	}
#endif
	/* the file saved with .txt after it, by an editor that adds it (and
	Windows Explorer hides it) */
	snprintf(other, sizeof(other), "%s.txt", path);
	if (SDL_GetPathInfo(other, NULL))
	{
#ifdef _WIN32
		platform_log("settings: %s is not read: Windows saved it as config.toml.txt; rename it to config.toml", other);
#else
		platform_log("settings: %s is not read; rename it to config.toml", other);
#endif
	}
}
#endif

/* the passwords a file may hold instead of their variable (a container's
secrets): <variable>_FILE names it, and its first line is the value */
static const char *const config_secret_files[] =
{
	"server.password", "server.web_password", "server.web_owner_password", "server.web_moderator_password",
	"server.web_viewer_password",
};
#define NUMBER_OF_CONFIG_SECRET_FILES (sizeof(config_secret_files) / sizeof(config_secret_files[0]))

/* the value of the setting's <variable>_FILE (read once, at the first
question: a file changed after the start is not read again), NULL if
there is none, it cannot be read or its first line is empty */
static const char *config_environment_file(const struct config_setting *setting)
{
	static char values[NUMBER_OF_CONFIG_SECRET_FILES][512];
	static signed char read[NUMBER_OF_CONFIG_SECRET_FILES];
	size_t index;

	for (index = 0; index < NUMBER_OF_CONFIG_SECRET_FILES && strcmp(config_secret_files[index], setting->name); index++)
	{
	}
	if (index == NUMBER_OF_CONFIG_SECRET_FILES || !setting->environment)
		return NULL;
	if (!read[index])
	{
		char variable[96];
		const char *path;
		char *text;
		size_t size = 0, length;

		read[index] = -1;
		snprintf(variable, sizeof(variable), "%s_FILE", setting->environment);
		path = getenv(variable);
		if (!path || !path[0])
			return NULL;
		text = config_read_file(path, &size);
		if (!text)
		{
			platform_log("settings: %s names %s, which cannot be read: %s is not set by it", variable, path,
				setting->name);
			return NULL;
		}
		/* (the first line, its CR and LF off) */
		length = strcspn(text, "\n");
		if (length && text[length - 1] == '\r')
			length--;
		if (length >= sizeof(values[index]))
		{
			platform_log("settings: the first line of %s (%s) is longer than %d characters: %s is not set by it",
				path, variable, (int)sizeof(values[index]) - 1, setting->name);
			length = 0;
		}
		memcpy(values[index], text, length);
		values[index][length] = 0;
		memset(text, 0, size);
		free(text);
		read[index] = length ? 1 : -1;
	}
	return read[index] > 0 ? values[index] : NULL;
}

/* a setting's environment variable, NULL if it is not set; a dedicated
server's settings' empty ones are not set either (a panel passes every
variable it has, empty ones too: an empty password must not stand for one).
A password's variable that is not set may be read from its file
(<variable>_FILE: config_environment_file) */
static const char *config_environment(const struct config_setting *setting)
{
	const char *environment = setting->environment ? getenv(setting->environment) : NULL;

	if (environment && !environment[0] &&
		(setting->platforms == _platform_server || !strcmp(setting->name, "network.public_address")))
	{
		environment = NULL;
	}
	if (!environment)
		environment = config_environment_file(setting);
	return environment;
}

#if !defined(HALO_ANDROID) && !defined(HALO_WEB)
/* a dedicated server (port/linux/src/server_mode.c) has no window, sound,
microphone or player, never asks anything, and is reached only as its
settings say, whatever the file and the environment say of these (for this
run: nothing of it is written) */
/* halo --probe: no window, sound, update, Discord or menus' test; the
network test's probe, and a deadline (internet play as set: on by default,
which an invite needs) */
static void config_apply_probe_mode(void)
{
	static const char *const forced[][2] =
	{
		{ "debug.null_renderer", "true" },
		{ "debug.hidden_window", "true" },
		{ "display.fullscreen", "false" },
		{ "audio.enabled", "false" },
		{ "audio.voice_chat", "off" },
		{ "network.join_from_clipboard", "false" },
		{ "update.auto", "false" },
		{ "crash_reports.upload", "no" },
		{ "discord.application_id", "" },
		{ "debug.network_test", "probe" },
		{ "server.enabled", "false" },
		{ "debug.test_input", "" },
		{ "debug.menu_open", "" },
		{ "debug.exit_after", "45" },
	};
	size_t index;

	for (index = 0; index < sizeof(forced) / sizeof(forced[0]); index++)
	{
		long found = config_setting_index(forced[index][0]);

		if (found >= 0)
			config_set_from_text(&config_values[found], config_settings[found].type, forced[index][1]);
	}
}

static void config_apply_server_mode(void)
{
	static const char *const forced[][2] =
	{
		{ "server.enabled", "true" },
		{ "debug.null_renderer", "true" },
		{ "debug.hidden_window", "true" },
		{ "display.fullscreen", "false" },
		{ "display.vsync", "false" },
		{ "display.interpolation", "false" },
		{ "display.high_res_hud", "false" },
		{ "display.high_res_text", "false" },
		{ "display.anti_aliasing", "off" },
		{ "audio.enabled", "false" },
		{ "audio.voice_chat", "off" },
		{ "network.join_from_clipboard", "false" },
		{ "update.auto", "false" },
		{ "crash_reports.upload", "no" },
		{ "discord.application_id", "" },
		{ "debug.network_test", "" },
		{ "debug.test_input", "" },
		{ "debug.menu_open", "" },
	};
	const char *tunnel_port = getenv("HALO_NET_TUNNEL_PORT");
	size_t index;

	for (index = 0; index < sizeof(forced) / sizeof(forced[0]); index++)
	{
		long found = config_setting_index(forced[index][0]);

		if (found >= 0)
			config_set_from_text(&config_values[found], config_settings[found].type, forced[index][1]);
	}
	/* (the tunnel's port fixed, Halo PC's 2302, unless one is given: a
	server's players and firewall need to know it; a router asked to
	forward it only as server.upnp says) */
	if ((!tunnel_port || !*tunnel_port) && !config_values[config_setting_index("network.tunnel_port")].integer)
		config_values[config_setting_index("network.tunnel_port")].integer = 2302;
	config_values[config_setting_index("network.allow_upnp")].boolean =
		config_values[config_setting_index("server.upnp")].boolean;
}
#endif

static void config_load(void)
{
	char path[1024];
	size_t size = 0;
	char *text;
	size_t index;

	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const char *default_value = config_settings[index].default_value;

		if (config_settings[index].type == _config_string)
		{
			/* written as a TOML basic string without escapes */
			size_t length = strlen(default_value);

			config_values[index].string = length >= 2 ? config_copy(default_value + 1, length - 2) : strdup("");
		}
		else
		{
			config_set_from_text(&config_values[index], config_settings[index].type, default_value);
		}
	}

	config_path(path, sizeof(path));
#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
	config_report_unread(path);
	/* (port, this fork: the web build reads no command line,
	config_argument) */
#ifndef HALO_WEB
	/* (a dedicated server's file has its settings too: asked for by the
	command line or the environment, or by the file's server.enabled; a
	probe is never one, whatever they say) */
	config_probe_mode = config_argument("--probe", NULL, 0);
	{
		const char *environment = getenv("HALO_SERVER");

		config_server_mode = !config_probe_mode && (config_argument("--server", NULL, 0) ||
			(environment && config_environment_set(environment)));
	}
	/* (a run that only answers its command line, halo --version, writes no
	settings: the file read is left as it is) */
	config_answer_only = config_argument("--help", NULL, 0) || config_argument("--version", NULL, 0) ||
		config_argument("--keygen", NULL, 0) || config_argument("--sign", NULL, 0) || config_probe_mode;
#endif
#endif
	text = config_read_file(path, &size);
	if (text)
	{
		char *joined = config_join_sections(text);
		toml_result_t result;
		int skipped = 0;

		if (joined)
		{
			free(text);
			text = joined;
			size = strlen(text);
		}
		result = toml_parse(text, (int)size);
		/* (a line that cannot be read is skipped, in memory, so that the
		rest is) */
		while (!result.ok && skipped < CONFIG_MAXIMUM_SKIPPED_LINES &&
			config_skip_line(text, config_error_line(result.errmsg), result.errmsg))
		{
			toml_free(result);
			result = toml_parse(text, (int)strlen(text));
			skipped++;
		}
		if (result.ok)
		{
			char *updated = config_update_layout(text, result.toptab);
			char *completed;

			if (joined && !skipped && !config_answer_only)
			{
				if (config_write_file(path, text))
					platform_log("settings: joined the sections %s had twice", path);
				else
					platform_log("settings: cannot write %s", path);
			}
			if (updated)
			{
				/* (read as it is now, the defaults it gave up taking effect) */
				toml_result_t updated_result = toml_parse(updated, (int)strlen(updated));

				if (updated_result.ok)
				{
					toml_free(result);
					free(text);
					result = updated_result;
					text = updated;
				}
				else
				{
					toml_free(updated_result);
					free(updated);
					updated = NULL;
				}
			}
			{
				/* (a value that was an older version's default, followed) */
				char *followed = config_follow_old_defaults(text, result.toptab);
				toml_result_t followed_result;

				if (followed)
				{
					followed_result = toml_parse(followed, (int)strlen(followed));
					if (followed_result.ok)
					{
						toml_free(result);
						free(text);
						result = followed_result;
						text = followed;
						updated = text;
					}
					else
					{
						toml_free(followed_result);
						free(followed);
					}
				}
			}
			{
				/* (a value under a setting's older name, forwarded) */
				char *forwarded = config_forward_renamed(text, result.toptab);
				toml_result_t forwarded_result;

				if (forwarded)
				{
					forwarded_result = toml_parse(forwarded, (int)strlen(forwarded));
					if (forwarded_result.ok)
					{
						toml_free(result);
						free(text);
						result = forwarded_result;
						text = forwarded;
						updated = text;
					}
					else
					{
						toml_free(forwarded_result);
						free(forwarded);
					}
				}
			}
			config_find_new_defaults(text, result.toptab);
			for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
			{
				config_set_from_file(&config_values[index], &config_settings[index], result.toptab);
				config_given[index] = toml_seek(result.toptab, config_settings[index].name).type != TOML_UNKNOWN;
			}
			/* (until the player is asked, the values stay what they were: the
			new defaults take effect only when they say so) */
			for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
			{
				if (config_pending[index])
				{
					char old[64];

					config_unquote(config_pending_old[index], old, sizeof(old));
					config_set_from_text(&config_values[index], config_settings[index].type, old);
				}
			}
#if !defined(HALO_ANDROID) && !defined(HALO_WEB)
			if (config_values[config_setting_index("server.enabled")].boolean && !config_probe_mode)
				config_server_mode = 1;
#endif
			config_report_unknown_keys(result.toptab);
			platform_log("settings: %s", path);
			completed = skipped ? NULL : config_add_missing(text, result.toptab);
			config_report_commented(completed ? completed : text, result.toptab);
			/* (a file with lines skipped is left as the player wrote it, to
			be fixed) */
			if (skipped)
			{
				platform_log("settings: %d line%s of config.toml skipped; every other setting in it is used", skipped,
					skipped == 1 ? "" : "s");
			}
			/* (nothing written by a run that only answers its command line) */
			else if (!config_answer_only && (completed || updated) &&
				!config_write_file(path, completed ? completed : text))
			{
				platform_log("settings: cannot write %s", path);
			}
			free(completed);
		}
		else
		{
			platform_log("config.toml cannot be read (%s): every setting is at its default until that is fixed "
				"(a word value needs quotes: menus = \"pc\")", result.errmsg);
		}
		toml_free(result);
		free(text);
	}
	else if (!config_answer_only)
	{
		char *defaults = config_default_text();

		if (defaults && config_write_file(path, defaults))
			platform_log("settings: wrote the defaults to %s", path);
		else
			platform_log("settings: cannot write %s; using the defaults", path);
		free(defaults);
	}

	/* (the console has no environment) */
#ifndef HALO_XBOX_CONSOLE
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		const char *environment = config_environment(setting);

		/* (one set to "", "0", "false", "no" or "off" is as if it were not:
		HALO_LOG_ADDRESSES=0 never turns a setting on) */
		if (!environment || (setting->environment_style != _environment_value && !config_environment_set(environment)))
			continue;
		config_given[index] = 1;
		switch (setting->environment_style)
		{
		case _environment_value:
			config_set_from_text(&config_values[index], setting->type, environment);
			break;
		case _environment_set_is_true:
			config_values[index].boolean = 1;
			break;
		case _environment_set_is_false:
			config_values[index].boolean = 0;
			break;
		}
	}
#endif
#ifndef HALO_CONSOLE_NO_UPDATER
	/* (a packaged install's package manager updates it: update.auto is off
	and stays off, packaged_install.h; whether the game still looks for a
	new version to say so is the file's or the environment's choice) */
	if (platform_packaged())
	{
		long update = config_setting_index("update.auto");

		if (update >= 0)
		{
			packaged_update_check = config_values[update].boolean;
			config_values[update].boolean = 0;
			config_given[update] = 1;
			platform_log("settings: a packaged install: update.auto is off (the package manager updates the game)");
		}
	}
#endif
#if !defined(HALO_ANDROID) && !defined(HALO_WEB)
	if (config_server_mode)
		config_apply_server_mode();
	else if (config_probe_mode)
		config_apply_probe_mode();
#endif
	config_check_words();
#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
	{
		long mode = config_setting_index("display.mode");
		long fullscreen = config_setting_index("display.fullscreen");

		/* (said once: which of the two the window follows) */
		const char *value = config_values[mode].string;

		if ((!strcmp(value, "fullscreen") || !strcmp(value, "borderless") || !strcmp(value, "windowed")) &&
			config_given[fullscreen])
		{
			platform_log("settings: display.mode = \"%s\" is used; display.fullscreen is ignored",
				config_values[mode].string);
		}
	}
#endif
}

/* the file read, the first time a setting is asked for */
static void config_ensure_loaded(void)
{
	pthread_mutex_lock(&config_lock);
	if (!config_loaded)
	{
		config_load();
		config_loaded = 1;
	}
	pthread_mutex_unlock(&config_lock);
}

static const struct config_value *config_value(const char *name, enum config_type type)
{
	static const struct config_value none = { 0, 0, 0.0, "" };
	long index;

	config_ensure_loaded();
	index = config_setting_index(name);
	if (index < 0 || config_settings[index].type != type)
	{
		/* (said once a name: code asking every frame for a setting this build
		doesn't have once filled the log, and the disk, with the line) */
		static char said[32][64];
		static long said_count;
		long i;
		int known = 0;

		pthread_mutex_lock(&config_lock);
		for (i = 0; i < said_count && !known; i++)
			known = strncmp(said[i], name, sizeof(said[i]) - 1) == 0;
		if (!known && said_count < (long)(sizeof(said) / sizeof(said[0])))
		{
			strncpy(said[said_count], name, sizeof(said[said_count]) - 1);
			said_count++;
		}
		/* (past the first 32 names, none: a flood of different names is no
		better than one name's) */
		else if (!known)
		{
			known = 1;
		}
		pthread_mutex_unlock(&config_lock);
		if (!known)
			platform_log("settings: no %s setting %s", type == _config_string ? "string" : "such", name);
		return &none;
	}
#ifdef HALO_XBOX_CONSOLE
	/* (the shared code's settings the console has none of: empty, 0 or
	false, as before it had config.toml; the first few said in the log) */
	if (!(config_settings[index].platforms & CONFIG_PLATFORM))
	{
		/* (each setting said once, not at each read: the scoreboard asks
		every frame) */
		static unsigned char reported[1024];

		if (index < (long)sizeof(reported) && !reported[index])
		{
			reported[index] = 1;
			platform_log("settings: %s is not one of the console's; read as empty", name);
		}
		return &none;
	}
#endif
	return &config_values[index];
}

/* sets a setting for this run only, from its value as text, as its
environment variable does: config.toml is not changed (the console's old
switch files, port/xbox/src/xbox_config.c) */
int config_set(const char *name, const char *value)
{
	long index = config_setting_index(name);

	if (index < 0)
		return 0;
	/* (the file read first, so that it does not overwrite this) */
	config_value(name, config_settings[index].type);
	pthread_mutex_lock(&config_lock);
	config_set_from_text(&config_values[index], config_settings[index].type, value);
	config_given[index] = 1;
	config_change_count++;
	pthread_mutex_unlock(&config_lock);
	return 1;
}

/* ---------- writing a setting */

/* the longest string config_write takes */
#define CONFIG_MAXIMUM_WRITTEN_STRING 4096

/* sets a setting, for now and in config.toml, from its value as text
("true", "60", "1.5", "all"): its line there is changed (or added, or the
line at its default taken out of its comment, now that the player chose
it), the rest of the file kept as it is */
int config_write(const char *name, const char *value)
{
	const char *dot = strchr(name, '.');
	long index = config_setting_index(name);
	char section[64], key[64], wanted[80], current[64] = "", path[1024];
	struct config_text out = { 0 };
	size_t size = 0, line_size;
	char *text, *line_text;
	const char *line;
	int written = 0, in_section = 0, has_value, succeeded;

	/* (a string as long as a server's rotation, server_rotation.h's
	SERVER_ROTATION_MAXIMUM_TEXT; any other value is a word or a number) */
	if (index < 0 || !dot || (size_t)(dot - name) >= sizeof(section) ||
		strlen(value) > (config_settings[index].type == _config_string ? CONFIG_MAXIMUM_WRITTEN_STRING : 256))
	{
		return 0;
	}
	/* (the line: its key, and its value with each character escaped as
	\uXXXX at most) */
	line_size = strlen(dot + 1) + 6 * strlen(value) + 64;
	line_text = malloc(line_size);
	if (!line_text)
		return 0;
	/* (the file read first, as the other settings are) */
	config_value(name, config_settings[index].type);
#ifndef HALO_CONSOLE_NO_UPDATER
	if (platform_packaged() && !strcmp(name, "update.auto"))
	{
		platform_log("settings: update.auto stays off in a packaged install");
		return 0;
	}
#endif
	pthread_mutex_lock(&config_lock);
	config_set_from_text(&config_values[index], config_settings[index].type, value);
	/* (a run that only answers its command line, a probe's too, writes
	nothing: the value is this run's alone) */
	if (config_answer_only)
	{
		config_change_count++;
		pthread_mutex_unlock(&config_lock);
		free(line_text);
		return 1;
	}
	snprintf(section, sizeof(section), "%.*s", (int)(dot - name), name);
	snprintf(key, sizeof(key), "%s", dot + 1);
	switch (config_settings[index].type)
	{
	case _config_boolean:
		snprintf(line_text, line_size, "%s = %s\n", key, config_values[index].boolean ? "true" : "false");
		break;
	case _config_integer:
		snprintf(line_text, line_size, "%s = %ld\n", key, config_values[index].integer);
		break;
	case _config_real:
		/* (with its point: TOML reads 1 as an integer) */
		snprintf(line_text, line_size, "%s = %.15g", key, config_values[index].real);
		if (!strpbrk(line_text + strlen(key) + 3, ".en"))
			strcat(line_text, ".0");
		strcat(line_text, "\n");
		break;
	case _config_string:
	{
		char *end = line_text + snprintf(line_text, line_size, "%s = \"", key);
		const char *character;

		/* (a TOML basic string: quotes and backslashes escaped, and control
		characters, so that a value cannot end its line) */
		for (character = config_values[index].string; *character; character++)
		{
			if ((unsigned char)*character < 0x20 || *character == 0x7F)
			{
				end += sprintf(end, "\\u%04x", (unsigned)(unsigned char)*character);
				continue;
			}
			if (*character == '"' || *character == '\\')
				*end++ = '\\';
			*end++ = *character;
		}
		strcpy(end, "\"\n");
		break;
	}
	}
	snprintf(wanted, sizeof(wanted), "%s", section);
	config_path(path, sizeof(path));
	text = config_read_file(path, &size);
	/* (the commented-out line is the one replaced only when there is no
	value) */
	has_value = text && config_text_has_key(text, name, 0);
	for (line = text ? text : ""; *line;)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;

		if (config_line_section(line, end, current, sizeof(current)))
		{
			/* (leaving the section without the key: it goes at its end) */
			if (in_section && !written)
			{
				config_append(&out, line_text);
				written = 1;
			}
			in_section = !strcmp(current, wanted);
		}
		else if (in_section && !written &&
			(has_value ? config_line_key(line, end, key) : config_line_commented_key(line, end, key)))
		{
			config_append(&out, line_text);
			written = 1;
			line = next;
			continue;
		}
		{
			char *copy = config_copy(line, (size_t)(next - line));

			if (copy)
			{
				config_append(&out, copy);
				free(copy);
			}
		}
		line = next;
	}
	if (!written)
	{
		if (out.length && out.buffer[out.length - 1] != '\n')
			config_append(&out, "\n");
		if (!in_section)
		{
			char header[80];

			snprintf(header, sizeof(header), "\n[%s]\n", section);
			config_append(&out, header);
		}
		config_append(&out, line_text);
	}
	succeeded = out.buffer && config_write_file(path, out.buffer);
	config_change_count++;
	pthread_mutex_unlock(&config_lock);
	free(out.buffer);
	free(text);
	free(line_text);
	return succeeded;
}

/* the file's lines "# key = <an older default>" for a setting, at the
current default again, so that the file reads as this version writes it
(NULL if there is none) */
static char *config_refresh_commented(const char *text, const struct config_setting *setting)
{
	const char *dot = strchr(setting->name, '.');
	struct config_text out = { NULL, 0, 0 };
	char section[64], current[64] = "", buffer[256];
	const char *line;
	int changed = 0;

	snprintf(section, sizeof(section), "%.*s", (int)(dot - setting->name), setting->name);
	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;

		config_line_section(line, end, current, sizeof(current));
		if (!strcmp(current, section) && config_line_commented_key(line, end, dot + 1))
		{
			snprintf(buffer, sizeof(buffer), "# %s = %s\n", dot + 1, setting->default_value);
			config_append(&out, buffer);
			changed = 1;
		}
		else
		{
			char *copy = config_copy(line, (size_t)(next - line));

			if (copy)
				config_append(&out, copy);
			free(copy);
		}
		line = next;
	}
	if (!changed)
	{
		free(out.buffer);
		return NULL;
	}
	return out.buffer;
}

int config_new_defaults_pending(char *text, size_t size)
{
	size_t old;
	int count = 0;

	if (size)
		text[0] = 0;
	/* (the file read first) */
	config_value("game.defaults_answered", _config_integer);
	pthread_mutex_lock(&config_lock);
	for (old = 0; old < NUMBER_OF_CONFIG_OLD_DEFAULTS; old++)
	{
		long index = config_old_defaults[old].change ? config_setting_index(config_old_defaults[old].name) : -1;
		size_t used = strlen(text);

		if (index >= 0 && config_pending[index] && config_pending_old[index] == config_old_defaults[old].default_value)
		{
			snprintf(text + used, size - used, "%s%s", count ? "\n" : "", config_old_defaults[old].change);
			count++;
		}
	}
	pthread_mutex_unlock(&config_lock);
	return count;
}

void config_new_defaults_answer(int use_new)
{
	int pending[NUMBER_OF_CONFIG_SETTINGS];
	const char *old_value[NUMBER_OF_CONFIG_SETTINGS];
	size_t index;
	char path[1024];

	config_value("game.defaults_answered", _config_integer);
	pthread_mutex_lock(&config_lock);
	memcpy(pending, config_pending, sizeof(pending));
	memcpy(old_value, config_pending_old, sizeof(old_value));
	memset(config_pending, 0, sizeof(config_pending));
	pthread_mutex_unlock(&config_lock);
	if (use_new)
	{
		size_t size = 0;
		char *text;

		config_path(path, sizeof(path));
		text = config_read_file(path, &size);
		if (text)
		{
			toml_result_t result = toml_parse(text, (int)size);
			char *changed = NULL;

			/* (the values an older version added beside the commented ones,
			which pinned the old defaults, taken out; the commented lines
			at the old defaults written at the new ones) */
			if (result.ok)
				changed = config_drop_added_defaults(text, result.toptab);
			toml_free(result);
			for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
			{
				if (pending[index])
				{
					char *refreshed = config_refresh_commented(changed ? changed : text, &config_settings[index]);

					if (refreshed)
					{
						free(changed);
						changed = refreshed;
					}
				}
			}
			if (changed && !config_write_file(path, changed))
				platform_log("settings: cannot write %s", path);
			free(changed);
			free(text);
		}
		pthread_mutex_lock(&config_lock);
		for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
		{
			if (pending[index])
			{
				char value[64];

				config_unquote(config_settings[index].default_value, value, sizeof(value));
				config_set_from_text(&config_values[index], config_settings[index].type, value);
				platform_log("settings: %s follows this version's default (%s) now", config_settings[index].name,
					config_settings[index].default_value);
			}
		}
		config_change_count++;
		pthread_mutex_unlock(&config_lock);
	}
	else
	{
		for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
		{
			if (pending[index])
			{
				char value[64];

				config_unquote(old_value[index], value, sizeof(value));
				/* (a value an older version wrote is there already) */
				if (pending[index] == 1)
					config_write(config_settings[index].name, value);
				platform_log("settings: %s = %s is kept", config_settings[index].name, old_value[index]);
			}
		}
	}
	config_write("game.defaults_answered", "1");
}

int config_write_boolean(const char *name, int value)
{
	long index = config_setting_index(name);

	return index >= 0 && config_settings[index].type == _config_boolean && config_write(name, value ? "true" : "false");
}

/* a setting's value as text ("true", "60", "1.5", "all"): 0 if there is no
such setting */
int config_text(const char *name, char *text, size_t size)
{
	long index = config_setting_index(name);
	const struct config_value *value;

	if (index < 0)
		return 0;
	value = config_value(name, config_settings[index].type);
	switch (config_settings[index].type)
	{
	case _config_boolean:
		snprintf(text, size, "%s", value->boolean ? "true" : "false");
		break;
	case _config_integer:
		snprintf(text, size, "%ld", value->integer);
		break;
	case _config_real:
		snprintf(text, size, "%.15g", value->real);
		break;
	case _config_string:
		snprintf(text, size, "%s", value->string ? value->string : "");
		break;
	}
	return 1;
}

void config_folder(char *path, size_t size)
{
	char file[1024];
	char *separator;

	config_path(file, sizeof(file));
	separator = strrchr(file, '/');
#ifndef HALO_ANDROID
	if (!separator || (strrchr(file, '\\') && strrchr(file, '\\') > separator))
		separator = strrchr(file, '\\');
#endif
	if (separator)
		separator[1] = 0;
	else
		file[0] = 0;
	snprintf(path, size, "%s", file);
}

/* ---------- public code */

char *config_file_read(const char *path, size_t *size)
{
	return config_read_file(path, size);
}

#ifndef HALO_PORT_CONSOLE_LIMITS
/* the settings config.toml changes while the game runs, edited by hand
(Settings writes them through config_write): the volumes, which their
readers read again when config_changes moves (dsound_sdl.c,
sound_manager.c). The rest still take effect from the next start */
static const char *const config_live_settings[] =
{
	"audio.volume",
	"audio.music_volume",
	"audio.effects_volume",
};

enum
{
	/* config.toml's time looked at no more often than this (seconds; the
	C library's clock, which the dedicated server, without SDL, has too) */
	CONFIG_FOLLOW_INTERVAL_SECONDS = 1
};

/* config.toml changed on disk since it was last looked at: its live
settings read again from it (one given by the environment kept), and
config_changes moved if any is different */
static void config_follow_file(void)
{
	static time_t looked_at;
	static posix_ulong modified, modified_nanoseconds, modified_size;
	static int looked;
	static char path[1024];
	time_t now = time(NULL);
	struct posix_file_information info;
	size_t size = 0;
	char *text;
	toml_result_t result;
	size_t live;
	int changed = 0;

	if (!config_loaded || (looked && now - looked_at < CONFIG_FOLLOW_INTERVAL_SECONDS))
		return;
	looked_at = now;
	if (!path[0])
		config_path(path, sizeof(path));
	if (posix_stat(path, &info) != 0)
		return;
	/* (the first look keeps the file as it was read; the size too, for a
	file system whose times are whole seconds) */
	if (!looked || (info.modification_seconds == modified &&
		info.modification_nanoseconds == modified_nanoseconds && info.size_low == modified_size))
	{
		looked = 1;
		modified = info.modification_seconds;
		modified_nanoseconds = info.modification_nanoseconds;
		modified_size = info.size_low;
		return;
	}
	modified = info.modification_seconds;
	modified_nanoseconds = info.modification_nanoseconds;
	modified_size = info.size_low;
	text = config_read_file(path, &size);
	if (!text)
		return;
	result = toml_parse(text, (int)size);
	if (result.ok)
	{
		pthread_mutex_lock(&config_lock);
		for (live = 0; live < sizeof(config_live_settings) / sizeof(config_live_settings[0]); live++)
		{
			long index = config_setting_index(config_live_settings[live]);
			const struct config_setting *setting;
			struct config_value value;

			if (index < 0)
				continue;
			setting = &config_settings[index];
			if (getenv(setting->environment))
				continue;
			memset(&value, 0, sizeof(value));
			config_set_from_text(&value, setting->type, setting->default_value);
			config_set_from_file(&value, setting, result.toptab);
			if (value.real != config_values[index].real)
			{
				config_values[index].real = value.real;
				platform_log("settings: config.toml changed: %s = %g", setting->name, value.real);
				changed = 1;
			}
		}
		if (changed)
			config_change_count++;
		pthread_mutex_unlock(&config_lock);
	}
	toml_free(result);
	free(text);
}
#else
/* (not on a console: its config.toml is edited off the console, and a look
at the disk every second is a hitch the console can do without) */
static void config_follow_file(void)
{
}
#endif

unsigned long config_changes(void)
{
	config_follow_file();
	return config_change_count;
}

int config_server_mode_enabled(void)
{
	config_value("server.enabled", _config_boolean);
	return config_server_mode;
}

int config_setting_info(int index, struct config_setting_info *info)
{
	static const char *const types[] = { "boolean", "integer", "real", "string" };
	const struct config_setting *setting;
	const char *environment;

	if (index < 0 || (size_t)index >= NUMBER_OF_CONFIG_SETTINGS)
		return 0;
	setting = &config_settings[index];
	memset(info, 0, sizeof(*info));
	info->name = setting->name;
	info->type = types[setting->type];
	info->environment = setting->environment;
	info->comment = setting->comment;
	config_default(setting->name, info->default_text, sizeof(info->default_text));
	environment = config_environment(setting);
	info->pinned = environment && (setting->environment_style == _environment_value || config_environment_set(environment));
	info->written = (setting->platforms & CONFIG_PLATFORM) != 0;
	return 1;
}

int config_reload(const char *prefix)
{
	char path[1024];
	size_t size = 0;
	size_t length = strlen(prefix);
	char *text;
	toml_result_t result;
	size_t index;

	config_ensure_loaded();
	config_path(path, sizeof(path));
	text = config_read_file(path, &size);
	if (!text)
		return 0;
	result = toml_parse(text, (int)size);
	if (!result.ok)
	{
		platform_log("settings: %s cannot be read again (%s); the settings stay as they were", path, result.errmsg);
		toml_free(result);
		free(text);
		return 0;
	}
	pthread_mutex_lock(&config_lock);
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		const char *environment = config_environment(setting);
		struct config_value value;

		/* (the setting's own: those of the environment stay its) */
		if (strncmp(setting->name, prefix, length) ||
			(environment && (setting->environment_style == _environment_value || config_environment_set(environment))))
		{
			continue;
		}
		memset(&value, 0, sizeof(value));
		if (setting->type == _config_string)
		{
			size_t default_length = strlen(setting->default_value);

			value.string = default_length >= 2 ? config_copy(setting->default_value + 1, default_length - 2) : strdup("");
		}
		else
		{
			config_set_from_text(&value, setting->type, setting->default_value);
		}
		config_set_from_file(&value, setting, result.toptab);
		/* (the old string kept, as config_set_from_text keeps it) */
		config_values[index] = value;
	}
#if !defined(HALO_ANDROID) && !defined(HALO_WEB)
	/* (what the run forces, forced again: a server started by --server
	stays one whatever the file's server.enabled says, and its router asked
	as its server.upnp now says) */
	if (config_server_mode)
		config_apply_server_mode();
	else if (config_probe_mode)
		config_apply_probe_mode();
#endif
	config_change_count++;
	pthread_mutex_unlock(&config_lock);
	toml_free(result);
	free(text);
	config_check_words();
	return 1;
}

int config_default(const char *name, char *text, size_t size)
{
	long index = config_setting_index(name);
	const char *value;
	size_t length;

	if (index < 0)
		return 0;
	value = config_settings[index].default_value;
#ifndef HALO_CONSOLE_NO_UPDATER
	/* (Defaults on the settings screen: a packaged install's is off) */
	if (platform_packaged() && !strcmp(name, "update.auto"))
		value = "false";
#endif
	length = strlen(value);
	/* (a string's without its quotes: the defaults have no escapes) */
	if (config_settings[index].type == _config_string && length >= 2 && value[0] == '"')
	{
		value++;
		length -= 2;
	}
	snprintf(text, size, "%.*s", (int)length, value);
	return 1;
}

#ifndef HALO_CONSOLE_NO_UPDATER
int config_packaged_update_check(void)
{
	config_value("update.auto", _config_boolean);
	return packaged_update_check;
}
#endif

int config_boolean(const char *name)
{
	return config_value(name, _config_boolean)->boolean;
}

int halo_fixed_step(void)
{
	static int fixed_step = -1;

	if (fixed_step < 0)
		fixed_step = config_boolean("debug.fixed_step") != 0;
	return fixed_step;
}

long config_integer(const char *name)
{
	return config_value(name, _config_integer)->integer;
}

unsigned long network_client_timeout_milliseconds(void)
{
	long seconds = config_integer("network.client_timeout_seconds");

	if (seconds < 5)
		seconds = 5;
	if (seconds > 300)
		seconds = 300;
	return (unsigned long)seconds * 1000UL;
}

double config_real(const char *name)
{
	return config_value(name, _config_real)->real;
}

const char *config_string(const char *name)
{
	const char *string = config_value(name, _config_string)->string;

	return string ? string : "";
}
