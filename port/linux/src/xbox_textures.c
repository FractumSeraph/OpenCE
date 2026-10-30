/*
XBOX_TEXTURES.C

Xbox texture decoding and the OpenGL texture cache.

An Xbox texture is a Direct3D header - Common, Data (physical address),
Lock, Format and Size - over texels in guest memory. Power-of-two textures
are swizzled (Morton order, one level after another); textures with a Size
field are linear, with a pitch, and are addressed with texel coordinates.
DXT textures are stored as plain 4x4 blocks. Everything except DXT is
converted to 32-bit BGRA on upload (BC7, a Custom Edition map's, decoded).

A cached texture stays valid until any page it was read from is written;
memory_watch.c detects that by write-protecting the pages.
*/

#include "xgpu.h"
#include "hud_hires.h"
#include "menu_files.h"
#include "text_hires.h"
#include "port_config.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#ifdef HALO_GLES
#define GL_BGRA GL_RGBA
#endif
#include <stdlib.h>
#include <string.h>
/* (OpenCE's Android Custom Edition textures, HALO_ANDROID_CE_TEXTURES: the
32-bit Android guest's alone in this fork, android_ce_texture_cache.h) */
#include "../game/android_ce_texture_cache.h"
/* (DXT1 as ASTC 4x4, display.dxt1_astc: the Android app's, not the browser
build's, whose WebGL is asked for no KHR ASTC extension and which puts the
channels in order on the CPU, WebGL 2 having no texture swizzle; and an
ES build's alone: the 64-bit Android build draws without HALO_GLES) */
#if defined(HALO_ANDROID) && defined(HALO_GLES) && !defined(HALO_BROWSER)
#define XGPU_DXT1_ASTC 1
#endif
#ifdef HALO_ANDROID
#include "../../android/include/android_render_policy.h"
#endif

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83f1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83f2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83f3
#endif

/* ---------- formats */

enum texel_kind
{
	_texel_unknown,
	_texel_a8r8g8b8, _texel_x8r8g8b8, _texel_r5g6b5, _texel_a1r5g5b5, _texel_x1r5g5b5, _texel_a4r4g4b4,
	_texel_l8, _texel_al8, _texel_a8, _texel_a8l8, _texel_p8, _texel_g8b8, _texel_r8b8, _texel_r6g5b5,
	_texel_l16, _texel_v16u16, _texel_a8b8g8r8, _texel_b8g8r8a8, _texel_r8g8b8a8, _texel_r5g5b5a1,
	_texel_r4g4b4a4, _texel_yuy2, _texel_uyvy, _texel_d24s8, _texel_d16,
	_texel_dxt1, _texel_dxt3, _texel_dxt5, _texel_bc7,
};

struct format_information
{
	unsigned char kind;
	unsigned char bytes; /* per texel; per 4x4 block for DXT and BC7 */
	unsigned char linear;
};

static struct format_information format_information(DWORD format)
{
	static const struct format_information table[0x43] =
	{
		[0x00] = { _texel_l8, 1, 0 },
		[0x01] = { _texel_al8, 1, 0 },
		[0x02] = { _texel_a1r5g5b5, 2, 0 },
		[0x03] = { _texel_x1r5g5b5, 2, 0 },
		[0x04] = { _texel_a4r4g4b4, 2, 0 },
		[0x05] = { _texel_r5g6b5, 2, 0 },
		[0x06] = { _texel_a8r8g8b8, 4, 0 },
		[0x07] = { _texel_x8r8g8b8, 4, 0 },
		[0x0b] = { _texel_p8, 1, 0 },
		[0x0c] = { _texel_dxt1, 8, 0 },
		[0x0e] = { _texel_dxt3, 16, 0 },
		[0x0f] = { _texel_dxt5, 16, 0 },
		[0x10] = { _texel_a1r5g5b5, 2, 1 },
		[0x11] = { _texel_r5g6b5, 2, 1 },
		[0x12] = { _texel_a8r8g8b8, 4, 1 },
		[0x13] = { _texel_l8, 1, 1 },
		[0x16] = { _texel_r8b8, 2, 1 },
		[0x17] = { _texel_g8b8, 2, 1 },
		[0x19] = { _texel_a8, 1, 0 },
		[0x1a] = { _texel_a8l8, 2, 0 },
		[0x1b] = { _texel_al8, 1, 1 },
		[0x1c] = { _texel_x1r5g5b5, 2, 1 },
		[0x1d] = { _texel_a4r4g4b4, 2, 1 },
		[0x1e] = { _texel_x8r8g8b8, 4, 1 },
		[0x1f] = { _texel_a8, 1, 1 },
		[0x20] = { _texel_a8l8, 2, 1 },
		[0x24] = { _texel_yuy2, 2, 1 },
		[0x25] = { _texel_uyvy, 2, 1 },
		[0x27] = { _texel_r6g5b5, 2, 0 },
		[0x28] = { _texel_g8b8, 2, 0 },
		[0x29] = { _texel_r8b8, 2, 0 },
		[0x2a] = { _texel_d24s8, 4, 0 },
		[0x2b] = { _texel_d24s8, 4, 0 },
		[0x2c] = { _texel_d16, 2, 0 },
		[0x2d] = { _texel_d16, 2, 0 },
		[0x2e] = { _texel_d24s8, 4, 1 },
		[0x2f] = { _texel_d24s8, 4, 1 },
		[0x30] = { _texel_d16, 2, 1 },
		[0x31] = { _texel_d16, 2, 1 },
		[0x32] = { _texel_l16, 2, 0 },
		[0x33] = { _texel_v16u16, 4, 0 },
		[0x35] = { _texel_l16, 2, 1 },
		[0x36] = { _texel_v16u16, 4, 1 },
		[0x37] = { _texel_r6g5b5, 2, 1 },
		[0x38] = { _texel_r5g5b5a1, 2, 0 },
		[0x39] = { _texel_r4g4b4a4, 2, 0 },
		[0x3a] = { _texel_a8b8g8r8, 4, 0 },
		[0x3b] = { _texel_b8g8r8a8, 4, 0 },
		[0x3c] = { _texel_r8g8b8a8, 4, 0 },
		[0x3d] = { _texel_r5g5b5a1, 2, 1 },
		[0x3e] = { _texel_r4g4b4a4, 2, 1 },
		[0x3f] = { _texel_a8b8g8r8, 4, 1 },
		[0x40] = { _texel_b8g8r8a8, 4, 1 },
		[0x41] = { _texel_r8g8b8a8, 4, 1 },
		[D3DFMT_PORT_BC7] = { _texel_bc7, 16, 0 },
	};
	struct format_information unknown = { _texel_a8r8g8b8, 4, 0 };

	if (format < sizeof(table) / sizeof(table[0]) && table[format].kind != _texel_unknown)
		return table[format];
	return unknown;
}

static BOOL kind_compressed(unsigned char kind)
{
	return kind == _texel_dxt1 || kind == _texel_dxt3 || kind == _texel_dxt5 || kind == _texel_bc7;
}

/* ---------- geometry of a texture in memory */

static unsigned long floor_log2(unsigned long value)
{
	unsigned long result = 0;

	while (value > 1)
	{
		value >>= 1;
		result++;
	}
	return result;
}

static unsigned long level_dimension(unsigned long base, unsigned long level)
{
	unsigned long value = base >> level;

	return value ? value : 1;
}

