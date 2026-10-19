"""MPR031/MPR032 touch controller with a private software I2C bus."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID, CONF_SCL, CONF_SDA, CONF_ADDRESS

AUTO_LOAD = ["binary_sensor"]
MULTI_CONF = True
ns = cg.esphome_ns.namespace("mpr03x")
MPR03X = ns.class_("MPR03X", cg.PollingComponent)


def validate_config(config):
    for key in (CONF_SCL, CONF_SDA):
        if config[key].get("inverted", False):
            raise cv.Invalid(f"{key} cannot be inverted on an I2C bus")
    if config[CONF_SCL]["number"] == config[CONF_SDA]["number"]:
        raise cv.Invalid("scl and sda must use different pins")
    if config["release_threshold"] >= config["touch_threshold"]:
        raise cv.Invalid("release_threshold must be lower than touch_threshold")
    if config["update_interval"].total_milliseconds < 10:
        raise cv.Invalid("update_interval must be at least 10ms for the software bus")
    return config


# Runtime switches between input (released) and output-low. No open-drain
# flag or hardware I2C support is required from the platform.
CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(MPR03X),
        cv.Optional(CONF_SCL, default="P0"): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_SDA, default="P1"): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_ADDRESS, default=0x4A): cv.one_of(0x4A, 0x4B, int=True),
        cv.Optional("touch_threshold", default=12): cv.int_range(min=1, max=255),
        cv.Optional("release_threshold", default=6): cv.int_range(min=0, max=254),
        cv.Optional("afe_configuration", default=0x10): cv.int_range(min=0, max=255),
        cv.Optional("filter_configuration", default=0x24): cv.int_range(min=0, max=255),
        cv.Optional("half_period_us", default=10): cv.int_range(min=5, max=100),
        cv.Optional("internal_pullups", default=False): cv.boolean,
        cv.Optional("diagnostics", default=False): cv.boolean,
    }).extend(cv.polling_component_schema("20ms")),
    validate_config,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    for key in (CONF_SCL, CONF_SDA):
        pin = await cg.gpio_pin_expression(config[key])
        cg.add(getattr(var, f"set_{key}_pin")(pin))
    for key in (CONF_ADDRESS, "touch_threshold", "release_threshold",
                "afe_configuration", "filter_configuration", "half_period_us",
                "internal_pullups", "diagnostics"):
        cg.add(getattr(var, f"set_{key}")(config[key]))
