/* UPnP for the browser build: there is none. A web page cannot talk to the
 * router (posix_upnp.c and miniupnpc are left out of the build); browser
 * players reach each other through WebRTC instead. p2p.c then behaves as on
 * a network whose router has no UPnP. */

#include "posix.h"

#include <stdio.h>

int posix_upnp_forward_udp(unsigned short port, unsigned short preferred_port, posix_ulong *external_address,
	unsigned short *external_port, char *error, int error_size)
{
	(void)port;
	(void)preferred_port;
	(void)external_address;
	(void)external_port;
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "UPnP is not available in a browser");
	return 0;
}

void posix_upnp_stop_forwarding_udp(unsigned short external_port)
{
	(void)external_port;
}
