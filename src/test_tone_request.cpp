#include "test_tone_request.hpp"
#include "band_lookup.hpp"
#include <algorithm>
#include <cctype>
#include <limits>

namespace
{
std::optional<std::uint64_t> parse_legacy_exact_rf(
    const nlohmann::json &value,
    std::string &error)
{
    std::uint64_t candidate_frequency_hz = 0;
    if (value.is_number_unsigned())
    {
        candidate_frequency_hz = value.get<std::uint64_t>();
    }
    else if (value.is_number_integer())
    {
        const auto signed_frequency_hz = value.get<std::int64_t>();
        if (signed_frequency_hz > 0)
        {
            candidate_frequency_hz =
                static_cast<std::uint64_t>(signed_frequency_hz);
        }
    }

    if (candidate_frequency_hz == 0)
    {
        error = "frequency_hz must be a positive integer";
        return std::nullopt;
    }
    if (candidate_frequency_hz >
        static_cast<std::uint64_t>(std::numeric_limits<long long>::max()))
    {
        error = "frequency_hz is outside the supported RF range";
        return std::nullopt;
    }

    const BandLookup lookup;
    const auto validation = lookup.lookup(
        static_cast<double>(candidate_frequency_hz));
    const auto *validation_text = std::get_if<std::string>(&validation);
    if (validation_text == nullptr || *validation_text == "Invalid Frequency")
    {
        error = "frequency_hz is outside the supported RF range";
        return std::nullopt;
    }
    return candidate_frequency_hz;
}
} // namespace

TestToneRequestParseResult parse_test_tone_request(const nlohmann::json &j)
{
    auto fail=[](const char *s){ return TestToneRequestParseResult{{}, s}; };
    const bool has_source=j.contains("frequency_source"), has_band=j.contains("band"), has_frequency=j.contains("frequency_hz");
    if (!has_source) {
        if (has_band) return fail("band requires frequency_source");
        if (!has_frequency) return {{ParsedTestToneRequest{}}, {}};
        std::string error;
        const auto value = parse_legacy_exact_rf(j["frequency_hz"], error);
        if (!value.has_value()) return fail(error.c_str());
        return {{ParsedTestToneRequest{TestToneRequestSource::LegacyExactRf,{},*value}}, {}};
    }
    if (!j["frequency_source"].is_string()) return fail("frequency_source must be a string");
    const auto source=j["frequency_source"].get<std::string>();
    BandLookup lookup;
    if (source=="wspr_band") {
        if (has_frequency || !has_band || !j["band"].is_string()) return fail("wspr_band requires only band");
        const auto band=j["band"].get<std::string>();
        for (const auto &entry: lookup.canonical_wspr_band_catalog()) if (entry.band==band)
            return {{ParsedTestToneRequest{TestToneRequestSource::WsprBand,band,std::nullopt}},{}};
        if (band == "22m")
            return fail("22m is not in the amateur correlation catalog");
        return fail("band must be canonical");
    }
    if (source=="custom_rf") {
        if (has_band || !has_frequency || !j["frequency_hz"].is_number_integer()) return fail("custom_rf requires only positive integer frequency_hz");
        if (!j["frequency_hz"].is_number_unsigned() && j["frequency_hz"].get<std::int64_t>() <= 0) return fail("custom_rf requires only positive integer frequency_hz");
        const auto value=j["frequency_hz"].get<std::uint64_t>();
        if (value==0 || value>static_cast<std::uint64_t>(std::numeric_limits<long long>::max()) || !lookup.lookup_ham_band(static_cast<double>(value)).has_value()) return fail("frequency_hz is outside supported amateur bands");
        return {{ParsedTestToneRequest{TestToneRequestSource::CustomRf,{},value}},{}};
    }
    return fail("unknown frequency_source");
}

BoundedTestToneRequestParseResult parse_bounded_test_tone_request(
    const nlohmann::json &j)
{
    auto fail = [](const std::string &message) {
        return BoundedTestToneRequestParseResult{{}, message};
    };
    if (!j.contains("request_id") || !j["request_id"].is_string())
        return fail("request_id must be a string");
    const std::string request_id = j["request_id"].get<std::string>();
    if (request_id.empty() || request_id.size() > 128U)
        return fail("request_id must contain 1 to 128 characters");
    if (!std::all_of(request_id.begin(), request_id.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_' || c == '.';
        }))
        return fail("request_id contains unsupported characters");

    if (!j.contains("duration_ms") || !j["duration_ms"].is_number_integer())
        return fail("duration_ms must be a positive integer");
    if (!j["duration_ms"].is_number_unsigned() &&
        j["duration_ms"].get<std::int64_t>() <= 0)
        return fail("duration_ms must be a positive integer");
    const std::uint64_t duration_ms = j["duration_ms"].get<std::uint64_t>();
    if (duration_ms == 0 || duration_ms > MAX_BOUNDED_TONE_DURATION_MS)
        return fail("duration_ms must be between 1 and 60000");

    const TestToneRequestParseResult tone = parse_test_tone_request(j);
    if (!tone)
        return fail(tone.error);
    ParsedTestToneRequest parsed_tone = *tone.request;
    if (j.contains("rp1_development"))
    {
        const auto& value = j["rp1_development"];
        if (!value.is_object() || value.size() != 7 ||
            !value.value("enabled", false) ||
            !value.contains("route") || !value["route"].is_string() ||
            !value.value("physical_connection", false) ||
            !value.value("attenuation_and_load", false) ||
            !value.value("bounded_operation", false) ||
            !value.value("non_radiating_topology", false) ||
            !value.value("experimental_acknowledged", false))
            return fail("rp1_development requires every explicit confirmation");
        const std::string route = value["route"].get<std::string>();
        parsed_tone.rp1_development.enabled = true;
        parsed_tone.rp1_development.route_gpio = route == "GPIO4" ? 4 : route == "GPIO20" ? 20 : 0;
        if (parsed_tone.rp1_development.route_gpio == 0)
            return fail("rp1_development route must be GPIO4 or GPIO20");
        parsed_tone.rp1_development.physical_connection = true;
        parsed_tone.rp1_development.attenuation_and_load = true;
        parsed_tone.rp1_development.bounded_operation = true;
        parsed_tone.rp1_development.non_radiating_topology = true;
        parsed_tone.rp1_development.experimental_acknowledged = true;
        parsed_tone.rp1_development.operation_id = request_id;
    }
    return {{ParsedBoundedTestToneRequest{
        parsed_tone, request_id, duration_ms}}, {}};
}
