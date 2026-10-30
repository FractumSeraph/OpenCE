/*
CE_REPAIRS.C

Custom Edition and HaloMD maps' tags repaired where Halo PC's engine let
them be (cache_files.c, the maps past the Xbox's). Such maps were built by
Halo PC's tools and by others that edited the tags afterwards, and some have
things Halo PC's engine read without checking them, where this one asserts
or reads past a tag's end:

  - predicted resources (what the game reads ahead when an object, a
    first-person weapon, the UI or a BSP's cluster comes into view) naming a
    tag that is not a bitmap or sound, or a bitmap past the bitmap tag's:
    dropped (they are only read ahead);
  - predicted resources naming their tag with the salt of another build of
    the map's tags: given the tag's own (Halo PC's engine took the index
    alone, as cache_files.c does for these maps);
  - an object tag whose type is not its group's (a HaloMD map's scenery
    typed a light fixture, which Halo PC's engine made a light fixture of,
    reading the rest of a light fixture past the scenery's end): given its
    group's type;
  - an object's modifier shader that is not a shader, or of a type that
    cannot modify an object (an environment or model shader: Halo PC's
    engine, and this one, draw the object without it, this one logging
    each frame it is drawn): none;
  - a model's shader naming a tag that is not a shader (a HaloMD map's
    model has a light for one, which Halo PC's engine drew with whatever it
    found there): given the model's first shader that is one, or else the
    map's first shader; one naming a shader with the salt of another build:
    given the shader's own;
  - the scenario the map's header names with a group other than the
    scenario's (a map protector renames it: h2_ascension's, Headlong_PB2's
    and pitfall's is 'prot'), which Halo PC's engine took as the scenario
    whatever its group said: given the scenario's group, before the map's
    scenario is checked (ce_map_checks.c);
  - the rest of a map protector's work on the tag table (OpenCE's
    29426d53c, "Fix custom edition protected map loading"), which Halo PC's
    engine never read, as it found the tags by the references to them
    (ce_repairs_protection): a tag's name left outside the tag data or not
    terminated (stripped), given an empty one, but for a tag held by a
    resource map, which is found there by its name; a structure BSP the
    scenario names with another group given the structure BSP's; a tag
    whose group is none of Halo's (scrambled; the extractors' "INVALID")
    given the group the references to it name, in the tags and in the
    structure BSPs, when they all name the same one; and a tag with no data
    (a garbage tag), left with no group, which nothing then reads as one,
    and the references to it in the tags made none;
  - a tag whose parent groups are not its group's (Invader leaves a
    vehicle's and a water shader's none: beavercreek_rev_beta's), which
    the game's tag_get checks a vehicle against as an object: given its
    group's;
  - a model's or animation graph's nodes linked into a loop (a node's
    sibling or child one reached already: [h3]_sandtrap's cyborg graph has
    its spine's next sibling the pelvis, the first node), which the game
    walks without end, past its arrays of nodes: that link cut; and a
    node's link past the nodes (h2_ascension's has its parents a byte too
    high, 256 for 1): a sibling or child cut, a parent made the one the
    nodes' tree gives it;
  - a bitmap in MCC's high-quality compression (format 18: BC7, which
    Chimera draws on Halo PC and Halo PC's engine refused): made DXT5's
    format, whose 4x4 blocks are its size, and marked so that its blocks are
    decoded as BC7's when it is drawn (ce_bitmap_is_bc7: xbox_texture_cache.c,
    xbox_textures.c);
  - an effect's flags, in Gearbox's layout ("must be deterministic" in bit
    2, where the Xbox has it in bit 1: ce_effect_flags.h): read as the
    Xbox's, so that its effects are deterministic where Halo PC's are.

The repairs are made before the map is opened, to the image it is checked
in (ce_map_checks.c, so that its checks see the map as it will play), and
again when its tags load and when each of its BSPs loads. Nothing here
touches an Xbox map: only the maps in the slot past the Xbox's are read this
way (cache_files_windows.c).
*/

