/* Delta Peer in the browser (ChupathingyCE's docs/delta.md, "Delta Peer";
src/delta/README.md here): the client's side alone, as their
delta_peer_game.c has it, over a UDP socket of the game's own Winsock layer
(xnet.c: in the browser, the virtual sockets of web_loopback_net.c), so it
goes where the game's datagrams go: to a ChupathingyCE host through the
native gateway's invite tunnel, which carries any port.

A browser joins a ChupathingyCE host the game's way (unchanged); if the
host's advertisement has Delta's flag, the client says HELLO on its Delta
port (5160) and the host answers WELCOME, or nothing (then the legacy
protocol alone, after 4 seconds; the game never waits for any of it). The
sessions are ChupathingyCE's own code (delta/delta_peer.c and
delta/delta_wire.c, as they are). What the browser offers:
- platform: its platform key, which says "unknown" (Delta's registry has no
  browser) and that it hosts no Delta games;
- ce_maps: the host says its Custom Edition map's file size and BLAKE2b-256
  hash, and a browser whose copy (the site's custom_maps, which the site's
  server hashed: map-hashes.mjs, index.json) differs leaves rather than play
  another map;
- moderation: a dedicated server's warnings and notices (MOD_NOTICE) are
  shown on the HUD. The browser has no moderator key: it never signs in.
Not offered: profile (no player ID is shared). The signed legacy table's
kill switch (web_delta.c) turns any of these off. Nothing a host says
changes the game but leaving on a map that is not the host's. */

#include "platform.h"
#include "posix.h"
#include "halo_port_limits.h"
#include "delta/delta.h"
#include "delta/delta_peer.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

/* the game's: a network game left at the next frame (network_game_globals.c)
and an error the main menu shows next (ui_widget.c; the game's 16-bit
wchar_t); web_platform.c's custom_maps index */
void network_game_abort(void);
void display_error_text_when_main_menu_loaded(const unsigned short *text);
int web_custom_map_identity(const char *file_name, unsigned long long *size, unsigned char *hash);
/* the game's Winsock layer (xnet.c; halo_linux_winsock_names.h names them) */
SOCKET WSAAPI halo_ws_socket(int family, int type, int protocol);
int WSAAPI halo_ws_closesocket(SOCKET socket);
int WSAAPI halo_ws_bind(SOCKET socket, const struct sockaddr *address, int address_length);
int WSAAPI halo_ws_sendto(SOCKET socket, const char *buffer, int length, int flags, const struct sockaddr *address,
	int address_length);
int WSAAPI halo_ws_recvfrom(SOCKET socket, char *buffer, int length, int flags, struct sockaddr *address,
	int *address_length);
int WSAAPI halo_ws_ioctlsocket(SOCKET socket, long command, u_long *argument);
int WSAAPI halo_ws_getsockname(SOCKET socket, struct sockaddr *address, int *address_length);
u_short WSAAPI halo_ws_htons(u_short value);
u_short WSAAPI halo_ws_ntohs(u_short value);

static struct
{
	int ready;
	struct delta_peer peer;
	/* joined a host: the session is a client's */
	int client;
	SOCKET socket;
	/* the socket's port (host order; 0: none open) */
	unsigned short port;
	/* the host's map (MAP) last checked, by its number (delta_peer_host_map) */
	delta_u32 map_checked;
	/* the host's notices shown so far */
	delta_u32 notices_shown;
	int state_said;
} web_delta = { 0 };

/* ---------- the session's needs */

static void web_send(void *context, delta_u32 ipv4, unsigned short port, const unsigned char *data, int size)
{
	struct sockaddr_in address;

	(void)context;
	if (!web_delta.port)
		return;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = halo_ws_htons(port);
	address.sin_addr.s_addr = (unsigned long)ipv4;
	/* (lost as a datagram may be: the handshake says HELLO again) */
	halo_ws_sendto(web_delta.socket, (const char *)data, size, 0, (const struct sockaddr *)&address, sizeof(address));
}

static void web_log(void *context, const char *text)
{
	(void)context;
	platform_log("%s", text);
}

static delta_u32 web_random(void *context)
{
	delta_u32 value = 0;

	(void)context;
	posix_random_bytes(&value, sizeof(value));
	return value;
}

static int web_capability_disabled(void *context, int capability)
{
	(void)context;
	return delta_capability_disabled(capability);
}

