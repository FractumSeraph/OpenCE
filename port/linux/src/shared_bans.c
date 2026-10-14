/*
SHARED_BANS.C

Port (this fork): ban lists other hosts publish (shared_bans.h). A host
names them in network.shared_bans (their https:// addresses, with commas
between them); each is fetched at start and every SHARED_BANS_INTERVAL
after, on a thread of its own, and the ban lines of all of them are kept
in SHARED_BANS_FILE, which the host checks a joining machine against beside
its own bans.txt (network_distributed_banned). A list is bans.txt's form, a
line a ban: a list that cannot be fetched this time keeps the lines it had
the time before, and nothing but a list's ban lines is ever kept. The bans
are the publisher's: a host that does not trust its lists' publishers
names none (the default).
*/

#include "platform.h"
#include "port_config.h"
#include "shared_bans.h"
#ifdef HALO_GAME_BROWSER
#include "browser_http.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* how often the lists are fetched again, in milliseconds */
	SHARED_BANS_INTERVAL = 10 * 60 * 1000,
	/* the most a list may be, and the most lists */
	SHARED_BANS_MAXIMUM_SIZE = 1024 * 1024,
	SHARED_BANS_MAXIMUM_LISTS = 8,
	/* the longest line kept (bans.txt's reader takes 511 characters: a
	source's name is added to each line) */
	SHARED_BANS_MAXIMUM_LINE = 400,
	SHARED_BANS_MAXIMUM_SOURCE = 96,
	SHARED_BANS_MAXIMUM_URL = 512,
};

/* ---------- globals */

#ifdef HALO_GAME_BROWSER
/* each list's lines as last fetched (NULL: never), kept for the times it
cannot be fetched */
static struct
{
	char url[SHARED_BANS_MAXIMUM_URL];
	char *lines;
} shared_bans_lists[SHARED_BANS_MAXIMUM_LISTS];
static int shared_bans_list_count;
#endif

/* ---------- private code */

/* whether a line holds a field of this name (beginning the line or after a
tab) */
static int has_field(const char *line, size_t length, const char *name)
{
	size_t name_length = strlen(name);
	size_t at;

	for (at = 0; at + name_length <= length; at++)
	{
		if ((at == 0 || line[at - 1] == '\t') && !strncmp(line + at, name, name_length))
			return 1;
	}
	return 0;
}

#ifdef HALO_GAME_BROWSER

/* every list's lines into SHARED_BANS_FILE, written beside it and then put
in its place (never half a file) */
static void write_file(void)
{
	char path[1024], temporary[1100];
	FILE *file;
	int index, ok = 1;

	platform_translate_path(SHARED_BANS_FILE, path, sizeof(path));
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	file = fopen(temporary, "w");
	if (!file)
	{
		platform_log("shared bans: %s could not be written", temporary);
		return;
	}
	fprintf(file, "# The ban lists of network.shared_bans, fetched by this host (shared_bans.c):\n"
		"# rewritten each time they are fetched; this host's own bans are bans.txt.\n");
	for (index = 0; index < shared_bans_list_count; index++)
	{
		if (shared_bans_lists[index].lines && fputs(shared_bans_lists[index].lines, file) < 0)
			ok = 0;
	}
	if (fclose(file) != 0)
		ok = 0;
	if (!ok || rename(temporary, path) != 0)
	{
		remove(temporary);
		platform_log("shared bans: %s could not be written", path);
	}
}

/* a list's own name, for its lines (list=): its address without https:// */
static void source_name(const char *url, char *source, size_t size)
{
	const char *start = strstr(url, "://");

	snprintf(source, size, "%s", start ? start + 3 : url);
}

