#include "../test_tone_response.hpp"

#include <cstdlib>
#include <iostream>

static void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

static void require_common_fields(
    const nlohmann::json &response,
    bool started,
    bool already_active,
    bool blocked_by_active_transmission,
    bool blocked_by_enabled_transmission,
    const char *message,
    const char *case_name)
{
    require(response.contains("command") && response["command"] == "tone_start" &&
                response.contains("status") &&
                response["status"] == (started ? "ok" : "error") &&
                response.contains("tone_start") &&
                response["tone_start"] == (started ? "ok" : "rejected") &&
                response.contains("started") && response["started"] == started &&
                response.contains("already_active") &&
                response["already_active"] == already_active &&
                response.contains("blocked_by_active_transmission") &&
                response["blocked_by_active_transmission"] == blocked_by_active_transmission &&
                response.contains("blocked_by_enabled_transmission") &&
                response["blocked_by_enabled_transmission"] == blocked_by_enabled_transmission &&
                response.contains("message") && response["message"] == message,
            case_name);
}

static void require_no_semantic_details(
    const nlohmann::json &response,
    const char *case_name)
{
    require(!response.contains("frequency_source") && !response.contains("band") &&
                !response.contains("dial_frequency_hz") &&
                !response.contains("audio_offset_hz") &&
                !response.contains("actual_rf_frequency_hz") &&
                !response.contains("selector_gpio_enabled") &&
                !response.contains("selector_gpio") &&
                !response.contains("selector_gpio_active_high"),
            case_name);
}

int main()
{
    nlohmann::json reply = {{"request_id", "current"}, {"status", "ok"}};
    attach_rp1_operation_record(reply, {{"operationId", "previous"}, {"state", "complete"}}, false);
    require(!reply.contains("rp1_operation_record"), "must not attach a stale operation record");
    attach_rp1_operation_record(reply, {{"operationId", "current"}, {"state", "complete"}}, true);
    require(reply["status"] == "ok", "completed current operation remains successful");
    for (const char *state : {"failed", "cleanup-fault", "submit-failed", "acquire-failed", "draining"}) {
        reply["status"] = "ok";
        attach_rp1_operation_record(reply, {{"operationId", "current"}, {"state", state}}, true);
        require(reply["status"] == "error", "non-complete terminal operation must report error");
    }
    TestToneStartResult result;
    result.started = true;
    result.message = "started";
    result.lifecycle = {9, 17, true, "starting", "acceptance only", "none"};
    result.band = "20m";
    result.dial_frequency_hz = 14095600;
    result.audio_offset_hz = 1500;
    result.actual_rf_frequency_hz = 14097100;
    result.resolution_source =
        WsprBandResolutionSource::BandPreferenceNumeric;
    result.selector_gpio_enabled = true;
    result.selector_gpio = 17;
    result.selector_gpio_active_high = true;

    const auto active_high_band = build_test_tone_response(
        {TestToneRequestSource::WsprBand, "20m", {}}, result);
    require(active_high_band.at("test_tone") == nlohmann::json{
        {"generation", 9}, {"revision", 17}, {"owned", true},
        {"state", "starting"}, {"message", "acceptance only"}, {"restoration", "none"}},
        "additive lifecycle metadata preserves acceptance versus activity");
    require_common_fields(active_high_band, true, false, false, false, "started",
                          "active-high band common fields");
    require(active_high_band["frequency_source"] == "wspr_band" &&
                active_high_band["band"] == "20m" &&
                active_high_band["dial_frequency_hz"] == 14095600 &&
                active_high_band["audio_offset_hz"] == 1500 &&
                active_high_band["actual_rf_frequency_hz"] == 14097100 &&
                active_high_band["resolution_source"] ==
                    "band_preference_numeric" &&
                active_high_band["preset"].is_null() &&
                active_high_band["selector_gpio"].get<int>() == 17 &&
                active_high_band["selector_gpio_active_high"].get<bool>(),
            "active-high band response");

    result.selector_gpio_active_high = false;
    const auto active_low_custom = build_test_tone_response(
        {TestToneRequestSource::CustomRf, "", 14097123}, result);
    require_common_fields(active_low_custom, true, false, false, false, "started",
                          "active-low custom common fields");
    require(active_low_custom["frequency_source"] == "custom_rf" &&
                active_low_custom["band"] == "20m" &&
                active_low_custom["actual_rf_frequency_hz"] == 14097100 &&
                !active_low_custom.contains("dial_frequency_hz") &&
                !active_low_custom.contains("audio_offset_hz") &&
                !active_low_custom["selector_gpio_active_high"].get<bool>(),
            "active-low custom response");

    result.selector_gpio_enabled = false;
    result.selector_gpio = -1;
    result.selector_gpio_active_high = true;
    const auto disabled_selector = build_test_tone_response(
        {TestToneRequestSource::WsprBand, "20m", {}}, result);
    require_common_fields(disabled_selector, true, false, false, false, "started",
                          "selector-disabled common fields");
    require(!disabled_selector["selector_gpio_enabled"].get<bool>() &&
                disabled_selector["selector_gpio"].get<int>() == -1 &&
                !disabled_selector["selector_gpio_active_high"].get<bool>(),
            "selector-disabled response");

    const auto legacy_default = build_test_tone_response(
        {TestToneRequestSource::LegacyDefault, {}, {}}, result);
    require_common_fields(legacy_default, true, false, false, false, "started",
                          "legacy default common fields");
    require_no_semantic_details(legacy_default, "legacy default compatibility");

    const auto legacy_exact = build_test_tone_response(
        {TestToneRequestSource::LegacyExactRf, {}, 14097123}, result);
    require_common_fields(legacy_exact, true, false, false, false, "started",
                          "legacy exact common fields");
    require_no_semantic_details(legacy_exact, "legacy exact compatibility");

    result.started = false;
    result.already_active = false;
    result.blocked_by_active_transmission = true;
    result.blocked_by_enabled_transmission = false;
    result.message = "active transmission";
    const auto active_rejection = build_test_tone_response(
        {TestToneRequestSource::WsprBand, "20m", {}}, result);
    require_common_fields(active_rejection, false, false, true, false,
                          "active transmission", "active-transmission rejection");
    require_no_semantic_details(active_rejection,
                                "active-transmission rejection omits details");

    result.blocked_by_active_transmission = false;
    result.blocked_by_enabled_transmission = true;
    result.message = "schedule enabled";
    const auto enabled_rejection = build_test_tone_response(
        {TestToneRequestSource::CustomRf, "", 14097123}, result);
    require_common_fields(enabled_rejection, false, false, false, true,
                          "schedule enabled", "enabled-schedule rejection");
    require_no_semantic_details(enabled_rejection,
                                "enabled-schedule rejection omits details");

    result.already_active = true;
    result.blocked_by_enabled_transmission = false;
    result.message = "already active";
    const auto already_active = build_test_tone_response(
        {TestToneRequestSource::WsprBand, "20m", {}}, result);
    require_common_fields(already_active, false, true, false, false,
                          "already active", "already-active rejection");
    require_no_semantic_details(already_active,
                                "already-active rejection omits details");

    std::cout << "test tone response passed\n";
}
