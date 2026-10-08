/*
TORRENT.H

A small BitTorrent client (torrent.c, torrent_peer.c, torrent_tracker.c,
torrent_dht.c), with which the game downloads a Custom Edition map it lacks
from other players and seed boxes, and seeds the map it plays
(map_torrents.c). It knows a torrent as the maps' index lists it: a
single-file v1 torrent's info hash, file name, size and piece length. The
piece hashes come from the file (seeding) or from peers (the metadata
exchange, BEP 9) when downloading, and are checked against the info hash.

It finds peers through the DHT (BEP 5), the trackers given (UDP, BEP 15,
and plain HTTP), and peers' exchanges (BEP 11), and takes pieces from web
seeds too (BEP 19, plain HTTP). It runs on a thread of its own; every
function here may be called from any thread.
*/

#ifndef __HALO_LINUX_TORRENT_H
#define __HALO_LINUX_TORRENT_H

struct torrent_settings
{
	/* the TCP port peers connect to and the UDP port the DHT uses (the
	same number; 0: any free one) */
	int port;
	/* bytes a second, 0 for no limit */
	long upload_limit;
	long download_limit;
	/* comma-separated udp:// and http:// tracker URLs ("" for none) */
	const char *trackers;
	/* comma-separated http:// web seeds: a file's URL is <web seed>/<file
	name>, or the web seed itself if it does not end in "/" */
	const char *web_seeds;
	int dht;
	/* the most peers connected at once (across every torrent) */
	int maximum_peers;
};

enum torrent_state
{
	/* asking peers for the piece hashes */
	_torrent_state_metadata,
	/* the file's pieces being hashed (a file that is there) */
	_torrent_state_checking,
	_torrent_state_downloading,
	/* the file is complete */
	_torrent_state_seeding,
	_torrent_state_failed,
};

struct torrent_status
{
	enum torrent_state state;
	/* bytes of the file had, and its size */
	unsigned long long have_bytes;
	unsigned long long total_bytes;
	/* uploaded since added */
	unsigned long long uploaded_bytes;
	int peers_connected;
	int peers_known;
	int seeds_connected;
	/* bytes a second, over the last seconds */
	long download_rate;
	long upload_rate;
	/* what is happening, for a status line ("3 peers, DHT 120 nodes") or
	why it failed */
	char detail[128];
};

/* starts the client's thread, listening; 1 on success, else 0 and why in
error */
int torrent_start(const struct torrent_settings *settings, char *error, int error_size);
void torrent_stop(void);

/* adds a torrent: info_hash as 40 hex digits, the file's name in the
torrent, its size and piece length, and the path of the file: the one to
seed (seeding: it is checked and seeded, never written), or where the
download goes (a partial one there is checked and continued). Returns a
handle, or -1 with why in error */
int torrent_add(const char *info_hash, const char *name, unsigned long long size, unsigned long piece_length,
	const char *path, int seeding, char *error, int error_size);
/* stops and forgets a torrent (its file is left as it is) */
void torrent_remove(int handle);
/* moves a complete torrent's file to path (which must be on the same
file system), to go on seeding it from there; 1 on success */
int torrent_move(int handle, const char *path);
/* a torrent's state; 0 if the handle is not one */
int torrent_status_get(int handle, struct torrent_status *status);
/* a peer to try for the torrent (address and port in network byte
order): a known seed, say, or the host's own client */
void torrent_add_peer(int handle, unsigned long address, unsigned short port);

void torrent_set_limits(long upload_limit, long download_limit);
/* the port listened on (0: not started) */
int torrent_port(void);

#endif
