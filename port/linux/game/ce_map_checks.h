/*
CE_MAP_CHECKS.H

What a Custom Edition map's data is read through while it is checked or
converted (ce_map_checks.c): an image of the map's tag cache, or of one of its
structure BSPs, and the reason a map is refused.
*/

#ifndef __CE_MAP_CHECKS_H
#define __CE_MAP_CHECKS_H
#pragma once

#ifdef HALO_CUSTOM_EDITION

#include <string.h>

/* ---------- constants */

enum
{
	/* (cache_files.c's: where a Custom Edition map's tags are) */
	CE_IMAGE_TAG_CACHE_BASE = 0x40440000,
	CE_IMAGE_TAG_CACHE_SIZE = 0x01700000,
	CE_TAG_INSTANCE_SIZE = 0x20,
	/* the most one allocation of the port's converted models and level takes
	(ce_resources_allocate): a strip of a part's most triangles (ce_models.c),
	aligned */
	CE_LARGEST_CONVERSION_ALLOCATION = 0x20000,
};

/* the room above the tag cache for the port's converted models and level
(port/linux/src/platform.h): its size, 0 if it is not mapped, and always 0
on a console or with the console's limits */
#include "halo_port_limits.h"
#if defined(HALO_PORT_CONSOLE_LIMITS) || defined(HALO_CONSOLE_LIMITS)
#define CE_CONVERSION_ROOM 0UL
#else
extern unsigned int platform_ce_conversion_room;
#define CE_CONVERSION_ROOM ((unsigned long)platform_ce_conversion_room)
#endif

/* ---------- structures */

/* (cache_files.c's) */
struct ce_tag_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	unsigned long tag_index;
	unsigned long name;
	unsigned long base_address;
	unsigned long indexed;
	unsigned long unused;
};

/* bytes standing for the Xbox addresses [base, base + size): the map's tag
cache itself, or a copy of it (or of a BSP) being checked */
struct ce_image
{
	byte *data;
	unsigned long base;
	unsigned long size;
};

/* reads size bytes at offset in the map's file into buffer (context is the
caller's: the file being checked, or the one loaded): FALSE if they could not
all be read */
typedef boolean (*ce_map_read_proc)(void *context, unsigned long offset, void *buffer, unsigned long size);

/* ---------- inline code */

/* a tag's field, of any alignment */
static inline unsigned long ce_read_long(
	byte const *at)
{
	unsigned long value;

	memcpy(&value, at, sizeof(value));
	return value;
}

static inline void ce_write_long(
	byte *at,
	unsigned long value)
{
	memcpy(at, &value, sizeof(value));
}

static inline short ce_read_short(
	byte const *at)
{
	short value;

	memcpy(&value, at, sizeof(value));
	return value;
}

static inline void ce_write_short(
	byte *at,
	short value)
{
	memcpy(at, &value, sizeof(value));
}

/* ---------- prototypes */

/* whether [offset, offset + size) lies within [0, limit), without overflow */
boolean ce_range_within(unsigned long offset, unsigned long size, unsigned long limit);

/* the bytes at address, if all size of them are in the image; else NULL */
void *ce_image_pointer(struct ce_image const *image, unsigned long address, unsigned long size);

/* a tag block (count, address) in the image at field: its elements, each
element_size bytes, all in the image, and no more than maximum_count of them;
FALSE (refused, naming what) if not. An empty block has no elements (NULL) */
boolean ce_image_block(struct ce_image const *image, void const *field, unsigned long element_size,
	unsigned long maximum_count, char const *what, long *count, byte **elements);

/* a tag data (size, flags, file offset, address) in the image at field: its
bytes, all in the image (NULL, and *size 0, if it is empty); FALSE (refused)
if not */
boolean ce_image_data(struct ce_image const *image, void const *field, char const *what, unsigned long *size,
	byte **data);

/* a tag instance's name, for messages (in the image, or once the map is
loaded its copy: ce_resources_tag_name) */
char const *ce_image_tag_name(struct ce_image const *image, void const *instance);

/* a loaded map's tag name, copied out of its tag cache
(ce_resources_tag_names_copy, ce_resources.c), at its Xbox address; else
NULL */
char const *ce_resources_tag_name(unsigned long address);

/* the map being checked refused: the first reason given is kept (and
logged by ce_map_check); returns FALSE */
boolean ce_refuse(char const *format, ...);

/* the reason for the refusal that follows (called before ce_refuse), in a
player's words; only the first, and only before a refusal, is kept */
void ce_refusal_explain(char const *format, ...);

/* the message for a player about the map ce_map_check refused last ("" if
it was accepted): the map, and the reason when ce_refusal_explain gave one */
char const *ce_map_refusal_message(void);

/* whether a map is being checked (ce_map_check) rather than loaded */
boolean ce_map_checking(void);

/* why the map checked last was refused (ce_map_check), or empty */
char const *ce_map_refusal(void);

/* a refused map's file checked against the hash it should have: a hashed
name's digits (map_archive.h), or the version the host plays
(map_family_game_version); header_checksum is its header's. The verdict (is
the file damaged, or the map itself refused) in verdict (size bytes), logged
and shown as the refusal is */
void ce_map_refused_verify(char const *map_name, char const *path, unsigned long header_checksum, char *verdict,
	long size);

/* a protected map's tag table repaired as Halo PC's engine let it be
(ce_repairs.c): image is the map's own tag data (its tag header first, no
resource maps' tags yet), read_proc is how its structure BSPs are read from its
file (each within file_size bytes of it). Done before the map's tags are
checked (ce_map_checks.c) and again, the same, before they are used as they
load (ce_resources_tags_loaded) */
void ce_repairs_protection(struct ce_image const *image, ce_map_read_proc read_proc, void *context,
	unsigned long file_size);

/* the cache version of the map being checked or loaded (ce_map_checks.c):
Custom Edition's (609), or Halo PC retail's (7: HaloMD's maps, played as
<name>@md, whose bitmaps' pixels and sounds' samples are at offsets in Halo
PC's own resource maps, found in Custom Edition's by their tags' paths:
ce_resources.c) */
enum
{
	CE_CACHE_VERSION_CUSTOM_EDITION = 609,
	CE_CACHE_VERSION_RETAIL = 7,
};
extern long ce_map_cache_version;

/* the family of the map being checked or loaded, for messages: "Custom
Edition" or "HaloMD" */
char const *ce_map_family_name(void);

#endif

#endif
