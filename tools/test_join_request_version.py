"""The dedicated server's own player is not refused (fs.00253).

ChupathingyCE's dedicated server (server/src, chupathingyce-server) hosts a
game and then joins it itself through 127.0.0.1, a host whose version its
client was not told (network_game_client_joined_version 0xFFFF). After the
network 43 merge that request was cut to the Xbox's 0x70 bytes, which dropped
its network_version (a field since network 42, past the 0x70): the server
read version 0, logged "join requires OpenCE network version 43 (client 0)"
and _rejection_code_version_too_old, and re-hosted its game every 5 seconds
(fixed by 74558eff8).

CI has no retail maps for a live server (tools/test_dedicated_server.py
skips its cases with clients for that reason), so this is the unit-level
test: tools/tests/join_request_version_test.c, built with the game's packet
table and encoder (source/networking/network_messages.c,
source/memory/data_*.c) and network rules (port/linux/game/network_rules.c)
compiled with the build's own commands (build.ninja: `python3 configure.py`
first; HOST_FUZZ_BUILD names the build, macos or linux64 by default) plus
AddressSanitizer and UndefinedBehaviorSanitizer, makes the request as the
client does for a host of a version not known and reads it as the host does.
The source checks tie that harness to the client and host code it mirrors.
Without a clang with the sanitizers' runtimes, or the build, the build test
is skipped (it fails where HALO_SANITIZERS_REQUIRED=1, as in CI).

    python3 -m pytest -q tools/test_join_request_version.py
"""

import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools import fuzz_support  # noqa: E402

CLIENT = ROOT / "source/networking/network_client_manager.c"
SERVER = ROOT / "source/networking/network_server_message_handler.c"
RULES = ROOT / "port/linux/game/network_rules.c"
HARNESS = ROOT / "tools/tests/join_request_version_test.c"
SIZES = ROOT / "tools/tests/join_request_version_sizes.c"

# (the build whose units are compiled, as tools/test_host_fuzz.py's)
GAME_BUILD = os.environ.get("HOST_FUZZ_BUILD", "macos" if sys.platform == "darwin" else "linux64")
GAME_SOURCES = [
    "source/memory/data_packet_groups.c",
    "source/memory/data_packets.c",
    "source/memory/data_encoding.c",
    "source/memory/byte_swapping.c",
    "port/linux/game/network_rules.c",
]
# (the encoder reads unaligned on purpose: the wire has no alignment)
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize=alignment", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer", "-O1", "-g"]


def function_body(source, name):
    """a C function's body, from its name at the start of a line to the
    closing brace at the start of a line"""
    match = re.search(rf"^\w[^\n;]*\b{name}\(\n?[^;{{]*?\)\n\{{\n(.*?)^\}}", source, re.S | re.M)
    assert match, f"{name} not found"
    return match.group(1)


def test_client_sends_the_tail_the_rules_give():
    """network_game_client_send_join_request encodes its request with the tail
    network_rules_join_request_tail_size gives for the host's version: what
    the harness encodes with"""
    body = function_body(CLIENT.read_text(encoding="utf-8"), "network_game_client_send_join_request")
    assert re.search(r"short tail = \(short\)network_rules_join_request_tail_size\(version, native\);", body), body
    assert re.search(r"int native = \(int\)sizeof\(join_game_request\) - 0x70;", body), body
    call = body[body.index("create_network_game_message_with_tail("):]
    call = call[:call.index(";")]
    assert re.search(r"sizeof\(join_game_request\),\s*tail\)$", call), call
    assert "join_game_request.network_version = HALO_PORT_NETWORK_VERSION;" in body


def test_unknown_version_takes_the_whole_request():
    """a host whose version is not known gets a tail of 0: the whole request,
    its version word kept"""
    body = function_body(RULES.read_text(encoding="utf-8"), "network_rules_join_request_tail_size")
    assert re.search(r"if \(version == NETWORK_RULES_VERSION_UNKNOWN\)\s*return 0;", body), body


def test_requests_end_with_the_version_word():
    """the client's and the host's join request each end with the version
    word, the last two bytes the harness writes and reads"""
    for path in (CLIENT, SERVER):
        source = path.read_text(encoding="utf-8")
        match = re.search(r"^struct message_client_join_game_request\n\{\n(.*?)^\};", source, re.S | re.M)
        assert match, path
        fields = [field.strip() for field in re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.S).split(";")]
        fields = [field for field in fields if field]
        assert fields[-1] == "word network_version", (path.name, fields)


def test_host_compares_the_version_it_decoded():
    """the host refuses a request whose decoded version is not its own, which
    is what the harness's reading checks against"""
    source = SERVER.read_text(encoding="utf-8")
    assert "if (join_game_request.network_version != HALO_PORT_NETWORK_VERSION)" in source
    assert "join requires OpenCE network version" in source
    assert "_network_game_packet_class_client_pregame))" in source


def _compile(source, output, extra, cc):
    from tools.test_network_messages import compile_command

    command = compile_command(GAME_BUILD, source, output, extra)
    # (the build's compiler, after a launcher if it has one: ccache in CI)
    if Path(command[0]).name in ("ccache", "sccache"):
        command = command[1:]
    command[0] = cc
    result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
    assert result.returncode == 0, f"{source}: the build failed\n{result.stdout}{result.stderr}"


def _game_ready():
    from tools.test_network_messages import object_command

    if not (ROOT / "build.ninja").is_file():
        return False
    try:
        object_command(GAME_BUILD, GAME_SOURCES[0])
    except (subprocess.CalledProcessError, IndexError, OSError):
        return False
    return True


def test_join_request_round_trip(tmp_path):
    """the request to a host of a version not known encodes whole, and the
    host's pre-check and decoder read back its own network version"""
    from tools.test_network_messages import object_command

    cc = fuzz_support.libfuzzer_compiler()
    if not cc:
        fuzz_support.skip("no clang with the sanitizers' runtimes")
    if not _game_ready():
        fuzz_support.skip(f"the game's units: python3 configure.py first, with the {GAME_BUILD} build")

    objects = []
    for source in GAME_SOURCES:
        output = tmp_path / (Path(source).stem + ".o")
        _compile(source, output, {"flags": SANITIZE}, cc)
        objects.append(str(output))
    # network_messages.c as the build has it, the sizes appended
    messages = "source/networking/network_messages.c"
    words, _ = object_command(GAME_BUILD, messages)
    built = next(word for word in words if word.replace("\\", "/").endswith(messages))
    wrapped = tmp_path / "network_messages_with_sizes.c"
    wrapped.write_text(f'#include "{(ROOT / built).as_posix()}"\n' + SIZES.read_text(encoding="utf-8"))
    output = tmp_path / "network_messages.o"
    _compile(messages, output, {"source": str(wrapped), "flags": SANITIZE}, cc)
    objects.append(str(output))

    target = [word for word in words if word.startswith("--target=")]
    binary = tmp_path / "join_request_version_test"
    result = subprocess.run([cc, *target, *SANITIZE, "-o", str(binary), str(HARNESS), *objects],
                            capture_output=True, text=True, cwd=ROOT)
    assert result.returncode == 0, result.stdout + result.stderr
    environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=0",
                       UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1")
    result = subprocess.run([str(binary)], capture_output=True, text=True, env=environment)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "join_request_version_test: ok" in result.stdout, result.stdout


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q", *sys.argv[1:]]))
