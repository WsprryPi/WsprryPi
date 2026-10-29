// SPDX-License-Identifier: MIT
#pragma once
#include <initializer_list>
#include <string_view>
namespace wsprry::logging {
// Producer-owned fixed templates. Arbitrary diagnostic bodies are never exports.
struct PicoExportTemplate {
    std::string_view code, message;
};
// Immutable producer templates also let the bounded firmware queue retain a
// template index rather than sixteen redundant maximum-length text buffers.
inline constexpr PicoExportTemplate pico_export_templates[] = {
    {"UDP_TEST", "UDP logging test event"},
    {"BOOT", "Application started"},
    {"CONFIG_SAVED", "Settings journal updated"},
    {"USB_STATE", "USB control connection changed"},
    {"USB_LOG", "Logging session started"},
    {"CLOCK_STATE", "UTC unavailable"},
    {"CLOCK_STATE", "UTC available"},
    {"JOB_STATE", "empty"},
    {"JOB_STATE", "missed"},
    {"JOB_STATE", "idle"},
    {"JOB_STATE", "loaded"},
    {"JOB_STATE", "armed"},
    {"JOB_STATE", "running"},
    {"JOB_STATE", "complete"},
    {"JOB_STATE", "failed"},
    {"JOB_STATE", "aborted"},
    {"JOB_STATE", "Idle"},
    {"JOB_STATE", "Loaded"},
    {"JOB_STATE", "Armed"},
    {"JOB_STATE", "Running"},
    {"JOB_STATE", "Complete"},
    {"JOB_STATE", "Failed"},
    {"JOB_STATE", "Aborted"},
    {"CLOCK_STATE", "UTC synchronized"},
    {"CLOCK_STATE", "UTC holdover"}};
inline unsigned pico_export_template(std::string_view code, std::string_view message) {
    unsigned index = 0;
    for (const auto& entry : pico_export_templates) {
        if (code == entry.code && message == entry.message)
            return index;
        ++index;
    }
    return index;
}
inline bool export_safe_pico(std::string_view code, std::string_view message) {
    return pico_export_template(code, message) <
           sizeof(pico_export_templates) / sizeof(pico_export_templates[0]);
}
inline bool export_safe_pi(std::string_view code, std::string_view message) {
    if (code != "PI_LOG")
        return false;
    for (const auto text : {"UDP logging test event", "Rebooting.", "Shutting down.",
                            "WSPR loop running.", "PPM Manager initialized successfully.",
                            "USB log collector unavailable; review its saved settings.",
                            "Pico startup is not ready; inspect endpoint status and explicitly "
                            "reconcile before scheduling.",
                            "Route restoration could not acknowledge idle application readiness."})
        if (message == text)
            return true;
    return false;
}
} // namespace wsprry::logging
