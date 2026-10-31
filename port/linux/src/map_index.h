/*
MAP_INDEX.H

The approved map list (the maps' torrent index, map_torrents.c) as it is
published between releases (port, this fork): the index's exact bytes,
signed with the fork's release key (map_index_key.h) over MAP_INDEX_CONTEXT
and them, by .github/workflows/map-index.yml (tools/map_torrents.py sign) on
every change to port/assets/network/map_torrents.txt. map_torrents.c fetches
it (maps.index_url), caches it and takes it in place of the build's own when
its serial (the "# serial <n>" line, which map_torrents.py raises with each
change) is newer: an approved map needs a push, not a release.

Plain checks alone here, no files or network: tools/test_map_index.py builds
this by itself with a test key.
*/

#ifndef MAP_INDEX_H
#define MAP_INDEX_H

#include <stddef.h>

/* the index's first line (tools/map_torrents.py) */
#define MAP_INDEX_FORMAT "# halo map torrents v1"
/* what the signature covers before the index's bytes */
#define MAP_INDEX_CONTEXT "fractum-halo-map-index 1\n"
#define MAP_INDEX_SIGNATURE_SIZE 64
/* the most a fetched or cached index may be */
#define MAP_INDEX_MAXIMUM_SIZE (16UL * 1024 * 1024)

/* the signature's 128 hex digits, with white space around them, into
signature (MAP_INDEX_SIGNATURE_SIZE bytes): 1 if they are that */
int map_index_signature_parse(const char *text, size_t length, unsigned char *signature);

/* whether signature signs the document (size bytes: MAP_INDEX_CONTEXT then
these) by one of map_index_key.h's keys: 1 if so */
int map_index_signature_check(const unsigned char *signature, const char *document, size_t size);

/* whether the document is an index (MAP_INDEX_FORMAT its first line): 1 */
int map_index_format(const char *document, size_t size);

/* the index's serial: its "# serial <n>" line among the comment lines it
opens with; 0 when it has none (an index from before serials) */
unsigned long long map_index_serial(const char *document, size_t size);

/* a cached index, the signature's 128 hex digits, a line feed and then the
document: the signature into signature, and where the document starts in
*document_offset; 1 if it is one */
int map_index_split(const char *text, size_t size, unsigned char *signature, size_t *document_offset);

#endif