/* the legacy table's relay: web_delta.c's (checked there whatever its
source) */
static delta_u32 web_legacy_table_serial(void *context)
{
	(void)context;
	return delta_legacy_relay() ? delta_legacy_serial() : DELTA_WIRE_TABLE_NONE;
}

static int web_legacy_table_signed(void *context, unsigned char *buffer, int size)
{
	(void)context;
	return delta_legacy_relay() ? delta_legacy_signed((char *)buffer, size) : 0;
}

static int web_legacy_table_offer(void *context, const unsigned char *table, int size)
{
	(void)context;
	return delta_legacy_relay() && delta_legacy_offer((const char *)table, size);
}

static void web_delta_ready(void)
{
	struct delta_peer_env env;
	struct delta_peer_local local;

	if (web_delta.ready)
		return;
	web_delta.ready = 1;
	web_delta.socket = INVALID_SOCKET;
	memset(&env, 0, sizeof(env));
	env.send_datagram = web_send;
	env.log_line = web_log;
	env.random_number = web_random;
	env.legacy_table_serial = web_legacy_table_serial;
	env.legacy_table_signed = web_legacy_table_signed;
	env.legacy_table_offer = web_legacy_table_offer;
	env.capability_disabled = web_capability_disabled;
	memset(&local, 0, sizeof(local));
	local.capabilities = (delta_u32)1 << _delta_capability_platform | (delta_u32)1 << _delta_capability_ce_maps |
		(delta_u32)1 << _delta_capability_moderation;
	local.legacy_version = HALO_PORT_NETWORK_VERSION;
	/* (the platform policy's row for an unknown platform; a browser hosts
	no Delta game: its own rooms are the site's) */
	delta_platform_policy_default(_delta_platform_unknown, &local.key);
	local.key.platform = _delta_platform_unknown;
	local.key.version = DELTA_PLATFORM_KEY_VERSION;
	local.key.flags = 0;
	local.key.host_players = 0;
	snprintf(local.build, sizeof(local.build), "OpenCE web %d", HALO_PORT_NETWORK_VERSION);
	delta_peer_initialize(&web_delta.peer, &env, &local);
}

/* ---------- the socket */

static void close_socket(void)
{
	if (web_delta.socket != INVALID_SOCKET)
		halo_ws_closesocket(web_delta.socket);
	web_delta.socket = INVALID_SOCKET;
	web_delta.port = 0;
}

/* a socket on a port the system picks: 1 if it is open */
static int open_socket(void)
{
	struct sockaddr_in address;
	int length = sizeof(address);
	u_long nonblocking = 1;

	if (web_delta.socket != INVALID_SOCKET)
		return 1;
	web_delta.socket = halo_ws_socket(AF_INET, SOCK_DGRAM, 0);
	if (web_delta.socket == INVALID_SOCKET)
		return 0;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	if (halo_ws_ioctlsocket(web_delta.socket, FIONBIO, &nonblocking) == SOCKET_ERROR ||
		halo_ws_bind(web_delta.socket, (const struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR ||
		halo_ws_getsockname(web_delta.socket, (struct sockaddr *)&address, &length) == SOCKET_ERROR)
	{
		close_socket();
		return 0;
	}
	web_delta.port = halo_ws_ntohs(address.sin_port);
	return web_delta.port != 0;
}

static void receive(void)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM + 1];
	int count;

	for (count = 0; count < DELTA_PEER_FRAME_DATAGRAMS && web_delta.socket != INVALID_SOCKET; count++)
	{
		struct sockaddr_in from;
		int length = sizeof(from);
		int size = halo_ws_recvfrom(web_delta.socket, (char *)data, (int)sizeof(data), 0, (struct sockaddr *)&from, &length);

		if (size == SOCKET_ERROR)
			break;
		if (length < (int)sizeof(from) || from.sin_family != AF_INET)
			continue;
		/* (one too big is dropped by the session: read as one byte more) */
		delta_peer_receive(&web_delta.peer, GetTickCount(), (delta_u32)from.sin_addr.s_addr, halo_ws_ntohs(from.sin_port),
			data, size);
	}
}

/* ---------- Custom Edition maps' identity (ce_maps) */

