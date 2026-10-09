/* Delta's legacy table in the browser (ChupathingyCE's network family:
docs/delta.md, and port/linux/src/delta.c, which the browser build compiles
as the native builds do).

delta.c keeps the signed table in use: the built-in numbers, the cache in the
save root, and tables other machines offer. What it fetches over HTTPS in the
native builds (HALO_GAME_BROWSER), the browser cannot: the page
(online_client.js) fetches the document and its signature from this site's
server (server.mjs relays halo.milenko.org's /v1/delta/legacy, which has no
CORS) or GitHub, at start and every four hours, puts the signed table in this
file's buffer and hands it to web_delta_offer_table, which offers it to
delta.c as any other source (checked, and taken only if newer). */

#include "platform.h"
#include "delta.h"

#include <emscripten/emscripten.h>

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

/* 1 if the table was taken (newer, and it checks), 0 if not (not newer, or
it does not check: delta.c logs which) */
EMSCRIPTEN_KEEPALIVE int web_delta_offer_table(int size, int from_github)
{
	unsigned int before = delta_legacy_serial();

	if (size < 0 || size > (int)sizeof(delta_offered))
		return -1;
	if (delta_legacy_offer(delta_offered, size))
	{
		platform_log("Delta: legacy table %u from %s", delta_legacy_serial(), from_github ? "GitHub" : "Delta List");
		return 1;
	}
	return delta_legacy_serial() == before ? 0 : 1;
}

/* the serial in use, so the page asks for no table it has */
EMSCRIPTEN_KEEPALIVE unsigned int web_delta_table_serial(void)
{
	return delta_legacy_serial();
}
