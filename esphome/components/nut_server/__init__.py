import re

import esphome.codegen as cg
from esphome.components import socket
from esphome.components.const import CONF_DESCRIPTION
from esphome.components.ups_hid import CONF_UPS_HID_ID, UpsHid
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PASSWORD, CONF_PORT, CONF_USERNAME
from esphome.types import ConfigType

try:
    from esphome.components.const import CONF_ALLOWED_IPS
except ImportError:  # ESPHome 2026.9 and older
    CONF_ALLOWED_IPS = "allowed_ips"

CODEOWNERS = ["@DrifterDave"]
DEPENDENCIES = ["network", "ups_hid"]
AUTO_LOAD = ["socket"]

CONF_MAX_CLIENTS = "max_clients"
CONF_UPS_NAME = "ups_name"

nut_server_ns = cg.esphome_ns.namespace("nut_server")
NutServer = nut_server_ns.class_("NutServer", cg.Component)

_UPS_NAME_RE = re.compile(r"[A-Za-z0-9_.-]+")
# Credentials are stored in fixed buffers in the session (NUT_CREDENTIAL_MAX - 1)
_CREDENTIAL = cv.All(cv.string_strict, cv.Length(min=1, max=63))


def validate_ups_name(value: str) -> str:
    value = cv.string_strict(value)
    if not _UPS_NAME_RE.fullmatch(value):
        raise cv.Invalid("UPS name may only contain letters, digits, '_', '.' and '-'")
    return value


def _consume_sockets(config: ConfigType) -> ConfigType:
    socket.consume_sockets(config[CONF_MAX_CLIENTS], "nut_server")(config)
    socket.consume_sockets(1, "nut_server", socket.SocketType.TCP_LISTEN)(config)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(NutServer),
            cv.GenerateID(CONF_UPS_HID_ID): cv.use_id(UpsHid),
            cv.Optional(CONF_UPS_NAME, default="ups"): validate_ups_name,
            cv.Optional(CONF_DESCRIPTION, default=""): cv.string,
            cv.Optional(CONF_PORT, default=3493): cv.port,
            cv.Optional(CONF_MAX_CLIENTS, default=4): cv.int_range(min=1, max=8),
            cv.Inclusive(CONF_USERNAME, "credentials"): _CREDENTIAL,
            cv.Inclusive(CONF_PASSWORD, "credentials"): cv.sensitive(_CREDENTIAL),
            cv.Optional(CONF_ALLOWED_IPS): cv.All(
                cv.ensure_list(cv.ipv4network), cv.Length(min=1, max=32)
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _consume_sockets,
)


async def to_code(config: ConfigType) -> None:
    ups = await cg.get_variable(config[CONF_UPS_HID_ID])
    var = cg.new_Pvariable(config[CONF_ID], ups)
    await cg.register_component(var, config)
    cg.add_define("NUT_SERVER_MAX_CLIENTS", config[CONF_MAX_CLIENTS])
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_ups_name(config[CONF_UPS_NAME]))
    cg.add(var.set_description(config[CONF_DESCRIPTION]))
    if (username := config.get(CONF_USERNAME)) is not None:
        cg.add(var.set_credentials(username, config[CONF_PASSWORD]))
    # Own allow list rather than socket.add_ipv4_allow(), which ESPHome 2026.9 does not have
    if networks := config.get(CONF_ALLOWED_IPS):
        cg.add_define("NUT_SERVER_ALLOWED_IPS_COUNT", len(networks))
        for net in networks:
            # s_addr layout on the little-endian targets
            addr = int.from_bytes(net.network_address.packed, "little")
            mask = int.from_bytes(net.netmask.packed, "little")
            cg.add(var.add_allowed_network(addr, mask))
