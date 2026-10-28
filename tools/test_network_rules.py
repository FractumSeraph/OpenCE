"""Tests of network versions 30 to 38: the C tests of the rules every machine
shares (port/linux/tests/network_rules_test.c: the aim assist policy
message's and the input entry's bytes, the stamp window, the elevator's
correction and riders, the policy's reach), built with the build machine's C
compiler, and what is in the sources that the C tests cannot reach: the
pregame messages' table and numbers, the message 90 and 91 numbers, the
host's sends and the client's drops, the settings that carry the policy, and
(port, this fork) the game settings record of 35's layout this build keeps,
widened from an older host's and narrowed for an older client."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
MESSAGES_H = ROOT / "source/networking/network_messages.h"
MESSAGES_C = ROOT / "source/networking/network_messages.c"
SERVER_MANAGER_C = ROOT / "source/networking/network_server_manager.c"
DISTRIBUTED_H = ROOT / "port/linux/game/network_distributed.h"
DISTRIBUTED_C = ROOT / "port/linux/game/network_distributed.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_rules(tmp_path):
    """the rules' C tests, warnings as errors"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "network_rules_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/network_rules_test.c"), str(ROOT / "port/linux/game/network_rules.c"),
         "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "network_rules_test: ok" in result.stdout


def _field_sizes():
    """each game message's packet fields' bytes, by name (pad 0, bytes 1,
    shorts 2, longs 4), from network_messages.c's field lists"""
    sizes = {}
    source = MESSAGES_C.read_text(encoding="utf-8")
    for fields, name in re.findall(r"\t\{\n((?:\t\t.*\n)*?)\t\},\n\tNETWORK_GAME_MESSAGE_DEFINITION\((\w+),", source):
        sizes[name] = sum(int(count) * {"pad": 0, "bytes": 1, "shorts": 2, "longs": 4}.get(kind, 0)
                          for kind, count in re.findall(r"_data_packet_field_(\w+), (\d+)", fields))
        sizes[name] += sum(int(count) for kind, count in re.findall(r"_data_packet_field_(pad), (\d+)", fields))
    return sizes


def test_pregame_messages_numbers_and_payloads():
    """begin game 8, graceful exit 9, pregame keep-alive 10, postgame
    keep-alive 11; begin game a 16-bit value and 2 bytes of padding (4 bytes),
    graceful exit and the pregame keep-alive 32 bits (4), the postgame
    keep-alive 16 (2), each message with its own field list"""
    header = MESSAGES_H.read_text(encoding="utf-8")
    enum = header[header.index("enum network_game_message_type"):]
    names = re.findall(r"\t_message_(\w+)", enum[:enum.index("};")])
    assert names.index("server_begin_game") == 8
    assert names.index("server_graceful_game_exit_pregame") == 9
    assert names.index("server_pregame_keep_alive") == 10
    assert names.index("server_postgame_keep_alive") == 11
    sizes = _field_sizes()
    assert sizes["server_begin_game"] == 4
    assert sizes["server_graceful_game_exit_pregame"] == 4
    assert sizes["server_pregame_keep_alive"] == 4
    assert sizes["server_postgame_keep_alive"] == 2
    source = MESSAGES_C.read_text(encoding="utf-8")
    # (the structs the table sizes: 4, 4, 4 and 2 bytes)
    for name, size in (("server_begin_game", "0x04"), ("server_graceful_game_exit_pregame", "0x04"),
                       ("server_pregame_keep_alive", "0x04"), ("server_postgame_keep_alive", "0x02")):
        assert f"DEFINE_NETWORK_GAME_MESSAGE(message_{name}, {size});" in source, name


def test_keep_alive_is_sent_zero_and_taken_whatever_it_says():
    """the host's keep-alive carries 0 in all four bytes; the client reads the
    value and ignores it (an older sender's last two bytes were its stack)"""
    server = SERVER_MANAGER_C.read_text(encoding="utf-8")
    assert "struct message_server_pregame_keep_alive message_packet = { 0 };" in server
    client = (ROOT / "source/networking/network_client_message_handler.c").read_text(encoding="utf-8")
    handler = client[client.index("static boolean network_game_client_handle_message_server_pregame_keep_alive(\n"):]
    handler = handler[:handler.index("network_game_client_handle_message_server_postgame_keep_alive")]
    assert "keep_alive.unused" not in handler


