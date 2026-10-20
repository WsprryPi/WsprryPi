// SPDX-License-Identifier: MIT
#include "fleet_schedule.hpp"
#include "WSPR-Transmitter/src/execution_plan_compiler.hpp"
#include "WSPR-Transmitter/src/wspr_reference_adapter.hpp"
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
namespace wsprrypi {
namespace {
constexpr std::uint64_t second = 1000000000ULL;
std::uint64_t integer(const nlohmann::json& j, const char* key, std::uint64_t min, std::uint64_t max) {
    const auto& value = j.at(key);
    if (!value.is_number_unsigned() && (!value.is_number_integer() || value.get<std::int64_t>() < 0))
        throw std::runtime_error(std::string(key) + " must be an integer");
    const auto n = value.get<std::uint64_t>();
    if (n < min || n > max) throw std::runtime_error(std::string(key) + " is outside its allowed range");
    return n;
}
void keys(const nlohmann::json& j, std::set<std::string> allowed) {
    if (!j.is_object() || j.size() != allowed.size()) throw std::runtime_error("Invalid output schedule fields");
    for (const auto& [key, unused] : j.items()) {
        (void)unused;
        if (!allowed.contains(key)) throw std::runtime_error("Unknown output schedule field: " + key);
    }
}
double frequency(const nlohmann::json& j, const char* field) {
    if (!j.at(field).is_number()) throw std::runtime_error("Frequency must be numeric Hz");
    const double f = j.at(field).get<double>();
    if (!std::isfinite(f) || f < 1 || f > 200000000) throw std::runtime_error("Frequency is outside 1 to 200000000 Hz");
    return f;
}
}
TransmissionRequest wtp_fleet_request(const nlohmann::json& schedule) {
    const auto mode = schedule.at("mode").get<std::string>();
    std::set<std::string> fields{"mode", "period_seconds", "phase_seconds", "frequency_hz"};
    TransmissionRequest result;
    result.output.backend = BackendKind::WTP;
    result.metadata.origin = "fleet";
    const double rf = frequency(schedule, "frequency_hz");
    if (mode == "tone") {
        fields.insert("duration_ms");
        result.mode = TransmissionMode::TONE;
        result.payload = TonePayload{rf, std::chrono::milliseconds(integer(schedule, "duration_ms", 1, 600000)), {}};
    } else if (mode == "wspr") {
        fields.insert({"callsign", "locator", "power_dbm"});
        const auto callsign = schedule.at("callsign").get<std::string>();
        const auto locator = schedule.at("locator").get<std::string>();
        if (callsign.empty() || callsign.size() > 16 || locator.size() > 6)
            throw std::runtime_error("Invalid WSPR station fields");
        const auto power = integer(schedule, "power_dbm", 0, 60);
        auto prepared = build_prepared_wspr_transmission(callsign, locator, static_cast<int>(power));
        if (prepared.frames.size() != 1)
            throw std::runtime_error("Fleet WSPR currently needs a single-frame callsign and locator");
        prepared.current_frame = 1;
        result.mode = TransmissionMode::WSPR;
        result.payload = WsprPayload{std::move(prepared), rf, {}};
    } else if (mode == "qrss" || mode == "fskcw" || mode == "dfcw") {
        fields.insert({"message", "dot_ms"});
        const auto message = schedule.at("message").get<std::string>();
        if (message.empty() || message.size() > 80) throw std::runtime_error("Message must contain 1 to 80 characters");
        const auto dot = std::chrono::milliseconds(integer(schedule, "dot_ms", 100, 60000));
        MorseTiming timing{dot, dot * 3, dot, dot * 3, dot * 7};
        if (mode == "qrss") {
            result.mode = TransmissionMode::QRSS;
            result.payload = QrssPayload{message, rf, timing, {}};
        } else {
            fields.insert("shift_hz");
            const auto shift = schedule.at("shift_hz").get<double>();
            if (!std::isfinite(shift) || shift <= 0 || shift > 1000 || rf + shift > 200000000)
                throw std::runtime_error("Shift must be greater than 0 and at most 1000 Hz");
            if (mode == "fskcw") {
                result.mode = TransmissionMode::FSKCW;
                result.payload = FskcwPayload{message, rf + shift, rf, timing, {}};
            } else {
                result.mode = TransmissionMode::DFCW;
                timing.dash = dot;
                result.payload = DfcwPayload{message, rf, rf + shift, timing, {}};
            }
        }
    } else throw std::runtime_error("Unsupported fleet mode");
    keys(schedule, std::move(fields));
    const auto period = integer(schedule, "period_seconds", 60, 86400);
    const auto phase = integer(schedule, "phase_seconds", 0, period - 1);
    if (mode == "wspr" && (period % 120 != 0 || phase % 120 != 0))
        throw std::runtime_error("WSPR period and phase must align to two-minute boundaries");
    return result;
}
void validate_wtp_fleet_schedule(const nlohmann::json& schedule) {
    auto request = wtp_fleet_request(schedule);
    ExecutionPlanCompiler compiler;
    const auto plan = compiler.compile(request);
    if (plan.events.empty() || plan.events.size() > 512 || plan.summary.total_duration.count() <= 0 ||
        static_cast<std::uint64_t>(plan.summary.total_duration.count()) >=
            schedule.at("period_seconds").get<std::uint64_t>() * second)
        throw std::runtime_error("Finite job must fit inside its repeat period and 512 events");
}
std::uint64_t wtp_fleet_next_slot(const nlohmann::json& schedule,
    std::uint64_t earliest_ns, std::uint64_t last_start_ns) {
    const auto period = integer(schedule, "period_seconds", 60, 86400) * second;
    auto phase = integer(schedule, "phase_seconds", 0, period / second - 1) * second;
    if (schedule.at("mode") == "wspr") phase += second;
    const auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (last_start_ns >= max || earliest_ns > max - period) throw std::runtime_error("Fleet UTC overflow");
    const auto earliest = std::max(earliest_ns, last_start_ns + 1);
    const auto next = earliest <= phase ? phase : ((earliest - phase + period - 1) / period) * period + phase;
    if (next > max) throw std::runtime_error("Fleet UTC overflow");
    return next;
}
}
