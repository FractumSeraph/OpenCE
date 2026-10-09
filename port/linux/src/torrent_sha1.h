/*
TORRENT_SHA1.H

SHA-1 (FIPS 180-4), which BitTorrent names its pieces, its torrents and its
DHT nodes by (torrent.c). Not a cryptographic use: the hashes are checked
against those the torrent's maker computed.
*/

#ifndef __HALO_LINUX_TORRENT_SHA1_H
#define __HALO_LINUX_TORRENT_SHA1_H

#define SHA1_DIGEST_SIZE 20

struct sha1_state
{
	unsigned long state[5];
	unsigned long long length;
	unsigned char buffer[64];
	int buffered;
};

void sha1_start(struct sha1_state *state);
void sha1_update(struct sha1_state *state, const void *bytes, unsigned long size);
void sha1_finish(struct sha1_state *state, unsigned char digest[SHA1_DIGEST_SIZE]);
/* the digest of one buffer */
void sha1_bytes(const void *bytes, unsigned long size, unsigned char digest[SHA1_DIGEST_SIZE]);

#endif
