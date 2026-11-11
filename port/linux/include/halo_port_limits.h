/*
HALO_PORT_LIMITS.H

Multiplayer session limits of the native builds (Windows, Linux, Android),
force-included by halo_linux_prefix.h and halo_windows_prefix.h.

The Xbox game allows 16 players on at most 4 machines (up to 4 players each
on split screen). The native builds allow 128 players on up to 128
machines; split screen stays at 4 players per machine.

128 is the largest session that fits the game's existing records: player,
machine and team indices are stored in signed chars (0..127 with NONE), and
a finishing place in 7 bits.
*/

#ifndef __HALO_PORT_LIMITS_H
#define __HALO_PORT_LIMITS_H

/* what a console build asks of the shared code (its capabilities) */
#include "halo_console_features.h"

/* ---------- session limits */

/* A console's build (HALO_CONSOLE_PLAYERS, the original Xbox's) stays the
Xbox: 16 players, though on as many machines as players, one each, as the
native builds' players come. HALO_CONSOLE_LIMITS gives a desktop build the
console's limits, to test the console's side of cross-play against the PC
builds on one computer (configure.py --console-limits); it is never a
release. */
#if defined(HALO_CONSOLE_PLAYERS) || defined(HALO_CONSOLE_LIMITS)
#define HALO_PORT_CONSOLE_LIMITS 1
#ifdef HALO_CONSOLE_PLAYERS
/* HALO_CONSOLE_PLAYERS (configure.py --console-players) is the players the
console's build takes: 16, the Xbox's, unless a test build asks for 32 or 64
(which a stock console's memory does not hold: the build says so when it
starts). The tables of the players, the machines and the tunnel's peers
(P2P_MAXIMUM_PEERS, p2p_internal.h) are sized by it; a host's game with more
is left as a full game is (network_client_message_handler.c). */
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS HALO_CONSOLE_PLAYERS
/* one more machine than players: a dedicated host is machine #0, so 16
players are machines #1 to #16 */
#define HALO_PORT_MAXIMUM_NETWORK_MACHINES (HALO_CONSOLE_PLAYERS + 1)
#else
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 16
#define HALO_PORT_MAXIMUM_NETWORK_MACHINES 16
#endif
#else
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 128
#define HALO_PORT_MAXIMUM_NETWORK_MACHINES 128
#endif

/* the slots on the network: the PC builds' (128 and 128 since network
version 11; network version 28 keeps them), which never change; a build with the console's limits folds the PCs' game settings
record and game type states into its own slots as they arrive
(port/linux/game/network_game_layout.c) */
#define HALO_PORT_WIRE_NETWORK_PLAYERS 128
#define HALO_PORT_WIRE_NETWORK_MACHINES 128

/* a host polls its listening socket and one socket per machine; the Xbox's
Winsock headers default to 64 (the prefix headers define FD_SETSIZE from
this before any of them is read) */
#define HALO_PORT_FD_SETSIZE 256

/* ---------- memory capacity for these limits (game state and pools) */

#include "halo_port_capacity.h"

/* ---------- the desktop extras a console's build leaves out */

#include "halo_port_features.h"

/* ---------- struct network_game layout

The game settings record (struct network_game) is declared separately in
several networking and interface units; its layout follows from the limits.
Every copy checks its offsets against these values. The Xbox values (4
machines, 16 players) are 0x226 and 0x434. */

#define HALO_PORT_NETWORK_MACHINE_SIZE 0x44
#define HALO_PORT_NETWORK_PLAYER_SIZE 0x20
#define HALO_PORT_NETWORK_GAME_MACHINES_OFFSET 0x114
#define HALO_PORT_NETWORK_GAME_PLAYER_COUNT_OFFSET \
	(HALO_PORT_NETWORK_GAME_MACHINES_OFFSET + HALO_PORT_MAXIMUM_NETWORK_MACHINES * HALO_PORT_NETWORK_MACHINE_SIZE)
#define HALO_PORT_NETWORK_GAME_PLAYERS_OFFSET (HALO_PORT_NETWORK_GAME_PLAYER_COUNT_OFFSET + 2)
#define HALO_PORT_NETWORK_GAME_PLAYERS_END \
	(HALO_PORT_NETWORK_GAME_PLAYERS_OFFSET + HALO_PORT_MAXIMUM_NETWORK_PLAYERS * HALO_PORT_NETWORK_PLAYER_SIZE)
#define HALO_PORT_NETWORK_GAME_RANDOM_SEED_OFFSET (HALO_PORT_NETWORK_GAME_PLAYERS_END + 2)
/* (then the gametype's PC options, struct game_variant_options: 0x2C bytes) */
#define HALO_PORT_NETWORK_GAME_VARIANT_OPTIONS_OFFSET (HALO_PORT_NETWORK_GAME_PLAYERS_END + 0xA)
#define HALO_PORT_NETWORK_GAME_LOCAL_DATA_OFFSET (HALO_PORT_NETWORK_GAME_PLAYERS_END + 0x36)
#define HALO_PORT_NETWORK_GAME_SIZE (HALO_PORT_NETWORK_GAME_PLAYERS_END + 0x3A + 64) /* MCC content and metadata SHA-256 */
/* ... at the network's slots (13,200 bytes) */
#define HALO_PORT_WIRE_NETWORK_GAME_SIZE (HALO_PORT_NETWORK_GAME_MACHINES_OFFSET + \
	HALO_PORT_WIRE_NETWORK_MACHINES * HALO_PORT_NETWORK_MACHINE_SIZE + 2 + \
	HALO_PORT_WIRE_NETWORK_PLAYERS * HALO_PORT_NETWORK_PLAYER_SIZE + 0x3A + 64)
