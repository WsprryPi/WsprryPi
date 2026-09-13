// SPDX-License-Identifier: MIT
#include "WSPR-Transmitter/src/execution_plan_compiler.hpp"
#include "wtp_integration/execution_plan.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace wsprrypi;
using namespace std::chrono_literals;
namespace {
unsigned checks{};
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x))                                                                  \
      throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #x);  \
  } while (false)

wtp::Capabilities caps() {
  wtp::Capabilities c;
  c.profiles = {"rf-events/1"};
  c.modes = {wtp::Mode::Wspr, wtp::Mode::Qrss, wtp::Mode::Fskcw,
             wtp::Mode::Dfcw, wtp::Mode::Cw,   wtp::Mode::Tone};
  c.frequency_ranges = {{1, std::numeric_limits<std::uint64_t>::max()}};
  c.max_events = 512;
  c.max_payload_bytes = 65536;
  c.max_job_duration_ns = 86'400'000'000'000ULL;
  c.maximum_arm_uncertainty_ns = 500'000'000;
  return c;
}
WtpPlanOptions options() {
  return {std::string(32, 'a'), 1'800'000'000'000'000'000ULL, 1000};
}
ExecutionPlan plan(double frequency = 14'097'100) {
  ExecutionPlan p;
  p.id.value = 13;
  p.request_id.value = 27;
  p.mode = TransmissionMode::TONE;
  p.duration_was_explicit = true;
  p.events = {{0ns, 1s, RfEventType::RF_ON, frequency, true}};
  p.summary.event_count = 1;
  p.summary.total_duration = 1s;
  return p;
}
WtpPreparedPlan convert(const ExecutionPlan &p,
                        const wtp::Capabilities &c = caps(),
                        const WtpPlanOptions &o = options()) {
  auto result = prepare_wtp_plan(p, c, o);
  if (!result)
    throw std::runtime_error(result.explanation);
  CHECK(result.error == WtpPlanError::None && result.explanation.empty() &&
        !result.event_index);
  CHECK(result.prepared->plan_id.value == p.id.value);
  CHECK(result.prepared->request_id.value == p.request_id.value);
  CHECK(result.prepared->arm.job_id == result.prepared->job.job_id);
  CHECK(result.prepared->arm.start_utc_ns == o.start_utc_ns);
  CHECK(result.prepared->arm.max_start_uncertainty_ns ==
        o.max_start_uncertainty_ns);
  return std::move(*result.prepared);
}
void rejected(const ExecutionPlan &p, WtpPlanError e,
              const wtp::Capabilities &c = caps(),
              const WtpPlanOptions &o = options()) {
  auto result = prepare_wtp_plan(p, c, o);
  CHECK(!result && !result.prepared && result.error == e &&
        !result.explanation.empty());
}
void extended_message_boundaries() {
  ExecutionPlanCompiler compiler;
  TransmissionRequest r;
  r.output.backend = BackendKind::WTP;
  MorseTiming timing{1s, 3s, 1s, 3s, 7s};
  auto current = caps();
  current.max_job_duration_ns = 3'600'000'000'000ULL;
  for (auto mode : {TransmissionMode::QRSS, TransmissionMode::FSKCW, TransmissionMode::DFCW}) {
    r.mode = mode;
    for (unsigned count : {31U,32U,33U}) {
      const std::string text(count,'?');
      if (mode == TransmissionMode::QRSS) r.payload = QrssPayload{text,135500,timing,{}};
      else if (mode == TransmissionMode::FSKCW) r.payload = FskcwPayload{text,135500,135495,timing,{}};
      else r.payload = DfcwPayload{text,135500,135495,timing,{}};
      bool refused = false;
      try {
        auto plan = compiler.compile(r);
        CHECK(plan.events.size() == count * 12 - 1);
        CHECK(prepare_wtp_plan(plan,current,options()));
        auto old = current; old.max_events = 162;
        CHECK(!prepare_wtp_plan(plan,old,options()));
      } catch (const std::runtime_error&) { refused = true; }
      CHECK(refused == (count > 32));
      r.output.backend = BackendKind::SIMULATED;
      CHECK(compiler.compile(r).events.size() == count * 12 - 1);
      r.output.backend = BackendKind::WTP;
    }
  }
  for (auto mode : {TransmissionMode::QRSS, TransmissionMode::FSKCW, TransmissionMode::DFCW}) {
    r.mode = mode;
    for (unsigned spaces : {31U, 32U}) {
      const auto text = std::string(spaces, ' ') + "E";
      if (mode == TransmissionMode::QRSS) r.payload = QrssPayload{text,135500,timing,{}};
      else if (mode == TransmissionMode::FSKCW) r.payload = FskcwPayload{text,135500,135495,timing,{}};
      else r.payload = DfcwPayload{text,135500,135495,timing,{}};
      bool refused = false;
      try { CHECK(compiler.compile(r).events.size() == 1); }
      catch (const std::runtime_error&) { refused = true; }
      CHECK(refused == (spaces == 32));
    }
  }
  for (auto duration : {3'599'999'999'999LL,3'600'000'000'000LL,3'600'000'000'001LL}) {
    auto p = plan(); p.events[0].duration = std::chrono::nanoseconds(duration);
    p.summary.total_duration = p.events[0].duration;
    CHECK(bool(prepare_wtp_plan(p,current,options())) == (duration <= 3'600'000'000'000LL));
  }
  r.mode = TransmissionMode::QRSS;
  timing.dot = std::chrono::nanoseconds::max();
  r.payload = QrssPayload{"EE",135500,timing,{}};
  bool overflow = false;
  try { (void)compiler.compile(r); } catch (const std::runtime_error&) { overflow = true; }
  CHECK(overflow);
}

