/*
NETWORK_RECORDING.C

Port (this fork): the host's recordings of its games (network_recording.h),
for its admins to watch again, through any player's eyes, in a browser
(port/web/assets/replay/index.html), when someone is reported as a cheat.

With network.record_games on, a host (a dedicated server, or a player's
game) writes each game to recordings\<date>_<time>_<map>.hrec in its data
folder: the map's walls (its structure's collision surfaces, as the fair
play checks meet them), each player's name, team, address and hardware id
as they join, and every tick each living player's position, eye height,
aim, crouch, triggers, zoom, health, weapon and vehicle; each bullet's hit
on a player with the fair play checks' verdict (network_fairplay.c), and
each kill. The oldest recordings go once the folder holds more than
network.recordings_megabytes. A recording is about 40 megabytes an hour of
a full game. Keep the folder private: it holds players' addresses.

The file (little-endian): "HREC", a version (1, 4 bytes), then blocks:
'R', the records' size, the block's size (4 bytes each, the same), and the
records as they are (a reader takes 'B' too: the records compressed with
zlib, as the first recordings were, which a browser's DecompressionStream
"deflate" opens). A record is its type (1 byte), its size (4 bytes) and
that many bytes; one of a type a reader does not know it skips:

  1 game:     unix time (4), ticks a second (2), the server's name, the
              map, the game type (each a byte's length, then that text)
  2 walls:    vertex count (4), the vertices (3 floats each), polygon count
              (4), then each polygon's flags (1: two-sided 1, invisible 2,
              climbable 4, breakable 8), vertex count (1) and vertices (4 each)
  3 player:   tick (4), player (1), team (1), name, hardware id, address
  4 frame:    tick (4), count (1), then each living player's: player (1),
              flags (1: crouching 1, primary trigger 2, secondary trigger
              4, zoomed 8, riding 16), position (3 floats: their feet),
              eye height (2, millimetres), aim (yaw, pitch: 2 each,
              radians times 10000), health and shields (1 each, of 255;
              shields to 2.0), weapon and vehicle (2 each: a tag's number,
              0xFFFF for none)
  5 tag:      number (2), its name
  6 hit:      tick (4), shooter (1), target (1), where (3 floats), verdict
              (1: network_recording.h), degrees off aim, distance (floats)
  7 kill:     tick (4), killer (1), killed (1), headshot (1)
  8 end:      tick (4)
  9 move:     tick (4), player (1), why (1: too far from where the host had
              them 1, too fast 2, too high 3, hovering 4), how much (a float)

A block goes to the file every 10 seconds, or sooner when 192 kilobytes of
records have come: a host that stops loses no more than that.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "objects/objects.h"
#include "physics/collision_bsp_definitions.h"
#include "scenario/scenario.h"
#include "tag_files/tag_files.h"
#include "units/units.h"
#include "network_distributed.h"
#include "network_recording.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* network_server_manager_internal.h's (as game_events.c has it) */
struct network_game *network_game_server_get_game(struct network_game_server *server);
/* the platform layer's (port_config.h, p2p.h) */
int config_boolean(const char *name);
long config_integer(const char *name);
void p2p_hardware_id_sanitize(char *destination, int size, const char *source);

/* ---------- constants */

#define RECORDING_FOLDER "d:\\recordings"
#define RECORDING_VERSION 1

enum
{
	_record_game = 1,
	_record_walls,
	_record_player,
	_record_frame,
	_record_tag,
	_record_hit,
	_record_kill,
	_record_end,
	_record_move,
};

enum
{
	/* records gathered before a block is written, and how long at most */
	RECORDING_BLOCK_BYTES = 192 * 1024,
	RECORDING_BUFFER_BYTES = 256 * 1024,
	RECORDING_BLOCK_TICKS = 10 * TICKS_PER_SECOND,
	/* the tags numbered, at most */
	RECORDING_MAXIMUM_TAGS = 256,
	/* the walls' vertices and polygons, at most */
	RECORDING_MAXIMUM_WALL_BYTES = 8 * 1024 * 1024,
	/* the files a folder is pruned of, at most at once */
	RECORDING_MAXIMUM_FILES = 1024,
};

