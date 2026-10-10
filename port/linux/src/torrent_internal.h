/*
TORRENT_INTERNAL.H

What the BitTorrent client's files share (torrent.h): the session, its
torrents, their peers, trackers and web seeds, and the functions each file
gives the others. Everything is the client thread's, under torrent_lock,
which the API's functions take; the thread lets it go while it waits in
select.

Addresses and ports are in network byte order, as sockaddr_in holds them.
*/

#ifndef __HALO_LINUX_TORRENT_INTERNAL_H
#define __HALO_LINUX_TORRENT_INTERNAL_H

#include "platform.h"
#include "posix.h"
#include "torrent.h"
#include "torrent_bencode.h"
#include "torrent_sha1.h"

#include <pthread.h>

#define TORRENT_MAXIMUM_TORRENTS 8
/* connected peers of one torrent */
#define TORRENT_MAXIMUM_PEERS 24
/* addresses known of one torrent's peers */
#define TORRENT_MAXIMUM_CANDIDATES 256
/* (pieces under way at once: peers' and a web seed's; at 8 a slow peer's
pieces left a web seed nothing to take) */
#define TORRENT_MAXIMUM_ACTIVE_PIECES 24
#define TORRENT_BLOCK_SIZE 16384
/* a Custom Edition cache is at most 768 MB (cache_file_formats.h), and the
index's piece lengths at least 256 KB (tools/map_torrents.py) */
#define TORRENT_MAXIMUM_PIECES 3072
#define TORRENT_MINIMUM_PIECE_LENGTH (256UL << 10)
#define TORRENT_MAXIMUM_PIECE_LENGTH (8UL << 20)
#define TORRENT_MAXIMUM_BLOCKS_PER_PIECE (TORRENT_MAXIMUM_PIECE_LENGTH / TORRENT_BLOCK_SIZE)
/* block requests outstanding to one peer, and a peer's to us */
#define TORRENT_MAXIMUM_PIPELINE 32
#define TORRENT_MAXIMUM_QUEUED_REQUESTS 64
#define TORRENT_MAXIMUM_TRACKERS 8
/* web seed connections (each web seed given has WEB_SEED_CONNECTIONS of
them: torrent_tracker.c) */
#define TORRENT_MAXIMUM_WEB_SEEDS 8
/* a message from a peer: a piece message's block and its header, or a
bitfield of the most pieces; anything longer ends the connection */
#define TORRENT_RECEIVE_BUFFER (TORRENT_BLOCK_SIZE + 4096)
#define TORRENT_SEND_BUFFER (4 * TORRENT_BLOCK_SIZE)
#define TORRENT_METADATA_PIECE 16384
#define TORRENT_MAXIMUM_METADATA (TORRENT_MAXIMUM_PIECES * SHA1_DIGEST_SIZE + 4096)
/* (the metadata's pieces, as a bitfield: at least a byte of it) */
#define TORRENT_MAXIMUM_METADATA_PIECES \
	(((TORRENT_MAXIMUM_METADATA + TORRENT_METADATA_PIECE - 1) / TORRENT_METADATA_PIECE + 7) / 8 * 8)
#define TORRENT_HTTP_REQUEST_SIZE 1024
#define TORRENT_HTTP_HEADER_SIZE 8192
#define TORRENT_RATE_SECONDS 5

#define TORRENT_SECOND 1000UL
#define TORRENT_MINUTE (60 * TORRENT_SECOND)
/* a metadata piece asked of a peer that has not come after this long is
asked of another */
#define TORRENT_METADATA_REQUEST_TIMEOUT (10 * TORRENT_SECOND)
/* Winsock's SO_ERROR, which the XDK's headers do not name; posix_net.c
and win32_net.c take it */
#define TORRENT_SO_ERROR 0x1007

/* where a peer's address came from */
enum torrent_source
{
	_torrent_source_tracker,
	_torrent_source_dht,
	_torrent_source_pex,
	_torrent_source_incoming,
};

struct torrent_candidate
{
	unsigned long address;
	unsigned short port;
	unsigned char source;
	unsigned char failures;
	unsigned char connected;
	unsigned long last_tried;
};

enum torrent_peer_state
{
	_torrent_peer_connecting,
	/* connected (or accepted): the handshakes under way */
	_torrent_peer_handshake,
	_torrent_peer_connected,
};

struct torrent_request
{
	int piece;
	int begin;
	int length;
	unsigned long time;
};

