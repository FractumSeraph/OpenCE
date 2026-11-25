/* The browser build's stand-ins for what ChupathingyCE's MCC lobby switch
 * (source/networking/network_server_manager.c, from its 38aae92ee) calls of
 * the native builds: there is no dedicated server in a browser
 * (server/src/dedicated.c). The invite prompt's (a browser host's switch of
 * its lobby to or from an MCC map goes ahead, as if the host had said yes)
 * is port/linux/game/invite_prompt.c's own build without the game browser,
 * since ChupathingyCE's 1b397cca8. */

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
