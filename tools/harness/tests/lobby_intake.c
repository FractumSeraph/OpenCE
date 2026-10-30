#include "harness.h"

/* The server browser's intake (port/linux/src/p2p_lobby.c, listing_wanted
and p2p_lobby_slot_heard): which listings heard take a place in the queue,
and a broker's burst on subscribing (its kept slots, most of them games long
gone) leaving every live game a place. Port (this fork): a listing of any
version is let in, for the browser to show or mark, and a full queue gives a
joinable one (version_joinable, Delta's join range) the place of one that is
not */
#include "config.inc"

/* Delta's join range: the built-in one, a build's with no signed table
(delta.c, delta_use_built_in) */
static int delta_legacy_minimum(void)
{
	return HALO_PORT_NETWORK_VERSION_MINIMUM;
}

static int delta_legacy_maximum(void)
{
	return HALO_PORT_NETWORK_VERSION_MAXIMUM;
}

static int newer_count;
static void newer_heard(const unsigned char *key_hash)
{
	(void)key_hash;
	newer_count++;
}

/* the browser: browsing, nothing listed yet, the queue empty */
struct game
{
	int payload_size;
	unsigned char payload[MAXIMUM_LISTING_SIZE];
	unsigned long heard_time;
};

static struct
{
	int browsing;
	struct queued queue[MAXIMUM_QUEUED];
	int queue_count;
} lobby = { 1 };

static struct game *find_game(const unsigned char *key_hash)
{
	(void)key_hash;
	return NULL;
}

static unsigned long p2p_now(void)
{
	return 1;
}

/* this machine's key's hash: no slot's heard */
static const unsigned char *p2p_public_key(void)
{
	return NULL;
}

static void p2p_key_hash(const unsigned char *public_key, unsigned char *hash)
{
	(void)public_key;
	memset(hash, 0xEE, P2P_KEY_HASH_SIZE);
}

static void own_slot_heard(const unsigned char *payload, int size, int retained)
{
	(void)payload;
	(void)size;
	(void)retained;
	CHECK(0, "a slot taken for this machine's own");
}

#include "under_test.inc"

/* a listing as a host makes it (listing_make), its signature zeroes: the
intake does not check it; its key's first bytes are the host's number */
static int make(unsigned char *bytes, int version, int flags, long age, int host)
{
	int size = 0;
	unsigned long when = (unsigned long)(time(NULL) - age);
	const char *texts[3] = { "a game", "bloodgulch", "slayer" };
	int text;

	bytes[size++] = 'H';
	bytes[size++] = 'L';
	bytes[size++] = LISTING_FORMAT;
	bytes[size++] = (unsigned char)(version >> 8);
	bytes[size++] = (unsigned char)version;
	bytes[size++] = (unsigned char)flags;
	memset(bytes + size, 0, 4);
	size += 4;
	bytes[size++] = (unsigned char)(when >> 24);
	bytes[size++] = (unsigned char)(when >> 16);
	bytes[size++] = (unsigned char)(when >> 8);
	bytes[size++] = (unsigned char)when;
	memset(bytes + size, 7, P2P_KEY_SIZE);
	bytes[size] = (unsigned char)(host >> 8);
	bytes[size + 1] = (unsigned char)host;
	size += P2P_KEY_SIZE;
	memset(bytes + size, 9, P2P_TOKEN_SIZE);
	size += P2P_TOKEN_SIZE;
	bytes[size++] = 1;
	bytes[size++] = 16;
	bytes[size++] = 2;
	for (text = 0; text < 3; text++)
	{
		int length = (int)strlen(texts[text]);

		bytes[size++] = (unsigned char)length;
		memcpy(bytes + size, texts[text], (size_t)length);
		size += length;
	}
	memset(bytes + size, 0, STAMP_SIZE + P2P_SIGNATURE_SIZE);
	return size + STAMP_SIZE + P2P_SIGNATURE_SIZE;
}

static int wanted(int version, int flags, long age, int retained)
{
	unsigned char bytes[MAXIMUM_LISTING_SIZE];
	unsigned char key_hash[P2P_KEY_HASH_SIZE] = { 0 };
	int size = make(bytes, version, flags, age, 0);

	return listing_wanted(bytes, size, retained, key_hash);
}

