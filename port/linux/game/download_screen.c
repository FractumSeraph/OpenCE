/*
DOWNLOAD_SCREEN.C

The screen a map's download shows over the menus: Halo's loading screen
(source/interface/progress_bar.c), its light sweeping from left to right as
the map comes, with the map's name, how the download goes and B to stop it
under it, in the menus' fonts. Whichever download is under way: over
BitTorrent (map_torrents.h; from the site in the browser's build), from the
map repositories and archives the Online Games screen offers (map_download.h,
ChupathingyCE's; its panel is behind it), or from the host's own map sharing
(map_share.h), whose B leaves the lobby as the menus behind do. The menus behind it go on, so
that the join waiting for the map follows once it is there
(menu_functions.c: map_fetch_update, lobby_browser_web_join_update), but
they take no presses while it is up: B stops the download, and the join
with it. maps.download_screen = false leaves the menus up instead (the
games' list's status line says how the download goes).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/progress_bar.h"
#include "interface/ui_widget.h"
#include "rasterizer/rasterizer.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"

/* ---------- constants */

enum
{
	/* (event_manager.c's event type of a button, which it keeps to itself) */
	DOWNLOAD_EVENT_BUTTON = 3,
	/* (draw_string.c's justifications, which it keeps to itself) */
	DOWNLOAD_TEXT_LEFT = 0,
	DOWNLOAD_TEXT_CENTERED = 2,
};

/* ---------- prototypes */

/* port/linux/src (map_torrents.h, port_config.c) */
int map_torrents_download_progress(float *fraction, char *name, int size);
#ifdef HALO_GAME_BROWSER
#include "../src/map_download.h"
#endif
#include "map_share.h"
int map_torrents_fetching(char *status, int size);
int config_boolean(char const *name);
/* menu_functions.c */
void ui_widget_port_map_fetch_cancel(void);
/* ui_widget.c */
boolean main_menu_is_active(void);
#ifdef HALO_GAME_BROWSER
/* browser_screen.c */
boolean browser_screen_active(void);
#endif

/* ---------- globals */

static struct
{
	/* drawn last frame (the screen is let go of when it ends) */
	boolean shown;
	/* the share done, as last drawn: the light never goes back */
	real progress;
} download_screen;

/* ---------- private code */

/* which download the screen follows */
enum
{
	_download_none,
	_download_torrents,
	_download_repositories,
	_download_host,
};

/* the download under way (the first of BitTorrent's, the repositories' and
the host's that is), its share done, the map's name and a line of how it
goes */
static short download_screen_source(
	float *fraction,
	char *name,
	int name_size,
	char *status,
	int status_size)
{
	status[0] = 0;
	if (map_torrents_download_progress(fraction, name, name_size))
	{
		char text[256];

		/* (the status line's numbers: "Downloading <name>: " is the title's) */
		if (map_torrents_fetching(text, sizeof(text)))
		{
			char const *numbers = strstr(text, ": ");

			snprintf(status, (size_t)status_size, "%s", numbers ? numbers + 2 : text);
		}
		return _download_torrents;
	}
#ifdef HALO_GAME_BROWSER
	{
		struct map_download_progress progress;

		map_download_get(&progress);
		if (progress.state == MAP_DOWNLOAD_RUNNING)
		{
			char *extension;

			*fraction = progress.total ? (float)progress.received / (float)progress.total : 0.0f;
			snprintf(name, (size_t)name_size, "%s", progress.name);
			extension = strrchr(name, '.');
			if (extension && !strcmp(extension, ".map"))
				*extension = 0;
			if (progress.detail[0])
				snprintf(status, (size_t)status_size, "%s", progress.detail);
			else if (progress.total)
				snprintf(status, (size_t)status_size, "%u of %u MB", (progress.received + 0x80000) >> 20,
					(progress.total + 0x80000) >> 20);
			return _download_repositories;
		}
	}
#endif
	{
		real share;

		if (map_share_download_status(&share, name, name_size, status, status_size))
		{
			*fraction = share;
			return _download_host;
		}
	}
	return _download_none;
}

