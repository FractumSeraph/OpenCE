/*
TORRENT_DHT.C

The BitTorrent client's DHT (BEP 5; torrent_internal.h), on the session's
UDP socket: a node of the mainline DHT, enough of one to find the peers of
a torrent and to announce this machine as one.

It keeps a flat table of the nodes heard from, closest to its own id
first, rather than Kademlia's buckets: a few hundred nodes are plenty for
lookups that start from the closest known and walk closer. A lookup asks
the closest nodes not yet asked (a few at a time) for the peers of a
torrent, takes the closer nodes they name, and ends when none closer
answer; then it announces to the closest that gave it a token. Queries from
other nodes are answered (ping, find_node, get_peers, announce_peer), so
this node holds up its end.
*/

#include "torrent_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID_SIZE 20
#define TABLE_SIZE 256
#define LOOKUP_NODES 32
#define LOOKUPS (TORRENT_MAXIMUM_TORRENTS + 1)
#define TRANSACTIONS 128
#define ALPHA 4
#define CLOSEST_RETURNED 8
#define ANNOUNCE_TO 8
#define QUERY_TIMEOUT (5 * TORRENT_SECOND)
#define LOOKUP_TIMEOUT (90 * TORRENT_SECOND)
#define LOOKUP_INTERVAL (15 * TORRENT_MINUTE)
#define LOOKUP_INTERVAL_FEW_PEERS (3 * TORRENT_MINUTE)
#define FEW_PEERS 5
#define SELF_LOOKUP_INTERVAL (10 * TORRENT_MINUTE)
#define BOOTSTRAP_RETRY (TORRENT_MINUTE)
#define NODE_STALE (15 * TORRENT_MINUTE)
#define NODE_FAILURES 3
#define TOKEN_LIFETIME (5 * TORRENT_MINUTE)
#define TOKEN_SIZE 4
#define STORE_HASHES 32
#define STORE_PEERS 8
#define STORE_LIFETIME (30 * TORRENT_MINUTE)
#define MESSAGE_SIZE 1500

enum query_kind
{
	_query_ping,
	_query_find_node,
	_query_get_peers,
	_query_announce_peer,
};

struct node
{
	int used;
	unsigned char id[ID_SIZE];
	unsigned long address;
	unsigned short port;
	unsigned long last_seen;
	int failures;
};

struct lookup_node
{
	unsigned char id[ID_SIZE];
	unsigned long address;
	unsigned short port;
	int queried;
	int responded;
	unsigned char token[32];
	int token_size;
};

struct lookup
{
	int used;
	unsigned char target[ID_SIZE];
	/* the torrent's index, or -1 for the lookup of this node's own id */
	int torrent;
	struct lookup_node nodes[LOOKUP_NODES];
	int count;
	int outstanding;
	unsigned long started;
	int values;
};

struct transaction
{
	int used;
	unsigned char id[2];
	enum query_kind kind;
	int lookup;
	int node;
	unsigned long address;
	unsigned short port;
	unsigned long time;
};

struct stored_peer
{
	unsigned long address;
	unsigned short port;
	unsigned long time;
};

struct stored_hash
{
	int used;
	unsigned char info_hash[ID_SIZE];
	struct stored_peer peers[STORE_PEERS];
};

static struct
{
	int started;
	unsigned char id[ID_SIZE];
	struct node table[TABLE_SIZE];
	struct lookup lookups[LOOKUPS];
	struct transaction transactions[TRANSACTIONS];
	unsigned short next_transaction;
	unsigned long bootstrap_time;
	struct
	{
		const char *host;
		unsigned short port;
		unsigned long address;
	} bootstrap[4];
	unsigned long self_lookup_time;
	unsigned char secrets[2][16];
	unsigned long secret_time;
	struct stored_hash store[STORE_HASHES];
} dht;

/* ---------- ids */

static int closer(const unsigned char *a, const unsigned char *b, const unsigned char *target)
{
	int index;

	for (index = 0; index < ID_SIZE; index++)
	{
		unsigned char da = (unsigned char)(a[index] ^ target[index]);
		unsigned char db = (unsigned char)(b[index] ^ target[index]);

		if (da != db)
			return da < db;
	}
	return 0;
}

/* ---------- the table */

static struct node *table_find(unsigned long address, unsigned short port)
{
	int index;

	for (index = 0; index < TABLE_SIZE; index++)
	{
		if (dht.table[index].used && dht.table[index].address == address && dht.table[index].port == port)
			return &dht.table[index];
	}
	return NULL;
}

