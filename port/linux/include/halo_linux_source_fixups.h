/*
HALO_LINUX_SOURCE_FIXUPS.H

The port's functions that the game's sources call, declared for them.
*/

#ifndef __HALO_LINUX_SOURCE_FIXUPS_H
#define __HALO_LINUX_SOURCE_FIXUPS_H

/* frames between the 30 Hz ticks (port/linux/game/render_interpolation.c);
the platform layer reads the display.interpolation setting */
struct observer_result;
struct render_camera;
struct real_matrix4x3;
int halo_interpolation_enabled(void);
float game_time_get_tick_fraction(void);
void render_interpolation_tick(void);
void render_interpolation_reset(void);
void render_interpolation_frame_begin(void);
void render_interpolation_frame_end(void);
float render_interpolation_fraction(void);
unsigned char render_interpolation_drawing(void);
struct real_matrix4x3 *render_interpolation_object_node_matrices(long object_index);
float *render_interpolation_object_function_values(long object_index);
struct observer_result const *render_interpolation_camera(short local_player_index,
	struct observer_result const *observer);
void render_interpolation_first_person(short local_player_index, struct real_matrix4x3 *node_matrices,
	short node_count, struct render_camera const *camera);
float render_interpolation_game_time_sec(long ticks);
float render_interpolation_ticks_since(long now, long then);

/* whether the map being loaded, by the name it was loaded by, is a
downloaded one, whose scripts may call and set less: one map sharing
downloaded, or one game.downloaded_maps names (port/linux/game/map_share.c) */
int map_share_map_downloaded(char const *map_name);

/* an IP address as the log may show it: a public one as a tag of the run's,
never whole (port/linux/src/log_address.c, log_address.h) */
const char *log_address(const unsigned char *bytes, int length, int port, char *text, int size);

/* the port's menus' screen for one of ui.map's, by name, else the name
(port/linux/game/menu_tags.c) */
char const *pc_menus_screen(char const *name);
/* debug.fixed_step: a game of this machine alone runs a tick a frame
(port/linux/src/port_config.c; game_time.c, main.c) */
int halo_fixed_step(void);
/* a Custom Edition map's HUD meter drawn from a bitmap its HUD draws as
artwork too, whose channels are kept as they are (not a Halo PC meter's
swizzle: port/linux/game/ce_hud.c): set for that draw alone, which then
samples the fill order from alpha and the shape from red (hud_draw.c;
port/linux/src/xbox_textures.c, nv2a_psh.c). OpenCE's, for ChupathingyCE's
Custom Edition loader */
void halo_custom_edition_hud_meter(int active);
/* the width of the screen the game draws, 480 lines tall: the device's or
the display's shape, or 640 (port/linux/src/d3d8_gl.c) */
long halo_screen_width(void);
/* the screen's pixels to the Xbox's one (port/linux/src/d3d8_gl.c) */
float halo_screen_scale(void);
/* takes up a new width between frames (F11); returns the width */
long halo_screen_commit(void);
/* the shadow maps' pixels for each of their 128 texels each way, a power of
two (display.shadow_resolution) */
long halo_shadow_map_scale(void);
/* how many times a model's size in the 480-line screen's pixels to measure
it at (display.model_detail, source/models/models.c): level nonzero, to
choose its level of detail, otherwise whether it is drawn */
float halo_model_detail_scale(int level);
/* whether display.model_detail is "stock" (port/linux/src/d3d8_gl.c), for
each permutation's level (port/linux/game/model_detail.c) */
int halo_model_detail_stock(void);
/* while TRUE, drawing shifts right to center 640-column layouts */
void halo_screen_ui_offset(unsigned char centered);
/* names a model lighting vertex program, whose draws can be lit for each
pixel (port/linux/src/d3d8_gl.c, display.per_pixel_lighting) */
void halo_vertex_shader_lighting(unsigned long handle);
/* display.anti_aliasing's pass over a window's 3D view, before the HUD and
menus (source/render/render.c): the window's bounds on the screen */
void halo_screen_anti_alias(short x0, short y0, short x1, short y1);
/* the loading screen's picture, RGBA bytes with its top row first, over the
whole back buffer (port/linux/src/d3d8_gl.c, port/linux/game/loading_screen.c) */
void halo_draw_picture(const unsigned char *rgba, long width, long height);
/* display.hdr's tone mapping of a window's 3D view, before its
anti-aliasing (source/render/render.c) */
void halo_screen_tone_map(short x0, short y0, short x1, short y1);
#ifdef HALO_SWITCH_CONSOLE
/* whether the zoom's blur is drawn on a small copy of the screen and blended
over it once (source/rasterizer/xbox/rasterizer_xbox_screen_effect.c), and
that blend's shader on or off for the draws that follow: the Switch's
renderer (port/switch/guest/d3d8_dk.c; debug.original_zoom) */
unsigned char halo_screen_fast_warp(void);
void halo_screen_warp_blend(unsigned char on);
/* whether a blur screen effect is drawn on small targets (the same file;
debug.original_blur) */
unsigned char halo_screen_fast_blur(void);
#endif
/* network.client_timeout_seconds, in milliseconds (port/linux/src/port_config.c) */
unsigned long network_client_timeout_milliseconds(void);
/* a dedicated server (halo --server: port/linux/src/server_mode.c,
port/linux/game/server_director.c; docs/dedicated_server.md): whether this
run is one; its start, before the game's (shell_xbox.c); its frames' pace
(main.c); the game's fatal error ended with (main.c); and the console's
output, which it logs and captures for its commands' replies (terminal.c) */
int halo_server_mode(void);
void server_mode_start(void);
void server_mode_pace_frame(void);
void server_fatal(char const *text);
void server_console_output(const char *text);
/* ... whether debug.txt's lines go to the standard error too
(server.log_debug_echo: errors.c) */
int halo_server_debug_echo(void);
/* ... the level its rotation plays next, NULL if none (game_engine.c) */
char const *server_director_first_level(void);
/* the mouse in the menus (source/interface/ui_widget.c) */
#include "halo_ui_pointer.h"
/* a level not in the maps folder (source/cache/cache_files_windows.c): a
campaign map left out of the first start's Multiplayer only, offered for
download (port/linux/src/sdl_platform.c; nonzero once it is here), and the
words the damaged disc error says instead for it (nonzero for such a map) */
int platform_offer_missing_map(const char *name);
int platform_missing_map_message(const char *name, char *text, int size);

#endif
