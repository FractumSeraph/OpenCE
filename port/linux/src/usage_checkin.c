/*
USAGE_CHECKIN.C

(port, this fork) How many copies of Fractum Halo are in use: soon after it
starts and every twelve hours while it runs, the game tells the fork's
server (halovps.fractumseraph.net, server/deploy/halo-usage) that it is
running, with this install's ID (random, made the first time and kept in
the save root as usage_id.txt: nothing about the player or the machine),
the build's version, channel, platform and architecture. The server counts
installs a day by their IDs, and keeps the address each came from.
network.usage_stats = false sends nothing. The game list's thread sends it
(browser.c): it blocks for a request's length at most, and an answer or
none changes nothing in the game.
*/

#ifdef HALO_GAME_BROWSER

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "browser_http.h"
#include "build_identity.h"
#include "p2p_internal.h"
#include "usage_checkin.h"

#include <stdio.h>
#include <string.h>

/* where the check-in goes (USAGE_CHECKIN_URL overrides it for a test) */
#ifndef USAGE_CHECKIN_URL
#define USAGE_CHECKIN_URL "https://halovps.fractumseraph.net/usage/v1/checkin"
#endif
/* the first a minute after the game starts (not while it loads), then every
twelve hours; one that got no answer is tried again an hour later */
#define USAGE_FIRST_DELAY (60UL * 1000)
#define USAGE_INTERVAL (12UL * 60 * 60 * 1000)
#define USAGE_RETRY (60UL * 60 * 1000)
#define USAGE_ID_BYTES 16

static unsigned long usage_started;
static unsigned long usage_next;
static int usage_logged;

/* this install's ID, as 32 hex digits: read from usage_id.txt, or made and
written there; 0 when there is none to be had */
static int usage_install_id(char id[2 * USAGE_ID_BYTES + 1])
{
	static const char digits[] = "0123456789abcdef";
	char path[1024];
	unsigned char bytes[USAGE_ID_BYTES];
	FILE *file;
	int index;

	snprintf(path, sizeof(path), "%s/usage_id.txt", platform_save_root());
	file = fopen(path, "r");
	if (file)
	{
		int ok = fscanf(file, "%32s", id) == 1 && strlen(id) == 2 * USAGE_ID_BYTES &&
			strspn(id, digits) == 2 * USAGE_ID_BYTES;

		fclose(file);
		if (ok)
			return 1;
	}
	posix_random_bytes(bytes, sizeof(bytes));
	for (index = 0; index < USAGE_ID_BYTES; index++)
	{
		id[2 * index] = digits[bytes[index] >> 4];
		id[2 * index + 1] = digits[bytes[index] & 15];
	}
	id[2 * USAGE_ID_BYTES] = 0;
	file = fopen(path, "w");
	if (!file)
		return 0;
	fprintf(file, "%s\n", id);
	fclose(file);
	return 1;
}

/* the platform as the counts name it: the build's, with the dedicated
server and the browser build told apart */
static const char *usage_platform(void)
{
#if defined(HALO_SERVER)
	return "server";
#elif defined(HALO_WEB)
	return "web";
#else
	return build_identity_platform();
#endif
}

/* (a JSON string's text: the build's names are letters, digits and . _ - +
and spaces, but nothing else is let through) */
static void usage_clean(char *out, size_t size, const char *text)
{
	size_t length = 0;

	for (; text && *text && length + 1 < size; text++)
	{
		char c = *text;

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("._-+ ()", c))
			out[length++] = c;
	}
	out[length] = 0;
}

void usage_checkin_update(void)
{
	char id[2 * USAGE_ID_BYTES + 1];
	char version[48], channel[24], platform[24], architecture[24];
	char body[512], response[128], error[256];
	unsigned long now = p2p_now();
	int status;

	if (!usage_started)
	{
		usage_started = now ? now : 1;
		usage_next = usage_started + USAGE_FIRST_DELAY;
		return;
	}
	if ((long)(now - usage_next) < 0)
		return;
	if (!config_boolean("network.usage_stats"))
	{
		if (!usage_logged)
			platform_log("usage: network.usage_stats is false: nothing is sent");
		usage_logged = 1;
		usage_next = now + USAGE_INTERVAL;
		return;
	}
	if (!usage_install_id(id))
	{
		usage_next = now + USAGE_INTERVAL;
		return;
	}
	usage_clean(version, sizeof(version), build_identity_version());
	usage_clean(channel, sizeof(channel), build_identity_channel());
	usage_clean(platform, sizeof(platform), usage_platform());
	usage_clean(architecture, sizeof(architecture), build_identity_architecture());
	snprintf(body, sizeof(body),
		"{\"id\": \"%s\", \"version\": \"%s\", \"channel\": \"%s\", \"platform\": \"%s\", \"architecture\": \"%s\"}",
		id, version, channel, platform, architecture);
	status = posix_browser_request(USAGE_CHECKIN_URL, body, "application/json", response, sizeof(response), error,
		sizeof(error));
	if (status == 204 && !usage_logged)
		platform_log("usage: checked in (network.usage_stats; false sends nothing)");
	else if (status != 204)
		platform_log("usage: the check-in got %d%s%s", status, status ? "" : ": ", status ? "" : error);
	usage_logged = 1;
	usage_next = now + (status ? USAGE_INTERVAL : USAGE_RETRY);
}

#endif