static void table_add(const unsigned char *id, unsigned long address, unsigned short port)
{
	struct node *node = table_find(address, port);
	int index;
	int victim = -1;

	if (!address || !port || !memcmp(id, dht.id, ID_SIZE))
		return;
	if (node)
	{
		memcpy(node->id, id, ID_SIZE);
		node->last_seen = torrent_now();
		node->failures = 0;
		return;
	}
	for (index = 0; index < TABLE_SIZE; index++)
	{
		node = &dht.table[index];
		if (!node->used)
		{
			victim = index;
			break;
		}
		if (node->failures >= NODE_FAILURES || torrent_elapsed(node->last_seen, NODE_STALE))
		{
			victim = index;
			break;
		}
		/* (a full table of good nodes keeps the closest to our id) */
		if (victim < 0 || closer(dht.table[victim].id, node->id, dht.id))
			victim = index;
	}
	if (victim < 0 || (dht.table[victim].used && dht.table[victim].failures < NODE_FAILURES &&
		!torrent_elapsed(dht.table[victim].last_seen, NODE_STALE) && !closer(id, dht.table[victim].id, dht.id)))
	{
		return;
	}
	node = &dht.table[victim];
	memset(node, 0, sizeof(*node));
	node->used = 1;
	memcpy(node->id, id, ID_SIZE);
	node->address = address;
	node->port = port;
	node->last_seen = torrent_now();
}

/* the count closest good nodes to target, into nodes; returns how many */
static int table_closest(const unsigned char *target, struct node **nodes, int count)
{
	int index;
	int found = 0;

	for (index = 0; index < TABLE_SIZE; index++)
	{
		struct node *node = &dht.table[index];
		int position;

		if (!node->used || node->failures >= NODE_FAILURES)
			continue;
		for (position = found; position > 0 && closer(node->id, nodes[position - 1]->id, target); position--)
		{
			if (position < count)
				nodes[position] = nodes[position - 1];
		}
		if (position < count)
		{
			nodes[position] = node;
			if (found < count)
				found++;
		}
	}
	return found;
}

int torrent_dht_node_count(void)
{
	int index;
	int count = 0;

	for (index = 0; index < TABLE_SIZE; index++)
		count += dht.table[index].used && dht.table[index].failures < NODE_FAILURES;
	return count;
}

/* ---------- tokens and the store */

static void secrets_tick(void)
{
	if (!dht.secret_time || torrent_elapsed(dht.secret_time, TOKEN_LIFETIME))
	{
		memcpy(dht.secrets[1], dht.secrets[0], sizeof(dht.secrets[0]));
		posix_random_bytes(dht.secrets[0], sizeof(dht.secrets[0]));
		dht.secret_time = torrent_now();
	}
}

static void token_make(const unsigned char *secret, unsigned long address, unsigned char *token)
{
	unsigned char bytes[16 + 4];
	unsigned char digest[SHA1_DIGEST_SIZE];

	memcpy(bytes, secret, 16);
	memcpy(bytes + 16, &address, 4);
	sha1_bytes(bytes, sizeof(bytes), digest);
	memcpy(token, digest, TOKEN_SIZE);
}

static int token_check(unsigned long address, const unsigned char *token, int size)
{
	unsigned char expected[TOKEN_SIZE];
	int which;

	if (size != TOKEN_SIZE)
		return 0;
	for (which = 0; which < 2; which++)
	{
		token_make(dht.secrets[which], address, expected);
		if (!memcmp(expected, token, TOKEN_SIZE))
			return 1;
	}
	return 0;
}

static void store_peer(const unsigned char *info_hash, unsigned long address, unsigned short port)
{
	struct stored_hash *hash = NULL;
	int index;
	int oldest = 0;

	for (index = 0; index < STORE_HASHES; index++)
	{
		if (dht.store[index].used && !memcmp(dht.store[index].info_hash, info_hash, ID_SIZE))
		{
			hash = &dht.store[index];
			break;
		}
		if (!dht.store[index].used)
			oldest = index;
	}
	if (!hash)
	{
		hash = &dht.store[oldest];
		memset(hash, 0, sizeof(*hash));
		hash->used = 1;
		memcpy(hash->info_hash, info_hash, ID_SIZE);
	}
	for (index = 0; index < STORE_PEERS; index++)
	{
		if (hash->peers[index].address == address && hash->peers[index].port == port)
			break;
		if (!hash->peers[index].address || hash->peers[index].time < hash->peers[oldest].time)
			oldest = index;
	}
	if (index == STORE_PEERS)
		index = oldest;
	hash->peers[index].address = address;
	hash->peers[index].port = port;
	hash->peers[index].time = torrent_now();
}

