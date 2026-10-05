"""Keep fast multiplayer hosting playable before the user changes rules."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLAYER_UI = ROOT / "source/interface/player_ui.c"


def function_body(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def test_fast_setup_seeds_slayer_before_playlist_and_server_creation():
    source = PLAYER_UI.read_text(encoding="ascii")
    body = function_body(source, "void player_ui_fast_setup_network_server(")

    clear = body.index("player_ui_globals.multiplayer_variant_specified = FALSE;")
    build = body.index(
        'game_engine_get_variant_by_name(&default_variant, "slayer")')
    select = body.index("player_ui_set_game_variant(")
    playlist = body.index("game_engine_playlist_initialize();")
    create = body.index("create_global_network_game_server()")

    # playlist_initialize snapshots the selected variant into global_stage;
    # server creation then copies that stage into its authoritative game.
    assert clear < build < select < playlist < create
