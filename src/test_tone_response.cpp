#include "test_tone_response.hpp"
void attach_rp1_operation_record(nlohmann::json &reply, const nlohmann::json &record,
                                 bool terminal)
{
    // Startup is asynchronous: never attach the preceding operation's result.
    if (record.value("operationId", "") != reply.value("request_id", "")) return;
    reply["rp1_operation_record"] = record;
    if (terminal && record.value("state", "") != "complete")
    {
        reply["status"] = "error";
        reply["message"] = "RP1 operation failed; inspect rp1_operation_record.";
    }
}
nlohmann::json build_test_tone_response(const ParsedTestToneRequest&r,const TestToneStartResult&s){nlohmann::json j={{"command","tone_start"},{"started",s.started},{"already_active",s.already_active},{"blocked_by_active_transmission",s.blocked_by_active_transmission},{"blocked_by_enabled_transmission",s.blocked_by_enabled_transmission},{"message",s.message},{"status",s.started?"ok":"error"},{"tone_start",s.started?"ok":"rejected"}};j["test_tone"]=test_tone_status_json(s.lifecycle);if(!s.started)return j;if(r.source==TestToneRequestSource::WsprBand){j["frequency_source"]="wspr_band";j["band"]=s.band;j["dial_frequency_hz"]=s.dial_frequency_hz;j["audio_offset_hz"]=s.audio_offset_hz;j["actual_rf_frequency_hz"]=s.actual_rf_frequency_hz;j["resolution_source"]=wspr_band_resolution_source_name(s.resolution_source);j["preset"]=s.preset?nlohmann::json(*s.preset):nlohmann::json(nullptr);}else if(r.source==TestToneRequestSource::CustomRf){j["frequency_source"]="custom_rf";j["band"]=s.band;j["actual_rf_frequency_hz"]=s.actual_rf_frequency_hz;j["resolution_source"]="custom_rf";}if(r.source==TestToneRequestSource::WsprBand||r.source==TestToneRequestSource::CustomRf){j["selector_gpio_enabled"]=s.selector_gpio_enabled;j["selector_gpio"]=s.selector_gpio;j["selector_gpio_active_high"]=s.selector_gpio_enabled?s.selector_gpio_active_high:false;}return j;}

nlohmann::json test_tone_status_json(const TestToneStatus &status)
{
    return {{"generation", status.generation}, {"revision", status.revision},
            {"owned", status.owned}, {"state", status.state},
            {"message", status.message}, {"restoration", status.restoration}};
}
