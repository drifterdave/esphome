import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_MODEL, CONF_STATUS, ENTITY_CATEGORY_DIAGNOSTIC
from esphome.types import ConfigType

from . import CONF_UPS_HID_ID, UpsHid

DEPENDENCIES = ["ups_hid"]

CONF_SERIAL = "serial"
CONF_TEST_RESULT = "test_result"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_UPS_HID_ID): cv.use_id(UpsHid),
        cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_MODEL): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_SERIAL): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_TEST_RESULT): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_UPS_HID_ID])
    # Explicit loop instead of text_sensor.sub_text_sensors(), which ESPHome 2026.9 does not have
    for key, setter in (
        (CONF_STATUS, hub.set_status_text_sensor),
        (CONF_MODEL, hub.set_model_text_sensor),
        (CONF_SERIAL, hub.set_serial_text_sensor),
        (CONF_TEST_RESULT, hub.set_test_result_text_sensor),
    ):
        if (conf := config.get(key)) is not None:
            cg.add(setter(await text_sensor.new_text_sensor(conf)))
