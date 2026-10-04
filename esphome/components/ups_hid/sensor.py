import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_BATTERY_LEVEL,
    CONF_BATTERY_VOLTAGE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_VOLTAGE,
    ICON_GAUGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
    UNIT_SECOND,
    UNIT_VOLT,
)
from esphome.types import ConfigType

from . import CONF_UPS_HID_ID, UpsHid

DEPENDENCIES = ["ups_hid"]

CONF_INPUT_VOLTAGE = "input_voltage"
CONF_LOAD = "load"
CONF_OUTPUT_VOLTAGE = "output_voltage"
CONF_RUNTIME = "runtime"


def _voltage_schema() -> cv.Schema:
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_UPS_HID_ID): cv.use_id(UpsHid),
        cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_BATTERY_VOLTAGE): _voltage_schema(),
        cv.Optional(CONF_RUNTIME): sensor.sensor_schema(
            unit_of_measurement=UNIT_SECOND,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_DURATION,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_INPUT_VOLTAGE): _voltage_schema(),
        cv.Optional(CONF_OUTPUT_VOLTAGE): _voltage_schema(),
        cv.Optional(CONF_LOAD): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            icon=ICON_GAUGE,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_UPS_HID_ID])
    sensors = sensor.sub_sensors(config)
    await sensors(CONF_BATTERY_LEVEL, hub.set_battery_level_sensor)
    await sensors(CONF_BATTERY_VOLTAGE, hub.set_battery_voltage_sensor)
    await sensors(CONF_RUNTIME, hub.set_runtime_sensor)
    await sensors(CONF_INPUT_VOLTAGE, hub.set_input_voltage_sensor)
    await sensors(CONF_OUTPUT_VOLTAGE, hub.set_output_voltage_sensor)
    await sensors(CONF_LOAD, hub.set_load_sensor)
