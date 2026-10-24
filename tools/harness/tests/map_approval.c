/*
MAP_APPROVAL.C (test)

Port (this fork): the approved map list, the maps' torrent index read as
port/linux/src/map_torrents.c reads it (map_torrents_approved_hash,
map_torrents_approved, map_torrents_unapproved_text): a host's offer is
approved by its map sharing hash (the share:<hash> column), a resource map by
its name, a map the repositories would give by its name and the host's
version (the header checksum, the map sharing hash's first 32 bits or the
whole-file BLAKE2b's). test_map_approval.py takes the code (under_test.inc);
this gives it the index it reads.
*/

#include "harness.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char logged[1024];

static void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(logged, sizeof(logged), format, arguments);
	va_end(arguments);
}

#include "under_test.inc"

static void index_load(void)
{
}

#define SHARE "share:af352483f44dace2e74561daee6b43b612d38a8dbd6f9a612fb437fdf59410af"
#define WHOLE "blake2b:7b4ae7d1b6327ca3000000000000000000000000000000000000000000000000"

static char names[4096];

static void add(const char *name, unsigned long checksum, const char *sources)
{
	struct index_entry *entry = &torrent_maps_index.entries[torrent_maps_index.count++];
	unsigned char whole[32];

	memset(entry, 0, sizeof(*entry));
	entry->name = torrent_maps_index.names_size;
	memcpy(names + torrent_maps_index.names_size, name, strlen(name) + 1);
	torrent_maps_index.names_size += (unsigned long)strlen(name) + 1;
	entry->checksum = checksum;
	entry->sources = torrent_maps_index.names_size;
	memcpy(names + torrent_maps_index.names_size, sources, strlen(sources) + 1);
	torrent_maps_index.names_size += (unsigned long)strlen(sources) + 1;
	entry->has_share = column_hash(names + entry->sources, "share:", entry->share);
	entry->has_blake2b = column_hash(names + entry->sources, "blake2b:", whole);
	entry->blake2b_prefix = entry->has_blake2b ? (unsigned int)whole[0] << 24 | (unsigned int)whole[1] << 16 |
		(unsigned int)whole[2] << 8 | (unsigned int)whole[3] : 0;
}

static void hex_bytes(const char *hex, unsigned char *bytes)
{
	int index;

	for (index = 0; index < 32; index++)
	{
		char pair[3] = { hex[index * 2], hex[index * 2 + 1], 0 };

		bytes[index] = (unsigned char)strtoul(pair, NULL, 16);
	}
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	static struct index_entry entries[8];
	unsigned char hash[32];
	unsigned char other[32];
	char text[400];

	torrent_maps_index.entries = entries;
	torrent_maps_index.names = names;
	add("Sneak.map", 0x12345678, WHOLE "\t" SHARE "\thalomaps:1");
	add("bitmaps.map", 0, "halomaps:2");
	add("old.map", 0xCAFEF00D, "halonet:old.zip");
	hex_bytes(&SHARE[6], hash);
	memcpy(other, hash, sizeof(other));
	other[31] ^= 1;
	if (!strcmp(case_name, "offer-hash"))
	{
		CHECK(map_torrents_approved_hash(NULL, hash), "an indexed map's sharing hash is not approved");
		CHECK(!map_torrents_approved_hash(NULL, other), "a hash one bit off an indexed one is approved");
		CHECK(!map_torrents_approved_hash("old.map", other), "a map whose hash is not indexed is approved by name");
		CHECK(map_torrents_approved_hash("custom_maps\\bitmaps", other), "a resource map is not approved by name");
		return 0;
	}
	if (!strcmp(case_name, "version"))
	{
		CHECK(map_torrents_approved("Sneak", 0x12345678), "a map is not approved by its header checksum");
		CHECK(map_torrents_approved("sneak.map", 0xAF352483), "a map is not approved by its sharing hash's prefix");
		CHECK(map_torrents_approved("SNEAK", 0x7B4AE7D1), "a map is not approved by its BLAKE2b's prefix");
		CHECK(map_torrents_approved("Sneak", 0), "a map of any version is not approved by its name");
		CHECK(!map_torrents_approved("Sneak", 0x11111111), "another version of a map is approved");
		CHECK(!map_torrents_approved("Snake", 0x12345678), "another map of an indexed checksum is approved");
		CHECK(map_torrents_approved("old", 0xCAFEF00D), "a map without hash columns is not approved by its checksum");
		CHECK(!map_torrents_approved("old", 0xAF352483), "a map without hash columns is approved by another's prefix");
		return 0;
	}
	if (!strcmp(case_name, "text"))
	{
		map_torrents_unapproved_text(text, (int)sizeof(text), "custom_maps\\secret", other, 0);
		CHECK(strstr(text, "secret.map") != NULL, "the text names no file: %s", text);
		CHECK(strstr(text, "af352483") != NULL, "the text has not the hash's first digits: %s", text);
		CHECK(strstr(logged, "af352483f44dace2e74561daee6b43b612d38a8dbd6f9a612fb437fdf59410ae") != NULL,
			"the log has not the whole hash: %s", logged);
		CHECK(strlen(text) <= 170, "the text is longer than the error dialog's five lines: %s", text);
		CHECK(strstr(text, MAP_APPROVAL_CONTACT) != NULL, "the text says whom to ask nowhere: %s", text);
		CHECK(strstr(text, "malware") != NULL, "the text says not why: %s", text);
		CHECK(strstr(logged, "secret.map") != NULL, "the log has not the file: %s", logged);
		map_torrents_unapproved_text(text, (int)sizeof(text), "secret.map", NULL, 0xAF352483);
		CHECK(strstr(text, "af352483") != NULL && strstr(text, "secret.map.map") == NULL,
			"the text has not the host's version, or the name twice: %s", text);
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
