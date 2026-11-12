/* join_request_version_sizes.c: appended to network_messages.c as the game
builds it (tools/test_join_request_version.py): the join request's size and
the build's network version, for join_request_version_test.c */

const int join_request_version_test_size = sizeof(message_client_join_game_request);
const int join_request_version_test_network_version = HALO_PORT_NETWORK_VERSION;
