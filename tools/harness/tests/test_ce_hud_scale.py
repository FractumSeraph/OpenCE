"""The real Custom Edition HUD conversion of ce_hud.c: the placements drawn at half their scale (Halo PC's high
resolution scale flag, a bitmap's half HUD scale and force HUD high resolution scale flags), numbers keeping their flag
for hud_draw_numbers, and what the maps Chimera lists rely on (OpenCE's custom_edition_behaviours.inc): the digits
halved, the bitmaps' flags cleared, the overlays' blend functions and overlays not drawn, the model shaders' detail
after reflection flipped."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["elements", "numbers-keep-flag", "items", "digits-kept", "digits-halved", "bitmap-flags-cleared",
         "detail-after-reflection", "behaviours"]
FUNCTIONS = ("ce_range_within", "ce_hud_read_long", "ce_hud_tag_cache_pointer", "ce_hud_reference_tag",
             "ce_hud_bitmap_halves_scale", "ce_hud_placement", "ce_hud_element", "ce_hud_block_placements",
             "ce_hud_items_placements", "ce_hud_bitmap_scale_flags_clear", "ce_hud_digits_halve",
             "ce_hud_meter_bitmap", "ce_hud_block_meter_bitmaps", "ce_hud_unit_interface", "ce_hud_weapon_interface",
             "ce_hud_grenade_interface", "ce_hud_tags_loaded", "ce_hud_tags_unloaded", "ce_hud_map_identity",
             "ce_hud_halo_pc_overlay_blend_functions", "ce_hud_overlays_blocked", "ce_hud_bitmap_is_meter")

# a fault in ce_hud.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "bitmap-flags-ignored": (("PLACEMENT_HIGH_RESOLUTION_SCALE_FLAG) || bitmap_halves_scale)",
                              "PLACEMENT_HIGH_RESOLUTION_SCALE_FLAG))"), "ce_hud_placement", "elements"),
    "items-by-flag-only": (("bitmap_halves_scale ||\n\t\t\t(bitmap_offset", "(bitmap_offset"),
                           "ce_hud_block_placements", "items"),
    "digits-rounded-down": (("(value + 1) / 2", "value / 2"), "ce_hud_digits_halve", "digits-halved"),
    "any-checksum": (("ce_hud_behaviour_maps[index].tags_checksum == tags_checksum &&", ""),
                     "ce_hud_map_identity", "behaviours"),
    "numbers-halved": (("ce_hud_items_placements(hud + WEAPON_HUD_CROSSHAIRS_OFFSET",
                        "ce_hud_block_placements(hud + WEAPON_HUD_NUMBERS_OFFSET, WEAPON_HUD_NUMBER_SIZE, "
                        "WEAPON_HUD_ELEMENT_OFFSET, NO_ELEMENT_BITMAP, FALSE);\n\tce_hud_items_placements(hud + "
                        "WEAPON_HUD_CROSSHAIRS_OFFSET"), "ce_hud_weapon_interface", "numbers-keep-flag"),
    "shaders-by-another-behaviour": (("BEHAVIOUR_FLAG(_ce_behaviour_invert_detail_after_reflection))\n",
                                      "BEHAVIOUR_FLAG(_ce_behaviour_hud_number_scale))\n"),
                                     "ce_hud_tags_loaded", "detail-after-reflection"),
}


def generated(fault=None, faulty_function=None):
    source = read("port/linux/game/ce_hud.c")
    checks_header = read("port/linux/game/ce_map_checks.h")
    config = enum_with(checks_header, "CE_IMAGE_TAG_CACHE_BASE") + "\n"
    config += structure(checks_header, "ce_tag_instance") + "\n"
    config += enum_with(source, "PLACEMENT_SCALE_X_OFFSET") + "\n"
    config += enum_with(source, "_ce_behaviour_gearbox_meters") + "\n"
    config += re.search(r"^#define BEHAVIOUR_FLAG\(.*$", source, re.M).group(0) + "\n"
    table = re.search(r"^#define BEHAVIOUR\(name\).*?^#undef BEHAVIOUR$", source, re.M | re.S)
    assert table, "the behaviours' table not found in ce_hud.c"
    config += table.group(0) + "\n"
    globals_ = re.search(r"^/\* what the map loaded relies on.*?^static long ce_hud_meter_bitmap_count;$", source,
                         re.M | re.S)
    assert globals_, "ce_hud.c's globals not found"
    config += globals_.group(0) + "\n"
    text = "boolean ce_hud_bitmap_is_meter(void const *bitmap);\n"
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
