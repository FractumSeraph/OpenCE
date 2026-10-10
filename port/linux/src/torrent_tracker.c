/*
TORRENT_TRACKER.C

The BitTorrent client's trackers and web seeds (torrent_internal.h): UDP
trackers (BEP 15) on the session's UDP socket, HTTP trackers (BEP 3) and
web seeds (BEP 19, a piece at a time by its byte range) through a small
HTTP/1.1 client of its own, one connection a request, closed after.

Only plain http:// is spoken: the game has no TLS of its own on every
platform. An https:// tracker or web seed is logged and left out.
*/

#include "torrent_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UDP_CONNECT_MAGIC 0x41727101980ULL
#define UDP_CONNECTION_LIFETIME (60 * TORRENT_SECOND)
#define UDP_TIMEOUT (15 * TORRENT_SECOND)
#define HTTP_CONNECT_TIMEOUT (10 * TORRENT_SECOND)
#define HTTP_TRACKER_TIMEOUT (30 * TORRENT_SECOND)
#define HTTP_IDLE_TIMEOUT (30 * TORRENT_SECOND)
#define HTTP_BODY_MAXIMUM (256 * 1024)
#define RESOLVE_LIFETIME (10 * TORRENT_MINUTE)
#define ANNOUNCE_MINIMUM (60 * TORRENT_SECOND)
/* how long a download waits for the metadata from peers before it fetches
the whole file from the web seeds (checked against the info hash after) */
#define WEB_WHOLE_WAIT (10 * TORRENT_SECOND)
/* answers that were not the piece asked for (shorter, or refused), in a
row, for each web seed connection, after which the whole file is no
longer fetched from the web seeds */
#define WEB_WHOLE_FAILURES 3
#define ANNOUNCE_DEFAULT (30 * TORRENT_MINUTE)
#define ANNOUNCE_RETRY_MAXIMUM (30 * TORRENT_MINUTE)
#define WEB_SEED_RETRY (5 * TORRENT_SECOND)
#define WEB_SEED_RETRY_MAXIMUM (5 * TORRENT_MINUTE)

enum
{
	_udp_action_connect,
	_udp_action_announce,
	_udp_action_scrape,
	_udp_action_error,
};

enum
{
	_event_none,
	_event_completed,
	_event_started,
	_event_stopped,
};

/* ---------- URLs */

int torrent_url_parse(const char *url, char *host, int host_size, unsigned short *port, char *path, int path_size)
{
	const char *start;
	const char *slash;
	const char *colon;
	int host_length;

	if (!strncmp(url, "http://", 7))
		start = url + 7;
	else if (!strncmp(url, "udp://", 6))
		start = url + 6;
	else
		return 0;
	slash = strchr(start, '/');
	host_length = slash ? (int)(slash - start) : (int)strlen(start);
	if (!host_length || host_length >= host_size)
		return 0;
	memcpy(host, start, (size_t)host_length);
	host[host_length] = 0;
	*port = url[0] == 'u' ? 6969 : 80;
	colon = strchr(host, ':');
	if (colon)
	{
		int value = atoi(colon + 1);

		if (value <= 0 || value > 65535)
			return 0;
		*port = (unsigned short)value;
		host[colon - host] = 0;
	}
	snprintf(path, (size_t)path_size, "%s", slash ? slash : "/");
	return 1;
}

static void text_list_next(const char **text, char *item, int size)
{
	const char *comma;
	int length;

	item[0] = 0;
	while (**text == ' ' || **text == ',')
		(*text)++;
	if (!**text)
		return;
	comma = strchr(*text, ',');
	length = comma ? (int)(comma - *text) : (int)strlen(*text);
	while (length && (*text)[length - 1] == ' ')
		length--;
	if (length >= size)
		length = size - 1;
	memcpy(item, *text, (size_t)length);
	item[length] = 0;
	*text = comma ? comma + 1 : *text + strlen(*text);
}

void torrent_trackers_configure(void)
{
	const char *text = torrent_session.trackers_text;
	char item[256];
	int web_seeds;
	int connections;

	torrent_session.tracker_count = 0;
	torrent_session.web_seed_count = 0;
	memset(torrent_session.web_seed_addresses, 0, sizeof(torrent_session.web_seed_addresses));
	for (;;)
	{
		struct torrent_tracker *tracker;

		text_list_next(&text, item, sizeof(item));
		if (!item[0])
			break;
		if (torrent_session.tracker_count >= TORRENT_MAXIMUM_TRACKERS)
		{
			torrent_log("too many trackers: %s left out", item);
			continue;
		}
		tracker = &torrent_session.trackers[torrent_session.tracker_count];
		memset(tracker, 0, sizeof(*tracker));
		if (!torrent_url_parse(item, tracker->host, sizeof(tracker->host), &tracker->port, tracker->path,
			sizeof(tracker->path)))
		{
			torrent_log("the tracker %s is not a udp:// or http:// URL: left out", item);
			continue;
		}
		snprintf(tracker->url, sizeof(tracker->url), "%s", item);
		tracker->udp = !strncmp(item, "udp://", 6);
		torrent_session.tracker_count++;
	}
	/* (the web seeds counted first: the connections are shared out among
	them, a few to each, a piece on each. One piece at a time from a server
	far away was a piece a round trip or so, some 250 KB a second; four
	connections to each of two web seeds left a third none) */
	text = torrent_session.web_seeds_text;
	web_seeds = 0;
	for (;;)
	{
		text_list_next(&text, item, sizeof(item));
		if (!item[0])
			break;
		web_seeds += !strncmp(item, "http://", 7);
	}
	connections = web_seeds ? TORRENT_MAXIMUM_WEB_SEEDS / web_seeds : 0;
	if (connections < 1)
		connections = 1;
	text = torrent_session.web_seeds_text;
	for (;;)
	{
		int connection;

		text_list_next(&text, item, sizeof(item));
		if (!item[0])
			break;
		if (strncmp(item, "http://", 7))
		{
			torrent_log("the web seed %s is not an http:// URL: left out", item);
			continue;
		}
		if (torrent_session.web_seed_count >= TORRENT_MAXIMUM_WEB_SEEDS)
		{
			torrent_log("too many web seeds: %s left out", item);
			continue;
		}
		for (connection = 0; connection < connections &&
			torrent_session.web_seed_count < TORRENT_MAXIMUM_WEB_SEEDS; connection++)
		{
			snprintf(torrent_session.web_seeds[torrent_session.web_seed_count++],
				sizeof(torrent_session.web_seeds[0]), "%s", item);
		}
	}
}

