"""Tests of the host's line of sight on clients' reported hits
(network.hit_line_of_sight): the C tests of its geometry (port/linux/tests/
line_of_sight_test.c: a wall between shooter and target refused, a clear
line taken, the target's edge, glass, a vehicle gunner's gun, a grenade
around a corner, an explosion's core, a projectile that homes in or arcs,
the budget), built with the build machine's C compiler, and what the C tests
cannot reach: the setting, its documentation, and that the host asks it for
shots and explosions, not melee blows or collisions."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
DAMAGE_C = ROOT / "port/linux/game/network_damage.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_geometry(tmp_path):
    """the geometry's C tests, warnings as errors"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "line_of_sight_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/line_of_sight_test.c"), str(ROOT / "port/linux/game/line_of_sight.c"),
         "-o", str(binary), "-lm"],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "line_of_sight_test: ok" in result.stdout


def test_setting_on_by_default_and_documented():
    config = (ROOT / "port/linux/src/port_config.c").read_text(encoding="utf-8")
    assert re.search(r'"network\.hit_line_of_sight", _config_boolean, "true", "HALO_NET_HIT_LINE_OF_SIGHT"', config)
    assert "`network.hit_line_of_sight` | `true` | `HALO_NET_HIT_LINE_OF_SIGHT` |" in \
        (ROOT / "port/linux/README.md").read_text(encoding="utf-8")
    assert "network.hit_line_of_sight" in (ROOT / "port/linux/NETCODE.md").read_text(encoding="utf-8")


def test_host_checks_shots_and_explosions_only():
    source = DAMAGE_C.read_text(encoding="utf-8")
    assert 'config_boolean("network.hit_line_of_sight")' in source
    # (after the target's own checks; never for a melee blow or a collision. port (this fork):
    # OpenCE's own check runs just before it, under the same setting, and a shot that one saw
    # only from a VR hand (network.fire_origins) skips this one, which looks from no hand)
    check = re.search(r"if \(damage_line_of_sight && !melee && !collision && !\(damage_report_seen_by_hand && !area\) &&\s*"
                      r"!distributed_report_in_sight\(player_index, report, target, definition, &fired_reach, area\)\)",
                      source)
    assert check
    assert source.index("distributed_target_seen(report->object_index") < check.start()
    opence = source.index("if (!distributed_report_line_of_sight(machine_index, player_index, &report->damage.origin, &centre,")
    assert source.rindex("damage_report_seen_by_hand = FALSE;", 0, opence) < opence < check.start()
    assert re.search(r"if \(damage_line_of_sight && !melee && !collision && !area &&",
                     source[source.rindex("damage_report_seen_by_hand = FALSE;", 0, opence):opence])
    # (counted with the other refusals, and logged)
    assert "damage_line_of_sight_reports++" in source
    assert "with no line of sight" in source
