/*
CE_HUD_SCALE.C (test)

The real Custom Edition HUD conversion of port/linux/game/ce_hud.c: which
placements are drawn at half their scale (Halo PC's high resolution scale
flag, and a bitmap's half HUD scale or force HUD high resolution scale
flags), that numbers keep their flag for hud_draw_numbers, the HUD digits
halved and the bitmaps' flags cleared on the maps that rely on it, a model
shader's detail after reflection flipped on those that rely on that, and
what a map relies on found by its name and tag data checksum
(custom_edition_behaviours.inc). test_ce_hud_scale.py takes the constants,
the tag instance's structure and the behaviours (config.inc) and the code
(under_test.inc).
*/

#include "harness.h"

#pragma clang diagnostic ignored "-Wfour-char-constants"
#pragma GCC diagnostic ignored "-Wmultichar"

#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))
#define _error_silent 0
#define error(priority, ...) ((void)(priority))

#include "config.inc"

/* the tags' handles: a salt and their index */
#define TAG_HANDLE_SALT 0xe1740000UL

/* the tag cache: FAKE_TAGS_SIZE bytes from CE_IMAGE_TAG_CACHE_BASE */
enum
{
	FAKE_TAGS_SIZE = 0x10000,

	INSTANCES = 0x100,
	BITMAP_HALF = 0x1000,
	BITMAP_PLAIN = 0x1100,
	BITMAP_DIGITS = 0x1200,
	BITMAP_FORCE = 0x1300,
	WEAPON_HUD = 0x2000,
	GRENADE_HUD = 0x3000,
	GLOBALS = 0x4000,
	DIGITS = 0x5000,
	MODEL_SHADER = 0x6000,
};

enum
{
	_tag_bitmap_half,
	_tag_bitmap_plain,
	_tag_bitmap_digits,
	_tag_bitmap_force,
	_tag_weapon_hud,
	_tag_grenade_hud,
	_tag_globals,
	_tag_digits,
	_tag_model_shader,
	NUMBER_OF_FAKE_TAGS
};

static byte fake_tags[FAKE_TAGS_SIZE];

#undef xbox_pointer
#define xbox_pointer(address) ((void *)(fake_tags + ((unsigned long)(address) - CE_IMAGE_TAG_CACHE_BASE)))

#include "under_test.inc"

static byte *at(
	unsigned long offset)
{
	return fake_tags + offset;
}

static unsigned long address_of(
	unsigned long offset)
{
	return CE_IMAGE_TAG_CACHE_BASE + offset;
}

static void put_long(
	unsigned long offset,
	unsigned long value)
{
	memcpy(at(offset), &value, sizeof(value));
}

static void put_block(
	unsigned long offset,
	long count,
	unsigned long elements)
{
	put_long(offset + TAG_BLOCK_COUNT_OFFSET, (unsigned long)count);
	put_long(offset + TAG_BLOCK_ADDRESS_OFFSET, count ? address_of(elements) : 0);
}

static void put_reference(
	unsigned long offset,
	long tag)
{
	put_long(offset + TAG_REFERENCE_INDEX_OFFSET, tag == NONE ? (unsigned long)NONE : TAG_HANDLE_SALT | (unsigned long)tag);
}

static void put_placement(
	unsigned long offset,
	boolean high_resolution_scale)
{
	float one = 1.0f;

	memcpy(at(offset + PLACEMENT_SCALE_X_OFFSET), &one, sizeof(one));
	memcpy(at(offset + PLACEMENT_SCALE_Y_OFFSET), &one, sizeof(one));
	*(unsigned short *)at(offset + PLACEMENT_SCALING_FLAGS_OFFSET) =
		high_resolution_scale ? PLACEMENT_HIGH_RESOLUTION_SCALE_FLAG : 0;
}

static float scale_of(
	unsigned long offset)
{
	float scale;

	memcpy(&scale, at(offset + PLACEMENT_SCALE_X_OFFSET), sizeof(scale));
	return scale;
}