/* the host's Custom Edition map checked against the site's file of that
name, once each time MAP says another. One that differs is no game to play
(its objects would not be the host's): the player is told, and the game is
left. One the site has not, or has not hashed yet, is the game's own join to
judge (it says which map is missing) */
static void client_map(void)
{
	struct delta_wire_map theirs;
	delta_u32 generation = delta_peer_host_map(&web_delta.peer, &theirs);
	char file_name[DELTA_WIRE_MAP_NAME_SIZE + 8];
	unsigned long long size, their_size;
	unsigned char hash[DELTA_WIRE_MAP_HASH_SIZE];

	if (!generation || generation == web_delta.map_checked)
		return;
	web_delta.map_checked = generation;
	/* (the Xbox's maps are every copy's; HaloMD's and Halo PC's the site has
	none of) */
	if (theirs.family != 1 || !(theirs.flags & DELTA_WIRE_MAP_HASHED))
		return;
	snprintf(file_name, sizeof(file_name), "%s.map", theirs.name);
	if (!web_custom_map_identity(file_name, &size, hash))
	{
		platform_log("Delta Peer: map identity: the host plays %s, which this site has not hashed", file_name);
		return;
	}
	their_size = (unsigned long long)theirs.size_high << 32 | theirs.size_low;
	if (size == their_size && !memcmp(hash, theirs.hash, sizeof(hash)))
	{
		platform_log("Delta Peer: map identity: %s is the host's (size and hash match)", file_name);
		return;
	}
	{
		char message[256];
		unsigned short text[256];
		int index;

		platform_log("Delta Peer: map identity: this site's %s differs from the host's (%llu bytes here, %llu "
			"there): leaving the game", file_name, size, their_size);
		snprintf(message, sizeof(message),
			"The host plays another version of %s than this site has: the two files differ, so the game was left.",
			file_name);
		for (index = 0; message[index] && index < (int)(sizeof(text) / sizeof(text[0])) - 1; index++)
			text[index] = (unsigned short)(unsigned char)message[index];
		text[index] = 0;
		display_error_text_when_main_menu_loaded(text);
		network_game_abort();
	}
}

static void stop(void)
{
	if (web_delta.client)
		delta_peer_stop(&web_delta.peer);
	close_socket();
	web_delta.client = 0;
	web_delta.map_checked = 0;
	web_delta.notices_shown = 0;
	web_delta.state_said = 0;
}

/* ---------- the hooks (network_client_manager.c, HALO_WEB) */

void web_delta_peer_client_frame(int joined, unsigned int host_ipv4, int host_speaks_delta, int machine_index,
	const signed char *player_machines)
{
	web_delta_ready();
	if (!joined)
	{
		if (web_delta.client)
			stop();
		return;
	}
	web_delta.client = 1;
	if (host_speaks_delta && !open_socket())
		host_speaks_delta = 0;
	receive();
	delta_peer_client_frame(&web_delta.peer, GetTickCount(), joined, (delta_u32)host_ipv4, DELTA_PEER_PORT,
		host_speaks_delta, (unsigned char)(machine_index >= 0 && machine_index < DELTA_PEER_MAXIMUM_MACHINES ?
			machine_index : DELTA_WIRE_NO_MACHINE), player_machines);
	if (!web_delta.state_said && web_delta.peer.client_state >= _delta_peer_client_delta)
	{
		platform_log("Delta Peer: %s", web_delta.peer.client_state == _delta_peer_client_delta ?
			"the host speaks Delta" : "the legacy protocol alone with this host");
		web_delta.state_said = 1;
	}
	client_map();
}

void web_delta_peer_stop(void)
{
	if (web_delta.ready)
		stop();
}

/* a host's notice (a dedicated server's warning) not shown yet, as text
for the HUD (the game's 16-bit characters): 1 if there is one */
int web_delta_peer_take_notice(unsigned short *text, int size)
{
	const struct delta_peer_client_moderation *moderation = &web_delta.peer.client_moderation;
	int index;

	if (!web_delta.ready || !web_delta.client || size <= 0 || moderation->notice_count == web_delta.notices_shown)
		return 0;
	web_delta.notices_shown = moderation->notice_count;
	index = 0;
	if (moderation->notice.kind == _delta_moderation_notice_warning)
	{
		static const char warning[] = "Warning: ";

		for (; warning[index] && index < size - 1; index++)
			text[index] = (unsigned char)warning[index];
	}
	{
		const char *from = moderation->notice.text;

		for (; *from && index < size - 1; from++)
			text[index++] = (unsigned char)*from;
	}
	text[index] = 0;
	platform_log("Delta Peer: the host's notice: %s", moderation->notice.text);
	return 1;
}

/* the client's handshake (enum delta_peer_client_state) */
int web_delta_peer_client_state(void)
{
	return web_delta.ready && web_delta.client ? web_delta.peer.client_state : _delta_peer_client_off;
}
