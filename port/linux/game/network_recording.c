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

The file (little-endian): "HREC", a version (2; 1 before records 10 to 16
and 'G' blocks, 4 bytes), then blocks: a kind, the records' size and the
block's size (4 bytes each), and the block: 'G' the records as a gzip
member (event_gzip.c's, which a browser's DecompressionStream "gzip"
opens), 'R' the records as they are (a block gzip would not shrink), 'B'
the records compressed with zlib (the first recordings', "deflate"). A
record is its type (1 byte), its size (4 bytes) and that many bytes; one of
a type a reader does not know it skips:

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
 10 objects:  tick (4), whole (1: 1 every object, every 5 seconds; 0 those
              that moved or are new), count (2), then each: object (4, its
              datum index), type (1: objects.h's, a vehicle 1, a weapon 2,
              equipment 3, garbage 4, a projectile 5, a machine 7), tag (2),
              position (3 floats), forward and up (3 each, 2 bytes: times
              32767), flags (1: at rest 1); a whole pass of more than 2048
              objects is several records; every 2 ticks: vehicles, items
              no one holds, projectiles and grenades, doors and elevators
 11 gone:     tick (4), count (2), then each object (4) gone since
 12 pose:     tick (4), count (1), then each living player's: player (1),
              body facing (2, radians times 10000), velocity (3, 2 bytes
              each: world units a tick times 1000), animation state (1:
              units.h's _unit_state_*), action (1), flags (1: flashlight 1,
              camouflaged 2, melee 4, throwing a grenade 8, reloading 16),
              animation graph (2, a tag's number), animation and its frame
              (2 each), camouflage (1, of 255), frag and plasma grenades (1
              each), the weapon's rounds loaded and total (2 each), the other
              weapon (2, a tag's number), seat (1, 255 none)
 13 event:    tick (4), kind (1: event_log.h's EVENT_LOG_*: a kill 0, a medal
              1, an objective 2, a pickup 3, a ride 4, a spawn 5, a
              moderator's action 9, an anti-cheat's flag 10), player and other
              (1 each, 255 none), tags (3, 2 each: a tag's number), values
              (4, 4 each), bits (4), position and other position (3 floats
              each), as Delta Stats' recorder notes them (game_events.c; only
              while it records, network.report_events)
 14 score:    tick (4), score to win (4), team scores (2, 4 each), count (1),
              then each player's: player (1), team (1), kills, deaths,
              assists, suicides and betrayals (2 each); every second
 15 chat:     tick (4), kind and scope (1 each, delta_wire.h's), speaker (1:
              the game's player list's index, 255 the server or the game),
              the speaker's name, text
 16 map:      family (1: halo_map_families.h's), file (its name, without
              .map), hash (32: SHA-256, map_share.h's; zeros if unknown)

A block goes to the file every 10 seconds, or sooner when 192 kilobytes of
records have come: a host that stops loses no more than that.

With network.record_public on too, each block also goes to the game's
public copy, recordings\public\<the same name>, for anyone to watch, live
or again (public_records): a player record without its hardware id and
address (both empty), a hit without its verdict, degrees off aim and
distance (zeros), and no move records, nor events of a moderator's action
or an anti-cheat's flag, nor team chat; the format is the same, so any
reader opens both.
A block that shows the game being played (any record but the game's, its
map's, its walls and its tags) is held back network.record_public_delay seconds before
it is written, so that no one watching can tell its players where the
others are; the rest of the game is written when it ends.
recordings\public\live.json says which copy is being written: {"live":
true or false, "file", "server", "map", "game_type", "started" (unix
time), "delay" (seconds), "ticks_per_second"}. When the game list takes the
game's carnage report, <name>.json beside the copy gives its address:
{"report": "<network.browser_url>/games/<id>"}.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "items/weapons.h"
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
#include "map_share.h"
#include "halo_map_families.h"

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
/* (event_log.h's: a gzip member, the C library's malloc; freed by
event_log_free, as this unit's free is the game's allocator's) */
unsigned char *event_gzip(unsigned char const *data, size_t length, size_t *compressed_length);
void event_log_free(char *json);

/* ---------- constants */

#define RECORDING_FOLDER "d:\\recordings"
#define RECORDING_VERSION 2

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
	_record_objects,
	_record_gone,
	_record_pose,
	_record_event,
	_record_score,
	_record_chat,
	_record_map,
};

/* event_log.h's EVENT_LOG_* the public copy leaves out: a moderator's
action and an anti-cheat's flag */
enum
{
	RECORDING_EVENT_MODERATION = 9,
	RECORDING_EVENT_FLAG = 10,
	/* a chat line's scope the public copy leaves out (delta_wire.h's
	_delta_chat_scope_team) */
	RECORDING_CHAT_SCOPE_TEAM = 1,
};

enum
{
	/* records gathered before a block is written, and how long at most */
	RECORDING_BLOCK_BYTES = 192 * 1024,
	RECORDING_BUFFER_BYTES = 256 * 1024,
	RECORDING_BLOCK_TICKS = 10 * TICKS_PER_SECOND,
	/* the tags numbered, at most, and their names' length */
	RECORDING_MAXIMUM_TAGS = 512,
	RECORDING_TAG_NAME_SIZE = 96,
	/* the objects followed (by absolute index; one past it is not
	recorded), how often (ticks), and how often every one of them is */
	RECORDING_MAXIMUM_OBJECTS = MAXIMUM_OBJECTS_PER_MAP,
	/* the objects one record holds, at most (a record is within a block) */
	RECORDING_OBJECTS_PER_RECORD = 2048,
	RECORDING_OBJECT_TICKS = 2,
	RECORDING_WHOLE_OBJECT_TICKS = 5 * TICKS_PER_SECOND,
	RECORDING_OBJECT_BYTES = 4 + 1 + 2 + 12 + 6 + 6 + 1,
	RECORDING_POSE_BYTES = 1 + 2 + 6 + 1 + 1 + 1 + 2 + 2 + 2 + 1 + 1 + 1 + 2 + 2 + 2 + 1,
	RECORDING_SCORE_TICKS = TICKS_PER_SECOND,
	/* the walls' vertices and polygons, at most */
	RECORDING_MAXIMUM_WALL_BYTES = 8 * 1024 * 1024,
	/* the files a folder is pruned of, at most at once */
	RECORDING_MAXIMUM_FILES = 1024,
	/* the public copy's blocks held back (network.record_public_delay), at
	most, and their bytes: past these the oldest is written early */
	PUBLIC_MAXIMUM_BLOCKS = 512,
	PUBLIC_MAXIMUM_BYTES = 16 * 1024 * 1024,
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
	/* the tags numbered so far, by their names */
	char tags[RECORDING_MAXIMUM_TAGS][RECORDING_TAG_NAME_SIZE];
	short tag_count;
	/* the tag indices numbered so far, and their numbers */
	long tag_indices[RECORDING_MAXIMUM_TAGS];
	unsigned long tag_numbers[RECORDING_MAXIMUM_TAGS];
	short tag_index_count;
	/* the objects written: each one's datum index (NONE: none) and what was
	written of it, by absolute index; when they and every one were last
	written, and the scores */
	struct
	{
		long object_index;
		real_point3d position;
		short forward[3];
		short up[3];
		boolean seen;
	} objects[RECORDING_MAXIMUM_OBJECTS];
	long objects_time;
	long whole_objects_time;
	long score_time;
	/* the player each slot had when last told (DATUM_INDEX_NEW), NONE, and
	their team then (a team change tells them again) */
	long players[MAXIMUM_TRACKED_PLAYERS];
	short player_teams[MAXIMUM_TRACKED_PLAYERS];
	/* the game's last tick recorded (the end's: the game's time is reset by
	then) */
	long last_tick;
	/* the public copy (network.record_public): its file, its name in the
	public folder (kept after it ends, for its carnage report), what its
	game is, and its blocks held back until they are old enough */
	FILE *public_file;
	char public_name[MAX_PATH];
	char public_server[64];
	char public_map[64];
	char public_game_type[64];
	unsigned long public_started;
	struct
	{
		long time;
		unsigned long length;
		byte *data;
	} pending[PUBLIC_MAXIMUM_BLOCKS];
	short pending_first;
	short pending_count;
	unsigned long pending_bytes;
} recording;

/* ---------- private code */

/* records written as a block; the recording ended if they cannot be */
/* records written to a file as a block: a gzip member ('G', event_gzip.c's
deflate: the game's own zlib, 1.1.3, compresses nothing right in the
64-bit builds' sources, the dedicated servers', and crashed them), or as
they are ('R') when that would not shrink them; FALSE if it cannot be */
static boolean block_write(
	FILE *file,
	byte const *records,
	unsigned long length)
{
	byte header[9];
	size_t packed_length = 0;
	unsigned char *packed = event_gzip(records, length, &packed_length);
	byte const *block = records;
	unsigned long block_length = length;
	boolean written;

	header[0] = 'R';
	if (packed && packed_length < length)
	{
		header[0] = 'G';
		block = packed;
		block_length = (unsigned long)packed_length;
	}
	header[1] = (byte)length;
	header[2] = (byte)(length >> 8);
	header[3] = (byte)(length >> 16);
	header[4] = (byte)(length >> 24);
	header[5] = (byte)block_length;
	header[6] = (byte)(block_length >> 8);
	header[7] = (byte)(block_length >> 16);
	header[8] = (byte)(block_length >> 24);
	written = fwrite(header, sizeof(header), 1, file) == 1 && fwrite(block, block_length, 1, file) == 1;
	if (packed)
		event_log_free((char *)packed);
	if (written)
		fflush(file);
	return written;
}

static void recording_write_block(
	byte const *records,
	unsigned long length)
{
	if (!recording.file || !length)
		return;
	if (!block_write(recording.file, records, length))
	{
		error(_error_log, "recording: could not write to the recording (a full disk?); it ends");
		fclose(recording.file);
		recording.file = NULL;
		recording.failed = TRUE;
	}
}

/* ---------- the public copy (network.record_public) */

/* the records as anyone may see them, written to out (as large as the
records: nothing grows): a player's hardware id and address left out, a
hit's fair play verdict, how far off aim and how far it went zeroed, and
the moves the host refused left out, the events of a moderator's action
or an anti-cheat's flag, and team chat; *live is set when a record shows the game
as it is played (any but the game's, its map's, its walls, its tags and its
players) */
static unsigned long public_records(
	byte const *records,
	unsigned long length,
	byte *out,
	boolean *live)
{
	unsigned long read = 0;
	unsigned long written = 0;

	*live = FALSE;
	while (read + 5 <= length)
	{
		byte type = records[read];
		unsigned long size = (unsigned long)records[read + 1] | ((unsigned long)records[read + 2] << 8) |
			((unsigned long)records[read + 3] << 16) | ((unsigned long)records[read + 4] << 24);
		byte const *payload = records + read + 5;

		if (size > length - read - 5)
			break;
		read += 5 + size;
		if (type != _record_game && type != _record_walls && type != _record_tag && type != _record_player &&
			type != _record_map)
		{
			*live = TRUE;
		}
		if (type == _record_move)
			continue;
		/* (an event: tick 4, then its kind) */
		if (type == _record_event && size >= 5 &&
			(payload[4] == RECORDING_EVENT_MODERATION || payload[4] == RECORDING_EVENT_FLAG))
		{
			continue;
		}
		/* (a chat line: tick 4, kind 1, then its scope; a team's own lines are
		that team's, not for the other to read) */
		if (type == _record_chat && size >= 6 && payload[5] == RECORDING_CHAT_SCOPE_TEAM)
			continue;
		if (type == _record_player && size >= 7 && 6 + 1 + (unsigned long)payload[6] <= size)
		{
			/* (tick, player, team and name kept; an empty hardware id and
			address after them) */
			unsigned long kept = 6 + 1 + payload[6];

			out[written] = type;
			out[written + 1] = (byte)(kept + 2);
			out[written + 2] = (byte)((kept + 2) >> 8);
			out[written + 3] = 0;
			out[written + 4] = 0;
			csmemcpy(out + written + 5, payload, kept);
			out[written + 5 + kept] = 0;
			out[written + 5 + kept + 1] = 0;
			written += 5 + kept + 2;
			continue;
		}
		csmemcpy(out + written, records + read - 5 - size, 5 + size);
		/* (a hit: tick 4, shooter 1, target 1, where 12, then the verdict 1,
		degrees off aim 4 and distance 4, zeroed) */
		if (type == _record_hit && size >= 27)
			csmemset(out + written + 5 + 18, 0, 9);
		written += 5 + size;
	}
	return written;
}

/* a block of the public copy written; the copy ends if it cannot be */
static void public_write(
	byte const *records,
	unsigned long length)
{
	if (!recording.public_file || !length)
		return;
	if (!block_write(recording.public_file, records, length))
	{
		error(_error_log, "recording: could not write to the public copy (a full disk?); it ends");
		fclose(recording.public_file);
		recording.public_file = NULL;
	}
}

/* the held back blocks written: those old enough, or all of them */
static void public_write_due(
	boolean all)
{
	long delay = config_integer("network.record_public_delay") * TICKS_PER_SECOND;
	long now = game_time_get();

	while (recording.pending_count)
	{
		short first = recording.pending_first;

		if (!all && now - recording.pending[first].time < delay &&
			recording.pending_count < PUBLIC_MAXIMUM_BLOCKS && recording.pending_bytes <= PUBLIC_MAXIMUM_BYTES)
		{
			break;
		}
		public_write(recording.pending[first].data, recording.pending[first].length);
		recording.pending_bytes -= recording.pending[first].length;
		free(recording.pending[first].data);
		recording.pending[first].data = NULL;
		recording.pending_first = (short)((first + 1) % PUBLIC_MAXIMUM_BLOCKS);
		recording.pending_count--;
	}
}

/* a block's records added to the public copy: at once if they show nothing
of the game being played (the map's walls, a game's start) and nothing is
held back before them, else held back network.record_public_delay seconds,
so that no one can watch a game being played to tell its players where the
others are */
static void public_add(
	byte const *records,
	unsigned long length)
{
	byte *copy;
	boolean live;
	unsigned long kept;
	short last;

	if (!recording.public_file || !length)
		return;
	copy = malloc(length);
	if (!copy)
		return;
	kept = public_records(records, length, copy, &live);
	if (!kept)
	{
		free(copy);
		return;
	}
	if (!live && !recording.pending_count)
	{
		public_write(copy, kept);
		free(copy);
		return;
	}
	if (recording.pending_count >= PUBLIC_MAXIMUM_BLOCKS)
		public_write_due(FALSE);
	last = (short)((recording.pending_first + recording.pending_count) % PUBLIC_MAXIMUM_BLOCKS);
	recording.pending[last].time = game_time_get();
	recording.pending[last].length = kept;
	recording.pending[last].data = copy;
	recording.pending_count++;
	recording.pending_bytes += kept;
}

/* text for a JSON string: quotes, backslashes and controls left out */
static void public_json_text(
	char *out,
	size_t size,
	char const *text)
{
	size_t used = 0;

	for (; *text && used + 1 < size; text++)
	{
		if (*text != '"' && *text != '\\' && (unsigned char)*text >= 0x20)
			out[used++] = *text;
	}
	out[used] = 0;
}

/* the public folder's live.json, which a page showing the game live reads:
whether a game is being recorded now, its file, and what it is */
static void public_status_write(
	boolean live)
{
	char server[64], map[64], game_type[64], name[MAX_PATH];
	FILE *file;

	if (!recording.public_name[0])
		return;
	public_json_text(server, sizeof(server), recording.public_server);
	public_json_text(map, sizeof(map), recording.public_map);
	public_json_text(game_type, sizeof(game_type), recording.public_game_type);
	public_json_text(name, sizeof(name), recording.public_name);
	file = fopen(RECORDING_FOLDER "\\public\\live.json.new", "wb");
	if (!file)
		return;
	fprintf(file, "{\"live\": %s, \"file\": \"%s\", \"server\": \"%s\", \"map\": \"%s\", \"game_type\": \"%s\", "
		"\"started\": %lu, \"delay\": %ld, \"ticks_per_second\": %d}\n",
		live ? "true" : "false", name, server, map, game_type, recording.public_started,
		config_integer("network.record_public_delay"), TICKS_PER_SECOND);
	fclose(file);
	/* (MoveFileA replaces nothing) */
	DeleteFileA(RECORDING_FOLDER "\\public\\live.json");
	MoveFileA(RECORDING_FOLDER "\\public\\live.json.new", RECORDING_FOLDER "\\public\\live.json");
}

/* the records so far written as a block */
static void recording_flush(
	void)
{
	recording.flushed_time = game_time_get();
	recording_write_block(recording.buffer, recording.length);
	public_add(recording.buffer, recording.length);
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

/* a tag's number in the recording by its name (told the first time),
RECORDING_NO_TAG for none. (Never while a record is being written: the tag
record would land inside it) */
static unsigned long recording_tag_named(
	char const *name)
{
	byte *at;
	short index;

	if (!name || !name[0])
		return RECORDING_NO_TAG;
	for (index = 0; index < recording.tag_count; index++)
	{
		if (!strncmp(recording.tags[index], name, RECORDING_TAG_NAME_SIZE - 1))
			return (unsigned long)index;
	}
	if (recording.tag_count >= RECORDING_MAXIMUM_TAGS)
		return RECORDING_NO_TAG;
	at = recording_begin(_record_tag, 2 + text_size(name));
	if (!at)
		return RECORDING_NO_TAG;
	index = recording.tag_count++;
	snprintf(recording.tags[index], sizeof(recording.tags[index]), "%s", name);
	at = put_u16(at, (unsigned long)index);
	put_text(at, name);
	return (unsigned long)index;
}

/* the tag's number in the recording, RECORDING_NO_TAG for none (each tag
index's kept: several are asked for each player every tick) */
static unsigned long recording_tag(
	long tag_index)
{
	short index;
	unsigned long number;

	if (tag_index == NONE)
		return RECORDING_NO_TAG;
	for (index = 0; index < recording.tag_index_count; index++)
	{
		if (recording.tag_indices[index] == tag_index)
			return recording.tag_numbers[index];
	}
	number = recording_tag_named(tag_get_name(tag_index));
	if (number != RECORDING_NO_TAG && recording.tag_index_count < RECORDING_MAXIMUM_TAGS)
	{
		recording.tag_indices[recording.tag_index_count] = tag_index;
		recording.tag_numbers[recording.tag_index_count] = number;
		recording.tag_index_count++;
	}
	return number;
}

/* the oldest recordings deleted while the folder holds more than its
megabytes (the names begin with their time: the oldest sorts first) */
static void recording_prune(
	char const *folder,
	char const *setting)
{
	static char names[RECORDING_MAXIMUM_FILES][MAX_PATH];
	static unsigned long sizes[RECORDING_MAXIMUM_FILES];
	WIN32_FIND_DATAA data;
	HANDLE find;
	long count = 0;
	long index;
	double total = 0.0;
	double limit = (double)config_integer(setting) * 1024.0 * 1024.0;
	char pattern[MAX_PATH];

	if (!(limit > 0.0))
		return;
	snprintf(pattern, sizeof(pattern), "%s\\*.hrec", folder);
	find = FindFirstFileA(pattern, &data);
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
		snprintf(path, sizeof(path), "%s\\%s", folder, names[oldest]);
		if (DeleteFileA(path))
			error(_error_silent, "recording: %s deleted (the folder is over %s)", names[oldest], setting);
		/* (and its carnage report's link beside it, a public copy's) */
		if (strlen(path) > 5)
		{
			strcpy(path + strlen(path) - 5, ".json");
			DeleteFileA(path);
		}
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
	/* (the public copy's too: the walls show no one where anyone is) */
	public_add(walls, size + 5);
	free(walls);
}

/* the map's family, file and hash, for a replay viewer to find the map
itself and draw it as the game does */
static void recording_map(
	char const *map)
{
	char file[MAX_PATH] = "";
	char path[MAX_PATH] = "";
	byte hash[MAP_SHARE_HASH_BYTES];
	short family = map[0] ? map_family_parse(map, file, sizeof(file)) : NONE;
	byte *at;

	csmemset(hash, 0, sizeof(hash));
	if (family == NONE || !file[0])
		return;
	if (map_family_find(family, file, path, sizeof(path)) && !map_share_map_hash(path, hash))
		csmemset(hash, 0, sizeof(hash));
	at = recording_begin(_record_map, 1 + text_size(file) + MAP_SHARE_HASH_BYTES);
	if (!at)
		return;
	at = put_u8(at, (unsigned long)family);
	at = put_text(at, file);
	csmemcpy(at, hash, MAP_SHARE_HASH_BYTES);
}

/* a unit vector as 3 shorts (times 32767) */
static void vector_quantize(
	real_vector3d const *vector,
	short *out)
{
	out[0] = (short)PIN(vector->i * 32767.0f, -32767.0f, 32767.0f);
	out[1] = (short)PIN(vector->j * 32767.0f, -32767.0f, 32767.0f);
	out[2] = (short)PIN(vector->k * 32767.0f, -32767.0f, 32767.0f);
}

/* whether an object is one the recording follows: a vehicle, an item no
one holds, a projectile or grenade, a door or elevator */
static boolean recording_object_followed(
	struct object_datum const *object)
{
	switch (object->object.type)
	{
		case _object_type_vehicle:
		case _object_type_projectile:
		case _object_type_machine:
			return TRUE;
		case _object_type_weapon:
		case _object_type_equipment:
		case _object_type_garbage:
			return object->object.parent_object_index == NONE;
		default:
			return FALSE;
	}
}

/* the objects that moved or are new since they were last written (all of
them every RECORDING_WHOLE_OBJECT_TICKS), and those gone */
static void recording_objects(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	long now = game_time_get();
	boolean whole = recording.whole_objects_time == NONE || now - recording.whole_objects_time >= RECORDING_WHOLE_OBJECT_TICKS;
	static long written[RECORDING_MAXIMUM_OBJECTS];
	static long gone[RECORDING_MAXIMUM_OBJECTS];
	long written_count = 0;
	long gone_count = 0;
	long index, first;
	byte *at;

	if (whole)
		recording.whole_objects_time = now;
	for (index = 0; index < RECORDING_MAXIMUM_OBJECTS; index++)
		recording.objects[index].seen = FALSE;
	/* (which to write, their tags numbered first: a tag record must not land
	inside the objects') */
	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index);
		short forward[3], up[3];
		real_vector3d moved;

		if (absolute_index < 0 || absolute_index >= RECORDING_MAXIMUM_OBJECTS || !recording_object_followed(object))
			continue;
		recording.objects[absolute_index].seen = TRUE;
		vector_quantize(&object->object.forward, forward);
		vector_quantize(&object->object.up, up);
		vector_from_points3d(&recording.objects[absolute_index].position, &object->object.position, &moved);
		if (!whole && recording.objects[absolute_index].object_index == iterator.index &&
			magnitude_squared3d(&moved) < 0.0001f &&
			!csmemcmp(forward, recording.objects[absolute_index].forward, sizeof(forward)) &&
			!csmemcmp(up, recording.objects[absolute_index].up, sizeof(up)))
		{
			continue;
		}
		recording_tag(object->definition_index);
		written[written_count++] = absolute_index;
		/* (another object at the place of one written: that one is gone) */
		if (recording.objects[absolute_index].object_index != NONE &&
			recording.objects[absolute_index].object_index != iterator.index)
		{
			gone[gone_count++] = recording.objects[absolute_index].object_index;
		}
		recording.objects[absolute_index].object_index = iterator.index;
		recording.objects[absolute_index].position = object->object.position;
		csmemcpy(recording.objects[absolute_index].forward, forward, sizeof(forward));
		csmemcpy(recording.objects[absolute_index].up, up, sizeof(up));
	}
	for (index = 0; index < RECORDING_MAXIMUM_OBJECTS; index++)
	{
		if (recording.objects[index].object_index != NONE && !recording.objects[index].seen)
		{
			gone[gone_count++] = recording.objects[index].object_index;
			recording.objects[index].object_index = NONE;
		}
	}
	if (gone_count && (at = recording_begin(_record_gone, 4 + 2 + 4 * (unsigned long)gone_count)) != NULL)
	{
		at = put_u32(at, (unsigned long)now);
		at = put_u16(at, (unsigned long)gone_count);
		for (index = 0; index < gone_count; index++)
			at = put_u32(at, (unsigned long)gone[index]);
	}
	if (!written_count && !whole)
		return;
	/* (a record of at most RECORDING_OBJECTS_PER_RECORD of them at a time:
	a map's every object would be more than a block) */
	for (first = 0; first < written_count || (first == 0 && whole); first += RECORDING_OBJECTS_PER_RECORD)
	{
		long count = MIN(written_count - first, RECORDING_OBJECTS_PER_RECORD);

		at = recording_begin(_record_objects, 4 + 1 + 2 + RECORDING_OBJECT_BYTES * (unsigned long)count);
		if (!at)
			return;
		at = put_u32(at, (unsigned long)now);
		at = put_u8(at, whole ? 1 : 0);
		at = put_u16(at, (unsigned long)count);
		for (index = first; index < first + count; index++)
		{
			long absolute_index = written[index];
			long object_index = recording.objects[absolute_index].object_index;
			struct object_datum *datum = object_get(object_index);
			short const *forward = recording.objects[absolute_index].forward;
			short const *up = recording.objects[absolute_index].up;

			at = put_u32(at, (unsigned long)object_index);
			at = put_u8(at, (unsigned long)datum->object.type);
			at = put_u16(at, recording_tag(datum->definition_index));
			at = put_real(at, datum->object.position.x);
			at = put_real(at, datum->object.position.y);
			at = put_real(at, datum->object.position.z);
			at = put_u16(at, (unsigned long)(unsigned short)forward[0]);
			at = put_u16(at, (unsigned long)(unsigned short)forward[1]);
			at = put_u16(at, (unsigned long)(unsigned short)forward[2]);
			at = put_u16(at, (unsigned long)(unsigned short)up[0]);
			at = put_u16(at, (unsigned long)(unsigned short)up[1]);
			at = put_u16(at, (unsigned long)(unsigned short)up[2]);
			at = put_u8(at, magnitude_squared3d(&datum->object.translational_velocity) < 0.000001f ? 1 : 0);
		}
		if (!written_count)
			break;
	}
}

/* a unit's weapon in a slot (NONE: none) */
static long unit_weapon(
	struct unit_datum const *unit,
	short slot)
{
	return slot >= 0 && slot < MAXIMUM_WEAPONS_PER_UNIT ? unit->unit.weapon_object_indices[slot] : NONE;
}

/* the weapon a unit holds other than its current one (NONE: none) */
static long unit_other_weapon(
	struct unit_datum const *unit)
{
	short slot;

	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		if (slot != unit->unit.current_weapon_index && unit->unit.weapon_object_indices[slot] != NONE)
			return unit->unit.weapon_object_indices[slot];
	}
	return NONE;
}

/* each living player's pose: how they stand and move, what they do and carry */
static void recording_pose(
	short const *player_indices,
	struct unit_datum *const *units,
	short count)
{
	short index;
	byte *at;

	/* (the tags first) */
	for (index = 0; index < count; index++)
	{
		long other = unit_other_weapon(units[index]);

		recording_tag(units[index]->object.animation.animation_graph_index);
		recording_tag(other != NONE ? object_get(other)->definition_index : NONE);
	}
	at = recording_begin(_record_pose, 4 + 1 + RECORDING_POSE_BYTES * (unsigned long)count);
	if (!at)
		return;
	at = put_u32(at, (unsigned long)game_time_get());
	at = put_u8(at, (unsigned long)count);
	for (index = 0; index < count; index++)
	{
		struct unit_datum const *unit = units[index];
		long weapon_index = unit_weapon(unit, unit->unit.current_weapon_index);
		long other = unit_other_weapon(unit);
		real_vector3d const *velocity = &unit->object.translational_velocity;
		real facing = (real)atan2(unit->object.forward.j, unit->object.forward.i);
		unsigned long rounds_loaded = 0, rounds_total = 0;
		byte flags = 0;

		if (weapon_index != NONE)
		{
			struct weapon_datum *weapon = weapon_get(weapon_index);

			rounds_loaded = (unsigned long)(unsigned short)MAX(weapon->weapon.magazines[0].rounds_loaded, 0);
			rounds_total = (unsigned long)(unsigned short)MAX(weapon->weapon.magazines[0].rounds_total, 0);
		}
		if (TEST_FLAG(unit->unit.flags, _unit_integrated_light_on_bit))
			flags |= 1;
		if (unit->unit.active_camouflage > 0.05f)
			flags |= 2;
		if (unit->unit.melee_attack_state > 0)
			flags |= 4;
		if (unit->unit.grenade_throw_state > 0)
			flags |= 8;
		if (TEST_FLAG(unit->unit.control_flags, _unit_control_weapon_reload_bit))
			flags |= 16;
		at = put_u8(at, (unsigned long)player_indices[index]);
		at = put_u16(at, (unsigned long)(unsigned short)(short)(facing * 10000.0f));
		at = put_u16(at, (unsigned long)(unsigned short)(short)PIN(velocity->i * 1000.0f, -32767.0f, 32767.0f));
		at = put_u16(at, (unsigned long)(unsigned short)(short)PIN(velocity->j * 1000.0f, -32767.0f, 32767.0f));
		at = put_u16(at, (unsigned long)(unsigned short)(short)PIN(velocity->k * 1000.0f, -32767.0f, 32767.0f));
		at = put_u8(at, (unsigned long)(byte)unit->unit.animation.state);
		at = put_u8(at, (unsigned long)(byte)unit->unit.animation.action);
		at = put_u8(at, flags);
		at = put_u16(at, recording_tag(unit->object.animation.animation_graph_index));
		at = put_u16(at, (unsigned long)(unsigned short)unit->object.animation.state.index);
		at = put_u16(at, (unsigned long)(unsigned short)unit->object.animation.state.frame_index);
		at = put_u8(at, (unsigned long)PIN(unit->unit.active_camouflage * 255.0f, 0.0f, 255.0f));
		at = put_u8(at, (unsigned long)(byte)MAX(unit->unit.grenade_counts[0], 0));
		at = put_u8(at, NUMBER_OF_UNIT_GRENADE_TYPES > 1 ? (unsigned long)(byte)MAX(unit->unit.grenade_counts[1], 0) : 0);
		at = put_u16(at, rounds_loaded);
		at = put_u16(at, rounds_total);
		at = put_u16(at, recording_tag(other != NONE ? object_get(other)->definition_index : NONE));
		at = put_u8(at, unit->object.parent_object_index != NONE && unit->unit.parent_seat_index >= 0 ?
			(unsigned long)(byte)unit->unit.parent_seat_index : 255);
	}
}

/* the teams' scores and each player's tally, every second */
static void recording_score(
	void)
{
	struct game_variant *variant = game_engine_get_variant();
	short slots[MAXIMUM_TRACKED_PLAYERS];
	short count = 0;
	short player_index;
	byte *at;

	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		if (distributed_player(player_index))
			slots[count++] = player_index;
	}
	at = recording_begin(_record_score, 4 + 4 + 8 + 1 + 12 * (unsigned long)count);
	if (!at)
		return;
	at = put_u32(at, (unsigned long)game_time_get());
	at = put_u32(at, variant ? (unsigned long)variant->universal_variant.score_to_win : 0);
	at = put_u32(at, (unsigned long)game_engine_get_team_score(0));
	at = put_u32(at, (unsigned long)game_engine_get_team_score(1));
	at = put_u8(at, (unsigned long)count);
	for (player_index = 0; player_index < count; player_index++)
	{
		struct player_datum *player = distributed_player(slots[player_index]);

		at = put_u8(at, (unsigned long)slots[player_index]);
		at = put_u8(at, (unsigned long)(player->team_index & 0xFF));
		at = put_u16(at, (unsigned long)(unsigned short)player->statistics.kills[0]);
		at = put_u16(at, (unsigned long)(unsigned short)player->statistics.deaths);
		at = put_u16(at, (unsigned long)(unsigned short)player->statistics.assists[0]);
		at = put_u16(at, (unsigned long)(unsigned short)player->statistics.suicides);
		at = put_u16(at, (unsigned long)(unsigned short)player->statistics.friendly_fire_kills);
	}
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
	recording.last_tick = game_time_get();
	recording.flushed_time = game_time_get();
	recording.length = 0;
	recording.tag_count = 0;
	recording.tag_index_count = 0;
	for (character = 0; character < MAXIMUM_TRACKED_PLAYERS; character++)
		recording.players[character] = NONE;
	{
		long object;

		for (object = 0; object < RECORDING_MAXIMUM_OBJECTS; object++)
			recording.objects[object].object_index = NONE;
	}
	/* (every object, and the scores, at the first tick) */
	recording.objects_time = NONE;
	recording.whole_objects_time = NONE;
	recording.score_time = NONE;
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
	recording_prune(RECORDING_FOLDER, "network.recordings_megabytes");
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
	/* (the public copy, network.record_public: the same name in the public
	folder, its game told in live.json) */
	recording.public_file = NULL;
	recording.public_name[0] = 0;
	if (config_boolean("network.record_public"))
	{
		char public_path[MAX_PATH + 16];
		char const *name = strrchr(file_name, '\\') + 1;

		CreateDirectoryA(RECORDING_FOLDER "\\public", NULL);
		recording_prune(RECORDING_FOLDER "\\public", "network.record_public_megabytes");
		snprintf(public_path, sizeof(public_path), RECORDING_FOLDER "\\public\\%s", name);
		recording.public_file = fopen(public_path, "wb");
		if (recording.public_file)
		{
			byte header[8] = { 'H', 'R', 'E', 'C', RECORDING_VERSION, 0, 0, 0 };

			fwrite(header, sizeof(header), 1, recording.public_file);
			snprintf(recording.public_name, sizeof(recording.public_name), "%s", name);
			snprintf(recording.public_server, sizeof(recording.public_server), "%s", server_name);
			snprintf(recording.public_map, sizeof(recording.public_map), "%s", base);
			snprintf(recording.public_game_type, sizeof(recording.public_game_type), "%s", game_type);
			recording.public_started = (unsigned long)now;
			public_status_write(TRUE);
		}
		else
			error(_error_log, "recording: could not make the public copy %s", public_path);
	}
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
	recording_map(map);
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
			put_u32(at, (unsigned long)recording.last_tick);
		recording_flush();
		if (recording.file)
			fclose(recording.file);
	}
	/* (the public copy whole: the game is over, nothing held back is a
	help to its players any more) */
	if (recording.public_file)
	{
		public_write_due(TRUE);
		fclose(recording.public_file);
		recording.public_file = NULL;
		public_status_write(FALSE);
	}
	while (recording.pending_count)
	{
		free(recording.pending[recording.pending_first].data);
		recording.pending[recording.pending_first].data = NULL;
		recording.pending_first = (short)((recording.pending_first + 1) % PUBLIC_MAXIMUM_BLOCKS);
		recording.pending_count--;
	}
	recording.pending_bytes = 0;
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

	if (recording.players[player_index] == player_id && recording.player_teams[player_index] == player->team_index)
		return;
	recording.players[player_index] = player_id;
	recording.player_teams[player_index] = (short)player->team_index;
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
	recording.last_tick = game_time_get();
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
	/* (how each living player stands, moves and what they carry; the
	objects every few ticks; the scores every second) */
	{
		short pose_players[MAXIMUM_TRACKED_PLAYERS];
		struct unit_datum *pose_units[MAXIMUM_TRACKED_PLAYERS];
		long now = game_time_get();

		for (index = 0; index < living_count; index++)
		{
			pose_players[index] = living[index].player_index;
			pose_units[index] = living[index].unit;
		}
		recording_pose(pose_players, pose_units, living_count);
		if (recording.objects_time == NONE || now - recording.objects_time >= RECORDING_OBJECT_TICKS)
		{
			recording.objects_time = now;
			recording_objects();
		}
		if (recording.score_time == NONE || now - recording.score_time >= RECORDING_SCORE_TICKS)
		{
			recording.score_time = now;
			recording_score();
		}
	}
	if (recording.length >= RECORDING_BLOCK_BYTES || game_time_get() - recording.flushed_time >= RECORDING_BLOCK_TICKS)
		recording_flush();
	/* (the public copy's held back blocks, once old enough) */
	public_write_due(FALSE);
}