static boolean flagged(
	unsigned long offset)
{
	return (*(unsigned short *)at(offset + PLACEMENT_SCALING_FLAGS_OFFSET) & PLACEMENT_HIGH_RESOLUTION_SCALE_FLAG) != 0;
}

static unsigned short bitmap_flags(
	unsigned long offset)
{
	return *(unsigned short *)at(offset + BITMAP_GROUP_FLAGS_OFFSET);
}

static void put_instance(
	long tag,
	unsigned long group_tag,
	unsigned long offset)
{
	struct ce_tag_instance *instance = (struct ce_tag_instance *)at(INSTANCES + tag * CE_TAG_INSTANCE_SIZE);

	instance->group_tag = group_tag;
	instance->tag_index = TAG_HANDLE_SALT | (unsigned long)tag;
	instance->base_address = address_of(offset);
}

/* where the weapon HUD's elements are */
enum
{
	STATICS = WEAPON_HUD + 0x200,
	NUMBERS = WEAPON_HUD + 0x400,
	CROSSHAIRS = WEAPON_HUD + 0x500,
	CROSSHAIR_ITEMS = WEAPON_HUD + 0x600,
	OVERLAYS = WEAPON_HUD + 0x800,
	OVERLAY_ITEMS = WEAPON_HUD + 0x900,
	GRENADE_ITEMS = GRENADE_HUD + 0x400,
	INTERFACE_BITMAPS = GLOBALS + 0x200,
};