/* a tracker's address, looked up once in a while (blocking briefly) */
static unsigned long tracker_address(struct torrent_tracker *tracker)
{
	if (!tracker->address || torrent_elapsed(tracker->resolved_time, RESOLVE_LIFETIME))
	{
		tracker->address = posix_resolve_ipv4(tracker->host);
		tracker->resolved_time = torrent_now();
		if (!tracker->address)
			torrent_log("the tracker %s cannot be resolved", tracker->host);
	}
	return tracker->address;
}

/* a web seed's address, looked up once in a while (blocking briefly), as a
tracker's is */
static unsigned long web_seed_address(int index, const char *host)
{
	if (!torrent_session.web_seed_addresses[index] ||
		torrent_elapsed(torrent_session.web_seed_resolved_times[index], RESOLVE_LIFETIME))
	{
		torrent_session.web_seed_addresses[index] = posix_resolve_ipv4(host);
		torrent_session.web_seed_resolved_times[index] = torrent_now();
	}
	return torrent_session.web_seed_addresses[index];
}

/* ---------- the HTTP client */

void torrent_http_close(struct torrent_http *http)
{
	/* (an idle one's socket is -1, or 0 from a torrent zeroed: not a
	socket of ours) */
	if (http->socket >= 0 && http->state != _torrent_http_idle)
		posix_socket_close(http->socket);
	http->socket = -1;
	free(http->body);
	http->body = NULL;
	http->body_capacity = 0;
	if (http->state != _torrent_http_done && http->state != _torrent_http_failed)
		http->state = _torrent_http_idle;
}

static void http_fail(struct torrent_http *http, const char *reason)
{
	snprintf(http->error, sizeof(http->error), "%s", reason);
	http->state = _torrent_http_failed;
	torrent_http_close(http);
}

int torrent_http_start(struct torrent_http *http, unsigned long address, unsigned short port, const char *host,
	const char *path, long long first, long long last, void (*stream)(void *context, const unsigned char *bytes,
	int size), void *context)
{
	struct sockaddr_in to;
	char range[64];

	torrent_http_close(http);
	memset(http, 0, sizeof(*http));
	http->socket = -1;
	http->address = address;
	http->port = port;
	http->stream = stream;
	http->context = context;
	http->content_length = -1;
	http->started = torrent_now();
	range[0] = 0;
	if (last >= 0)
	{
		snprintf(range, sizeof(range), "Range: bytes=%lld-%lld\r\n", first, last);
		http->ranged = 1;
		http->range_first = first;
	}
	{
		char host_header[160];

		if (port == 80)
			snprintf(host_header, sizeof(host_header), "%s", host);
		else
			snprintf(host_header, sizeof(host_header), "%s:%u", host, port);
		http->request_size = snprintf(http->request, sizeof(http->request),
			"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: OpenCE\r\nAccept: */*\r\nConnection: close\r\n%s\r\n",
			path, host_header, range);
	}
	if (http->request_size <= 0 || http->request_size >= (int)sizeof(http->request))
	{
		http_fail(http, "the request is too long");
		return 0;
	}
	http->socket = posix_socket(AF_INET, SOCK_STREAM, 0);
	if (http->socket < 0 || posix_socket_set_nonblocking(http->socket, 1) < 0)
	{
		http_fail(http, "no socket");
		return 0;
	}
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = torrent_network_short(port);
	to.sin_addr.s_addr = address;
	if (posix_socket_connect(http->socket, &to, sizeof(to)) < 0 && !torrent_would_block())
	{
		http_fail(http, "cannot connect");
		return 0;
	}
	http->state = _torrent_http_connecting;
	return 1;
}

/* whether text starts with name, in any case */
static int starts_with_nocase(const char *text, const char *name, size_t length)
{
	size_t index;

	for (index = 0; index < length; index++)
	{
		char a = text[index], b = name[index];

		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = (char)(b - 'A' + 'a');
		if (a != b || !a)
			return 0;
	}
	return 1;
}

