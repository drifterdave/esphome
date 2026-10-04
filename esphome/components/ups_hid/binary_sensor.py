import esphome.codegen as cg
from esphome.components import binary_sensor
from esphome.components.const import CONF_CONNECTED
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_BATTERY_CHARGING,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PLUG,
    DEVICE_CLASS_PROBLEM,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from esphome.types import ConfigType

from . import CONF_UPS_HID_ID, UpsHid

DEPENDENCIES = ["ups_hid"]

CONF_CHARGING = "charging"
CONF_LOW_BATTERY = "low_battery"
CONF_ONLINE = "online"
CONF_OVERLOAD = "overload"
CONF_REPLACE_BATTERY = "replace_battery"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_UPS_HID_ID): cv.use_id(UpsHid),
        cv.Optional(CONF_ONLINE): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PLUG,
        ),
        cv.Optional(CONF_CHARGING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY_CHARGING,
        ),
        cv.Optional(CONF_LOW_BATTERY): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY,
        ),
        cv.Optional(CONF_REPLACE_BATTERY): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM,
        ),
        cv.Optional(CONF_OVERLOAD): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM,
        ),
        cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_UPS_HID_ID])
    binary_sensors = binary_sensor.sub_binary_sensors(config)
    await binary_sensors(CONF_ONLINE, hub.set_online_binary_sensor)
    await binary_sensors(CONF_CHARGING, hub.set_charging_binary_sensor)
    await binary_sensors(CONF_LOW_BATTERY, hub.set_low_battery_binary_sensor)
    await binary_sensors(CONF_REPLACE_BATTERY, hub.set_replace_battery_binary_sensor)
    await binary_sensors(CONF_OVERLOAD, hub.set_overload_binary_sensor)
    await binary_sensors(CONF_CONNECTED, hub.set_connected_binary_sensor)