struct torrent_peer
{
	int used;
	int socket;
	unsigned long address;
	unsigned short port;
	int incoming;
	enum torrent_peer_state state;
	unsigned long started;
	unsigned long last_received;
	unsigned long last_sent;
	unsigned char id[20];
	int handshake_sent;
	int am_choking;
	int am_interested;
	int peer_choking;
	int peer_interested;
	unsigned char bitfield[TORRENT_MAXIMUM_PIECES / 8];
	int have_count;
	/* the extension protocol (BEP 10): the peer's ids for ut_metadata and
	ut_pex (0: not supported), and the metadata size it told */
	int extensions;
	int ut_metadata;
	int ut_pex;
	int metadata_size;
	/* the metadata piece asked of it (-1: none) */
	int metadata_piece;
	unsigned long metadata_request_time;
	unsigned char in[TORRENT_RECEIVE_BUFFER];
	int in_size;
	unsigned char out[TORRENT_SEND_BUFFER];
	int out_size;
	/* our block requests to the peer, and its to us */
	struct torrent_request requests[TORRENT_MAXIMUM_PIPELINE];
	int request_count;
	struct torrent_request queued[TORRENT_MAXIMUM_QUEUED_REQUESTS];
	int queued_count;
	unsigned long long downloaded;
	unsigned long long uploaded;
	unsigned long unchoked_time;
	/* no block came for a while after a request: asked less of */
	int snubbed;
};

/* an HTTP/1.1 GET on its own socket (torrent_tracker.c): a tracker's
announce, read whole, or a web seed's range, streamed to a function */
struct torrent_http
{
	int socket;
	enum
	{
		_torrent_http_idle,
		_torrent_http_connecting,
		_torrent_http_sending,
		_torrent_http_headers,
		_torrent_http_body,
		_torrent_http_done,
		_torrent_http_failed,
	} state;
	unsigned long started;
	unsigned long address;
	unsigned short port;
	char request[TORRENT_HTTP_REQUEST_SIZE];
	int request_size;
	int request_sent;
	unsigned char in[TORRENT_HTTP_HEADER_SIZE];
	int in_size;
	int status;
	/* a Range asked for: answered only by 206 from its first byte */
	int ranged;
	long long range_first;
	long long content_length;
	long long body_received;
	/* a response read whole (NULL: streamed) */
	unsigned char *body;
	int body_capacity;
	void (*stream)(void *context, const unsigned char *bytes, int size);
	void *context;
	char error[96];
};

struct torrent_tracker
{
	char url[256];
	int udp;
	char host[128];
	unsigned short port;
	char path[160];
	unsigned long address;
	unsigned long resolved_time;
};

struct torrent_tracker_state
{
	unsigned long next_time;
	int failures;
	int started_sent;
	int completed_sent;
	/* a UDP tracker's connection (BEP 15) */
	unsigned long long connection_id;
	unsigned long connection_time;
	unsigned long transaction;
	int stage;
	unsigned long sent_time;
	int peers_last;
	struct torrent_http http;
};

struct torrent_web_seed_state
{
	struct torrent_http http;
	/* the piece being fetched (-1: none), and the bytes of it had */
	int piece;
	int received;
	int failures;
	unsigned long retry_time;
	unsigned char block[TORRENT_BLOCK_SIZE];
	int block_size;
};

struct torrent_active_piece
{
	int piece;
	int block_count;
	int received_count;
	unsigned char received[TORRENT_MAXIMUM_BLOCKS_PER_PIECE / 8];
	unsigned char requested[TORRENT_MAXIMUM_BLOCKS_PER_PIECE / 8];
	unsigned long started;
	/* a web seed connection is fetching it (torrent_piece_to_request_whole
	until torrent_piece_unrequested): the endgame does not hand it on */
	int web_seed;
};