static int http_header_value(const char *headers, const char *name, char *value, int size)
{
	const char *line = headers;
	size_t length = strlen(name);

	while ((line = strstr(line, "\r\n")) != NULL)
	{
		line += 2;
		if (starts_with_nocase(line, name, length) && line[length] == ':')
		{
			const char *end = strstr(line, "\r\n");
			const char *start = line + length + 1;
			int count;

			while (*start == ' ')
				start++;
			count = end ? (int)(end - start) : (int)strlen(start);
			if (count >= size)
				count = size - 1;
			memcpy(value, start, (size_t)count);
			value[count] = 0;
			return 1;
		}
	}
	return 0;
}

static void http_body_bytes(struct torrent_http *http, const unsigned char *bytes, int size)
{
	if (http->content_length >= 0 && http->body_received + size > http->content_length)
		size = (int)(http->content_length - http->body_received);
	if (size <= 0)
		return;
	if (http->stream)
	{
		http->stream(http->context, bytes, size);
		/* (the stream may have failed it, or closed it with its torrent) */
		if (http->state != _torrent_http_body)
			return;
	}
	else
	{
		if (http->body_received + size > HTTP_BODY_MAXIMUM)
		{
			http_fail(http, "the answer is too long");
			return;
		}
		if ((long long)http->body_capacity < http->body_received + size)
		{
			int capacity = (int)(http->body_received + size) * 2;
			unsigned char *grown = realloc(http->body, (size_t)capacity);

			if (!grown)
			{
				http_fail(http, "out of memory");
				return;
			}
			http->body = grown;
			http->body_capacity = capacity;
		}
		memcpy(http->body + http->body_received, bytes, (size_t)size);
	}
	http->body_received += size;
	if (http->content_length >= 0 && http->body_received >= http->content_length)
	{
		http->state = _torrent_http_done;
		posix_socket_close(http->socket);
		http->socket = -1;
	}
}

static void http_headers_received(struct torrent_http *http, int header_size)
{
	char value[64];

	http->in[header_size] = 0;
	if (strncmp((char *)http->in, "HTTP/1.", 7) || header_size < 12)
	{
		http_fail(http, "not an HTTP answer");
		return;
	}
	http->status = atoi((char *)http->in + 9);
	if (http_header_value((char *)http->in, "Transfer-Encoding", value, sizeof(value)) &&
		strstr(value, "chunked"))
	{
		http_fail(http, "a chunked answer");
		return;
	}
	if (http_header_value((char *)http->in, "Content-Length", value, sizeof(value)))
		http->content_length = atoll(value);
	/* (a range (a web seed's piece) must be answered with that range: a
	server that ignores Range sends the whole file, 200, for every piece) */
	if (http->ranged && http->status == 206 &&
		(!http_header_value((char *)http->in, "Content-Range", value, sizeof(value)) ||
		!starts_with_nocase(value, "bytes ", 6) || atoll(value + 6) != http->range_first))
	{
		http_fail(http, "the answer is not the range asked for");
		return;
	}
	if (http->ranged ? http->status != 206 : http->status != 200 && http->status != 206)
	{
		snprintf(http->error, sizeof(http->error), "HTTP %d", http->status);
		http->state = _torrent_http_failed;
		torrent_http_close(http);
		return;
	}
	http->state = _torrent_http_body;
	if (http->content_length == 0)
	{
		http->state = _torrent_http_done;
		torrent_http_close(http);
	}
}

static void http_ready(struct torrent_http *http, int writable)
{
	if (http->state == _torrent_http_connecting)
	{
		int error = 0;
		int length = sizeof(error);

		if (posix_socket_getsockopt(http->socket, SOL_SOCKET, TORRENT_SO_ERROR, &error, &length) < 0 || error)
		{
			http_fail(http, "cannot connect");
			return;
		}
		http->state = _torrent_http_sending;
	}
	if (http->state == _torrent_http_sending)
	{
		int sent = posix_socket_send(http->socket, http->request + http->request_sent,
			http->request_size - http->request_sent, 0);

		if (sent < 0 && !torrent_would_block())
		{
			http_fail(http, "the request could not be sent");
			return;
		}
		if (sent > 0)
			http->request_sent += sent;
		if (http->request_sent >= http->request_size)
			http->state = _torrent_http_headers;
		return;
	}
	if (writable)
		return;
	for (;;)
	{
		unsigned char buffer[16384];
		int received;

		if (http->state == _torrent_http_headers)
		{
			int room = TORRENT_HTTP_HEADER_SIZE - 1 - http->in_size;
			char *end;

			if (room <= 0)
			{
				http_fail(http, "the headers are too long");
				return;
			}
			received = posix_socket_recv(http->socket, http->in + http->in_size, room, 0);
			if (received < 0)
			{
				if (!torrent_would_block())
					http_fail(http, "the connection failed");
				return;
			}
			if (received == 0)
			{
				http_fail(http, "the connection closed before the headers");
				return;
			}
			http->in_size += received;
			http->in[http->in_size] = 0;
			end = strstr((char *)http->in, "\r\n\r\n");
			if (!end)
				continue;
			{
				int header_size = (int)(end + 4 - (char *)http->in);
				int body_size = http->in_size - header_size;
				unsigned char body[TORRENT_HTTP_HEADER_SIZE];

				memcpy(body, http->in + header_size, (size_t)body_size);
				http_headers_received(http, header_size);
				if (http->state == _torrent_http_body && body_size)
					http_body_bytes(http, body, body_size);
			}
			if (http->state != _torrent_http_body)
				return;
			continue;
		}
		if (http->state != _torrent_http_body)
			return;
		received = posix_socket_recv(http->socket, buffer, sizeof(buffer), 0);
		if (received < 0)
		{
			if (!torrent_would_block())
				http_fail(http, "the connection failed");
			return;
		}
		if (received == 0)
		{
			/* (the end: whole if the length was not told) */
			if (http->content_length < 0)
			{
				http->state = _torrent_http_done;
				torrent_http_close(http);
			}
			else
			{
				http_fail(http, "the connection closed early");
			}
			return;
		}
		http->started = torrent_now();
		http_body_bytes(http, buffer, received);
		if (http->state != _torrent_http_body)
			return;
	}
}

