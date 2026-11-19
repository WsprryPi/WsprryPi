// SPDX-License-Identifier: MIT
#pragma once
#include "solar_configuration.hpp"
#include "solar_clock.hpp"
#include "transmitter_runtime_bridge.hpp"
#include <atomic>
#include <chrono>
namespace wsprrypi {
// Application-owned admission state only. The existing scheduler continues to
// own timing, preparation, launch, cancellation, and completion callbacks.
struct NativeOccurrence {
    NativeSolarSnapshot snapshot;
    SolarEvaluation evaluation;
    RecurrenceListDigests digests;
    std::array<std::uint32_t,5> sizes{};
    std::vector<std::uint64_t> starts,realized_tones;
    std::string id;
    std::string ini_signature;
    std::uint64_t reload_generation{};
    std::atomic_bool reserved{false},accepted{false},resolved{false};
    std::atomic<std::uint64_t> execution_generation{0};
    std::atomic_bool terminal_pending{false};
};
std::shared_ptr<NativeOccurrence> prepare_native_occurrence(const ArgParserConfig &,
    std::span<const std::uint64_t> starts);
bool reserve_native_occurrence(const std::shared_ptr<NativeOccurrence> &,const nlohmann::json &realized_intent,
    std::span<const std::uint64_t> realized_tones={});
bool accept_native_occurrence(const std::shared_ptr<NativeOccurrence> &,const std::function<void()> &accept);
void refuse_native_occurrence(const std::shared_ptr<NativeOccurrence> &) noexcept;
void finish_native_occurrence(bool output_reconciled) noexcept;
bool native_occurrence_active() noexcept;
bool native_launch_current(const std::shared_ptr<NativeOccurrence> &) noexcept;
TransmitterRuntimeCallback native_execution_callback(const std::shared_ptr<NativeOccurrence> &);
std::shared_ptr<NativeOccurrence> active_native_occurrence();
// Closed status field values, combined with authoritative binding/output by
// the management service. Reading this observation performs no persistence.
nlohmann::json native_recurrence_observation(std::string_view self = {});
void poll_native_recurrence();
void defer_native_skip(const std::shared_ptr<NativeOccurrence> &);
void reset_native_recurrence_observation() noexcept;
std::uint64_t next_native_wspr_start(const ArgParserConfig &);
namespace testing {
// Typed fixture seam; never a CLI/INI/environment/browser setting.
void set_native_clock_reader(std::function<SolarClockObservation(bool)>);
void set_native_wspr_start(std::function<std::uint64_t()>);
}
}