struct torrent
{
	int used;
	unsigned char info_hash[SHA1_DIGEST_SIZE];
	char info_hash_hex[2 * SHA1_DIGEST_SIZE + 1];
	char name[256];
	unsigned long long size;
	unsigned long piece_length;
	int piece_count;
	char path[1024];
	int seeding;
	enum torrent_state state;
	char failure[128];
	/* the pieces' hashes (piece_count of them), once known */
	unsigned char *piece_hashes;
	unsigned char have[TORRENT_MAXIMUM_PIECES / 8];
	int have_count;
	int file;
	/* the metadata (the info dictionary) being fetched from peers */
	unsigned char *metadata;
	int metadata_size;
	int metadata_piece_count;
	unsigned char metadata_have[TORRENT_MAXIMUM_METADATA_PIECES / 8];
	int metadata_have_count;
	/* checking: the next piece to hash */
	int check_piece;
	struct torrent_active_piece active[TORRENT_MAXIMUM_ACTIVE_PIECES];
	int active_count;
	struct torrent_peer peers[TORRENT_MAXIMUM_PEERS];
	struct torrent_candidate candidates[TORRENT_MAXIMUM_CANDIDATES];
	int candidate_count;
	unsigned long last_connect_time;
	struct torrent_tracker_state trackers[TORRENT_MAXIMUM_TRACKERS];
	struct torrent_web_seed_state web_seeds[TORRENT_MAXIMUM_WEB_SEEDS];
	/* the whole file from the web seeds, with no metadata (no peer has given
	it a while into a download: torrent_web_seeds_tick): its pieces fetched
	unchecked, then the file hashed, which must make the info hash. -1:
	not again (the web seeds' file was not this torrent's) */
	int web_whole;
	unsigned char web_whole_have[TORRENT_MAXIMUM_PIECES / 8];
	unsigned char web_whole_requested[TORRENT_MAXIMUM_PIECES / 8];
	int web_whole_count;
	/* answers to it in a row that were not the piece (torrent_tracker.c) */
	int web_whole_failures;
	unsigned long dht_next_lookup;
	unsigned long dht_last_announce;
	unsigned long long downloaded;
	unsigned long long uploaded;
	long download_by_second[TORRENT_RATE_SECONDS];
	long upload_by_second[TORRENT_RATE_SECONDS];
	unsigned long rate_second;
	unsigned long added_time;
	unsigned long choke_time;
};

struct torrent_session
{
	int running;
	int stop_requested;
	pthread_t thread;
	/* the thread's own account of itself, and its signal as it ends (the
	ports' pthreads have no join) */
	int thread_running;
	pthread_cond_t stopped;
	int port;
	int dht;
	int maximum_peers;
	char trackers_text[1024];
	char web_seeds_text[1024];
	int listen_socket;
	int udp_socket;
	unsigned char peer_id[20];
	struct torrent torrents[TORRENT_MAXIMUM_TORRENTS];
	struct torrent_tracker trackers[TORRENT_MAXIMUM_TRACKERS];
	int tracker_count;
	char web_seeds[TORRENT_MAXIMUM_WEB_SEEDS][256];
	int web_seed_count;
	/* each web seed connection's server, looked up once in a while (0: not
	yet, or it could not be) */
	unsigned long web_seed_addresses[TORRENT_MAXIMUM_WEB_SEEDS];
	unsigned long web_seed_resolved_times[TORRENT_MAXIMUM_WEB_SEEDS];
	long upload_limit;
	long download_limit;
	double upload_tokens;
	double download_tokens;
	unsigned long tokens_time;
};

extern struct torrent_session torrent_session;
extern pthread_mutex_t torrent_lock;

/* ---------- torrent.c */

unsigned long torrent_now(void);
int torrent_elapsed(unsigned long since, unsigned long milliseconds);
int torrent_reached(unsigned long when);
/* the file, hashed, as a seeding torrent's: the whole file fetched from the
web seeds checked against the info hash (torrent.c) */
void torrent_web_whole_check(struct torrent *torrent);
int torrent_bit_test(const unsigned char *bits, int index);
void torrent_bit_set(unsigned char *bits, int index);
void torrent_bit_clear(unsigned char *bits, int index);
void torrent_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
const char *torrent_address_text(unsigned long address, unsigned short port, char *text);
unsigned long torrent_network_long(unsigned long value);
unsigned short torrent_network_short(unsigned short value);
void torrent_put_long(unsigned char *bytes, unsigned long value);
unsigned long torrent_get_long(const unsigned char *bytes);
void torrent_put_short(unsigned char *bytes, unsigned short value);
unsigned short torrent_get_short(const unsigned char *bytes);
int torrent_would_block(void);
/* a socket of the type, not blocking, bound to port (network order, 0 for
any): -1 on failure */
int torrent_open_socket(int type, unsigned short port, unsigned short *bound_port);
int torrent_random(int bound);
/* the bytes of piece (the last one shorter) */
int torrent_piece_size(const struct torrent *torrent, int piece);
int torrent_has_piece(const struct torrent *torrent, int piece);
/* a peer's address to try (not when known already); 1 if it was new */
int torrent_candidate_add(struct torrent *torrent, unsigned long address, unsigned short port,
	enum torrent_source source);
