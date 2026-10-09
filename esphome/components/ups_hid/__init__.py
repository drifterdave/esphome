from esphome import automation
import esphome.codegen as cg
from esphome.components import usb_host
import esphome.config_validation as cv
from esphome.const import CONF_COMMAND, CONF_ID, CONF_UPDATE_INTERVAL
from esphome.core import ID, TimePeriod
from esphome.cpp_generator import MockObj
from esphome.types import ConfigType, TemplateArgsType

CODEOWNERS = ["@DrifterDave"]
DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["usb_host"]
MULTI_CONF = True

CONF_UPS_HID_ID = "ups_hid_id"
CONF_SHUTDOWN_DELAY = "shutdown_delay"

# Default device: APC Back-UPS family, including the Back-UPS ES 600M1 (BE600M1).
# For CyberPower set vid 0x0764 and pid 0x0501 (most desktop models) or 0x0601.
APC_VENDOR_ID = 0x051D
APC_BACK_UPS_PRODUCT_ID = 0x0002

ups_hid_ns = cg.esphome_ns.namespace("ups_hid")
UpsHid = ups_hid_ns.class_("UpsHid", usb_host.USBClient)
UpsCommand = ups_hid_ns.enum("UpsCommand")
UpsCommandAction = ups_hid_ns.class_("UpsCommandAction", automation.Action)

COMMANDS = {
    "beeper.enable": UpsCommand.UPS_COMMAND_BEEPER_ENABLE,
    "beeper.disable": UpsCommand.UPS_COMMAND_BEEPER_DISABLE,
    "beeper.mute": UpsCommand.UPS_COMMAND_BEEPER_MUTE,
    "test.battery.start.quick": UpsCommand.UPS_COMMAND_TEST_BATTERY_START_QUICK,
    "test.battery.start.deep": UpsCommand.UPS_COMMAND_TEST_BATTERY_START_DEEP,
    "test.battery.stop": UpsCommand.UPS_COMMAND_TEST_BATTERY_STOP,
    "test.panel.start": UpsCommand.UPS_COMMAND_TEST_PANEL_START,
    "test.panel.stop": UpsCommand.UPS_COMMAND_TEST_PANEL_STOP,
    "load.off.delay": UpsCommand.UPS_COMMAND_LOAD_OFF_DELAY,
    "shutdown.reboot": UpsCommand.UPS_COMMAND_SHUTDOWN_REBOOT,
    "shutdown.stop": UpsCommand.UPS_COMMAND_SHUTDOWN_STOP,
}

CONFIG_SCHEMA = usb_host.usb_device_schema(
    UpsHid, APC_VENDOR_ID, APC_BACK_UPS_PRODUCT_ID
).extend(
    {
        cv.Optional(CONF_UPDATE_INTERVAL, default="10s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=TimePeriod(seconds=1)),
        ),
        cv.Optional(CONF_SHUTDOWN_DELAY, default="20s"): cv.All(
            cv.positive_time_period_seconds,
            cv.Range(min=TimePeriod(seconds=1), max=TimePeriod(seconds=32767)),
        ),
    }
)


async def to_code(config: ConfigType) -> None:
    # register_component() forwards update_interval to set_update_interval()
    var = await usb_host.register_usb_client(config)
    cg.add(var.set_shutdown_delay(config[CONF_SHUTDOWN_DELAY]))


# A plain Action class rather than register_apply_action, so this also works on ESPHome 2026.9.
@automation.register_action(
    "ups_hid.command",
    UpsCommandAction,
    cv.maybe_simple_value(
        {
            cv.GenerateID(CONF_ID): cv.use_id(UpsHid),
            cv.Required(CONF_COMMAND): cv.templatable(cv.enum(COMMANDS, lower=True)),
        },
        key=CONF_COMMAND,
    ),
    synchronous=True,
)
async def ups_hid_command_to_code(
    config: ConfigType,
    action_id: ID,
    template_arg: cg.TemplateArguments,
    args: TemplateArgsType,
) -> MockObj:
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    template_ = await cg.templatable(config[CONF_COMMAND], args, UpsCommand)
    cg.add(var.set_command(template_))
    return var
