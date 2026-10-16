/*
DATA_DOWNLOAD_WANTED.C (test)

The real choice of the maps folder's files a first start downloads
(port/linux/src/sdl_platform.c, data_download_wanted): Multiplayer only
takes all but the campaign's ten maps, Halo Custom Edition's shared maps
included; the campaign's download, later, takes those ten alone; Everything
takes every file. test_data_download_wanted.py takes the enum (wanted.inc)
and the code (under_test.inc).
*/

#include "harness.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

typedef int BOOL;
#define SDL_strncasecmp(a, b, n) strncasecmp((a), (b), (n))
#define SDL_strcasecmp(a, b) strcasecmp((a), (b))

#include "wanted.inc"
#include "under_test.inc"

static char const *const campaign[] = { "a10.map", "A30.MAP", "a50.map", "b30.map", "b40.map", "c10.map", "c20.map",
	"c40.map", "d20.map", "d40.map" };
static char const *const others[] = { "ui.map", "bloodgulch.map", "putput.map", "loading.tga", "a10x.map",
	"a1.map", "d40.map.partial", "c40" };

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	unsigned index;

	/* Multiplayer only: no campaign map, everything else, Custom Edition's too */
	if (!strcmp(case_name, "multiplayer"))
	{
		for (index = 0; index < sizeof(campaign) / sizeof(campaign[0]); index++)
			CHECK(!data_download_wanted(DATA_MAPS_MULTIPLAYER, campaign[index], FALSE), "%s taken", campaign[index]);
		for (index = 0; index + 1 < sizeof(others) / sizeof(others[0]); index++)
		{
			if (!strcmp(others[index], "c40"))
				continue;
			CHECK(data_download_wanted(DATA_MAPS_MULTIPLAYER, others[index], FALSE), "%s left out", others[index]);
		}
		CHECK(data_download_wanted(DATA_MAPS_MULTIPLAYER, "bitmaps.map", TRUE), "Custom Edition's left out");
		return 0;
	}
	/* the campaign's: those ten alone, never Custom Edition's (already here) */
	if (!strcmp(case_name, "campaign"))
	{
		for (index = 0; index < sizeof(campaign) / sizeof(campaign[0]); index++)
			CHECK(data_download_wanted(DATA_MAPS_CAMPAIGN, campaign[index], FALSE), "%s left out", campaign[index]);
		for (index = 0; index < sizeof(others) / sizeof(others[0]); index++)
		{
			if (!strcmp(others[index], "c40"))
				continue;
			CHECK(!data_download_wanted(DATA_MAPS_CAMPAIGN, others[index], FALSE), "%s taken", others[index]);
		}
		CHECK(!data_download_wanted(DATA_MAPS_CAMPAIGN, "bitmaps.map", TRUE), "Custom Edition's taken");
		/* (a level's name alone, as the game asks: platform_offer_missing_map) */
		CHECK(data_is_campaign_map("c40") && !data_is_campaign_map("c4") && !data_is_campaign_map("ui"),
			"a level's bare name");
		return 0;
	}
	/* Everything: every file */
	if (!strcmp(case_name, "everything"))
	{
		for (index = 0; index < sizeof(campaign) / sizeof(campaign[0]); index++)
			CHECK(data_download_wanted(DATA_MAPS_ALL, campaign[index], FALSE), "%s left out", campaign[index]);
		for (index = 0; index < sizeof(others) / sizeof(others[0]); index++)
			CHECK(data_download_wanted(DATA_MAPS_ALL, others[index], FALSE), "%s left out", others[index]);
		CHECK(data_download_wanted(DATA_MAPS_ALL, "loc.map", TRUE), "Custom Edition's left out");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
