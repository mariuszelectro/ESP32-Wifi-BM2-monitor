import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components import ble_client

AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor"]
DEPENDENCIES = ["esp32_ble_tracker", "ble_client"]
CODEOWNERS = ["@andrewbackway"]

bm2_ble_ns = cg.esphome_ns.namespace("bm2_ble")
BM2BLEComponent = bm2_ble_ns.class_("BM2BLEComponent", cg.Component, ble_client.BLEClientNode)

CONF_BM2_BLE_ID = "bm2_ble_id"

SENSOR_TYPES = [
    "voltage",
    "battery",
]

TEXT_SENSOR_TYPES = [
    "status",
]

BINARY_SENSOR_TYPES = [
    "charging",
    "weak_battery",
]


def validate_sensor_type(value):
    value = cv.string(value)
    if value.upper() not in [t.upper() for t in SENSOR_TYPES]:
        raise cv.Invalid(f"Unknown sensor type: {value}. Must be one of {SENSOR_TYPES}")
    return value.lower()


def validate_text_sensor_type(value):
    value = cv.string(value)
    if value.upper() not in [t.upper() for t in TEXT_SENSOR_TYPES]:
        raise cv.Invalid(f"Unknown text sensor type: {value}. Must be one of {TEXT_SENSOR_TYPES}")
    return value.lower()


def validate_binary_sensor_type(value):
    value = cv.string(value)
    if value.upper() not in [t.upper() for t in BINARY_SENSOR_TYPES]:
        raise cv.Invalid(f"Unknown binary sensor type: {value}. Must be one of {BINARY_SENSOR_TYPES}")
    return value.lower()


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BM2BLEComponent),
        cv.Required("ble_client_id"): cv.use_id(ble_client.BLEClient),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    ble = await cg.get_variable(config["ble_client_id"])
    cg.add(ble.register_ble_node(var))
