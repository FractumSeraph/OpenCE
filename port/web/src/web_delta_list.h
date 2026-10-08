/* Delta List in the browser: ChupathingyCE's game list (halo.milenko.org's
/v1/games), for the in-game Server Browser (menu_functions.c, HALO_WEB).
See web_delta_list.c. Text crosses as UTF-16 (unsigned short): the game's
wide characters, whatever this unit's wchar_t is. */

#ifndef WEB_DELTA_LIST_H
#define WEB_DELTA_LIST_H

struct p2p_listing;

/* the list's games that games[0, count) has not (by their invite's token),
added after them, as far as maximum; returns the new count. Only games this
build can join: of its network versions, with a whole invite. */
int web_delta_list_add_games(struct p2p_listing *games, int count, int maximum);

/* what the list says of a game of the browser's (by its invite), into text
(size characters, its end included), no longer than characters: its host
and who is playing, "DEDICATED, LINUX, DELTA: Alice, Bob +3 more",
"OPENCE"; empty when the list does not have it. The score to win (0: not
known) */
void web_delta_list_detail_text(const char *invite, unsigned short *text, int size, int characters);
int web_delta_list_score_limit(const char *invite);
/* whether the list says a dedicated server hosts it */
int web_delta_list_dedicated(const char *invite);

#endif
