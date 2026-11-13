#!/usr/bin/env python3
"""Find the menus' buttons that lead nowhere, from the game's own dump of
them (HALO_MENU_DUMP=1: menu_tags.c's menu_dump, one "menu-dump:" log line
a widget, after the menus are put into ui.map: the Xbox's widgets as this
fork changes them and port/assets/menus' own).

    python3 tools/menu_audit.py <log with the menu-dump lines>

From the menus' root (the main menu) it follows what each screen opens. On
each screen it walks the lists' rows (the children of a widget whose focus
moves through them), as the engine sends a press: from the screen down
the focus's path, the first widget with a handler for the event takes it
(ui_widget.c widget_instance_process_one_event_recursive: a parent
handles an event before its focused child). A row's A (or Start) does
nothing when no widget on its path has a handler, or the one that takes it
only runs a function that does nothing: "unwired", or a port function
menu_functions.c never answers (its dispatcher returns TRUE for those).
A row that is a setting (it, or a widget above it, takes left or right) is
not counted: its A has nothing to do.

It also lists open handlers with no widget, and screens with no rows.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# handler flags (menu_tags.c _event_handler_*_bit)
CLOSE_CURRENT, CLOSE_OTHER, CLOSE_ALL, OPEN, RELOAD_SELF, RELOAD_OTHER, GIVE_FOCUS, RUN, REPLACE, BACK, SCRIPT, \
    BRANCH = range(12)
# widget flags (menu_tags.c widget_flag_names)
UP_DOWN_CHILDREN, LEFT_RIGHT_CHILDREN, UP_DOWN_ITEMS, LEFT_RIGHT_ITEMS = 3, 4, 5, 6
SPINNER = 2
NOTHING_FUNCTIONS = {"", "NULL", "unused", "unwired"}
ACTIVATE = ("a", "start")
SETTING_EVENTS = ("left", "right", "stick_left", "stick_right")
# (B is not here: a screen with no handler for it goes back, ui_widget.c)
EMITS = {"mouse emit x event": ("x",), "emit custom activation event": ("custom_activation",)}


def handled_port_functions():
    """the port function names menu_functions.c (and the files beside it)
    answer: by name, or by the three it numbers"""
    names = {"port quit game", "port setting load", "port setting save"}
    prefixes = set()
    for path in list((ROOT / "port/linux/game").glob("*.c")) + list((ROOT / "port/linux/game").glob("*.inl")):
        text = path.read_text(encoding="utf-8", errors="replace")
        names |= set(re.findall(r'strcmp\(\s*\w+\s*,\s*"([^"]+)"\s*\)', text))
        prefixes |= set(re.findall(r'strncmp\(\s*\w+\s*,\s*"([^"]+)"\s*,', text))
    return names, prefixes


def port_function_names():
    text = (ROOT / "port/linux/game/menu_tags.c").read_text(encoding="utf-8", errors="replace")
    body = re.search(r"port_function_names\[\]\s*=\s*\{(.*?)\n\};", text, re.S).group(1)
    return set(re.findall(r'"([^"]*)"', re.sub(r"/\*.*?\*/", "", body, flags=re.S)))


def read_dump(path):
    widgets, root, widget = {}, None, None
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        at = line.find("menu-dump: ")
        if at < 0:
            continue
        line = line[at + len("menu-dump: "):]
        if line.startswith("root "):
            root = line[5:].strip()
            continue
        if line.startswith("+|") and widget:
            # (the rest of a long widget's line)
            parts = line[2:].split("|")
        elif line.startswith("W|"):
            parts = line.split("|")
            widget = {"name": parts[1], "type": int(parts[2]), "flags": int(parts[3], 16), "description": parts[4],
                      "events": [], "inputs": [], "children": [], "conditionals": []}
            widgets[widget["name"]] = widget
            parts = parts[5:]
        else:
            continue
        for part in parts:
            kind, _, value = part.partition(":")
            if kind == "E":
                event, flags, function, target = value.split(":", 3)
                widget["events"].append((event, int(flags, 16), function, target))
            elif kind == "I":
                widget["inputs"].append(value)
            elif kind == "C":
                widget["children"].append(value)
            elif kind == "K":
                widget["conditionals"].append(value)
    return root, widgets


def main():
    show_map = "--map" in sys.argv
    rows_map = []
    root, widgets = read_dump([a for a in sys.argv[1:] if not a.startswith("--")][0])
    handled, prefixes = handled_port_functions()
    ours = port_function_names()

    def does_nothing(function):
        if function in NOTHING_FUNCTIONS:
            return True
        if function in ours and function not in handled and not any(function.startswith(p) for p in prefixes):
            return True
        return False

    def effect(handler):
        """what a handler does, or None"""
        event, flags, function, target = handler
        what = []
        if flags & (1 << RUN) and not does_nothing(function):
            what.append("runs " + function)
        for bit, word in ((OPEN, "opens"), (REPLACE, "replaces itself with")):
            if flags & (1 << bit):
                what.append(f"{word} {target}" if target else f"{word} NOTHING")
        for bit, word in ((CLOSE_CURRENT, "closes"), (CLOSE_OTHER, "closes another"), (CLOSE_ALL, "closes all"),
                          (BACK, "goes back"), (RELOAD_SELF, "reloads"), (RELOAD_OTHER, "reloads another"),
                          (GIVE_FOCUS, "gives focus"), (SCRIPT, "runs a script")):
            if flags & (1 << bit):
                what.append(word)
        return ", ".join(what) or None

    problems = []
    # screens: the root, then what is opened
    # (and, with --all, every screen: those the game opens itself, the
    # pause menu and the Online Games and Delta screens among them)
    extra = sorted(n for n in widgets if "--all" in sys.argv and re.search(r"(screen|wrapper|menu)$", n))
    screens, queue, seen = [], [root] + extra, set()
    while queue:
        name = queue.pop(0)
        if not name or name in seen or name not in widgets:
            if name and name not in widgets and name not in seen:
                problems.append(f"MISSING  {name}: opened, but there is no such widget")
                seen.add(name)
            continue
        seen.add(name)
        screens.append(name)

        def walk(widget_name, path):
            widget = widgets.get(widget_name)
            if not widget:
                return
            path = path + [widget]
            for event, flags, function, target in widget["events"]:
                if flags & ((1 << OPEN) | (1 << REPLACE)):
                    if not target:
                        problems.append(f"NO TARGET  {widget_name} [{event}] opens no widget ({name})")
                    else:
                        queue.append(target)
            for conditional in widget["conditionals"]:
                queue.append(conditional)
            rows = widget["type"] != SPINNER and widget["flags"] & (
                (1 << UP_DOWN_CHILDREN) | (1 << LEFT_RIGHT_CHILDREN) | (1 << UP_DOWN_ITEMS) | (1 << LEFT_RIGHT_ITEMS))
            for child in widget["children"]:
                if rows:
                    check_row(child, path + [widgets.get(child)] if widgets.get(child) else path)
                walk(child, path)

        def check_row(row_name, path):
            path = [w for w in path if w]
            if any(e[0] in SETTING_EVENTS for w in path for e in w["events"]):
                return
            for event in ACTIVATE:
                taken = None
                for widget in path:
                    handlers = [h for h in widget["events"] if h[0] == event]
                    if handlers:
                        taken = (widget, handlers)
                        break
                if not taken:
                    # (a row with no handler of its own and nothing above it:
                    # a label, when the row has no left_mouse either)
                    row = path[-1]
                    if row["events"] and event == "a":
                        problems.append(f"NOTHING  {row_name}: A has no handler on its path ({name})")
                    return
                widget, handlers = taken
                # (a button that sends another event, as the PC version's
                # button bars do: B, X, or the screen's custom activation,
                # which then goes down the same path; menu_functions.c)
                for handler in handlers:
                    emitted = EMITS.get(handler[2])
                    if not emitted:
                        continue
                    answered = None
                    for other in path:
                        found = [h for h in other["events"] if h[0] in emitted]
                        if found:
                            answered = found
                            break
                    if not answered or not any(effect(h) for h in answered):
                        problems.append(f"NOTHING  {row_name}: {event.upper()} sends {'/'.join(emitted)}, which "
                                        f"nothing on its path answers ({name})")
                if show_map and event == "a":
                    rows_map.append(f"{name} :: {row_name} -> " + "; ".join(effect(h) or "nothing" for h in handlers)
                                    + ("" if widget["name"] == row_name else f" (by {widget['name']})"))
                if not any(effect(h) for h in handlers):
                    functions = ", ".join(sorted({h[2] or "-" for h in handlers}))
                    problems.append(f"NOTHING  {row_name}: {event.upper()} is taken by {widget['name']}, "
                                    f"which only runs {functions} ({name})")
                return

        walk(name, [])

    for row in rows_map:
        print(row)
    for problem in sorted(set(problems)):
        print(problem)
    print(f"\n{len(screens)} screens reached from {root}, {len(widgets)} widgets", file=sys.stderr)


if __name__ == "__main__":
    main()