int network_recording_public_name(
	char *name,
	int size)
{
	if (!recording.public_name[0] || size <= 0)
		return 0;
	snprintf(name, (size_t)size, "%s", recording.public_name);
	return 1;
}

void network_recording_public_report(
	char const *name,
	char const *report_url)
{
	char path[MAX_PATH + 32];
	char url[256];
	size_t length = strlen(name);
	FILE *file;

	/* (only a name the public folder could have: no folders) */
	if (length < 6 || length >= MAX_PATH || strpbrk(name, "\\/:") || strcmp(name + length - 5, ".hrec"))
		return;
	public_json_text(url, sizeof(url), report_url);
	snprintf(path, sizeof(path), RECORDING_FOLDER "\\public\\%.*s.json", (int)(length - 5), name);
	file = fopen(path, "wb");
	if (!file)
		return;
	fprintf(file, "{\"report\": \"%s\"}\n", url);
	fclose(file);
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

void network_recording_event(
	long tick,
	short type,
	short player_index,
	short other_index,
	char const *const tags[3],
	int const values[4],
	unsigned long bits,
	float const position[3],
	float const other_position[3])
{
	unsigned long numbers[3];
	short index;
	byte *at;

	if (!recording.file)
		return;
	/* (the tags first) */
	for (index = 0; index < 3; index++)
		numbers[index] = tags && tags[index] ? recording_tag_named(tags[index]) : RECORDING_NO_TAG;
	at = recording_begin(_record_event, 4 + 1 + 1 + 1 + 6 + 16 + 4 + 24);
	if (!at)
		return;
	at = put_u32(at, (unsigned long)tick);
	at = put_u8(at, (unsigned long)(byte)type);
	at = put_u8(at, player_index >= 0 && player_index < 255 ? (unsigned long)player_index : 255);
	at = put_u8(at, other_index >= 0 && other_index < 255 ? (unsigned long)other_index : 255);
	for (index = 0; index < 3; index++)
		at = put_u16(at, numbers[index]);
	for (index = 0; index < 4; index++)
		at = put_u32(at, values ? (unsigned long)values[index] : 0);
	at = put_u32(at, bits);
	for (index = 0; index < 3; index++)
		at = put_real(at, position ? position[index] : 0.0f);
	for (index = 0; index < 3; index++)
		at = put_real(at, other_position ? other_position[index] : 0.0f);
}

void network_recording_chat(
	byte kind,
	byte scope,
	short speaker_index,
	char const *name,
	int name_length,
	char const *text,
	int length)
{
	char speaker[64];
	char line[256];
	byte *at;

	if (!recording.file || !text || length <= 0)
		return;
	if (length > (int)sizeof(line) - 1)
		length = (int)sizeof(line) - 1;
	csmemcpy(line, text, (size_t)length);
	line[length] = 0;
	if (!name || name_length < 0)
		name_length = 0;
	if (name_length > (int)sizeof(speaker) - 1)
		name_length = (int)sizeof(speaker) - 1;
	if (name_length)
		csmemcpy(speaker, name, (size_t)name_length);
	speaker[name_length] = 0;
	at = recording_begin(_record_chat, 4 + 1 + 1 + 1 + text_size(speaker) + text_size(line));
	if (!at)
		return;
	at = put_u32(at, (unsigned long)game_time_get());
	at = put_u8(at, kind);
	at = put_u8(at, scope);
	at = put_u8(at, speaker_index >= 0 && speaker_index < 255 ? (unsigned long)speaker_index : 255);
	at = put_text(at, speaker);
	put_text(at, line);
}
