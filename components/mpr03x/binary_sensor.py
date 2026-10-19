import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID, CONF_CHANNEL
from . import MPR03X, ns

DEPENDENCIES = ["mpr03x"]
MPR03XBinarySensor = ns.class_("MPR03XBinarySensor", binary_sensor.BinarySensor)
CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(MPR03XBinarySensor).extend({
    cv.Required("mpr03x_id"): cv.use_id(MPR03X),
    cv.Required(CONF_CHANNEL): cv.int_range(min=0, max=2),
})


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    parent = await cg.get_variable(config["mpr03x_id"])
    cg.add(parent.register_sensor(var, config[CONF_CHANNEL]))