static struct stored_hash *store_find(const unsigned char *info_hash)
{
	int index;

	for (index = 0; index < STORE_HASHES; index++)
	{
		if (dht.store[index].used && !memcmp(dht.store[index].info_hash, info_hash, ID_SIZE))
			return &dht.store[index];
	}
	return NULL;
}

/* ---------- sending */

static void message_send(unsigned long address, unsigned short port, const unsigned char *bytes, int size)
{
	struct sockaddr_in to;

	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = port;
	to.sin_addr.s_addr = address;
	posix_socket_sendto(torrent_session.udp_socket, bytes, size, 0, &to, sizeof(to));
}

static struct transaction *transaction_new(enum query_kind kind, int lookup, int node, unsigned long address,
	unsigned short port)
{
	int index;
	struct transaction *transaction = NULL;

	for (index = 0; index < TRANSACTIONS; index++)
	{
		if (!dht.transactions[index].used)
		{
			transaction = &dht.transactions[index];
			break;
		}
	}
	if (!transaction)
		return NULL;
	memset(transaction, 0, sizeof(*transaction));
	transaction->used = 1;
	dht.next_transaction++;
	transaction->id[0] = (unsigned char)(dht.next_transaction >> 8);
	transaction->id[1] = (unsigned char)dht.next_transaction;
	transaction->kind = kind;
	transaction->lookup = lookup;
	transaction->node = node;
	transaction->address = address;
	transaction->port = port;
	transaction->time = torrent_now();
	return transaction;
}

/* a query: ping; find_node of target; get_peers of info_hash; announce_peer
of info_hash with token */
static int query_send(enum query_kind kind, unsigned long address, unsigned short port, const unsigned char *target,
	const unsigned char *token, int token_size, int lookup, int node)
{
	unsigned char buffer[MESSAGE_SIZE];
	struct bencode_writer writer;
	struct transaction *transaction = transaction_new(kind, lookup, node, address, port);
	static const char *const names[] = { "ping", "find_node", "get_peers", "announce_peer" };

	if (!transaction)
		return 0;
	bencode_writer_start(&writer, buffer, sizeof(buffer));
	bencode_write_dictionary_start(&writer);
	bencode_write_text(&writer, "a");
	bencode_write_dictionary_start(&writer);
	bencode_write_text(&writer, "id");
	bencode_write_bytes(&writer, dht.id, ID_SIZE);
	switch (kind)
	{
	case _query_ping:
		break;
	case _query_find_node:
		bencode_write_text(&writer, "target");
		bencode_write_bytes(&writer, target, ID_SIZE);
		break;
	case _query_get_peers:
		bencode_write_text(&writer, "info_hash");
		bencode_write_bytes(&writer, target, ID_SIZE);
		break;
	case _query_announce_peer:
		bencode_write_text(&writer, "implied_port");
		bencode_write_integer(&writer, 1);
		bencode_write_text(&writer, "info_hash");
		bencode_write_bytes(&writer, target, ID_SIZE);
		bencode_write_text(&writer, "port");
		bencode_write_integer(&writer, torrent_session.port);
		bencode_write_text(&writer, "token");
		bencode_write_bytes(&writer, token, token_size);
		break;
	}
	bencode_write_end(&writer);
	bencode_write_text(&writer, "q");
	bencode_write_text(&writer, names[kind]);
	bencode_write_text(&writer, "t");
	bencode_write_bytes(&writer, transaction->id, 2);
	bencode_write_text(&writer, "y");
	bencode_write_text(&writer, "q");
	bencode_write_end(&writer);
	if (writer.overflow)
	{
		transaction->used = 0;
		return 0;
	}
	message_send(address, port, buffer, writer.size);
	return 1;
}

