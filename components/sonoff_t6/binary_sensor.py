import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from . import SonoffT6, ns

DEPENDENCIES = ["sonoff_t6"]
TouchBinarySensor = ns.class_("TouchBinarySensor", binary_sensor.BinarySensor)
CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(TouchBinarySensor).extend({
    cv.Required("sonoff_t6_id"): cv.use_id(SonoffT6),
    cv.Required("button_id"): cv.int_range(min=1, max=3),
})


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    parent = await cg.get_variable(config["sonoff_t6_id"])
    cg.add(parent.register_sensor(var, config["button_id"]))