static void world(
	void)
{
	long element;

	memset(fake_tags, 0, sizeof(fake_tags));
	put_instance(_tag_bitmap_half, 'bitm', BITMAP_HALF);
	put_instance(_tag_bitmap_plain, 'bitm', BITMAP_PLAIN);
	put_instance(_tag_bitmap_digits, 'bitm', BITMAP_DIGITS);
	put_instance(_tag_bitmap_force, 'bitm', BITMAP_FORCE);
	put_instance(_tag_weapon_hud, 'wphi', WEAPON_HUD);
	put_instance(_tag_grenade_hud, 'grhi', GRENADE_HUD);
	put_instance(_tag_globals, 'matg', GLOBALS);
	put_instance(_tag_digits, 'hud#', DIGITS);
	put_instance(_tag_model_shader, 'soso', MODEL_SHADER);
	/* (a model shader two-sided, its detail after reflection not set) */
	*(unsigned short *)at(MODEL_SHADER + MODEL_SHADER_FLAGS_OFFSET) = 1 << 1;
	*(unsigned short *)at(BITMAP_HALF + BITMAP_GROUP_FLAGS_OFFSET) = BITMAP_GROUP_HALF_HUD_SCALE_FLAG | 1;
	*(unsigned short *)at(BITMAP_PLAIN + BITMAP_GROUP_FLAGS_OFFSET) = 1;
	*(unsigned short *)at(BITMAP_FORCE + BITMAP_GROUP_FLAGS_OFFSET) = BITMAP_GROUP_FORCE_HUD_HIGH_RESOLUTION_SCALE_FLAG;

	/* the weapon HUD: three statics (a bitmap that halves, one flagged,
	one of neither), a flagged number, a crosshair whose bitmap halves its
	items and an overlay whose bitmap does not (one item flagged) */
	put_block(WEAPON_HUD + WEAPON_HUD_STATICS_OFFSET, 3, STATICS);
	for (element = 0; element < 3; element++)
	{
		unsigned long placement = STATICS + element * WEAPON_HUD_STATIC_SIZE + WEAPON_HUD_ELEMENT_OFFSET;

		put_placement(placement, element == 1);
		put_reference(placement + ELEMENT_BITMAP_OFFSET, element == 0 ? _tag_bitmap_half :
			element == 1 ? _tag_bitmap_plain : NONE);
	}
	put_block(WEAPON_HUD + WEAPON_HUD_METERS_OFFSET, 0, 0);
	put_block(WEAPON_HUD + WEAPON_HUD_NUMBERS_OFFSET, 1, NUMBERS);
	put_placement(NUMBERS + WEAPON_HUD_ELEMENT_OFFSET, TRUE);
	put_block(WEAPON_HUD + WEAPON_HUD_CROSSHAIRS_OFFSET, 1, CROSSHAIRS);
	put_reference(CROSSHAIRS + WEAPON_HUD_ITEMS_BITMAP_OFFSET, _tag_bitmap_force);
	put_block(CROSSHAIRS + WEAPON_HUD_ITEMS_OFFSET, 2, CROSSHAIR_ITEMS);
	put_placement(CROSSHAIR_ITEMS, FALSE);
	put_placement(CROSSHAIR_ITEMS + WEAPON_HUD_CROSSHAIR_ITEM_SIZE, FALSE);
	put_block(WEAPON_HUD + WEAPON_HUD_OVERLAYS_OFFSET, 1, OVERLAYS);
	put_reference(OVERLAYS + WEAPON_HUD_ITEMS_BITMAP_OFFSET, _tag_bitmap_plain);
	put_block(OVERLAYS + WEAPON_HUD_ITEMS_OFFSET, 2, OVERLAY_ITEMS);
	put_placement(OVERLAY_ITEMS, TRUE);
	put_placement(OVERLAY_ITEMS + WEAPON_HUD_OVERLAY_ITEM_SIZE, FALSE);

	/* the grenade HUD: a background of neither, a count background whose
	bitmap halves, flagged count numbers, overlay items halved by their
	bitmap */
	put_placement(GRENADE_HUD + GRENADE_HUD_BACKGROUND_OFFSET, FALSE);
	put_reference(GRENADE_HUD + GRENADE_HUD_BACKGROUND_OFFSET + ELEMENT_BITMAP_OFFSET, _tag_bitmap_plain);
	put_placement(GRENADE_HUD + GRENADE_HUD_COUNT_BACKGROUND_OFFSET, FALSE);
	put_reference(GRENADE_HUD + GRENADE_HUD_COUNT_BACKGROUND_OFFSET + ELEMENT_BITMAP_OFFSET, _tag_bitmap_half);
	put_placement(GRENADE_HUD + GRENADE_HUD_COUNT_NUMBERS_OFFSET, TRUE);
	put_reference(GRENADE_HUD + GRENADE_HUD_OVERLAY_BITMAP_OFFSET, _tag_bitmap_half);
	put_block(GRENADE_HUD + GRENADE_HUD_OVERLAY_ITEMS_OFFSET, 1, GRENADE_ITEMS);
	put_placement(GRENADE_ITEMS, FALSE);

	/* the HUD digits: 9, 8, -3, 0, 5 and 0 wide, their bitmap the digits' */
	put_block(GLOBALS + GLOBALS_INTERFACE_BITMAPS_OFFSET, 1, INTERFACE_BITMAPS);
	put_reference(INTERFACE_BITMAPS + INTERFACE_BITMAPS_HUD_DIGITS_OFFSET, _tag_digits);
	put_reference(DIGITS, _tag_bitmap_digits);
	at(DIGITS + HUD_NUMBER_METRICS_OFFSET)[0] = 9;
	at(DIGITS + HUD_NUMBER_METRICS_OFFSET)[1] = 8;
	at(DIGITS + HUD_NUMBER_METRICS_OFFSET)[2] = (byte)-3;
	at(DIGITS + HUD_NUMBER_METRICS_OFFSET)[4] = 5;
}

