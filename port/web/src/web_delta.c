/* Delta's legacy number in the browser (ChupathingyCE's network family:
docs/delta.md in their repository, and src/delta/README.md here).

The network version a host announces and the range of hosts' versions a
client joins. OpenCE's builds join only hosts of their exact version;
ChupathingyCE's join back to the newest breaking version of the
compatibility table (delta.h's DELTA_LEGACY_VERSIONS), and so does the
browser build: network_client_manager.c's join check and p2p_lobby.c's
listings read delta_legacy_minimum and _maximum (both under HALO_WEB).

The signed legacy table (ChupathingyCE's delta.c, from which this file's
table code is taken, CC0): a JSON document signed with their Ed25519 key
(delta/delta_key.h). Its rows are by wire, and the browser build's wire
(DELTA_WIRE, "opence-web") has none, so the numbers stay the built-in ones;
what the browser takes from a table is its kill switch
(disabled_capabilities: a Delta capability found unsafe, which Delta Peer
then never offers) and its serial, for Delta Peer to say. Where tables come
from, the newest serial winning (an equal or older one is never taken):
- the cache, delta_legacy.signed in the save root, read at start;
- the page (online_client.js), which fetches the document and its
  signature from this site's server (server.mjs relays halo.milenko.org's
  /v1/delta/legacy, which has no CORS) or GitHub, at start and every four
  hours, and hands them to web_delta_offer_table;
- another machine (delta_legacy_offer: Delta Peer).
Everything from outside is hostile: its size is checked first, then the
signature, and only then is the document read, by a strict parser that
skips what it does not know. A table that does not verify is dropped and
logged; nothing is shown. */

#include "platform.h"
#include "port_config.h"
#include "halo_port_limits.h"
#include "delta/delta.h"
#include "delta/delta_key.h"

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <emscripten/emscripten.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DELTA_LEGACY_FORMAT 1
#define DELTA_SIGNATURE_SIZE 64
#define DELTA_KEY_COUNT (sizeof(delta_public_keys) / sizeof(*delta_public_keys))
/* a serial's epoch, its top byte (delta_key.h) */
#define DELTA_EPOCH(serial) ((unsigned int)(serial) >> 24)
#define DELTA_CACHE_NAME "delta_legacy.signed"

enum
{
	/* (how deep the document's objects and arrays may nest) */
	JSON_DEPTH = 8,
	/* (the most disabled capabilities read) */
	MAXIMUM_DISABLED = 64,
	/* (an OpenCE network version is an unsigned 16-bit number) */
	MAXIMUM_VERSION = 65535,
};

/* the capability registry's names (delta.h), as the table spells them */
static const char *const delta_capability_names[] =
{
	"platform", "profile", "server_messages", "chat", "ce_maps", "md_maps", "coop", "ai_sync", "vote",
	"console_slots", "moderation",
};
_Static_assert(sizeof(delta_capability_names) / sizeof(*delta_capability_names) == NUMBER_OF_DELTA_CAPABILITIES,
	"a name for each capability");

struct delta_table
{
	unsigned int serial;
	long long issued;
	/* (this build's wire's row, if the table has one) */
	int has_row;
	int announce, minimum, maximum;
	char follows[DELTA_FOLLOWS_SIZE];
	unsigned long disabled;
};

static pthread_mutex_t delta_lock = PTHREAD_MUTEX_INITIALIZER;
/* (the cache's file: one writer at a time, taken before delta_lock) */
static pthread_mutex_t delta_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t delta_once = PTHREAD_ONCE_INIT;

static struct
{
	int announce, minimum, maximum;
	char follows[DELTA_FOLLOWS_SIZE];
	unsigned long disabled;
	/* the signed table in use (malloc'd), and its serial; none: 0 */
	unsigned int serial;
	char *signed_table;
	int signed_size;
} delta;

/* ---------- the built-in numbers */

enum
{
	additive,
	breaking,
};

