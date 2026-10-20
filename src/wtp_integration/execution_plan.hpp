// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once

#include "WSPR-Transmitter/src/execution_plan.hpp"
#include "WTP-Client/include/wtp/codec.hpp"

namespace wsprrypi {

struct WtpPlanOptions {
  std::string job_id; // Caller assigns a fresh ID for each execution.
  std::uint64_t start_utc_ns{}, max_start_uncertainty_ns{};
};

struct WtpPreparedPlan {
  PlanId plan_id;
  RequestId request_id;
  wtp::Job job;
  wtp::ArmRequest arm;
};

enum class WtpPlanError {
  None,
  Identity,
  Capabilities,
  UnsupportedMode,
  ImplicitTone,
  Calibration,
  Envelope,
  Timeline,
  EventType,
  Frequency,
  EventLimit,
  DurationLimit,
  Schedule,
  WireLimit
};

struct WtpPlanResult {
  std::optional<WtpPreparedPlan> prepared;
  WtpPlanError error{WtpPlanError::None};
  std::string explanation;
  std::optional<std::size_t> event_index;
  explicit operator bool() const noexcept { return prepared.has_value(); }
};

// Pure conversion only. Does not select a backend, authorize RF, establish
// ownership, assess the device clock or submit LOAD/ARM. CAPS must come from
// the selected device's validated session; admission is checked again there.
WtpPlanResult prepare_wtp_plan(const ExecutionPlan &plan,
                               const wtp::Capabilities &caps,
                               const WtpPlanOptions &options);

} // namespace wsprrypi
