/*
RECORDING_PUBLIC.C (test)

The real filter of a recording's public copy (port/linux/game/network_recording.c,
public_records): what anyone may see of a game. A player's hardware id and
address never reach it, nor a hit's fair play verdict or the host's refused
moves; the rest is kept as it is. test_recording_public.py takes the record
types (records.inc) and the code (under_test.inc).
*/

#include "harness.h"

#include <stdio.h>
#include <string.h>

#define csmemcpy(destination, source, size) memcpy((destination), (source), (size))
#define csmemset(destination, value, size) memset((destination), (value), (size))

#include "records.inc"
#include "under_test.inc"

static byte in[1024];
static byte out[1024];
static unsigned long in_length;

/* a record: its type, its size, then the bytes */
static byte *record(byte type, unsigned long size)
{
	byte *at = in + in_length;

	at[0] = type;
	at[1] = (byte)size;
	at[2] = (byte)(size >> 8);
	at[3] = 0;
	at[4] = 0;
	in_length += 5 + size;
	return at + 5;
}

static byte *text(byte *at, char const *value)
{
	size_t length = strlen(value);

	*at++ = (byte)length;
	memcpy(at, value, length);
	return at + length;
}

/* a player record: tick, player 3, team 1, then the three texts */
static void player(char const *name, char const *hardware_id, char const *address)
{
	byte *at = record(_record_player, 4 + 1 + 1 + 1 + strlen(name) + 1 + strlen(hardware_id) + 1 + strlen(address));

	memset(at, 0, 4);
	at[4] = 3;
	at[5] = 1;
	at = text(at + 6, name);
	at = text(at, hardware_id);
	text(at, address);
}

/* whether the bytes are anywhere in the output */
static boolean found(byte const *bytes, unsigned long size, unsigned long length)
{
	unsigned long index;

	for (index = 0; index + size <= length; index++)
	{
		if (!memcmp(out + index, bytes, size))
			return TRUE;
	}
	return FALSE;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	boolean live = TRUE;
	unsigned long length;

	/* a player's name kept, their hardware id and address gone */
	if (!strcmp(case_name, "no-private-data"))
	{
		player("Chief", "0123456789abcdef0123456789abcdef", "203.0.113.7");
		length = public_records(in, in_length, out, &live);
		CHECK(!found((byte const *)"0123456789abcdef", 16, length), "the hardware id is in the public copy");
		CHECK(!found((byte const *)"203.0.113.7", 11, length), "the address is in the public copy");
		CHECK(found((byte const *)"\5Chief", 6, length), "the player's name is not kept");
		/* (type, size 14: tick 4, player, team, name 6, two empty texts) */
		CHECK(length == 5 + 4 + 1 + 1 + 6 + 2, "the player record is %lu bytes", length);
		CHECK(out[1] == 14 && out[5 + 5] == 1 && out[5 + 4] == 3, "the record's size, player or team is wrong");
		CHECK(out[length - 1] == 0 && out[length - 2] == 0, "the two texts after the name are not empty");
		CHECK(!live, "a player joining is not the game being played");
		return 0;
	}
	/* a hit's verdict, degrees and distance zeroed; where it hit kept */
	if (!strcmp(case_name, "hit-without-verdict"))
	{
		byte *at = record(_record_hit, 27);

		memset(at, 0x11, 18);
		memset(at + 18, 0x7f, 9);
		length = public_records(in, in_length, out, &live);
		CHECK(length == 5 + 27, "the hit record is %lu bytes", length);
		CHECK(out[5 + 17] == 0x11, "where the hit was is not kept");
		for (in_length = 18; in_length < 27; in_length++)
			CHECK(out[5 + in_length] == 0, "byte %lu of the hit (its verdict, aim or distance) is kept", in_length);
		CHECK(live, "a hit is the game being played");
		return 0;
	}
	/* the host's refused moves left out, the records around them kept */
	if (!strcmp(case_name, "no-moves"))
	{
		byte *at = record(_record_kill, 7);

		memset(at, 0x22, 7);
		at = record(_record_move, 10);
		memset(at, 0x33, 10);
		at = record(_record_end, 4);
		memset(at, 0x44, 4);
		length = public_records(in, in_length, out, &live);
		CHECK(length == 5 + 7 + 5 + 4, "%lu bytes, not the kill and the end", length);
		CHECK(out[0] == _record_kill && out[12] == _record_end, "the records kept are not the kill and the end");
		CHECK(!found((byte const *)"\x33\x33\x33\x33", 4, length), "the refused move is in the public copy");
		return 0;
	}
	/* a record that runs past the block ends the reading, nothing after it */
	if (!strcmp(case_name, "truncated"))
	{
		byte *at = record(_record_frame, 8);

		memset(at, 0x55, 8);
		at = record(_record_tag, 200);
		in_length -= 150;
		length = public_records(in, in_length, out, &live);
		CHECK(length == 5 + 8, "%lu bytes from a block whose last record is cut short", length);
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
