/*
TORRENT_BENCODE.H

Bencoding (BEP 3), as the BitTorrent client's messages are written: the
trackers' answers, the DHT's messages, the extension handshake and the
metadata exchange (torrent.c, torrent_dht.c, torrent_tracker.c).

A parsed document is a tree of nodes over the bytes it was parsed from,
which must stay as they were while the nodes are read (strings point into
them). A writer appends to a buffer, and says if it overflowed.
*/

#ifndef __HALO_LINUX_TORRENT_BENCODE_H
#define __HALO_LINUX_TORRENT_BENCODE_H

enum bencode_type
{
	_bencode_integer,
	_bencode_string,
	_bencode_list,
	_bencode_dictionary,
};

struct bencode_node
{
	unsigned char type;
	/* the next node of the list or dictionary this one is in (-1: none);
	a dictionary's children alternate key, value */
	int next;
	/* a list's or dictionary's first child (-1: none), and how many */
	int child;
	int count;
	long long integer;
	/* a string's bytes, in the parsed data */
	const unsigned char *bytes;
	int length;
};

/* (the DHT's answers hold up to a few hundred values: enough) */
#define BENCODE_MAXIMUM_NODES 1024

struct bencode_document
{
	struct bencode_node nodes[BENCODE_MAXIMUM_NODES];
	int count;
};

/* parses one value at the start of data: the number of bytes it took (its
root is node 0), or -1 if it is not bencoded, is nested too deep or has too
many nodes. Bytes after the value are left (a metadata message's payload) */
int bencode_parse(struct bencode_document *document, const void *data, int size);

/* a dictionary's value for key (-1: none, or node is not a dictionary) */
int bencode_find(const struct bencode_document *document, int node, const char *key);
/* a dictionary's value for key that has the type, or -1 */
int bencode_find_typed(const struct bencode_document *document, int node, const char *key, enum bencode_type type);
/* the integer of node, or fallback if it is not one */
long long bencode_integer(const struct bencode_document *document, int node, long long fallback);
/* node's string: its bytes and length; 0 if it is not a string */
int bencode_string(const struct bencode_document *document, int node, const unsigned char **bytes, int *length);
/* node's string, if it has exactly length bytes */
const unsigned char *bencode_string_of(const struct bencode_document *document, int node, int length);
/* node's string as text (NUL terminated) into text, cut to fit; "" if it is
not a string */
void bencode_text(const struct bencode_document *document, int node, char *text, int size);

/* ---------- writing */

struct bencode_writer
{
	unsigned char *data;
	int size;
	int capacity;
	/* something did not fit: the data is not to be used */
	int overflow;
};

void bencode_writer_start(struct bencode_writer *writer, void *buffer, int capacity);
void bencode_write_integer(struct bencode_writer *writer, long long value);
void bencode_write_bytes(struct bencode_writer *writer, const void *bytes, int length);
void bencode_write_text(struct bencode_writer *writer, const char *text);
/* the raw bytes of a value already bencoded (an info dictionary) */
void bencode_write_raw(struct bencode_writer *writer, const void *bytes, int length);
void bencode_write_list_start(struct bencode_writer *writer);
void bencode_write_dictionary_start(struct bencode_writer *writer);
/* ends a list or a dictionary (its keys must have been written in sorted
order, as the specification asks) */
void bencode_write_end(struct bencode_writer *writer);

#endif