static const struct
{
	int version;
	int kind;
} delta_versions[] =
{
#define DELTA_VERSION_ROW(version, first_build, kind) { version, kind },
	DELTA_LEGACY_VERSIONS(DELTA_VERSION_ROW)
#undef DELTA_VERSION_ROW
};

/* the oldest host version this build joins: the newest breaking version up
to its own, when its own has a row; else its own alone */
static int delta_built_in_minimum(void)
{
	int minimum = HALO_PORT_NETWORK_VERSION;
	int has_row = 0;
	size_t index;

	for (index = 0; index < sizeof(delta_versions) / sizeof(*delta_versions); index++)
	{
		if (delta_versions[index].version == HALO_PORT_NETWORK_VERSION)
			has_row = 1;
	}
	if (!has_row)
		return HALO_PORT_NETWORK_VERSION;
	for (index = 0; index < sizeof(delta_versions) / sizeof(*delta_versions); index++)
	{
		if (delta_versions[index].version <= HALO_PORT_NETWORK_VERSION && delta_versions[index].kind == breaking)
			minimum = delta_versions[index].version;
	}
	return minimum;
}

/* ---------- the document (JSON, hostile) */

struct json
{
	const char *at;
	const char *end;
	int depth;
};

static void json_space(struct json *json)
{
	while (json->at < json->end &&
		(*json->at == ' ' || *json->at == '\t' || *json->at == '\n' || *json->at == '\r'))
	{
		json->at++;
	}
}

static int json_take(struct json *json, char c)
{
	json_space(json);
	if (json->at < json->end && *json->at == c)
	{
		json->at++;
		return 1;
	}
	return 0;
}

static int delta_hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* a string: its text between the quotes, its escapes checked but not
decoded (escaped: whether it has any) */
static int json_string(struct json *json, const char **text, int *length, int *escaped)
{
	const char *start;

	*escaped = 0;
	if (!json_take(json, '"'))
		return 0;
	start = json->at;
	while (json->at < json->end)
	{
		unsigned char c = (unsigned char)*json->at;

		if (c == '"')
		{
			*text = start;
			*length = (int)(json->at - start);
			json->at++;
			return 1;
		}
		if (c < 0x20)
			return 0;
		if (c == '\\')
		{
			*escaped = 1;
			if (++json->at >= json->end)
				return 0;
			c = (unsigned char)*json->at;
			if (c == 'u')
			{
				int index;

				for (index = 1; index <= 4; index++)
				{
					if (json->at + index >= json->end || delta_hex_digit(json->at[index]) < 0)
						return 0;
				}
				json->at += 4;
			}
			else if (!strchr("\"\\/bfnrt", c))
				return 0;
		}
		json->at++;
	}
	return 0;
}

static int json_is(const char *text, int length, const char *name)
{
	return (size_t)length == strlen(name) && !memcmp(text, name, (size_t)length);
}

/* a whole number from minimum to maximum (no sign, fraction or exponent) */
static int json_integer(struct json *json, long long minimum, long long maximum, long long *value)
{
	long long number = 0;
	const char *start;

	json_space(json);
	start = json->at;
	while (json->at < json->end && *json->at >= '0' && *json->at <= '9')
	{
		if (json->at - start >= 16)
			return 0;
		number = number * 10 + (*json->at - '0');
		json->at++;
	}
	if (json->at == start || (json->at - start > 1 && *start == '0'))
		return 0;
	if (json->at < json->end && (*json->at == '.' || *json->at == 'e' || *json->at == 'E'))
		return 0;
	if (number < minimum || number > maximum)
		return 0;
	*value = number;
	return 1;
}

static int json_literal(struct json *json, const char *literal)
{
	size_t length = strlen(literal);

	if ((size_t)(json->end - json->at) < length || memcmp(json->at, literal, length))
		return 0;
	json->at += length;
	return 1;
}

static int json_digits(struct json *json)
{
	const char *start = json->at;

	while (json->at < json->end && *json->at >= '0' && *json->at <= '9')
		json->at++;
	return json->at > start;
}

