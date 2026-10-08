/*
TORRENT_BENCODE.C

Bencoding (torrent_bencode.h): a parser into a tree of nodes over the
bytes parsed, and a writer. Both are written for untrusted input: a value
that is cut short, nested too deep, holds too many nodes or names a length
past the data is refused as a whole.
*/

#include "torrent_bencode.h"

#include <stdio.h>
#include <string.h>

#define MAXIMUM_DEPTH 32

struct parser
{
	struct bencode_document *document;
	const unsigned char *data;
	int size;
	int position;
	int depth;
};

static int node_new(struct parser *parser, enum bencode_type type)
{
	struct bencode_node *node;

	if (parser->document->count >= BENCODE_MAXIMUM_NODES)
		return -1;
	node = &parser->document->nodes[parser->document->count];
	memset(node, 0, sizeof(*node));
	node->type = (unsigned char)type;
	node->next = -1;
	node->child = -1;
	return parser->document->count++;
}

/* a decimal number up to the terminator (which is consumed): 0 on success */
static int parse_number(struct parser *parser, unsigned char terminator, long long *value)
{
	long long result = 0;
	int negative = 0;
	int digits = 0;

	if (parser->position < parser->size && parser->data[parser->position] == '-')
	{
		negative = 1;
		parser->position++;
	}
	while (parser->position < parser->size)
	{
		unsigned char character = parser->data[parser->position];

		if (character == terminator)
		{
			parser->position++;
			if (!digits)
				return -1;
			*value = negative ? -result : result;
			return 0;
		}
		if (character < '0' || character > '9' || digits >= 18)
			return -1;
		result = result * 10 + (character - '0');
		digits++;
		parser->position++;
	}
	return -1;
}

static int parse_value(struct parser *parser);

static int parse_children(struct parser *parser, int node, int dictionary)
{
	int last = -1;
	int count = 0;

	if (++parser->depth > MAXIMUM_DEPTH)
		return -1;
	while (parser->position < parser->size && parser->data[parser->position] != 'e')
	{
		int child;

		if (dictionary && (count & 1) == 0 && (parser->data[parser->position] < '0' ||
			parser->data[parser->position] > '9'))
		{
			/* (a key must be a string) */
			return -1;
		}
		child = parse_value(parser);
		if (child < 0)
			return -1;
		if (last < 0)
			parser->document->nodes[node].child = child;
		else
			parser->document->nodes[last].next = child;
		last = child;
		count++;
	}
	if (parser->position >= parser->size || (dictionary && (count & 1)))
		return -1;
	parser->position++;
	parser->document->nodes[node].count = dictionary ? count / 2 : count;
	parser->depth--;
	return 0;
}

static int parse_value(struct parser *parser)
{
	unsigned char character;
	int node;

	if (parser->position >= parser->size)
		return -1;
	character = parser->data[parser->position];
	if (character == 'i')
	{
		long long value;

		parser->position++;
		node = node_new(parser, _bencode_integer);
		if (node < 0 || parse_number(parser, 'e', &value) != 0)
			return -1;
		parser->document->nodes[node].integer = value;
		return node;
	}
	if (character == 'l' || character == 'd')
	{
		parser->position++;
		node = node_new(parser, character == 'l' ? _bencode_list : _bencode_dictionary);
		if (node < 0 || parse_children(parser, node, character == 'd') != 0)
			return -1;
		return node;
	}
	if (character >= '0' && character <= '9')
	{
		long long length;

		node = node_new(parser, _bencode_string);
		if (node < 0 || parse_number(parser, ':', &length) != 0 || length < 0 ||
			length > parser->size - parser->position)
		{
			return -1;
		}
		parser->document->nodes[node].bytes = parser->data + parser->position;
		parser->document->nodes[node].length = (int)length;
		parser->position += (int)length;
		return node;
	}
	return -1;
}

