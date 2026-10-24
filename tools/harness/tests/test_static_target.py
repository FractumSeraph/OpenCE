"""The scenery and devices whose damage every machine shares: a body to destroy, or a shield to break (from
network version 34, by the version a client's host advertised: network_rules_of_version)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run


def generated(fault=None):
    source = read('port/linux/game/network_damage.c')
    rules = read('port/linux/game/network_rules.c')
    distributed = read('port/linux/game/network_distributed.c')
    # (the version rule itself, network_distributed.c's, and the reach, network_rules.c's)
    code = '\n'.join([function(distributed, 'network_rules_of_version'),
                      function(rules, 'network_rules_static_within')] +
                     [function(source, name) for name in ('static_target', 'network_damage_client_runs_shields',
                                                          'static_target_damage', 'static_target_nearest')])
    # (the network version from which shield-only scenery is shared: network_rules.h's, as halo_port_limits.h's)
    header = read('port/linux/game/network_rules.h')
    limits = read('port/linux/include/halo_port_limits.h')
    assert constant(header, 'NETWORK_RULES_STATIC_SHIELDS_VERSION') == \
        constant(limits, 'HALO_PORT_SHIELD_TARGETS_NETWORK_VERSION')
    code = ('#define NETWORK_RULES_STATIC_SHIELDS_VERSION %d\n#define NETWORK_RULES_VERSION_UNKNOWN %d\n' %
            (constant(header, 'NETWORK_RULES_STATIC_SHIELDS_VERSION'),
             constant(header, 'NETWORK_RULES_VERSION_UNKNOWN')) + code)
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', ['body', 'shield', 'neither', 'damage', 'near', 'older_host', 'shields'])
def test_case(case):
    status, output = run(build('static_target', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    # (the shield-only objects left out, as before: Rat Race's shields, a map's tripmines)
    (('||\n\t\t\t(network_rules_of_version(NETWORK_RULES_STATIC_SHIELDS_VERSION) &&\n'
      '\t\t\t\tobject_get_maximum_shield_vitality(object_index, TRUE) > 0.0f)', ''), 'shield'),
    # (a client of a host before network version 34 taking that version's shields as 34's)
    (('(network_rules_of_version(NETWORK_RULES_STATIC_SHIELDS_VERSION) &&\n\t\t\t\tobject_get_maximum_shield_vitality',
      '(object_get_maximum_shield_vitality'), 'older_host'),
    (('return network_rules_of_version(NETWORK_RULES_STATIC_SHIELDS_VERSION) &&\n\t\tdamage->owner_object_index',
      'return\n\t\tdamage->owner_object_index'), 'older_host'),
    (('host >= version', 'host >= version - 1'), 'older_host'),
    (('host != NETWORK_RULES_VERSION_UNKNOWN && ', ''), 'older_host'),
    (('return (unsigned int)delta_legacy_announce() >= version;', 'return FALSE;'), 'older_host'),
    (('STATIC_TARGET_TYPES)', 'STATIC_TARGET_TYPES | _object_mask_unit)'), 'neither'),
    (('object_get_maximum_body_vitality(object_index, TRUE) > 0.0f ||', ''), 'body'),
    (('damage->owner_object_index != NONE && static_target(damage->owner_object_index)', 'FALSE'), 'damage'),
    (('distance < nearest_distance', 'distance > nearest_distance'), 'near'),
    (('if (!static_target(iterator.index))\n\t\t\tcontinue;', ''), 'near'),
    (('return network_rules_of_version(NETWORK_RULES_STATIC_SHIELDS_VERSION) && static_target(object_index);',
      'return static_target(object_index);'), 'shields'),
])
def test_negative_control(fault, case):
    status, output = run(build('static_target', generated(fault)), case)
    assert status == CHECK_FAILED, output
