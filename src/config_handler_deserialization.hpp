/**
 * @file config_handler_deserialization.hpp
 * @brief Private runtime-configuration JSON deserialization boundary.
 */

#ifndef CONFIG_HANDLER_DESERIALIZATION_HPP
#define CONFIG_HANDLER_DESERIALIZATION_HPP

#include "config_types.hpp"
#include "json.hpp"

#include <array>

namespace config_handler_deserialization
{
void default_band_gpio_config(
    std::array<BandGPIOConfig, HAM_BAND_COUNT> &band_gpio);
void deserialize_json_to_runtime_config(
    const nlohmann::json &source,
    ArgParserConfig &target);
} // namespace config_handler_deserialization

#endif // CONFIG_HANDLER_DESERIALIZATION_HPP
