/* Android CE's bounded, stable pixel storage, outside the Xbox window. */
#ifndef ANDROID_CE_TEXTURE_CACHE_H
#define ANDROID_CE_TEXTURE_CACHE_H

/* (port, this fork: the 32-bit Android guest's alone, as OpenCE's Android
build is. Not the browser build, which says HALO_ANDROID too but keeps the
web's texture cache (OpenCE's web build is not HALO_ANDROID), nor the native
64-bit Android build, whose Direct3D headers hold 32-bit Xbox addresses where
this cache would put a heap pointer: xbox_textures.c, xbox_texture_cache.c,
d3d8_gl.c and hud_hires_tags.c test this) */
#if defined(HALO_ANDROID) && !defined(HALO_BROWSER) && !defined(HALO_64BIT) && !defined(HALO_WINDOW_REBASE)
#define HALO_ANDROID_CE_TEXTURES 1
#endif

struct bitmap_data;
typedef void (*android_ce_texture_load_proc)(struct bitmap_data *, void *);
void *android_ce_texture_get(struct bitmap_data *bitmap, unsigned long bytes,
	int load, android_ce_texture_load_proc read);
void android_ce_texture_delete(struct bitmap_data *bitmap);
void android_ce_texture_reset(void);
void android_ce_texture_idle(void);
int android_ce_texture_resident(const struct bitmap_data *bitmap);

/* Only an exact registered header grants the renderer access to heap pixels.
 * Cached map pixels are filled synchronously before this header is published. */
const void *android_ce_texture_pixels(const void *resource, unsigned long *bytes);
void android_ce_texture_gpu_forget(const void *resource);
void android_ce_texture_unbind(const void *resource);

#endif
