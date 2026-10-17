/*
MAP_ARCHIVE_SOURCES.C (test)

The real reading of an index line's archive columns (port/linux/src/map_archives.c,
archive_source): what follows the info hash is tab-separated "<key>:<id>"
columns, and the BLAKE2b-256's column (blake2b:<hash>, tools/map_torrents.py)
is no archive's, so an archive's zip is found after it and nothing is taken
for one from it. test_map_archive_sources.py takes the archives' table and
the code (under_test.inc).
*/

#include "harness.h"

#include <stdio.h>
#include <string.h>

#include "under_test.inc"

#define HASH "blake2b:45e602cb50957867f75f82b94da564dada8e8d102cadee576d08b4949d8f1f34"

static int archive_named(const char *key)
{
	int archive;

	for (archive = 0; archive < ARCHIVE_COUNT; archive++)
	{
		if (!strcmp(archives[archive].key, key))
			return archive;
	}
	return -1;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	char id[512];
	int halonet = archive_named("halonet");
	int halomaps = archive_named("halomaps");

	CHECK(halonet >= 0 && halomaps >= 0, "the archives' table has no halonet or halomaps");
	/* the archives' zips after the hash, each found */
	if (!strcmp(case_name, "after-the-hash"))
	{
		CHECK(archive_source(HASH "\thalonet:alpha pack.zip\thalomaps:1234", halomaps, id, sizeof(id)),
			"HaloMaps's zip after the hash is not found");
		CHECK(!strcmp(id, "1234"), "HaloMaps's id is %s", id);
		CHECK(archive_source(HASH "\thalonet:alpha pack.zip\thalomaps:1234", halonet, id, sizeof(id)),
			"HaloNet's zip after the hash is not found");
		CHECK(!strcmp(id, "alpha pack.zip"), "HaloNet's id is %s", id);
		return 0;
	}
	/* the hash alone names no archive */
	if (!strcmp(case_name, "hash-alone"))
	{
		CHECK(!archive_source(HASH, halonet, id, sizeof(id)), "the hash is taken for HaloNet's zip (%s)", id);
		CHECK(!archive_source(HASH, halomaps, id, sizeof(id)), "the hash is taken for HaloMaps's zip (%s)", id);
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