void xgpu_texture_describe(DWORD format_word, DWORD size_word, struct xgpu_texture_description *description)
{
	struct format_information information;

	memset(description, 0, sizeof(*description));
	description->format = (format_word & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT;
	information = format_information(description->format);
	description->cube_map = (format_word & D3DFORMAT_CUBEMAP) != 0;
	description->compressed = kind_compressed(information.kind);
	if (size_word)
	{
		description->width = (size_word & D3DSIZE_WIDTH_MASK) + 1;
		description->height = ((size_word & D3DSIZE_HEIGHT_MASK) >> D3DSIZE_HEIGHT_SHIFT) + 1;
		description->depth = 1;
		description->levels = 1;
		description->pitch = (((size_word & D3DSIZE_PITCH_MASK) >> D3DSIZE_PITCH_SHIFT) + 1) * D3DTEXTURE_PITCH_ALIGNMENT;
		description->linear = TRUE;
	}
	else
	{
		description->width = 1UL << ((format_word & D3DFORMAT_USIZE_MASK) >> D3DFORMAT_USIZE_SHIFT);
		description->height = 1UL << ((format_word & D3DFORMAT_VSIZE_MASK) >> D3DFORMAT_VSIZE_SHIFT);
		description->depth = 1UL << ((format_word & D3DFORMAT_PSIZE_MASK) >> D3DFORMAT_PSIZE_SHIFT);
		description->levels = (format_word & D3DFORMAT_MIPMAP_MASK) >> D3DFORMAT_MIPMAP_SHIFT;
		if (!description->levels)
			description->levels = 1;
		description->linear = information.linear;
		description->pitch = description->width * information.bytes;
	}
	if ((format_word & D3DFORMAT_DIMENSION_MASK) >> D3DFORMAT_DIMENSION_SHIFT != 3)
		description->depth = 1;
}

static unsigned long level_bytes(const struct xgpu_texture_description *description, unsigned long level)
{
	struct format_information information = format_information(description->format);
	unsigned long width = level_dimension(description->width, level);
	unsigned long height = level_dimension(description->height, level);
	unsigned long depth = level_dimension(description->depth, level);

	if (description->compressed)
		return ((width + 3) / 4) * ((height + 3) / 4) * information.bytes * depth;
	if (description->linear)
		return description->pitch * height;
	return width * height * depth * information.bytes;
}

unsigned long xgpu_texture_level_offset(const struct xgpu_texture_description *description, unsigned long level)
{
	unsigned long offset = 0;
	unsigned long index;

	for (index = 0; index < level && index < description->levels; index++)
		offset += level_bytes(description, index);
	return offset;
}

unsigned long xgpu_texture_face_size(const struct xgpu_texture_description *description)
{
	unsigned long size = xgpu_texture_level_offset(description, description->levels);

	if (description->cube_map)
		size = (size + D3DTEXTURE_CUBEFACE_ALIGNMENT - 1) & ~(unsigned long)(D3DTEXTURE_CUBEFACE_ALIGNMENT - 1);
	return size;
}

/* whether the size is one D3DDevice_GetDeviceCaps allows (d3d8_gl.c): up
to 4096 by 4096, and 512 each way for a volume */
static BOOL texture_size_supported(const struct xgpu_texture_description *description)
{
	if (description->depth > 1)
		return description->width <= 512 && description->height <= 512 && description->depth <= 512;
	return description->width <= 4096 && description->height <= 4096;
}

unsigned long xgpu_texture_level_pitch(const struct xgpu_texture_description *description, unsigned long level)
{
	struct format_information information = format_information(description->format);

	if (description->linear)
		return description->pitch;
	if (description->compressed)
		return ((level_dimension(description->width, level) + 3) / 4) * information.bytes;
	return level_dimension(description->width, level) * information.bytes;
}

/* ---------- swizzling */

struct swizzle_masks
{
	unsigned long x, y, z;
};

static struct swizzle_masks swizzle_masks(unsigned long width, unsigned long height, unsigned long depth)
{
	struct swizzle_masks masks = { 0, 0, 0 };
	unsigned long bit = 1, mask_bit = 1;
	BOOL done;

	/* bits of x, y and z alternate until each dimension runs out */
	do
	{
		done = TRUE;
		if (bit < width)
		{
			masks.x |= mask_bit;
			mask_bit <<= 1;
			done = FALSE;
		}
		if (bit < height)
		{
			masks.y |= mask_bit;
			mask_bit <<= 1;
			done = FALSE;
		}
		if (bit < depth)
		{
			masks.z |= mask_bit;
			mask_bit <<= 1;
			done = FALSE;
		}
		bit <<= 1;
	} while (!done);
	return masks;
}

static unsigned long spread(unsigned long mask, unsigned long value)
{
	unsigned long result = 0, bit = 1;

	while (value && bit)
	{
		if (mask & bit)
		{
			if (value & 1)
				result |= bit;
			value >>= 1;
		}
		bit <<= 1;
	}
	return result;
}

/* ---------- texel conversion */

static unsigned long expand5(unsigned long v) { return (v << 3) | (v >> 2); }
static unsigned long expand6(unsigned long v) { return (v << 2) | (v >> 4); }
static unsigned long expand4(unsigned long v) { return v * 0x11; }

static unsigned long argb(unsigned long a, unsigned long r, unsigned long g, unsigned long b)
{
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static unsigned char clamp_byte(long value)
{
	return (unsigned char)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static unsigned long yuv_to_argb(long y, long u, long v)
{
	long c = y - 16, d = u - 128, e = v - 128;

	return argb(255, clamp_byte((298 * c + 409 * e + 128) >> 8),
		clamp_byte((298 * c - 100 * d - 208 * e + 128) >> 8),
		clamp_byte((298 * c + 516 * d + 128) >> 8));
}

/* a texel's 16 and 32 bits, read only for the kinds that have them (the last
texel of a 1-byte texture read 3 bytes past it) */
#define TEXEL16(source) ((unsigned long)(source)[0] | ((unsigned long)(source)[1] << 8))
#define TEXEL32(source) (TEXEL16(source) | ((unsigned long)(source)[2] << 16) | ((unsigned long)(source)[3] << 24))

static unsigned long convert_texel(unsigned char kind, const unsigned char *source, const D3DCOLOR *palette,
	unsigned long x, const unsigned char *row)
{
	switch (kind)
	{
	case _texel_a8r8g8b8: return TEXEL32(source);
	case _texel_x8r8g8b8: return TEXEL32(source) | 0xff000000UL;
	case _texel_r5g6b5: return argb(255, expand5(TEXEL16(source) >> 11), expand6((TEXEL16(source) >> 5) & 0x3f), expand5(TEXEL16(source) & 0x1f));
	case _texel_a1r5g5b5: return argb((TEXEL16(source) & 0x8000) ? 255 : 0, expand5((TEXEL16(source) >> 10) & 0x1f), expand5((TEXEL16(source) >> 5) & 0x1f), expand5(TEXEL16(source) & 0x1f));
	case _texel_x1r5g5b5: return argb(255, expand5((TEXEL16(source) >> 10) & 0x1f), expand5((TEXEL16(source) >> 5) & 0x1f), expand5(TEXEL16(source) & 0x1f));
	case _texel_a4r4g4b4: return argb(expand4(TEXEL16(source) >> 12), expand4((TEXEL16(source) >> 8) & 0xf), expand4((TEXEL16(source) >> 4) & 0xf), expand4(TEXEL16(source) & 0xf));
	case _texel_l8: return argb(255, source[0], source[0], source[0]);
	case _texel_al8: return argb(source[0], source[0], source[0], source[0]);
	case _texel_a8: return argb(source[0], 255, 255, 255);
	case _texel_a8l8: return argb(source[1], source[0], source[0], source[0]);
	case _texel_p8: return palette ? palette[source[0]] : argb(255, source[0], source[0], source[0]);
	/* V8U8 shares this format: U (the low byte) reads as red, V as green */
	case _texel_g8b8: return argb(255, source[0], source[1], 0);
	case _texel_r8b8: return argb(255, source[1], 0, source[0]);
	case _texel_r6g5b5: return argb(255, expand6(TEXEL16(source) >> 10), expand5((TEXEL16(source) >> 5) & 0x1f), expand5(TEXEL16(source) & 0x1f));
	case _texel_l16: return argb(255, source[1], source[1], source[1]);
	case _texel_v16u16: return argb(255, source[1], source[3], 0);
	case _texel_a8b8g8r8: return argb(source[3], source[0], source[1], source[2]);
	case _texel_b8g8r8a8: return argb(source[0], source[1], source[2], source[3]);
	case _texel_r8g8b8a8: return argb(source[0], source[3], source[2], source[1]);
	case _texel_r5g5b5a1: return argb((TEXEL16(source) & 1) ? 255 : 0, expand5(TEXEL16(source) >> 11), expand5((TEXEL16(source) >> 6) & 0x1f), expand5((TEXEL16(source) >> 1) & 0x1f));
	case _texel_r4g4b4a4: return argb(expand4(TEXEL16(source) & 0xf), expand4(TEXEL16(source) >> 12), expand4((TEXEL16(source) >> 8) & 0xf), expand4((TEXEL16(source) >> 4) & 0xf));
	case _texel_yuy2:
	{
		const unsigned char *pair = row + (x & ~1UL) * 2;

		return yuv_to_argb(pair[(x & 1) ? 2 : 0], pair[1], pair[3]);
	}
	case _texel_uyvy:
	{
		const unsigned char *pair = row + (x & ~1UL) * 2;

		return yuv_to_argb(pair[(x & 1) ? 3 : 1], pair[0], pair[2]);
	}
	case _texel_d24s8: return argb(255, source[3], source[3], source[3]);
	case _texel_d16: return argb(255, source[1], source[1], source[1]);
	default: return TEXEL32(source);
	}
}

/* one level (or 3D slice set) of an uncompressed texture into BGRA; FALSE
when out of memory */
static BOOL decode_level(const struct xgpu_texture_description *description, unsigned long level,
	const unsigned char *source, const D3DCOLOR *palette, unsigned long *destination)
{
	struct format_information information = format_information(description->format);
	unsigned long width = level_dimension(description->width, level);
	unsigned long height = level_dimension(description->height, level);
	unsigned long depth = level_dimension(description->depth, level);
	unsigned long x, y, z;

	if (description->linear || description->pc_layout)
	{
		/* (a linear texture's rows are its pitch apart; Halo PC's, a level's
		width, a linear one's too) */
		unsigned long pitch = description->linear && !description->pc_layout ? description->pitch :
			width * information.bytes;
		/* only the texels a row's pitch holds: a Size word whose pitch is
		narrower than its width (a map's bitmap) read past the texture's
		pitch * height bytes; the rest of such a row is black (a YUV texel
		reads its pair's four bytes) */
		unsigned long row_texels = information.bytes ? pitch / information.bytes : 0;

		if (information.kind == _texel_yuy2 || information.kind == _texel_uyvy)
			row_texels &= ~1UL;
		for (z = 0; z < depth; z++)
		for (y = 0; y < height; y++)
		{
			const unsigned char *row = source + (z * height + y) * pitch;

			for (x = 0; x < width; x++)
				destination[(z * height + y) * width + x] = x < row_texels ?
					convert_texel(information.kind, row + x * information.bytes, palette, x, row) : 0;
		}
		return TRUE;
	}
	{
		struct swizzle_masks masks = swizzle_masks(width, height, depth);
		unsigned long *x_offsets = malloc(width * sizeof(unsigned long));

		if (!x_offsets)
			return FALSE;
		for (x = 0; x < width; x++)
			x_offsets[x] = spread(masks.x, x);
		for (z = 0; z < depth; z++)
		{
			unsigned long z_offset = spread(masks.z, z);

			for (y = 0; y < height; y++)
			{
				unsigned long y_offset = spread(masks.y, y) | z_offset;

				for (x = 0; x < width; x++)
				{
					const unsigned char *texel = source + (x_offsets[x] | y_offset) * information.bytes;

					destination[(z * height + y) * width + x] = convert_texel(information.kind, texel, palette, x, texel);
				}
			}
		}
		free(x_offsets);
	}
	return TRUE;
}

/* ---------- BC7 decoding, for Custom Edition maps' bitmaps in MCC's
high-quality compression (D3DFMT_PORT_BC7), decoded on every driver: the
format, BPTC, is not one every GL the port runs on has (ES 3.0, GL 3.3) */

/* each mode's subsets, partition bits, rotation bits, index selection bits,
color and alpha bits, endpoint and shared p-bits, and index bits (primary,
secondary) */
static const struct
{
	unsigned char subsets, partition_bits, rotation_bits, selection_bits, color_bits, alpha_bits,
		endpoint_pbits, shared_pbits, index_bits, secondary_index_bits;
} bc7_modes[8] =
{
	{ 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
	{ 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
	{ 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
	{ 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
	{ 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
	{ 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
	{ 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
	{ 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 },
};

/* the two-subset partitions, a bit a texel set for those of the second */
static const unsigned short bc7_partitions2[64] =
{
	0xcccc, 0x8888, 0xeeee, 0xecc8, 0xc880, 0xfeec, 0xfec8, 0xec80,
	0xc800, 0xffec, 0xfe80, 0xe800, 0xffe8, 0xff00, 0xfff0, 0xf000,
	0xf710, 0x008e, 0x7100, 0x08ce, 0x008c, 0x7310, 0x3100, 0x8cce,
	0x088c, 0x3110, 0x6666, 0x366c, 0x17e8, 0x0ff0, 0x718e, 0x399c,
	0xaaaa, 0xf0f0, 0x5a5a, 0x33cc, 0x3c3c, 0x55aa, 0x9696, 0xa55a,
	0x73ce, 0x13c8, 0x324c, 0x3bdc, 0x6996, 0xc33c, 0x9966, 0x0660,
	0x0272, 0x04e4, 0x4e40, 0x2720, 0xc936, 0x936c, 0x39c6, 0x639c,
	0x9336, 0x9cc6, 0x817e, 0xe718, 0xccf0, 0x0fcc, 0x7744, 0xee22,
};

/* the three-subset partitions, two bits a texel (the first texel lowest) */
static const unsigned long bc7_partitions3[64] =
{
	0xaa685050, 0x6a5a5040, 0x5a5a4200, 0x5450a0a8, 0xa5a50000, 0xa0a05050, 0x5555a0a0, 0x5a5a5050,
	0xaa550000, 0xaa555500, 0xaaaa5500, 0x90909090, 0x94949494, 0xa4a4a4a4, 0xa9a59450, 0x2a0a4250,
	0xa5945040, 0x0a425054, 0xa5a5a500, 0x55a0a0a0, 0xa8a85454, 0x6a6a4040, 0xa4a45000, 0x1a1a0500,
	0x0050a4a4, 0xaaa59090, 0x14696914, 0x69691400, 0xa08585a0, 0xaa821414, 0x50a4a450, 0x6a5a0200,
	0xa9a58000, 0x5090a0a8, 0xa8a09050, 0x24242424, 0x00aa5500, 0x24924924, 0x24499224, 0x50a50a50,
	0x500aa550, 0xaaaa4444, 0x66660000, 0xa5a0a5a0, 0x50a050a0, 0x69286928, 0x44aaaa44, 0x66666600,
	0xaa444444, 0x54a854a8, 0x95809580, 0x96969600, 0xa85454a8, 0x80959580, 0xaa141414, 0x96960000,
	0xaaaa1414, 0xa05050a0, 0xa0a5a5a0, 0x96000000, 0x40804080, 0xa9a8a9a8, 0xaaaaaa44, 0x2a4a5254,
};

/* the texel of each subset past the first whose index is a bit short */
static const unsigned char bc7_anchors2[64] =
{
	15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
	15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2,
	15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6,
	6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15,
};
static const unsigned char bc7_anchors3[2][64] =
{
	{
		3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3,
		3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15,
		8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15,
		3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3, 3,
	},
	{
		15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8,
		15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8,
		15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8,
		15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8,
	},
};

static const unsigned char bc7_weights2[4] = { 0, 21, 43, 64 };
static const unsigned char bc7_weights3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
static const unsigned char bc7_weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

/* the next `count` bits of a block, its lowest first */
static unsigned long bc7_bits(const unsigned char *block, unsigned long *position, unsigned long count)
{
	unsigned long value = 0, bit;

	for (bit = 0; bit < count; bit++, (*position)++)
		value |= (unsigned long)((block[*position >> 3] >> (*position & 7)) & 1) << bit;
	return value;
}

static unsigned long bc7_interpolate(unsigned long e0, unsigned long e1, unsigned long index, unsigned long bits)
{
	unsigned long weight = bits == 2 ? bc7_weights2[index] : bits == 3 ? bc7_weights3[index] : bc7_weights4[index];

	return (e0 * (64 - weight) + e1 * weight + 32) >> 6;
}

/* one 4x4 block's texels, as 32-bit ARGB words (a reserved mode's are
transparent black) */
static void bc7_decode_block(const unsigned char *block, unsigned long texels[16])
{
	unsigned long position = 0, mode, partition = 0, rotation = 0, selection = 0;
	unsigned long subsets, channel, endpoint, texel;
	unsigned long endpoints[6][4];
	unsigned long subset_of[16];
	unsigned long indices[16], secondary_indices[16];

	for (mode = 0; mode < 8 && !(block[0] & (1 << mode)); mode++)
		;
	if (mode == 8)
	{
		for (texel = 0; texel < 16; texel++)
			texels[texel] = 0;
		return;
	}
	position = mode + 1;
	subsets = bc7_modes[mode].subsets;
	partition = bc7_bits(block, &position, bc7_modes[mode].partition_bits);
	rotation = bc7_bits(block, &position, bc7_modes[mode].rotation_bits);
	selection = bc7_bits(block, &position, bc7_modes[mode].selection_bits);
	/* (the endpoints' red, then green, blue and alpha) */
	for (channel = 0; channel < 4; channel++)
	{
		unsigned long bits = channel < 3 ? bc7_modes[mode].color_bits : bc7_modes[mode].alpha_bits;

		for (endpoint = 0; endpoint < subsets * 2; endpoint++)
			endpoints[endpoint][channel] = bits ? bc7_bits(block, &position, bits) : 255;
	}
	/* (each endpoint's p-bit, or each subset's shared one, below its
	channels' bits; then each channel made 8 bits) */
	for (endpoint = 0; endpoint < subsets * 2; endpoint++)
	{
		unsigned long pbit = 0;
		BOOL has_pbit = bc7_modes[mode].endpoint_pbits || bc7_modes[mode].shared_pbits;

		if (bc7_modes[mode].endpoint_pbits)
			pbit = bc7_bits(block, &position, 1);
		else if (bc7_modes[mode].shared_pbits && !(endpoint & 1))
			pbit = bc7_bits(block, &position, 1);
		else if (bc7_modes[mode].shared_pbits)
			pbit = (block[(position - 1) >> 3] >> ((position - 1) & 7)) & 1;
		for (channel = 0; channel < 4; channel++)
		{
			unsigned long bits = channel < 3 ? bc7_modes[mode].color_bits : bc7_modes[mode].alpha_bits;
			unsigned long value = endpoints[endpoint][channel];

			if (!bits)
				continue;
			if (has_pbit)
			{
				value = (value << 1) | pbit;
				bits++;
			}
			value <<= 8 - bits;
			endpoints[endpoint][channel] = value | (value >> bits);
		}
	}
	for (texel = 0; texel < 16; texel++)
	{
		if (subsets == 2)
			subset_of[texel] = (bc7_partitions2[partition] >> texel) & 1;
		else if (subsets == 3)
			subset_of[texel] = (bc7_partitions3[partition] >> (texel * 2)) & 3;
		else
			subset_of[texel] = 0;
	}
	/* (each subset's anchor texel's index a bit short, its top bit 0) */
	for (texel = 0; texel < 16; texel++)
	{
		BOOL anchor = texel == 0 ||
			(subsets == 2 && texel == bc7_anchors2[partition]) ||
			(subsets == 3 && (texel == bc7_anchors3[0][partition] || texel == bc7_anchors3[1][partition]));

		indices[texel] = bc7_bits(block, &position, bc7_modes[mode].index_bits - (anchor ? 1 : 0));
	}
	for (texel = 0; texel < 16; texel++)
	{
		secondary_indices[texel] = bc7_modes[mode].secondary_index_bits ?
			bc7_bits(block, &position, bc7_modes[mode].secondary_index_bits - (texel == 0 ? 1 : 0)) : 0;
	}
	for (texel = 0; texel < 16; texel++)
	{
		unsigned long *e0 = endpoints[subset_of[texel] * 2], *e1 = endpoints[subset_of[texel] * 2 + 1];
		unsigned long color_index = indices[texel], color_bits = bc7_modes[mode].index_bits;
		unsigned long alpha_index = indices[texel], alpha_bits = bc7_modes[mode].index_bits;
		unsigned long value[4], swap;

		if (bc7_modes[mode].secondary_index_bits)
		{
			if (selection)
			{
				color_index = secondary_indices[texel];
				color_bits = bc7_modes[mode].secondary_index_bits;
			}
			else
			{
				alpha_index = secondary_indices[texel];
				alpha_bits = bc7_modes[mode].secondary_index_bits;
			}
		}
		for (channel = 0; channel < 3; channel++)
			value[channel] = bc7_interpolate(e0[channel], e1[channel], color_index, color_bits);
		value[3] = bc7_modes[mode].alpha_bits ? bc7_interpolate(e0[3], e1[3], alpha_index, alpha_bits) : 255;
		/* (a rotation's channel swapped with alpha) */
		if (rotation)
		{
			swap = value[3];
			value[3] = value[rotation - 1];
			value[rotation - 1] = swap;
		}
		texels[texel] = argb(value[3], value[0], value[1], value[2]);
	}
}

static void bc7_decode_level(const unsigned char *source, unsigned long width, unsigned long height,
	unsigned long depth, unsigned long *destination)
{
	unsigned long blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
	unsigned long z, bx, by, x, y;

	for (z = 0; z < depth; z++)
	{
		for (by = 0; by < blocks_y; by++)
		{
			for (bx = 0; bx < blocks_x; bx++)
			{
				unsigned long texels[16];

				bc7_decode_block(source + ((z * blocks_y + by) * blocks_x + bx) * 16, texels);
				for (y = 0; y < 4; y++)
				{
					for (x = 0; x < 4; x++)
					{
						unsigned long px = bx * 4 + x, py = by * 4 + y;

						if (px < width && py < height)
							destination[(z * height + py) * width + px] = texels[y * 4 + x];
					}
				}
			}
		}
	}
}

#ifdef HALO_GLES
/* ---------- DXT decoding, for ES drivers without S3TC (Mali) */

static unsigned long color565(unsigned long value)
{
	return argb(255, expand5(value >> 11), expand6((value >> 5) & 0x3f), expand5(value & 0x1f));
}

static unsigned long mix(unsigned long a, unsigned long b, unsigned long weight_a, unsigned long weight_b,
	unsigned long divisor)
{
	unsigned long result = 0;
	int shift;

	for (shift = 0; shift < 24; shift += 8)
	{
		unsigned long channel = (((a >> shift) & 0xff) * weight_a + ((b >> shift) & 0xff) * weight_b) / divisor;

		result |= channel << shift;
	}
	return result | 0xff000000UL;
}

/* one 4x4 block's colors; dxt1 selects the punch-through alpha mode */
static void dxt_color_block(const unsigned char *block, BOOL dxt1, unsigned long colors[16])
{
	unsigned long c0 = block[0] | (block[1] << 8);
	unsigned long c1 = block[2] | (block[3] << 8);
	unsigned long palette[4];
	unsigned long bits = block[4] | (block[5] << 8) | ((unsigned long)block[6] << 16) | ((unsigned long)block[7] << 24);
	int index;

	palette[0] = color565(c0);
	palette[1] = color565(c1);
	if (c0 > c1 || !dxt1)
	{
		palette[2] = mix(palette[0], palette[1], 2, 1, 3);
		palette[3] = mix(palette[0], palette[1], 1, 2, 3);
	}
	else
	{
		palette[2] = mix(palette[0], palette[1], 1, 1, 2);
		palette[3] = 0;
	}
	for (index = 0; index < 16; index++)
		colors[index] = palette[(bits >> (index * 2)) & 3];
}

static void dxt_decode_level(unsigned char kind, const unsigned char *source, unsigned long width, unsigned long height,
	unsigned long depth, unsigned long *destination)
{
	unsigned long blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
	unsigned long block_bytes = kind == _texel_dxt1 ? 8 : 16;
	unsigned long z, bx, by, x, y;

	for (z = 0; z < depth; z++)
	{
		for (by = 0; by < blocks_y; by++)
		{
			for (bx = 0; bx < blocks_x; bx++)
			{
				const unsigned char *block = source + ((z * blocks_y + by) * blocks_x + bx) * block_bytes;
				unsigned long colors[16];
				unsigned long alpha[16];
				int index;

				if (kind == _texel_dxt1)
				{
					dxt_color_block(block, TRUE, colors);
					for (index = 0; index < 16; index++)
						alpha[index] = colors[index] >> 24;
				}
				else
				{
					dxt_color_block(block + 8, FALSE, colors);
					if (kind == _texel_dxt3)
					{
						for (index = 0; index < 16; index++)
							alpha[index] = expand4((block[index / 2] >> ((index & 1) * 4)) & 0xf);
					}
					else
					{
						unsigned long a0 = block[0], a1 = block[1], values[8];
						unsigned long long bits = 0;
						int bit;

						for (bit = 0; bit < 6; bit++)
							bits |= (unsigned long long)block[2 + bit] << (bit * 8);
						values[0] = a0;
						values[1] = a1;
						if (a0 > a1)
						{
							for (index = 2; index < 8; index++)
								values[index] = ((8 - index) * a0 + (index - 1) * a1) / 7;
						}
						else
						{
							for (index = 2; index < 6; index++)
								values[index] = ((6 - index) * a0 + (index - 1) * a1) / 5;
							values[6] = 0;
							values[7] = 255;
						}
						for (index = 0; index < 16; index++)
							alpha[index] = values[(bits >> (index * 3)) & 7];
					}
				}
				for (y = 0; y < 4; y++)
				{
					for (x = 0; x < 4; x++)
					{
						unsigned long px = bx * 4 + x, py = by * 4 + y;

						if (px < width && py < height)
						{
							destination[(z * height + py) * width + px] =
								(colors[y * 4 + x] & 0x00ffffffUL) | (alpha[y * 4 + x] << 24);
						}
					}
				}
			}
		}
	}
}

#ifdef XGPU_DXT1_ASTC
/* ---------- DXT1 as ASTC 4x4, for ES drivers with ASTC and no S3TC

A DXT1 block is two 565 colors and a 2-bit index for each texel, which an
ASTC 4x4 block holds nearly as it is: the colors as its endpoints, the
indices as its weights. So DXT1 needs no encoder here, only its bits moved,
and takes 8 bits a texel on the GPU instead of the 32 it is decoded to
(display.dxt1_astc). DXT3 and DXT5, whose alpha does not fit beside the
colors, are still decoded. A block is one of three kinds:
- four colors (c0 > c1): RGB endpoints of 8 bits and 2-bit weights, 0, 21,
  43 and 64 sixty-fourths of the way (DXT's thirds within a 64th);
- three colors, none transparent: RGB endpoints of 8 bits and weights of
  0, 32 and 64 (exact);
- three colors and transparent texels: two planes, the colors' weights and
  alpha's (0 or 255), with RGBA endpoints of 7 bits, as many as the block
  has room for; a transparent texel takes the darker color, where DXT's is
  black.
(The layout: the Khronos Data Format Specification's "ASTC" chapter.) */

#define GL_COMPRESSED_RGBA_ASTC_4x4_KHR 0x93b0

/* a field's bits written into the block from the bit given, lowest first;
or for weights (reversed), down from the block's top */
static void astc_bits(unsigned char block[16], unsigned long position, unsigned long count, unsigned long value,
	BOOL reversed)
{
	unsigned long bit;

	for (bit = 0; bit < count; bit++)
	{
		unsigned long at = reversed ? 127 - (position + bit) : position + bit;

		if ((value >> bit) & 1)
			block[at >> 3] |= (unsigned char)(1 << (at & 7));
	}
}

/* five trits from their 8 bits, as the specification unpacks them */
static void astc_trits_unpacked(unsigned long bits, unsigned char trits[5])
{
	unsigned long c;

	if (((bits >> 2) & 7) == 7)
	{
		c = (((bits >> 5) & 7) << 2) | (bits & 3);
		trits[4] = 2;
		trits[3] = 2;
	}
	else
	{
		c = bits & 0x1f;
		if (((bits >> 5) & 3) == 3)
		{
			trits[4] = 2;
			trits[3] = (unsigned char)((bits >> 7) & 1);
		}
		else
		{
			trits[4] = (unsigned char)((bits >> 7) & 1);
			trits[3] = (unsigned char)((bits >> 5) & 3);
		}
	}
	if ((c & 3) == 3)
	{
		trits[2] = 2;
		trits[1] = (unsigned char)((c >> 4) & 1);
		trits[0] = (unsigned char)((((c >> 3) & 1) << 1) | ((c >> 2) & 1 & ~(c >> 3)));
	}
	else if (((c >> 2) & 3) == 3)
	{
		trits[2] = 2;
		trits[1] = 2;
		trits[0] = (unsigned char)(c & 3);
	}
	else
	{
		trits[2] = (unsigned char)((c >> 4) & 1);
		trits[1] = (unsigned char)((c >> 2) & 3);
		trits[0] = (unsigned char)((((c >> 1) & 1) << 1) | (c & 1 & ~(c >> 1)));
	}
}

/* the weights (0 to 2, count of them) as trits, packed five to 8 bits, the
last group with only the bits its trits need, from the block's top */
static void astc_trit_weights(unsigned char block[16], const unsigned char *weights, unsigned long count)
{
	static unsigned char packed[243];
	static BOOL made;
	unsigned long group, position = 0;

	if (!made)
	{
		static BOOL filled[243];
		unsigned long bits;

		/* (the smallest bits for each five, so that a last group's unused
		trits, zero, need no bits) */
		for (bits = 0; bits < 256; bits++)
		{
			unsigned char trits[5];
			unsigned long index;

			astc_trits_unpacked(bits, trits);
			index = trits[0] + 3 * trits[1] + 9 * trits[2] + 27 * trits[3] + 81 * trits[4];
			if (!filled[index])
			{
				filled[index] = TRUE;
				packed[index] = (unsigned char)bits;
			}
		}
		made = TRUE;
	}
	for (group = 0; group < count; group += 5)
	{
		static const unsigned char group_bits[6] = { 0, 2, 4, 5, 7, 8 };
		unsigned long index = 0, scale = 1, trit, in_group = count - group < 5 ? count - group : 5;

		for (trit = 0; trit < in_group; trit++, scale *= 3)
			index += weights[group + trit] * scale;
		astc_bits(block, position, group_bits[in_group], packed[index], TRUE);
		position += group_bits[in_group];
	}
}

/* one DXT1 block as ASTC 4x4 */
static void dxt1_astc_block(const unsigned char *source, unsigned char block[16])
{
	unsigned long c0 = source[0] | (source[1] << 8), c1 = source[2] | (source[3] << 8);
	unsigned long indices = source[4] | (source[5] << 8) | ((unsigned long)source[6] << 16) |
		((unsigned long)source[7] << 24);
	unsigned long endpoints[2][3], sums[2], channel, texel;
	unsigned char weights[32];
	BOOL four = c0 > c1, transparent = FALSE, swap;
	unsigned long bits = 8, maximum = four ? 3 : 2;

	/* (each index's weight from the first color: DXT's order is c0, c1,
	then the colors between) */
	static const unsigned char four_weights[4] = { 0, 3, 1, 2 }, three_weights[4] = { 0, 2, 1, 0 };

	memset(block, 0, 16);
	for (texel = 0; texel < 16; texel++)
		transparent |= !four && ((indices >> (texel * 2)) & 3) == 3;
	if (transparent)
		bits = 7;
	endpoints[0][0] = expand5(c0 >> 11);
	endpoints[0][1] = expand6((c0 >> 5) & 0x3f);
	endpoints[0][2] = expand5(c0 & 0x1f);
	endpoints[1][0] = expand5(c1 >> 11);
	endpoints[1][1] = expand6((c1 >> 5) & 0x3f);
	endpoints[1][2] = expand5(c1 & 0x1f);
	for (texel = 0; texel < 2; texel++)
	{
		sums[texel] = 0;
		for (channel = 0; channel < 3; channel++)
		{
			/* (7 bits: the nearest the decoder's bit replication gives) */
			if (bits == 7)
				endpoints[texel][channel] = ((endpoints[texel][channel] >> 1) << 1) | (endpoints[texel][channel] >> 7);
			sums[texel] += endpoints[texel][channel];
		}
	}
	/* (the decoder swaps the endpoints, and blends blue into the others,
	where the second's sum is less: the lesser comes first here instead) */
	swap = sums[1] < sums[0];
	for (texel = 0; texel < 16; texel++)
	{
		unsigned long index = (indices >> (texel * 2)) & 3;
		unsigned char weight = four ? four_weights[index] : three_weights[index];

		if (!four && index == 3)
			weight = 0;
		else if (swap)
			weight = (unsigned char)(maximum - weight);
		if (transparent)
		{
			weights[texel * 2] = weight;
			weights[texel * 2 + 1] = index != 3 ? 2 : 0;
		}
		else
			weights[texel] = weight;
	}
	/* the block mode (a 4x4 grid; 2-bit weights, trits, or trits on two
	planes), one partition, and its color endpoint mode (RGB direct, or
	RGBA direct) */
	astc_bits(block, 0, 11, four ? 0x042 : transparent ? 0x451 : 0x051, FALSE);
	astc_bits(block, 13, 4, transparent ? 12 : 8, FALSE);
	for (channel = 0; channel < 3; channel++)
	{
		astc_bits(block, 17 + channel * 2 * bits, bits, endpoints[swap][channel] >> (8 - bits), FALSE);
		astc_bits(block, 17 + (channel * 2 + 1) * bits, bits, endpoints[!swap][channel] >> (8 - bits), FALSE);
	}
	if (transparent)
	{
		/* (alpha 0 at the first endpoint and 255 at the second, and on the
		second plane: the color component selector, below the weights) */
		astc_bits(block, 17 + 7 * bits, bits, 0x7f, FALSE);
		astc_bits(block, 128 - 52 - 2, 2, 3, FALSE);
		astc_trit_weights(block, weights, 32);
	}
	else if (four)
	{
		for (texel = 0; texel < 16; texel++)
			astc_bits(block, texel * 2, 2, weights[texel], TRUE);
	}
	else
		astc_trit_weights(block, weights, 16);
}

static void dxt1_astc_level(const unsigned char *source, unsigned long width, unsigned long height,
	unsigned char *blocks)
{
	unsigned long block, count = ((width + 3) / 4) * ((height + 3) / 4);

	for (block = 0; block < count; block++)
		dxt1_astc_block(source + block * 8, blocks + block * 16);
}
#endif
#endif

static GLenum compressed_format(unsigned char kind)
{
	switch (kind)
	{
	case _texel_dxt1: return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
	case _texel_dxt3: return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
	default: return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
	}
}

/* ---------- upload */

/* debug.texture_dump_directory writes level 0 of every upload as a TGA, read back from GL */
static void texture_dump(GLenum target, const struct xgpu_texture_description *description)
{
#ifdef HALO_GLES
	/* ES cannot read textures back */
	(void)target;
	(void)description;
}
#else
	static unsigned long dump_index = 0;
	const char *directory = *config_string("debug.texture_dump_directory") ?
		config_string("debug.texture_dump_directory") : NULL;
	unsigned long width = description->width, height = description->height;
	unsigned char header[18];
	unsigned char *pixels;
	char path[512];
	FILE *file;

	if (!directory || target != GL_TEXTURE_2D)
		return;
	pixels = malloc(width * height * 4);
	if (!pixels)
		return;
	glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
	snprintf(path, sizeof(path), "%s/tex%05lu_fmt%02x_%lux%lu.tga", directory, dump_index++,
		(unsigned)description->format, width, height);
	file = fopen(path, "wb");
	if (file)
	{
		memset(header, 0, sizeof(header));
		header[2] = 2;
		header[12] = (unsigned char)width; header[13] = (unsigned char)(width >> 8);
		header[14] = (unsigned char)height; header[15] = (unsigned char)(height >> 8);
		header[16] = 32; header[17] = 0x28;
		fwrite(header, 1, sizeof(header), file);
		fwrite(pixels, 4, width * height, file);
		fclose(file);
	}
	free(pixels);
}
#endif

#ifdef HALO_BGFX
BOOL xgpu_texture_decode_level(const struct xgpu_texture_description *description, unsigned long level,
	const unsigned char *source, const D3DCOLOR *palette, unsigned long *destination)
{
	return decode_level(description, level, source, palette, destination);
}

int xgpu_texture_dxt(const struct xgpu_texture_description *description)
{
	switch (format_information(description->format).kind)
	{
	case _texel_dxt1: return 1;
	case _texel_dxt3: return 3;
	case _texel_dxt5: return 5;
	default: return 0;
	}
}

unsigned long xgpu_texture_level_bytes(const struct xgpu_texture_description *description, unsigned long level)
{
	return level_bytes(description, level);
}
#endif

static void upload(GLuint texture, GLenum target, const struct xgpu_texture_description *description,
	const unsigned char *base, const D3DCOLOR *palette)
{
	struct format_information information = format_information(description->format);
	unsigned long face_count = description->cube_map ? 6 : 1;
	unsigned long face_size = xgpu_texture_face_size(description);
	unsigned long largest = description->width * description->height * description->depth;
	BOOL decode_compressed = FALSE;
	unsigned long *converted;
	unsigned char *astc = NULL;
	unsigned long face, level;
#if defined(HALO_WEB) || defined(HALO_BROWSER)
	/* for each order the texels keep their channels in (the Xbox's, a Halo
	PC multipurpose map's, a Halo PC HUD meter's), the channel (red, green,
	blue, alpha) each channel is taken from, as the texture swizzle below
	samples them where there is one */
	static const unsigned char channel_sources[3][4] =
	{
		{ 0, 1, 2, 3 },
		{ 2, 1, 3, 0 },
		{ 3, 3, 3, 0 },
	};
	int channel_order = description->pc_meter ? 2 : description->pc_multipurpose ? 1 : 0;
#endif

#ifdef HALO_GLES
	decode_compressed = description->compressed && !xgpu_capabilities.s3tc;
#endif
	if (information.kind == _texel_bc7)
		decode_compressed = TRUE;
#ifdef XGPU_DXT1_ASTC
	/* (DXT1 as ASTC 4x4, its bits moved instead of decoded: dxt1_astc_block) */
	if (decode_compressed && information.kind == _texel_dxt1 && target != GL_TEXTURE_3D &&
		xgpu_capabilities.astc && config_boolean("display.dxt1_astc"))
	{
		astc = malloc(((description->width + 3) / 4) * ((description->height + 3) / 4) * 16);
		if (!astc)
		{
			platform_log("textures: no memory to convert a %lux%lux%lu texture; it is not drawn",
				description->width, description->height, description->depth);
			return;
		}
		decode_compressed = FALSE;
	}
#endif
#if defined(HALO_WEB) || defined(HALO_BROWSER)
	/* WebGL's S3TC takes 2D textures whose sides are multiples of 4; and a
	texture whose channels are where Halo PC keeps them is decoded, to put
	them in order (WebGL 2 has no texture swizzle) */
	decode_compressed |= description->compressed &&
		(target == GL_TEXTURE_3D || (description->width & 3) || (description->height & 3) ||
			channel_order != 0);
#endif
	converted = description->compressed && !decode_compressed ? NULL : malloc(largest * sizeof(unsigned long));
	if (!converted && !(description->compressed && !decode_compressed))
	{
		platform_log("textures: no memory to convert a %lux%lux%lu texture; it is not drawn",
			description->width, description->height, description->depth);
		return;
	}
	glBindTexture(target, texture);
	xgpu_gl_state_invalidate();
#if !defined(HALO_BROWSER) && !defined(HALO_WEB)
	/* the channel each channel is sampled from, set on every upload: a
	texture object is reused for whatever pixels arrive at its address (WebGL
	2 has no texture swizzle: the browser builds put the channels in order
	before they are uploaded instead, below) */
	{
		GLint channels[4] = { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };

#ifdef HALO_GLES
		/* converted texels are BGRA in memory (32-bit ARGB words); ES takes
		RGBA */
		if (converted)
		{
			channels[0] = GL_BLUE;
			channels[2] = GL_RED;
		}
#endif
		/* a Halo PC HUD meter: its fill order (alpha) sampled as the color,
		its shape (the color) as alpha, as the Xbox's meter shader reads
		them (D3DCOMMON_PORT_PC_METER) */
		if (description->pc_meter)
		{
			GLint red = channels[0];

			channels[0] = channels[1] = channels[2] = channels[3];
			channels[3] = red;
		}
		/* a Halo PC multipurpose map: specular (blue), self-illumination
		(green), color change (alpha) and the auxiliary mask (red) sampled
		where the Xbox's model shaders read them, red, green, blue and alpha
		(D3DCOMMON_PORT_PC_MULTIPURPOSE) */
		else if (description->pc_multipurpose)
		{
			GLint red = channels[0];

			channels[0] = channels[2];
			channels[2] = channels[3];
			channels[3] = red;
		}
		glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, channels[0]);
		glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, channels[1]);
		glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, channels[2]);
		glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, channels[3]);
	}
#endif
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, (GLint)description->levels - 1);
	for (face = 0; face < face_count; face++)
	{
		GLenum image_target = description->cube_map ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : target;

		for (level = 0; level < description->levels; level++)
		{
			const unsigned char *source = base + face * face_size + xgpu_texture_level_offset(description, level);
			GLsizei width = (GLsizei)level_dimension(description->width, level);
			GLsizei height = (GLsizei)level_dimension(description->height, level);
			GLsizei depth = (GLsizei)level_dimension(description->depth, level);

			/* (Halo PC's cube map: its levels one after another, each the six
			faces', the second and third swapped) */
			if (description->pc_layout && description->cube_map)
				source = base + xgpu_texture_level_offset(description, level) * 6 +
					(face == 1 ? 2 : face == 2 ? 1 : face) * level_bytes(description, level);

			if (astc)
			{
#ifdef XGPU_DXT1_ASTC
				dxt1_astc_level(source, (unsigned long)width, (unsigned long)height, astc);
				glCompressedTexImage2D(image_target, (GLint)level, GL_COMPRESSED_RGBA_ASTC_4x4_KHR, width, height, 0,
					(GLsizei)(((width + 3) / 4) * ((height + 3) / 4) * 16), astc);
#endif
			}
			else if (description->compressed && !decode_compressed)
			{
				if (target == GL_TEXTURE_3D)
					glCompressedTexImage3D(image_target, (GLint)level, compressed_format(information.kind), width, height, depth, 0,
						(GLsizei)level_bytes(description, level), source);
				else
					glCompressedTexImage2D(image_target, (GLint)level, compressed_format(information.kind), width, height, 0,
						(GLsizei)level_bytes(description, level), source);
			}
			else
			{
				if (information.kind == _texel_bc7)
					bc7_decode_level(source, (unsigned long)width, (unsigned long)height, (unsigned long)depth,
						converted);
				else
#ifdef HALO_GLES
				if (decode_compressed)
					dxt_decode_level(information.kind, source, (unsigned long)width, (unsigned long)height,
						(unsigned long)depth, converted);
				else
#endif
				if (!decode_level(description, level, source, palette, converted))
				{
					platform_log("textures: no memory to convert a %lux%lux%lu texture; it is not drawn",
						description->width, description->height, description->depth);
					free(converted);
					return;
				}
#if defined(HALO_WEB) || defined(HALO_BROWSER)
				{
					/* BGRA (32-bit ARGB words) to the RGBA WebGL takes (WebGL 2
					has no texture swizzle), each channel from where the texels
					keep it: a Halo PC HUD meter's or multipurpose map's in this
					fork's browser build too (it swapped blue and red alone, so
					Custom Edition maps' meters drew their fill and shape
					crossed) */
					unsigned long texel, count = (unsigned long)width * (unsigned long)height * (unsigned long)depth;

					for (texel = 0; texel < count; texel++)
					{
						unsigned long value = converted[texel];

						if (channel_order == 0)
						{
							converted[texel] = (value & 0xff00ff00UL) | ((value >> 16) & 0xffUL) | ((value & 0xffUL) << 16);
						}
						else
						{
							/* (red, green, blue and alpha, as stored) */
							unsigned long stored[4];
							unsigned long channel;

							stored[0] = (value >> 16) & 0xffUL;
							stored[1] = (value >> 8) & 0xffUL;
							stored[2] = value & 0xffUL;
							stored[3] = (value >> 24) & 0xffUL;
							converted[texel] = 0;
							for (channel = 0; channel < 4; channel++)
								converted[texel] |= stored[channel_sources[channel_order][channel]] << (8 * channel);
						}
					}
				}
#endif
#ifdef HALO_BIG_ENDIAN
				/* (each texel is a host word, 0xAARRGGBB: GL_BGRA's bytes are a
				little-endian word's) */
				{
					unsigned long texel, texels = (unsigned long)width * (unsigned long)height *
						(unsigned long)(target == GL_TEXTURE_3D ? depth : 1);

					for (texel = 0; texel < texels; texel++)
						converted[texel] = __builtin_bswap32(converted[texel]);
				}
#endif
				if (target == GL_TEXTURE_3D)
					glTexImage3D(image_target, (GLint)level, GL_RGBA8, width, height, depth, 0, GL_BGRA, GL_UNSIGNED_BYTE, converted);
				else
					glTexImage2D(image_target, (GLint)level, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, converted);
			}
		}
	}
	free(converted);
	free(astc);
	texture_dump(target, description);
}

/* ---------- cache */

struct texture_entry
{
	struct texture_entry *next;
	DWORD data, format_word, size_word;
	unsigned long palette_hash;
	GLuint texture;
	GLenum target;
	struct xgpu_texture_description description;
	unsigned long address, size;
	unsigned long generation;
	#ifdef HALO_BROWSER
	unsigned long content_hash;
	unsigned long content_hash_frame;
	unsigned long content_hash_interval;
	#endif
	unsigned long last_used_frame;
	/* the high-res HUD texture drawn in its place (hud_hires.h), or -1, as
of its settings' changes (hud_hires_settings) */
	long override;
	unsigned long override_settings;
	GLuint hud_upscale;
	unsigned long hud_upscale_levels;
	BOOL hud_upscale_checked;
	/* the newest generation of its pages (memory_watch_generation) as of the
	memory watch serial read before it was found: the same while no watched
	page has been written since (0: never found) */
	unsigned long watched_serial, watched_generation;
#ifdef HALO_ANDROID_CE_TEXTURES
	const void *ce_resource;
#endif
};

#define TEXTURE_BUCKET_COUNT 4096
#define TEXTURE_SWEEP_SECONDS 10
#define TEXTURE_IDLE_SECONDS 30
#define TEXTURE_SWEEP_COUNT (TEXTURE_IDLE_SECONDS / TEXTURE_SWEEP_SECONDS)
#define MAXIMUM_PALETTE_VARIANTS 8

static struct texture_entry *texture_buckets[TEXTURE_BUCKET_COUNT];

/* Draws mostly bind the textures the draws before them bound. A lookup of a
texture that is not palettized is remembered with the memory watch serial it
started at: while no watched page has been written since, and no texture
has been dropped, the same lookup finds the same current texture. */
#define RECENT_TEXTURE_COUNT 512

static struct
{
	DWORD data, format_word, size_word;
	struct texture_entry *entry;
	unsigned long watch_serial;
	unsigned long drop_serial;
#ifdef HALO_ANDROID_CE_TEXTURES
	const void *ce_resource;
#endif
} recent_textures[RECENT_TEXTURE_COUNT];
static unsigned long texture_drop_serial = 1;
static unsigned long texture_frame = 0;
/* the frame of each of the last sweeps, a sweep apart, and when the next
is due */
static unsigned long texture_sweep_frames[TEXTURE_SWEEP_COUNT];
static unsigned long texture_sweep_count = 0;
static Uint64 texture_next_sweep = 0;

#ifdef HALO_BROWSER
/* Browser Wasm has no page protection, so it cannot use memory_watch.c's
fault-driven dirty tracking.  Back off to one content check every eight frames
for textures that stay unchanged; a changing texture immediately returns to
checks every frame.  This preserves animated texture updates without hashing
the full static texture set on every presentation. */
static unsigned long texture_content_hash(const unsigned char *bytes, unsigned long size)
{
	unsigned long hash = 2166136261UL ^ size;

	while (size >= 4)
	{
		unsigned long word = (unsigned long)bytes[0] |
			((unsigned long)bytes[1] << 8) |
			((unsigned long)bytes[2] << 16) |
			((unsigned long)bytes[3] << 24);

		hash = (hash ^ word) * 16777619UL;
		hash = (hash << 5) | (hash >> 27);
		bytes += 4;
		size -= 4;
	}
	while (size--)
		hash = (hash ^ *bytes++) * 16777619UL;
	return hash ? hash : 1;
}
#endif

/* (every bit of the three mixed into the top ones: textures are aligned,
and few sizes and formats are common) */
static unsigned long bucket_index(DWORD data, DWORD format_word, DWORD size_word)
{
	unsigned long hash = (unsigned long)data * 2654435761UL ^ (unsigned long)format_word * 2246822519UL ^
		(unsigned long)size_word * 3266489917UL;

	return ((hash & 0xffffffffUL) >> 20) % TEXTURE_BUCKET_COUNT;
}

/* palettized textures are cached per palette contents: the game rewrites
palettes freely, and often cycles a texture through a few of them */
static unsigned long palette_hash(const D3DCOLOR *palette)
{
	unsigned long hash = 2166136261UL, index;

	if (!palette)
		return 0;
	for (index = 0; index < 256; index++)
		hash = (hash ^ palette[index]) * 16777619UL;
	return hash ? hash : 1;
}

#ifdef HALO_ANDROID_CE_TEXTURES
void android_ce_texture_gpu_forget(const void *resource)
{
	const DWORD *header = resource;
	struct texture_entry **link = &texture_buckets[bucket_index(header[1], header[3], header[4])];

	while (*link)
	{
		struct texture_entry *entry = *link;
		if (entry->ce_resource == resource)
		{
			*link = entry->next;
			glDeleteTextures(1, &entry->texture);
			if (entry->hud_upscale)
				glDeleteTextures(1, &entry->hud_upscale);
			texture_drop_serial++;
			free(entry);
		}
		else
			link = &entry->next;
	}
	xgpu_gl_state_invalidate();
}
#endif

/* whether an entry's pixels can be read: in the contiguous memory (an Xbox
address on the 64-bit builds), or a registered Android CE allocation */
static BOOL texture_entry_pixels_valid(const struct texture_entry *entry)
{
#ifdef HALO_ANDROID_CE_TEXTURES
	/* Registration and the allocation-length check at creation admit these
	 * pixels. Their owner removes the GPU entry before freeing the storage. */
	if (entry->ce_resource)
		return entry->size != 0;
#endif
#ifdef HALO_64BIT
	return platform_is_contiguous(xbox_pointer(entry->address)) &&
		platform_is_contiguous(xbox_pointer(entry->address + entry->size - 1));
#else
	return platform_is_contiguous((void *)entry->address) &&
		platform_is_contiguous((void *)(entry->address + entry->size - 1));
#endif
}

/* a Custom Edition HUD meter being drawn from a bitmap its HUD draws as
artwork too (halo_custom_edition_hud_meter; hud_draw.c, ce_hud.c): its
texture's channels are as Halo PC keeps them, which that draw alone samples
as the Xbox's meter shader reads them (nv2a_pixel_shader_key's
ce_meter_channels). OpenCE's, for ChupathingyCE's loader, which marks a
meter drawn as nothing else in its header instead (D3DCOMMON_PORT_PC_METER) */
static int custom_edition_hud_meter_active;

void halo_custom_edition_hud_meter(int active)
{
	custom_edition_hud_meter_active = active;
}

/* display.high_res_hud, read again only when a setting changes: it is asked
at every bind, and config_boolean takes the settings' lock and looks the name
up among them all */
static BOOL high_res_hud(void)
{
	static unsigned long read_at = (unsigned long)-1;
	static BOOL enabled;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.high_res_hud") != 0;
	}
	return enabled;
}

/* an entry's GL texture and description: its high-res HUD texture's, if it has
one, with the bitmap's own size (which its coordinates are in) */
static GLuint texture_entry_result(struct texture_entry *entry, GLenum *target,
	struct xgpu_texture_description *description)
{
	*target = entry->target;
	*description = entry->description;
	/* (the high-res text's atlas, for its placeholder bitmap: text_hires.h) */
	{
		GLuint atlas = text_hires_atlas_texture(entry->data);

		if (atlas)
		{
			description->levels = 1;
			return atlas;
		}
	}
	/* (a menu's bitmap: menu_files.h) */
	{
		unsigned long levels;
		GLuint art = menu_art_texture(entry->data, &levels);

		if (art)
		{
			description->levels = levels;
			description->hires = TRUE;
			return art;
		}
	}
	/* (a high-res texture drawn for some sprites, for the placeholder the
	game draws them from: hud_hires.h) */
	{
		GLuint texture = hud_hires_placeholder_texture(entry->data, &description->levels);

		if (texture)
		{
			description->hires = TRUE;
			return texture;
		}
	}
	if (entry->override >= 0)
	{
		GLuint texture = hud_hires_override_texture(entry->override, &description->levels);

		if (texture)
		{
			description->hires = TRUE;
			description->hires_coverage = hud_hires_override_coverage(entry->override);
			description->hires_point_threshold = hud_hires_override_point_threshold(entry->override);
			return texture;
		}
	}
	/* These sheets keep the map's art and coordinates. Decode through the
	 * normal format reader; the HUD module owns only the interpolation. */
	if (high_res_hud())
	{
		if (!entry->hud_upscale_checked)
		{
			long kind = hud_hires_upscale_at(entry->address, entry->description.width, entry->description.height);

			entry->hud_upscale_checked = TRUE;
			/* Verified stock CE sheets also include compressed and 512-pixel atlases. */
			if (entry->size && (!entry->description.compressed || kind == 4) && entry->target == GL_TEXTURE_2D &&
				format_information(entry->description.format).kind != _texel_p8 &&
				entry->description.width <= (kind == 4 ? 512 : 256) &&
				entry->description.height <= (kind == 4 ? 512 : 256) &&
				kind &&
				/* (port, this fork: OpenCE's "texels in the Xbox's channel
				order" is, with ChupathingyCE's Custom Edition loader, a
				texture that is neither a Halo PC meter's nor a multipurpose
				map's: the cache marks those, xbox_texture_cache.c) */
				!entry->description.pc_meter && !entry->description.pc_multipurpose &&
				texture_entry_pixels_valid(entry))
			{
				unsigned long *pixels = malloc(entry->description.width * entry->description.height * 4);

				if (pixels && decode_level(&entry->description, 0, (const unsigned char *)xbox_pointer(entry->address), NULL, pixels))
					entry->hud_upscale = hud_hires_upscale_texture((const unsigned char *)pixels,
						entry->description.width, entry->description.height, kind, &entry->hud_upscale_levels);
				free(pixels);
			}
		}
		if (entry->hud_upscale)
		{
			description->hires = TRUE;
			description->levels = entry->hud_upscale_levels;
			return entry->hud_upscale;
		}
	}
	/* Mixed-use CE sheets stay raw for ordinary artwork. Only a meter draw
	reads their alpha as the threshold and red as the shape. Replacements
	and sheets already converted above keep their existing channel order.
	(port, this fork: those already converted are the textures the cache
	marked a Halo PC meter's or multipurpose map's) */
	description->ce_meter_channels = custom_edition_hud_meter_active &&
		!entry->description.pc_meter && !entry->description.pc_multipurpose;
	return entry->texture;
}

GLuint xgpu_texture_get(const DWORD *resource, const D3DCOLOR *palette, GLenum *target,
	struct xgpu_texture_description *description)
{
	DWORD data = resource[1], format_word = resource[3], size_word = resource[4];
	struct texture_entry **bucket = &texture_buckets[bucket_index(data, format_word, size_word)];
	struct texture_entry *entry;
	unsigned long generation;
	BOOL palettized = ((format_word & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == 0x0b;
	unsigned long hash = palettized ? palette_hash(palette) : 0;

	struct texture_entry *oldest_variant = NULL;
	unsigned long variant_count = 0;
	static int no_cache = -1;
	unsigned long recent = bucket_index(data, format_word, size_word) % RECENT_TEXTURE_COUNT;
	#ifndef HALO_BROWSER
	unsigned long watch_serial = memory_watch_serial();
#ifdef HALO_ANDROID_CE_TEXTURES
	unsigned long ce_bytes;
	const void *ce_pixels = android_texture_fixes ? android_ce_texture_pixels(resource, &ce_bytes) : NULL;
	const void *ce_resource = ce_pixels ? resource : NULL;
#endif

	/* (a texture that is not palettized has the one entry, until one is
	dropped: remembered, a page written since only means checking it) */
	entry = NULL;
	if (!palettized && recent_textures[recent].entry && recent_textures[recent].data == data &&
#ifdef HALO_ANDROID_CE_TEXTURES
		recent_textures[recent].ce_resource == ce_resource &&
#endif
		recent_textures[recent].format_word == format_word && recent_textures[recent].size_word == size_word &&
		recent_textures[recent].drop_serial == texture_drop_serial)
	{
		entry = recent_textures[recent].entry;
		if (recent_textures[recent].watch_serial == watch_serial)
		{
			entry->last_used_frame = texture_frame;
			return texture_entry_result(entry, target, description);
		}
	}
	#else
	/* (no recent lookups without memory_watch.c: always the buckets) */
	entry = NULL;
	#endif

	if (!entry)
	{
		for (entry = *bucket; entry; entry = entry->next)
		{
			if (entry->data == data && entry->format_word == format_word && entry->size_word == size_word)
			{
#ifdef HALO_ANDROID_CE_TEXTURES
				if (entry->ce_resource != ce_resource)
					continue;
#endif
				if (entry->palette_hash == hash)
					break;
				variant_count++;
				if (!oldest_variant || entry->last_used_frame < oldest_variant->last_used_frame)
					oldest_variant = entry;
			}
		}
	}
	if (!entry && variant_count >= MAXIMUM_PALETTE_VARIANTS)
	{
		/* a palette that keeps changing reuses the stalest copy */
		entry = oldest_variant;
		entry->palette_hash = hash;
		entry->generation = 0;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
		{
			xgpu_texture_describe(format_word, size_word, description);
			*target = GL_TEXTURE_2D;
			return 0;
		}
		entry->data = data;
		entry->format_word = format_word;
		entry->size_word = size_word;
		entry->palette_hash = hash;
		xgpu_texture_describe(format_word, size_word, &entry->description);
		entry->target = entry->description.cube_map ? GL_TEXTURE_CUBE_MAP :
			entry->description.depth > 1 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
#ifdef HALO_64BIT
		entry->address = (unsigned int)data | PLATFORM_CONTIGUOUS_BASE; /* an Xbox address */
#else
		entry->address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
#endif
		/* (a size beyond D3DDevice_GetDeviceCaps' is never uploaded: its
		byte counts would not fit in 32 bits) */
		entry->size = texture_size_supported(&entry->description) ?
			xgpu_texture_face_size(&entry->description) * (entry->description.cube_map ? 6 : 1) : 0;
#ifdef HALO_ANDROID_CE_TEXTURES
		entry->ce_resource = ce_resource;
		if (ce_resource)
		{
			entry->address = (unsigned long)ce_pixels;
			if (entry->size > ce_bytes)
				entry->size = 0;
		}
#endif
		if (!entry->size)
		{
			platform_log("textures: a %lux%lux%lu texture is larger than the device takes; it is not drawn",
				entry->description.width, entry->description.height, entry->description.depth);
		}
		entry->generation = 0;
		entry->override = -1;
		glGenTextures(1, &entry->texture);
		entry->next = *bucket;
		*bucket = entry;
	}

	if (no_cache < 0)
		no_cache = config_boolean("debug.texture_no_cache");
	#ifdef HALO_BROWSER
	if (platform_is_contiguous((void *)entry->address) &&
		platform_is_contiguous((void *)(entry->address + entry->size - 1)))
	{
		BOOL changed = !entry->generation;

		if (changed || no_cache || texture_frame - entry->content_hash_frame >=
			(entry->content_hash_interval ? entry->content_hash_interval : 1))
		{
			unsigned long content_hash;

			content_hash = texture_content_hash((const unsigned char *)entry->address, entry->size);

			entry->content_hash_frame = texture_frame;
			if (!entry->generation || content_hash != entry->content_hash)
			{
				entry->content_hash = content_hash;
				entry->content_hash_interval = 1;
				changed = TRUE;
			}
			else if (entry->content_hash_interval < 8)
			{
				entry->content_hash_interval = entry->content_hash_interval ?
					entry->content_hash_interval * 2 : 1;
			}
		}
		if (changed || no_cache)
		{
			entry->generation = 1;
			/* (which bitmap is here may have changed with the pixels) */
			entry->override = -1;
			if (!palettized && !entry->description.cube_map && entry->description.depth == 1)
			{
				unsigned long levels;

				entry->override = hud_hires_override_find(entry->address, entry->description.width,
					entry->description.height, entry->description.levels > 1 ?
					xgpu_texture_level_offset(&entry->description, 1) : xgpu_texture_face_size(&entry->description));
				if (entry->override >= 0 && !hud_hires_override_texture(entry->override, &levels))
					entry->override = -1;
			}
			if (entry->override < 0)
				upload(entry->texture, entry->target, &entry->description,
					(const unsigned char *)entry->address, palette);
		}
	}
	#else
	/* (its pages' newest generation, found again only once a watched page
	has been written: a large texture's pages, scanned for every draw that
	bound it, were much of a frame with many objects) */
#ifdef HALO_ANDROID_CE_TEXTURES
	if (entry->ce_resource)
		generation = 1; /* Published CE map pixels do not change while resident. */
	else
#endif
	if (entry->watched_serial && entry->watched_serial == watch_serial)
		generation = entry->watched_generation;
	else
	{
		generation = memory_watch_generation(entry->address, entry->size);
		entry->watched_serial = watch_serial;
		entry->watched_generation = generation;
	}
	if (!entry->generation || generation > entry->generation || no_cache ||
		entry->override_settings != hud_hires_settings())
	{
		/* (whose layout, as the bitmap now here is laid out) */
		entry->description.pc_layout = (resource[0] & D3DCOMMON_PORT_PC_LAYOUT) != 0;
		entry->description.pc_meter = (resource[0] & D3DCOMMON_PORT_PC_METER) != 0;
		entry->description.pc_multipurpose = (resource[0] & D3DCOMMON_PORT_PC_MULTIPURPOSE) != 0;
		/* protect first, so a write racing with the upload is noticed */
#ifdef HALO_ANDROID_CE_TEXTURES
		if (entry->ce_resource)
			entry->generation = 1;
		else
#endif
		{
			memory_watch_protect(entry->address, entry->size);
			entry->generation = memory_watch_generation(entry->address, entry->size);
		}
		if (!entry->generation)
			entry->generation = 1;
		/* (which bitmap is here may have changed with the pixels, and
		whether it has a stand-in with the settings) */
		if (entry->hud_upscale)
		{
			glDeleteTextures(1, &entry->hud_upscale);
			entry->hud_upscale = 0;
			xgpu_gl_state_invalidate();
		}
		entry->hud_upscale_checked = FALSE;
		entry->override = -1;
		entry->override_settings = hud_hires_settings();
		if (entry->size && !palettized && !entry->description.cube_map && entry->description.depth == 1)
		{
			unsigned long levels;

			entry->override = hud_hires_override_find(entry->address, entry->description.width,
				entry->description.height, entry->description.levels > 1 ?
				xgpu_texture_level_offset(&entry->description, 1) : xgpu_texture_face_size(&entry->description));
			if (entry->override >= 0 && !hud_hires_override_texture(entry->override, &levels))
				entry->override = -1;
		}
		if (entry->override < 0 && entry->size && texture_entry_pixels_valid(entry))
		{
			if (config_boolean("debug.texture_log"))
			{
#ifdef HALO_64BIT
				const unsigned char *bytes = xbox_pointer(entry->address);
				unsigned int index, ones = 0, zeros = 0;
#else
				const unsigned char *bytes = (const unsigned char *)entry->address;
				unsigned long index, ones = 0, zeros = 0;
#endif

				for (index = 0; index < entry->size; index++)
				{
					ones += bytes[index] == 0xff;
					zeros += bytes[index] == 0;
				}
				platform_log("texture upload %08lx fmt %02lx %lux%lu size %lu gen %lu ff %lu%% 00 %lu%%",
					(unsigned long)data, (unsigned long)entry->description.format, entry->description.width,
					entry->description.height, entry->size, entry->generation,
					ones * 100 / entry->size, zeros * 100 / entry->size);
			}
			upload(entry->texture, entry->target, &entry->description, (const unsigned char *)xbox_pointer(entry->address), palette);
			xgpu_statistics_texture_upload(entry->size);
		}
	}
	#endif
	entry->last_used_frame = texture_frame;
	#ifndef HALO_BROWSER
	if (!palettized && !no_cache)
	{
		recent_textures[recent].data = data;
		recent_textures[recent].format_word = format_word;
		recent_textures[recent].size_word = size_word;
		recent_textures[recent].entry = entry;
		recent_textures[recent].watch_serial = watch_serial;
		recent_textures[recent].drop_serial = texture_drop_serial;
#ifdef HALO_ANDROID_CE_TEXTURES
		recent_textures[recent].ce_resource = ce_resource;
#endif
	}
	#endif
	return texture_entry_result(entry, target, description);
}

void xgpu_texture_cache_begin_frame(void)
{
	unsigned long index, idle_since;
	unsigned long changed = hud_hires_settings_changed();
	Uint64 now;

	/* Refresh only map bitmaps belonging to the setting that changed. Keep
	 * decoded replacement art and world textures; rebuild native pixels on
	 * demand too, since an override may have skipped their first upload. */
	if (changed)
	{
		for (index = 0; index < TEXTURE_BUCKET_COUNT; index++)
		{
			struct texture_entry *entry;

			for (entry = texture_buckets[index]; entry; entry = entry->next)
				if (hud_hires_bitmap_affected(entry->address,
					(long)entry->description.width, (long)entry->description.height, changed))
					entry->generation = 0;
		}
		/* Recent hits bypass the generation check. Forget lookups, not textures. */
		memset(recent_textures, 0, sizeof(recent_textures));
		xgpu_gl_state_invalidate();
	}
	texture_frame++;
	now = SDL_GetTicks();
	if (now < texture_next_sweep)
		return;
	texture_next_sweep = now + TEXTURE_SWEEP_SECONDS * 1000;
	/* drop the textures not used since the sweep half a minute ago (none
	before the first sweeps, which find frame 0). The time is counted in
	seconds, not frames: 1800 frames, half a minute at 60 frames a second, is
	five seconds at 360, and a texture out of sight that long was converted
	and uploaded again when it came back into view */
	idle_since = texture_sweep_frames[texture_sweep_count % TEXTURE_SWEEP_COUNT];
	texture_sweep_frames[texture_sweep_count % TEXTURE_SWEEP_COUNT] = texture_frame;
	texture_sweep_count++;
	for (index = 0; index < TEXTURE_BUCKET_COUNT; index++)
	{
		struct texture_entry **link = &texture_buckets[index];

		while (*link)
		{
			struct texture_entry *entry = *link;

			if (texture_frame - entry->last_used_frame > texture_frame - idle_since)
			{
				*link = entry->next;
				glDeleteTextures(1, &entry->texture);
				if (entry->hud_upscale)
					glDeleteTextures(1, &entry->hud_upscale);
				xgpu_gl_state_invalidate();
				texture_drop_serial++;
				free(entry);
			}
			else
			{
				link = &entry->next;
			}
		}
	}
}