void torrent_http_tick(struct torrent_http *http)
{
	switch (http->state)
	{
	case _torrent_http_connecting:
	case _torrent_http_sending:
		if (torrent_elapsed(http->started, HTTP_CONNECT_TIMEOUT))
			http_fail(http, "no answer");
		break;
	case _torrent_http_headers:
	case _torrent_http_body:
		if (torrent_elapsed(http->started, HTTP_IDLE_TIMEOUT))
			http_fail(http, "the answer stopped coming");
		break;
	default:
		break;
	}
}

static int http_active(const struct torrent_http *http)
{
	return http->socket >= 0 && http->state >= _torrent_http_connecting && http->state <= _torrent_http_body;
}

/* every torrent's HTTP objects: the trackers', then the web seeds' */
static struct torrent_http *http_each(int *torrent_index, int *index)
{
	while (*torrent_index < TORRENT_MAXIMUM_TORRENTS)
	{
		struct torrent *torrent = &torrent_session.torrents[*torrent_index];

		if (torrent->used && *index < TORRENT_MAXIMUM_TRACKERS + TORRENT_MAXIMUM_WEB_SEEDS)
		{
			int which = (*index)++;

			return which < TORRENT_MAXIMUM_TRACKERS ? &torrent->trackers[which].http :
				&torrent->web_seeds[which - TORRENT_MAXIMUM_TRACKERS].http;
		}
		(*torrent_index)++;
		*index = 0;
	}
	return NULL;
}

void torrent_http_sockets(int *read, int *read_count, int *write, int *write_count, int maximum)
{
	int torrent_index = 0, index = 0;
	struct torrent_http *http;

	while ((http = http_each(&torrent_index, &index)) != NULL)
	{
		if (!http_active(http))
			continue;
		if (*read_count < maximum)
			read[(*read_count)++] = http->socket;
		if ((http->state == _torrent_http_connecting || http->state == _torrent_http_sending) &&
			*write_count < maximum)
		{
			write[(*write_count)++] = http->socket;
		}
	}
}

int torrent_http_socket_ready(int socket, int writable)
{
	int torrent_index = 0, index = 0;
	struct torrent_http *http;

	while ((http = http_each(&torrent_index, &index)) != NULL)
	{
		if (http_active(http) && http->socket == socket)
		{
			http_ready(http, writable);
			return 1;
		}
	}
	return 0;
}

int torrent_http_count(void)
{
	int torrent_index = 0, index = 0;
	struct torrent_http *http;
	int count = 0;

	while ((http = http_each(&torrent_index, &index)) != NULL)
		count += http_active(http);
	return count;
}

/* ---------- trackers */

static void peers_from_compact(struct torrent *torrent, const unsigned char *bytes, int size)
{
	int index;
	int added = 0;

	for (index = 0; index + 6 <= size; index += 6)
	{
		unsigned long address;
		unsigned short port;

		memcpy(&address, bytes + index, 4);
		memcpy(&port, bytes + index + 4, 2);
		added += torrent_candidate_add(torrent, address, port, _torrent_source_tracker);
	}
	if (added)
		torrent_log("%s: %d new peers from a tracker", torrent->name, added);
}

/* what to tell the tracker: started first; completed once a download
finished (a torrent seeded from the start never downloaded) */
static int announce_event(struct torrent *torrent, struct torrent_tracker_state *state)
{
	if (!state->started_sent)
		return _event_started;
	if (torrent->state == _torrent_state_seeding && torrent->downloaded && !state->completed_sent)
		return _event_completed;
	return _event_none;
}

static unsigned long long bytes_left(const struct torrent *torrent)
{
	unsigned long long have = 0;
	int piece;

	for (piece = 0; piece < torrent->piece_count; piece++)
	{
		if (torrent_has_piece(torrent, piece))
			have += (unsigned long long)torrent_piece_size(torrent, piece);
	}
	return torrent->size - have;
}

static void tracker_failed(struct torrent_tracker *tracker, struct torrent_tracker_state *state,
	const char *reason)
{
	unsigned long wait = ANNOUNCE_MINIMUM << (state->failures < 6 ? state->failures : 6);

	state->failures++;
	state->stage = 0;
	if (wait > ANNOUNCE_RETRY_MAXIMUM)
		wait = ANNOUNCE_RETRY_MAXIMUM;
	state->next_time = torrent_now() + wait;
	torrent_log("the tracker %s: %s (asking again in %u s)", tracker->url, reason,
		(unsigned int)(wait / TORRENT_SECOND));
}