/* any value, checked and passed over */
static int json_skip(struct json *json)
{
	const char *text;
	int length, escaped;
	char c;

	json_space(json);
	if (json->at >= json->end)
		return 0;
	c = *json->at;
	if (c == '"')
		return json_string(json, &text, &length, &escaped);
	if (c == '{' || c == '[')
	{
		char close = c == '{' ? '}' : ']';
		int ok = 1;

		if (++json->depth > JSON_DEPTH)
			return 0;
		json->at++;
		if (!json_take(json, close))
		{
			do
			{
				if (c == '{' && (!json_string(json, &text, &length, &escaped) || !json_take(json, ':')))
					return 0;
				if (!json_skip(json))
					return 0;
			} while (json_take(json, ','));
			ok = json_take(json, close);
		}
		json->depth--;
		return ok;
	}
	if (c == 't')
		return json_literal(json, "true");
	if (c == 'f')
		return json_literal(json, "false");
	if (c == 'n')
		return json_literal(json, "null");
	if (c == '-')
		json->at++;
	if (json->at < json->end && *json->at == '0')
		json->at++;
	else if (!json_digits(json))
		return 0;
	if (json->at < json->end && *json->at == '.')
	{
		json->at++;
		if (!json_digits(json))
			return 0;
	}
	if (json->at < json->end && (*json->at == 'e' || *json->at == 'E'))
	{
		json->at++;
		if (json->at < json->end && (*json->at == '+' || *json->at == '-'))
			json->at++;
		if (!json_digits(json))
			return 0;
	}
	return 1;
}

/* a row's "follows": an OpenCE build's tag, letters, digits, '.', '-' and
'_' */
static int json_follows(struct json *json, struct delta_table *table)
{
	const char *text;
	int length, escaped, index;

	if (!json_string(json, &text, &length, &escaped) || escaped || length < 1 || length >= DELTA_FOLLOWS_SIZE)
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = text[index];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
			c == '_'))
		{
			return 0;
		}
	}
	memcpy(table->follows, text, (size_t)length);
	table->follows[length] = 0;
	return 1;
}

/* a wire's row: {"announce": A, "minimum": Mi, "maximum": Ma}, and
optionally "follows": "build-N" */
static int json_row(struct json *json, struct delta_table *table)
{
	int seen = 0;

	if (!json_take(json, '{'))
		return 0;
	if (json_take(json, '}'))
		return 0;
	do
	{
		const char *key;
		int length, escaped, bit = 0;
		int *field = NULL;
		long long value;

		if (!json_string(json, &key, &length, &escaped) || !json_take(json, ':'))
			return 0;
		if (!escaped && json_is(key, length, "announce"))
			field = &table->announce, bit = 1;
		else if (!escaped && json_is(key, length, "minimum"))
			field = &table->minimum, bit = 2;
		else if (!escaped && json_is(key, length, "maximum"))
			field = &table->maximum, bit = 4;
		else if (!escaped && json_is(key, length, "follows"))
		{
			/* (once) */
			if ((seen & 8) || !json_follows(json, table))
				return 0;
			seen |= 8;
			continue;
		}
		if (!field)
		{
			if (!json_skip(json))
				return 0;
			continue;
		}
		if ((seen & bit) || !json_integer(json, 1, MAXIMUM_VERSION, &value))
			return 0;
		*field = (int)value;
		seen |= bit;
	} while (json_take(json, ','));
	return json_take(json, '}') && (seen & 7) == 7;
}

/* "wires": {"<wire>": row, ...}: this build's row read, the others checked */
static int json_wires(struct json *json, struct delta_table *table)
{
	if (!json_take(json, '{'))
		return 0;
	if (json_take(json, '}'))
		return 1;
	json->depth++;
	do
	{
		const char *key;
		int length, escaped;

		if (!json_string(json, &key, &length, &escaped) || !json_take(json, ':'))
			return 0;
		if (!escaped && json_is(key, length, DELTA_WIRE))
		{
			/* (one row a wire: two would read differently elsewhere) */
			if (table->has_row || !json_row(json, table))
				return 0;
			table->has_row = 1;
		}
		else if (!json_skip(json))
			return 0;
	} while (json_take(json, ','));
	json->depth--;
	return json_take(json, '}');
}