/* an answer to a query: our id, and the nodes closest to target, the
token for the asker, and the peers of info_hash as asked */
static void response_send(unsigned long address, unsigned short port, const unsigned char *transaction_id,
	int transaction_id_size, const unsigned char *target, int with_token, const struct stored_hash *peers)
{
	unsigned char buffer[MESSAGE_SIZE];
	struct bencode_writer writer;

	bencode_writer_start(&writer, buffer, sizeof(buffer));
	bencode_write_dictionary_start(&writer);
	bencode_write_text(&writer, "r");
	bencode_write_dictionary_start(&writer);
	bencode_write_text(&writer, "id");
	bencode_write_bytes(&writer, dht.id, ID_SIZE);
	if (target)
	{
		struct node *closest[CLOSEST_RETURNED];
		int count = table_closest(target, closest, CLOSEST_RETURNED);
		unsigned char compact[CLOSEST_RETURNED * 26];
		int index;

		for (index = 0; index < count; index++)
		{
			memcpy(compact + index * 26, closest[index]->id, ID_SIZE);
			memcpy(compact + index * 26 + 20, &closest[index]->address, 4);
			memcpy(compact + index * 26 + 24, &closest[index]->port, 2);
		}
		bencode_write_text(&writer, "nodes");
		bencode_write_bytes(&writer, compact, count * 26);
	}
	if (with_token)
	{
		unsigned char token[TOKEN_SIZE];

		token_make(dht.secrets[0], address, token);
		bencode_write_text(&writer, "token");
		bencode_write_bytes(&writer, token, TOKEN_SIZE);
	}
	if (peers)
	{
		int index;

		bencode_write_text(&writer, "values");
		bencode_write_list_start(&writer);
		for (index = 0; index < STORE_PEERS; index++)
		{
			unsigned char compact[6];

			if (!peers->peers[index].address || torrent_elapsed(peers->peers[index].time, STORE_LIFETIME))
				continue;
			memcpy(compact, &peers->peers[index].address, 4);
			memcpy(compact + 4, &peers->peers[index].port, 2);
			bencode_write_bytes(&writer, compact, 6);
		}
		bencode_write_end(&writer);
	}
	bencode_write_end(&writer);
	bencode_write_text(&writer, "t");
	bencode_write_bytes(&writer, transaction_id, transaction_id_size);
	bencode_write_text(&writer, "y");
	bencode_write_text(&writer, "r");
	bencode_write_end(&writer);
	if (!writer.overflow)
		message_send(address, port, buffer, writer.size);
}

/* ---------- lookups */

static struct lookup *lookup_of_torrent(int torrent)
{
	int index;

	for (index = 0; index < LOOKUPS; index++)
	{
		if (dht.lookups[index].used && dht.lookups[index].torrent == torrent)
			return &dht.lookups[index];
	}
	return NULL;
}

/* a node for the lookup, if closer than its farthest or it has room */
static void lookup_add_node(struct lookup *lookup, const unsigned char *id, unsigned long address,
	unsigned short port)
{
	int index;
	int position;

	if (!address || !port)
		return;
	for (index = 0; index < lookup->count; index++)
	{
		if (lookup->nodes[index].address == address && lookup->nodes[index].port == port)
			return;
	}
	for (position = lookup->count; position > 0 && closer(id, lookup->nodes[position - 1].id, lookup->target);
		position--)
	{
	}
	if (position >= LOOKUP_NODES)
		return;
	if (lookup->count == LOOKUP_NODES)
	{
		/* (the farthest gives way, unless it is being asked) */
		if (lookup->nodes[LOOKUP_NODES - 1].queried && !lookup->nodes[LOOKUP_NODES - 1].responded)
			return;
		lookup->count--;
	}
	memmove(&lookup->nodes[position + 1], &lookup->nodes[position],
		(size_t)(lookup->count - position) * sizeof(lookup->nodes[0]));
	memset(&lookup->nodes[position], 0, sizeof(lookup->nodes[0]));
	memcpy(lookup->nodes[position].id, id, ID_SIZE);
	lookup->nodes[position].address = address;
	lookup->nodes[position].port = port;
	lookup->count++;
}

