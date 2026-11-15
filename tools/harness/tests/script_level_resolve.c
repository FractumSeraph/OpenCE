/* Real destination resolvers with a small catalog; no retail maps needed. */
#include "harness.h"
#ifndef _WIN32
#include <strings.h>
#define _stricmp strcasecmp
#endif
#define csstrncpy strncpy
/* (this fork: a Halo PC map is <name>@ce, @md or @pc, its map families,
port/linux/game/map_families.c, which the stand-ins below tell; a level of
the same family is one this machine can load, cache_files_map_loadable) */
enum { _map_family_xbox, _map_family_custom_edition, _map_family_halomd, _map_family_halo_pc };
static int mcc_lookups, ce_lookups;
static char const *catalog_level = "mcc_maps\\campaign\\next";
static boolean catalog_campaign = TRUE;
static boolean mcc_level_name(char const *name) { return name && !strncmp(name, "mcc_maps\\", 9); }
static short mcc_maps_find(char const *name) { mcc_lookups++; return strcmp(name, catalog_level) ? NONE : 0; }
static boolean mcc_maps_campaign(short index) { (void)index; return catalog_campaign; }
static char const *mcc_maps_level_name(short index) { (void)index; return catalog_level; }
static short map_family_parse(char const *map, char *file, long size)
{
    size_t length = map ? strlen(map) : 0;
    (void)file; (void)size;
    return length > 3 && !strcmp(map + length - 3, "@ce") ? _map_family_custom_edition : _map_family_xbox;
}
static char const *map_family_suffix(short family) { return family == _map_family_custom_edition ? "@ce" : ""; }
static boolean cache_files_map_loadable(char const *map_name, char *reason, long reason_size)
{
    (void)reason; (void)reason_size;
    ce_lookups++; return !strcmp(map_name, "next@ce");
}
#include "under_test.inc"

int main(int argc, char **argv)
{
    char next[64] = "unchanged", long_name[66];
    char const *name, *current = "mcc_maps\\campaign\\start", *requested = "next";
    size_t size = sizeof(next);
    boolean expected = TRUE;
    CHECK(argc == 2, "case required");
    name = argv[1];
    if (!strcmp(name, "mcc")) {}
    else if (!strcmp(name, "mcc-path")) requested = "levels/next.MAP";
    else if (!strcmp(name, "mcc-foreign-path")) {
        requested = "mcc_maps\\other\\next.map";
        /* Paths in script strings cannot select another package. */
    } else if (!strcmp(name, "mcc-missing")) { requested = "absent"; expected = FALSE; }
    else if (!strcmp(name, "mcc-multiplayer")) { catalog_campaign = FALSE; expected = FALSE; }
    else if (!strcmp(name, "ce")) { current = "start@ce"; requested = "levels\\next.map"; }
    else if (!strcmp(name, "ce-missing")) { current = "start@ce"; requested = "absent"; expected = FALSE; }
    else if (!strcmp(name, "xbox")) { current = "levels\\a10\\a10"; expected = FALSE; }
    else if (!strcmp(name, "empty")) { requested = "levels/"; expected = FALSE; }
    else if (!strcmp(name, "long")) {
        memset(long_name, 'a', sizeof(long_name) - 1); long_name[sizeof(long_name) - 1] = 0;
        requested = long_name; expected = FALSE;
    } else if (!strcmp(name, "small-buffer")) { size = strlen(catalog_level); expected = FALSE; }
    else if (!strcmp(name, "transitions")) {
        CHECK(main_script_level_resolve(current, "next", next, sizeof(next)), "MCC resolves");
        CHECK(main_script_level_resolve("start@ce", "next", next, sizeof(next)), "CE resolves after MCC");
        CHECK(!strcmp(next, "next@ce"), "no MCC result leaks into CE");
        CHECK(!main_script_level_resolve("levels\\a10\\a10", "next", next, sizeof(next)), "Xbox unchanged");
        CHECK(main_script_level_resolve(current, "next", next, sizeof(next)), "MCC resolves after Xbox");
        CHECK(mcc_lookups == 2 && ce_lookups == 1, "each catalog owns only its family");
        return 0;
    } else CHECK(FALSE, "unknown case");
    CHECK(main_script_level_resolve(current, requested, next, size) == expected, "destination acceptance");
    if (expected) CHECK(!strcmp(next, mcc_level_name(current) ? catalog_level : "next@ce"), "canonical destination");
    else CHECK(!strcmp(next, "unchanged"), "refusal leaves output alone");
    if (!mcc_level_name(current)) CHECK(!mcc_lookups, "legacy paths never query MCC catalog");
    else CHECK(!ce_lookups, "MCC paths never query CE catalog");
    CHECK(!mcc_main_script_level_resolve(current, requested, next, 0), "zero output capacity refused");
    return 0;
}