/* "disabled_capabilities": ["<name>", ...]: the names this build knows */
static int json_disabled(struct json *json, struct delta_table *table)
{
	int count = 0;

	if (!json_take(json, '['))
		return 0;
	if (json_take(json, ']'))
		return 1;
	do
	{
		const char *name;
		int length, escaped, index;

		if (++count > MAXIMUM_DISABLED || !json_string(json, &name, &length, &escaped))
			return 0;
		for (index = 0; !escaped && index < NUMBER_OF_DELTA_CAPABILITIES; index++)
		{
			if (json_is(name, length, delta_capability_names[index]))
				table->disabled |= 1UL << index;
		}
	} while (json_take(json, ','));
	return json_take(json, ']');
}

/* the document into table; 0 (and why) if it is not one this build reads */
static int delta_table_parse(const char *document, size_t size, struct delta_table *table, const char **why)
{
	struct json json = { document, document + size, 0 };
	long long format = 0, value;
	int seen = 0;

	memset(table, 0, sizeof(*table));
	*why = "it is not a legacy table";
	if (size > DELTA_LEGACY_DOCUMENT_SIZE || !json_take(&json, '{') || json_take(&json, '}'))
		return 0;
	json.depth = 1;
	do
	{
		const char *key;
		int length, escaped, bit = 0, ok;

		if (!json_string(&json, &key, &length, &escaped) || !json_take(&json, ':'))
			return 0;
		if (escaped)
			ok = json_skip(&json);
		else if (json_is(key, length, "delta_legacy"))
			bit = 1, ok = json_integer(&json, 0, 1000000, &format);
		else if (json_is(key, length, "serial"))
		{
			/* (not 0xFFFFFFFF: on the wire that is a machine taking no tables) */
			bit = 2, ok = json_integer(&json, 1, 4294967294LL, &value);
			if (ok)
				table->serial = (unsigned int)value;
		}
		else if (json_is(key, length, "issued"))
			bit = 4, ok = json_integer(&json, 0, 1LL << 53, &table->issued);
		else if (json_is(key, length, "wires"))
			bit = 8, ok = json_wires(&json, table);
		else if (json_is(key, length, "disabled_capabilities"))
			bit = 16, ok = json_disabled(&json, table);
		else
			ok = json_skip(&json);
		/* (a key twice would read differently elsewhere) */
		if (!ok || (seen & bit))
			return 0;
		seen |= bit;
	} while (json_take(&json, ','));
	if (!json_take(&json, '}'))
		return 0;
	json_space(&json);
	if (json.at != json.end || (seen & (1 | 2 | 8)) != (1 | 2 | 8))
		return 0;
	if (format != DELTA_LEGACY_FORMAT)
	{
		*why = "its format is newer than this build reads";
		return 0;
	}
	if (table->has_row && !(table->minimum <= table->announce && table->announce <= table->maximum))
	{
		*why = "its row for this build's wire is not a range";
		return 0;
	}
	return 1;
}

/* whether the table's row (if it has one) widens the built-in numbers: it
may announce a newer number and join a wider range, never less */
static int delta_table_widens(const struct delta_table *table)
{
	return !table->has_row || (table->announce >= HALO_PORT_NETWORK_VERSION &&
		table->minimum <= delta_built_in_minimum() && table->maximum >= HALO_PORT_NETWORK_VERSION);
}

/* ---------- the signature */

static int delta_key_set(const unsigned char *key)
{
	unsigned char any = 0;
	int index;

	for (index = 0; index < 32; index++)
		any |= key[index];
	return any != 0;
}