static void tracker_answered(struct torrent *torrent, struct torrent_tracker *tracker,
	struct torrent_tracker_state *state, long interval, int peers)
{
	state->failures = 0;
	state->stage = 0;
	state->started_sent = 1;
	if (torrent->state == _torrent_state_seeding && torrent->downloaded)
		state->completed_sent = 1;
	state->peers_last = peers;
	if (interval < (long)(ANNOUNCE_MINIMUM / TORRENT_SECOND))
		interval = (long)(ANNOUNCE_MINIMUM / TORRENT_SECOND);
	if (interval > (long)(ANNOUNCE_DEFAULT / TORRENT_SECOND) * 4)
		interval = (long)(ANNOUNCE_DEFAULT / TORRENT_SECOND) * 4;
	state->next_time = torrent_now() + (unsigned long)interval * TORRENT_SECOND;
	torrent_log("%s: the tracker %s answered, %d peers, next in %ld s", torrent->name, tracker->url, peers, interval);
}

/* ---- UDP (BEP 15) */

static void udp_send(unsigned long address, unsigned short port, const unsigned char *packet, int size)
{
	struct sockaddr_in to;

	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = torrent_network_short(port);
	to.sin_addr.s_addr = address;
	posix_socket_sendto(torrent_session.udp_socket, packet, size, 0, &to, sizeof(to));
}

static void put_long_long(unsigned char *bytes, unsigned long long value)
{
	torrent_put_long(bytes, (unsigned long)(value >> 32));
	torrent_put_long(bytes + 4, (unsigned long)(value & 0xFFFFFFFFULL));
}

static unsigned long long get_long_long(const unsigned char *bytes)
{
	return (unsigned long long)torrent_get_long(bytes) << 32 | torrent_get_long(bytes + 4);
}

static void udp_connect_send(struct torrent_tracker *tracker, struct torrent_tracker_state *state)
{
	unsigned char packet[16];

	posix_random_bytes(&state->transaction, sizeof(state->transaction));
	put_long_long(packet, UDP_CONNECT_MAGIC);
	torrent_put_long(packet + 8, _udp_action_connect);
	torrent_put_long(packet + 12, state->transaction);
	udp_send(tracker->address, tracker->port, packet, sizeof(packet));
	state->stage = 1;
	state->sent_time = torrent_now();
}

static void udp_announce_send(struct torrent *torrent, struct torrent_tracker *tracker,
	struct torrent_tracker_state *state, int event)
{
	unsigned char packet[98];

	posix_random_bytes(&state->transaction, sizeof(state->transaction));
	put_long_long(packet, state->connection_id);
	torrent_put_long(packet + 8, _udp_action_announce);
	torrent_put_long(packet + 12, state->transaction);
	memcpy(packet + 16, torrent->info_hash, 20);
	memcpy(packet + 36, torrent_session.peer_id, 20);
	put_long_long(packet + 56, torrent->downloaded);
	put_long_long(packet + 64, bytes_left(torrent));
	put_long_long(packet + 72, torrent->uploaded);
	torrent_put_long(packet + 80, (unsigned long)event);
	torrent_put_long(packet + 84, 0);
	posix_random_bytes(packet + 88, 4);
	torrent_put_long(packet + 92, (unsigned long)-1);
	torrent_put_short(packet + 96, (unsigned short)torrent_session.port);
	udp_send(tracker->address, tracker->port, packet, sizeof(packet));
	state->stage = 2;
	state->sent_time = torrent_now();
}

static void udp_tracker_tick(struct torrent *torrent, struct torrent_tracker *tracker,
	struct torrent_tracker_state *state)
{
	if (state->stage)
	{
		if (torrent_elapsed(state->sent_time, UDP_TIMEOUT))
			tracker_failed(tracker, state, "no answer");
		return;
	}
	if (!torrent_reached(state->next_time))
		return;
	if (!tracker_address(tracker))
	{
		tracker_failed(tracker, state, "cannot be resolved");
		return;
	}
	if (!state->connection_id || torrent_elapsed(state->connection_time, UDP_CONNECTION_LIFETIME))
		udp_connect_send(tracker, state);
	else
		udp_announce_send(torrent, tracker, state, announce_event(torrent, state));
}

void torrent_tracker_udp_received(const unsigned char *data, int size, unsigned long address, unsigned short port)
{
	unsigned long action, transaction;
	int torrent_index, index;

	if (size < 8)
		return;
	action = torrent_get_long(data);
	transaction = torrent_get_long(data + 4);
	for (torrent_index = 0; torrent_index < TORRENT_MAXIMUM_TORRENTS; torrent_index++)
	{
		struct torrent *torrent = &torrent_session.torrents[torrent_index];

		if (!torrent->used)
			continue;
		for (index = 0; index < torrent_session.tracker_count; index++)
		{
			struct torrent_tracker *tracker = &torrent_session.trackers[index];
			struct torrent_tracker_state *state = &torrent->trackers[index];

			if (!tracker->udp || !state->stage || state->transaction != transaction ||
				tracker->address != address || torrent_network_short(tracker->port) != port)
			{
				continue;
			}
			if (action == _udp_action_connect && state->stage == 1 && size >= 16)
			{
				state->connection_id = get_long_long(data + 8);
				state->connection_time = torrent_now();
				udp_announce_send(torrent, tracker, state, announce_event(torrent, state));
			}
			else if (action == _udp_action_announce && state->stage == 2 && size >= 20)
			{
				long interval = (long)torrent_get_long(data + 8);

				peers_from_compact(torrent, data + 20, size - 20);
				tracker_answered(torrent, tracker, state, interval, (size - 20) / 6);
			}
			else if (action == _udp_action_error)
			{
				char message[128];
				int length = size - 8 < (int)sizeof(message) - 1 ? size - 8 : (int)sizeof(message) - 1;

				memcpy(message, data + 8, (size_t)length);
				message[length] = 0;
				state->connection_id = 0;
				tracker_failed(tracker, state, message);
			}
			return;
		}
	}
}