#ifdef HALO_CUSTOM_EDITION

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "structures/structure_bsp_definitions.h"
#include "ce_map_checks.h"
#include "ce_effect_flags.h"

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* (a tag block's or tag data's count or size: larger ones would not
	fit in a tag cache in any case) */
	CE_MAXIMUM_ELEMENTS = 0x10000,

	/* predicted resources (predicted_resources.h): their type, resource
	index and tag index */
	PREDICTED_RESOURCE_SIZE = 0x08,
	PREDICTED_RESOURCE_BITMAP = 0,
	PREDICTED_RESOURCE_SOUND = 1,
	/* their blocks: an object's (object_definitions.h), a weapon's first
	person's (weapon_definitions.h) and the scenario's UI's
	(scenario_definitions.h) */
	OBJECT_PREDICTED_RESOURCES_OFFSET = 0x170,
	WEAPON_PREDICTED_RESOURCES_OFFSET = 0x4e4,
	SCENARIO_PREDICTED_RESOURCES_OFFSET = 0xec,
	/* an object's modifier shader (object_definitions.h: a tag reference,
	its tag index last) */
	OBJECT_MODIFIER_SHADER_OFFSET = 0x90,
	TAG_REFERENCE_INDEX_OFFSET = 0x0c,
	/* a shader's type (shader_definitions.h), Halo PC's: those that can be
	modifiers (shaders.c, shader_type_is_valid_for_modifier: 1, and
	transparent to plasma, with Halo PC's extended chicago at 7) */
	SHADER_TYPE_OFFSET = 0x24,
	SHADER_TYPE_FIRST_MODIFIER = 5,
	SHADER_TYPE_LAST_MODIFIER = 11,
	/* (bitmap_group.h: its bitmaps block) */
	BITMAP_GROUP_BITMAPS_OFFSET = 0x60,
	/* a bitmap (bitmap_group.h's bitmap_data): its format and flags; MCC's
	high-quality compression (BC7), DXT5 and the compressed flag
	(bitmaps.h); the port's mark of a BC7 bitmap made DXT5's format, kept in
	its mipmap pad, which nothing reads, so bitmap_verify still sees Halo's
	own flags only; a high flag no Halo tool sets, cleared from every bitmap
	as before */
	BITMAP_DATA_SIZE = 0x30,
	BITMAP_DATA_FORMAT_OFFSET = 0x0c,
	BITMAP_DATA_FLAGS_OFFSET = 0x0e,
	BITMAP_DATA_MIPMAP_PAD_OFFSET = 0x16,
	BITMAP_FORMAT_DXT5 = 16,
	BITMAP_FORMAT_BC7 = 18,
	BITMAP_COMPRESSED_FLAG = 0x02,
	BITMAP_PORT_BC7_MARK = 0x4337,
	BITMAP_STRAY_HIGH_FLAG = 0x8000,

	/* a model's shaders (model_definitions.h): each a tag reference and
	a permutation */
	MODEL_SHADERS_OFFSET = 0xdc,
	MODEL_SHADER_SIZE = 0x20,
	MODEL_HEADER_SIZE = 0xe8,
	SHADER_GROUP = 'shdr',
	SCENARIO_GROUP = 'scnr',

	/* a model's and an animation graph's nodes (model_definitions.h,
	model_animation_definitions.h): each its next sibling, first child and
	parent, at the same place; the most either has (ce_models.c) */
	MODEL_NODES_OFFSET = 0xb8,
	MODEL_NODE_SIZE = 0x9c,
	ANIMATION_GRAPH_NODES_OFFSET = 0x68,
	ANIMATION_GRAPH_NODE_SIZE = 0x40,
	NODE_LINKS_OFFSET = 0x20,
	CE_MAXIMUM_NODES = 64,

	/* the tag header (ce_map_checks.c's ce_tag_header): its instances, its
	scenario's handle and its tags' count, and its size */
	TAG_HEADER_INSTANCES_OFFSET = 0x00,
	TAG_HEADER_SCENARIO_OFFSET = 0x04,
	TAG_HEADER_TAG_COUNT_OFFSET = 0x0c,
	TAG_HEADER_SIZE = 0x28,
	/* a tag instance's unused last word (ce_tag_instance), where a tag of
	no usable name is given an empty one */
	TAG_INSTANCE_UNUSED_OFFSET = 0x1c,
	/* a tag's name, with its terminator (ce_map_checks.c's) */
	MAXIMUM_TAG_NAME_LENGTH = 256,
	/* a tag reference (tag_groups.h): its group, name, name's length and
	tag index */
	TAG_REFERENCE_SIZE = 0x10,
	/* the scenario's structure BSPs (scenario_definitions.h): each one's
	place in the file and its tag index; no more than the game has
	(ce_map_checks.c's) */
	SCENARIO_BSPS_OFFSET = 0x5a4,
	SCENARIO_BSP_SIZE = 0x20,
	SCENARIO_BSP_FILE_OFFSET_OFFSET = 0x00,
	SCENARIO_BSP_FILE_SIZE_OFFSET = 0x04,
	SCENARIO_BSP_TAG_INDEX_OFFSET = 0x1c,
	MAXIMUM_BSPS = 16,
	STRUCTURE_BSP_GROUP = 'sbsp',
};

/* no group (a tag's, or a parent group's) */
#define NO_GROUP 0xffffffff

/* what a protected map's tag table is to each tag (ce_repairs_protection) */
enum
{
	/* none of the below: left as it is */
	_protection_tag_kept = 0,
	/* of no group of Halo's, with data: its group is the one the references
	to it name */
	_protection_tag_unknown,
	/* and a reference to it named one */
	_protection_tag_named,
	/* and references to it named two: left as it is */
	_protection_tag_named_twice,
	/* with no data: no group, and the references to it none */
	_protection_tag_empty,
};

/* ---------- structures */

/* what was repaired, for the log */
struct ce_repair_counts
{
	long predicted_resources_dropped;
	long predicted_resources_salted;
	long object_types;
	long modifier_shaders;
	long model_shaders;
	long node_links;
	long parent_groups;
	long bc7_bitmaps;
	long deterministic_effects;
};

/* ---------- globals */

/* the object types' groups, by type (object_definitions.h, objects.c) */
static unsigned long const ce_object_type_groups[] =
{
	'bipd', 'vehi', 'weap', 'eqip', 'garb', 'proj', 'scen', 'mach', 'ctrl', 'lifi', 'plac', 'ssce',
};

/* the groups with parents, and their parents (tag_groups.c's, as Halo PC
has them) */
static struct
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
} const ce_group_parents[] =
{
	{ 'bipd', { 'unit', 'obje' } },
	{ 'vehi', { 'unit', 'obje' } },
	{ 'weap', { 'item', 'obje' } },
	{ 'eqip', { 'item', 'obje' } },
	{ 'garb', { 'item', 'obje' } },
	{ 'mach', { 'devi', 'obje' } },
	{ 'ctrl', { 'devi', 'obje' } },
	{ 'lifi', { 'devi', 'obje' } },
	{ 'proj', { 'obje', 0xffffffff } },
	{ 'scen', { 'obje', 0xffffffff } },
	{ 'plac', { 'obje', 0xffffffff } },
	{ 'ssce', { 'obje', 0xffffffff } },
	{ 'unit', { 'obje', 0xffffffff } },
	{ 'item', { 'obje', 0xffffffff } },
	{ 'devi', { 'obje', 0xffffffff } },
	{ 'senv', { 'shdr', 0xffffffff } },
	{ 'soso', { 'shdr', 0xffffffff } },
	{ 'sotr', { 'shdr', 0xffffffff } },
	{ 'schi', { 'shdr', 0xffffffff } },
	{ 'scex', { 'shdr', 0xffffffff } },
	{ 'swat', { 'shdr', 0xffffffff } },
	{ 'sgla', { 'shdr', 0xffffffff } },
	{ 'smet', { 'shdr', 0xffffffff } },
	{ 'spla', { 'shdr', 0xffffffff } },
};

