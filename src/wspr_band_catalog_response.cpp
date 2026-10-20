/**
 * @file wspr_band_catalog_response.cpp
 * @brief Implements read-only WSPR band catalog WebSocket serialization.
 */

#include "wspr_band_catalog_response.hpp"

#include "band_lookup.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "json.hpp"

namespace
{
    std::int64_t integral_json_frequency_hz(
        double frequency_hz,
        const char *field_name)
    {
        if (!std::isfinite(frequency_hz) ||
            frequency_hz < 0.0 ||
            std::trunc(frequency_hz) != frequency_hz ||
            frequency_hz >=
                static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        {
            throw std::runtime_error(
                std::string(field_name) +
                " must be a non-negative safely representable integral Hz value.");
        }

        return static_cast<std::int64_t>(frequency_hz);
    }
}

std::string build_wspr_band_catalog_response_json(
    const BandLookup &lookup,
    double audio_offset_hz,
    const std::string &frequency_profile,
    const WsprBandPreferences &band_preferences)
{
    using json = nlohmann::json;

    const std::int64_t offset_hz =
        integral_json_frequency_hz(audio_offset_hz, "audio_offset_hz");
    json response = {
        {"command", "wspr_band_catalog"},
        {"status", "ok"},
        {"audio_offset_hz", offset_hz},
        {"frequency_profile", frequency_profile},
        {"band_preferences", json::object()},
        {"bands", json::array()},
        {"presets", json::array()},
    };
    for (const auto &[band, preference] : band_preferences)
    {
        if (const auto *preset = std::get_if<std::string>(&preference))
            response["band_preferences"][band] = *preset;
        else
            response["band_preferences"][band] =
                std::get<std::uint64_t>(preference);
    }

    for (const WsprBandCatalogEntry &band :
         lookup.canonical_wspr_band_catalog(frequency_profile, band_preferences))
    {
        if (band.dial_frequency_hz >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            throw std::runtime_error(
                "dial_frequency_hz must be a safely representable integral Hz value.");
        }

        const std::int64_t dial_frequency_hz =
            static_cast<std::int64_t>(band.dial_frequency_hz);
        if ((offset_hz > 0 &&
             dial_frequency_hz > std::numeric_limits<std::int64_t>::max() - offset_hz) ||
            (offset_hz < 0 &&
             dial_frequency_hz < std::numeric_limits<std::int64_t>::min() - offset_hz))
        {
            throw std::runtime_error(
                "tone_frequency_hz is outside the supported integral JSON range.");
        }

        response["bands"].push_back({
            {"band", band.band},
            {"dial_frequency_hz", dial_frequency_hz},
            {"tone_frequency_hz", dial_frequency_hz + offset_hz},
            {"resolution_source",
             wspr_band_resolution_source_name(band.resolution_source)},
            {"preset", band.preset ? json(*band.preset) : json(nullptr)},
        });
    }

    for (const WsprPresetCatalogEntry &preset : lookup.complete_wspr_preset_catalog())
    {
        if (preset.dial_frequency_hz >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            throw std::runtime_error(
                "preset dial_frequency_hz must be a safely representable integral Hz value.");
        }

        response["presets"].push_back({
            {"preset", preset.preset},
            {"band", preset.band},
            {"dial_frequency_hz", static_cast<std::int64_t>(preset.dial_frequency_hz)},
            {"existing_common", preset.existing_common},
        });
    }

    return response.dump();
}