#define RECORDING_NO_TAG 0xFFFF

/* ---------- globals */

static struct
{
	FILE *file;
	boolean started;
	boolean failed;
	long started_time;
	long flushed_time;
	unsigned long length;
	byte buffer[RECORDING_BUFFER_BYTES];
	/* the tags numbered so far */
	long tags[RECORDING_MAXIMUM_TAGS];
	short tag_count;
	/* the player each slot had when last told (DATUM_INDEX_NEW), NONE */
	long players[MAXIMUM_TRACKED_PLAYERS];
} recording;

/* ---------- private code */

/* records written as a block; the recording ended if they cannot be */
static void recording_write_block(
	byte const *records,
	unsigned long length)
{
	byte header[9];

	if (!recording.file || !length)
		return;
	/* (as they are: the game's own zlib, 1.1.3, compresses nothing right in
	the 64-bit builds' sources, the dedicated servers', and crashed them) */
	header[0] = 'R';
	header[1] = (byte)length;
	header[2] = (byte)(length >> 8);
	header[3] = (byte)(length >> 16);
	header[4] = (byte)(length >> 24);
	header[5] = header[1];
	header[6] = header[2];
	header[7] = header[3];
	header[8] = header[4];
	if (fwrite(header, sizeof(header), 1, recording.file) != 1 || fwrite(records, length, 1, recording.file) != 1)
	{
		error(_error_log, "recording: could not write to the recording (a full disk?); it ends");
		fclose(recording.file);
		recording.file = NULL;
		recording.failed = TRUE;
	}
	else
		fflush(recording.file);
}

/* the records so far written as a block */
static void recording_flush(
	void)
{
	recording.flushed_time = game_time_get();
	recording_write_block(recording.buffer, recording.length);
	recording.length = 0;
}

/* room for a record of the size, the block written first if it must be:
the record's start, NULL for none (too large, or no recording) */
static byte *recording_begin(
	byte type,
	unsigned long size)
{
	byte *record;

	if (!recording.file || size + 5 > RECORDING_BUFFER_BYTES)
		return NULL;
	if (recording.length + size + 5 > RECORDING_BUFFER_BYTES)
		recording_flush();
	if (!recording.file)
		return NULL;
	record = recording.buffer + recording.length;
	record[0] = type;
	record[1] = (byte)size;
	record[2] = (byte)(size >> 8);
	record[3] = (byte)(size >> 16);
	record[4] = (byte)(size >> 24);
	recording.length += size + 5;
	return record + 5;
}

static byte *put_u8(byte *at, unsigned long value)
{
	at[0] = (byte)value;
	return at + 1;
}

static byte *put_u16(byte *at, unsigned long value)
{
	at[0] = (byte)value;
	at[1] = (byte)(value >> 8);
	return at + 2;
}

static byte *put_u32(byte *at, unsigned long value)
{
	at[0] = (byte)value;
	at[1] = (byte)(value >> 8);
	at[2] = (byte)(value >> 16);
	at[3] = (byte)(value >> 24);
	return at + 4;
}

static byte *put_real(byte *at, real value)
{
	unsigned long bits;

	csmemcpy(&bits, &value, sizeof(bits));
	return put_u32(at, bits);
}

/* a text: its length (at most 255) and its bytes */
static byte *put_text(byte *at, char const *text)
{
	size_t length = text ? strlen(text) : 0;

	if (length > 255)
		length = 255;
	at = put_u8(at, (unsigned long)length);
	if (length)
		csmemcpy(at, text, length);
	return at + length;
}

/* the size a text takes */
static unsigned long text_size(char const *text)
{
	size_t length = text ? strlen(text) : 0;

	return 1 + (unsigned long)(length > 255 ? 255 : length);
}

/* the tag's number in the recording (told the first time), RECORDING_NO_TAG
for none */
static unsigned long recording_tag(
	long tag_index)
{
	char const *name;
	byte *at;
	short index;

	if (tag_index == NONE)
		return RECORDING_NO_TAG;
	for (index = 0; index < recording.tag_count; index++)
	{
		if (recording.tags[index] == tag_index)
			return (unsigned long)index;
	}
	if (recording.tag_count >= RECORDING_MAXIMUM_TAGS)
		return RECORDING_NO_TAG;
	name = tag_get_name(tag_index);
	at = recording_begin(_record_tag, 2 + text_size(name));
	if (!at)
		return RECORDING_NO_TAG;
	index = recording.tag_count++;
	recording.tags[index] = tag_index;
	at = put_u16(at, (unsigned long)index);
	put_text(at, name);
	return (unsigned long)index;
}

