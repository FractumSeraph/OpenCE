/* Delta Stats in the browser (ChupathingyCE's game list, halo.milenko.org;
src/delta/README.md): a joined game's report (game_engine.c, HALO_WEB)
handed to the page, which sends it (online_client.js, deltaGameReport) with
this browser's player key, if the game was joined through an invite and the
player shares their results; and then confirms the local players' lines. */

#include <emscripten/emscripten.h>

/* the report (a JSON object) and the local players' names (a JSON array of
strings), on the game's thread; the page copies both before this returns */
void web_delta_stats_report(const char *report, const char *names)
{
	MAIN_THREAD_EM_ASM({
		if (typeof window !== "undefined" && window.HaloOnline && window.HaloOnline.deltaGameReport)
			window.HaloOnline.deltaGameReport(UTF8ToString($0), UTF8ToString($1));
	}, report, names);
}