static void lookup_start(const unsigned char *target, int torrent)
{
	struct lookup *lookup = NULL;
	struct node *closest[LOOKUP_NODES];
	int count;
	int index;

	for (index = 0; index < LOOKUPS; index++)
	{
		if (!dht.lookups[index].used)
		{
			lookup = &dht.lookups[index];
			break;
		}
	}
	if (!lookup)
		return;
	memset(lookup, 0, sizeof(*lookup));
	lookup->used = 1;
	memcpy(lookup->target, target, ID_SIZE);
	lookup->torrent = torrent;
	lookup->started = torrent_now();
	count = table_closest(target, closest, LOOKUP_NODES);
	for (index = 0; index < count; index++)
		lookup_add_node(lookup, closest[index]->id, closest[index]->address, closest[index]->port);
	/* (none known: the bootstrap nodes, whose ids are not known) */
	if (!count)
	{
		for (index = 0; index < 4; index++)
		{
			unsigned char id[ID_SIZE];

			memset(id, 0xFF, sizeof(id));
			if (dht.bootstrap[index].address)
			{
				lookup_add_node(lookup, id, dht.bootstrap[index].address,
					torrent_network_short(dht.bootstrap[index].port));
			}
		}
	}
}

static void lookup_finish(struct lookup *lookup)
{
	int index = (int)(lookup - dht.lookups);
	int transaction;

	if (lookup->torrent >= 0)
	{
		struct torrent *torrent = &torrent_session.torrents[lookup->torrent];
		int announced = 0;
		int node;

		for (node = 0; node < lookup->count && announced < ANNOUNCE_TO; node++)
		{
			if (lookup->nodes[node].responded && lookup->nodes[node].token_size)
			{
				query_send(_query_announce_peer, lookup->nodes[node].address, lookup->nodes[node].port,
					lookup->target, lookup->nodes[node].token, lookup->nodes[node].token_size, -1, -1);
				announced++;
			}
		}
		if (torrent->used)
		{
			/* (one that had no node to ask, before the bootstrap nodes were
			resolved: again soon) */
			torrent->dht_next_lookup = torrent_now() + (!lookup->count ? 5 * TORRENT_SECOND :
				lookup->values >= FEW_PEERS ? LOOKUP_INTERVAL : LOOKUP_INTERVAL_FEW_PEERS);
			torrent->dht_last_announce = torrent_now();
			torrent_log("%s: the DHT lookup found %d peers (announced to %d nodes; %d nodes known)", torrent->name,
				lookup->values, announced, torrent_dht_node_count());
		}
	}
	else
	{
		dht.self_lookup_time = torrent_now();
		torrent_log("the DHT knows %d nodes", torrent_dht_node_count());
	}
	for (transaction = 0; transaction < TRANSACTIONS; transaction++)
	{
		if (dht.transactions[transaction].used && dht.transactions[transaction].lookup == index)
			dht.transactions[transaction].used = 0;
	}
	lookup->used = 0;
}

static void lookup_tick(struct lookup *lookup)
{
	int index = (int)(lookup - dht.lookups);
	int node;
	int unqueried = 0;

	while (lookup->outstanding < ALPHA)
	{
		int next = -1;

		for (node = 0; node < lookup->count; node++)
		{
			if (!lookup->nodes[node].queried)
			{
				next = node;
				break;
			}
		}
		if (next < 0)
			break;
		lookup->nodes[next].queried = 1;
		if (query_send(lookup->torrent >= 0 ? _query_get_peers : _query_find_node, lookup->nodes[next].address,
			lookup->nodes[next].port, lookup->target, NULL, 0, index, next))
		{
			lookup->outstanding++;
		}
	}
	for (node = 0; node < lookup->count; node++)
		unqueried += !lookup->nodes[node].queried;
	if ((!unqueried && !lookup->outstanding) || torrent_elapsed(lookup->started, LOOKUP_TIMEOUT))
		lookup_finish(lookup);
}

void torrent_dht_lookup(struct torrent *torrent)
{
	int index = (int)(torrent - torrent_session.torrents);

	torrent->dht_next_lookup = torrent_now() + LOOKUP_INTERVAL_FEW_PEERS;
	if (!dht.started || lookup_of_torrent(index))
		return;
	/* (nothing to ask yet: once the bootstrap nodes are resolved) */
	if (!torrent_dht_node_count() && !dht.bootstrap[0].address && !dht.bootstrap[1].address &&
		!dht.bootstrap[2].address && !dht.bootstrap[3].address)
	{
		torrent->dht_next_lookup = torrent_now() + 5 * TORRENT_SECOND;
		return;
	}
	lookup_start(torrent->info_hash, index);
}

/* ---------- receiving */

static void nodes_received(struct lookup *lookup, const unsigned char *compact, int size)
{
	int index;

	for (index = 0; index + 26 <= size; index += 26)
	{
		unsigned long address;
		unsigned short port;

		memcpy(&address, compact + index + 20, 4);
		memcpy(&port, compact + index + 24, 2);
		if (lookup)
			lookup_add_node(lookup, compact + index, address, port);
	}
}

