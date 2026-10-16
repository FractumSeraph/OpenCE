"""Text chat and the keyboard (port/linux/src/xinput_sdl.c, port/linux/game/chat.c): the real keystroke filter feeding
the real chat line. While a line is up the keys typed reach it, Enter says it and Escape closes it, and either gives
the keyboard back to the game. ChupathingyCE's first chat build passed no key press while the line was up: the line
could never be closed, and the keyboard drove nothing for the rest of the game (the negative control)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["enter", "escape", "backspace", "closed"]

# a fault, the file it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    # (ChupathingyCE's first chat build: no key press passes while a line is up)
    "keys-held-back": (("console_is_active() || chat_typing ||", "console_is_active() ||"), "xinput", "enter"),
    "escape-held-back": (("console_is_active() || chat_typing ||", "console_is_active() ||"), "xinput", "escape"),
    # (a line closed without giving the keyboard back)
    "keyboard-kept": (("platform_chat_typing(chat.log_open);", ";"), "chat", "escape"),
}


def generated(fault=None, faulty_file=None):
    inputs = read("port/linux/src/xinput_sdl.c").replace("\r\n", "\n")
    chat = read("port/linux/game/chat.c").replace("\r\n", "\n")
    keys = "\n".join([
        enum_with(read("source/input/input.h"), "_key_escape"),
        structure(read("source/input/input.h"), "key_stroke"),
        structure(read("port/linux/src/sdl_platform.h"), "platform_keystroke"),
        structure(read("port/include/xdk/xdk_pdb.h"), "_XINPUT_DEBUG_KEYSTROKE"),
        "typedef struct _XINPUT_DEBUG_KEYSTROKE XINPUT_DEBUG_KEYSTROKE;",
    ]) + "\n"
    xinput = "\n".join(function(inputs, name) for name in ("platform_chat_typing", "XInputDebugGetKeystroke"))
    line = "\n".join(function(chat, name) for name in ("typing_end", "typing_keys"))
    if fault and faulty_file == "xinput":
        xinput = mutated(xinput, *fault)
    if fault and faulty_file == "chat":
        line = mutated(line, *fault)
    return (("keys.inc", keys), ("under_test.inc", xinput + "\n" + line + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("chat_keys", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_file, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("chat_keys", generated(fault, faulty_file)), case)
    assert status != 0, f"{control}: {case} did not catch it"