/* the map's HUD converted, as a map of this name and checksum */
static void load(
	char const *name,
	unsigned long checksum)
{
	world();
	ce_hud_tags_unloaded();
	ce_hud_map_identity(name, (long)strlen(name), checksum);
	ce_hud_tags_loaded(at(INSTANCES), NUMBER_OF_FAKE_TAGS);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	unsigned long statics = STATICS + WEAPON_HUD_ELEMENT_OFFSET;

	/* an element flagged, or drawing a bitmap that asks it, at half its
	scale, the flag cleared; one of neither as it was */
	CASE("elements")
	{
		load("some map", 0x12345678);
		CHECK(scale_of(statics) == 0.5f, "a static whose bitmap has the half HUD scale: %g", scale_of(statics));
		CHECK(scale_of(statics + WEAPON_HUD_STATIC_SIZE) == 0.5f && !flagged(statics + WEAPON_HUD_STATIC_SIZE),
			"a flagged static: %g", scale_of(statics + WEAPON_HUD_STATIC_SIZE));
		CHECK(scale_of(statics + 2 * WEAPON_HUD_STATIC_SIZE) == 1.0f, "a static of neither: %g",
			scale_of(statics + 2 * WEAPON_HUD_STATIC_SIZE));
		CHECK(scale_of(GRENADE_HUD + GRENADE_HUD_BACKGROUND_OFFSET) == 1.0f &&
			scale_of(GRENADE_HUD + GRENADE_HUD_COUNT_BACKGROUND_OFFSET) == 0.5f, "the grenade backgrounds: %g, %g",
			scale_of(GRENADE_HUD + GRENADE_HUD_BACKGROUND_OFFSET),
			scale_of(GRENADE_HUD + GRENADE_HUD_COUNT_BACKGROUND_OFFSET));
		return 0;
	}
	/* a number keeps its flag and scale: hud_draw_numbers halves its digits */
	CASE("numbers-keep-flag")
	{
		load("some map", 0x12345678);
		CHECK(flagged(NUMBERS + WEAPON_HUD_ELEMENT_OFFSET) && scale_of(NUMBERS + WEAPON_HUD_ELEMENT_OFFSET) == 1.0f,
			"a weapon's number: %g", scale_of(NUMBERS + WEAPON_HUD_ELEMENT_OFFSET));
		CHECK(flagged(GRENADE_HUD + GRENADE_HUD_COUNT_NUMBERS_OFFSET) &&
			scale_of(GRENADE_HUD + GRENADE_HUD_COUNT_NUMBERS_OFFSET) == 1.0f, "the grenade count: %g",
			scale_of(GRENADE_HUD + GRENADE_HUD_COUNT_NUMBERS_OFFSET));
		return 0;
	}
	/* crosshair and overlay items: halved by their own flag, or by the
	bitmap of their crosshair or overlay (force HUD high resolution scale
	too) */
	CASE("items")
	{
		load("some map", 0x12345678);
		CHECK(scale_of(CROSSHAIR_ITEMS) == 0.5f && scale_of(CROSSHAIR_ITEMS + WEAPON_HUD_CROSSHAIR_ITEM_SIZE) == 0.5f,
			"the crosshair's items: %g, %g", scale_of(CROSSHAIR_ITEMS),
			scale_of(CROSSHAIR_ITEMS + WEAPON_HUD_CROSSHAIR_ITEM_SIZE));
		CHECK(scale_of(OVERLAY_ITEMS) == 0.5f && scale_of(OVERLAY_ITEMS + WEAPON_HUD_OVERLAY_ITEM_SIZE) == 1.0f,
			"the overlay's items: %g, %g", scale_of(OVERLAY_ITEMS), scale_of(OVERLAY_ITEMS + WEAPON_HUD_OVERLAY_ITEM_SIZE));
		CHECK(scale_of(GRENADE_ITEMS) == 0.5f, "the grenade overlay's item: %g", scale_of(GRENADE_ITEMS));
		return 0;
	}
	/* a map that is not listed: its digits as they are */
	CASE("digits-kept")
	{
		load("bigass_v3", 0x12345678);
		CHECK(at(DIGITS + HUD_NUMBER_METRICS_OFFSET)[0] == 9 && !(bitmap_flags(BITMAP_DIGITS) &
			BITMAP_GROUP_HALF_HUD_SCALE_FLAG), "metrics %d, flags %04x", at(DIGITS + HUD_NUMBER_METRICS_OFFSET)[0],
			bitmap_flags(BITMAP_DIGITS));
		return 0;
	}
	/* hud_number_scale (bigass_v3, by name in any case and checksum): the
	digits' metrics halved, rounding up, and their bitmap's half HUD scale */
	CASE("digits-halved")
	{
		signed char const expected[6] = { 5, 4, -1, 0, 3, 0 };
		long metric;

		load("BigAss_V3", 0x852EE757);
		for (metric = 0; metric < 6; metric++)
		{
			CHECK((signed char)at(DIGITS + HUD_NUMBER_METRICS_OFFSET)[metric] == expected[metric], "metric %ld: %d",
				metric, (signed char)at(DIGITS + HUD_NUMBER_METRICS_OFFSET)[metric]);
		}
		CHECK(bitmap_flags(BITMAP_DIGITS) & BITMAP_GROUP_HALF_HUD_SCALE_FLAG, "flags %04x", bitmap_flags(BITMAP_DIGITS));
		return 0;
	}
	/* disable_bitmap_hud_scale_flags (frogfoot v1.1): the bitmaps' flags
	cleared first, so nothing is halved by them, the other flags kept */
	CASE("bitmap-flags-cleared")
	{
		load("frogfoot v1.1", 0xFC356BC2);
		CHECK(bitmap_flags(BITMAP_HALF) == 1 && bitmap_flags(BITMAP_FORCE) == 0, "flags %04x, %04x",
			bitmap_flags(BITMAP_HALF), bitmap_flags(BITMAP_FORCE));
		CHECK(scale_of(statics) == 1.0f && scale_of(CROSSHAIR_ITEMS) == 1.0f, "halved anyway: %g, %g",
			scale_of(statics), scale_of(CROSSHAIR_ITEMS));
		CHECK(scale_of(statics + WEAPON_HUD_STATIC_SIZE) == 0.5f, "a flagged static: %g",
			scale_of(statics + WEAPON_HUD_STATIC_SIZE));
		return 0;
	}
	/* invert_detail_after_reflection (cmt_cliffrun, by name and checksum):
	the model shaders' detail after reflection flipped, their other flags
	kept; another map's, or another checksum's, as they were */
	CASE("detail-after-reflection")
	{
		unsigned short flags;

		load("cmt_cliffrun", 0x4D01139C);
		flags = *(unsigned short *)at(MODEL_SHADER + MODEL_SHADER_FLAGS_OFFSET);
		CHECK(flags == ((1 << 1) | MODEL_SHADER_DETAIL_AFTER_REFLECTION_FLAG), "cmt_cliffrun: %04x", flags);
		load("cmt_cliffrun", 0x4D01139D);
		flags = *(unsigned short *)at(MODEL_SHADER + MODEL_SHADER_FLAGS_OFFSET);
		CHECK(flags == 1 << 1, "cmt_cliffrun of another checksum: %04x", flags);
		load("bigass_v3", 0x852EE757);
		flags = *(unsigned short *)at(MODEL_SHADER + MODEL_SHADER_FLAGS_OFFSET);
		CHECK(flags == 1 << 1, "bigass_v3: %04x", flags);
		return 0;
	}
	/* what hud_draw.c asks: by name and checksum, for the map loaded only */
	CASE("behaviours")
	{
		load("bigass_v3", 0x852EE757);
		CHECK(ce_hud_halo_pc_overlay_blend_functions() && !ce_hud_overlays_blocked(), "bigass_v3");
		load("zanzibar", 0xCD2FC615);
		CHECK(!ce_hud_halo_pc_overlay_blend_functions() && ce_hud_overlays_blocked(), "zanzibar");
		load("zanzibar", 0xCD2FC616);
		CHECK(!ce_hud_overlays_blocked(), "zanzibar of another checksum");
		load("zanzibar", 0xCD2FC615);
		ce_hud_tags_unloaded();
		CHECK(!ce_hud_overlays_blocked(), "after the map is unloaded");
		return 0;
	}

	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