/* the file's bytes at offset; 1 on success */
int torrent_file_read(struct torrent *torrent, unsigned long long offset, void *buffer, int size);
int torrent_file_write(struct torrent *torrent, unsigned long long offset, const void *buffer, int size);
/* a block that came (from a peer or a web seed): written, and its piece
checked once whole; 1 if it was wanted */
int torrent_block_received(struct torrent *torrent, int piece, int begin, const unsigned char *data, int length);
/* a block to ask a peer for, of a piece it has: 1 and the request, or 0 if
there is none (the peer's bitfield; NULL: a web seed, which has every
piece). The block is marked requested */
int torrent_block_to_request(struct torrent *torrent, const unsigned char *bitfield, int *piece, int *begin,
	int *length);
/* a block asked for that did not come: open to asking again */
void torrent_block_unrequested(struct torrent *torrent, int piece, int begin);
/* a whole piece to fetch (a web seed's), every block of it marked
requested; 1 and the piece, or 0 if there is none to start */
int torrent_piece_to_request_whole(struct torrent *torrent, int *piece);
/* a piece's blocks not received, open to asking again (a web seed's fetch
of it ended) */
void torrent_piece_unrequested(struct torrent *torrent, int piece);
/* whether a peer with this bitfield has a piece this torrent wants */
int torrent_wants_from(const struct torrent *torrent, const unsigned char *bitfield);
/* a piece of the metadata that came from a peer; 1 if it was wanted */
int torrent_metadata_received(struct torrent *torrent, int piece, const unsigned char *data, int size,
	int total_size);
/* the metadata piece to ask a peer for next (-1: none wanted now) */
int torrent_metadata_piece_to_request(struct torrent *torrent);
/* bytes the limits let go now; taken when sent or received */
long torrent_upload_allowance(void);
long torrent_download_allowance(void);
void torrent_uploaded(struct torrent *torrent, long bytes);
void torrent_downloaded(struct torrent *torrent, long bytes);
void torrent_fail(struct torrent *torrent, const char *reason);

/* ---------- torrent_peer.c */

void torrent_peers_tick(struct torrent *torrent);
/* an incoming connection: given to the torrent its handshake names */
void torrent_peer_accept(int socket, unsigned long address, unsigned short port);
/* the sockets the peers wait on (read; write when something is to send) */
void torrent_peers_sockets(int *read, int *read_count, int *write, int *write_count, int maximum);
/* a socket ready: the peer's (1 if it was one's) */
int torrent_peer_socket_ready(int socket, int writable);
void torrent_peer_close(struct torrent *torrent, struct torrent_peer *peer, const char *reason);
void torrent_peers_close_all(struct torrent *torrent);
/* a piece now had: every peer told */
void torrent_peers_have(struct torrent *torrent, int piece);
/* the metadata now known: the peers' bitfields counted, requests begun */
void torrent_peers_metadata_known(struct torrent *torrent);
int torrent_peers_count(const struct torrent *torrent, int *seeds);

/* ---------- torrent_tracker.c */

void torrent_trackers_configure(void);
void torrent_trackers_tick(struct torrent *torrent);
void torrent_trackers_stop(struct torrent *torrent);
/* a datagram on the UDP socket that is a tracker's (not the DHT's) */
void torrent_tracker_udp_received(const unsigned char *data, int size, unsigned long address, unsigned short port);
void torrent_web_seeds_tick(struct torrent *torrent);
void torrent_http_sockets(int *read, int *read_count, int *write, int *write_count, int maximum);
int torrent_http_socket_ready(int socket, int writable);
void torrent_http_close(struct torrent_http *http);
/* a GET of http://host:port/path, with the Range header for first..last
when last >= 0; the body kept whole (stream NULL) or streamed */
int torrent_http_start(struct torrent_http *http, unsigned long address, unsigned short port, const char *host,
	const char *path, long long first, long long last, void (*stream)(void *context, const unsigned char *bytes,
	int size), void *context);
void torrent_http_tick(struct torrent_http *http);
/* host:port (the port 80 when none) and the path of an http:// URL; 0 if
it is not one */
int torrent_url_parse(const char *url, char *host, int host_size, unsigned short *port, char *path, int path_size);
int torrent_http_count(void);

/* ---------- torrent_dht.c */

void torrent_dht_start(void);
void torrent_dht_stop(void);
void torrent_dht_tick(void);
void torrent_dht_received(const unsigned char *data, int size, unsigned long address, unsigned short port);
/* asks the DHT for the torrent's peers (now, then as it sees fit) */
void torrent_dht_lookup(struct torrent *torrent);
int torrent_dht_node_count(void);

#endif