def test_aim_assist_policy_is_message_90():
    header = DISTRIBUTED_H.read_text(encoding="utf-8")
    assert re.search(r"_distributed_message_aim_assist_policy = 90,", header)
    source = DISTRIBUTED_C.read_text(encoding="utf-8")
    # the host sends it to a machine that is ready and to all when it changes,
    # reliably; a client takes it from the host's stream only; a host takes none
    assert "distributed_tell_aim_assist(machine_index);" in source
    assert "_distributed_to_clients_reliably" in source[source.index("static void distributed_send_aim_assist"):]
    handler = source[source.index("case _distributed_message_aim_assist_policy:\n\t{"):]
    handler = handler[:handler.index("break;")]
    assert "machine_index == NONE" in handler and "_game_connection_network_client" in handler
    assert "!main_menu_is_active()" in handler and "network_connection_last_read_was_unreliable()" in handler
    # a client starts every game blocked, when its host's version has the policy
    assert "network_game_client_host_network_version() >= AIM_ASSIST_POLICY_NETWORK_VERSION;" in source
    assert "#define AIM_ASSIST_POLICY_NETWORK_VERSION 32" in source


def test_shield_effects_are_message_91():
    header = DISTRIBUTED_H.read_text(encoding="utf-8")
    assert re.search(r"_distributed_message_shield_effects = 91,", header)
    source = DISTRIBUTED_C.read_text(encoding="utf-8")
    # (port, this fork: network_shield_effects.c's, which draws them) a client
    # takes it from the host's stream only, whole; a host takes none; it is
    # stock again at every game
    handler = source[source.index("case _distributed_message_shield_effects:\n"):]
    handler = handler[:handler.index("break;")]
    assert "network_shield_effects_handle_message(machine_index, message, size);" in handler
    assert source.count("case _distributed_message_shield_effects:") == 3
    assert "network_shield_effects_reset();" in source[source.index("void network_distributed_new_game("):]
    effects = (ROOT / "port/linux/game/network_shield_effects.c").read_text(encoding="utf-8")
    taken = effects[effects.index("void network_shield_effects_handle_message("):]
    taken = taken[:taken.index("\n}\n")]
    assert "machine_index != NONE" in taken and "game_connection() != _game_connection_network_client" in taken
    assert "network_connection_last_read_was_unreliable()" in taken and "size != sizeof(message)" in taken
    assert "client_effects = _shield_effects_stock;" in effects


def test_unknown_kinds_leave_the_forks_own():
    """a message kind this build does not know is dropped before anything
    reads it, and said once a game unless the host's layout lists it; (port,
    this fork) its own kinds 78 to 86, vote kick, voice and map sharing, are
    known: 64 to the last without a gap"""
    source = DISTRIBUTED_C.read_text(encoding="utf-8")
    known = source[source.index("static boolean distributed_kind_known("):]
    known = known[:known.index("\n}\n")]
    assert "type >= _distributed_message_actor_states && type < NUMBER_OF_DISTRIBUTED_MESSAGES" in known
    assert "_distributed_message_votekick == _distributed_message_coop_device_states + 1" in source
    handler = source[source.index("void network_distributed_handle_message("):]
    assert handler.index("map_share_handles_message(") < handler.index("distributed_kind_known(header.type)")
    assert "csmemset(distributed_unknown_kinds_seen, 0, sizeof(distributed_unknown_kinds_seen));" in source
    # (the host's shield effects: up to the host's layout's largest, a value
    # this build has no look for drawn as stock)
    effects = (ROOT / "port/linux/game/network_shield_effects.c").read_text(encoding="utf-8")
    assert "network_rules_layout(network_game_client_host_network_version(), &layout);" in effects


def test_settings_record_by_the_hosts_version():
    """the client takes the record's size by the host's version, widens an
    older host's into this build's (35's layout: OpenCE's game types, which
    this fork plays), and leaves no game for its game type"""
    source = (ROOT / "source/networking/network_client_message_handler.c").read_text(encoding="utf-8")
    piece = source[source.index("static boolean network_game_client_receive_game_settings_piece("):]
    piece = piece[:piece.index("\n}\n")]
    assert "network_rules_settings_size_taken(network_game_client_host_network_version(), piece->total_size)" in piece
    assert "network_game_layout_from_version(" in piece
    assert "network_game_client_note_host_network_version(NETWORK_GAME_CLIENT_EXTENDED_OPTIONS_VERSION)" in piece
    assert "network_rules_settings_fold_options(" not in piece and "network_rules_settings_fold(" not in piece
    assert "leave_unsupported_game_type" not in source and "doesn't support yet" not in source
    assert "HALO_PORT_WIRE_NETWORK_GAME_SIZE == NETWORK_GAME_CLIENT_SETTINGS_EXTENDED_SIZE" in source
    # (a newer version's layout from the table: the options' bytes by the
    # host's version, the record no larger than the staging's room, folded
    # into this build's, 35's, before it is widened or taken)
    assert "network_rules_settings_options_size(network_game_client_host_network_version()," in piece
    assert "piece->total_size > NETWORK_GAME_CLIENT_SETTINGS_MAXIMUM" in piece
    assert piece.index("network_rules_settings_fold_extended(") < piece.index("network_game_layout_from_version(")
    assert "(unsigned long)received, NETWORK_GAME_CLIENT_SETTINGS_EXTENDED_SIZE," in piece
    assert "sizeof(network_game_client_settings_staging) == NETWORK_GAME_CLIENT_SETTINGS_MAXIMUM" in source


