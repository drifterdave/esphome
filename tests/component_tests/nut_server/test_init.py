"""Tests for the nut_server configuration helpers."""

import pytest

from esphome import config_validation as cv
from esphome.components import socket
from esphome.components.nut_server import (
    CONF_MAX_CLIENTS,
    _consume_sockets,
    validate_ups_name,
)


@pytest.mark.parametrize("name", ["ups", "apc-600", "rack.ups_1"])
def test_validate_ups_name_accepts(name: str) -> None:
    assert validate_ups_name(name) == name


@pytest.mark.parametrize("name", ["", "my ups", "ups@home", "ups/1"])
def test_validate_ups_name_rejects(name: str) -> None:
    with pytest.raises(cv.Invalid):
        validate_ups_name(name)


def test_consume_sockets_counts_clients_and_listener() -> None:
    config = {CONF_MAX_CLIENTS: 3}
    assert _consume_sockets(config) is config
    counts = socket.get_socket_counts()
    assert counts.tcp == 3
    assert counts.tcp_listen == 1
    assert "nut_server=3" in counts.tcp_details
