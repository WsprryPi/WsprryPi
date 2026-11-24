// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp_runtime_bridge.hpp"
#include "wtp_managed_output.hpp"
#include "wtp_integration/management_identity.hpp"
#include "pico_usb_runtime_ownership.hpp"
#include <mutex>
#include <thread>
#include <stdexcept>
namespace {
std::mutex operation_mutex;
struct ManagedGate {std::string token;std::thread::id thread;};
std::optional<ManagedGate> managed_gate;
bool stop_reserved{};
constexpr auto retired = "Migration required: WTP transmitter backend is retired";
}
// No outgoing execution owner exists. Preserve the short reservation gate
// shared with native settings/STOP so their atomic observation remains valid.
std::optional<wsprrypi::WtpManagedOutput> wsprrypi::wtp_runtime_managed_output() {
  std::unique_lock operation(operation_mutex,std::try_to_lock);
  if(!operation.owns_lock())return {};
  WtpManagedOutput result;
  result.output.generation=1;
  result.cancellable=true;
  result.stop_reserved=stop_reserved;
  const auto token=wtp_random_identity();
  const auto release=[token]{
    std::lock_guard lock(operation_mutex);
    if(managed_gate&&managed_gate->token==token){managed_gate.reset();stop_reserved=false;}
  };
  if(!managed_gate&&!wtp_runtime_firmware_busy()){
    result.reserve_configuration=[token]{
      std::unique_lock lock(operation_mutex,std::try_to_lock);
      if(!lock.owns_lock()||managed_gate||wtp_runtime_firmware_busy())return false;
      managed_gate=ManagedGate{token,std::this_thread::get_id()};return true;
    };
    result.release_configuration=release;
    result.reserve_stop=[token,expected=result.output](const ManagedOutputObservation &observed){
      std::unique_lock lock(operation_mutex,std::try_to_lock);
      if(!lock.owns_lock()||managed_gate||wtp_runtime_firmware_busy()||observed!=expected)return false;
      managed_gate=ManagedGate{token,std::this_thread::get_id()};stop_reserved=true;return true;
    };
    result.release_stop=release;
  }
  return result;
}
std::string wtp_runtime_selection_error(const std::optional<WtpSettings> &settings){
  if(settings)return retired;
  std::lock_guard lock(operation_mutex);
  return managed_gate&&managed_gate->thread!=std::this_thread::get_id()?"management_busy":"";
}
void select_wtp_runtime(const std::optional<WtpSettings> &settings){if(settings)throw std::runtime_error(retired);}
bool wtp_runtime_selected() noexcept{return false;}
bool wtp_runtime_ready() noexcept{return false;}
bool wtp_runtime_firmware_busy() noexcept{return wsprrypi::pico_usb_any_update_blocked();}
bool wtp_runtime_invalidate_for_reload(){std::lock_guard lock(operation_mutex);return managed_gate.has_value();}
bool wtp_runtime_defers_reload() noexcept{return false;}
std::chrono::nanoseconds wtp_runtime_preparation_lead(){throw std::runtime_error(retired);}
void wtp_runtime_poll_usb_time(){} // The independent management owner supplies UTC.
void wtp_runtime_poll_idle(){}
wsprrypi::CleanupResult wtp_runtime_recover(){return {false,retired};}
std::string wtp_runtime_json(){return nlohmann::json{{"selected",false},{"firmware_maintenance",wtp_runtime_firmware_busy()}}.dump();}
wsprrypi::PicoHttpResponse wtp_runtime_management(const std::string &,const std::string &,const std::string &,const std::string &,const std::string &){
  return {409,R"({"error":{"code":"member_not_selected"}})",{}};
}
wsprrypi::PicoHttpResponse wtp_runtime_firmware(const std::string &,const std::string &){
  return {409,R"({"error":{"message":"Select a Pico connected to this Pi by USB first."}})",{}};
}
wsprrypi::CleanupResult wtp_runtime_cancel_job(const std::string &){return {false,retired};}
nlohmann::json wtp_runtime_safe_probe(const std::function<nlohmann::json()> &probe){std::lock_guard lock(operation_mutex);return probe();}
bool wtp_runtime_switch_allowed() noexcept{std::lock_guard lock(operation_mutex);return !managed_gate;}
