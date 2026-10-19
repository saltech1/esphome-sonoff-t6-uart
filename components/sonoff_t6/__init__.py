"""SONOFF T6 UART touch controller, based on stock firmware and captures."""
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import uart
from esphome.const import CONF_ID, CONF_UART_ID

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor"]
MULTI_CONF = True
ns = cg.esphome_ns.namespace("sonoff_t6")
SonoffT6 = ns.class_("SonoffT6", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SonoffT6),
    cv.Optional("release_timeout", default="30s"): cv.All(
        cv.positive_time_period_milliseconds,
        cv.Range(min=cv.TimePeriod(milliseconds=1000)),
    ),
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)

_VALIDATE_UART = uart.final_validate_device_schema(
    "sonoff_t6", baud_rate=115200, require_rx=True,
    data_bits=8, parity="NONE", stop_bits=1,
)


def validate_uart(config):
    _VALIDATE_UART(config)
    full = fv.full_config.get()
    bus = full.get_config_for_path(full.get_path_for_id(config[CONF_UART_ID])[:-1])
    if bus.get("debug", {}).get("dummy_receiver", False):
        raise cv.Invalid("Set uart.debug.dummy_receiver: false; sonoff_t6 must receive the bytes")
    return config


FINAL_VALIDATE_SCHEMA = validate_uart


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_release_timeout(config["release_timeout"]))