/* the oldest recordings deleted while the folder holds more than its
megabytes (the names begin with their time: the oldest sorts first) */
static void recording_prune(
	void)
{
	static char names[RECORDING_MAXIMUM_FILES][MAX_PATH];
	static unsigned long sizes[RECORDING_MAXIMUM_FILES];
	WIN32_FIND_DATAA data;
	HANDLE find;
	long count = 0;
	long index;
	double total = 0.0;
	double limit = (double)config_integer("network.recordings_megabytes") * 1024.0 * 1024.0;

	if (!(limit > 0.0))
		return;
	find = FindFirstFileA(RECORDING_FOLDER "\\*.hrec", &data);
	if (find == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if (count < RECORDING_MAXIMUM_FILES)
		{
			snprintf(names[count], sizeof(names[count]), "%s", data.cFileName);
			sizes[count] = data.nFileSizeLow;
			total += (double)data.nFileSizeLow;
			count++;
		}
	}
	while (FindNextFileA(find, &data));
	CloseHandle(find);
	while (total > limit && count > 1)
	{
		long oldest = 0;
		char path[MAX_PATH + 32];

		for (index = 1; index < count; index++)
		{
			if (strcmp(names[index], names[oldest]) < 0)
				oldest = index;
		}
		snprintf(path, sizeof(path), RECORDING_FOLDER "\\%s", names[oldest]);
		if (DeleteFileA(path))
			error(_error_silent, "recording: %s deleted (the folder is over network.recordings_megabytes)", names[oldest]);
		total -= (double)sizes[oldest];
		names[oldest][0] = '\x7f';
		names[oldest][1] = 0;
		count--;
		/* (the deleted one's place: the last's) */
		if (oldest != count)
		{
			csmemcpy(names[oldest], names[count], sizeof(names[oldest]));
			sizes[oldest] = sizes[count];
		}
	}
}

/* the map's walls: its structure's collision surfaces, each a polygon */
static void recording_walls(
	void)
{
	struct collision_bsp *bsp = global_collision_bsp_get();
	unsigned long size;
	long polygon_count = 0;
	long index;
	byte *walls;
	byte *at;

	if (!bsp)
		return;
	/* (the size: the vertices, then each surface's edges counted round it) */
	size = 4 + 12 * (unsigned long)bsp->vertices.count + 4;
	for (index = 0; index < bsp->surfaces.count; index++)
	{
		struct collision_surface *surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, index, struct collision_surface);
		long edge_index = surface->first_edge_index;
		long corners = 0;

		do
		{
			struct collision_edge *edge;

			if (edge_index < 0 || edge_index >= bsp->edges.count)
				break;
			edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
			edge_index = edge->surface_indices[0] == index ? edge->edge_indices[0] : edge->edge_indices[1];
			corners++;
		}
		while (edge_index != surface->first_edge_index && corners < 255);
		if (corners >= 3 && corners < 255)
		{
			size += 2 + 4 * (unsigned long)corners;
			polygon_count++;
		}
	}
	if (size > RECORDING_MAXIMUM_WALL_BYTES)
	{
		error(_error_log, "recording: the map's walls (%lu bytes) are too large to record", size);
		return;
	}
	/* (the walls a block of their own, after the records before them) */
	walls = (byte *)malloc(size + 5);
	if (!walls)
		return;
	recording_flush();
	at = put_u8(walls, _record_walls);
	at = put_u32(at, size);
	at = put_u32(at, (unsigned long)bsp->vertices.count);
	for (index = 0; index < bsp->vertices.count; index++)
	{
		struct collision_vertex *vertex = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, index, struct collision_vertex);

		at = put_real(at, vertex->point.x);
		at = put_real(at, vertex->point.y);
		at = put_real(at, vertex->point.z);
	}
	at = put_u32(at, (unsigned long)polygon_count);
	for (index = 0; index < bsp->surfaces.count; index++)
	{
		struct collision_surface *surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, index, struct collision_surface);
		long corner_vertices[255];
		long edge_index = surface->first_edge_index;
		long corners = 0;
		long corner;

		do
		{
			struct collision_edge *edge;

			if (edge_index < 0 || edge_index >= bsp->edges.count)
				break;
			edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
			if (edge->surface_indices[0] == index)
			{
				corner_vertices[corners] = edge->vertex_indices[0];
				edge_index = edge->edge_indices[0];
			}
			else
			{
				corner_vertices[corners] = edge->vertex_indices[1];
				edge_index = edge->edge_indices[1];
			}
			corners++;
		}
		while (edge_index != surface->first_edge_index && corners < 255);
		if (corners < 3 || corners >= 255)
			continue;
		at = put_u8(at, surface->flags);
		at = put_u8(at, (unsigned long)corners);
		for (corner = 0; corner < corners; corner++)
			at = put_u32(at, (unsigned long)corner_vertices[corner]);
	}
	recording_write_block(walls, size + 5);
	free(walls);
}

