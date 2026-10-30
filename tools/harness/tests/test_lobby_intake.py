"""The server browser's intake (p2p_lobby.c's listing_wanted and p2p_lobby_slot_heard): which listings take a queue
place, and a broker's burst of kept slots leaving every live game one.

Port (this fork): listings of every version are let in, for the browser to show or mark (version_joinable: Delta's
join range, delta_legacy_minimum to _maximum, here the built-in one a build uses with no signed table); a full queue
gives a joinable listing the place of one that is not, so the burst runs through the fork's p2p_lobby_slot_heard."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure

SIZES = ('P2P_KEY_SIZE', 'P2P_KEY_HASH_SIZE', 'P2P_TOKEN_SIZE', 'P2P_SEALED_TOKEN_SIZE', 'P2P_SIGNATURE_SIZE',
         'P2P_LISTING_NAME_SIZE', 'P2P_LISTING_MAP_SIZE', 'P2P_LISTING_GAMETYPE_SIZE')


def size(source, name):
    """an enum constant set to a small sum of numbers (P2P_SEALED_TOKEN_SIZE = 24 + 16 + 16)"""
    value = re.search(r"\b" + name + r"\s*=\s*([\d +]+)[,\n]", source).group(1)
    return sum(int(part) for part in value.split("+"))


def generated(fault=None):
    source = read('port/linux/src/p2p_lobby.c')
    headers = read('port/linux/src/p2p_internal.h') + read('port/linux/src/p2p.h')
    limits = read('port/linux/include/halo_port_limits.h')
    config = ''.join(f'#define {name} {size(headers, name)}\n' for name in SIZES)
    for name in ('HALO_PORT_NETWORK_VERSION', 'HALO_PORT_NETWORK_VERSION_MINIMUM', 'HALO_PORT_NETWORK_VERSION_MAXIMUM'):
        config += f'#define {name} {re.search("#define " + name + r" (\d+)", limits).group(1)}\n'
    # (the file's constants: the listing's format and sizes, the windows and the queue's places)
    config += re.search(r"^enum\n\{\n\tLISTING_FORMAT.*?^\};\n", source, re.M | re.S).group(0)
    config += structure(source, 'queued') + '\n'
    code = '\n'.join([structure(source, 'listing')] +
                     [function(source, name) for name in ('sanitize', 'get_long', 'get_text', 'listing_read',
                                                          'listing_current', 'version_joinable', 'queued_joinable',
                                                          'listing_wanted', 'p2p_lobby_slot_heard')])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', ['filter', 'burst'])
def test_case(case):
    status, output = run(build('lobby_intake', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    # every listing let in, however old
    (('return listing_current(&listing, retained);', 'return 1;'), 'burst'),
    # (OpenCE's control, "every version let in", is this fork's design: its equivalent is the join range's check)
    # every version taken as joinable
    (('return version >= delta_legacy_minimum() && version <= delta_legacy_maximum();', 'return 1;'), 'burst'),
    # a full queue that takes nothing more, a joinable listing's place never made
    (('if (!queued_joinable(payload, size))\n\t\t\treturn;', 'return;'), 'burst'),
    # a newer version's host counted though this build joins it
    (('if (listing.version > HALO_PORT_NETWORK_VERSION && listing.version > delta_legacy_maximum() &&',
      'if (listing.version > HALO_PORT_NETWORK_VERSION &&'), 'filter'),
])
def test_negative_control(fault, case):
    status, output = run(build('lobby_intake', generated(fault)), case)
    assert status == CHECK_FAILED, output