static void response_received(const struct bencode_document *document, const unsigned char *transaction_id,
	unsigned long address, unsigned short port)
{
	int index;
	struct transaction *transaction = NULL;
	int r = bencode_find(document, 0, "r");
	const unsigned char *id;
	const unsigned char *nodes;
	int nodes_size;
	struct lookup *lookup = NULL;

	for (index = 0; index < TRANSACTIONS; index++)
	{
		if (dht.transactions[index].used && !memcmp(dht.transactions[index].id, transaction_id, 2) &&
			dht.transactions[index].address == address)
		{
			transaction = &dht.transactions[index];
			break;
		}
	}
	if (!transaction)
		return;
	transaction->used = 0;
	id = bencode_string_of(document, bencode_find(document, r, "id"), ID_SIZE);
	if (!id)
		return;
	table_add(id, address, port);
	if (transaction->lookup >= 0 && dht.lookups[transaction->lookup].used)
	{
		lookup = &dht.lookups[transaction->lookup];
		lookup->outstanding--;
		if (transaction->node >= 0 && transaction->node < lookup->count &&
			lookup->nodes[transaction->node].address == address)
		{
			struct lookup_node *node = &lookup->nodes[transaction->node];
			const unsigned char *token;
			int token_size;

			node->responded = 1;
			memcpy(node->id, id, ID_SIZE);
			if (bencode_string(document, bencode_find(document, r, "token"), &token, &token_size) &&
				token_size > 0 && token_size <= (int)sizeof(node->token))
			{
				memcpy(node->token, token, (size_t)token_size);
				node->token_size = token_size;
			}
		}
	}
	if (bencode_string(document, bencode_find(document, r, "nodes"), &nodes, &nodes_size))
		nodes_received(lookup, nodes, nodes_size);
	if (lookup && lookup->torrent >= 0)
	{
		int values = bencode_find_typed(document, r, "values", _bencode_list);
		struct torrent *torrent = &torrent_session.torrents[lookup->torrent];

		if (values >= 0 && torrent->used)
		{
			int child;

			for (child = document->nodes[values].child; child >= 0; child = document->nodes[child].next)
			{
				const unsigned char *compact = bencode_string_of(document, child, 6);

				if (compact)
				{
					unsigned long peer_address;
					unsigned short peer_port;

					memcpy(&peer_address, compact, 4);
					memcpy(&peer_port, compact + 4, 2);
					lookup->values += torrent_candidate_add(torrent, peer_address, peer_port, _torrent_source_dht);
				}
			}
		}
	}
}

static void query_received(const struct bencode_document *document, const unsigned char *transaction_id,
	int transaction_id_size, unsigned long address, unsigned short port)
{
	char name[16];
	int a = bencode_find(document, 0, "a");
	const unsigned char *id = bencode_string_of(document, bencode_find(document, a, "id"), ID_SIZE);
	const unsigned char *target;

	if (!id)
		return;
	table_add(id, address, port);
	bencode_text(document, bencode_find(document, 0, "q"), name, sizeof(name));
	if (!strcmp(name, "ping"))
	{
		response_send(address, port, transaction_id, transaction_id_size, NULL, 0, NULL);
	}
	else if (!strcmp(name, "find_node"))
	{
		target = bencode_string_of(document, bencode_find(document, a, "target"), ID_SIZE);
		if (target)
			response_send(address, port, transaction_id, transaction_id_size, target, 0, NULL);
	}
	else if (!strcmp(name, "get_peers"))
	{
		target = bencode_string_of(document, bencode_find(document, a, "info_hash"), ID_SIZE);
		if (target)
			response_send(address, port, transaction_id, transaction_id_size, target, 1, store_find(target));
	}
	else if (!strcmp(name, "announce_peer"))
	{
		const unsigned char *token;
		int token_size;
		long long announced_port = bencode_integer(document, bencode_find(document, a, "port"), 0);

		target = bencode_string_of(document, bencode_find(document, a, "info_hash"), ID_SIZE);
		if (bencode_integer(document, bencode_find(document, a, "implied_port"), 0))
			announced_port = torrent_network_short(port);
		if (target && bencode_string(document, bencode_find(document, a, "token"), &token, &token_size) &&
			token_check(address, token, token_size) && announced_port > 0 && announced_port < 65536)
		{
			store_peer(target, address, torrent_network_short((unsigned short)announced_port));
			response_send(address, port, transaction_id, transaction_id_size, NULL, 0, NULL);
		}
	}
}