/* the recording's file opened, its game and walls written */
static void recording_start(
	void)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *network_game = server ? network_game_server_get_game(server) : NULL;
	struct game_variant *variant = game_engine_get_variant();
	char map[64] = "";
	char server_name[64] = "";
	char game_type[64] = "";
	char file_name[MAX_PATH];
	char when[32] = "";
	char const *base;
	time_t now = time(NULL);
	struct tm *local = localtime(&now);
	unsigned long size;
	byte *at;
	short character;

	recording.started = TRUE;
	recording.started_time = game_time_get();
	recording.flushed_time = game_time_get();
	recording.length = 0;
	recording.tag_count = 0;
	for (character = 0; character < MAXIMUM_TRACKED_PLAYERS; character++)
		recording.players[character] = NONE;
	if (network_game)
	{
		snprintf(map, sizeof(map), "%s", network_game->map.name);
		for (character = 0; character < 16 && network_game->name[character] && character < (short)sizeof(server_name) - 1;
			character++)
		{
			server_name[character] = player_name_character_ascii(network_game->name[character]);
		}
		server_name[character] = 0;
	}
	if (variant)
	{
		for (character = 0; character < 12 && variant->human_readable_game_description[character] &&
			character < (short)sizeof(game_type) - 1; character++)
		{
			game_type[character] = player_name_character_ascii(variant->human_readable_game_description[character]);
		}
		game_type[character] = 0;
		/* (a built-in variant has no name of its own: its game type's, as
		game_events.c has it) */
		if (!game_type[0])
		{
			static char const *const engines[] = { "", "CTF", "Slayer", "Oddball", "King of the Hill", "Race" };
			long engine = variant->game_engine_index;

			snprintf(game_type, sizeof(game_type), "%s%s", variant->universal_variant.teams && engine == 2 ? "Team " : "",
				engine >= 0 && engine < (long)NUMBEROF(engines) ? engines[engine] : "");
		}
	}
	CreateDirectoryA(RECORDING_FOLDER, NULL);
	recording_prune();
	if (local)
		strftime(when, sizeof(when), "%Y-%m-%d_%H-%M-%S", local);
	/* (the map's own name, its folders gone, kept to letters, digits, -, _) */
	base = strrchr(map, '\\');
	base = base ? base + 1 : map;
	snprintf(file_name, sizeof(file_name), RECORDING_FOLDER "\\%s_", when);
	for (character = 0; base[character] && character < 40; character++)
	{
		char c = base[character];
		size_t length = strlen(file_name);

		file_name[length] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '_' ? c : '-';
		file_name[length + 1] = 0;
	}
	strncat(file_name, ".hrec", sizeof(file_name) - strlen(file_name) - 1);
	recording.file = fopen(file_name, "wb");
	if (!recording.file)
	{
		error(_error_log, "recording: could not make %s", file_name);
		recording.failed = TRUE;
		return;
	}
	{
		byte header[8] = { 'H', 'R', 'E', 'C', RECORDING_VERSION, 0, 0, 0 };

		fwrite(header, sizeof(header), 1, recording.file);
	}
	error(_error_silent, "recording: %s", file_name);
	size = 4 + 2 + text_size(server_name) + text_size(map) + text_size(game_type);
	at = recording_begin(_record_game, size);
	if (at)
	{
		at = put_u32(at, (unsigned long)now);
		at = put_u16(at, TICKS_PER_SECOND);
		at = put_text(at, server_name);
		at = put_text(at, map);
		put_text(at, game_type);
	}
	recording_walls();
}

