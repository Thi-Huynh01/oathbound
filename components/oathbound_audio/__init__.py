import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID

DEPENDENCIES = ["network"]

oathbound_audio_ns = cg.esphome_ns.namespace("oathbound_audio")
OathboundAudio = oathbound_audio_ns.class_(
    "OathboundAudio",
    cg.Component
)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(OathboundAudio),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
