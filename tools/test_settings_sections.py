"""The settings table (port/linux/src/port_config.c) keeps each section's
settings together: config.toml is written in the table's order, a section's
header before its first setting, so a section that came back later in the
table would be written as a second table of the same name, which TOML (and
the game reading the file back) refuses."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def test_each_section_is_one_run():
    text = (ROOT / "port" / "linux" / "src" / "port_config.c").read_text(encoding="utf-8")
    table = text[text.index("static const struct config_setting config_settings"):]
    table = table[:table.index("\n};")]
    sections = [match.group(1) for match in re.finditer(r'^\t\{ "([a-z_0-9]+)\.', table, re.M)]
    assert sections, "no settings found"
    runs = [section for index, section in enumerate(sections) if index == 0 or sections[index - 1] != section]
    repeated = sorted({section for section in runs if runs.count(section) > 1})
    assert not repeated, f"sections split in the settings table: {repeated}"