/* a line of text centered at y in the menus' 640x480, in a menu font */
static void download_screen_line(
	char const *font,
	short y,
	real_argb_color const *color,
	char const *text)
{
	rectangle2d bounds;

	bounds.x0 = 0;
	bounds.x1 = 640;
	bounds.y0 = y;
	bounds.y1 = (short)(y + 40);
	draw_string_set_draw_mode(tag_loaded(FONT_GROUP_TAG, font), NONE, DOWNLOAD_TEXT_CENTERED, 0, color);
	rasterizer_draw_string(&bounds, NULL, NULL, 0, text);
}

/* ---------- public code */

/* whether the screen is up: a map's download under way, at the menus */
boolean download_screen_active(
	void)
{
	float fraction;
	char name[64];
	char status[160];

	/* (over Online Games too: its panel, which shows the repositories'
	downloads, is behind it; browser_screen.c, map_download.c) */
	return main_menu_is_active() && config_boolean("maps.download_screen") &&
		download_screen_source(&fraction, name, sizeof(name), status, sizeof(status)) != _download_none;
}

/* each frame the menus run while the screen is up (ui_widget.c's
process_ui_widgets): the presses are the screen's, and B stops the download */
void download_screen_process(
	void)
{
	struct event_record event;
	float fraction;
	char name[64];
	char status[160];
	short source = download_screen_source(&fraction, name, sizeof(name), status, sizeof(status));

	/* (the host's map sharing: the menus behind take the presses, B leaving
	the lobby, which ends the download) */
	if (source == _download_host)
		return;
	while (get_next_event(&event, NONE))
	{
		if (event.type == DOWNLOAD_EVENT_BUTTON && event.data.button.index == _gamepad_analog_button_b)
		{
#ifdef HALO_GAME_BROWSER
			if (source == _download_repositories)
				map_download_cancel();
			else
#endif
				ui_widget_port_map_fetch_cancel();
			break;
		}
	}
	/* (the menus behind take nothing) */
	event_manager_flush();
}

/* each frame, after the menus are drawn (ui_widget.c's render_ui_widgets,
the first player's) */
void download_screen_render(
	void)
{
	real_argb_color const title_color = { 1.0f, 0.9f, 0.93f, 0.99f };
	real_argb_color const status_color = { 1.0f, 0.56f, 0.65f, 0.78f };
	real_argb_color const legend_color = { 1.0f, 1.0f, 1.0f, 1.0f };
	float fraction;
	char name[64];
	char text[256];
	char status[160];
	real progress;
	rectangle2d bounds;
	short index;
	short source = _download_none;

	/* (nothing drawn without a renderer: debug.null_renderer, a server's) */
	if (!download_screen_active() || config_boolean("debug.null_renderer") ||
		(source = download_screen_source(&fraction, name, sizeof(name), status, sizeof(status))) == _download_none)
	{
		if (download_screen.shown)
		{
			download_screen.shown = FALSE;
			progress_bar_download_end();
		}
		return;
	}
	if (!download_screen.shown)
	{
		download_screen.shown = TRUE;
		download_screen.progress = 0.0f;
	}
	/* (the screen draws from a share above 0, and only forward: a share
	falls back as the next file's size is learned) */
	progress = PIN(fraction, 0.001f, 1.0f);
	if (progress < download_screen.progress)
		progress = download_screen.progress;
	download_screen.progress = progress;
	progress_bar_download_display(progress);

	snprintf(text, sizeof(text), "DOWNLOADING %s", name);
	for (index = 0; text[index]; index++)
	{
		if (text[index] >= 'a' && text[index] <= 'z')
			text[index] = (char)(text[index] - 'a' + 'A');
	}
	download_screen_line("ui\\large_ui", 300, &title_color, text);
	if (status[0])
		download_screen_line("ui\\small_ui", 334, &status_color, status);
	/* (the B button's icon, as the menus' legends draw it: from the left, as
	the icon and the words after it are each placed after the one before) */
	bounds.x0 = 290;
	bounds.x1 = 640;
	bounds.y0 = 362;
	bounds.y1 = 402;
	draw_string_set_draw_mode(tag_loaded(FONT_GROUP_TAG, "ui\\small_ui"), NONE, DOWNLOAD_TEXT_LEFT, 0,
		&legend_color);
	draw_string_and_hack_in_icons(&bounds, NULL, NULL, 0, source == _download_host ? L"%b-button=LEAVE" :
		L"%b-button=STOP", FALSE);
}