/* Halo PC's tag groups (tag_groups.c's, as Custom Edition's tools have
them): a tag of any other has its group scrambled (ce_repairs_protection).
The first NUMBER_OF_CE_PARENT_GROUPS are only ever parents, never a tag's
own */
#define NUMBER_OF_CE_PARENT_GROUPS 5
static unsigned long const ce_groups[] =
{
	'obje', 'unit', 'item', 'devi', 'shdr',
	'actr', 'actv', 'ant!', 'antr', 'bipd', 'bitm', 'boom', 'cdmg', 'coll', 'colo', 'cont', 'ctrl', 'deca',
	'DeLa', 'devc', 'dobc', 'effe', 'elec', 'eqip', 'flag', 'fog ', 'font', 'foot', 'garb', 'glw!', 'grhi',
	'hmt ', 'hud#', 'hudg', 'itmc', 'jpt!', 'lens', 'lifi', 'ligh', 'lsnd', 'mach', 'matg', 'metr', 'mgs2',
	'mod2', 'mode', 'mply', 'ngpr', 'part', 'pctl', 'phys', 'plac', 'pphy', 'proj', 'rain', 'sbsp', 'scen',
	'scex', 'schi', 'scnr', 'senv', 'sgla', 'sky ', 'smet', 'snd!', 'snde', 'soso', 'sotr', 'Soul', 'spla',
	'ssce', 'str#', 'swat', 'tagc', 'trak', 'udlg', 'unhi', 'ustr', 'vcky', 'vehi', 'weap', 'wind', 'wphi',
};

/* the loaded map's tags (ce_repairs_tags_loaded), for its BSPs' */
static struct ce_tag_instance *ce_loaded_instances;
static long ce_loaded_tag_count;

/* ---------- private code */

static struct ce_tag_instance *ce_instance(
	void *tag_instances,
	long index)
{
	return (struct ce_tag_instance *)((byte *)tag_instances + index * CE_TAG_INSTANCE_SIZE);
}

/* the instance a tag index names by its index alone (as Halo PC's engine
took it), or NULL */
static struct ce_tag_instance *ce_instance_by_index(
	void *tag_instances,
	long tag_count,
	unsigned long tag_index)
{
	if (tag_index == 0xffffffff || (tag_index & 0xffff) >= (unsigned long)tag_count)
		return NULL;
	return ce_instance(tag_instances, (long)(tag_index & 0xffff));
}

static boolean ce_is_of_group(
	struct ce_tag_instance const *instance,
	unsigned long group_tag)
{
	return instance->group_tag == group_tag || instance->parent_group_tags[0] == group_tag ||
		instance->parent_group_tags[1] == group_tag;
}

static boolean ce_is_shader(
	struct ce_tag_instance const *instance)
{
	return instance->group_tag == 'scex' || ce_is_of_group(instance, SHADER_GROUP);
}

/* a tag block at field (count, address): its elements, if all of them are
in the image (no refusal: the checks refuse what they read) */
static byte *ce_block(
	struct ce_image const *image,
	byte const *field,
	unsigned long element_size,
	long *count)
{
	long block_count = (long)ce_read_long(field);
	byte *elements;

	*count = 0;
	if (block_count <= 0 || block_count > CE_MAXIMUM_ELEMENTS)
		return NULL;
	elements = ce_image_pointer(image, ce_read_long(field + 4), (unsigned long)block_count * element_size);
	if (elements)
		*count = block_count;
	return elements;
}

/* a predicted resources block: those naming nothing they can be dropped
(the rest moved down over them, the count lessened), those naming their tag
with another salt given its own */
static void ce_predicted_resources_repair(
	struct ce_image const *image,
	byte *field,
	void *tag_instances,
	long tag_count,
	struct ce_repair_counts *counts)
{
	long count, index, kept = 0;
	byte *resources = ce_block(image, field, PREDICTED_RESOURCE_SIZE, &count);

	for (index = 0; index < count; index++)
	{
		byte *resource = resources + index * PREDICTED_RESOURCE_SIZE;
		short type = ce_read_short(resource);
		short resource_index = ce_read_short(resource + 2);
		unsigned long tag_index = ce_read_long(resource + 4);
		struct ce_tag_instance *instance = ce_instance_by_index(tag_instances, tag_count, tag_index);
		boolean valid = TRUE;

		if (type == PREDICTED_RESOURCE_BITMAP)
		{
			byte *group = instance && instance->group_tag == 'bitm' ?
				ce_image_pointer(image, instance->base_address, BITMAP_GROUP_BITMAPS_OFFSET + 4) : NULL;

			valid = group && resource_index >= 0 &&
				(unsigned long)resource_index < ce_read_long(group + BITMAP_GROUP_BITMAPS_OFFSET);
		}
		else if (type == PREDICTED_RESOURCE_SOUND)
			valid = instance && instance->group_tag == 'snd!';
		if (!valid)
		{
			counts->predicted_resources_dropped++;
			continue;
		}
		if (instance && instance->tag_index != tag_index)
		{
			ce_write_long(resource + 4, instance->tag_index);
			counts->predicted_resources_salted++;
		}
		if (kept != index)
			memmove(resources + kept * PREDICTED_RESOURCE_SIZE, resource, PREDICTED_RESOURCE_SIZE);
		kept++;
	}
	if (kept != count)
		ce_write_long(field, (unsigned long)kept);
}

