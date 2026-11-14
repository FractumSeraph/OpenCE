#ifndef MCC_MAIN_H
#define MCC_MAIN_H
boolean mcc_main_load_failed(char const *name);
boolean mcc_main_network_load_failed(char const *name);
boolean mcc_main_map_completed(void);
/* Resolve a script's map_name within the current MCC package. */
boolean mcc_main_script_level_resolve(char const *current, char const *requested, char *next, size_t size);
/* requested: the mission the level's own scripts named with map_name
 * (main_script_map_name), which needs no campaign manifest; NULL for none. */
boolean mcc_main_campaign_won(char const *requested);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_main_campaign_won(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_main_script_level_resolve(a0, a1, a2, a3) \
	((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (boolean)0)
#define mcc_main_load_failed(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_main_network_load_failed(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_main_map_completed() ((boolean)0)
#endif

#endif