void compiler_plans() {
  ExecutionPlanCompiler compiler;
  TransmissionRequest r;
  r.id.value = 29;
  r.output.backend = BackendKind::SIMULATED;
  r.mode = TransmissionMode::WSPR;
  WsprPayload w;
  w.base_frequency_hz = 14'097'100;
  w.prepared.frames.resize(1);
  w.prepared.power_dbm =
      20; // Already encoded metadata, never electrical output power.
  for (std::size_t i = 0; i < 162; ++i)
    w.prepared.frames[0].symbols[i] = i % 4;
  r.payload = w;
  const auto p = compiler.compile(r);
  const auto prepared = convert(p);
  CHECK(prepared.job.mode == wtp::Mode::Wspr);
  CHECK(prepared.job.events.size() == 162);
  CHECK(prepared.job.total_duration_ns == 110'591'999'892ULL);
  constexpr std::uint64_t frequencies[] = {
      14'097'097'802'734'375ULL, 14'097'099'267'578'125ULL,
      14'097'100'732'421'875ULL, 14'097'102'197'265'625ULL};
  for (std::size_t i = 0; i < 162; ++i) {
    CHECK(prepared.job.events[i].offset_ns == i * 682'666'666ULL);
    CHECK(prepared.job.events[i].duration_ns == 682'666'666);
    CHECK(prepared.job.events[i].frequency_nhz == frequencies[i % 4]);
  }
  // A producer using cumulative rational boundaries must keep its exact total
  // too; the converter must not re-round each period or substitute the above.
  auto rational = p;
  for (std::uint64_t i = 0; i < 162; ++i) {
    const auto start = (i * 2'048'000'000 + 1) / 3;
    const auto end = ((i + 1) * 2'048'000'000 + 1) / 3;
    rational.events[i].offset_from_start = std::chrono::nanoseconds(start);
    rational.events[i].duration = std::chrono::nanoseconds(end - start);
  }
  rational.summary.total_duration = 110'592'000'000ns;
  auto exact = convert(rational);
  CHECK(exact.job.total_duration_ns == 110'592'000'000ULL);
  for (std::uint64_t i = 0; i < 162; ++i) {
    CHECK(exact.job.events[i].offset_ns == (i * 2'048'000'000 + 1) / 3);
    CHECK(exact.job.events[i].duration_ns ==
          (i % 3 == 1 ? 682'666'666 : 682'666'667));
  }

  MorseTiming timing{1s, 3s, 1s, 3s, 7s};
  r.mode = TransmissionMode::QRSS;
  r.payload = QrssPayload{"A", 137500, timing, {}};
  auto qrss = convert(compiler.compile(r));
  CHECK(qrss.job.mode == wtp::Mode::Qrss && qrss.job.events.size() == 3);
  CHECK(qrss.job.total_duration_ns == 5'000'000'000ULL);
  CHECK(qrss.job.events[0].frequency_nhz == 137'500'000'000'000ULL);
  CHECK(!qrss.job.events[1].rf_on && !qrss.job.events[1].frequency_nhz);
  CHECK(qrss.job.events[2].offset_ns == 2'000'000'000ULL &&
        qrss.job.events[2].duration_ns == 3'000'000'000ULL);
  r.mode = TransmissionMode::FSKCW;
  r.payload = FskcwPayload{"A", 137501, 137500, timing, {}};
  auto fskcw = convert(compiler.compile(r));
  CHECK(fskcw.job.mode == wtp::Mode::Fskcw && fskcw.job.events.size() == 3);
  CHECK(fskcw.job.events[0].frequency_nhz == 137'501'000'000'000ULL);
  CHECK(fskcw.job.events[1].rf_on &&
        fskcw.job.events[1].frequency_nhz == 137'500'000'000'000ULL);
  r.mode = TransmissionMode::DFCW;
  r.payload = DfcwPayload{"A", 137500, 137501, timing, {}};
  auto dfcw = convert(compiler.compile(r));
  CHECK(dfcw.job.mode == wtp::Mode::Dfcw &&
        dfcw.job.total_duration_ns == 3'000'000'000ULL);
  CHECK(!dfcw.job.events[1].frequency_nhz && !dfcw.job.events[1].rf_on);
  CHECK(dfcw.job.events[2].frequency_nhz == 137'501'000'000'000ULL);
  r.mode = TransmissionMode::TONE;
  r.payload = TonePayload{137500, 1s, {}};
  auto tone = convert(compiler.compile(r));
  CHECK(tone.job.mode == wtp::Mode::Tone &&
        tone.job.total_duration_ns == 1'000'000'001);
  CHECK(tone.job.events.size() == 2 && tone.job.events[1].duration_ns == 1);
  CHECK(!tone.job.events[1].rf_on && !tone.job.events[1].frequency_nhz);
  r.payload = TonePayload{137500, {}, {}};
  rejected(compiler.compile(r), WtpPlanError::ImplicitTone);
}

void boundaries() {
  auto p = plan();
  auto c = caps();
  auto o = options();
  auto prepared = convert(p);
  CHECK(prepared.job.allow_frequency_adjustment == false);
  p.policy.allow_backend_approximation = true;
  CHECK(convert(p).job.allow_frequency_adjustment == false);
  p.policy.allow_quantization = true;
  CHECK(convert(p).job.allow_frequency_adjustment == true);
  p = plan();
  for (auto bad : {TransmissionMode::CW, static_cast<TransmissionMode>(999)}) {
    p.mode = bad;
    rejected(p, WtpPlanError::UnsupportedMode);
  }
  p = plan();
  c.modes = {wtp::Mode::Qrss};
  rejected(p, WtpPlanError::UnsupportedMode, c);
  c = caps();
  c.profiles.clear();
  rejected(p, WtpPlanError::Capabilities, c);
  c = caps();
  c.max_events = 513;
  rejected(p, WtpPlanError::Capabilities, c);
  c = caps();
  c.max_events = 0;
  rejected(p, WtpPlanError::Capabilities, c);
  c = caps();
  c.frequency_ranges = {{2, 1}};
  rejected(p, WtpPlanError::Capabilities, c);
  c = caps();
  c.max_job_duration_ns = 86'400'000'000'001ULL;
  rejected(p, WtpPlanError::Capabilities, c);
  c = caps();
  c.max_payload_bytes = 65537;
  rejected(p, WtpPlanError::Capabilities, c);
  c = caps();
  c.max_payload_bytes = 1;
  rejected(p, WtpPlanError::WireLimit, c);
  c = caps();
  c.max_job_duration_ns = 999'999'999;
  rejected(p, WtpPlanError::DurationLimit, c);
  c.max_job_duration_ns = 1'000'000'000;
  convert(p, c);
  c = caps();
  c.frequency_ranges = {{14'097'100'000'000'000ULL, 14'097'100'000'000'000ULL}};
  convert(p, c);
  ++c.frequency_ranges[0].minimum_nhz;
  ++c.frequency_ranges[0].maximum_nhz;
  rejected(p, WtpPlanError::Frequency, c);
  c = caps();
  for (double f :
       {0.0, -1.0, std::numeric_limits<double>::infinity(), std::nan(""),
        std::numeric_limits<double>::denorm_min(), 18'446'744'074.0})
    rejected(plan(f), WtpPlanError::Frequency);
  CHECK(convert(plan(0.125)).job.events[0].frequency_nhz == 125'000'000);
  CHECK(convert(plan(0x1p-10)).job.events[0].frequency_nhz ==
        976563); // Exact half, upward.
  CHECK(
      convert(plan(std::nextafter(0x1p-10, 0.0))).job.events[0].frequency_nhz ==
      976562);
  for (double ppm :
       {1.0, -1.0, std::nan(""), std::numeric_limits<double>::infinity()}) {
    p.calibration.ppm = ppm;
    rejected(p, WtpPlanError::Calibration);
  }
  p = plan();
  p.calibration.reference_frequency_hz = 137500;
  rejected(p, WtpPlanError::Calibration);
  p = plan();
  p.events[0].envelope.fade_shape = FadeShape::LINEAR;
  rejected(p, WtpPlanError::Envelope);
  p = plan();
  p.events[0].envelope.fade_in = 1ns;
  rejected(p, WtpPlanError::Envelope);
  p = plan();
  p.events[0].envelope.fade_out = -1ns;
  rejected(p, WtpPlanError::Envelope);
  p = plan();
  p.events[0].type = RfEventType::RF_OFF;
  rejected(p, WtpPlanError::EventType);
  p = plan();
  p.events[0].type = static_cast<RfEventType>(999);
  rejected(p, WtpPlanError::EventType);
  p = plan();
  p.events[0].type = RfEventType::SET_FREQUENCY;
  convert(p);
  p.events[0].rf_on = false;
  rejected(p, WtpPlanError::EventType);
  p.events[0].type = RfEventType::HOLD;
  p.events[0].frequency_hz = std::nan("");
  CHECK(!convert(p).job.events[0].frequency_nhz); // Inactive frequency is not
                                                  // an output request.
  p = plan();
  p.events.clear();
  rejected(p, WtpPlanError::Timeline);
  p = plan();
  p.summary.event_count = 2;
  rejected(p, WtpPlanError::Timeline);
  p = plan();
  p.summary.total_duration = 0ns;
  rejected(p, WtpPlanError::Timeline);
  p = plan();
  p.events[0].offset_from_start = -1ns;
  rejected(p, WtpPlanError::Timeline);
  p = plan();
  p.events[0].offset_from_start = 1ns;
  rejected(p, WtpPlanError::Timeline);
  p = plan();
  p.events[0].duration = 0ns;
  rejected(p, WtpPlanError::Timeline);
  p = plan();
  p.events[0].duration = std::chrono::nanoseconds::max();
  rejected(p, WtpPlanError::Timeline);
  p = plan();
  p.events[0].duration = 999'999'999ns;
  rejected(p, WtpPlanError::Timeline);
  p = plan();
  o.job_id = std::string(32, 'A');
  rejected(p, WtpPlanError::Identity, c, o);
  o = options();
  o.job_id += 'a';
  rejected(p, WtpPlanError::Identity, c, o);
  o = options();
  o.start_utc_ns = std::numeric_limits<std::uint64_t>::max() - 1'000'000'000;
  convert(p, c, o);
  ++o.start_utc_ns;
  rejected(p, WtpPlanError::Schedule, c, o);
  o = options();
  o.max_start_uncertainty_ns = c.maximum_arm_uncertainty_ns;
  convert(p, c, o);
  ++o.max_start_uncertainty_ns;
  rejected(p, WtpPlanError::Schedule, c, o);
  p = plan();
  p.events.resize(512, p.events[0]);
  for (std::size_t i = 0; i < 512; ++i) {
    p.events[i].offset_from_start = std::chrono::nanoseconds(i);
    p.events[i].duration = 1ns;
  }
  p.summary = {512ns, 512, 0, 0};
  CHECK(convert(p).job.events.size() ==
        512); // No coalescing even identical adjacent RF.
  c.max_events = 511;
  rejected(p, WtpPlanError::EventLimit, c);
  c = caps();
  p.events.push_back(p.events.back());
  p.summary.event_count = 513;
  rejected(p, WtpPlanError::EventLimit, c);
  p = plan();
  p.events[0].duration = 86'400'000'000'000ns;
  p.summary.total_duration = p.events[0].duration;
  convert(p);
  p.summary.total_duration += 1ns;
  rejected(p, WtpPlanError::DurationLimit);
}

void wire_and_atomicity() {
  auto p = plan();
  const auto before = p.events[0].frequency_hz;
  auto prepared = convert(p);
  p.events[0].frequency_hz = 0;
  rejected(p, WtpPlanError::Frequency);
  CHECK(prepared.job.events[0].frequency_nhz == 14'097'100'000'000'000ULL);
  CHECK(p.events[0].frequency_hz ==
        0); // Neither input nor earlier output overwritten.
  p.events[0].frequency_hz = before;
  const std::string id(32, 'b');
  auto load = wtp::encode_request({id, id, wtp::Operation::Load, prepared.job});
  CHECK(load);
  auto decoded = wtp::decode(*load.payload);
  CHECK(decoded);
  const auto &job =
      std::get<wtp::Job>(std::get<wtp::Request>(*decoded.message).body);
  CHECK(job.job_id == options().job_id &&
        job.total_duration_ns == 1'000'000'000);
  CHECK(job.events[0].frequency_nhz == 14'097'100'000'000'000ULL);
  auto arm = wtp::encode_request({id, id, wtp::Operation::Arm, prepared.arm});
  CHECK(arm && wtp::decode(*arm.payload));
  auto c = caps();
  c.max_payload_bytes = static_cast<int>(load.payload->size());
  convert(p, c);
  --c.max_payload_bytes;
  rejected(p, WtpPlanError::WireLimit, c);

  // Fail after building a valid prefix: no partial job may escape, and the
  // diagnostic must identify the actual failing source event.
  p.events.push_back(p.events[0]);
  p.events[1].offset_from_start = 1s;
  p.summary = {2s, 2, 0, 0};
  p.events[1].frequency_hz = 0;
  auto late_failure = prepare_wtp_plan(p, caps(), options());
  CHECK(!late_failure.prepared &&
        late_failure.error == WtpPlanError::Frequency &&
        late_failure.event_index == 1);
  p.events[1].frequency_hz = before;
  for (auto offset : {999'999'999ns, 1'000'000'001ns}) {
    p.events[1].offset_from_start = offset;
    auto gap_or_overlap = prepare_wtp_plan(p, caps(), options());
    CHECK(!gap_or_overlap.prepared &&
          gap_or_overlap.error == WtpPlanError::Timeline &&
          gap_or_overlap.event_index == 1);
  }
  p.events[1].offset_from_start = 1s;
  c = caps();
  c.frequency_ranges = {{1, 1000},
                        {14'097'100'000'000'000ULL, 14'097'100'000'000'000ULL}};
  CHECK(convert(p, c).job.events.size() == 2);
  p.events[1].frequency_hz = 1.0; // In the gap between advertised ranges.
  rejected(p, WtpPlanError::Frequency, c);
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--frequency-vectors") {
      std::uint64_t bits;
      while (std::cin >> std::hex >> bits) {
        auto result = prepare_wtp_plan(plan(std::bit_cast<double>(bits)),
                                       caps(), options());
        if (result)
          std::cout << *result.prepared->job.events[0].frequency_nhz << '\n';
        else
          std::cout << "rejected\n";
      }
      return 0;
    }
    compiler_plans();
    extended_message_boundaries();
    boundaries();
    wire_and_atomicity();
    std::cout << "WTP execution-plan checks passed: " << checks << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
