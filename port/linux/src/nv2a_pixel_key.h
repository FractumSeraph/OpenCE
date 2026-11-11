/*
NV2A_PIXEL_KEY.H

The pixel shader key (xgpu.h), by itself: what the shader key lists'
reader and writer need of it (xgpu_shader_keys.c), and their host test
(port/linux/tests/shader_keys_test.c) with them. DWORD and D3DRS_PS_MAX
come from the includer.
*/

#ifndef __HALO_LINUX_NV2A_PIXEL_KEY_H
#define __HALO_LINUX_NV2A_PIXEL_KEY_H

/* everything a translated pixel shader depends on; the GLSL program cache
is keyed by these bytes. A field added here needs a slot of its own in the
shader key lists (xgpu_shader_keys.c, port/assets/shaders/README.md) */
struct nv2a_pixel_shader_key
{
	DWORD combiner_state[D3DRS_PS_MAX];
	/* D3DRS_PSTEXTUREMODES lies past D3DRS_PS_MAX */
	DWORD texture_modes;
	unsigned char sampler_type[4];
	unsigned char alpha_kill[4];
	/* D3DTSS_COLORSIGN: channels (bit 0 alpha ... bit 3 blue, as
	D3DTSIGN_*) that hold signed data in an unsigned texture format */
	unsigned char color_sign[4];
	/* D3DCMP_* function for the alpha test, or 0 when disabled */
	unsigned long alpha_test_function;
	unsigned char fog_enable;
	unsigned char fog_table_mode;
	/* inside a visibility test: count the samples that pass (Android) */
	unsigned char count_samples;
	/* a high-res HUD meter (hud_hires.h) drawn with the meter's blend (the
	destination kept by the source's alpha): that alpha is eased to 1 by the
	coverage texture 0's green holds, so that the meter darkens what is
	behind it only where it covers it (the Xbox's point-sampled meters stop
	at their texels' edges; filtered ones have a fringe of faint texels) */
	unsigned char coverage_alpha;
	/* a model lighting program's draw lit for each pixel
	(display.per_pixel_lighting): nv2a_vertex_lighting's lights, or 0 for
	the diffuse color the vertex shader computed */
	unsigned char per_pixel_lighting;
	/* drawn into a multisampled target (display.anti_aliasing's
	multisampling), its samples a pixel: the alpha test covers samples in
	proportion to how far alpha is past the reference, not all of the pixel
	or none of it, so that cut-out edges (foliage, grates) are smoothed too */
	unsigned char alpha_test_samples;
	/* Discrete meter thresholds in texture 0's red are read at level zero,
	without filtering. Coverage/brightness still use the filtered lookup. */
	unsigned char point_threshold;
	/* a model lighting program's draw the Light effects light
	(plasma_model_shader.c): their falloff and walls in model_lighting */
	unsigned char plasma_model;	/* (port, this fork) a Custom Edition HUD meter drawn from a bitmap also
	drawn as artwork, whose channels are as Halo PC keeps them: texture 0
	sampled as a Halo PC meter's swizzle does (alpha as the color, red as
	alpha; OpenCE's, halo_custom_edition_hud_meter) */
	unsigned char ce_meter_channels;
	/* (port, this fork) the source's blend factor, applied here instead (1 the
	blend color, 2 one less it, 3 its alpha, 4 one less that; 0 none): WebGL
	refuses a blend of a constant color factor with a constant alpha one, as
	the meters' (shader_transparent_meter) */
	unsigned char source_blend;
};

#endif
