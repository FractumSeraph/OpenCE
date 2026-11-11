/* (port, this fork: the 64-bit builds compile MCC's code, but play no MCC
map, mcc_cache.c's MCC_CACHE_64BIT_REFUSED: its tag addresses are a 32-bit
build's pointers, which this build's Xbox addresses are not) */
#if defined(HALO_64BIT) && defined(__clang__)
#pragma clang diagnostic ignored "-Wint-conversion"
#pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"
#pragma clang diagnostic ignored "-Wpointer-integer-compare"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"
#endif
#include "cseries.h"
#include "mcc_cache.h"
#include "mcc_environment_shader.h"

void mcc_environment_shader_begin(unsigned short flags)
{
    /* 0x8 is the MCC environment tag's alternate bump attenuation flag.
     * A specular-mask-only texture does not carry a surface normal. */
    mcc_environment_shader_set(mcc_cache_tags_loaded() && (flags&8) && !(flags&2));
}

void mcc_environment_shader_end(void)
{
    mcc_environment_shader_set(0);
}
