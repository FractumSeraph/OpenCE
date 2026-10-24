/*
MAP_SHARE_HASH.C

Prints the map sharing hash (port/linux/game/map_share_files.c) of each map
file named: BLAKE2b-256 of "OpenCE map 1", the file's size (4 bytes,
little-endian) and the BLAKE2b-256 of each of its 256 KB pieces, the last
shorter. It is what a host offers a joining player for its map, and what the
maps' torrent index's share:<hash> column holds (tools/map_torrents.py
share), the whitelist a client checks an offer against.

	cc -O2 -o map_share_hash tools/map_share_hash.c port/third_party/monocypher/monocypher.c
	map_share_hash <map files...>   (or file names one a line on stdin, with -)

Each line: the hash in hex, a tab, the size, a tab, the file's path. A file
that cannot be read prints "error" in place of the hash.
*/

#include "../port/third_party/monocypher/monocypher.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIECE_BYTES 0x40000
#define LABEL "OpenCE map 1"
#define LABEL_BYTES 12

static int hash_file(const char *path, unsigned char *buffer)
{
	FILE *stream = fopen(path, "rb");
	crypto_blake2b_ctx file;
	unsigned char piece_hash[32];
	unsigned char hash[32];
	unsigned char size_bytes[4];
	long long size;
	size_t got;
	int index;

	if (!stream || fseek(stream, 0, SEEK_END) != 0 || (size = ftell(stream)) < 0 || size > 0xFFFFFFFFLL ||
		fseek(stream, 0, SEEK_SET) != 0)
	{
		if (stream)
			fclose(stream);
		printf("error\t0\t%s\n", path);
		return 1;
	}
	crypto_blake2b_init(&file, 32);
	crypto_blake2b_update(&file, (const uint8_t *)LABEL, LABEL_BYTES);
	for (index = 0; index < 4; index++)
		size_bytes[index] = (unsigned char)((unsigned long)size >> (index * 8));
	crypto_blake2b_update(&file, size_bytes, 4);
	while ((got = fread(buffer, 1, PIECE_BYTES, stream)) > 0)
	{
		crypto_blake2b(piece_hash, 32, buffer, got);
		crypto_blake2b_update(&file, piece_hash, 32);
	}
	if (ferror(stream))
	{
		fclose(stream);
		printf("error\t0\t%s\n", path);
		return 1;
	}
	fclose(stream);
	crypto_blake2b_final(&file, hash);
	for (index = 0; index < 32; index++)
		printf("%02x", hash[index]);
	printf("\t%lld\t%s\n", size, path);
	return 0;
}

int main(int argc, char **argv)
{
	unsigned char *buffer = malloc(PIECE_BYTES);
	char line[4096];
	int failed = 0;
	int which;

	if (!buffer)
		return 2;
	for (which = 1; which < argc; which++)
	{
		if (strcmp(argv[which], "-"))
		{
			failed |= hash_file(argv[which], buffer);
			continue;
		}
		while (fgets(line, sizeof(line), stdin))
		{
			size_t length = strlen(line);

			while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
				line[--length] = 0;
			if (length)
				failed |= hash_file(line, buffer);
			fflush(stdout);
		}
	}
	free(buffer);
	return failed;
}
