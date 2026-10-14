/*
SHARED_BANS.H

Port (this fork): ban lists that other hosts publish, which a host takes
beside its own bans.txt (shared_bans.c; server/docs/admin.md, "Shared ban
lists"). Plain types only: the game's units and the platform's both call it.
*/

#ifndef __SHARED_BANS_H
#define __SHARED_BANS_H

#include <stddef.h>

/* where the lists' bans are kept, as the game names its files (a ban line
each, as bans.txt has them: network_distributed.c reads both) */
#define SHARED_BANS_FILE "d:\\shared_bans.txt"

/* at start (sdl_platform.c, the dedicated server's server_platform.c): the
lists of network.shared_bans fetched now and every few minutes after, on a
thread of their own */
void shared_bans_start(void);

/* a list's text (size bytes; not NUL terminated) to the ban lines kept of it,
into out (out_size bytes, NUL terminated): each line of bans.txt's form
(tab separated fields, with a hardware id, hwid=, or an address, ip=) of
printable characters, no longer than a line bans.txt keeps, with
"\tlist=<source>" after it; the rest (comments, blank lines, anything else)
left out. Returns the bytes written, without the NUL */
size_t shared_bans_filter(const char *text, size_t size, const char *source, char *out, size_t out_size);

#endif
