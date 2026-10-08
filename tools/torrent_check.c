/*
TORRENT_CHECK.C

A check of the map torrents' BitTorrent client (port/linux/src/torrent.c)
outside the game: it seeds a file, or downloads one from a peer given, as
the game would a map, and says how it goes. tools/test_linux_port.py runs
two of these against each other on the loopback; by hand:

    torrent_check seed <file> <info hash> <piece length> <port>
    torrent_check get <name> <size> <info hash> <piece length> <peer ip:port> <path> [<seconds>]
        [--trackers udp://...] [--dht]

built with the client, the platform layer's POSIX files and this file's
own stand-ins for the platform (platform_log and the like):
tools/test_linux_port.py gives the command.
*/

#include "platform.h"
#include "posix.h"
#include "torrent.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's, for the client */
void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	fprintf(stderr, "torrent_check: ");
	vfprintf(stderr, format, arguments);
	fprintf(stderr, "\n");
	va_end(arguments);
}

#ifdef _WIN32
/* the Xbox kernel's clock and sleep, which the game's platform layer
gives (xbox_kernel.c) and this tool does not build: the Windows ones */
#undef GetTickCount
#undef Sleep
__declspec(dllimport) unsigned long __stdcall GetTickCount(void);
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);
unsigned long __stdcall halo_xbox_GetTickCount(void)
{
	return GetTickCount();
}
void __stdcall halo_xbox_Sleep(unsigned long milliseconds)
{
	Sleep(milliseconds);
}
/* win32_posix.c's start-up asks whether this is the crash reporter */
int crash_reporter_process(void)
{
	return 0;
}
static void sleep_milliseconds(int milliseconds)
{
	Sleep((unsigned long)milliseconds);
}
#else
#include <time.h>
DWORD WINAPI GetTickCount(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}
static void sleep_milliseconds(int milliseconds)
{
	struct timespec wait = { milliseconds / 1000, (milliseconds % 1000) * 1000000L };

	nanosleep(&wait, NULL);
}
#endif

static const char *state_names[] = { "metadata", "checking", "downloading", "seeding", "failed" };

static void report(int handle)
{
	struct torrent_status status;

	if (!torrent_status_get(handle, &status))
	{
		printf("no such torrent\n");
		return;
	}
	printf("%s: %llu of %llu bytes, %d peers (%d seeds), down %ld up %ld B/s, uploaded %llu: %s\n",
		state_names[status.state], status.have_bytes, status.total_bytes, status.peers_connected,
		status.seeds_connected, status.download_rate, status.upload_rate, status.uploaded_bytes, status.detail);
	fflush(stdout);
}

static unsigned long parse_address(const char *text, unsigned short *port)
{
	char host[128];
	const char *colon = strrchr(text, ':');
	unsigned long address;

	if (!colon)
		return 0;
	snprintf(host, sizeof(host), "%.*s", (int)(colon - text), text);
	*port = (unsigned short)atoi(colon + 1);
	address = posix_resolve_ipv4(host);
	*port = (unsigned short)((*port << 8) | (*port >> 8));
	return address;
}

int main(int argc, char **argv)
{
	struct torrent_settings settings;
	char error[128];
	int handle;
	int index;
	long upload_limit = 0;

	memset(&settings, 0, sizeof(settings));
	settings.trackers = "";
	settings.web_seeds = "";
	settings.maximum_peers = 20;
	for (index = 1; index < argc; index++)
	{
		if (!strcmp(argv[index], "--trackers") && index + 1 < argc)
			settings.trackers = argv[++index];
		else if (!strcmp(argv[index], "--web-seeds") && index + 1 < argc)
			settings.web_seeds = argv[++index];
		else if (!strcmp(argv[index], "--dht"))
			settings.dht = 1;
		else if (!strcmp(argv[index], "--upload-limit") && index + 1 < argc)
			upload_limit = atol(argv[++index]);
	}
	settings.upload_limit = upload_limit;
	if (argc >= 6 && !strcmp(argv[1], "seed"))
	{
		settings.port = atoi(argv[5]);
		if (!torrent_start(&settings, error, sizeof(error)))
		{
			printf("cannot start: %s\n", error);
			return 1;
		}
		printf("port %d\n", torrent_port());
		fflush(stdout);
		handle = torrent_add(argv[3], strrchr(argv[2], '/') ? strrchr(argv[2], '/') + 1 :
			strrchr(argv[2], '\\') ? strrchr(argv[2], '\\') + 1 : argv[2], 0, (unsigned long)atol(argv[4]), argv[2], 1,
			error, sizeof(error));
		if (handle < 0)
		{
			/* (the size: the file's) */
			struct posix_file_information information;

			if (posix_stat(argv[2], &information) != 0)
			{
				printf("cannot stat %s\n", argv[2]);
				return 1;
			}
			handle = torrent_add(argv[3], strrchr(argv[2], '/') ? strrchr(argv[2], '/') + 1 :
				strrchr(argv[2], '\\') ? strrchr(argv[2], '\\') + 1 : argv[2],
				(unsigned long long)information.size_high << 32 | information.size_low, (unsigned long)atol(argv[4]),
				argv[2], 1, error, sizeof(error));
		}
		if (handle < 0)
		{
			printf("cannot add: %s\n", error);
			return 1;
		}
		for (;;)
		{
			report(handle);
			sleep_milliseconds(2000);
		}
	}
	if (argc >= 8 && !strcmp(argv[1], "get"))
	{
		unsigned short port = 0;
		unsigned long address = parse_address(argv[6], &port);
		int seconds = argc >= 9 && argv[8][0] != '-' ? atoi(argv[8]) : 120;
		int elapsed;

		settings.port = 0;
		if (!torrent_start(&settings, error, sizeof(error)))
		{
			printf("cannot start: %s\n", error);
			return 1;
		}
		handle = torrent_add(argv[4], argv[2], strtoull(argv[3], NULL, 10), (unsigned long)atol(argv[5]), argv[7], 0,
			error, sizeof(error));
		if (handle < 0)
		{
			printf("cannot add: %s\n", error);
			return 1;
		}
		if (address)
			torrent_add_peer(handle, address, port);
		for (elapsed = 0; elapsed < seconds; elapsed++)
		{
			struct torrent_status status;

			report(handle);
			torrent_status_get(handle, &status);
			if (status.state == _torrent_state_seeding)
			{
				printf("complete\n");
				torrent_stop();
				return 0;
			}
			if (status.state == _torrent_state_failed)
			{
				printf("failed: %s\n", status.detail);
				torrent_stop();
				return 1;
			}
			sleep_milliseconds(1000);
		}
		printf("timed out\n");
		torrent_stop();
		return 1;
	}
	fprintf(stderr, "usage: torrent_check seed <file> <info hash> <piece length> <port>\n"
		"       torrent_check get <name> <size> <info hash> <piece length> <ip:port> <path> [<seconds>]\n");
	return 2;
}