/* an object tag's type made its group's, if it is not */
static void ce_object_type_repair(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	struct ce_repair_counts *counts)
{
	byte *object = ce_image_pointer(image, instance->base_address, OBJECT_PREDICTED_RESOURCES_OFFSET + 0xc);
	short type, group_type;

	if (!object)
		return;
	for (group_type = 0; group_type < (short)NUMBEROF(ce_object_type_groups); group_type++)
	{
		if (ce_object_type_groups[group_type] == instance->group_tag)
			break;
	}
	type = ce_read_short(object);
	if (group_type >= (short)NUMBEROF(ce_object_type_groups) || type == group_type)
		return;
	error(_error_silent, "%s map: object %s is typed %d, not its group's %d (made its group's)",
		ce_map_family_name(), ce_image_tag_name(image, instance), type, group_type);
	memcpy(object, &group_type, sizeof(group_type));
	counts->object_types++;
}

/* an object's modifier shader made none if it is not a shader, or not of
a type that can be one (render_objects.c draws the object without it) */
static void ce_modifier_shader_repair(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	void *tag_instances,
	long tag_count,
	struct ce_repair_counts *counts)
{
	byte *reference = ce_image_pointer(image, instance->base_address + OBJECT_MODIFIER_SHADER_OFFSET, 0x10);
	struct ce_tag_instance *shader;
	byte *data;
	short type;

	if (!reference || ce_read_long(reference + TAG_REFERENCE_INDEX_OFFSET) == 0xffffffff)
		return;
	shader = ce_instance_by_index(tag_instances, tag_count, ce_read_long(reference + TAG_REFERENCE_INDEX_OFFSET));
	data = shader && ce_is_shader(shader) ? ce_image_pointer(image, shader->base_address, SHADER_TYPE_OFFSET + 2) :
		NULL;
	type = data ? ce_read_short(data + SHADER_TYPE_OFFSET) : -1;
	if (type == 1 || (type >= SHADER_TYPE_FIRST_MODIFIER && type <= SHADER_TYPE_LAST_MODIFIER))
	{
		if (shader->tag_index != ce_read_long(reference + TAG_REFERENCE_INDEX_OFFSET))
			ce_write_long(reference + TAG_REFERENCE_INDEX_OFFSET, shader->tag_index);
		return;
	}
	ce_write_long(reference + TAG_REFERENCE_INDEX_OFFSET, 0xffffffff);
	counts->modifier_shaders++;
}

/* a model's shaders that name a tag that is not a shader (or a shader of
another salt) repaired */
static void ce_model_shaders_repair(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	void *tag_instances,
	long tag_count,
	struct ce_repair_counts *counts)
{
	byte *model = ce_image_pointer(image, instance->base_address, MODEL_HEADER_SIZE);
	byte *shaders;
	long count, index;
	struct ce_tag_instance *substitute = NULL;

	if (!model)
		return;
	shaders = ce_block(image, model + MODEL_SHADERS_OFFSET, MODEL_SHADER_SIZE, &count);
	for (index = 0; index < count; index++)
	{
		byte *reference = shaders + index * MODEL_SHADER_SIZE;
		struct ce_tag_instance *shader = ce_instance_by_index(tag_instances, tag_count,
			ce_read_long(reference + TAG_REFERENCE_INDEX_OFFSET));

		if (shader && ce_is_shader(shader))
		{
			if (shader->tag_index != ce_read_long(reference + TAG_REFERENCE_INDEX_OFFSET))
				ce_write_long(reference + TAG_REFERENCE_INDEX_OFFSET, shader->tag_index);
			continue;
		}
		/* (the model's first shader that is one, else the map's first) */
		if (!substitute)
		{
			long other;

			for (other = 0; other < count && !substitute; other++)
			{
				struct ce_tag_instance *candidate = ce_instance_by_index(tag_instances, tag_count,
					ce_read_long(shaders + other * MODEL_SHADER_SIZE + TAG_REFERENCE_INDEX_OFFSET));

				if (candidate && ce_is_shader(candidate))
					substitute = candidate;
			}
			for (other = 0; other < tag_count && !substitute; other++)
			{
				if (ce_is_shader(ce_instance(tag_instances, other)))
					substitute = ce_instance(tag_instances, other);
			}
			if (!substitute)
				return;
		}
		ce_write_long(reference, substitute->group_tag);
		ce_write_long(reference + TAG_REFERENCE_INDEX_OFFSET, substitute->tag_index);
		counts->model_shaders++;
	}
}

/* a model's or an animation graph's nodes (the block at field, each
node_size bytes): a next sibling or first child past the nodes cut, then the
nodes walked from the first, by next siblings and first children, as the
game walks them, and a link to a node reached already cut; a parent past
the nodes made the node's parent in that walk (none if it is not reached) */
static void ce_node_links_repair(
	struct ce_image const *image,
	byte *field,
	unsigned long node_size,
	struct ce_repair_counts *counts)
{
	long node_count;
	byte *nodes = ce_block(image, field, node_size, &node_count);
	boolean reached[CE_MAXIMUM_NODES];
	short parents[CE_MAXIMUM_NODES];
	short queue[CE_MAXIMUM_NODES];
	long read_index = 0, write_index = 0, index;

	if (!nodes || node_count > CE_MAXIMUM_NODES)
		return;
	for (index = 0; index < node_count; index++)
	{
		byte *links = nodes + index * node_size + NODE_LINKS_OFFSET;
		long link;

		for (link = 0; link < 2; link++)
		{
			short linked = ce_read_short(links + link * sizeof(short));

			if (linked != NONE && (linked < 0 || linked >= node_count))
			{
				ce_write_short(links + link * sizeof(short), NONE);
				counts->node_links++;
			}
		}
	}
	memset(reached, 0, sizeof(reached));
	reached[0] = TRUE;
	parents[0] = NONE;
	queue[write_index++] = 0;
	while (read_index < write_index)
	{
		short node_index = queue[read_index++];
		byte *links = nodes + node_index * node_size + NODE_LINKS_OFFSET;
		long link;

		/* (its next sibling, of its parent, then its first child) */
		for (link = 0; link < 2; link++)
		{
			short linked = ce_read_short(links + link * sizeof(short));

			if (linked == NONE)
				continue;
			if (reached[linked])
			{
				ce_write_short(links + link * sizeof(short), NONE);
				counts->node_links++;
				continue;
			}
			reached[linked] = TRUE;
			parents[linked] = link ? node_index : parents[node_index];
			queue[write_index++] = linked;
		}
	}
	for (index = 0; index < node_count; index++)
	{
		byte *parent = nodes + index * node_size + NODE_LINKS_OFFSET + 2 * sizeof(short);
		short parent_index = ce_read_short(parent);

		if (parent_index != NONE && (parent_index < 0 || parent_index >= node_count))
		{
			ce_write_short(parent, reached[index] ? parents[index] : NONE);
			counts->node_links++;
		}
	}
}

