#include "../test_tone_request.hpp"
#include "../band_lookup.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

static void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

struct RejectionCase
{
    const char *name;
    nlohmann::json request;
    const char *reason;
};

int main()
{
    using json = nlohmann::json;

    {
        const json development = {
            {"enabled", true}, {"route", "GPIO4"},
            {"physical_connection", true}, {"attenuation_and_load", true},
            {"bounded_operation", true}, {"non_radiating_topology", true},
            {"experimental_acknowledged", true}};
        const auto parsed = parse_bounded_test_tone_request(json{
            {"command", "bounded_tone"},
            {"request_id", "phase7-001"},
            {"duration_ms", 2000},
            {"frequency_source", "custom_rf"},
            {"frequency_hz", 14097100},
            {"rp1_development", development}});
        require(parsed && parsed.request->request_id == "phase7-001" &&
                    parsed.request->duration_ms == 2000 &&
                    parsed.request->tone.rp1_development.enabled &&
                    parsed.request->tone.rp1_development.route_gpio == 4 &&
                    parsed.request->tone.rp1_development.operation_id == "phase7-001",
                "bounded request accepts correlated finite custom RF tone");
        auto incomplete = development;
        incomplete.erase("attenuation_and_load");
        require(!parse_bounded_test_tone_request(json{
                    {"request_id", "phase7-002"}, {"duration_ms", 1000},
                    {"rp1_development", incomplete}}),
                "development request rejects an incomplete confirmation set");
        auto wrong_route = development;
        wrong_route["route"] = "GPIO18";
        require(!parse_bounded_test_tone_request(json{
                    {"request_id", "phase7-003"}, {"duration_ms", 1000},
                    {"rp1_development", wrong_route}}),
                "development request rejects a non-allowlisted route");
    }
    for (const auto &candidate : std::vector<json>{
             json{{"command", "bounded_tone"}, {"duration_ms", 2000}},
             json{{"command", "bounded_tone"}, {"request_id", "bad id"},
                  {"duration_ms", 2000}},
             json{{"command", "bounded_tone"}, {"request_id", "x"},
                  {"duration_ms", 0}},
             json{{"command", "bounded_tone"}, {"request_id", "x"},
                  {"duration_ms", 60001}}})
    {
        require(!parse_bounded_test_tone_request(candidate),
                "bounded request rejects missing, malformed, or unsafe bounds");
    }
    const auto legacy_default = parse_test_tone_request(
        json{{"command", "tone_start"}});
    require(legacy_default &&
                legacy_default.request->source == TestToneRequestSource::LegacyDefault &&
                legacy_default.request->band.empty() &&
                !legacy_default.request->frequency_hz.has_value(),
            "legacy default request");

    for (const std::uint64_t frequency_hz :
         {std::uint64_t{14097123}, std::uint64_t{14000000}})
    {
        const auto legacy_exact = parse_test_tone_request(
            json{{"frequency_hz", frequency_hz}});
        require(legacy_exact &&
                    legacy_exact.request->source ==
                        TestToneRequestSource::LegacyExactRf &&
                    legacy_exact.request->frequency_hz == frequency_hz &&
                    legacy_exact.request->band.empty(),
                "valid legacy exact RF");
    }

    for (const auto &entry : BandLookup().canonical_wspr_band_catalog())
    {
        const auto band_request = parse_test_tone_request(
            json{{"frequency_source", "wspr_band"}, {"band", entry.band}});
        require(band_request &&
                    band_request.request->source == TestToneRequestSource::WsprBand &&
                    band_request.request->band == entry.band &&
                    !band_request.request->frequency_hz.has_value(),
                "canonical WSPR band");
    }

    for (const std::uint64_t frequency_hz :
         {std::uint64_t{14097123}, std::uint64_t{223500000},
          std::uint64_t{435000000}})
    {
        const auto custom_rf = parse_test_tone_request(
            json{{"frequency_source", "custom_rf"}, {"frequency_hz", frequency_hz}});
        require(custom_rf &&
                    custom_rf.request->source == TestToneRequestSource::CustomRf &&
                    custom_rf.request->frequency_hz == frequency_hz &&
                    custom_rf.request->band.empty(),
                "valid numeric custom RF");
    }

    const std::vector<RejectionCase> rejected_requests{
        {"null frequency_source", {{"frequency_source", nullptr}}, "frequency_source"},
        {"Boolean frequency_source", {{"frequency_source", true}}, "frequency_source"},
        {"signed frequency_source", {{"frequency_source", -1}}, "frequency_source"},
        {"unsigned frequency_source", {{"frequency_source", std::uint64_t{1}}}, "frequency_source"},
        {"floating frequency_source", {{"frequency_source", 1.0}}, "frequency_source"},
        {"array frequency_source", {{"frequency_source", json::array()}}, "frequency_source"},
        {"object frequency_source", {{"frequency_source", json::object()}}, "frequency_source"},
        {"empty frequency_source", {{"frequency_source", ""}}, "frequency_source"},
        {"unknown frequency_source", {{"frequency_source", "unknown"}}, "frequency_source"},

        {"missing wspr_band band", {{"frequency_source", "wspr_band"}}, "band"},
        {"null wspr_band band", {{"frequency_source", "wspr_band"}, {"band", nullptr}}, "band"},
        {"Boolean wspr_band band", {{"frequency_source", "wspr_band"}, {"band", true}}, "band"},
        {"integer wspr_band band", {{"frequency_source", "wspr_band"}, {"band", 20}}, "band"},
        {"floating wspr_band band", {{"frequency_source", "wspr_band"}, {"band", 20.0}}, "band"},
        {"array wspr_band band", {{"frequency_source", "wspr_band"}, {"band", json::array()}}, "band"},
        {"object wspr_band band", {{"frequency_source", "wspr_band"}, {"band", json::object()}}, "band"},
        {"empty wspr_band band", {{"frequency_source", "wspr_band"}, {"band", ""}}, "band"},
        {"unknown wspr_band band", {{"frequency_source", "wspr_band"}, {"band", "unknown"}}, "band"},
        {"removed 22m wspr_band", {{"frequency_source", "wspr_band"}, {"band", "22m"}}, "22m"},
        {"lf wspr_band alias", {{"frequency_source", "wspr_band"}, {"band", "lf"}}, "band"},
        {"mf wspr_band alias", {{"frequency_source", "wspr_band"}, {"band", "mf"}}, "band"},
        {"noncanonical wspr_band case", {{"frequency_source", "wspr_band"}, {"band", "20M"}}, "band"},
        {"wspr_band with frequency", {{"frequency_source", "wspr_band"}, {"band", "20m"}, {"frequency_hz", 1}}, "wspr_band"},

        {"missing custom_rf frequency", {{"frequency_source", "custom_rf"}}, "custom_rf"},
        {"null custom_rf frequency", {{"frequency_source", "custom_rf"}, {"frequency_hz", nullptr}}, "custom_rf"},
        {"Boolean custom_rf frequency", {{"frequency_source", "custom_rf"}, {"frequency_hz", true}}, "custom_rf"},
        {"string custom_rf frequency", {{"frequency_source", "custom_rf"}, {"frequency_hz", "14097123"}}, "custom_rf"},
        {"floating custom_rf frequency", {{"frequency_source", "custom_rf"}, {"frequency_hz", 14097123.0}}, "custom_rf"},
        {"fractional custom_rf frequency", {{"frequency_source", "custom_rf"}, {"frequency_hz", 14097123.5}}, "custom_rf"},
        {"zero custom_rf frequency", {{"frequency_source", "custom_rf"}, {"frequency_hz", 0}}, "frequency_hz"},
        {"negative custom_rf frequency", {{"frequency_source", "custom_rf"}, {"frequency_hz", -1}}, "custom_rf"},
        {"oversized custom_rf frequency", {{"frequency_source", "custom_rf"}, {"frequency_hz", std::numeric_limits<std::uint64_t>::max()}}, "frequency_hz"},
        {"out-of-band custom_rf frequency", {{"frequency_source", "custom_rf"}, {"frequency_hz", 1234567}}, "frequency_hz"},
        {"custom_rf with band", {{"frequency_source", "custom_rf"}, {"frequency_hz", 14097123}, {"band", "20m"}}, "custom_rf"},

        {"legacy band ambiguity", {{"band", "20m"}}, "band"},
        {"legacy band and frequency ambiguity", {{"band", "20m"}, {"frequency_hz", 14097123}}, "band"},
        {"legacy null frequency", {{"frequency_hz", nullptr}}, "frequency_hz"},
        {"legacy Boolean frequency", {{"frequency_hz", true}}, "frequency_hz"},
        {"legacy string frequency", {{"frequency_hz", "14097123"}}, "frequency_hz"},
        {"legacy floating frequency", {{"frequency_hz", 14097123.0}}, "frequency_hz"},
        {"legacy fractional frequency", {{"frequency_hz", 14097123.5}}, "frequency_hz"},
        {"legacy zero frequency", {{"frequency_hz", 0}}, "frequency_hz"},
        {"legacy negative frequency", {{"frequency_hz", -1}}, "frequency_hz"},
        {"legacy oversized frequency", {{"frequency_hz", std::numeric_limits<std::uint64_t>::max()}}, "frequency_hz"},
        {"legacy out-of-band frequency", {{"frequency_hz", 1234567}}, "frequency_hz"},
    };

    for (const RejectionCase &test : rejected_requests)
    {
        const auto result = parse_test_tone_request(test.request);
        require(!result && !result.request.has_value() && !result.error.empty() &&
                    result.error.find(test.reason) != std::string::npos,
                test.name);
    }

    std::cout << "test tone request parser passed\n";
}
