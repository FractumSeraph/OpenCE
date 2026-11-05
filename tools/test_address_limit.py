"""Tests of a host's limit on the machines of one address: the C tests
(port/linux/tests/address_limit_test.c, of port/linux/game/network_rules.c's
rules), built with the build machine's C compiler, and what the sources must
hold that the C tests cannot reach: the host's check before a machine is
added (its own machine and one joining again left out), OpenCE's refusal
code 13, internet play's stand-ins counted by their peers' real addresses,
the settings (a player's host off by default, a dedicated server's 8), and
no address written whole in the log."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SERVER_MANAGER_C = ROOT / "source/networking/network_server_manager.c"
GAME_PROTOCOL_H = ROOT / "source/networking/network_game_protocol.h"
CLIENT_MANAGER_C = ROOT / "source/networking/network_client_manager.c"
DISTRIBUTED_C = ROOT / "port/linux/game/network_distributed.c"
PORT_CONFIG_C = ROOT / "port/linux/src/port_config.c"
SERVER_CONFIG_C = ROOT / "server/src/server_config.c"
SERVER_CONFIG_H = ROOT / "server/src/server_config.h"
DEDICATED_C = ROOT / "server/src/dedicated.c"
SERVER_ADMIN_C = ROOT / "server/src/server_admin.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def _function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start)]


def test_rules(tmp_path):
    """the limit's C tests, warnings as errors"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "address_limit_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/address_limit_test.c"), str(ROOT / "port/linux/game/network_rules.c"),
         "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "address_limit_test: ok" in result.stdout


def test_host_checks_before_adding_the_machine():
    accept = _function(SERVER_MANAGER_C.read_text(encoding="utf-8"),
                       "boolean network_game_server_accept_client_machine_into_game(")
    check = accept.index("network_distributed_address_full(address.address.long_words[0], others, count)")
    assert check < accept.index("network_game_add_machine(&server->game, &new_machine)")
    loop = accept[accept.index("unsigned long others[MAXIMUM_NETWORK_MACHINE_COUNT];"):check]
    # (the joined machines, not the host's own, nor one of the same stand-in,
    # which the join drops)
    assert "network_game_server_client_machine_is_joined_to_game(server, other)" in loop
    assert "!network_game_server_client_machine_is_local(server, other)" in loop
    assert "network_game_server_client_machine_addresses[other_index] != address.address.long_words[0]" in loop
    piece = accept[accept.index("network_distributed_address_limit() > 0") - 300:check + 600]
    assert "!network_game_server_client_machine_is_local(server, machine)" in piece
    # (port, this fork: OpenCE's refusal codes are network_game_protocol.h's enum, the code
    # _rejection_code_address_full; see test_refusal_is_opence_code_13)
    assert "network_game_server_refusal_code = _rejection_code_address_full;" in piece
    assert "transport_address_to_string(&address)" in piece
    # (port, this fork: OpenCE's own limit, a dedicated server's server.max_machines_per_address,
    # network_link.c, is checked too, before this one)
    assert accept.index("refusal = network_link_address_limit(network_game_server_get_game(server), machine_index,") < \
        accept.index("network_distributed_address_limit() > 0")


def test_refusal_is_opence_code_13():
    # (port, this fork: the server's refusal codes are OpenCE's enum, which numbers the address's
    # 13: count its members, each from 0, none given a number past version_too_old's)
    protocol = GAME_PROTOCOL_H.read_text(encoding="utf-8")
    body = protocol[protocol.index("_rejection_code_version_too_old = 0,"):]
    body = body[:body.index("NUMBER_OF_SERVER_REJECTION_CODES")]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    codes = re.findall(r"(_rejection_code_[a-z_]+)(\s*=\s*\d+)?\s*,", body)
    assert all(not number for _, number in codes[1:])
    assert [name for name, _ in codes].index("_rejection_code_address_full") == 13
    assert "network_game_server_refusal_code = _rejection_code_address_full;" in \
        SERVER_MANAGER_C.read_text(encoding="utf-8")
    client = CLIENT_MANAGER_C.read_text(encoding="utf-8")
    assert "#define REJECTION_CODE_ADDRESS_FULL 13" in client
    assert "return JOIN_REASON_ADDRESS_FULL;" in client


def test_stand_ins_counted_by_real_address():
    source = DISTRIBUTED_C.read_text(encoding="utf-8")
    # (port, this fork: not static, network_votekick.c, network_link.c and the server's commands use it too)
    real = _function(source, "\nunsigned long distributed_real_address(")
    assert "(address & 0xFFC00000) == 0x64400000" in real
    assert "p2p_peer_endpoint_address(network)" in real
    full = _function(source, "boolean network_distributed_address_full(")
    assert "distributed_real_address(others[index])" in full
    assert "network_rules_address_full((uint32_t)distributed_real_address(address)" in full
    # (no address in the log from here)
    assert "error(" not in full and "network_event(" not in full


def test_settings():
    config = PORT_CONFIG_C.read_text(encoding="utf-8")
    assert re.search(r'\{ "network\.max_machines_per_address", _config_integer, "0", '
                     r'"HALO_NET_MAX_MACHINES_PER_ADDRESS"', config)
    assert '"max_machines_per_address", SERVER_FIELD_INTEGER, 0, SERVER_SETTINGS_MAXIMUM_MACHINES_PER_ADDRESS' in \
        SERVER_CONFIG_C.read_text(encoding="utf-8")
    header = SERVER_CONFIG_H.read_text(encoding="utf-8")
    assert "SERVER_SETTINGS_DEFAULT_MACHINES_PER_ADDRESS = 8," in header
    dedicated = DEDICATED_C.read_text(encoding="utf-8")
    assert 'setting_number("HALO_DEDICATED_MAX_MACHINES_PER_ADDRESS", &settings,' in dedicated
    assert "network_distributed_set_address_limit(maximum);" in dedicated
    admin = SERVER_ADMIN_C.read_text(encoding="utf-8")
    assert "case SERVER_SETTING_MAX_MACHINES_PER_ADDRESS: network_distributed_set_address_limit(" in admin
    assert "setting == SERVER_SETTING_MAX_MACHINES_PER_ADDRESS" in admin