/* ... and a host's of network versions 11 to 34, whose PC options were their
first 0x1C bytes (game_engine.h's GAME_VARIANT_OPTIONS_V1_SIZE): its record
is 16 bytes shorter, which a client of such a host widens to this one as it
arrives (network_game_layout_from_version, network_client_message_handler.c) */
#define HALO_PORT_LEGACY_OPTIONS_SHORTFALL 0x10
/* ... and a host's of network versions before 42, which had no MCC digests
(the record's last 64 bytes, zero for Xbox and Custom Edition maps): its
record is 64 bytes shorter, widened with zeros the same way, and a host sends
the shorter record to a machine of such a version (port: this fork) */
#define HALO_PORT_LEGACY_MCC_SHORTFALL 64

/* ---------- system link protocol

The native builds' messages differ from the Xbox game's (longer arrays, the
game settings record in fragments), so they search for games with their own
protocol version and never see the Xbox game's, or it theirs. */

#define HALO_PORT_NETWORK_GAME_MESSAGE_VERSION 2

/* The native builds' network code has a version of its own (an unsigned
16-bit number): machines of different versions cannot play together, and a
client does not join a host of another version, but tells the player which
is newer (network_client_manager.c). A host advertises it, with its netcode,
in its game's advertisement's reserved bytes (network_server_message_handler.c),
which hosts built before there was a version send as zeros: version 0.
Raise it with any change to what the machines send each other.
Version 42 integrates MCC admission/sharing and shared checkpoint, boat,
clock and scripted-pose agreement. Every map family uses the same version.
Version 43: an MCC map's damage impulses follow the damage's direction, a
vehicle on its side throws its riders, chat marks the host's lines, a co-op
cutscene's sound carries how far the host has played it, and a machine's
MCC conversion holds a round's start out of an allowance; a player standing
on a vehicle rides it (a unit's state carries the deck it stands on, the map's
riding choice has its own message: port/linux/NETCODE.md).
port (this fork): 43, OpenCE's newest, which its hosts announce (OpenCE's
clients join only their own number), its join request 42's (40's pass and
proof after the hardware id, then the version) and its votes 41's
(network_votekick.c, network_map_vote.c), speaking the wire chupa-43a,
its game settings record of 35's layout (OpenCE's game types); ChupathingyCE's
hosts announce 32. Delta clients (ChupathingyCE's 0.8.0e to 0.8.0h, this
fork's) join a host whose wire plays with theirs (chupa-38a, from chupa-28a,
delta.h) whatever number it announces, and are sent the record of the older
of their HELLO's version and this one (network_server_message_handler.c). */
#define HALO_PORT_NETWORK_VERSION 43
/* ... the versions whose hosts a client joins: its own, and those that differ
from it only in what the other machines leave out (a message a machine of
the other version does not know it drops). Which are which is delta.h's
table (DELTA_LEGACY_VERSIONS): the minimum is its newest breaking version,
the maximum its newest and the next (DELTA_LEGACY_PRESUMED): hosts of
OpenCE's next versions are joined before a release follows them, as most of its raises leave the game's
messages as they were (tools/test_delta.py checks them; they stay numbers
here, which the command repository reads). A host
never checks a client's version: the client does (network_client_manager.c),
so the range is the client's. */
#define HALO_PORT_NETWORK_VERSION_MINIMUM 11
#define HALO_PORT_NETWORK_VERSION_MAXIMUM 44
/* ... the numbers in use (port/linux/src/delta.c): the three above, until a
legacy table (docs/delta.md) widens them; code asks these, not the numbers */
int delta_legacy_announce(void);
int delta_legacy_minimum(void);
int delta_legacy_maximum(void);
/* ... what a client converts of an older host's (keyed on network_game_client_
host_network_version, network_client_manager.c, by network_rules_of_version;
a host not known plays by the older rules, until its game settings record
shows it is of 35 or later): before 33, no ALLY FRIENDLY FIRE bit in the game
settings' cooperative_flags (on, as the campaign has it); before 34, the
scenery and devices whose shield alone damage breaks are not static targets
(network_damage.c, network_rules.h's NETWORK_RULES_STATIC_SHIELDS_VERSION);
before 35, the game settings record's shorter PC options (no OpenCE
gametype); before 36, no shield effects message (the stock ones); before
37, no game kind in a LAN advertisement's fourth reserved byte or an internet
listing's stamp (a Delta host of the old layout has its major there and its
wire after it, so a kind is read only from a host without them); before 38,
no "players" shield effects (a client of 36 or 37 takes it for none it knows
and draws the stock ones); before 39, no dedicated server flag (0x04 of the
advertisement's flags, which is also the old layout's DELTA_ADVERTISED_FLAG:
this fork's client takes it for the dedicated flag from a host of 39 or
later with Delta's tag at the tail or without the old layout's wire,
network_client_manager.c) and no client build message (92, which an older
host drops) */
#define HALO_PORT_ALLY_FRIENDLY_FIRE_NETWORK_VERSION 33
#define HALO_PORT_SHIELD_TARGETS_NETWORK_VERSION 34
#define HALO_PORT_GAME_MODES_NETWORK_VERSION 35
#define HALO_PORT_SHIELD_EFFECTS_NETWORK_VERSION 36
#define HALO_PORT_GAME_KIND_NETWORK_VERSION 37
#define HALO_PORT_PLAYERS_SHIELD_EFFECTS_NETWORK_VERSION 38
#define HALO_PORT_DEDICATED_NETWORK_VERSION 39
/* ... and before 41, the vote messages (78, 79) in their older layout, the
first byte the player's index, not the vote's kind (network_votekick.c): this
fork, of 41, reads none from a host of an older version, nor a client whose
Delta HELLO says one (network_distributed.c), rather than misread them */
#define HALO_PORT_VOTES_NETWORK_VERSION 41
/* ... and before 42 (OpenCE's first breaking raise this fork plays both
sides of, as a client): a distributed message's header without the
checkpoint epoch (8 bytes, not 12), the game settings record without the MCC
digests (HALO_PORT_LEGACY_MCC_SHORTFALL), the join request without its last
two bytes (the version), no MCC maps or messages (96 to 106); and before 43,
no deck or scripted pose parts in a unit's state, no riding message (107),
a vehicle throws its riders only upside down, chat lines and settings without
the host's bits, and a co-op cutscene sound's target as before. A host of
this fork plays 43 alone and refuses a Delta client whose HELLO says an
older version (network_rules_session_version, network_rules.h) */
#define HALO_PORT_MCC_NETWORK_VERSION 42
/* ... the oldest version this build's hosts play (42's header, with the
checkpoint epoch, to every machine): network.announce may not pin an older
one (delta.c), and a Delta machine whose HELLO says an older one is refused
(network_server_message_handler.c, network_game_server_settings_refused) */
#define HALO_PORT_HOST_NETWORK_VERSION_MINIMUM 42
#define HALO_PORT_RIDING_NETWORK_VERSION 43
/* ... and whether a game this machine hosts is under way, which holds the
number announced until its end (delta.h, auto-announce) */
void delta_legacy_hold(int hold);
/* ... the advertisement's reserved bytes: the version (a little-endian word),
then flags, then (version 37) the game's kind: an OpenCE gametype's mode in
the low seven bits, Hardcore in bit 7 (port/linux/game/game_modes.h's
game_modes_kind; port: this fork plays them and sends it). A Delta host's
own bytes are at the tail (delta.h, DELTA_ADVERTISED_TAG_OFFSET). */
#define HALO_PORT_ADVERTISED_VERSION_OFFSET 0
#define HALO_PORT_ADVERTISED_FLAGS_OFFSET 2
#define HALO_PORT_ADVERTISED_KIND_OFFSET 3
/* ... the host plays the distributed netcode (always, since the lockstep
netcode was removed; hosts of version 4 built before then may not) */
#define HALO_PORT_ADVERTISED_DISTRIBUTED_FLAG 0x01
/* ... the game is under way (loading, playing or over), not in its lobby:
the menus show it before joining it (hosts built before then never set it) */
#define HALO_PORT_ADVERTISED_IN_PROGRESS_FLAG 0x02
/* ... a dedicated server's game (version 39), with no player of its own
(halo --server: port/linux/src/server_mode.c; port, this fork: and
server/src/dedicated.c's, delta_peer_advertised_flags): the menus say so.
Hosts of Delta's old layout set 0x04 for Delta (delta.h's
DELTA_ADVERTISED_FLAG) */
#define HALO_PORT_ADVERTISED_DEDICATED_FLAG 0x04
#define HALO_PORT_ADVERTISED_DEDICATED_VERSION 39

/* a message header's 12-bit length allows messages of up to 0xFFF bytes,
header included; the per-tick update of 128 players is 3,857 */
#define HALO_PORT_MAXIMUM_NETWORK_MESSAGE_SIZE 0x1000

/* the packet codec's limit on a decoded or encoded packet; the per-tick
update of 128 players decodes to 0x1010 bytes */
#define HALO_PORT_NETWORK_PACKET_SIZE 0x1100

/* the game settings record (HALO_PORT_NETWORK_GAME_SIZE, 13,200 bytes at 128
machines and players) does not fit one message; it is sent in pieces of
this many bytes (4 pieces), each 3,594 bytes on the wire */
#define HALO_PORT_NETWORK_GAME_SETTINGS_FRAGMENT_SIZE 0xE00

#endif /* __HALO_PORT_LIMITS_H */
