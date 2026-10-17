// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "usb_management.hpp"
#include "browser_api.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <set>
#include <stdexcept>
#if defined(__linux__)
#include <sys/timex.h>
#endif
namespace wsprrypi {
namespace {
using Json = nlohmann::json;
bool identity(const std::string &s) {
  return s.size() == 32 && std::all_of(s.begin(),s.end(),[](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::uint64_t decimal(const Json &j) {
  if (!j.is_string()) throw std::runtime_error("invalid_decimal");
  const auto s = j.get<std::string>(); std::uint64_t value{};
  auto result = std::from_chars(s.data(),s.data()+s.size(),value);
  if (s.empty() || s.size()>20 || (s.size()>1 && s[0]=='0') || result.ec!=std::errc{} || result.ptr!=s.data()+s.size())
    throw std::runtime_error("invalid_decimal");
  return value;
}
}
HostUtcObservation qualified_host_utc() noexcept {
  HostUtcObservation value;
#if defined(__linux__)
  timex quality{};
  const auto before = std::chrono::steady_clock::now();
  const int state = adjtimex(&quality); // modes=0: read-only clock quality.
  const auto wall = std::chrono::system_clock::now();
  const auto after = std::chrono::steady_clock::now();
  if (state != TIME_OK || (quality.status & (STA_UNSYNC | STA_CLOCKERR | STA_INS | STA_DEL)) ||
      quality.maxerror < 0 || quality.esterror < 0 || quality.maxerror > 500000) return value;
  const auto utc = std::chrono::duration_cast<std::chrono::nanoseconds>(wall.time_since_epoch()).count();
  if (utc <= 0) return value;
  value.monotonic_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(after.time_since_epoch()).count();
  value.utc_ns = static_cast<std::uint64_t>(utc);
  value.uncertainty_ns = static_cast<std::uint64_t>(std::max(quality.maxerror,quality.esterror))*1000ULL +
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(after-before).count());
  value.qualified = true;
#endif
  return value;
}
UsbManagementClient::UsbManagementClient(wtp::ByteStream &stream, Clock now, Wait wait, std::function<bool()> cancelled)
    : stream_(stream), now_(std::move(now)), wait_(std::move(wait)), cancelled_(std::move(cancelled)) {}
void UsbManagementClient::fail(const char *reason) {
  connected_ = false; session_.clear(); diagnostic_ = reason; stream_.close();
}
void UsbManagementClient::close() noexcept {
  std::lock_guard lock(mutex_); fail("usb_disconnected");
}
bool UsbManagementClient::connected() const { std::lock_guard lock(mutex_); return connected_; }
std::string UsbManagementClient::device() const { std::lock_guard lock(mutex_); return target_.is_object()?target_.value("device_id",""):""; }
std::string UsbManagementClient::boot() const { std::lock_guard lock(mutex_); return target_.is_object()?target_.value("boot_id",""):""; }
Json UsbManagementClient::observation() const {
  std::lock_guard lock(mutex_);
  return {{"connected",connected_},{"target",target_},{"discovery",hello_},{"time",time_},{"diagnostic",diagnostic_}};
}
Json UsbManagementClient::exchange(const Json &request, std::uint64_t deadline) {
  const auto payload=request.dump();
  if (payload.size()>32768) throw std::runtime_error("request_too_large");
  std::vector<std::uint8_t> frame{'U','M','P','1',
    static_cast<std::uint8_t>(payload.size()>>24),static_cast<std::uint8_t>(payload.size()>>16),
    static_cast<std::uint8_t>(payload.size()>>8),static_cast<std::uint8_t>(payload.size())};
  frame.insert(frame.end(),payload.begin(),payload.end());
  std::uint64_t observed=now_();
  const auto transfer=[&](bool writing, std::span<std::uint8_t> bytes) {
    std::size_t offset=0;
    while (offset<bytes.size()) {
      const auto now=now_();
      if (cancelled_ && cancelled_()) throw std::runtime_error("usb_cancelled");
      if (now<observed || now>=deadline) throw std::runtime_error("usb_deadline");
      observed=now;
      auto result=writing?stream_.write(bytes.subspan(offset)):stream_.read(bytes.subspan(offset));
      if (result.state==wtp::IoState::WouldBlock) { wait_(1); continue; }
      if (result.state!=wtp::IoState::Progress || !result.count || result.count>bytes.size()-offset)
        throw std::runtime_error("usb_unavailable");
      offset+=result.count;
    }
  };
  transfer(true,frame);
  std::array<std::uint8_t,8> header{}; transfer(false,header);
  if (!std::equal(header.begin(),header.begin()+4,frame.begin())) throw std::runtime_error("invalid_usb_frame");
  const auto length=(std::uint32_t(header[4])<<24)|(std::uint32_t(header[5])<<16)|(std::uint32_t(header[6])<<8)|header[7];
  if (!length || length>65536) throw std::runtime_error("invalid_usb_length");
  std::vector<std::uint8_t> response(length); transfer(false,response);
  // The shared parser bounds browser requests to 32768. Resource responses may
  // be 65536, so enforce duplicate/depth handling directly for this envelope.
  std::vector<std::set<std::string>> objects;
  auto result=Json::parse(response.begin(),response.end(),[&](int depth,Json::parse_event_t event,Json &value) {
    if (depth>32) throw std::runtime_error("nesting_limit");
    if (event==Json::parse_event_t::object_start) objects.emplace_back();
    if (event==Json::parse_event_t::object_end) objects.pop_back();
    if (event==Json::parse_event_t::key && !objects.back().insert(value.get<std::string>()).second)
      throw std::runtime_error("duplicate_key");
    return true;
  });
  if (!result.is_object() || result.size()!=8 || result.value("schema","")!="usb-management/1" ||
      result.at("id")!=request.at("id") || !result.at("status").is_number_unsigned() ||
      !result.at("session").is_string() || !result.at("body").is_string() ||
      !result.at("etag").is_string() || !result.at("delivery_required").is_boolean() ||
      !result.at("target").is_object()) throw std::runtime_error("invalid_usb_response");
  return result;
}
bool UsbManagementClient::hello(const std::string &expected_device) {
  std::lock_guard lock(mutex_);
  try {
    connected_=false; target_=nullptr; session_.clear(); next_id_=1;
    auto response=exchange({{"schema","usb-management/1"},{"id",next_id_},{"operation","hello"}},now_()+2000);
    const auto &target=response.at("target");
    if (response.at("status")!=200 || response.at("delivery_required")!=false || target.size()!=3 ||
        target.value("scope","")!="member" || !identity(target.value("device_id","")) ||
        !identity(target.value("boot_id","")) || !identity(response.at("session").get<std::string>()) ||
        (!expected_device.empty() && target.at("device_id")!=expected_device)) throw std::runtime_error("usb_identity_mismatch");
    hello_=strict_browser_json(response.at("body").get<std::string>());
    if (!hello_.is_object() || hello_.value("schema","")!="usb-management-discovery/1")
      throw std::runtime_error("unsupported_usb_management");
    target_=target; session_=response.at("session"); connected_=true; diagnostic_.clear(); return true;
  } catch (const std::exception &e) { fail(e.what()); return false; }
}
PicoHttpResponse UsbManagementClient::request_locked(const std::string &path,const std::string &method,
    const std::string &body,const std::string &revision,unsigned timeout,
    const std::function<void()> &response_observed) {
  if (!connected_) return {409,R"({"error":{"code":"usb_management_required"}})",{}};
  bool mutation=method!="GET";
  try {
    if (++next_id_>2147483647U) throw std::runtime_error("usb_session_exhausted");
    const auto deadline=now_()+std::min(timeout,10000U);
    auto response=exchange({{"schema","usb-management/1"},{"id",next_id_},{"operation","request"},
      {"session",session_},{"target",target_},{"method",method},{"path",path},{"body",body},{"if_match",revision}},deadline);
    if (response.at("session")!=session_ || response.at("target")!=target_ || response.at("delivery_required")!=true)
      throw std::runtime_error("usb_binding_mismatch");
    PicoHttpResponse result{response.at("status").get<unsigned>(),response.at("body").get<std::string>(),response.at("etag").get<std::string>()};
    // Time samples are bounded by the response, before its delivery ACK.
    // The ACK cannot move the instant already sampled on the Pico.
    if (response_observed) response_observed();
    auto ack=exchange({{"schema","usb-management/1"},{"id",next_id_},{"operation","ack"},
      {"session",session_},{"target",target_}},deadline);
    if (ack.at("session")!=session_ || ack.at("target")!=target_ || ack.at("status")!=200 || ack.at("delivery_required")!=false)
      throw std::runtime_error("usb_ack_unconfirmed");
    diagnostic_.clear(); return result;
  } catch (const std::exception &e) {
    fail(e.what());
    return {503,Json{{"error",{{"code",mutation?"management_outcome_unknown":"usb_unavailable"}}},{"mutation_attempted",mutation}}.dump(),{}};
  }
}
PicoHttpResponse UsbManagementClient::request(const std::string &path,const std::string &method,
    const std::string &body,const std::string &revision,unsigned timeout) {
  std::lock_guard lock(mutex_); return request_locked(path,method,body,revision,timeout);
}
bool UsbManagementClient::synchronize(const std::function<HostUtcObservation()> &observe) {
  std::lock_guard lock(mutex_);
  const auto reject=[&](const char *reason) { time_={{"source","usb-physical"},{"accepted",false},{"reason",reason},{"observed_ms",std::to_string(now_())}}; return false; };
  try {
    const auto before=observe();
    if (!before.qualified) return reject("host_utc_unqualified");
    HostUtcObservation after;
    std::uint64_t ack_started_ms{};
    const auto sample=request_locked("/api/usb/v1/time/sample","GET",{},{},500,[&] {
      after=observe(); ack_started_ms=now_();
    });
    const auto ack_finished_ms=now_();
    if (sample.status!=200) return reject("usb_sample_unavailable");
    if (!after.qualified || after.monotonic_ns<before.monotonic_ns || after.utc_ns<before.utc_ns)
      return reject("host_clock_changed");
    // Excluding ACK latency must not remove the clock-step/quality check
    // across that ACK. This observation validates the existing sample;
    // its later time is never used to recenter the captured UTC instant.
    const auto confirmed=observe();
    if (!confirmed.qualified || confirmed.monotonic_ns<after.monotonic_ns || confirmed.utc_ns<after.utc_ns)
      return reject("host_clock_changed");
    const auto elapsed=after.monotonic_ns-before.monotonic_ns;
    const auto wall_elapsed=after.utc_ns-before.utc_ns;
    const auto sample_step=elapsed>wall_elapsed?elapsed-wall_elapsed:wall_elapsed-elapsed;
    const auto ack_elapsed=confirmed.monotonic_ns-after.monotonic_ns;
    const auto ack_wall=confirmed.utc_ns-after.utc_ns;
    const auto ack_step=ack_elapsed>ack_wall?ack_elapsed-ack_wall:ack_wall-ack_elapsed;
    if (confirmed.monotonic_ns-before.monotonic_ns>1000000000ULL ||
        sample_step>1000000ULL || ack_step>1000000ULL || sample_step+ack_step>1000000ULL)
      return reject("usb_sample_delay_or_clock_step");
    const auto step=sample_step+ack_step;
    const auto value=strict_browser_json(sample.body);
    (void)decimal(value.at("monotonic_ns"));
    if (!value.at("sample").is_string()) return reject("invalid_time_sample");
    const auto error=std::max({before.uncertainty_ns,after.uncertainty_ns,confirmed.uncertainty_ns});
    // The Pico sample lies somewhere inside [before, after]. Its UTC
    // midpoint has a worst-case radius of ceil(RTT/2), even with fully
    // asymmetric delivery. Keep host error, observed wall/mono skew and
    // the existing 100us processing/drift margin; assume no symmetry.
    const auto radius=elapsed/2+elapsed%2;
    if (error>UINT64_MAX-radius-step-100000ULL) return reject("time_overflow");
    const auto uncertainty=error+radius+step+100000ULL;
    const auto utc=before.utc_ns+wall_elapsed/2;
    const auto result=request_locked("/api/usb/v1/time/submit","POST",Json{{"sample",value.at("sample")},
      {"utc_ns",std::to_string(utc)},{"uncertainty_ns",std::to_string(uncertainty)},{"leap","normal"}}.dump(),{},500);
    const auto outcome=strict_browser_json(result.body);
    const bool accepted=result.status==200 && outcome.is_object() && outcome.value("accepted",false);
    time_={{"source","usb-physical"},{"accepted",accepted},{"selected",outcome.value("selected",false)},
      {"selected_source",outcome.value("source",std::string{})},{"uncertainty_ns",std::to_string(uncertainty)},
      {"host_uncertainty_ns",std::to_string(error)},{"sample_round_trip_ns",std::to_string(elapsed)},
      {"sample_radius_ns",std::to_string(radius)},{"clock_discrepancy_ns",std::to_string(step)},
      {"post_ack_clock_discrepancy_ns",std::to_string(ack_step)},
      {"processing_margin_ns","100000"},{"sample_ack_ms",std::to_string(ack_finished_ms>=ack_started_ms?ack_finished_ms-ack_started_ms:0)},
      {"observed_ms",std::to_string(now_())},{"result",outcome}};
    return accepted;
  } catch (...) { return reject("invalid_time_exchange"); }
}
} // namespace wsprrypi