/* ---- HTTP */

static void http_announce_start(struct torrent *torrent, struct torrent_tracker *tracker,
	struct torrent_tracker_state *state)
{
	char path[512];
	char info_hash[3 * 20 + 1];
	char peer_id[3 * 20 + 1];
	int index;
	int event = announce_event(torrent, state);
	static const char *const event_names[] = { "", "completed", "started", "stopped" };

	/* (every byte of both escaped: simplest, and always right) */
	for (index = 0; index < 20; index++)
	{
		sprintf(info_hash + index * 3, "%%%02X", torrent->info_hash[index]);
		sprintf(peer_id + index * 3, "%%%02X", torrent_session.peer_id[index]);
	}
	snprintf(path, sizeof(path),
		"%s%cinfo_hash=%s&peer_id=%s&port=%d&uploaded=%llu&downloaded=%llu&left=%llu&compact=1&numwant=50%s%s",
		tracker->path, strchr(tracker->path, '?') ? '&' : '?', info_hash, peer_id, torrent_session.port,
		torrent->uploaded, torrent->downloaded, bytes_left(torrent), event ? "&event=" : "", event_names[event]);
	state->stage = 1;
	state->sent_time = torrent_now();
	if (!torrent_http_start(&state->http, tracker->address, tracker->port, tracker->host, path, 0, -1, NULL, NULL))
		tracker_failed(tracker, state, state->http.error);
}

static void http_announce_answered(struct torrent *torrent, struct torrent_tracker *tracker,
	struct torrent_tracker_state *state)
{
	struct bencode_document *document = malloc(sizeof(*document));
	struct torrent_http *http = &state->http;
	int peers = 0;

	if (!document)
		return;
	if (http->body && bencode_parse(document, http->body, (int)http->body_received) > 0 &&
		document->nodes[0].type == _bencode_dictionary)
	{
		int node = bencode_find(document, 0, "failure reason");
		const unsigned char *compact;
		int compact_length;

		if (node >= 0)
		{
			char reason[128];

			bencode_text(document, node, reason, sizeof(reason));
			tracker_failed(tracker, state, reason);
		}
		else
		{
			long interval = (long)bencode_integer(document, bencode_find(document, 0, "interval"),
				(long long)(ANNOUNCE_DEFAULT / TORRENT_SECOND));

			node = bencode_find(document, 0, "peers");
			if (bencode_string(document, node, &compact, &compact_length))
			{
				peers_from_compact(torrent, compact, compact_length);
				peers = compact_length / 6;
			}
			else if (node >= 0 && document->nodes[node].type == _bencode_list)
			{
				int child;

				for (child = document->nodes[node].child; child >= 0; child = document->nodes[child].next)
				{
					char ip[64];
					long long port = bencode_integer(document, bencode_find(document, child, "port"), 0);

					bencode_text(document, bencode_find(document, child, "ip"), ip, sizeof(ip));
					if (ip[0] && port > 0 && port < 65536)
					{
						unsigned long address = posix_resolve_ipv4(ip);

						if (torrent_candidate_add(torrent, address, torrent_network_short((unsigned short)port),
							_torrent_source_tracker))
						{
							peers++;
						}
					}
				}
			}
			tracker_answered(torrent, tracker, state, interval, peers);
		}
	}
	else
	{
		tracker_failed(tracker, state, "the answer is not bencoded");
	}
	free(document);
	torrent_http_close(http);
	http->state = _torrent_http_idle;
}

static void http_tracker_tick(struct torrent *torrent, struct torrent_tracker *tracker,
	struct torrent_tracker_state *state)
{
	struct torrent_http *http = &state->http;

	if (state->stage)
	{
		torrent_http_tick(http);
		if (http->state == _torrent_http_done)
			http_announce_answered(torrent, tracker, state);
		else if (http->state == _torrent_http_failed)
		{
			tracker_failed(tracker, state, http->error);
			torrent_http_close(http);
			http->state = _torrent_http_idle;
		}
		return;
	}
	if (!torrent_reached(state->next_time))
		return;
	if (!tracker_address(tracker))
	{
		tracker_failed(tracker, state, "cannot be resolved");
		return;
	}
	http_announce_start(torrent, tracker, state);
}

void torrent_trackers_tick(struct torrent *torrent)
{
	int index;

	for (index = 0; index < torrent_session.tracker_count; index++)
	{
		struct torrent_tracker *tracker = &torrent_session.trackers[index];
		struct torrent_tracker_state *state = &torrent->trackers[index];

		/* (a download's completion told at once, not at the next announce) */
		if (!state->stage && announce_event(torrent, state) == _event_completed)
			state->next_time = torrent_now();
		if (tracker->udp)
			udp_tracker_tick(torrent, tracker, state);
		else
			http_tracker_tick(torrent, tracker, state);
	}
}

