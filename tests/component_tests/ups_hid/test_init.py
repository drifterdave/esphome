"""Tests for the ups_hid configuration."""

from pathlib import Path
import re

from esphome.components import ups_hid

UPS_DATA_CPP = Path(ups_hid.__file__).parent / "ups_data.cpp"


def test_commands_match_cpp_table() -> None:
    """The YAML command names must be exactly the NUT names in the C++ command table, in enum order."""
    source = UPS_DATA_CPP.read_text(encoding="utf-8")
    table = source[source.index("COMMANDS[UPS_COMMAND_COUNT]") :]
    table = table[: table.index("};")]
    cpp_names = re.findall(r'\{"([a-z.]+)",', table)
    assert cpp_names == list(ups_hid.COMMANDS)
    for name, enum_value in ups_hid.COMMANDS.items():
        expected = "UPS_COMMAND_" + name.upper().replace(".", "_")
        assert str(enum_value).endswith(expected)
