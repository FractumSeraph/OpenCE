/*
DATA_DOWNLOAD_WANTED.C (test)

The real choice of the maps folder's files a first start downloads
(port/linux/src/sdl_platform.c, data_download_wanted): Multiplayer only
takes all but the campaign's ten maps, Halo Custom Edition's shared maps
included and Halo MCC's shared sets left out; the campaign's download, later,
takes those ten alone; MCC's shared files, when an MCC map needs them, take
MCC's two sets alone; Everything takes every file. test_data_download_wanted.py
takes the enums (wanted.inc) and the code (under_test.inc).
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
			CHECK(!data_download_wanted(DATA_MAPS_MULTIPLAYER, campaign[index], DATA_FOLDER_MAPS), "%s taken",
				campaign[index]);
		for (index = 0; index + 1 < sizeof(others) / sizeof(others[0]); index++)
		{
			if (!strcmp(others[index], "c40"))
				continue;
			CHECK(data_download_wanted(DATA_MAPS_MULTIPLAYER, others[index], DATA_FOLDER_MAPS), "%s left out",
				others[index]);
		}
		CHECK(data_download_wanted(DATA_MAPS_MULTIPLAYER, "bitmaps.map", DATA_FOLDER_CUSTOM_EDITION),
			"Custom Edition's left out");
		CHECK(!data_download_wanted(DATA_MAPS_MULTIPLAYER, "bitmaps.map", DATA_FOLDER_MCC_MULTIPLAYER) &&
			!data_download_wanted(DATA_MAPS_MULTIPLAYER, "bitmaps.map", DATA_FOLDER_MCC_SINGLEPLAYER), "MCC's taken");
		return 0;
	}
	/* the campaign's: those ten alone, never Custom Edition's (already here) */
	if (!strcmp(case_name, "campaign"))
	{
		for (index = 0; index < sizeof(campaign) / sizeof(campaign[0]); index++)
			CHECK(data_download_wanted(DATA_MAPS_CAMPAIGN, campaign[index], DATA_FOLDER_MAPS), "%s left out", campaign[index]);
		for (index = 0; index < sizeof(others) / sizeof(others[0]); index++)
		{
			if (!strcmp(others[index], "c40"))
				continue;
			CHECK(!data_download_wanted(DATA_MAPS_CAMPAIGN, others[index], DATA_FOLDER_MAPS), "%s taken", others[index]);
		}
		CHECK(!data_download_wanted(DATA_MAPS_CAMPAIGN, "bitmaps.map", DATA_FOLDER_CUSTOM_EDITION),
			"Custom Edition's taken");
		/* (an MCC set's file named as a campaign level is still MCC's) */
		CHECK(!data_download_wanted(DATA_MAPS_CAMPAIGN, "a10.map", DATA_FOLDER_MCC_SINGLEPLAYER), "MCC's taken");
		/* (a level's name alone, as the game asks: platform_offer_missing_map) */
		CHECK(data_is_campaign_map("c40") && !data_is_campaign_map("c4") && !data_is_campaign_map("ui"),
			"a level's bare name");
		return 0;
	}
	/* Everything: every file */
	if (!strcmp(case_name, "everything"))
	{
		for (index = 0; index < sizeof(campaign) / sizeof(campaign[0]); index++)
			CHECK(data_download_wanted(DATA_MAPS_ALL, campaign[index], DATA_FOLDER_MAPS), "%s left out", campaign[index]);
		for (index = 0; index < sizeof(others) / sizeof(others[0]); index++)
			CHECK(data_download_wanted(DATA_MAPS_ALL, others[index], DATA_FOLDER_MAPS), "%s left out", others[index]);
		CHECK(data_download_wanted(DATA_MAPS_ALL, "loc.map", DATA_FOLDER_CUSTOM_EDITION), "Custom Edition's left out");
		CHECK(data_download_wanted(DATA_MAPS_ALL, "bitmaps.map", DATA_FOLDER_MCC_SINGLEPLAYER) &&
			data_download_wanted(DATA_MAPS_ALL, "sounds.map", DATA_FOLDER_MCC_MULTIPLAYER), "MCC's left out");
		return 0;
	}
	/* MCC's shared files: its two sets alone */
	if (!strcmp(case_name, "mcc"))
	{
		CHECK(data_download_wanted(DATA_MAPS_MCC_SHARED, "bitmaps.map", DATA_FOLDER_MCC_SINGLEPLAYER) &&
			data_download_wanted(DATA_MAPS_MCC_SHARED, "bitmaps.map", DATA_FOLDER_MCC_MULTIPLAYER) &&
			data_download_wanted(DATA_MAPS_MCC_SHARED, "sounds.map", DATA_FOLDER_MCC_MULTIPLAYER), "MCC's left out");
		for (index = 0; index < sizeof(campaign) / sizeof(campaign[0]); index++)
			CHECK(!data_download_wanted(DATA_MAPS_MCC_SHARED, campaign[index], DATA_FOLDER_MAPS), "%s taken",
				campaign[index]);
		for (index = 0; index < sizeof(others) / sizeof(others[0]); index++)
			CHECK(!data_download_wanted(DATA_MAPS_MCC_SHARED, others[index], DATA_FOLDER_MAPS), "%s taken",
				others[index]);
		CHECK(!data_download_wanted(DATA_MAPS_MCC_SHARED, "bitmaps.map", DATA_FOLDER_CUSTOM_EDITION),
			"Custom Edition's taken");
		/* (only the sets' own names: sounds.map is the multiplayer set's alone) */
		CHECK(data_is_mcc_shared(DATA_FOLDER_MCC_MULTIPLAYER, "SOUNDS.MAP") &&
			!data_is_mcc_shared(DATA_FOLDER_MCC_SINGLEPLAYER, "sounds.map") &&
			!data_is_mcc_shared(DATA_FOLDER_MCC_MULTIPLAYER, "loc.map") &&
			!data_is_mcc_shared(DATA_FOLDER_MCC_SINGLEPLAYER, "a10.map"), "a set's names");
		return 0;
	}
	/* each start's shared files: every shared file, Halo PC's too, and no
	map of the maps folder; Halo PC's in no download but this and Everything */
	if (!strcmp(case_name, "shared"))
	{
		for (index = 0; index < sizeof(campaign) / sizeof(campaign[0]); index++)
			CHECK(!data_download_wanted(DATA_MAPS_SHARED_FILES, campaign[index], DATA_FOLDER_MAPS), "%s taken",
				campaign[index]);
		for (index = 0; index < sizeof(others) / sizeof(others[0]); index++)
			CHECK(!data_download_wanted(DATA_MAPS_SHARED_FILES, others[index], DATA_FOLDER_MAPS), "%s taken",
				others[index]);
		CHECK(data_download_wanted(DATA_MAPS_SHARED_FILES, "loc.map", DATA_FOLDER_CUSTOM_EDITION) &&
			data_download_wanted(DATA_MAPS_SHARED_FILES, "bitmaps.map", DATA_FOLDER_MCC_SINGLEPLAYER) &&
			data_download_wanted(DATA_MAPS_SHARED_FILES, "sounds.map", DATA_FOLDER_MCC_MULTIPLAYER) &&
			data_download_wanted(DATA_MAPS_SHARED_FILES, "sounds.map", DATA_FOLDER_HALO_PC), "a shared file left out");
		CHECK(!data_download_wanted(DATA_MAPS_MULTIPLAYER, "bitmaps.map", DATA_FOLDER_HALO_PC) &&
			!data_download_wanted(DATA_MAPS_CAMPAIGN, "bitmaps.map", DATA_FOLDER_HALO_PC) &&
			!data_download_wanted(DATA_MAPS_MCC_SHARED, "bitmaps.map", DATA_FOLDER_HALO_PC), "Halo PC's taken");
		CHECK(data_download_wanted(DATA_MAPS_ALL, "bitmaps.map", DATA_FOLDER_HALO_PC), "Halo PC's left out of Everything");
		CHECK(data_is_halo_pc_shared("SOUNDS.MAP") && data_is_halo_pc_shared("bitmaps.map") &&
			!data_is_halo_pc_shared("loc.map") && !data_is_halo_pc_shared("ui.map"), "Halo PC's names");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
