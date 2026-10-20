/**
 * @file config_handler_serialization.hpp
 * @brief Private runtime-configuration JSON serialization boundary.
 */

#ifndef CONFIG_HANDLER_SERIALIZATION_HPP
#define CONFIG_HANDLER_SERIALIZATION_HPP

#include "config_types.hpp"
#include "json.hpp"

#include <array>
#include <string>
#include <utility>

namespace config_handler_serialization
{
using BandJsonKeys =
    std::array<std::pair<HamBand, const char *>, HAM_BAND_COUNT>;

const char *config_serialization_mode_name(ModeType mode) noexcept;
std::string config_serialization_trim(const std::string &value);
int config_serialization_integer(
    const nlohmann::json &source,
    const std::string &context,
    int base = 10);
std::string config_serialization_si5351_i2c_address(int address);
int config_serialization_gpio_transmit_pin(int gpio) noexcept;
const BandJsonKeys &band_json_keys() noexcept;
nlohmann::json public_config_from_internal_json(
    const nlohmann::json &source);

void serialize_runtime_config_to_json(
    const ArgParserConfig &source,
    nlohmann::json &target);
void apply_public_config_to_internal_json(
    const nlohmann::json &public_json,
    nlohmann::json &internal_json);
} // namespace config_handler_serialization

#endif // CONFIG_HANDLER_SERIALIZATION_HPP