void torrent_dht_received(const unsigned char *data, int size, unsigned long address, unsigned short port)
{
	struct bencode_document *document;
	char y[4];
	const unsigned char *transaction_id;
	int transaction_id_size;

	if (!dht.started)
		return;
	document = malloc(sizeof(*document));
	if (!document)
		return;
	if (bencode_parse(document, data, size) > 0 && document->nodes[0].type == _bencode_dictionary &&
		bencode_string(document, bencode_find(document, 0, "t"), &transaction_id, &transaction_id_size) &&
		transaction_id_size > 0 && transaction_id_size <= 16)
	{
		bencode_text(document, bencode_find(document, 0, "y"), y, sizeof(y));
		if (!strcmp(y, "r") && transaction_id_size == 2)
			response_received(document, transaction_id, address, port);
		else if (!strcmp(y, "q"))
			query_received(document, transaction_id, transaction_id_size, address, port);
		else if (!strcmp(y, "e") && transaction_id_size == 2)
		{
			/* (an error: the node answered, with nothing) */
			int index;

			for (index = 0; index < TRANSACTIONS; index++)
			{
				struct transaction *transaction = &dht.transactions[index];

				if (transaction->used && !memcmp(transaction->id, transaction_id, 2) &&
					transaction->address == address)
				{
					transaction->used = 0;
					if (transaction->lookup >= 0 && dht.lookups[transaction->lookup].used)
						dht.lookups[transaction->lookup].outstanding--;
				}
			}
		}
	}
	free(document);
}

/* ---------- each tick */

static void transactions_tick(void)
{
	int index;

	for (index = 0; index < TRANSACTIONS; index++)
	{
		struct transaction *transaction = &dht.transactions[index];
		struct node *node;

		if (!transaction->used || !torrent_elapsed(transaction->time, QUERY_TIMEOUT))
			continue;
		transaction->used = 0;
		node = table_find(transaction->address, transaction->port);
		if (node)
			node->failures++;
		if (transaction->lookup >= 0 && dht.lookups[transaction->lookup].used)
			dht.lookups[transaction->lookup].outstanding--;
	}
}

static void bootstrap_tick(void)
{
	int index;
	int resolved = 0;

	if (torrent_dht_node_count() >= 8)
		return;
	if (dht.bootstrap_time && !torrent_elapsed(dht.bootstrap_time, BOOTSTRAP_RETRY))
		return;
	dht.bootstrap_time = torrent_now();
	for (index = 0; index < 4; index++)
	{
		if (!dht.bootstrap[index].address)
			dht.bootstrap[index].address = posix_resolve_ipv4(dht.bootstrap[index].host);
		resolved += dht.bootstrap[index].address != 0;
	}
	if (!resolved)
	{
		torrent_log("no DHT bootstrap node could be resolved (no internet?)");
		return;
	}
	/* (the table filled by looking our own id up) */
	if (!lookup_of_torrent(-1))
		lookup_start(dht.id, -1);
}

void torrent_dht_tick(void)
{
	int index;

	if (!dht.started)
		return;
	secrets_tick();
	transactions_tick();
	bootstrap_tick();
	if (torrent_dht_node_count() >= 8 && torrent_elapsed(dht.self_lookup_time, SELF_LOOKUP_INTERVAL) &&
		!lookup_of_torrent(-1))
	{
		lookup_start(dht.id, -1);
	}
	for (index = 0; index < LOOKUPS; index++)
	{
		if (dht.lookups[index].used)
			lookup_tick(&dht.lookups[index]);
	}
}

void torrent_dht_start(void)
{
	memset(&dht, 0, sizeof(dht));
	posix_random_bytes(dht.id, sizeof(dht.id));
	dht.bootstrap[0].host = "router.bittorrent.com";
	dht.bootstrap[0].port = 6881;
	dht.bootstrap[1].host = "dht.transmissionbt.com";
	dht.bootstrap[1].port = 6881;
	dht.bootstrap[2].host = "router.utorrent.com";
	dht.bootstrap[2].port = 6881;
	dht.bootstrap[3].host = "dht.libtorrent.org";
	dht.bootstrap[3].port = 25401;
	dht.started = 1;
}

void torrent_dht_stop(void)
{
	memset(&dht, 0, sizeof(dht));
}