def test_host_sends_each_machine_the_record_of_its_version():
    """(port, this fork) the host's record is 35's layout: a machine of 35 to
    38 takes it whole, an older one narrowed, and is refused a game of
    OpenCE's game types, a newer layout's (a signed table's) widened; the
    host's own machine takes it whole"""
    source = (ROOT / "source/networking/network_server_message_handler.c").read_text(encoding="utf-8")
    pieces = source[source.index("static boolean network_game_server_send_settings_pieces("):]
    pieces = pieces[:pieces.index("\n}\n")]
    assert "network_rules_settings_record_copy(" in pieces and "network_rules_settings_extended_copy(" not in pieces
    assert "network_rules_settings_layout_copy(" not in source
    assert "wire_size != NETWORK_GAME_SERVER_SETTINGS_EXTENDED_SIZE" in pieces
    assert "options == NETWORK_GAME_SERVER_OPTIONS_EXTENDED_SIZE" in pieces
    assert "HALO_PORT_WIRE_NETWORK_GAME_SIZE == NETWORK_GAME_SERVER_SETTINGS_EXTENDED_SIZE" in source
    size = source[source.index("static long network_game_server_settings_layout("):]
    size = size[:size.index("\n}\n")]
    local = size[size.index("network_game_server_client_machine_is_local(server, machine)"):]
    assert local.index("entry->size = NETWORK_GAME_SERVER_SETTINGS_EXTENDED_SIZE;") < local.index("return entry->size;")
    assert local.index("entry->options = NETWORK_GAME_SERVER_OPTIONS_EXTENDED_SIZE;") < local.index("return entry->size;")
    assert "entry->options = network_rules_host_settings_options(announce, delta_version);" in size
    refused = source[source.index("static boolean network_game_server_settings_refused("):]
    refused = refused[:refused.index("\n}\n")]
    assert "network_rules_host_takes_game_type(" in refused and "game_modes_variant_is_opence(" in refused
    assert "network_game_server_refuse_game_type(slot);" in refused
    # (every send asks: one machine's, every machine's, and the late joiner's wait)
    assert source.count("network_game_server_settings_refused(server, machine, size)") == 3
    assert source.count("network_game_server_send_settings_pieces(server, machine, ") == 2
    assert source.count(", size, options)") == 2
    manager = SERVER_MANAGER_C.read_text(encoding="utf-8")
    refuse = manager[manager.index("boolean network_game_server_refuse_game_type("):]
    refuse = refuse[:refuse.index("\n}\n")]
    assert "network_game_server_drop_machine(machine_index, _rejection_code_version_too_old)" in refuse
    assert "network_distributed_host_notice(notice);" in refuse


def test_policy_setting_is_documented():
    config = (ROOT / "port/linux/src/port_config.c").read_text(encoding="utf-8")
    assert re.search(r'"network\.block_mouse_aim_assist", _config_boolean, "false", "HALO_NET_BLOCK_MOUSE_AIM_ASSIST"', config)
    assert "`network.block_mouse_aim_assist` | `false` |" in (ROOT / "port/linux/README.md").read_text(encoding="utf-8")
    assert "`HALO_NET_BLOCK_MOUSE_AIM_ASSIST` | `false` |" in (ROOT / "server/docs/settings.md").read_text(encoding="utf-8")


