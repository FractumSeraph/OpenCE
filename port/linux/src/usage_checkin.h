/*
USAGE_CHECKIN.H

(port, this fork) The anonymous usage check-in (usage_checkin.c): how many
installs of Fractum Halo are in use, by version and platform.
network.usage_stats = false sends nothing.
*/

#ifndef USAGE_CHECKIN_H
#define USAGE_CHECKIN_H

/* the game list's thread, each pass (browser.c): a check-in when one is due */
void usage_checkin_update(void);

#endif