/* a tag given its group's parent groups, if it has others */
static void ce_parent_groups_repair(
	struct ce_tag_instance *instance,
	struct ce_repair_counts *counts)
{
	long index;

	for (index = 0; index < NUMBEROF(ce_group_parents); index++)
	{
		if (ce_group_parents[index].group_tag != instance->group_tag)
			continue;
		if (instance->parent_group_tags[0] != ce_group_parents[index].parent_group_tags[0] ||
			instance->parent_group_tags[1] != ce_group_parents[index].parent_group_tags[1])
		{
			instance->parent_group_tags[0] = ce_group_parents[index].parent_group_tags[0];
			instance->parent_group_tags[1] = ce_group_parents[index].parent_group_tags[1];
			counts->parent_groups++;
		}
		return;
	}
}

/* a bitmap tag's BC7 bitmaps made DXT5's format and marked; the mark taken
from any other */
static void ce_bitmaps_repair(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	struct ce_repair_counts *counts)
{
	byte *group = ce_image_pointer(image, instance->base_address, BITMAP_GROUP_BITMAPS_OFFSET + 0xc);
	byte *bitmaps;
	long count, index;

	if (!group)
		return;
	bitmaps = ce_block(image, group + BITMAP_GROUP_BITMAPS_OFFSET, BITMAP_DATA_SIZE, &count);
	for (index = 0; index < count; index++)
	{
		byte *bitmap = bitmaps + index * BITMAP_DATA_SIZE;
		short format, mark;
		unsigned short flags;

		memcpy(&format, bitmap + BITMAP_DATA_FORMAT_OFFSET, sizeof(format));
		memcpy(&flags, bitmap + BITMAP_DATA_FLAGS_OFFSET, sizeof(flags));
		memcpy(&mark, bitmap + BITMAP_DATA_MIPMAP_PAD_OFFSET, sizeof(mark));
		flags &= (unsigned short)~BITMAP_STRAY_HIGH_FLAG;
		if (format == BITMAP_FORMAT_BC7)
		{
			format = BITMAP_FORMAT_DXT5;
			flags |= BITMAP_COMPRESSED_FLAG;
			mark = BITMAP_PORT_BC7_MARK;
			counts->bc7_bitmaps++;
		}
		else if (mark == BITMAP_PORT_BC7_MARK)
			mark = 0;
		memcpy(bitmap + BITMAP_DATA_FORMAT_OFFSET, &format, sizeof(format));
		memcpy(bitmap + BITMAP_DATA_FLAGS_OFFSET, &flags, sizeof(flags));
		memcpy(bitmap + BITMAP_DATA_MIPMAP_PAD_OFFSET, &mark, sizeof(mark));
	}
}

/* an effect's flags read as the Xbox's (ce_effect_flags.h) */
static void ce_effect_flags_repair(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	struct ce_repair_counts *counts)
{
	byte *effect = ce_image_pointer(image, instance->base_address, CE_EFFECT_FLAGS_OFFSET + 4);
	unsigned long flags;

	if (!effect)
		return;
	flags = ce_effect_flags_from_gearbox(ce_read_long(effect + CE_EFFECT_FLAGS_OFFSET));
	ce_write_long(effect + CE_EFFECT_FLAGS_OFFSET, flags);
	if (flags & CE_EFFECT_XBOX_MUST_BE_DETERMINISTIC)
		counts->deterministic_effects++;
}

/* the place of a group among Halo PC's (ce_groups), or NONE */
static long ce_group_find(
	unsigned long group_tag)
{
	long index;

	for (index = 0; index < (long)NUMBEROF(ce_groups); index++)
	{
		if (ce_groups[index] == group_tag)
			return index;
	}
	return NONE;
}

/* a tag given a group, and that group's parents (ce_group_parents) */
static void ce_group_give(
	struct ce_tag_instance *instance,
	unsigned long group_tag)
{
	long index;

	instance->group_tag = group_tag;
	instance->parent_group_tags[0] = NO_GROUP;
	instance->parent_group_tags[1] = NO_GROUP;
	for (index = 0; index < (long)NUMBEROF(ce_group_parents); index++)
	{
		if (ce_group_parents[index].group_tag == group_tag)
		{
			instance->parent_group_tags[0] = ce_group_parents[index].parent_group_tags[0];
			instance->parent_group_tags[1] = ce_group_parents[index].parent_group_tags[1];
		}
	}
}

/* the tags whose names are not in the tag data, or not terminated within a
tag path's length (map protection strips them; Halo PC's engine found tags
by the references to them, not their names), given an empty one: a zero in
their instance's unused word, at instances_address (the instances' Xbox
address) in the image. Not a tag held by a resource map: it is found there
by its name, and is refused without one (ce_map_checks.c). Returns how many */
static long ce_protection_names(
	struct ce_image const *image,
	unsigned long instances_address,
	struct ce_tag_instance *instances,
	long tag_count)
{
	long index, emptied = 0;

	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = &instances[index];
		char const *name = ce_image_pointer(image, instance->name, 1);

		/* (a name in the image is no further from its end than its size) */
		if (instance->indexed || (name && memchr(name, 0, MIN(image->base + image->size - instance->name,
			(unsigned long)MAXIMUM_TAG_NAME_LENGTH))))
		{
			continue;
		}
		instance->unused = 0;
		instance->name = instances_address + (unsigned long)index * CE_TAG_INSTANCE_SIZE + TAG_INSTANCE_UNUSED_OFFSET;
		emptied++;
	}
	return emptied;
}

