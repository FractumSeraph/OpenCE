"""The scenery and devices whose damage every machine shares: a body to destroy, or a shield to break."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run


def generated(fault=None):
    source = read('port/linux/game/network_damage.c')
    code = '\n'.join(function(source, name) for name in ('static_target_shields_shared', 'static_target',
                                                         'static_target_damage', 'distributed_distance_squared',
                                                         'static_target_near'))
    # (the network version from which shield-only scenery is shared: halo_port_limits.h's)
    limits = read('port/linux/include/halo_port_limits.h')
    code = ('#define HALO_PORT_SHIELD_TARGETS_NETWORK_VERSION %d\n' %
            constant(limits, 'HALO_PORT_SHIELD_TARGETS_NETWORK_VERSION') + code)
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', ['body', 'shield', 'neither', 'damage', 'near', 'older_host'])
def test_case(case):
    status, output = run(build('static_target', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    # (the shield-only objects left out, as before: Rat Race's shields, a map's tripmines)
    (('||\n\t\t\t(static_target_shields_shared() && object_get_maximum_shield_vitality(object_index, TRUE) > 0.0f)', ''),
     'shield'),
    # (a client of a host before network version 34 taking that version's shields as 34's)
    (('static_target_shields_shared() && object_get_maximum_shield_vitality', 'object_get_maximum_shield_vitality'),
     'older_host'),
    (('return static_target_shields_shared() &&\n', 'return\n'), 'older_host'),
    (('host_before(HALO_PORT_SHIELD_TARGETS_NETWORK_VERSION)', 'host_before(33)'), 'older_host'),
    (('game_connection() != _game_connection_network_client ||', ''), 'older_host'),
    (('STATIC_TARGET_TYPES)', 'STATIC_TARGET_TYPES | _object_mask_unit)'), 'neither'),
    (('object_get_maximum_body_vitality(object_index, TRUE) > 0.0f ||', ''), 'body'),
    (('damage->owner_object_index != NONE && static_target(damage->owner_object_index)', 'FALSE'), 'damage'),
    (('distance < nearest_distance', 'distance > nearest_distance'), 'near'),
    (('if (!static_target(iterator.index))\n\t\t\tcontinue;', ''), 'near'),
])
def test_negative_control(fault, case):
    status, output = run(build('static_target', generated(fault)), case)
    assert status == CHECK_FAILED, output