/* the recording ended: its end written, the file closed */
static void recording_end(
	void)
{
	byte *at;

	if (recording.file)
	{
		at = recording_begin(_record_end, 4);
		if (at)
			put_u32(at, (unsigned long)game_time_get());
		recording_flush();
		if (recording.file)
			fclose(recording.file);
	}
	recording.file = NULL;
	recording.started = FALSE;
	recording.failed = FALSE;
	recording.length = 0;
}

/* a player's name, address and hardware id, when their slot has someone new */
static void recording_note_player(
	short player_index,
	struct player_datum *player)
{
	long player_id = DATUM_INDEX_NEW(player_index, player->identifier);
	long machine_index;
	char name[24];
	char hardware_id[40] = "";
	char address[32] = "";
	short character;
	byte *at;

	if (recording.players[player_index] == player_id)
		return;
	recording.players[player_index] = player_id;
	for (character = 0; character < (short)NUMBEROF(player->name) && player->name[character] &&
		character < (short)sizeof(name) - 1; character++)
	{
		name[character] = player_name_character_ascii(player->name[character]);
	}
	name[character] = 0;
	machine_index = distributed_player_machine(player_index);
	if (machine_index != NONE)
	{
		char const *id = network_game_server_machine_hardware_id(machine_index);

		if (id)
			p2p_hardware_id_sanitize(hardware_id, 33, id);
		distributed_machine_address_text(machine_index, address, sizeof(address));
	}
	at = recording_begin(_record_player, 4 + 1 + 1 + text_size(name) + text_size(hardware_id) + text_size(address));
	if (!at)
		return;
	at = put_u32(at, (unsigned long)game_time_get());
	at = put_u8(at, (unsigned long)player_index);
	at = put_u8(at, (unsigned long)(player->team_index & 0xFF));
	at = put_text(at, name);
	at = put_text(at, hardware_id);
	put_text(at, address);
}

/* ---------- public code */

void network_recording_new_game(
	void)
{
	recording_end();
}

