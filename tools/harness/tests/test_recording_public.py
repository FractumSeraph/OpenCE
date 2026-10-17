"""The real filter of a recording's public copy (port/linux/game/network_recording.c, public_records): no player's
hardware id or address, no fair play verdict and no refused move reaches what anyone may watch; the rest is kept."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["no-private-data", "hit-without-verdict", "no-moves", "truncated", "no-moderation-events"]

# a fault in public_records, and the case that must catch it
NEGATIVE_CONTROLS = {
    "private-data-kept": (("if (type == _record_player && size >= 7", "if (0 && type == _record_player && size >= 7"),
                          "no-private-data"),
    "verdict-kept": (("csmemset(out + written + 5 + 18, 0, 9);", ";"), "hit-without-verdict"),
    "moves-kept": (("if (type == _record_move)\n\t\t\tcontinue;", ""), "no-moves"),
    "join-shown-live": (("type != _record_tag && type != _record_player &&", "type != _record_tag &&"),
                        "no-private-data"),
    "flags-kept": (("(payload[4] == RECORDING_EVENT_MODERATION || payload[4] == RECORDING_EVENT_FLAG)", "(0)"),
                   "no-moderation-events"),
}


def generated(fault=None):
    source = read("port/linux/game/network_recording.c").replace("\r\n", "\n")
    code = function(source, "public_records")
    if fault:
        code = mutated(code, *fault)
    records = enum_with(source, "_record_player") + "\n" + enum_with(source, "RECORDING_EVENT_MODERATION") + "\n"
    return (("records.inc", records), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("recording_public", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("recording_public", generated(fault)), case)
    assert status != 0, f"{control}: {case} did not catch it"
