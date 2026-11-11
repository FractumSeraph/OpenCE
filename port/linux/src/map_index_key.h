/*
MAP_INDEX_KEY.H

The public keys a fetched approved map list (map_list_index.h) is taken from:
Ed25519, 32 bytes each. They are the fork's release keys (update_key.h's
last two, which sign its releases): the list's workflow
(.github/workflows/map-index.yml) signs with the same secret,
UPDATE_SIGNING_KEY, over MAP_INDEX_CONTEXT and the list, so that no
signature of one can stand for the other. ChupathingyCE's keys are not here:
the fork's maps are approved by the fork alone (map_torrents.h).

To rotate, as update_key.h says, here too.
*/

#ifndef MAP_INDEX_KEY_H
#define MAP_INDEX_KEY_H

#ifndef HALO_MAP_INDEX_TEST_KEY
static const unsigned char map_index_public_keys[][32] =
{
	/* the fork's release key, 2026-10-16 */
	{
		0xc1, 0x20, 0x6c, 0xa2, 0xbe, 0xca, 0x78, 0xe1, 0x1c, 0xb1, 0x05, 0x30, 0xdd, 0x6e, 0x73, 0x3c,
		0x45, 0x61, 0xa6, 0xf1, 0x76, 0x2d, 0x49, 0x5d, 0xb1, 0xa6, 0x62, 0x53, 0xf3, 0x5a, 0x24, 0x3e,
	},
	/* (and its recovery key, kept offline) */
	{
		0xbb, 0x14, 0x21, 0xe0, 0x14, 0x7f, 0x12, 0x10, 0xa8, 0x44, 0x65, 0x32, 0xf6, 0x70, 0x96, 0xfc,
		0xe8, 0x87, 0xc7, 0xb5, 0x97, 0x87, 0x5c, 0x55, 0x27, 0x12, 0x98, 0xe4, 0x71, 0x88, 0xf8, 0xd3,
	},
};
#else
/* (a test's key, given by the test's build: HALO_MAP_INDEX_TEST_KEY is its bytes) */
static const unsigned char map_index_public_keys[][32] = { { HALO_MAP_INDEX_TEST_KEY } };
#endif

#endif