void network_recording_host_tick(
	void)
{
	struct
	{
		short player_index;
		struct unit_datum *unit;
	} living[MAXIMUM_TRACKED_PLAYERS];
	short living_count = 0;
	short player_index;
	short index;
	byte *at;

	if (recording.failed || !config_boolean("network.record_games"))
		return;
	if (!recording.started)
	{
		if (!game_in_progress() || !global_collision_bsp_get())
			return;
		recording_start();
	}
	if (!recording.file)
		return;
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		struct player_datum *player = distributed_player(player_index);
		long unit_index;

		if (!player)
			continue;
		recording_note_player(player_index, player);
		unit_index = distributed_living_unit(player);
		if (unit_index == NONE)
			continue;
		living[living_count].player_index = player_index;
		living[living_count].unit = unit_get(unit_index);
		living_count++;
	}
	/* (the tags first: a frame refers to them) */
	for (index = 0; index < living_count; index++)
	{
		struct unit_datum *unit = living[index].unit;
		long weapon_index = unit->unit.current_weapon_index != NONE ?
			unit->unit.weapon_object_indices[unit->unit.current_weapon_index] : NONE;

		recording_tag(weapon_index != NONE ? object_get(weapon_index)->definition_index : NONE);
		recording_tag(unit->object.parent_object_index != NONE ?
			object_get(unit->object.parent_object_index)->definition_index : NONE);
	}
	at = recording_begin(_record_frame, 4 + 1 + 26 * (unsigned long)living_count);
	if (at)
	{
		at = put_u32(at, (unsigned long)game_time_get());
		at = put_u8(at, (unsigned long)living_count);
		for (index = 0; index < living_count; index++)
		{
			struct unit_datum *unit = living[index].unit;
			long unit_index = distributed_living_unit(distributed_player(living[index].player_index));
			long weapon_index = unit->unit.current_weapon_index != NONE ?
				unit->unit.weapon_object_indices[unit->unit.current_weapon_index] : NONE;
			real_point3d eye;
			real_vector3d aim = unit->unit.aiming_vector;
			byte flags = 0;
			real yaw, pitch, height;

			unit_get_camera_position(unit_index, &eye);
			normalize3d(&aim);
			yaw = (real)atan2(aim.j, aim.i);
			pitch = (real)asin(PIN(aim.k, -1.0f, 1.0f));
			height = (eye.z - unit->object.position.z) * 1000.0f;
			if (TEST_FLAG(unit->unit.control_flags, _unit_control_crouch_modifier_bit))
				flags |= 1;
			if (TEST_FLAG(unit->unit.control_flags, _unit_control_weapon_primary_trigger_bit))
				flags |= 2;
			if (TEST_FLAG(unit->unit.control_flags, _unit_control_weapon_secondary_trigger_bit))
				flags |= 4;
			/* (NONE unzoomed: a char, which is unsigned on ARM) */
			if ((signed char)unit->unit.current_zoom_level >= 0)
				flags |= 8;
			if (unit->object.parent_object_index != NONE)
				flags |= 16;
			at = put_u8(at, (unsigned long)living[index].player_index);
			at = put_u8(at, flags);
			at = put_real(at, unit->object.position.x);
			at = put_real(at, unit->object.position.y);
			at = put_real(at, unit->object.position.z);
			at = put_u16(at, (unsigned long)(short)PIN(height, -32000.0f, 32000.0f));
			at = put_u16(at, (unsigned long)(short)(yaw * 10000.0f));
			at = put_u16(at, (unsigned long)(short)(pitch * 10000.0f));
			at = put_u8(at, (unsigned long)PIN(unit->object.body_vitality * 255.0f, 0.0f, 255.0f));
			at = put_u8(at, (unsigned long)PIN(unit->object.shield_vitality * 127.5f, 0.0f, 255.0f));
			at = put_u16(at, recording_tag(weapon_index != NONE ? object_get(weapon_index)->definition_index : NONE));
			at = put_u16(at, recording_tag(unit->object.parent_object_index != NONE ?
				object_get(unit->object.parent_object_index)->definition_index : NONE));
		}
	}
	if (recording.length >= RECORDING_BLOCK_BYTES || game_time_get() - recording.flushed_time >= RECORDING_BLOCK_TICKS)
		recording_flush();
}

void network_recording_hit(
	short shooter_index,
	short victim_index,
	real_point3d const *impact,
	byte verdict,
	real angle,
	real distance)
{
	byte *at = recording_begin(_record_hit, 4 + 1 + 1 + 12 + 1 + 4 + 4);

	if (!at)
		return;
	at = put_u32(at, (unsigned long)game_time_get());
	at = put_u8(at, (unsigned long)shooter_index);
	at = put_u8(at, (unsigned long)victim_index);
	at = put_real(at, impact->x);
	at = put_real(at, impact->y);
	at = put_real(at, impact->z);
	at = put_u8(at, verdict);
	at = put_real(at, angle);
	put_real(at, distance);
}

void network_recording_move(
	short player_index,
	byte kind,
	real amount)
{
	byte *at = recording_begin(_record_move, 4 + 1 + 1 + 4);

	if (!at)
		return;
	at = put_u32(at, (unsigned long)game_time_get());
	at = put_u8(at, (unsigned long)player_index);
	at = put_u8(at, kind);
	put_real(at, amount);
}

void network_recording_kill(
	short killer_index,
	short victim_index,
	boolean headshot)
{
	byte *at = recording_begin(_record_kill, 4 + 1 + 1 + 1);

	if (!at)
		return;
	at = put_u32(at, (unsigned long)game_time_get());
	at = put_u8(at, (unsigned long)(killer_index & 0xFF));
	at = put_u8(at, (unsigned long)(victim_index & 0xFF));
	put_u8(at, headshot ? 1 : 0);
}
