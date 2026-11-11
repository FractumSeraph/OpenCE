/* (port, this fork: as MCC's units, mcc_cache.c's MCC_CACHE_64BIT_REFUSED) */
#if defined(HALO_64BIT) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wint-conversion"
#pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"
#pragma clang diagnostic ignored "-Wpointer-integer-compare"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"
#endif
/* The first-person cache can outlive a scripted equipment replacement.
 * Release its effects while the deleted weapon still exists, on both the
 * authoritative machine and clients applying an object deletion. */
#include "mcc_cache.h"

void mcc_first_person_weapon_deleted(long weapon_index)
{
    short local;
    if (!mcc_cache_tags_loaded() || weapon_index == NONE) return;
    for (local = 0; local < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; ++local)
        if (first_person_weapon_get(local)->weapon_index == weapon_index)
            first_person_weapon_forget_weapon(local);
}
#if defined(HALO_64BIT) && defined(__clang__)
#pragma clang diagnostic pop
#endif
