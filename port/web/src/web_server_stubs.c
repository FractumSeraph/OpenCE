/* The browser build's stand-ins for what ChupathingyCE's MCC lobby switch
 * (source/networking/network_server_manager.c, from its 38aae92ee) calls of
 * the native builds: there is no dedicated server in a browser
 * (server/src/dedicated.c), and no invite prompt (port/linux/game/
 * invite_prompt.c, HALO_GAME_BROWSER's): a browser host's switch of its
 * lobby to or from an MCC map goes ahead as it chose, as if the host had
 * said yes. (unsigned char: the game's boolean, cseries.h) */

unsigned char dedicated_server_active(void)
{
	return 0;
}

long dedicated_playlist_count(void)
{
	return 0;
}

char const *dedicated_playlist_map(long entry)
{
	(void)entry;
	return "";
}

void invite_prompt_ask(char const *title, char const *question, char const *detail,
	void (*answer_function)(unsigned char yes))
{
	(void)title;
	(void)question;
	(void)detail;
	if (answer_function)
		answer_function(1);
}

void invite_prompt_ask_cancel(void (*answer_function)(unsigned char yes))
{
	(void)answer_function;
}
