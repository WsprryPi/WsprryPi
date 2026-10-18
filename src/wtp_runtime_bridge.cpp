// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp_runtime_bridge.hpp"
#include "wtp_integration/configuration.hpp"
#include "wtp_integration/target_runtime.hpp"
#include "json.hpp"
#include "wtp_integration/browser_api.hpp"
#include <openssl/crypto.h>
#include "wtp_integration/usb_cdc.hpp"
#include "wtp_integration/tls.hpp"
#include "wtp_integration/plain_tcp.hpp"
#include "pico_usb_installer.hpp"
#include "pico_usb_installer_state.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
namespace {
using NativeRuntime = wsprrypi::WtpTargetRuntime;
std::mutex operation_mutex, runtime_mutex;
std::shared_ptr<NativeRuntime> runtime;
std::shared_ptr<NativeRuntime> get() {
  std::lock_guard lock(runtime_mutex);
  return runtime;
}
std::shared_ptr<NativeRuntime> require() {
  auto r = get();
  if (!r)
    throw std::runtime_error("Pico backend is not selected");
  if (r->settings.transport == "usb" && wsprrypi::pico_usb_update_blocked(r->settings.usb_serial))
    throw std::runtime_error("Selected Pico is in firmware maintenance; finish or recover its update first");
  return r;
}
} // namespace
void set_wtp_runtime_for_test(WtpSettings settings,
                              wsprrypi::WtpScheduleClock &clock,
                              wsprrypi::wtp::ByteStream &stream,
                              wsprrypi::wtp::SessionOptions options,
                              std::function<bool()> reopen) {
  std::lock_guard operation(operation_mutex);
  std::lock_guard lock(runtime_mutex);
  if (runtime)
    throw std::logic_error("Test runtime must start unselected");
  runtime =
      std::make_shared<NativeRuntime>(std::move(settings), clock, stream,
                                      std::move(options), std::move(reopen));
}
std::string
wtp_runtime_selection_error(const std::optional<WtpSettings> &settings) {
  std::lock_guard operation(operation_mutex);
  auto r = get();
  try {
    if (settings) validate_wtp_settings(*settings, true);
    if (settings && !settings->device_id.empty() &&
        (!r || r->settings.device_id != settings->device_id) &&
        wsprrypi::wtp_output_identity_in_use(settings->device_id))
      return "This WTP output already has an independent fleet assignment";
    bool changed = r && (!settings || r->settings != *settings);
    if (settings && settings->transport == "network") {
      wsprrypi::TlsCredentials candidate({settings->hostname, settings->tls_identity,
          settings->tls_ca, settings->tls_certificate, settings->tls_key,
          static_cast<unsigned>(settings->tcp_port)});
      changed = changed || (r && (!r->credentials || !r->credentials->matches(candidate)));
    }
    if (r && changed && !r->app->replaceable())
      return "Resolve Pico ownership and output before changing transport, endpoint or credentials";
  } catch (const std::exception &error) { return error.what(); }
  return {};
}
void set_wtp_management_transports_for_test(wsprrypi::PicoManagementExchange engineering,
    wsprrypi::ConsumerRadioClient::Transport consumer) {
  std::lock_guard operation(operation_mutex);
  auto r = require();
  if (!r->app->replaceable()) throw std::logic_error("Test transport requires idle runtime");
  r->engineering_transport_for_test = std::move(engineering);
  r->consumer_transport_for_test = std::move(consumer);
}
nlohmann::json wtp_runtime_safe_probe(const std::function<nlohmann::json()> &probe) {
  std::lock_guard operation(operation_mutex);
  auto r = get();
  if (r && !r->app->replaceable())
    throw std::runtime_error("Resolve Pico ownership and output before identifying another device");
  return probe();
}
bool wtp_runtime_switch_allowed() noexcept {
  std::lock_guard operation(operation_mutex);
  auto r = get();
  return !r || r->app->replaceable();
}
void wtp_runtime_prepare_skip() {
  std::lock_guard operation(operation_mutex);
  require()->app->prepare_skip();
}
void select_wtp_runtime(const std::optional<WtpSettings> &settings) {
  std::lock_guard operation(operation_mutex);
  std::lock_guard lock(runtime_mutex);
  if (runtime && settings && runtime->settings == *settings &&
      runtime->credentials_current())
    return;
  if (runtime && !runtime->app->replaceable())
    throw std::runtime_error("Resolve Pico ownership and output before "
                             "changing the backend or endpoint");
  auto lease = runtime && settings && !settings->device_id.empty() && runtime->settings.device_id == settings->device_id
      ? runtime->output_lease_snapshot() : std::shared_ptr<void>{};
  const bool management_requires_selection = runtime && !runtime->management_carrier.empty() &&
      runtime->management_carrier != "engineering_mtls";
  runtime = settings ? std::make_shared<NativeRuntime>(*settings, std::function<bool()>{}, std::move(lease)) : nullptr;
  if (runtime && management_requires_selection) runtime->management_carrier = "disconnected";
}
bool wtp_runtime_selected() noexcept { return static_cast<bool>(get()); }
bool wtp_runtime_ready() noexcept {
  auto r = get();
  return r && !(r->settings.transport == "usb" && wsprrypi::pico_usb_update_blocked(r->settings.usb_serial)) && r->app->ready();
}
bool wtp_runtime_invalidate_for_reload() {
  std::lock_guard operation(operation_mutex);
  auto r = get();
  if (!r)
    return false;
  r->app
      ->invalidate(); // CAS decides whether waiting work or committed work won.
  const auto phase = r->app->phase();
  if (phase == wsprrypi::WtpSchedulePhase::Preparing ||
      phase == wsprrypi::WtpSchedulePhase::Executing)
    return true;
  (void)r->app->stop();
  return false;
}
bool wtp_runtime_defers_reload() noexcept {
  auto r = get();
  if (!r)
    return false;
  auto phase = r->app->status().phase;
  return phase == wsprrypi::WtpSchedulePhase::Preparing ||
         phase == wsprrypi::WtpSchedulePhase::Executing;
}
WsprTransmitState wtp_runtime_state() noexcept {
  auto r = get();
  if (!r)
    return WsprTransmitState::DISABLED;
  auto s = r->app->status();
  if (s.recovery_required || s.safety_fault || !r->app->ready())
    return WsprTransmitState::FAILED;
  if (s.job && s.job->state == wsprrypi::wtp::State::Running)
    return WsprTransmitState::TRANSMITTING;
  if (r->app->active() || r->app->skipping() ||
      s.phase != wsprrypi::WtpSchedulePhase::Idle)
    return WsprTransmitState::ENABLED;
  if (s.last_report) {
    if (s.last_report->outcome == wsprrypi::WtpScheduleOutcome::Complete)
      return WsprTransmitState::COMPLETE;
    if (s.last_report->outcome == wsprrypi::WtpScheduleOutcome::Cancelled)
      return WsprTransmitState::CANCELLED;
    if (s.last_report->outcome == wsprrypi::WtpScheduleOutcome::Failed)
      return WsprTransmitState::FAILED;
  }
  return WsprTransmitState::DISABLED;
}
wsprrypi::TransmissionMode wtp_runtime_mode() noexcept {
  auto r = get();
  return r ? r->app->mode() : wsprrypi::TransmissionMode::WSPR;
}
void wtp_runtime_prepare(wsprrypi::TransmissionRequest r) {
  std::lock_guard operation(operation_mutex);
  auto target = require();
  target->refresh_usb_time(true);
  target->app->prepare(std::move(r));
}
std::chrono::nanoseconds wtp_runtime_preparation_lead() {
  return std::chrono::nanoseconds(require()->app->preparation_lead_ns());
}
void wtp_runtime_start() {
  std::lock_guard operation(operation_mutex);
  require()->app->start();
}
wsprrypi::CleanupResult wtp_runtime_stop() {
  auto r = get();
  if (r) r->usb_cancelled = true; // Interrupt bounded USB I/O before waiting for operation authority.
  if (r && r->tls.opening()) r->tls.cancel();
  std::lock_guard operation(operation_mutex);
  r = get();
  auto result = r ? r->app->stop() : wsprrypi::CleanupResult{true, {}};
  if (r) r->usb_cancelled = false;
  return result;
}
wsprrypi::StartupQuiesceResult wtp_runtime_inspect() {
  std::lock_guard operation(operation_mutex);
  auto target = require();
  target->refresh_usb_time(true);
  return target->app->inspect();
}
void wtp_runtime_poll_usb_time() {
  std::unique_lock operation(operation_mutex,std::try_to_lock);
  if (!operation) return;
  auto r=get();
  if (r && r->settings.transport=="usb" && !wsprrypi::pico_usb_update_blocked(r->settings.usb_serial)) r->refresh_usb_time();
}
void wtp_runtime_poll_idle() {
  std::unique_lock operation(operation_mutex, std::try_to_lock);
  if (!operation) return;
  auto r = get();
  if (r && r->settings.transport=="usb" && wsprrypi::pico_usb_update_blocked(r->settings.usb_serial)) return;
  if (r && r->consumer_management) {
    if (!r->consumer_management->active()) r->consumer_management->clear();
    // This selected management path owns its authenticated context and needs
    // no background WTP reconnect. Starting a job still negotiates WTP normally.
    return;
  }
  if (r) (void)r->app->poll_idle();
}
wsprrypi::CleanupResult wtp_runtime_recover() {
  std::lock_guard operation(operation_mutex);
  auto r = get();
  if (!r) return {false, "Pico is not selected"};
  if (r->settings.transport=="usb" && wsprrypi::pico_usb_update_blocked(r->settings.usb_serial))
    return {false,"Finish or recover the selected Pico firmware update first"};
  auto result=r->app->recover();
  if (result.ok) {
    (void)r->reconcile_usb_binding();
    if (r->has_usb_binding_fault()) return {false,"USB device identity changed. Reconcile matching USB control channels before another job."};
  }
  return result;
}
std::string wtp_runtime_json() {
  auto r = get();
  if (!r)
    return R"({"selected":false})";
  auto j = nlohmann::json::parse(
      wsprrypi::wtp_runtime_status_json(r->app->status()));
  j["now_ms"] = std::to_string(r->clock.now_ms());
  j["selected"] = true;
  j["transport"] = r->settings.transport;
  if (r->settings.transport=="usb") j["firmware_maintenance"]=wsprrypi::pico_usb_update_blocked(r->settings.usb_serial);
  if (r->settings.transport == "network" || r->settings.transport == "network_plain") {
    const auto n = r->settings.transport == "network" ? r->tls.observation()
                                                       : r->plain.observation();
    j["network"] = {{"hostname", r->settings.hostname}, {"port", r->settings.tcp_port},
        {"expected_identity", r->settings.transport == "network"
            ? wsprrypi::canonical_network_identity(r->settings.tls_identity.empty() ? r->settings.hostname : r->settings.tls_identity).value_or("") : ""},
        {"resolved_address", n.address}, {"authenticated_identity", n.authenticated_identity},
        {"security", r->settings.transport == "network" ? "tls" : "plain_lan"},
        {"state", n.state}, {"diagnostic", n.diagnostic}, {"observed_ms", std::to_string(n.observed_ms)}};
  }
  if (r->settings.transport == "usb") {
    j["usb"] = r->usb_status();
    j["usb_serial"]=r->settings.usb_serial;
    std::unique_lock selection_lock(operation_mutex,std::try_to_lock);
    if(selection_lock.owns_lock()) j["firmware_selection_id"]=r->management_selection_id;
    const auto &name = j["usb"]["hostname"];
    if (name.is_object()) { j["common_name"] = name.value("saved",""); j["name_pending_restart"] = name.value("pending_restart",false);
      j["common_name_confirmed"] = j["usb"].value("name_confirmed",false) &&
          j["usb"]["management"].is_object() && j["usb"]["management"].value("connected",false); }
  }
  j["worker_active"] = r->app->active();
  j["host_skip_waiting"] = r->app->skipping();
  const bool binding_fault=r->has_usb_binding_fault();
  j["ready"] = r->app->ready() && !binding_fault;
  if (binding_fault) j["diagnostic"]="USB device identity changed. Reconcile the Pico before another job.";
  j["management_available"] = r->app->replaceable(); // Local admission only; remote authentication is still required.
  j["host_utc_valid"] = wsprrypi::wtp_host_utc_valid();
  return j.dump();
}
std::optional<wsprrypi::WtpScheduleReport> wtp_runtime_completion() {
  auto r = get();
  return r ? r->app->take_completion() : std::nullopt;
}