/* the structure BSPs the scenario names (scenario_tag_index, its handle)
given the structure BSP's group if map protection renamed it, as it does the
scenario's: a tag with no data yet (a BSP has none until it loads) and not
held by a resource map. Returns how many */
static long ce_protection_bsp_groups(
	struct ce_image const *image,
	struct ce_tag_instance *instances,
	long tag_count,
	unsigned long scenario_tag_index)
{
	struct ce_tag_instance *scenario = ce_instance_by_index(instances, tag_count, scenario_tag_index);
	byte *data = scenario && scenario->tag_index == scenario_tag_index ?
		ce_image_pointer(image, scenario->base_address, SCENARIO_BSPS_OFFSET + 0xc) : NULL;
	byte *bsps;
	long count, index, regrouped = 0;

	if (!data)
		return 0;
	bsps = ce_block(image, data + SCENARIO_BSPS_OFFSET, SCENARIO_BSP_SIZE, &count);
	/* (more than the game has: refused by ce_map_checks.c in any case) */
	for (index = 0; index < count && index < MAXIMUM_BSPS; index++)
	{
		unsigned long tag_index = ce_read_long(bsps + index * SCENARIO_BSP_SIZE + SCENARIO_BSP_TAG_INDEX_OFFSET);
		struct ce_tag_instance *bsp = ce_instance_by_index(instances, tag_count, tag_index);

		if (!bsp || bsp->tag_index != tag_index || bsp->group_tag == STRUCTURE_BSP_GROUP || bsp->base_address ||
			bsp->indexed)
		{
			continue;
		}
		ce_group_give(bsp, STRUCTURE_BSP_GROUP);
		regrouped++;
	}
	return regrouped;
}

/* the references in size bytes at data (the tags, or a structure BSP read
from the file) to the tags of a protected map's tag table that are not as
they should be (states, by tag index: ce_repairs_protection): each of no
group named the group a reference to it names (named_groups), once, or
twice if two references name two (only a group a tag can be, and not a
structure BSP's: the scenario names those); each with no data, the
references to it made none when make_none (the tags; a BSP read so is only
read). A reference is known by what it is, a group of Halo's, then 8 bytes,
then a tag's handle, its salt and all, wherever it is in the data: the tags
are not walked by their definitions here, and a tag of no group could not
be (nor are the tag header and the tags' instances, which hold a tag's group
and handle so: the caller leaves them out) */
static void ce_protection_references(
	byte *data,
	unsigned long size,
	struct ce_tag_instance const *instances,
	long tag_count,
	byte *states,
	unsigned long *named_groups,
	boolean make_none,
	long *made_none)
{
	unsigned long offset;

	if (size < TAG_REFERENCE_SIZE)
		return;
	for (offset = 0; offset <= size - TAG_REFERENCE_SIZE; offset++)
	{
		unsigned long tag_index, absolute_index, group_tag;
		long group;

		tag_index = ce_read_long(data + offset + TAG_REFERENCE_INDEX_OFFSET);
		absolute_index = tag_index & 0xffff;
		if (tag_index == 0xffffffff || absolute_index >= (unsigned long)tag_count ||
			states[absolute_index] == _protection_tag_kept || instances[absolute_index].tag_index != tag_index)
		{
			continue;
		}
		group_tag = ce_read_long(data + offset);
		group = ce_group_find(group_tag);
		if (group == NONE)
			continue;
		if (states[absolute_index] == _protection_tag_empty)
		{
			if (make_none)
			{
				ce_write_long(data + offset + TAG_REFERENCE_INDEX_OFFSET, 0xffffffff);
				(*made_none)++;
			}
			continue;
		}
		if (group < NUMBER_OF_CE_PARENT_GROUPS || group_tag == STRUCTURE_BSP_GROUP)
			continue;
		if (states[absolute_index] == _protection_tag_unknown)
		{
			states[absolute_index] = _protection_tag_named;
			named_groups[absolute_index] = group_tag;
		}
		else if (states[absolute_index] == _protection_tag_named && named_groups[absolute_index] != group_tag)
			states[absolute_index] = _protection_tag_named_twice;
	}
}

static void ce_repairs_log(
	struct ce_repair_counts const *counts)
{
	if (ce_map_checking() || !(counts->predicted_resources_dropped | counts->predicted_resources_salted |
		counts->object_types | counts->modifier_shaders | counts->model_shaders | counts->node_links |
		counts->parent_groups | counts->bc7_bitmaps | counts->deterministic_effects))
	{
		return;
	}
	error(_error_silent, "%s map: %ld predicted resources dropped and %ld given their tags' salts, %ld object "
		"types, %ld modifier shaders and %ld model shaders repaired, %ld node links looping back or out of the nodes, "
		"%ld tags' parent groups, %ld BC7 bitmaps; %ld effects deterministic", ce_map_family_name(),
		counts->predicted_resources_dropped, counts->predicted_resources_salted, counts->object_types,
		counts->modifier_shaders, counts->model_shaders, counts->node_links, counts->parent_groups,
		counts->bc7_bitmaps, counts->deterministic_effects);
}

/* ---------- public code */

/* the scenario the map's header names (scenario_tag_index, which must be a
tag's handle) given the scenario's group if it has another; TRUE if it was */
boolean ce_repairs_scenario_group(
	void *tag_instances,
	long tag_count,
	unsigned long scenario_tag_index)
{
	struct ce_tag_instance *scenario = ce_instance_by_index(tag_instances, tag_count, scenario_tag_index);

	if (!scenario || scenario->tag_index != scenario_tag_index || scenario->group_tag == SCENARIO_GROUP)
		return FALSE;
	scenario->group_tag = SCENARIO_GROUP;
	scenario->parent_group_tags[0] = 0xffffffff;
	scenario->parent_group_tags[1] = 0xffffffff;
	return TRUE;
}

