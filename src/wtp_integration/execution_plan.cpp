// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "execution_plan.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace wsprrypi {
namespace {
constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t maximum_duration = 86'400'000'000'000ULL;

bool valid_id(const std::string &id) {
  return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

// Exact binary64 * 10^9 followed by nearest integer, ties upward. Two 64-bit
// words avoid both nonstandard integer extensions and long-double differences
// between x86 and ARM. Range checking happens before every floating conversion.
std::optional<std::uint64_t> nanohertz(double hz) {
  static_assert(std::numeric_limits<double>::is_iec559 &&
                std::numeric_limits<double>::digits == 53);
  if (!std::isfinite(hz) || hz <= 0 || hz >= 18'446'744'074.0)
    return {};
  int exponent{};
  const auto fraction = std::frexp(hz, &exponent);
  const auto mantissa = static_cast<std::uint64_t>(std::ldexp(fraction, 53));
  const auto low_product = (mantissa & 0xffffffffULL) * 1'000'000'000;
  const auto high_product = (mantissa >> 32) * 1'000'000'000;
  const std::uint64_t low = low_product + (high_product << 32);
  const std::uint64_t high = (high_product >> 32) + (low < low_product);
  const int shift = 53 - exponent; // At least 18 after the range check.
  std::uint64_t value{};
  bool round_up{};
  if (shift < 64) {
    if (high >> shift)
      return {};
    value = (high << (64 - shift)) | (low >> shift);
    round_up = ((low >> (shift - 1)) & 1) != 0;
  } else if (shift == 64) {
    value = high;
    round_up = (low >> 63) != 0;
  } else if (shift < 128) {
    value = high >> (shift - 64);
    round_up = ((high >> (shift - 65)) & 1) != 0;
  } else {
    return {}; // Far below half a nanohertz.
  }
  if (round_up) {
    if (value == maximum)
      return {};
    ++value;
  }
  return value ? std::optional{value} : std::nullopt;
}

std::optional<wtp::Mode> mode(TransmissionMode value) {
  switch (value) {
  case TransmissionMode::WSPR:
    return wtp::Mode::Wspr;
  case TransmissionMode::QRSS:
    return wtp::Mode::Qrss;
  case TransmissionMode::FSKCW:
    return wtp::Mode::Fskcw;
  case TransmissionMode::DFCW:
    return wtp::Mode::Dfcw;
  case TransmissionMode::TONE:
    return wtp::Mode::Tone;
  case TransmissionMode::CW:
    return {};
  }
  return {};
}

bool event_type(const RfEvent &event) {
  switch (event.type) {
  case RfEventType::RF_ON:
  case RfEventType::SET_FREQUENCY:
    return event.rf_on;
  case RfEventType::RF_OFF:
    return !event.rf_on;
  case RfEventType::HOLD:
    return true;
  }
  return false;
}

WtpPlanResult fail(WtpPlanError error, std::string reason,
                   std::optional<std::size_t> index = {}) {
  return {{}, error, std::move(reason), index};
}
} // namespace

WtpPlanResult prepare_wtp_plan(const ExecutionPlan &plan,
                               const wtp::Capabilities &caps,
                               const WtpPlanOptions &options) {
  if (!valid_id(options.job_id))
    return fail(
        WtpPlanError::Identity,
        "A fresh 32-character lowercase hexadecimal job ID is required");
  if (std::find(caps.profiles.begin(), caps.profiles.end(), "rf-events/1") ==
          caps.profiles.end() ||
      caps.max_events < 1 || caps.max_events > 512 ||
      caps.max_payload_bytes < 1 || caps.max_payload_bytes > 65536 ||
      !caps.max_job_duration_ns ||
      caps.max_job_duration_ns > maximum_duration ||
      caps.frequency_ranges.empty() || caps.frequency_ranges.size() > 32 ||
      std::any_of(caps.frequency_ranges.begin(), caps.frequency_ranges.end(),
                  [](const auto &r) {
                    return !r.minimum_nhz || r.minimum_nhz > r.maximum_nhz;
                  }))
    return fail(WtpPlanError::Capabilities,
                "Invalid or unsupported finite-job capabilities");
  const auto selected_mode = mode(plan.mode);
  if (!selected_mode || std::find(caps.modes.begin(), caps.modes.end(),
                                  *selected_mode) == caps.modes.end())
    return fail(WtpPlanError::UnsupportedMode,
                "Plan mode is not supported by this converter and device");
  if (plan.mode == TransmissionMode::TONE && !plan.duration_was_explicit)
    return fail(WtpPlanError::ImplicitTone,
                "WTP requires an explicitly finite tone duration");
  if (!std::isfinite(plan.calibration.ppm) || plan.calibration.ppm != 0 ||
      plan.calibration.reference_frequency_hz)
    return fail(WtpPlanError::Calibration,
                "Host calibration has no WTP representation; use independently "
                "calibrated device output");
  if (plan.events.empty() || plan.summary.event_count != plan.events.size() ||
      plan.summary.total_duration.count() <= 0)
    return fail(
        WtpPlanError::Timeline,
        "Plan summary must describe a nonempty positive-duration timeline");
  if (plan.events.size() > static_cast<std::size_t>(caps.max_events))
    return fail(WtpPlanError::EventLimit,
                "Plan has " + std::to_string(plan.events.size()) + " events; device limit is " +
                std::to_string(caps.max_events) + ". Reduce repetition or message complexity");
  const auto total =
      static_cast<std::uint64_t>(plan.summary.total_duration.count());
  if (total > caps.max_job_duration_ns)
    return fail(WtpPlanError::DurationLimit,
                "Plan duration is " + std::to_string(total) + " ns; device limit is " +
                std::to_string(caps.max_job_duration_ns) + " ns. Shorten timing or reduce repetition");
  if (total > maximum - options.start_utc_ns ||
      options.max_start_uncertainty_ns > caps.maximum_arm_uncertainty_ns)
    return fail(WtpPlanError::Schedule, "UTC job end overflows or requested "
                                        "uncertainty exceeds the device limit");

  WtpPreparedPlan prepared{
      plan.id,
      plan.request_id,
      {options.job_id,
       *selected_mode,
       total,
       {},
       plan.policy.allow_quantization},
      {options.job_id, options.start_utc_ns, options.max_start_uncertainty_ns}};
  prepared.job.events.reserve(plan.events.size());
  std::uint64_t end{};
  for (std::size_t i = 0; i < plan.events.size(); ++i) {
    const auto &source = plan.events[i];
    if (!event_type(source))
      return fail(WtpPlanError::EventType,
                  "Event type contradicts its RF gate or is unknown", i);
    if (source.envelope.fade_shape != FadeShape::NONE ||
        source.envelope.fade_in.count() != 0 ||
        source.envelope.fade_out.count() != 0)
      return fail(WtpPlanError::Envelope,
                  "Amplitude shaping cannot be represented by WTP rf-events/1",
                  i);
    if (source.offset_from_start.count() < 0 || source.duration.count() <= 0 ||
        static_cast<std::uint64_t>(source.offset_from_start.count()) != end ||
        static_cast<std::uint64_t>(source.duration.count()) > total - end)
      return fail(WtpPlanError::Timeline,
                  "Events must fill the timeline contiguously without overflow",
                  i);
    wtp::RfEvent event{end,
                       static_cast<std::uint64_t>(source.duration.count()),
                       source.rf_on,
                       {}};
    if (source.rf_on) {
      event.frequency_nhz = nanohertz(source.frequency_hz);
      if (!event.frequency_nhz ||
          !std::any_of(caps.frequency_ranges.begin(),
                       caps.frequency_ranges.end(), [&](const auto &r) {
                         return *event.frequency_nhz >= r.minimum_nhz &&
                                *event.frequency_nhz <= r.maximum_nhz;
                       }))
        return fail(WtpPlanError::Frequency,
                    "RF-on frequency is invalid or outside advertised ranges",
                    i);
    }
    prepared.job.events.push_back(event);
    end += event.duration_ns;
  }
  if (end != total)
    return fail(WtpPlanError::Timeline,
                "Events do not fill the declared total duration");

  // Session/request IDs have fixed width, so these envelopes measure the
  // eventual payload size without allocating transaction identities.
  const std::string placeholder(32, '0');
  const auto load = wtp::encode_request(
      {placeholder, placeholder, wtp::Operation::Load, prepared.job});
  const auto arm = wtp::encode_request(
      {placeholder, placeholder, wtp::Operation::Arm, prepared.arm});
  if (!load || !arm ||
      load.payload->size() > static_cast<std::size_t>(caps.max_payload_bytes) ||
      arm.payload->size() > static_cast<std::size_t>(caps.max_payload_bytes))
    return fail(
        WtpPlanError::WireLimit,
        "Prepared LOAD or ARM exceeds the wire or advertised payload limits");
  return {std::move(prepared), WtpPlanError::None, {}, {}};
}
} // namespace wsprrypi