def test_the_version_and_the_wire():
    limits = (ROOT / "port/linux/include/halo_port_limits.h").read_text(encoding="utf-8")
    delta = (ROOT / "port/linux/include/delta.h").read_text(encoding="utf-8")
    # (port, this fork: its hosts announce 39, OpenCE's newest, and speak
    # ChupathingyCE's wire of 0.8.1b, chupa-39a, its row 39 too, which its
    # clients of 0.8.0e on join by its range: tools/test_delta_compat.py)
    assert "#define HALO_PORT_NETWORK_VERSION 39\n" in limits
    assert '#define DELTA_WIRE "chupa-39a"' in delta
    assert 'X("chupa-36a"' not in delta and "DELTA_WIRE_HOST_OLDEST" not in delta
    assert "#define DELTA_LEGACY_NEWEST 39\n" in delta
    for number in (30, 31, 32, 33, 34, 35, 36, 37, 38, 39):
        assert re.search(rf'X\({number}, "build-\d+", additive\)', delta)
    assert "#define HALO_PORT_NETWORK_VERSION_MAXIMUM 40\n" in limits


def test_game_kind_beside_the_delta_tag():
    """port (this fork): network version 37's game kind is the LAN
    advertisement's fourth reserved byte, Delta's major in its old layout: a
    host writes the kind unless a dedicated server wrote the old layout for
    released clients there (its wire after it), and a client reads no kind
    from such a host"""
    handler = (ROOT / "source/networking/network_server_message_handler.c").read_text(encoding="utf-8")
    write = handler.index("advertisement.reserved[HALO_PORT_ADVERTISED_KIND_OFFSET] = game_modes_kind")
    assert '"chupa-", 6))' in handler[write - 400:write]
    assert handler.index("delta_peer_advertised_wire(advertisement.reserved") < write
    client = (ROOT / "source/networking/network_client_manager.c").read_text(encoding="utf-8")
    assert '"chupa-", 6);' in client and ".kind = old_slot ? 0 :" in client


def test_dedicated_flag_is_not_delta_flag():
    """port (this fork): OpenCE 39's dedicated flag is the bit Delta's old
    layout's flag is (0x04): a client takes it for the dedicated flag from a
    host of 39 or later with the Delta tag or without the old layout's wire,
    and leaves the reserved bytes whole for Delta's own reading
    (delta_compat_speaks_delta); a dedicated host sets both listing bits"""
    limits = (ROOT / "port/linux/include/halo_port_limits.h").read_text(encoding="utf-8")
    delta = (ROOT / "port/linux/include/delta.h").read_text(encoding="utf-8")
    assert "#define HALO_PORT_ADVERTISED_DEDICATED_FLAG 0x04" in limits and "#define DELTA_ADVERTISED_FLAG 0x04" in delta
    client = (ROOT / "source/networking/network_client_manager.c").read_text(encoding="utf-8")
    read = client[client.index("boolean old_slot ="):]
    read = read[:read.index("\n\t\t\t}\n")]
    assert "HALO_PORT_DEDICATED_NETWORK_VERSION" in read and "(tagged || !old_slot)" in read
    assert "reserved[HALO_PORT_ADVERTISED_FLAGS_OFFSET] &=" not in read
    dedicated = client[client.index("boolean network_game_client_advertised_game_dedicated("):]
    assert ".dedicated;" in dedicated[:dedicated.index("\n}")]
    lobby = (ROOT / "port/linux/src/p2p_lobby.c").read_text(encoding="utf-8")
    assert "_listing_dedicated = 32," in lobby and "_listing_chupa_dedicated = 128," in lobby
    assert lobby.count("_listing_dedicated | _listing_chupa_dedicated") >= 3


def test_elevator_carry_of_the_tick_is_for_network_games_only():
    """a local game (campaign, single player, film) carries an elevator's
    riders as the game always did; the tick's own carry is sampled and applied
    only in a network game, host or client"""
    source = (ROOT / "source/devices/device_machines.c").read_text(encoding="utf-8")
    networked = source[source.index("boolean machine_elevator_networked("):]
    networked = networked[:networked.index("\n}")]
    assert "_game_connection_network_client" in networked and "_game_connection_network_server" in networked
    assert "_game_connection_local" not in networked and "film" not in networked
    sample = source[source.index("boolean machine_elevator_sample("):]
    sample = sample[:sample.index("\n}")]
    assert "!machine_elevator_networked()" in sample.split("return FALSE")[0]
    update = source[source.index("boolean machine_update("):]
    update = update[:update.index("\n}")]
    legacy = update[update.index("if (!machine_elevator_networked()"):]
    assert "previous = machine->machine.elevator_position" in legacy
    assert legacy.index("machine_elevator_carry_riders") < legacy.index("_device_position_changed_bit")
    # objects.c samples through the same gate (the sample is refused locally)
    assert "machine_elevator_sample(object_index, &elevator_sample)" in \
        (ROOT / "source/objects/objects.c").read_text(encoding="utf-8")
