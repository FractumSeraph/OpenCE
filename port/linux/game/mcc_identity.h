/* MCC session identity is independent of a client's installation folder. */
#ifndef MCC_IDENTITY_H
#define MCC_IDENTITY_H
#ifndef HALO_MCC
#include "halo_port_features.h" /* HALO_MCC */
#endif
struct network_game;
struct mcc_runtime;
struct mcc_cache_source;
int mcc_identity_prepare_host(char const *name);
/* Changes when preparation selects different content, metadata or advisory origin;
 * zero when invalid. Sharing cancels offers prepared under an older revision. */
unsigned long mcc_identity_host_revision(void);
/* port: whether the host's settings for the map may go out (an MCC map's
once its identity is prepared, #1129), and the identity let go with its
server */
int mcc_identity_host_ready(char const *map);
void mcc_identity_host_clear(void);
/* port: (network_server_message_handler.c) the host's MCC bytes of a 43-layout
settings record, over a fragment of it */
void mcc_identity_host_overlay(char const *map, long record_size, long name_offset, long offset, long length,
    unsigned char *data);
void const *mcc_identity_host_settings(void const *game, long bytes, struct network_game *copy);
/* port: (network_client_message_handler.c) a settings record received whole */
void mcc_identity_wire_record(unsigned char const *record, long size);
int mcc_identity_accept(struct network_game const *game);
/* Acceptance may be provisional: these MCC-owned guards hold player addition,
 * precaching and game start until bounded frame updates finish admission. */
void mcc_identity_update(void);
int mcc_identity_pending(char const *name);
/* Requires the stage's published validation proof for this exact installed
 * destination. Never rescans aliases, hashes synchronously or offers again. */
int mcc_identity_accept_installed(char const name[128], unsigned char const digest[32],
    unsigned char const metadata[32],char const *installed);
/* A handler may take over a missing-package join. On success it owns releasing
 * the admission lease once the host confirms its transfer hold, refuses the
 * offer, or the player opts out. Queued intent must not release the hold. */
void mcc_identity_set_missing(int (*handler)(char const name[128], unsigned char const digest[32],unsigned char const metadata[32]));
/* The content identity of the package this machine hosts, when level is that package. */
int mcc_identity_host_package(char const *level, unsigned char digest[32]);
void mcc_identity_disconnect(void);
char const *mcc_identity_local_name(char const *name);
int mcc_identity_check(char const *name);
int mcc_identity_loaded(char const *name, struct mcc_runtime *runtime);
int mcc_resources_fingerprint(struct mcc_runtime *runtime, unsigned char digest[32]);
int mcc_resources_fingerprint_probe(char const *name, struct mcc_cache_source const *source,
    unsigned char digest[32]);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_identity_accept(a0) ((void)sizeof(a0), (int)1)
#define mcc_identity_disconnect() ((void)0)
#define mcc_identity_prepare_host(a0) ((void)sizeof(a0), (int)0)
#define mcc_identity_host_ready(a0) ((void)sizeof(a0), (int)1)
#define mcc_identity_host_clear() ((void)0)
#define mcc_identity_host_overlay(a0, a1, a2, a3, a4, a5) \
	((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (void)sizeof(a5))
#define mcc_identity_wire_record(a0, a1) ((void)sizeof(a0), (void)sizeof(a1))
#endif

#endif
