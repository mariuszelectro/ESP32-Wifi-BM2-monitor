import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TYPE
from esphome.components import binary_sensor as esphome_binary_sensor

from . import bm2_ble_ns, BM2BLEComponent, CONF_BM2_BLE_ID, validate_binary_sensor_type

BM2BleBinarySensor = bm2_ble_ns.class_("BM2BleBinarySensor", esphome_binary_sensor.BinarySensor, cg.PollingComponent)

CONFIG_SCHEMA = esphome_binary_sensor.binary_sensor_schema(BM2BleBinarySensor).extend({
    cv.GenerateID(): cv.declare_id(BM2BleBinarySensor),
    cv.Required(CONF_BM2_BLE_ID): cv.use_id(BM2BLEComponent),
    cv.Required(CONF_TYPE): validate_binary_sensor_type,
}).extend(cv.polling_component_schema('60s'))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await esphome_binary_sensor.register_binary_sensor(var, config)
    parent = await cg.get_variable(config[CONF_BM2_BLE_ID])
    cg.add(parent.add_entity(config[CONF_TYPE], var))
