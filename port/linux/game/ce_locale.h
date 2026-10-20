#ifndef __CE_LOCALE_H
#define __CE_LOCALE_H

/* Port (this fork): the text of the Custom Edition the maps come from, out of
its locale resource map (loc.map), for the PC menus' text in its language
(menu_tags.c). OpenCE's custom_edition_cache.c had these; this fork's Halo
PC maps are ChupathingyCE's ce_*.c, and ce_resources.c has them. */

#include "cseries.h"

/* loc.map (maps_ce, or its older places) opened, FALSE if there is none or
it is not one, until ce_locale_close */
boolean custom_edition_locale_open(void);
void custom_edition_locale_close(void);

/* The strings of its unicode string list tag `name` (as
"ui\\shell\\main_menu\\..."): each string's UTF-16 characters, its
terminator included, lengths[index] of them from strings[index]. One block,
released with free(); NULL if it holds no such list, or one that does not
fit in its tag. */
struct custom_edition_string_list
{
	long count;
	unsigned short const **strings;
	long *lengths;
};
struct custom_edition_string_list *custom_edition_locale_strings(char const *name);

#endif
