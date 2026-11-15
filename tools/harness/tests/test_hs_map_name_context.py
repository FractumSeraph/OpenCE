"""map_name keeps a map initializer out of the console's unrestricted path."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, enum_with, function, mutated, read, run


def generated(old_guard=False):
    runtime = read("source/hs/hs_runtime.c")
    code = "\n".join(function(runtime, name) for name in (
        "hs_runtime_port_executing_console_command", "hs_runtime_port_executing_script_type"))
    evaluator = function(read("source/hs/hs.c"), "main_set_map_name_evaluate")
    if old_guard:
        # (this fork's evaluator: its guard from the console's call to the
        # refusal, whatever lies between: a pack's menus and Stubbs mode's levels)
        first = evaluator.index("if (hs_runtime_port_executing_console_command())")
        last = evaluator.index('error(_error_silent, "map_name requires')
        last = evaluator.index('");', last) + 3
        evaluator = mutated(evaluator, evaluator[first:last],
            'if (hs_runtime_port_executing_script_type() != NONE)\n'
            '\t\t\tmain_script_map_name(xbox_pointer(arguments->value));\n\t\telse\n'
            '\t\t\tmain_set_map_name(xbox_pointer(arguments->value));')
    return (("types.inc", enum_with(runtime, "_hs_thread_type_script =") + "\n" +
             enum_with(read("source/hs/hs_scenario_definitions.h"), "_hs_script_startup =")),
            ("under_test.inc", code + "\n" + evaluator))


@pytest.mark.parametrize("case", ["startup", "dormant", "continuous", "global", "global-static",
                                  "console", "console-static", "no-thread", "uninitialized", "arguments-pending"])
def test_origin(case):
    status, output = run(build("hs_map_name_context", generated()), case)
    assert status == 0, output


def test_old_guard_exposes_global_initializer():
    status, output = run(build("hs_map_name_context", generated(True)), "global")
    assert status == 1, output