static void fetch_all(void)
{
	char *response = malloc(SHARED_BANS_MAXIMUM_SIZE + 1);
	char *kept = malloc(SHARED_BANS_MAXIMUM_SIZE + 1);
	int index, changed = 0;

	if (!response || !kept)
	{
		free(response);
		free(kept);
		return;
	}
	for (index = 0; index < shared_bans_list_count; index++)
	{
		char error[160] = "";
		char source[SHARED_BANS_MAXIMUM_SOURCE];
		int status = posix_browser_request(shared_bans_lists[index].url, NULL, NULL, response,
			SHARED_BANS_MAXIMUM_SIZE + 1, error, sizeof(error));
		size_t size, length;
		long count = 0;
		size_t at;

		if (status != 200)
		{
			platform_log("shared bans: %s could not be fetched (%s); its bans of before are kept",
				shared_bans_lists[index].url, status ? "an HTTP error" : error);
			continue;
		}
		size = strlen(response);
		source_name(shared_bans_lists[index].url, source, sizeof(source));
		length = shared_bans_filter(response, size, source, kept, SHARED_BANS_MAXIMUM_SIZE + 1);
		for (at = 0; at < length; at++)
			count += kept[at] == '\n';
		free(shared_bans_lists[index].lines);
		shared_bans_lists[index].lines = malloc(length + 1);
		if (shared_bans_lists[index].lines)
			memcpy(shared_bans_lists[index].lines, kept, length + 1);
		changed = 1;
		platform_log("shared bans: %s: %ld bans", shared_bans_lists[index].url, count);
	}
	free(response);
	free(kept);
	if (changed)
		write_file();
}

static void *shared_bans_thread(void *argument)
{
	(void)argument;
	for (;;)
	{
		fetch_all();
		Sleep(SHARED_BANS_INTERVAL);
	}
	return NULL;
}

#endif

/* ---------- public code */

size_t shared_bans_filter(const char *text, size_t size, const char *source, char *out, size_t out_size)
{
	size_t written = 0, at = 0;

	if (!out_size)
		return 0;
	out[0] = 0;
	while (at < size)
	{
		size_t start = at, length, index;
		int printable = 1;

		while (at < size && text[at] != '\n')
			at++;
		length = at - start;
		at++;
		if (length && text[start + length - 1] == '\r')
			length--;
		/* (a ban: not a comment, of printable characters and tabs, no
		longer than a ban line is kept, with a hardware id or an address) */
		if (!length || text[start] == '#' || length > SHARED_BANS_MAXIMUM_LINE)
			continue;
		for (index = start; index < start + length; index++)
		{
			unsigned char c = (unsigned char)text[index];

			if (c != '\t' && (c < 32 || c > 126))
				printable = 0;
		}
		if (!printable || (!has_field(text + start, length, "hwid=") && !has_field(text + start, length, "ip=")))
			continue;
		/* (the line, then the list it came from) */
		if (written + length + strlen("\tlist=") + strlen(source) + 2 > out_size)
			break;
		memcpy(out + written, text + start, length);
		written += length;
		written += (size_t)snprintf(out + written, out_size - written, "\tlist=%s\n", source);
	}
	return written;
}

void shared_bans_start(void)
{
#ifdef HALO_GAME_BROWSER
	static int started;
	const char *setting = config_string("network.shared_bans");
	pthread_t thread;

	if (started || !setting || !setting[0])
		return;
	started = 1;
	/* (the lists: https:// addresses, with commas between them) */
	while (*setting && shared_bans_list_count < SHARED_BANS_MAXIMUM_LISTS)
	{
		const char *comma = strchr(setting, ',');
		size_t length = comma ? (size_t)(comma - setting) : strlen(setting);

		while (length && (*setting == ' ' || *setting == '\t'))
		{
			setting++;
			length--;
		}
		while (length && (setting[length - 1] == ' ' || setting[length - 1] == '\t'))
			length--;
		if (length >= 9 && length < SHARED_BANS_MAXIMUM_URL && !strncmp(setting, "https://", 8))
		{
			memcpy(shared_bans_lists[shared_bans_list_count].url, setting, length);
			shared_bans_lists[shared_bans_list_count].url[length] = 0;
			shared_bans_list_count++;
		}
		else if (length)
		{
			platform_log("shared bans: '%.*s' is not an https:// address; left out", (int)length, setting);
		}
		if (!comma)
			break;
		setting = comma + 1;
	}
	if (!shared_bans_list_count)
		return;
	platform_log("shared bans: %d list(s), fetched every %d minutes into %s", shared_bans_list_count,
		SHARED_BANS_INTERVAL / 60000, SHARED_BANS_FILE);
	if (pthread_create(&thread, NULL, shared_bans_thread, NULL) == 0)
		pthread_detach(thread);
	else
		platform_log("shared bans: could not start their thread");
#endif
}