/* a protected map's tag table repaired (OpenCE's 29426d53c, for its own
loader): see above. Each step reads only what is in the image (or in a BSP's
bytes, read whole), so a map that is merely damaged is left as it is, for
the checks to refuse (ce_map_checks.c). It is the same, step for step, when
the map is checked and when it loads, on the same bytes */
void ce_repairs_protection(
	struct ce_image const *image,
	ce_map_read_proc read_proc,
	void *context,
	unsigned long file_size)
{
	struct ce_tag_instance *instances;
	unsigned long instances_address, scenario_tag_index;
	byte *states = NULL;
	unsigned long *named_groups = NULL;
	long tag_count, index;
	long names = 0, scenario_group = 0, bsp_groups = 0, unknown = 0, named = 0, named_twice = 0;
	long empty = 0, made_none = 0;

	if (image->size < TAG_HEADER_SIZE)
		return;
	instances_address = ce_read_long(image->data + TAG_HEADER_INSTANCES_OFFSET);
	scenario_tag_index = ce_read_long(image->data + TAG_HEADER_SCENARIO_OFFSET);
	tag_count = (long)ce_read_long(image->data + TAG_HEADER_TAG_COUNT_OFFSET);
	/* (as ce_map_checks.c checks them: else it refuses the map) */
	if (tag_count <= 0 || tag_count > 0xffff || instances_address % 4)
		return;
	instances = ce_image_pointer(image, instances_address, (unsigned long)tag_count * CE_TAG_INSTANCE_SIZE);
	if (!instances)
		return;
	names = ce_protection_names(image, instances_address, instances, tag_count);
	/* (the scenario's, then its BSPs', which it names: a BSP has no data, so
	that it is not taken for a garbage tag below) */
	scenario_group = ce_repairs_scenario_group(instances, tag_count, scenario_tag_index) ? 1 : 0;
	bsp_groups = ce_protection_bsp_groups(image, instances, tag_count, scenario_tag_index);
	states = malloc((size_t)tag_count);
	named_groups = malloc((size_t)tag_count * sizeof(*named_groups));
	if (!states || !named_groups)
		goto done;
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = &instances[index];

		states[index] = _protection_tag_kept;
		named_groups[index] = NO_GROUP;
		/* (a tag held by a resource map is found there by its group and
		name, and a BSP has no data until it loads) */
		if (instance->indexed || instance->group_tag == STRUCTURE_BSP_GROUP)
			continue;
		if (!instance->base_address)
		{
			/* (no group, so that nothing reads it as a tag of one: tag_get
			gives the empty tag for it, cache_files.c) */
			states[index] = _protection_tag_empty;
			ce_group_give(instance, NO_GROUP);
			empty++;
		}
		else if (ce_group_find(instance->group_tag) == NONE)
		{
			states[index] = _protection_tag_unknown;
			unknown++;
		}
	}
	if (!unknown && !empty)
		goto done;
	/* (the references the structure BSPs hold, to their shaders, lightmaps
	and the like, which nothing else may name; read whole, as they are when
	they load, but only to be read) */
	if (unknown && read_proc)
	{
		struct ce_tag_instance *scenario = ce_instance_by_index(instances, tag_count, scenario_tag_index);
		byte *data = scenario && scenario->tag_index == scenario_tag_index ?
			ce_image_pointer(image, scenario->base_address, SCENARIO_BSPS_OFFSET + 0xc) : NULL;
		byte *bsps = NULL;
		long count = 0;

		if (data)
			bsps = ce_block(image, data + SCENARIO_BSPS_OFFSET, SCENARIO_BSP_SIZE, &count);
		for (index = 0; index < count && index < MAXIMUM_BSPS; index++)
		{
			unsigned long offset = ce_read_long(bsps + index * SCENARIO_BSP_SIZE + SCENARIO_BSP_FILE_OFFSET_OFFSET);
			unsigned long size = ce_read_long(bsps + index * SCENARIO_BSP_SIZE + SCENARIO_BSP_FILE_SIZE_OFFSET);
			byte *bsp;

			/* (no larger than the tag cache it loads into: ce_map_checks.c) */
			if (!size || size > CE_IMAGE_TAG_CACHE_SIZE || !ce_range_within(offset, size, file_size))
				continue;
			bsp = malloc(size);
			if (!bsp)
				continue;
			if (read_proc(context, offset, bsp, size))
			{
				ce_protection_references(bsp, size, instances, tag_count, states, named_groups, FALSE,
					&made_none);
			}
			free(bsp);
		}
	}
	/* (the tags: not their header nor their instances, both in the image,
	the instances usually right after the header) */
	{
		unsigned long instances_offset = instances_address - image->base;
		unsigned long instances_end = instances_offset + (unsigned long)tag_count * CE_TAG_INSTANCE_SIZE;
		unsigned long after = MAX(instances_end, (unsigned long)TAG_HEADER_SIZE);

		if (instances_offset > TAG_HEADER_SIZE)
		{
			ce_protection_references(image->data + TAG_HEADER_SIZE, instances_offset - TAG_HEADER_SIZE, instances,
				tag_count, states, named_groups, TRUE, &made_none);
		}
		ce_protection_references(image->data + after, image->size - after, instances, tag_count, states,
			named_groups, TRUE, &made_none);
	}
	for (index = 0; index < tag_count; index++)
	{
		if (states[index] == _protection_tag_named)
		{
			ce_group_give(&instances[index], named_groups[index]);
			named++;
		}
		else if (states[index] == _protection_tag_named_twice)
			named_twice++;
	}

