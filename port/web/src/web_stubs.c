/*
WEB_STUBS.C

What the platform layer links against that a browser does not have:
internet play's router (UPnP, port/linux/src/posix_upnp.c), which stays off
in the web build (port/web/src/web_main.c), and the self-updater's HTTPS
downloads (posix_update.c), which the map archives' fetch asks for
(map_archives.c): none here.
*/

#include <string.h>

#include "posix.h"

int posix_upnp_forward_udp(unsigned short port, unsigned short preferred_port, posix_ulong *external_address,
	unsigned short *external_port, char *error, int error_size)
{
	(void)port;
	(void)preferred_port;
	(void)external_address;
	(void)external_port;
	if (error && error_size > 0)
	{
		strncpy(error, "a browser cannot reach the router", (size_t)error_size - 1);
		error[error_size - 1] = '\0';
	}
	return 0;
}

void posix_upnp_stop_forwarding_udp(unsigned short external_port)
{
	(void)external_port;
}

/* ---------- the self-updater's downloads (update.h): none in a page */

typedef void (*update_progress_proc)(void *context, unsigned long long received, unsigned long long total);

int update_request(const char *url, const char *form, const char *agent, const volatile int *stop,
	const char *path, update_progress_proc progress, void *context, char *error, int error_size)
{
	(void)url;
	(void)form;
	(void)agent;
	(void)stop;
	(void)path;
	(void)progress;
	(void)context;
	if (error && error_size > 0)
	{
		strncpy(error, "a page has no HTTPS download of its own", (size_t)error_size - 1);
		error[error_size - 1] = '\0';
	}
	return 0;
}

/* ---------- MSVC (scenario.c): a compiler barrier, an intrinsic clang knows
only on x86 and ARM */

void _ReadWriteBarrier(void)
{
	__atomic_signal_fence(__ATOMIC_SEQ_CST);
}
