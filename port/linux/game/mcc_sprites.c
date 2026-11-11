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
/* MCC effects may span more than eight bitmap pages. Submit full batches
 * through the normal renderer instead of dropping the remaining sprites. */
#include "cseries.h"
#include "mcc_cache.h"
#include "mcc_sprites.h"
#include "render/render_sprite.h"

int mcc_sprites_continue(struct build_sprite_data *data)
{
    short remaining;
    unsigned long flags;
    if (!mcc_cache_tags_loaded()) return FALSE;
    remaining = data->maximum_sprite_count - data->sprite_count;
    flags = data->flags & ((1u << NUMBER_OF_BUILD_SPRITES_FLAGS) - 1u);
    build_sprites_end(data);
    build_sprites_begin(data, remaining, data->bitmap_group_index, data->shader, flags);
    return TRUE;
}
