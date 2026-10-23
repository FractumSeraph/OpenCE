"""The real Custom Edition HUD conversion of ce_hud.c: the placements drawn at half their scale (Halo PC's high
resolution scale flag, a bitmap's half HUD scale and force HUD high resolution scale flags, ChupathingyCE's: a
placement given the bitmap it draws), numbers halved by their own flag (their digits by their bitmap's flags:
hud_bitmap_group_scale), and what the maps Chimera lists rely on (OpenCE's custom_edition_behaviours.inc): the digits
halved, the bitmaps' flags cleared, the overlays' blend functions and overlays not drawn, the model shaders' detail
after reflection flipped. And OpenCE's 384d9ba5 on it: a meter bitmap whose group a static element draws too is
sampled as a meter's by its meters' draws alone (mixed), Bigass Open v1.1's sheet of the Xbox's meter channels is
no Halo PC meter, Gearbox's meters asked for by hud_draw.c."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["elements", "numbers-halved", "items", "digits-kept", "digits-halved", "bitmap-flags-cleared",
         "detail-after-reflection", "behaviours", "mixed-meters", "xbox-ammo-meters"]
FUNCTIONS = ("ce_range_within", "ce_hud_read_long", "ce_hud_tag_cache_pointer", "ce_hud_reference_tag",
             "ce_hud_bitmap_group", "ce_hud_placement", "ce_hud_element_placement", "ce_hud_block_placements",
             "ce_hud_items_placements", "ce_hud_bitmap_scale_flags_clear", "ce_hud_digits_halve",
             "ce_hud_meter_bitmap", "ce_hud_block_meter_bitmaps", "ce_hud_static_bitmap", "ce_hud_block_static_bitmaps",
             "ce_hud_mixed_meters_find", "ce_hud_unit_interface", "ce_hud_weapon_interface",
             "ce_hud_grenade_interface", "ce_hud_tags_loaded", "ce_hud_tags_unloaded", "ce_hud_map_identity",
             "ce_hud_halo_pc_overlay_blend_functions", "ce_hud_overlays_blocked", "ce_hud_gearbox_meters",
             "ce_hud_bitmap_is_meter", "ce_hud_bitmap_is_mixed_meter")

# a fault in ce_hud.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "bitmap-flags-ignored": (("unsigned short bitmap_flags = bitmap_group ?",
                              "unsigned short bitmap_flags = 0 && bitmap_group ?"), "ce_hud_placement", "elements"),
    "items-by-flag-only": (("element + WEAPON_HUD_BITMAP_OFFSET);", "NULL);"), "ce_hud_items_placements", "items"),
    "digits-rounded-down": (("(value + 1) / 2", "value / 2"), "ce_hud_digits_halve", "digits-halved"),
    "any-checksum": (("ce_hud_behaviour_maps[index].tags_checksum == tags_checksum &&", ""),
                     "ce_hud_map_identity", "behaviours"),
    # (no "numbers-kept" control since ChupathingyCE 0.8.0d: ce_hud leaves a
    # number's placement alone, hud_draw_numbers halving its digits)
    "shaders-by-another-behaviour": (("BEHAVIOUR_FLAG(_ce_behaviour_invert_detail_after_reflection))\n",
                                      "BEHAVIOUR_FLAG(_ce_behaviour_hud_number_scale))\n"),
                                     "ce_hud_tags_loaded", "detail-after-reflection"),
    "mixed-by-any-group": (("if (ce_hud_static_groups[group] == ce_hud_meter_groups[index])", "if (1)"),
                           "ce_hud_mixed_meters_find", "mixed-meters"),
    "xbox-ammo-by-name-only": (("(ce_hud_behaviours & BEHAVIOUR_FLAG(_ce_behaviour_xbox_ammo_meter_channels)) && ", ""),
                               "ce_hud_meter_bitmap", "mixed-meters"),
}


def generated(fault=None, faulty_function=None):
    source = read("port/linux/game/ce_hud.c")
    checks_header = read("port/linux/game/ce_map_checks.h")
    config = enum_with(checks_header, "CE_IMAGE_TAG_CACHE_BASE") + "\n"
    config += structure(checks_header, "ce_tag_instance") + "\n"
    # (the room above the tag cache for converted models: none here)
    config += "#define CE_CONVERSION_ROOM 0UL\n"
    config += structure(checks_header, "ce_image") + "\n"
    config += enum_with(read("source/bitmaps/bitmap_group.h"), "_bitmap_group_half_hud_scale_bit") + "\n"
    config += enum_with(source, "PLACEMENT_SCALE_X_OFFSET") + "\n"
    config += enum_with(source, "_ce_behaviour_gearbox_meters") + "\n"
    config += re.search(r"^#define BEHAVIOUR_FLAG\(.*$", source, re.M).group(0) + "\n"
    table = re.search(r"^#define BEHAVIOUR\(name\).*?^#undef BEHAVIOUR$", source, re.M | re.S)
    assert table, "the behaviours' table not found in ce_hud.c"
    config += table.group(0) + "\n"
    globals_ = re.search(r"^/\* what the map loaded relies on.*?^static struct ce_image const \*ce_hud_image;$", source,
                         re.M | re.S)
    assert globals_, "ce_hud.c's globals not found"
    config += globals_.group(0) + "\n"
    text = "boolean ce_hud_bitmap_is_meter(void const *bitmap);\n"
    # (a tag's name: ce_map_checks.c's, the test's names here)
    text += "static char const *test_tag_name(void const *instance);\n"
    text += ("static char const *ce_image_tag_name(struct ce_image const *image, void const *instance)\n"
             "{\n\t(void)image;\n\treturn test_tag_name(instance);\n}\n")
    # (ChupathingyCE's Xbox button icons for HUD messages, which ce_hud_tags_loaded adds first, read from
    # ui.map: none here, as these maps' HUD scale is what is tested)
    text += "static long ce_hud_button_icon_group_count;\n"
    text += ("static void ce_hud_button_icons(byte *hud, struct ce_image const *image)\n"
             "{\n\t(void)hud;\n\t(void)image;\n}\n")
    for name in FUNCTIONS:
        code = function(read("port/linux/game/ce_map_checks.c") if name == "ce_range_within" else source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("config.inc", config), ("under_test.inc", text),
            ("custom_edition_behaviours.inc", read("port/linux/game/custom_edition_behaviours.inc")))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("ce_hud_scale", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("ce_hud_scale", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"ce_hud.c with a fault ({control}) passed '{case}': the test cannot see it"