done:
	if (states)
		free(states);
	if (named_groups)
		free(named_groups);
	if (!ce_map_checking() && (names | scenario_group | bsp_groups | unknown | empty))
	{
		error(_error_silent, "%s map: map protection repaired: %ld tags' names emptied, %ld scenario and %ld "
			"structure BSPs given their group, %ld of %ld tags of no group given the one the references to them "
			"name (%ld named as two groups, left as they were), %ld tags without data left with no group (%ld "
			"references to them made none)", ce_map_family_name(), names, scenario_group, bsp_groups, named,
			unknown, named_twice, empty, made_none);
	}
}

/* the map's tags repaired, in the image they are checked or loaded in (its
resource maps' tags already copied in: ce_resources.c) */
void ce_repairs_apply(
	struct ce_image const *image,
	void *tag_instances,
	long tag_count,
	unsigned long scenario_tag_index)
{
	struct ce_repair_counts counts;
	long index;

	memset(&counts, 0, sizeof(counts));
	/* (first: the repairs below find objects and shaders by their parents) */
	for (index = 0; index < tag_count; index++)
		ce_parent_groups_repair(ce_instance(tag_instances, index), &counts);
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = ce_instance(tag_instances, index);
		byte *data;

		if (ce_is_of_group(instance, 'obje'))
		{
			ce_object_type_repair(image, instance, &counts);
			ce_modifier_shader_repair(image, instance, tag_instances, tag_count, &counts);
			data = ce_image_pointer(image, instance->base_address, OBJECT_PREDICTED_RESOURCES_OFFSET + 0xc);
			if (data)
				ce_predicted_resources_repair(image, data + OBJECT_PREDICTED_RESOURCES_OFFSET, tag_instances, tag_count,
					&counts);
			data = instance->group_tag == 'weap' ?
				ce_image_pointer(image, instance->base_address, WEAPON_PREDICTED_RESOURCES_OFFSET + 0xc) : NULL;
			if (data)
				ce_predicted_resources_repair(image, data + WEAPON_PREDICTED_RESOURCES_OFFSET, tag_instances, tag_count,
					&counts);
		}
		else if (instance->group_tag == 'mod2')
		{
			ce_model_shaders_repair(image, instance, tag_instances, tag_count, &counts);
			data = ce_image_pointer(image, instance->base_address, MODEL_NODES_OFFSET + 0xc);
			if (data)
				ce_node_links_repair(image, data + MODEL_NODES_OFFSET, MODEL_NODE_SIZE, &counts);
		}
		else if (instance->group_tag == 'bitm')
			ce_bitmaps_repair(image, instance, &counts);
		else if (instance->group_tag == 'effe')
			ce_effect_flags_repair(image, instance, &counts);
		else if (instance->group_tag == 'antr')
		{
			data = ce_image_pointer(image, instance->base_address, ANIMATION_GRAPH_NODES_OFFSET + 0xc);
			if (data)
				ce_node_links_repair(image, data + ANIMATION_GRAPH_NODES_OFFSET, ANIMATION_GRAPH_NODE_SIZE, &counts);
		}
	}
	{
		struct ce_tag_instance *scenario = ce_instance_by_index(tag_instances, tag_count, scenario_tag_index);
		byte *data = scenario && scenario->group_tag == SCENARIO_GROUP ?
			ce_image_pointer(image, scenario->base_address, SCENARIO_PREDICTED_RESOURCES_OFFSET + 0xc) : NULL;

		if (data)
			ce_predicted_resources_repair(image, data + SCENARIO_PREDICTED_RESOURCES_OFFSET, tag_instances, tag_count,
				&counts);
	}
	ce_repairs_log(&counts);
}

/* whether a bitmap (bitmap_group.h's bitmap_data, in the loaded map's
tags) is one of MCC's BC7 bitmaps, made DXT5's format: its blocks are BC7's
(xbox_texture_cache.c) */
boolean ce_bitmap_is_bc7(
	void const *bitmap)
{
	short format, mark;

	memcpy(&format, (byte const *)bitmap + BITMAP_DATA_FORMAT_OFFSET, sizeof(format));
	memcpy(&mark, (byte const *)bitmap + BITMAP_DATA_MIPMAP_PAD_OFFSET, sizeof(mark));
	return format == BITMAP_FORMAT_DXT5 && mark == BITMAP_PORT_BC7_MARK;
}

/* the map's tags loaded (cache_files.c), its resource maps' tags copied in:
repaired in place, in its tag cache */
void ce_repairs_tags_loaded(
	void *tag_instances,
	long tag_count,
	unsigned long scenario_tag_index)
{
	struct ce_image image;

	image.data = xbox_pointer(CE_IMAGE_TAG_CACHE_BASE);
	image.base = CE_IMAGE_TAG_CACHE_BASE;
	image.size = CE_IMAGE_TAG_CACHE_SIZE;
	ce_loaded_instances = tag_instances;
	ce_loaded_tag_count = tag_count;
	if (ce_repairs_scenario_group(tag_instances, tag_count, scenario_tag_index))
		error(_error_silent, "Custom Edition map: its scenario given the scenario's group");
	ce_repairs_apply(&image, tag_instances, tag_count, scenario_tag_index);
}

/* one of its structure BSPs loaded (cache_files.c): its clusters'
predicted resources repaired */
void ce_repairs_bsp_loaded(
	struct structure_bsp *structure)
{
	struct ce_image image;
	struct ce_repair_counts counts;
	long index;

	if (!ce_loaded_instances)
		return;
	image.data = xbox_pointer(CE_IMAGE_TAG_CACHE_BASE);
	image.base = CE_IMAGE_TAG_CACHE_BASE;
	image.size = CE_IMAGE_TAG_CACHE_SIZE;
	memset(&counts, 0, sizeof(counts));
	for (index = 0; index < structure->clusters.count; index++)
	{
		struct structure_cluster *cluster = TAG_BLOCK_GET_ELEMENT(&structure->clusters, index, struct structure_cluster);

		ce_predicted_resources_repair(&image, (byte *)&cluster->predicted_resources, ce_loaded_instances,
			ce_loaded_tag_count, &counts);
	}
	ce_repairs_log(&counts);
}

#endif
