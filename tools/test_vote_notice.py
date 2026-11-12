"""OpenCE votes on a Chupathingy host (network_distributed.c). ChupathingyCE's
hosts drop every vote message unread and tell a client's player, once a game,
that the host has no votes (distributed_vote_dropped). Port (this fork): the
fork plays OpenCE's votes (the vote kick, 78 and 79, network_votekick.c; the
map vote, 94 and 95, network_map_vote.c) and reads OpenCE's client build (92),
so none of these kinds reaches distributed_unknown_kind and its hosts never
send that notice; the notice's code is kept, and still told once a machine,
for a kind of theirs the fork does not know (build/UPDATING.md, "Votes")."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DISTRIBUTED_C = ROOT / "port/linux/game/network_distributed.c"
CHAT_C = ROOT / "port/linux/game/chat.c"


def _body(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}\n", start)]


def test_votes_played_never_noted():
    text = DISTRIBUTED_C.read_text(encoding="utf-8")
    header = (ROOT / "port/linux/game/network_distributed.h").read_text(encoding="utf-8")
    # (the kinds are OpenCE's numbers, and this build knows each: none is "unknown")
    for name, number in (("votekick", 78), ("votekick_status", 79), ("client_build", 92),
                         ("map_vote", 94), ("map_vote_choice", 95)):
        assert f"_distributed_message_{name} = {number}," in header, name
    known = _body(text, "static boolean distributed_kind_known(")
    assert "type < NUMBER_OF_DISTRIBUTED_MESSAGES" in known
    # (port, this fork: 42's checkpoint and MCC kinds and 43's riding map follow it)
    assert "_distributed_message_map_vote_choice + 1 == _distributed_message_checkpoint" in text
    assert "_distributed_message_vehicle_riding_map + 1 == NUMBER_OF_DISTRIBUTED_MESSAGES" in text
    assert "_distributed_message_votekick == _distributed_message_coop_device_states + 1" in text
    handle = text[text.index("void network_distributed_handle_message("):]
    unknown_call = handle.index("if (!distributed_kind_known(header.type))\n\t{\n\t\tdistributed_unknown_kind(machine_index, header.type);")
    # (the map vote's messages are network_map_vote.c's, taken before the kind check)
    assert handle.index("if (network_map_vote_handles_message(message, size))") < unknown_call
    # (the vote kick's are played, after it)
    assert handle.index("network_votekick_handle_request(machine_index, entries, distributed_handling_stream_message);") > unknown_call
    assert handle.index("network_votekick_handle_status(entries);") > unknown_call
    # ChupathingyCE's notice: still once a machine a game, reached only from a kind this build
    # does not know (so never for a vote here)
    unknown = _body(text, "static void distributed_unknown_kind(")
    votes = unknown.index("type == NETWORK_RULES_VOTE_REQUEST_KIND || type == NETWORK_RULES_VOTE_STATUS_KIND")
    vote_block = unknown[votes:unknown.index("\t\treturn;\n\t}", votes)]
    assert "machine_index != NONE" in vote_block
    assert "distributed_vote_dropped(machine_index, type);" in vote_block
    assert text.count("distributed_vote_dropped(machine_index, type);") == 1
    assert text.count("distributed_unknown_kind(machine_index, header.type);") == 1
    dropped = _body(text, "static void distributed_vote_dropped(")
    assert "distributed_vote_machines_told[machine_index >> 3] |=" in dropped
    assert "chat_host_notice_to_machine((short)machine_index, \"Voting isn't supported on this server; ask a moderator.\");" in dropped
    assert "csmemset(distributed_vote_machines_told, 0, sizeof(distributed_vote_machines_told));" in text


def test_notice_only_to_opence_machines():
    text = CHAT_C.read_text(encoding="utf-8")
    notice = text[text.index("int chat_host_notice_to_machine(short machine, char const *text)\n{"):]
    notice = notice[:notice.index("\n}\n")]
    assert "bridge_hosting()" in notice and "bridge_machine(machine)" in notice
    assert "DELTA_CHAT_BRIDGE_ALLOWED" in notice