wsprrypi::CleanupResult wtp_runtime_cancel_job(const std::string &job_id) {
  auto candidate = get();
  if (candidate) {
    const auto current=candidate->app->status();
    if (current.job_id == job_id && (current.owns || current.phase == wsprrypi::WtpSchedulePhase::Waiting))
      candidate->usb_cancelled = true;
  }
  std::lock_guard operation(operation_mutex);
  struct CancelReset { std::shared_ptr<NativeRuntime> owner; ~CancelReset() { if(owner) owner->usb_cancelled=false; } } reset{candidate};
  auto r = get();
  if (!r) return {false, "Pico is not selected"};
  const auto status = r->app->status();
  if (job_id.empty() || status.job_id != job_id ||
      (!status.owns && status.phase != wsprrypi::WtpSchedulePhase::Waiting))
    return {false, "Only the current host-owned job may be cancelled"};
  return r->app->stop();
}

wsprrypi::PicoHttpResponse wtp_runtime_firmware(const std::string &method, const std::string &body) {
  using nlohmann::json;
  static wsprrypi::PicoUsbInstallerTask task;
  const auto failure=[](unsigned code,const std::string& reason) {
    return wsprrypi::PicoHttpResponse{code,json{{"error",{{"message",reason}}}}.dump(),{}};
  };
  auto r=get();
  if (!r || r->settings.transport!="usb" || !wsprrypi::pico_usb_serial(r->settings.usb_serial))
    return failure(409,"Select a Pico connected to this Pi by USB first.");
  const auto in_progress=task.status();
  if(method=="GET" && in_progress.value("running",false) &&
      in_progress.value("bound_serial","")==r->settings.usb_serial) {
    auto state=wsprrypi::pico_usb_public_task(in_progress);
    state["serial"]=in_progress.at("bound_serial"); state["selection_id"]=in_progress.at("bound_selection");
    state["device_id"]=in_progress.at("bound_device"); state["installed_version"]=nullptr;
    state["maintenance"]=wsprrypi::pico_usb_update_blocked(r->settings.usb_serial);
    state["default_branch"]="main";
    return {200,state.dump(),{}};
  }
  std::unique_lock selection_lock(operation_mutex,std::try_to_lock);
  if(!selection_lock.owns_lock()) return failure(409,"The selected Pico is busy; read firmware status again shortly.");
  const auto identity=r->app->status().identity;
  const std::string serial=r->settings.usb_serial, selection=r->management_selection_id;
  const auto public_status=[&] {
    auto state=wsprrypi::pico_usb_public_task(task.status());
    if (state.value("bound_serial",serial)!=serial) state={{"running",false},{"result",nullptr}};
    state["serial"]=serial; state["selection_id"]=selection;
    state["device_id"]=identity?json(identity->device_id):json(nullptr);
    state["installed_version"]=identity?json(identity->firmware_version):json(nullptr);
    state["maintenance"]=wsprrypi::pico_usb_update_blocked(serial);
    state["default_branch"]="main";
    if(state["maintenance"]==true) {
      try { const auto hold=wsprrypi::pico_usb_read_hold(wsprrypi::pico_usb_hold_path(serial));
        state["recovery_plan_id"]=hold.at("plan_id"); state["recovery_version"]=hold.at("version");
      } catch(...) { state["recovery_record_unavailable"]=true; }
    }
    return state;
  };
  if (method=="GET") return {200,public_status().dump(),{}};
  if (method!="POST") return failure(405,"Unsupported firmware operation.");
  if (body.size()>2048) return failure(413,"Firmware request is too large.");
  try {
    const auto input=wsprrypi::strict_browser_json(body);
    if (!input.is_object() || input.value("selection_id","")!=selection)
      return failure(409,"Read the selected USB Pico again before requesting firmware work.");
    const auto action=input.at("action").get<std::string>();
    const auto entry=std::filesystem::path("/usr/local/lib/wsprrypi/pico-usb/entry.py");
    if (!wsprrypi::pico_usb_entry_protected(entry))
      return failure(409,"The shared Pico USB installer is missing or not root-protected. Update the Pi package first.");
    const std::vector<std::string> base{"/usr/bin/python3","-I",entry.string()};
    const auto marker=wsprrypi::pico_usb_hold_path(serial);
    if (action=="plan") {
      if (!identity || wsprrypi::pico_usb_update_blocked(serial))
        return failure(409,"Reconcile the selected Pico or recover its outstanding firmware transaction first.");
      for (auto field=input.begin();field!=input.end();++field)
        if(field.key()!="action" && field.key()!="selection_id" && field.key()!="branch" && field.key()!="version" && field.key()!="stable")
          return failure(400,"Unsupported firmware plan field.");
      const std::string branch=input.value("branch",std::string("main"));
      if (branch!="main" && branch!="devel") return failure(400,"Select main or devel explicitly.");
      auto args=base;
      args.insert(args.end(),{"plan","--branch",branch});
      if(input.contains("version")) args.insert(args.end(),{"--version",input.at("version").get<std::string>()});
      if(input.contains("stable")) { if(input.at("stable")!=true || input.contains("version")) return failure(400,"Select one image source."); args.push_back("--stable"); }
      const auto plan=wsprrypi::wtp_random_identity();
      const auto destination=std::filesystem::path("/var/cache/wsprrypi/pico-usb")/plan;
      args.insert(args.end(),{"--output",destination.string()});
      const bool started=task.start([args,plan] {
        try { auto result=wsprrypi::pico_usb_installer_process(args); result["plan_id"]=plan; return result; }
        catch(const std::exception& error) { return json{{"ok",false},{"error",error.what()},{"recovery_required",false}}; }
      },{{"operation","plan"},{"plan_id",plan},{"branch",branch},{"bound_serial",serial},
         {"bound_device",identity->device_id},{"bound_selection",selection}});
      return started?wsprrypi::PicoHttpResponse{202,public_status().dump(),{}}:failure(409,"Firmware work is already running.");
    }
    if (input.size()!=4 || input.at("confirmed")!=true || (action!="upgrade" && action!="recover"))
      return failure(400,"A firmware operation requires an explicit confirmed plan.");
    const auto planned=task.status();
    const auto plan=input.at("plan_id").get<std::string>();
    json expected;
    std::filesystem::path bundle;
    std::string branch;
    if(action=="upgrade") {
      if (!identity || planned.value("running",false) || planned.value("operation","")!="plan" ||
          planned.value("plan_id","")!=plan || planned.value("bound_serial","")!=serial ||
          planned.value("bound_device","")!=identity->device_id || planned.value("bound_selection","")!=selection ||
          !planned.at("result").value("ok",false) || wsprrypi::pico_usb_update_blocked(serial))
        return failure(409,"Download and review a fresh plan for this selected USB Pico.");
      branch=planned.at("branch").get<std::string>(); expected=planned.at("result");
      expected["device_id"]=identity->device_id; expected["plan_id"]=plan; expected["serial"]=serial;
      bundle=std::filesystem::path("/var/cache/wsprrypi/pico-usb")/plan;
    } else {
      expected=wsprrypi::pico_usb_read_hold(marker);
      if(expected.at("serial")!=serial || expected.at("plan_id")!=plan)
        return failure(409,"Read the outstanding transaction for this selected Pico first.");
      branch=expected.at("branch").get<std::string>();
      if(!expected.contains("transaction") && !expected.value("verified",false))
        return failure(409,"Host interruption needs administrator recovery with the private transaction record. Keep this Pico paused.");
    }
    const bool started=task.start([r,serial,selection,branch,base,bundle,expected,marker,action]() mutable {
      std::lock_guard operation(operation_mutex);
      if (get()!=r || r->management_selection_id!=selection)
        throw std::runtime_error("Selected Pico changed; read its firmware status again.");
      json result={{"ok",false},{"error","Pico is busy or ownership is unresolved."},{"recovery_required",false}};
      const auto run=[&] {
        std::lock_guard connection(r->usb_connection_mutex);
        if(action=="upgrade") { expected["branch"]=branch; wsprrypi::pico_usb_save_hold(marker,expected,true); }
        if (r->usb_management) r->usb_management->close();
        r->management_usb.close(); r->usb_unavailable(); r->updater_lease.reset();
        if(action=="recover" && expected.value("verified",false)) result={{"ok",true},{"stage","complete"},{"serial",serial},{"source",expected.at("source")},{"version",expected.at("version")},{"target",expected.at("verified_target")}};
        else {
          auto args=base;
          if(action=="recover") args.insert(args.end(),{"recover","--transaction",expected.at("transaction").get<std::string>()});
          else args.insert(args.end(),{"upgrade","--branch",branch,"--bundle",bundle.string()});
          args.insert(args.end(),{"--device",serial,"--board","pico2_w","--yes","--state-dir","/var/lib/wsprrypi/pico-usb"});
          result=wsprrypi::pico_usb_installer_process(args);
        }
        if(result.contains("transaction")) expected["transaction"]=result.at("transaction");
        if(result.value("ok",false)) {
          if(result.value("stage","")!="complete" || result.value("serial","")!=serial ||
              result.value("source","")!=expected.at("source").get<std::string>() || result.value("version","")!=expected.at("version").get<std::string>())
            throw std::runtime_error("Shared installer did not verify the reviewed Pico image.");
          const auto& target=result.at("target");
          const auto boot=target.at("boot_id").get<std::string>();
          if(target.value("scope","")!="member" || target.at("device_id")!=expected.at("device_id") ||
              boot.size()!=32 || boot.find_first_not_of("0123456789abcdef")!=std::string::npos)
            throw std::runtime_error("Shared installer target binding is invalid.");
          expected["verified"]=true;
          expected["verified_target"]=result.at("target");
        }
        wsprrypi::pico_usb_save_hold(marker,expected);
      };
      const bool admitted=action=="recover"?(run(),true):r->app->idle_management(run,false,false);
      if (!admitted) return result;
      if (result.value("ok",false) || !result.value("recovery_required",true)) {
        // Keep the durable marker blocking every scheduler/frontend until
        // only this serialized recovery path verifies the new USB session.
        r->verified_update_reconnect=true;
        bool reconnected=false;
        try {
          reconnected=r->app->inspect().ok && r->reconcile_usb_binding();
          const auto observed=r->app->status().identity;
          reconnected=reconnected && observed && observed->device_id==expected.at("device_id").get<std::string>() &&
              (!result.value("ok",false) || (observed->firmware_version==expected.at("version").get<std::string>() &&
               observed->boot_id==expected.at("verified_target").at("boot_id").get<std::string>()));
          r->verified_update_reconnect=false;
        } catch(...) { r->verified_update_reconnect=false; throw; }
        if(reconnected) wsprrypi::pico_usb_remove_hold(marker);
        else {
          result["ok"]=false; result["recovery_required"]=true;
          result["error"]="Pi reconnection remains unconfirmed. Keep this Pico in maintenance and retry recovery.";
        }
      }
      return result;
    },{{"operation",action},{"plan_id",plan},{"branch",branch},{"bound_serial",serial},
       {"bound_device",expected.at("device_id")},{"bound_selection",selection}});
    return started?wsprrypi::PicoHttpResponse{202,public_status().dump(),{}}:failure(409,"Firmware work is already running.");
  } catch (const std::exception& error) { return failure(400,error.what()); }
}