void torrent_trackers_stop(struct torrent *torrent)
{
	int index;

	for (index = 0; index < TORRENT_MAXIMUM_TRACKERS; index++)
	{
		struct torrent_tracker *tracker = &torrent_session.trackers[index];
		struct torrent_tracker_state *state = &torrent->trackers[index];

		/* (a UDP tracker told we stopped, if it knew us; nothing waited for) */
		if (index < torrent_session.tracker_count && tracker->udp && state->started_sent && state->connection_id &&
			!torrent_elapsed(state->connection_time, UDP_CONNECTION_LIFETIME))
		{
			udp_announce_send(torrent, tracker, state, _event_stopped);
		}
		torrent_http_close(&state->http);
		memset(state, 0, sizeof(*state));
		state->http.socket = -1;
	}
	for (index = 0; index < TORRENT_MAXIMUM_WEB_SEEDS; index++)
	{
		torrent_http_close(&torrent->web_seeds[index].http);
		memset(&torrent->web_seeds[index], 0, sizeof(torrent->web_seeds[index]));
		torrent->web_seeds[index].http.socket = -1;
		torrent->web_seeds[index].piece = -1;
	}
}

/* ---------- web seeds (BEP 19) */

struct web_seed_context
{
	struct torrent *torrent;
	struct torrent_web_seed_state *state;
};

static void web_seed_bytes(void *context, const unsigned char *bytes, int size)
{
	struct web_seed_context *seed = context;
	struct torrent *torrent = seed->torrent;
	struct torrent_web_seed_state *state = seed->state;
	int piece_size = torrent_piece_size(torrent, state->piece);

	torrent_downloaded(torrent, size);
	/* (the whole file, no metadata yet: written as it comes, checked once
	it is all here) */
	if (torrent->state == _torrent_state_metadata)
	{
		if (size > piece_size - state->received)
			size = piece_size - state->received;
		if (size <= 0)
			return;
		/* (a write that failed ends the fetch: the bytes after it went to the
		wrong place) */
		if (!torrent_file_write(torrent, (unsigned long long)state->piece * torrent->piece_length +
			(unsigned long long)state->received, bytes, size))
		{
			http_fail(&state->http, "the file could not be written");
			return;
		}
		state->received += size;
		return;
	}
	while (size > 0 && state->received < piece_size)
	{
		int begin = state->received - state->block_size;
		int block_length = piece_size - begin < TORRENT_BLOCK_SIZE ? piece_size - begin : TORRENT_BLOCK_SIZE;
		int take = block_length - state->block_size;

		if (take > size)
			take = size;
		memcpy(state->block + state->block_size, bytes, (size_t)take);
		state->block_size += take;
		state->received += take;
		bytes += take;
		size -= take;
		if (state->block_size == block_length)
		{
			torrent_block_received(torrent, state->piece, begin, state->block, block_length);
			/* (a write or read back that failed failed the torrent, which
			closed this fetch) */
			if (state->http.state != _torrent_http_body)
				return;
			state->block_size = 0;
		}
	}
}

static struct web_seed_context web_seed_contexts[TORRENT_MAXIMUM_TORRENTS][TORRENT_MAXIMUM_WEB_SEEDS];

static void web_seed_failed(struct torrent *torrent, struct torrent_web_seed_state *state, const char *url,
	const char *reason)
{
	unsigned long wait = WEB_SEED_RETRY << (state->failures < 6 ? state->failures : 6);

	if (wait > WEB_SEED_RETRY_MAXIMUM)
		wait = WEB_SEED_RETRY_MAXIMUM;
	state->failures++;
	state->retry_time = torrent_now() + wait;
	if (state->piece >= 0 && torrent->state == _torrent_state_metadata)
		torrent_bit_clear(torrent->web_whole_requested, state->piece);
	else if (state->piece >= 0)
		torrent_piece_unrequested(torrent, state->piece);
	state->piece = -1;
	torrent_log("%s: the web seed %s: %s (trying again in %u s)", torrent->name, url, reason,
		(unsigned int)(wait / TORRENT_SECOND));
}

/* an answer to the whole file's fetch that was not the piece (shorter, the
server's file being shorter than the torrent's, or refused): after
WEB_WHOLE_FAILURES for each connection in a row the whole file is given
up and its pieces set aside, as when the file fetched is not the
torrent's (checking_tick, torrent.c); the metadata is looked for from
peers */
static void web_whole_answer_wrong(struct torrent *torrent)
{
	if (torrent->web_whole <= 0 ||
		++torrent->web_whole_failures < WEB_WHOLE_FAILURES * torrent_session.web_seed_count)
	{
		return;
	}
	torrent_log("%s: the web seeds' answers are not this torrent's pieces (%d in a row); looking to peers",
		torrent->name, torrent->web_whole_failures);
	torrent->web_whole = -1;
	torrent->web_whole_count = 0;
	memset(torrent->web_whole_have, 0, sizeof(torrent->web_whole_have));
	memset(torrent->web_whole_requested, 0, sizeof(torrent->web_whole_requested));
}

