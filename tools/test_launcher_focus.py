"""The launcher and the window's focus (launcher.c, sdl_platform.c). The
launcher reads the window's events before the game does, the first focus
gained among them; the game's input_state.focused was set only by that event,
so after the launcher the game took its window for unfocused, and
controller_remap_input drops every gamepad's input then (a player with a
second way to play, as Halo CE+, which opens the launcher at each start, had
a dead controller in the menus). Port (this fork): the launcher's loop ends
by taking the focus from SDL's window flags (platform_input_focus_sync)."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAUNCHER_C = ROOT / "port/linux/src/launcher.c"
SDL_PLATFORM_C = ROOT / "port/linux/src/sdl_platform.c"
REMAP_C = ROOT / "port/linux/src/controller_remap_input.c"


def _body(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}\n", start)]


def test_unfocused_drops_gamepads():
    # (why it matters: without the focus, a gamepad is taken for absent)
    body = _body(REMAP_C.read_text(encoding="utf-8"), "int controller_remap_input(")
    assert "if (!input.focused) connected = 0;" in body
    assert "if (!connected) memset(pad, 0, sizeof(*pad));" in body


def test_launcher_takes_the_focus_back():
    run = _body(LAUNCHER_C.read_text(encoding="utf-8"), "static void run(void)")
    loop = run.index("while (!launcher.done)")
    assert run.index("platform_input_focus_sync();") > loop
    sync = _body(SDL_PLATFORM_C.read_text(encoding="utf-8"), "void platform_input_focus_sync(void)")
    assert "SDL_WINDOW_INPUT_FOCUS" in sync
    assert "input_state.focused = focused;" in sync


def test_only_the_event_loops_set_the_focus():
    # (the game's events: focus lost and gained; anything else that reads the
    # window's events before the game does needs platform_input_focus_sync)
    text = SDL_PLATFORM_C.read_text(encoding="utf-8")
    assert text.count("input_state.focused = ") == 3
    polls = [path.name for path in (ROOT / "port/linux/src").glob("*.c")
             if "SDL_PollEvent(" in path.read_text(encoding="utf-8", errors="replace")]
    assert sorted(polls) == ["launcher.c", "sdl_platform.c", "updater.c"], polls