wsprrypi::PicoHttpResponse wtp_runtime_management(const std::string &resource,
    const std::string &method, const std::string &body, const std::string &revision,
    const std::string &expected_selection) {
  std::unique_lock operation(operation_mutex, std::try_to_lock);
  if (!operation.owns_lock()) return {409, R"({"error":{"code":"busy"}})", {}};
  auto r = get();
  if (!r) return {409, R"({"error":{"code":"member_not_selected"}})", {}};
  if(method!="GET" && r->settings.transport=="usb" && wsprrypi::pico_usb_update_blocked(r->settings.usb_serial))
    return {409,R"({"error":{"code":"firmware_maintenance"}})",{}};
  if (!expected_selection.empty() && expected_selection != r->management_selection_id)
    return {409,R"({"error":{"code":"member_read_required"}})",{}};
  wsprrypi::PicoHttpResponse response;
  try {
    const auto failure = [](unsigned status, const char* code) {
      return wsprrypi::PicoHttpResponse{status, nlohmann::json{{"error", {{"code", code}}}}.dump(), {}};
    };
    if (resource == "member/management") {
      if (method == "GET") {
        std::lock_guard connection(r->usb_connection_mutex);
        const bool connected = (r->consumer_management && r->consumer_management->active()) ||
            (r->management_carrier == "usb_physical" && r->usb_management && r->usb_management->connected());
        return {200, nlohmann::json{{"carrier", r->management_carrier.empty()
            ? (r->settings.transport == "network" ? "engineering_mtls" : r->settings.transport == "usb" ? "usb_physical" : "unsupported") : r->management_carrier},
            {"connected", connected}, {"host", r->management_host}, {"selection_id",r->management_selection_id},
            {"session",r->consumer_management ? r->consumer_management->lost_session() : nlohmann::json(nullptr)}}.dump(), {}};
      }
      if (method != "POST" || body.size() > 512) return failure(400, "invalid_management_selection");
      auto input = wsprrypi::strict_browser_json(body);
      auto* credential = input.is_object() && input.contains("password") && input["password"].is_string()
          ? &input["password"].get_ref<std::string&>() : nullptr;
      struct CredentialWipe { std::string* s; ~CredentialWipe() { if (s) OPENSSL_cleanse(s->data(), s->size()); } } credential_wipe{credential};
      if (!input.is_object() || !input.contains("action") || !input["action"].is_string())
        return failure(400, "invalid_management_selection");
      const auto action = input["action"].get<std::string>();
      if (action == "usb_physical") {
        if (input.size() != 1 || r->settings.transport != "usb") return failure(409,"usb_transport_required");
        if (!r->app->idle_management([&] {
          if (!r->ensure_usb_management()) { response=failure(409,"usb_management_unavailable"); return; }
          if (r->consumer_management) r->consumer_management->logout();
          r->consumer_management.reset(); r->management_host.clear();
          r->management_carrier="usb_physical"; r->management_selection_id=wsprrypi::wtp_random_identity();
          response={200,R"({"ok":true})",{}};
        },true)) return failure(409,"host_busy_or_output_unresolved");
        return response;
      }
      if (action == "inspect_logins" || action == "retire_login") {
        const bool retiring = action == "retire_login";
        if (input.size() != (retiring ? 6U : 3U) || !input.contains("carrier") || !input["carrier"].is_string() ||
            !input.contains("host") || !input["host"].is_string() ||
            (retiring && (!credential || !input.contains("scope") || !input.contains("selection_id") ||
                input["selection_id"] != r->management_selection_id))) return failure(400,"invalid_management_selection");
        const auto carrier = input["carrier"].get<std::string>(), host = input["host"].get<std::string>();
        const auto canonical = wsprrypi::canonical_network_identity(host);
        if (!canonical || *canonical != host || host.find(':') != std::string::npos ||
            (carrier != "consumer_ap" && carrier != "consumer_station") ||
            ((carrier == "consumer_ap") != (host == "192.168.4.1")) || r->settings.device_id.empty())
          return failure(400,"invalid_consumer_endpoint");
        if (!r->app->idle_management([&] {
          wsprrypi::ConsumerRadioClient proof(
              [owner=r.get(),host,&input,retiring](const auto& path,const auto& verb,const auto& payload,const auto& media) {
                // Invalidate self traffic only once the fully prepared proof
                // may take effect. Invalid input/start failures keep it usable.
                if (retiring && path == "/api/consumer/v1/recovery/finish" && owner->consumer_management) {
                  const auto &selected = owner->consumer_management->lost_session();
                  if (selected.is_object() && selected.value("selector", "") == input["scope"].value("target_selector", "") &&
                      selected.value("device_id", "") == input["scope"].value("device_id", "") &&
                      selected.value("boot_id", "") == input["scope"].value("boot_id", "")) owner->consumer_management->clear();
                }
                if (owner->consumer_transport_for_test) return owner->consumer_transport_for_test(path,verb,payload,media);
                return wsprrypi::consumer_http_request(owner->management_tcp,[owner]{return owner->clock.now_ms();},host,path,verb,payload,media);
              },[owner=r.get()]{return owner->clock.now_ms();});
          response = retiring ? proof.retire(r->settings.device_id,carrier,host,*credential,input["scope"])
                              : proof.recovery(r->settings.device_id,carrier,host);
          if (response.status == 200) {
            auto result = wsprrypi::strict_browser_json(response.body);
            if (retiring) {
              // The caller reviewed one exact scope; do not replace or reconnect.
              // A selected native session may have been retired; read its state
              // explicitly before any subsequent operation.
              if (r->consumer_management) {
                const auto &selected = r->consumer_management->lost_session();
                if (selected.is_object() && selected.value("selector", "") == input["scope"].value("target_selector", "") &&
                    selected.value("boot_id", "") == input["scope"].value("boot_id", "")) r->consumer_management->clear();
              }
              r->management_selection_id = wsprrypi::wtp_random_identity();
            }
            result["selection_id"] = r->management_selection_id;
            response.body = result.dump();
          }
        },true)) return failure(409,"host_busy_or_output_unresolved");
        return response;
      }
      if (action == "connect") {
        if (input.size() != 4 || !input.contains("carrier") || !input["carrier"].is_string() ||
            !input.contains("host") || !input["host"].is_string() ||
            !input.contains("password") || !input["password"].is_string()) return failure(400, "invalid_management_selection");
        const auto carrier = input["carrier"].get<std::string>(), host = input["host"].get<std::string>();
        const auto canonical = wsprrypi::canonical_network_identity(host);
        if ((carrier != "consumer_station" && carrier != "consumer_ap") ||
            !canonical || *canonical != host || host.find(':') != std::string::npos ||
            (carrier == "consumer_ap" && host != "192.168.4.1") ||
            (carrier == "consumer_station" && host == "192.168.4.1") || r->settings.device_id.empty())
          return failure(400, "invalid_consumer_endpoint");
        if (r->consumer_management && r->consumer_management->active())
          return failure(409, "management_session_already_selected");
        // Erase this parse's credential storage on every exit, including exceptions.
        auto& password = input["password"].get_ref<std::string&>();

        if (!r->app->idle_management([&] {
          r->management_carrier = carrier; r->management_host = host;
          r->management_selection_id = wsprrypi::wtp_random_identity();
          r->consumer_management = std::make_unique<wsprrypi::ConsumerRadioClient>(
              [owner = r.get(), host](const auto& path, const auto& verb, const auto& payload, const auto& media) {
                if (owner->consumer_transport_for_test) return owner->consumer_transport_for_test(path,verb,payload,media);
                return wsprrypi::consumer_http_request(owner->management_tcp, [owner] { return owner->clock.now_ms(); },
                    host, path, verb, payload, media);
              }, [owner = r.get()] { return owner->clock.now_ms(); });
          response = r->consumer_management->login(r->settings.device_id, carrier, password);
        }, true)) return failure(409, "host_busy_or_output_unresolved");
        return response;
      }
      if ((action != "disconnect" && action != "engineering_mtls") || input.size() != 1)
        return failure(400, "invalid_management_selection");
      if (action == "engineering_mtls" && r->settings.transport != "network")
        return failure(409, "network_transport_required");
      if (!r->app->idle_management([&] {
        if (r->consumer_management) r->consumer_management->logout();
        r->consumer_management.reset(); r->management_host.clear();
        std::lock_guard connection(r->usb_connection_mutex);
        if (r->usb_management) r->usb_management->close();
        r->usb_unavailable();
        r->management_selection_id = wsprrypi::wtp_random_identity();
        r->management_carrier = action == "engineering_mtls" ? "engineering_mtls" : "disconnected";
      }, true)) return failure(409, "host_busy_or_output_unresolved");
      return {200, R"({"ok":true})", {}};
    }
    if (r->management_carrier == "usb_physical") {
      if (!r->app->idle_management([&] {
        std::lock_guard connection(r->usb_connection_mutex);
        if (!r->ensure_usb_management()) { response=failure(409,"usb_management_unavailable"); return; }
        const auto exchange = [&](const std::string &path,const std::string &verb,const std::string &payload,const std::string &etag) {
          return r->usb_management->request(path.starts_with("/") ? path : "/api/v1/" + path,verb,payload,etag);
        };
        if (resource == "member/configuration") response = wsprrypi::pico_configuration_request(exchange,
          r->usb_management->device(),r->usb_management->boot(),method,body,revision,"usb_physical");
        else if (resource == "member/application" || resource == "member/station" || resource == "member/hardware" ||
            resource == "member/gpio-drive" || resource == "member/gpio-drive-progress" || resource == "member/pins" || resource == "member/pins/validate") response = wsprrypi::pico_resource_request(exchange,
          r->usb_management->device(),r->usb_management->boot(),resource.substr(7),method,body,revision,"usb_physical");
        else if (resource.starts_with("member/progress")) response = exchange("/api/v1/"+resource.substr(7),method,body,revision);
        else if (resource == "member/wifi") response = exchange("/api/usb/v1/wifi",method,body,revision);
        else if (resource == "member/hostname") { response = exchange("/api/v1/hostname",method,body,revision); r->observe_usb_name(response); }
        else if (resource == "member/ap") response = exchange("/api/usb/v1/ap",method,body,revision);
        else if (resource == "member/restart") response = exchange("/api/v1/restart",method,body,revision);
        else response = exchange(resource,method,body,revision);
      },true)) return failure(409,"host_busy_or_output_unresolved");
      return response;
    }
    const bool consumer = r->management_carrier == "consumer_station" || r->management_carrier == "consumer_ap";
    if (consumer) {
      if (!r->consumer_management || !r->consumer_management->active()) return failure(401, "consumer_login_required");
      if (!r->app->idle_management([&] {
        const auto exchange = [&](const auto& path, const auto& verb, const auto& payload, const auto& etag) {
          return r->consumer_management->request(path, verb, payload, etag);
        };
        if (resource == "member/configuration")
          response = wsprrypi::pico_configuration_request(exchange, r->consumer_management->device(),
              r->consumer_management->boot(), method, body, revision, r->management_carrier,
              r->consumer_management->generation());
        else if (resource == "member/application" || resource == "member/station" || resource == "member/hardware" ||
            resource == "member/gpio-drive" || resource == "member/gpio-drive-progress" || resource == "member/pins" || resource == "member/pins/validate")
          response = wsprrypi::pico_resource_request(exchange,r->consumer_management->device(),r->consumer_management->boot(),
              resource.substr(7),method,body,revision,r->management_carrier,r->consumer_management->generation());
        else response = exchange(resource, method, body, revision);
      }, true)) return failure(409, "host_busy_or_output_unresolved");
      return response;
    }
    if ((!r->management_carrier.empty() && r->management_carrier != "engineering_mtls") || r->settings.transport != "network")
      return failure(409, "management_carrier_required");
    if (!r->credentials_current())
      return {409, R"({"error":{"code":"credentials_changed_reload_required"}})", {}};
    if (!r->app->idle_management([&] {
      const auto exchange = [&](const std::string &path, const std::string &verb,
          const std::string &payload, const std::string &etag) {
        if (r->engineering_transport_for_test) return r->engineering_transport_for_test(path,verb,payload,etag);
        return wsprrypi::pico_http_request(r->tls, r->network_selection(), r->credentials,
            [&] { return r->clock.now_ms(); }, path, verb, payload, etag);
      };
      if (resource == "member/configuration") {
        const auto identity = r->app->status().identity;
        response = wsprrypi::pico_configuration_request(exchange,
            identity ? identity->device_id : "", identity ? identity->boot_id : "",
            method, body, revision);
      } else if (resource == "member/application" || resource == "member/station" || resource == "member/hardware" ||
          resource == "member/gpio-drive" || resource == "member/gpio-drive-progress" || resource == "member/pins" || resource == "member/pins/validate") {
        const auto identity = r->app->status().identity;
        response = wsprrypi::pico_resource_request(exchange,identity ? identity->device_id : "",identity ? identity->boot_id : "",
            resource.substr(7),method,body,revision);
      } else response = exchange(resource, method, body, revision);
    }, false, !(method == "PUT" && (resource == "member/station" || resource == "member/hardware" || resource == "member/gpio-drive" || resource == "member/gpio-drive-progress"))))
      return {409, R"({"error":{"code":"host_busy_or_output_unresolved"}})", {}};
  } catch (...) { return {503, R"({"error":{"code":"management_unavailable"}})", {}}; }
  return response;
}