static int delta_has_key(void)
{
	size_t index;

	for (index = 0; index < DELTA_KEY_COUNT; index++)
	{
		if (delta_key_set(delta_public_keys[index]))
			return 1;
	}
	return 0;
}

/* the signature's 128 hex digits (and white space after them) into bytes */
static int delta_signature_parse(const char *text, size_t length, unsigned char *signature)
{
	size_t index;

	while (length > DELTA_SIGNATURE_SIZE * 2 &&
		(text[length - 1] == '\n' || text[length - 1] == '\r' || text[length - 1] == ' ' || text[length - 1] == '\t'))
	{
		length--;
	}
	if (length != DELTA_SIGNATURE_SIZE * 2)
		return 0;
	for (index = 0; index < DELTA_SIGNATURE_SIZE; index++)
	{
		int high = delta_hex_digit(text[index * 2]);
		int low = delta_hex_digit(text[index * 2 + 1]);

		if (high < 0 || low < 0)
			return 0;
		signature[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

/* the key that signed the document (its index in delta_public_keys), or -1 */
static int delta_signature_signer(const char *document, size_t size, const unsigned char *signature)
{
	size_t index;

	for (index = 0; index < DELTA_KEY_COUNT; index++)
	{
		/* (Monocypher's check turns away an S past the group's order) */
		if (delta_key_set(delta_public_keys[index]) &&
			crypto_ed25519_check(signature, delta_public_keys[index], (const unsigned char *)document, size) == 0)
		{
			return (int)index;
		}
	}
	return -1;
}

/* ---------- the table in use */

enum delta_result
{
	_delta_taken,
	_delta_not_newer,
	_delta_invalid,
};

static void delta_use_built_in(void)
{
	delta.announce = HALO_PORT_NETWORK_VERSION;
	delta.minimum = delta_built_in_minimum();
	delta.maximum = HALO_PORT_NETWORK_VERSION;
	delta.follows[0] = 0;
	delta.disabled = 0;
}

static void delta_cache_path(char *path, size_t size)
{
	snprintf(path, size, "%s/%s", platform_save_root(), DELTA_CACHE_NAME);
}

/* the signed table written to the cache: a new file beside it, then moved
over it */
static void delta_cache_write(const char *signed_table, int size)
{
	char path[1024], temporary[1100];
	FILE *file;
	int ok;

	delta_cache_path(path, sizeof(path));
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	file = fopen(temporary, "wb");
	if (!file)
		return;
	ok = fwrite(signed_table, 1, (size_t)size, file) == (size_t)size;
	ok = fclose(file) == 0 && ok;
	if (ok && rename(temporary, path) != 0)
	{
		remove(path);
		ok = rename(temporary, path) == 0;
	}
	if (!ok)
	{
		remove(temporary);
		platform_log("Delta: the legacy table could not be cached");
	}
}

/* a document and its signature (hex) from source: checked, and used if it
is newer than the one in use */
static enum delta_result delta_take(const char *document, size_t size, const char *signature_text,
	size_t signature_length, const char *source, int cache)
{
	unsigned char signature[DELTA_SIGNATURE_SIZE];
	struct delta_table table;
	const char *why;
	char *signed_table;
	int signed_size, signer = -1;

	if (size > DELTA_LEGACY_DOCUMENT_SIZE)
	{
		platform_log("Delta: dropped the legacy table from %s: it is too large", source);
		return _delta_invalid;
	}
	if (!delta_signature_parse(signature_text, signature_length, signature) ||
		(signer = delta_signature_signer(document, size, signature)) < 0)
	{
		platform_log("Delta: dropped the legacy table from %s: its signature does not match", source);
		return _delta_invalid;
	}
	if (!delta_table_parse(document, size, &table, &why))
	{
		platform_log("Delta: dropped the legacy table from %s: %s", source, why);
		return _delta_invalid;
	}
	/* (only the keys whose last epoch reaches it sign a serial's epoch: the
	recovery key alone opens a new one, delta_key.h) */
	if (DELTA_EPOCH(table.serial) > delta_key_last_epochs[signer])
	{
		platform_log("Delta: dropped the legacy table %u from %s: its epoch (%u) is past its key's last (%u)",
			table.serial, source, DELTA_EPOCH(table.serial), (unsigned int)delta_key_last_epochs[signer]);
		return _delta_invalid;
	}
	if (!delta_table_widens(&table))
	{
		platform_log("Delta: dropped the legacy table %u from %s: it narrows this build's numbers", table.serial,
			source);
		return _delta_invalid;
	}
	signed_size = DELTA_SIGNATURE_SIZE * 2 + 1 + (int)size;
	signed_table = malloc((size_t)signed_size);
	if (!signed_table)
		return _delta_invalid;
	memcpy(signed_table, signature_text, DELTA_SIGNATURE_SIZE * 2);
	signed_table[DELTA_SIGNATURE_SIZE * 2] = '\n';
	memcpy(signed_table + DELTA_SIGNATURE_SIZE * 2 + 1, document, size);

	pthread_mutex_lock(&delta_lock);
	if (table.serial <= delta.serial)
	{
		pthread_mutex_unlock(&delta_lock);
		free(signed_table);
		return _delta_not_newer;
	}
	free(delta.signed_table);
	delta.signed_table = signed_table;
	delta.signed_size = signed_size;
	delta.serial = table.serial;
	delta_use_built_in();
	if (table.has_row)
	{
		delta.announce = table.announce;
		delta.minimum = table.minimum;
		delta.maximum = table.maximum;
		memcpy(delta.follows, table.follows, sizeof(delta.follows));
	}
	delta.disabled = table.disabled;
	/* (read here: the cache's table is taken inside delta_once's call) */
	{
		int announce = delta.announce, minimum = delta.minimum, maximum = delta.maximum;

		pthread_mutex_unlock(&delta_lock);
		platform_log("Delta: legacy table %u from %s: announcing %d, joining %d to %d%s; %d capabilities turned off",
			table.serial, source, announce, minimum, maximum,
			table.has_row ? "" : " (no row for " DELTA_WIRE ": the built-in numbers)",
			__builtin_popcountl(table.disabled));
	}
	/* (from the cache's own copy, and only while the table is still the
	one in use: another thread may take a newer one, and free this one,
	meanwhile) */
	if (cache)
	{
		char *copy = malloc((size_t)signed_size);
		int current;

		if (copy)
		{
			pthread_mutex_lock(&delta_cache_lock);
			pthread_mutex_lock(&delta_lock);
			current = delta.serial == table.serial && delta.signed_table == signed_table;
			if (current)
				memcpy(copy, signed_table, (size_t)signed_size);
			pthread_mutex_unlock(&delta_lock);
			if (current)
				delta_cache_write(copy, signed_size);
			pthread_mutex_unlock(&delta_cache_lock);
			free(copy);
		}
	}
	return _delta_taken;
}

/* a signed table (signature, line feed, document) from source */
static enum delta_result delta_take_signed(const char *signed_table, size_t size, const char *source, int cache)
{
	if (!signed_table || size < DELTA_SIGNATURE_SIZE * 2 + 1 || size > DELTA_LEGACY_SIGNED_SIZE ||
		signed_table[DELTA_SIGNATURE_SIZE * 2] != '\n')
	{
		platform_log("Delta: dropped the legacy table from %s: it is not a signed table", source);
		return _delta_invalid;
	}
	return delta_take(signed_table + DELTA_SIGNATURE_SIZE * 2 + 1, size - DELTA_SIGNATURE_SIZE * 2 - 1,
		signed_table, DELTA_SIGNATURE_SIZE * 2, source, cache);
}

static void delta_load(void)
{
	char path[1024];
	char *file;
	size_t size = 0;

	pthread_mutex_lock(&delta_lock);
	delta_use_built_in();
	pthread_mutex_unlock(&delta_lock);
	platform_log("Delta: network version %d, joining hosts of %d to %d", HALO_PORT_NETWORK_VERSION,
		delta_built_in_minimum(), HALO_PORT_NETWORK_VERSION);
	delta_cache_path(path, sizeof(path));
	file = config_file_read(path, &size);
	if (file)
	{
		delta_take_signed(file, size, "the cache", 0);
		free(file);
	}
}

/* ---------- the page's side */

/* the page puts a signed table here (the signature's hex digits, a line
feed, the document), then calls web_delta_offer_table with its size and
where it came from */
static char delta_offered[DELTA_LEGACY_SIGNED_SIZE];

EMSCRIPTEN_KEEPALIVE char *web_delta_table_buffer(void)
{
	return delta_offered;
}

EMSCRIPTEN_KEEPALIVE int web_delta_table_buffer_size(void)
{
	return (int)sizeof(delta_offered);
}

/* 1 if the table was taken (newer, and it checks), 0 if not newer, -1 if
it was dropped */
EMSCRIPTEN_KEEPALIVE int web_delta_offer_table(int size, int from_github)
{
	enum delta_result result;

	pthread_once(&delta_once, delta_load);
	if (size < 0 || size > (int)sizeof(delta_offered) || !delta_has_key())
		return -1;
	result = delta_take_signed(delta_offered, (size_t)size, from_github ? "GitHub" : "Delta List", 1);
	return result == _delta_taken ? 1 : result == _delta_not_newer ? 0 : -1;
}

/* the serial in use, so the page asks for no table it has */
EMSCRIPTEN_KEEPALIVE unsigned int web_delta_table_serial(void)
{
	return delta_legacy_serial();
}

/* ---------- public code */

void delta_legacy_start(void)
{
	pthread_once(&delta_once, delta_load);
}

int delta_legacy_announce(void)
{
	int number;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	number = delta.announce;
	pthread_mutex_unlock(&delta_lock);
	return number;
}

int delta_legacy_minimum(void)
{
	int number;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	number = delta.minimum;
	pthread_mutex_unlock(&delta_lock);
	return number;
}

int delta_legacy_maximum(void)
{
	int number;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	number = delta.maximum;
	pthread_mutex_unlock(&delta_lock);
	return number;
}

unsigned int delta_legacy_serial(void)
{
	unsigned int serial;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	serial = delta.serial;
	pthread_mutex_unlock(&delta_lock);
	return serial;
}

int delta_legacy_signed(char *buffer, int size)
{
	int used = 0;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	if (delta.signed_table && delta.signed_size <= size)
	{
		memcpy(buffer, delta.signed_table, (size_t)delta.signed_size);
		used = delta.signed_size;
	}
	pthread_mutex_unlock(&delta_lock);
	return used;
}

int delta_legacy_offer(const char *signed_table, int size)
{
	pthread_once(&delta_once, delta_load);
	if (size < 0)
		return 0;
	return delta_take_signed(signed_table, (size_t)size, "another machine", 1) == _delta_taken;
}

int delta_capability_disabled(int capability)
{
	int disabled;

	if (capability < 0 || capability >= NUMBER_OF_DELTA_CAPABILITIES)
		return 0;
	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	disabled = (delta.disabled >> capability) & 1;
	pthread_mutex_unlock(&delta_lock);
	return disabled;
}

void delta_legacy_following(char *text, int size)
{
	unsigned int serial;
	int announce;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	serial = delta.serial;
	announce = delta.announce;
	pthread_mutex_unlock(&delta_lock);
	if (size <= 0)
		return;
	snprintf(text, (size_t)size, "Following OpenCE network version %d", announce);
	if (serial)
		snprintf(text + strlen(text), (size_t)size - strlen(text), " (table %u)", serial);
	else
		snprintf(text + strlen(text), (size_t)size - strlen(text), " (built in)");
}

/* (the browser build reads no config.toml table of its own) */
int delta_legacy_override(void)
{
	return 0;
}

int delta_legacy_relay(void)
{
	return delta_has_key();
}