void torrent_web_seeds_tick(struct torrent *torrent)
{
	int index;
	int torrent_index = (int)(torrent - torrent_session.torrents);

	for (index = 0; index < torrent_session.web_seed_count; index++)
	{
		struct torrent_web_seed_state *state = &torrent->web_seeds[index];
		struct torrent_http *http = &state->http;
		const char *url = torrent_session.web_seeds[index];
		int whole;

		if (state->piece >= 0)
		{
			torrent_http_tick(http);
			if (http->state == _torrent_http_done)
			{
				int whole = torrent->state == _torrent_state_metadata && torrent->web_whole > 0;

				if (state->received == torrent_piece_size(torrent, state->piece))
				{
					state->failures = 0;
					if (whole)
						torrent->web_whole_failures = 0;
					if (whole && !torrent_bit_test(torrent->web_whole_have, state->piece))
					{
						torrent_bit_set(torrent->web_whole_have, state->piece);
						torrent->web_whole_count++;
					}
					/* (a piece still under way, some block of it not taken:
					open to asking again, and no longer this web seed's) */
					else if (!whole)
						torrent_piece_unrequested(torrent, state->piece);
				}
				else
				{
					/* (a short answer: the server's file is shorter than the
					torrent's, which asking again at once does not change) */
					web_seed_failed(torrent, state, url, "the answer is shorter than the piece");
					if (whole)
						web_whole_answer_wrong(torrent);
				}
				state->piece = -1;
				torrent_http_close(http);
				http->state = _torrent_http_idle;
			}
			else if (http->state == _torrent_http_failed)
			{
				/* (the server's answer was not the piece: refused, or the
				whole file for a range) */
				int refused = http->status == 200 || (http->status >= 400 && http->status < 500);
				int whole = torrent->state == _torrent_state_metadata && torrent->web_whole > 0;

				web_seed_failed(torrent, state, url, http->error);
				if (whole && refused)
					web_whole_answer_wrong(torrent);
				torrent_http_close(http);
				http->state = _torrent_http_idle;
			}
			continue;
		}
		/* (the whole file from the web seeds while no peer has given the
		metadata: once all here, checked against the info hash) */
		if (torrent->state == _torrent_state_metadata && torrent->web_whole > 0 &&
			torrent->web_whole_count == torrent->piece_count)
		{
			torrent_web_whole_check(torrent);
			return;
		}
		whole = torrent->state == _torrent_state_metadata && torrent->web_whole >= 0 &&
			torrent_elapsed(torrent->added_time, WEB_WHOLE_WAIT);
		if ((!whole && (torrent->state != _torrent_state_downloading || !torrent->piece_hashes)) ||
			!torrent_reached(state->retry_time) || torrent_download_allowance() < TORRENT_BLOCK_SIZE)
		{
			continue;
		}
		{
			char host[128];
			unsigned short port;
			char path[512];
			char file_url[400];
			unsigned long address;
			int piece;
			size_t length = strlen(url);

			if (url[length - 1] == '/')
			{
				/* (the name percent-encoded: some 600 maps' names have
				spaces, brackets or letters beyond ASCII) */
				const unsigned char *name = (const unsigned char *)torrent->name;
				int used = snprintf(file_url, sizeof(file_url), "%s", url);

				for (; *name && used < (int)sizeof(file_url) - 4; name++)
				{
					if ((*name >= 'A' && *name <= 'Z') || (*name >= 'a' && *name <= 'z') ||
						(*name >= '0' && *name <= '9') || strchr("-._~", *name))
					{
						file_url[used++] = (char)*name;
					}
					else
					{
						used += snprintf(file_url + used, sizeof(file_url) - used, "%%%02X", *name);
					}
				}
				file_url[used] = 0;
			}
			else
				snprintf(file_url, sizeof(file_url), "%s", url);
			if (!torrent_url_parse(file_url, host, sizeof(host), &port, path, sizeof(path)))
			{
				web_seed_failed(torrent, state, url, "not a URL");
				continue;
			}
			/* (the piece chosen first: an idle connection with nothing to
			fetch looks up no address) */
			if (whole)
			{
				/* (the next piece of the file not had nor asked for) */
				for (piece = 0; piece < torrent->piece_count; piece++)
				{
					if (!torrent_bit_test(torrent->web_whole_have, piece) &&
						!torrent_bit_test(torrent->web_whole_requested, piece))
					{
						break;
					}
				}
				if (piece >= torrent->piece_count)
					continue;
				if (!torrent->web_whole)
					torrent_log("%s: no peer has given the metadata: the whole file from the web seeds", torrent->name);
				torrent->web_whole = 1;
				torrent_bit_set(torrent->web_whole_requested, piece);
			}
			else if (!torrent_piece_to_request_whole(torrent, &piece))
				continue;
			state->piece = piece;
			state->received = 0;
			state->block_size = 0;
			address = web_seed_address(index, host);
			if (!address)
			{
				/* (the piece open to asking again) */
				web_seed_failed(torrent, state, url, "cannot be resolved");
				continue;
			}
			web_seed_contexts[torrent_index][index].torrent = torrent;
			web_seed_contexts[torrent_index][index].state = state;
			if (!torrent_http_start(http, address, port, host, path, (long long)piece * torrent->piece_length,
				(long long)piece * torrent->piece_length + torrent_piece_size(torrent, piece) - 1, web_seed_bytes,
				&web_seed_contexts[torrent_index][index]))
			{
				web_seed_failed(torrent, state, url, http->error);
			}
		}
	}
}
