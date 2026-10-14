"""The real filter of the shared ban lists (port/linux/src/shared_bans.c): a list another host publishes puts only ban
lines (a hardware id or an address, printable, short) in this host's shared_bans.txt, each with its list's name."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, constant, function, mutated, read, run  # noqa: E402

CASES = ["keeps-bans", "leaves-out-the-rest", "whole-field-names", "printable-and-short", "whole-lines-in-the-room"]

# a fault in shared_bans.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "comments-kept": (("text[start] == '#' || ", ""), "shared_bans_filter", "leaves-out-the-rest"),
    "anything-kept": (("(!has_field(text + start, length, \"hwid=\") && !has_field(text + start, length, \"ip=\"))",
                       "0"), "shared_bans_filter", "leaves-out-the-rest"),
    "names-anywhere": (("(at == 0 || line[at - 1] == '\\t') && ", ""), "has_field", "whole-field-names"),
    "controls-kept": (("printable = 0;", "printable = 1;"), "shared_bans_filter", "printable-and-short"),
    "long-lines-kept": ((" || length > SHARED_BANS_MAXIMUM_LINE", ""), "shared_bans_filter", "printable-and-short"),
    "no-list-name": (("\"\\tlist=%s\\n\", source", "\"\\n%s\", \"\""), "shared_bans_filter", "keeps-bans"),
}


def generated(fault=None, faulty_function=None):
    source = read("port/linux/src/shared_bans.c")
    config = f"#define SHARED_BANS_MAXIMUM_LINE {constant(source, 'SHARED_BANS_MAXIMUM_LINE')}\n"
    text = ""
    for name in ("has_field", "shared_bans_filter"):
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("shared_bans", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("shared_bans", generated(fault, faulty_function)), case)
    assert status != 0, f"{control}: {case} did not catch it"