/* a broker's slot heard: its key hash's text, the host's number */
static void heard(int version, int flags, long age, int retained, int host)
{
	unsigned char bytes[MAXIMUM_LISTING_SIZE];
	char hash_text[2 * P2P_KEY_HASH_SIZE + 1];
	int size = make(bytes, version, flags, age, host), index;

	for (index = 0; index < P2P_KEY_HASH_SIZE; index++)
		sprintf(hash_text + 2 * index, "%02x", index < 2 ? (unsigned)(host >> (8 - 8 * index)) & 0xFF : 0x11u);
	p2p_lobby_slot_heard(hash_text, bytes, size, retained);
}

int main(int argc, char **argv)
{
	char const *case_name = argc == 2 ? argv[1] : "";
	const int current = HALO_PORT_NETWORK_VERSION;
	const int minimum = HALO_PORT_NETWORK_VERSION_MINIMUM, maximum = HALO_PORT_NETWORK_VERSION_MAXIMUM;

	CASE("filter")
	{
		unsigned char key_hash[P2P_KEY_HASH_SIZE] = { 0 };

		CHECK(wanted(current, _listing_open, 0, 0), "a live game of this version");
		CHECK(wanted(current, _listing_open, 120, 1), "a kept slot of two minutes ago");
		CHECK(wanted(current, _listing_closed, 60, 1), "a host's recent tombstone (it ends a game)");
		CHECK(!wanted(current, _listing_open, RETAINED_WINDOW + 60, 1), "a kept slot of a game long gone");
		CHECK(!wanted(current, _listing_open, LIVE_WINDOW + 60, 0), "a live listing long gone, sent again");
		/* (this fork: every version's let in, to be shown or marked; only one
		past what this build joins is counted, for the empty list's words) */
		CHECK(wanted(current - 1, _listing_open, 0, 0) && newer_count == 0, "an older version's: not counted");
		CHECK(wanted(minimum - 1, _listing_open, 0, 0) && newer_count == 0, "a version too old to join: not counted");
		CHECK(wanted(maximum, _listing_open, 0, 0) && newer_count == 0, "the newest joinable version's: not counted");
		CHECK(wanted(maximum + 1, _listing_open, 0, 0) && newer_count == 1, "a newer version's: counted");
		CHECK(wanted(maximum + 1, _listing_closed, 0, 0) && newer_count == 1, "a newer version's tombstone");
		CHECK(!wanted(maximum + 1, _listing_open, LIVE_WINDOW + 60, 0) && newer_count == 2,
			"a newer version's long gone: counted, not let in");
		CHECK(!listing_wanted((const unsigned char *)"HL", 2, 0, key_hash), "a short message");
		CHECK(version_joinable(minimum) && version_joinable(current) && version_joinable(maximum) &&
			!version_joinable(minimum - 1) && !version_joinable(maximum + 1), "the join range");
	}
	else CASE("burst")
	{
		/* a public broker's kept slots, as test.mosquitto.org sent them on
		2026-10-24 (about 1,450, some 70 live): this version's games long
		gone, hosts of versions this build cannot join still running, and of
		versions it joins long gone; the live games (this version's and
		older ones it joins) spread among them */
		int host, queued, live_queued = 0, index;

		for (host = 0; host < 1470; host++)
		{
			int live = host % 21 == 20;

			if (live)
				heard(host % 2 ? current : current - 1 - host % 5, _listing_open, host % 7, 1, host);
			else if (host % 3 == 0)
				heard(current, _listing_open, 86400 * (1 + host % 30), 1, host);
			else if (host % 3 == 1)
				heard(host % 2 ? minimum - 1 - host % 5 : current - 1 - host % 20, _listing_open, host % 2 ? 30 : 86400,
					1, host);
			else
				heard(maximum + 1 + host % 3, _listing_open, 5, 1, host);
		}
		queued = lobby.queue_count;
		for (index = 0; index < lobby.queue_count; index++)
		{
			const unsigned char *key = lobby.queue[index].payload + 14;

			host = key[0] << 8 | key[1];
			live_queued += host % 21 == 20;
		}
		CHECK(queued == MAXIMUM_QUEUED, "%d places taken, of %d (the rest kept to be marked)", queued, MAXIMUM_QUEUED);
		CHECK(live_queued == 70, "%d of the 70 live games had a place in the queue (%d places taken)",
			live_queued, queued);
	}
	else
		return 2;
	return 0;
}