int bencode_parse(struct bencode_document *document, const void *data, int size)
{
	struct parser parser;

	document->count = 0;
	parser.document = document;
	parser.data = data;
	parser.size = size;
	parser.position = 0;
	parser.depth = 0;
	if (size <= 0 || parse_value(&parser) != 0)
		return -1;
	return parser.position;
}

int bencode_find(const struct bencode_document *document, int node, const char *key)
{
	const struct bencode_node *dictionary;
	int child;
	int length = (int)strlen(key);

	if (node < 0 || node >= document->count)
		return -1;
	dictionary = &document->nodes[node];
	if (dictionary->type != _bencode_dictionary)
		return -1;
	child = dictionary->child;
	while (child >= 0)
	{
		const struct bencode_node *name = &document->nodes[child];

		/* (a key, then its value; the parser made them pairs) */
		if (name->next < 0)
			break;
		if (name->type == _bencode_string && name->length == length && !memcmp(name->bytes, key, (size_t)length))
			return name->next;
		child = document->nodes[name->next].next;
	}
	return -1;
}

int bencode_find_typed(const struct bencode_document *document, int node, const char *key, enum bencode_type type)
{
	int value = bencode_find(document, node, key);

	return value >= 0 && document->nodes[value].type == type ? value : -1;
}

long long bencode_integer(const struct bencode_document *document, int node, long long fallback)
{
	if (node < 0 || node >= document->count || document->nodes[node].type != _bencode_integer)
		return fallback;
	return document->nodes[node].integer;
}

int bencode_string(const struct bencode_document *document, int node, const unsigned char **bytes, int *length)
{
	if (node < 0 || node >= document->count || document->nodes[node].type != _bencode_string)
		return 0;
	*bytes = document->nodes[node].bytes;
	*length = document->nodes[node].length;
	return 1;
}

const unsigned char *bencode_string_of(const struct bencode_document *document, int node, int length)
{
	const unsigned char *bytes;
	int actual;

	if (!bencode_string(document, node, &bytes, &actual) || actual != length)
		return NULL;
	return bytes;
}

void bencode_text(const struct bencode_document *document, int node, char *text, int size)
{
	const unsigned char *bytes;
	int length;

	text[0] = 0;
	if (size <= 1 || !bencode_string(document, node, &bytes, &length))
		return;
	if (length > size - 1)
		length = size - 1;
	memcpy(text, bytes, (size_t)length);
	text[length] = 0;
}

/* ---------- writing */

static void write_bytes(struct bencode_writer *writer, const void *bytes, int length)
{
	if (writer->overflow || length < 0 || length > writer->capacity - writer->size)
	{
		writer->overflow = 1;
		return;
	}
	memcpy(writer->data + writer->size, bytes, (size_t)length);
	writer->size += length;
}

void bencode_writer_start(struct bencode_writer *writer, void *buffer, int capacity)
{
	writer->data = buffer;
	writer->size = 0;
	writer->capacity = capacity;
	writer->overflow = 0;
}

void bencode_write_integer(struct bencode_writer *writer, long long value)
{
	char text[32];

	snprintf(text, sizeof(text), "i%llde", value);
	write_bytes(writer, text, (int)strlen(text));
}

void bencode_write_bytes(struct bencode_writer *writer, const void *bytes, int length)
{
	char prefix[16];

	snprintf(prefix, sizeof(prefix), "%d:", length);
	write_bytes(writer, prefix, (int)strlen(prefix));
	write_bytes(writer, bytes, length);
}

void bencode_write_text(struct bencode_writer *writer, const char *text)
{
	bencode_write_bytes(writer, text, (int)strlen(text));
}

void bencode_write_raw(struct bencode_writer *writer, const void *bytes, int length)
{
	write_bytes(writer, bytes, length);
}

void bencode_write_list_start(struct bencode_writer *writer)
{
	write_bytes(writer, "l", 1);
}

void bencode_write_dictionary_start(struct bencode_writer *writer)
{
	write_bytes(writer, "d", 1);
}

void bencode_write_end(struct bencode_writer *writer)
{
	write_bytes(writer, "e", 1);
}
