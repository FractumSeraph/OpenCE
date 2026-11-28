/*
FORK_DEVELOPERS.H

Fractum Halo's own developers: the Delta handles (on this build's home
network, chupathingy.org) whose players get the verified developer's blue
flame in this fork's builds (port/linux/game/developer_flames.c), as
ChupathingyCE's developer mark gives it (developer_flames_rules.h).

ChupathingyCE's mark is that network's to give, to its own developers; a
fork can't sign one. So this copy treats a handle on this list as if its
certificate carried the mark, and nothing more: every other rule of
developer_flames_card_shows still holds (a card this copy checked, a bound
proof, this network's certificate, shown by its verified name, not a
look-alike, the viewer's display.developer_flames on). The handle is the one
the network signed into the certificate, so it can't be claimed by typing a
name. Looks only: no role, permission or capability comes with it.

Only this fork's builds draw it; ChupathingyCE's and OpenCE's don't know the
list. Header only, so tools/test_developer_flames.py tests it anywhere.
*/

#ifndef FORK_DEVELOPERS_H
#define FORK_DEVELOPERS_H

#include <string.h>

/* lowercase a-z 0-9 _ -, as a certificate carries them */
static const char *const fork_developer_handles[] =
{
	"fractumseraph",
};

/* whether a verified handle is one of the fork's developers' */
static inline int fork_developer_handle(const char *handle)
{
	size_t index;

	if (!handle || !handle[0])
		return 0;
	for (index = 0; index < sizeof(fork_developer_handles) / sizeof(fork_developer_handles[0]); index++)
	{
		if (!strcmp(handle, fork_developer_handles[index]))
			return 1;
	}
	return 0;
}

#endif
