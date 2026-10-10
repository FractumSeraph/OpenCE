"""Guard the browser host map/mode ABI and its game-thread application."""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WEB_ONLINE = ROOT / "port/web/src/web_online_ui.c"
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


def string_array(body, name):
    match = re.search(
        rf"static char const \*const {name}\[\]\s*=\s*\{{(?P<items>.*?)\}};",
        body,
        re.DOTALL,
    )
    assert match, f"missing {name} whitelist"
    return re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', match.group("items"))


def test_configured_host_uses_one_atomic_request():
    source = WEB_ONLINE.read_text(encoding="ascii")
    body = function_body(
        source,
        "EMSCRIPTEN_KEEPALIVE int platform_web_online_host_configured(",
    )

    # (the Xbox's levels, then the Custom Edition maps the page lists, in the request's 8 bits)
    assert "!host_map_index_valid(map_index)" in body
    valid = function_body(source, "static int host_map_index_valid(")
    assert "_web_online_multiplayer_level_count +" in valid
    assert "&web_online_custom_map_count" in valid
    assert "map_index >= 0 && map_index < count && map_index <= 0xff" in valid
    assert "mode_index < 0" in body
    assert "mode_index >= _web_online_game_mode_count" in body
    assert body.count("atomic_store_explicit(") == 1
    assert "pack_request(_web_online_command_host, map_index, mode_index)" in body


def test_host_settings_are_applied_after_fast_server_setup():
    source = WEB_ONLINE.read_text(encoding="ascii")
    body = function_body(source, "static void setup_host(")

    setup = body.index("player_ui_fast_setup_network_server();")
    server_check = body.index("global_network_game_server_get()")
    configure = body.index("player_ui_configure_network_server_game(")
    ready = body.index("_web_online_state_hosting")
    assert setup < server_check < configure < ready


def test_advanced_host_settings_are_opt_in_and_bounded():
    source = WEB_ONLINE.read_text(encoding="ascii")
    advanced = function_body(
        source,
        "EMSCRIPTEN_KEEPALIVE int platform_web_online_host_advanced_configured(",
    )
    basic = function_body(
        source,
        "EMSCRIPTEN_KEEPALIVE int platform_web_online_host_configured(",
    )

    assert "WEB_ONLINE_REQUEST_ADVANCED_BIT" in advanced
    assert "score_to_win < 1 || score_to_win > 1000" in advanced
    assert "respawn_seconds < 0 || respawn_seconds > 30" in advanced
    assert "lives < 0 || lives > 99" in advanced
    assert "health_percent < 25 || health_percent > 400" in advanced
    assert "WEB_ONLINE_REQUEST_ADVANCED_BIT" not in basic

    setup = function_body(source, "static void setup_host(")
    assert "web_online.host_advanced_settings ?" in setup
    assert "player_ui_configure_network_server_game_advanced(" in setup
    assert "player_ui_configure_network_server_game(" in setup


def test_advanced_rules_modify_the_stock_variant_only_when_requested():
    source = PLAYER_UI.read_text(encoding="ascii")
    basic = function_body(
        source,
        "boolean player_ui_configure_network_server_game(",
    )
    advanced = function_body(
        source,
        "boolean player_ui_configure_network_server_game_advanced(",
    )
    internal = function_body(
        source,
        "static boolean player_ui_configure_network_server_game_internal(",
    )

    assert "FALSE," in basic
    assert "TRUE," in advanced
    built = internal.index("game_engine_get_variant_by_name(&variant, variant_name);")
    gated = internal.index("if (advanced_settings)")
    applied = internal.index("variant.universal_variant.score_to_win = score_to_win;")
    published = internal.index("network_game_server_change_game_variant(server, &variant);")
    assert built < gated < applied < published


def test_player_magnetism_setting_crosses_threads_through_an_atomic():
    source = WEB_ONLINE.read_text(encoding="ascii")
    setter = function_body(
        source,
        "EMSCRIPTEN_KEEPALIVE void platform_web_set_player_magnetism_enabled(",
    )
    update = function_body(source, "void web_online_ui_update(")

    assert "atomic_store_explicit(" in setter
    assert "&web_online_requested_player_magnetism" in setter
    assert "atomic_load_explicit(" in update
    # (the player's mouse and touch aim assist; the game-wide player_magnetism_flag is the host's rule)
    assert 'config_write_boolean("input.mouse_aim_assist", requested_magnetism);' in update
    assert "player_magnetism_flag =" not in update


def test_online_setup_restores_browser_identity_after_halo_clears_profiles():
    source = WEB_ONLINE.read_text(encoding="ascii")
    restore = function_body(
        source,
        "static void clear_multiplayer_joins_and_restore_customization(",
    )

    cleared = restore.index("player_ui_clear_multiplayer_joins();")
    invalidated = restore.index("web_online_applied_customization_sequence = 0;")
    reapplied = restore.index("apply_requested_player_customization();")
    assert cleared < invalidated < reapplied

    for signature in ("static void setup_host(", "static void setup_join("):
        body = function_body(source, signature)
        assert "clear_multiplayer_joins_and_restore_customization();" in body
        assert "player_ui_clear_multiplayer_joins();" not in body


def test_only_stock_maps_and_supported_modes_can_reach_engine_apis():
    source = PLAYER_UI.read_text(encoding="ascii")
    body = function_body(
        source,
        "static boolean player_ui_configure_network_server_game_internal(",
    )

    assert string_array(body, "multiplayer_levels") == [
        r"levels\\test\\beavercreek\\beavercreek",
        r"levels\\test\\sidewinder\\sidewinder",
        r"levels\\test\\damnation\\damnation",
        r"levels\\test\\ratrace\\ratrace",
        r"levels\\test\\prisoner\\prisoner",
        r"levels\\test\\hangemhigh\\hangemhigh",
        r"levels\\test\\chillout\\chillout",
        r"levels\\test\\carousel\\carousel",
        r"levels\\test\\boardingaction\\boardingaction",
        r"levels\\test\\bloodgulch\\bloodgulch",
        r"levels\\test\\wizard\\wizard",
        r"levels\\test\\putput\\putput",
        r"levels\\test\\longest\\longest",
    ]
    assert string_array(body, "game_modes") == [
        "slayer",
        "team_slayer",
        "ctf",
        "oddball",
        "king",
        "race",
    ]

    calls = [
        "main_set_multiplayer_map_name(map_name);",
        "game_engine_override_map_name(map_name);",
        "network_game_server_change_map_name(server, map_name);",
        "game_engine_get_variant_by_name(&variant, variant_name);",
        "player_ui_set_game_variant(&variant);",
        "network_game_server_change_game_variant(server, &variant);",
    ]
    positions = [body.index(call) for call in calls]
    assert positions == sorted(positions)
