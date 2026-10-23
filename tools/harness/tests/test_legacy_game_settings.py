"""An older host's game settings record on a client of this version (36): before 35 its PC options were their first
0x1C bytes (the record 16 bytes shorter), widened with OpenCE's options zeroed; before 33 no ALLY FRIENDLY FIRE bit."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run


def generated(fault=None):
    source = read('port/linux/game/network_game_layout.c')
    code = '\n'.join(function(source, name) for name in ('network_game_layout_size',
                                                         'network_game_layout_from_version'))
    if fault:
        code = mutated(code, *fault)
    return (('network_game_layout.h', read('port/linux/game/network_game_layout.h')), ('under_test.inc', code))


def test_numbers_are_the_games():
    """the layout's numbers are halo_port_limits.h's and game_engine.h's"""
    layout = read('port/linux/game/network_game_layout.h')
    limits = read('port/linux/include/halo_port_limits.h')
    engine = read('source/game/game_engine.h')
    assert constant(layout, 'NETWORK_GAME_LAYOUT_GAME_MODES_VERSION') == \
        constant(limits, 'HALO_PORT_GAME_MODES_NETWORK_VERSION')
    assert constant(layout, 'NETWORK_GAME_LAYOUT_ALLY_FRIENDLY_FIRE_VERSION') == \
        constant(limits, 'HALO_PORT_ALLY_FRIENDLY_FIRE_NETWORK_VERSION')
    assert constant(layout, 'NETWORK_GAME_LAYOUT_LEGACY_OPTIONS_SIZE') == constant(engine, 'GAME_VARIANT_OPTIONS_V1_SIZE')
    assert constant(layout, 'NETWORK_GAME_LAYOUT_TAIL_SIZE') - constant(layout, 'NETWORK_GAME_LAYOUT_LEGACY_TAIL_SIZE') == \
        constant(limits, 'HALO_PORT_LEGACY_OPTIONS_SHORTFALL')
    assert '(HALO_PORT_NETWORK_GAME_PLAYERS_END + 0x%X)' % constant(layout, 'NETWORK_GAME_LAYOUT_TAIL_SIZE') in limits


@pytest.mark.parametrize('case', ['legacy', 'legacy_ally', 'legacy_unknown', 'console', 'current', 'bad_size'])
def test_case(case):
    status, output = run(build('legacy_game_settings', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    # (OpenCE's options left as the bytes after the old ones, the local data not moved)
    (('memset(options + NETWORK_GAME_LAYOUT_LEGACY_OPTIONS_SIZE, 0,', 'memset(options + NETWORK_GAME_LAYOUT_OPTIONS_SIZE, 0,'),
     'legacy'),
    (('memmove(options + NETWORK_GAME_LAYOUT_OPTIONS_SIZE, options + NETWORK_GAME_LAYOUT_LEGACY_OPTIONS_SIZE,',
      'memmove(options + NETWORK_GAME_LAYOUT_LEGACY_OPTIONS_SIZE, options + NETWORK_GAME_LAYOUT_LEGACY_OPTIONS_SIZE,'),
     'legacy'),
    # (the ally bit cleared for every host, for none, or kept for one before 33)
    (('host_version != 0xFFFF && host_version < NETWORK_GAME_LAYOUT_ALLY_FRIENDLY_FIRE_VERSION', '1'), 'current'),
    (('host_version != 0xFFFF && host_version < NETWORK_GAME_LAYOUT_ALLY_FRIENDLY_FIRE_VERSION', '0'), 'legacy_ally'),
    (('host_version < NETWORK_GAME_LAYOUT_ALLY_FRIENDLY_FIRE_VERSION', 'host_version <= 34'), 'legacy'),
    # (a record of another size taken)
    (('else if (received != size)', 'else if (0)'), 'bad_size'),
])
def test_negative_control(fault, case):
    status, output = run(build('legacy_game_settings', generated(fault)), case)
    assert status == CHECK_FAILED, output
