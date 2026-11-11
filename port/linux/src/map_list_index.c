/*
MAP_LIST_INDEX.C

The approved map list's signature and serial (map_list_index.h), with Monocypher
(port/third_party/monocypher) and the fork's keys (map_index_key.h).
Everything here is from outside: sizes are checked first, and nothing is
read past them.
*/

#include "map_list_index.h"
#include "map_index_key.h"

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <stdlib.h>
#include <string.h>

#define MAP_LIST_INDEX_KEY_COUNT (sizeof(map_index_public_keys) / sizeof(*map_index_public_keys))
/* (a serial: unix seconds, or any number that grows; 19 digits fit) */
#define MAP_LIST_INDEX_SERIAL_DIGITS 19

static int map_index_hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int map_index_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int map_list_index_signature_parse(const char *text, size_t length, unsigned char *signature)
{
	size_t start = 0, end = length, index;

	while (start < end && map_index_space(text[start]))
		start++;
	while (end > start && map_index_space(text[end - 1]))
		end--;
	if (end - start != MAP_LIST_INDEX_SIGNATURE_SIZE * 2)
		return 0;
	for (index = 0; index < MAP_LIST_INDEX_SIGNATURE_SIZE; index++)
	{
		int high = map_index_hex_digit(text[start + index * 2]);
		int low = map_index_hex_digit(text[start + index * 2 + 1]);

		if (high < 0 || low < 0)
			return 0;
		signature[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

int map_list_index_signature_check(const unsigned char *signature, const char *document, size_t size)
{
	size_t context_size = strlen(MAP_LIST_INDEX_CONTEXT), index;
	unsigned char *message;
	int good = 0;

	if (size > MAP_LIST_INDEX_MAXIMUM_SIZE)
		return 0;
	message = malloc(context_size + size);
	if (!message)
		return 0;
	memcpy(message, MAP_LIST_INDEX_CONTEXT, context_size);
	memcpy(message + context_size, document, size);
	for (index = 0; index < MAP_LIST_INDEX_KEY_COUNT && !good; index++)
	{
		unsigned char any = 0;
		int byte;

		for (byte = 0; byte < 32; byte++)
			any |= map_index_public_keys[index][byte];
		/* (Monocypher's check turns away an S past the group's order) */
		if (any && crypto_ed25519_check(signature, map_index_public_keys[index], message, context_size + size) == 0)
			good = 1;
	}
	free(message);
	return good;
}

int map_list_index_format(const char *document, size_t size)
{
	size_t length = strlen(MAP_LIST_INDEX_FORMAT);

	return size >= length && !memcmp(document, MAP_LIST_INDEX_FORMAT, length) &&
		(size == length || document[length] == '\r' || document[length] == '\n');
}

unsigned long long map_list_index_serial(const char *document, size_t size)
{
	static const char key[] = "# serial ";
	size_t at = 0;

	/* (the comment lines it opens with: the first entry ends them) */
	while (at < size && document[at] == '#')
	{
		size_t end = at;

		while (end < size && document[end] != '\n')
			end++;
		if (end - at > sizeof(key) - 1 && !memcmp(document + at, key, sizeof(key) - 1))
		{
			unsigned long long serial = 0;
			size_t digit = at + sizeof(key) - 1;
			int count = 0;

			for (; digit < end && document[digit] >= '0' && document[digit] <= '9'; digit++)
			{
				if (++count > MAP_LIST_INDEX_SERIAL_DIGITS)
					return 0;
				serial = serial * 10 + (unsigned long long)(document[digit] - '0');
			}
			/* (nothing after the digits but the line's end) */
			while (digit < end && map_index_space(document[digit]))
				digit++;
			return count && digit == end ? serial : 0;
		}
		at = end + 1;
	}
	return 0;
}

int map_list_index_split(const char *text, size_t size, unsigned char *signature, size_t *document_offset)
{
	size_t line = MAP_LIST_INDEX_SIGNATURE_SIZE * 2;

	if (size <= line + 1 || text[line] != '\n' || size - line - 1 > MAP_LIST_INDEX_MAXIMUM_SIZE ||
		!map_list_index_signature_parse(text, line, signature))
	{
		return 0;
	}
	*document_offset = line + 1;
	return 1;
}
