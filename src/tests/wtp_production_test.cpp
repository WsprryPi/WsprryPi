// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "arg_parser.hpp"
#include "non_wspr_request_builder.hpp"
#include <limits>
#include "scheduling.hpp"
#include "config_handler.hpp"
#include "runtime_config_bridge.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "wtp_backend_peer.hpp"
#include "wtp_runtime_bridge.hpp"
#include "wtp_settings_json.hpp"
#include "wtp_integration/target_runtime.hpp"
#include "web_server_routes.hpp"
#include "httplib.hpp"
#include "backend_http_guard.hpp"
#include <fstream>
#include <filesystem>
#include <future>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
using namespace std::chrono_literals;
using backend_test::checks;
namespace {
struct Clock : wsprrypi::WtpScheduleClock {
  backend_test::Peer peer;
  std::uint64_t now_ms() const override { return peer.now; }
  std::optional<std::uint64_t> utc_now_ns() const override {
    return peer.utc_base + peer.mono();
  }
  void wait_ms(std::uint64_t n) override { peer.advance(peer.now + n); }
};
// Optional USB management storage is allowed to disappear during a maximum
// WTP LOAD. The physical CDC remains present; the old logical session is silent.
struct ReclaimedManagement : wsprrypi::wtp::ByteStream {
  std::vector<std::uint8_t> input;
  std::deque<std::uint8_t> output;
  unsigned opens{}, name_reads{}, time_submits{}, config_mutations{};
  bool reclaimed{}, closed{};
  std::string session;
  nlohmann::json target{{"scope","member"},{"device_id",backend_test::device},{"boot_id",backend_test::boot}};
  void open() { ++opens; reclaimed=closed=false;input.clear();output.clear();session=std::string(32,opens==1?'a':'b'); }
  wsprrypi::wtp::IoResult write(std::span<const std::uint8_t> bytes) override {
    if(closed)return {wsprrypi::wtp::IoState::Closed,0};
    input.insert(input.end(),bytes.begin(),bytes.end());
    if(input.size()>=8) {
      const auto length=(std::uint32_t(input[4])<<24)|(std::uint32_t(input[5])<<16)|(std::uint32_t(input[6])<<8)|input[7];
      if(input.size()==8+length) {
        const auto request=nlohmann::json::parse(input.begin()+8,input.end());input.clear();
        if(!reclaimed) {
          const auto operation=request.at("operation").get<std::string>();
          std::string body="{}";
          if(operation=="hello")body=R"({"schema":"usb-management-discovery/1"})";
          if(operation=="request") {
            const auto path=request.at("path").get<std::string>();
            if(path=="/api/v1/hostname") {++name_reads;body=nlohmann::json{{"schema","transmitter-hostname/1"},{"target",target},{"saved","pico.local"}}.dump();}
            else if(path=="/api/usb/v1/time/sample")body=R"({"sample":"fresh","monotonic_ns":"1000000"})";
            else if(path=="/api/usb/v1/time/submit") {++time_submits;body=R"({"accepted":true,"selected":true,"source":"controller"})";}
            else if(request.at("method")!="GET")++config_mutations;
          }
          const auto response=nlohmann::json{{"schema","usb-management/1"},{"id",request.at("id")},{"status",200},
            {"session",session},{"target",target},{"body",body},{"etag","\"r1\""},{"delivery_required",operation=="request"}}.dump();
          std::vector<std::uint8_t> frame{'U','M','P','1',std::uint8_t(response.size()>>24),std::uint8_t(response.size()>>16),std::uint8_t(response.size()>>8),std::uint8_t(response.size())};
          frame.insert(frame.end(),response.begin(),response.end());output.insert(output.end(),frame.begin(),frame.end());
        }
      }
    }
    return {wsprrypi::wtp::IoState::Progress,bytes.size()};
  }
  wsprrypi::wtp::IoResult read(std::span<std::uint8_t> bytes) override {
    auto count=std::min(bytes.size(),output.size());
    for(std::size_t i=0;i<count;++i){bytes[i]=output.front();output.pop_front();}
    return {count?wsprrypi::wtp::IoState::Progress:wsprrypi::wtp::IoState::WouldBlock,count};
  }
  void close() noexcept override {closed=true;}
};
void usb_management_reclaimed_at_load() {
  Clock clock;ReclaimedManagement management;
  WtpSettings settings{"/dev/ttyACM1","test-serial",backend_test::device,0xcafe,0x4012,1000};
  wsprrypi::WtpTargetRuntime target(settings,clock,clock.peer,
      {backend_test::sid,backend_test::owner_id,backend_test::device},[&]{clock.peer.open();return true;});
  target.usb_management_for_test=[&] {
    management.open();
    return std::make_unique<wsprrypi::UsbManagementClient>(management,[&]{return clock.now_ms();},
      [&](unsigned ms){clock.wait_ms(ms);},[&]{return target.usb_cancelled.load();});
  };
  target.usb_utc_observer=[&]{return wsprrypi::HostUtcObservation{true,clock.peer.mono(),backend_test::Peer::utc_base+clock.peer.mono(),1000};};
  CHECK(target.app->inspect().ok);
  clock.peer.before_handle=[&](auto operation) {
    if(operation==wsprrypi::wtp::Operation::Load)management.reclaimed=true;
    if(operation==wsprrypi::wtp::Operation::Arm)CHECK(management.opens==2 && management.name_reads==2 && management.time_submits==2);
  };
  wsprrypi::TransmissionRequest request;
  request.output.backend=wsprrypi::BackendKind::WTP;
  request.mode=wsprrypi::TransmissionMode::TONE;
  request.payload=wsprrypi::TonePayload{14097100,200ms,{}};
  request.policy.allow_unqualified_frequency=true;
  target.app->prepare(request);target.app->start();
  const auto deadline=std::chrono::steady_clock::now()+15s;
  while(target.app->active() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
  CHECK(!target.app->active());
  CHECK(std::find(clock.peer.operations.begin(),clock.peer.operations.end(),wsprrypi::wtp::Operation::Arm)!=clock.peer.operations.end());
  CHECK(management.opens==2 && management.name_reads==2 && management.time_submits==2 && management.config_mutations==0);
  CHECK(target.usb_status()["management"]["connected"]==true && target.usb_status()["name_confirmed"]==true);
  CHECK(target.app->stop().ok);
}
void usb_management_known_binding_fault() {
  Clock clock;ReclaimedManagement management;
  WtpSettings settings{"/dev/ttyACM1","test-serial",backend_test::device,0xcafe,0x4012,1000};
  wsprrypi::WtpTargetRuntime target(settings,clock,clock.peer,
      {backend_test::sid,backend_test::owner_id,backend_test::device},[&]{clock.peer.open();return true;});
  target.usb_management_for_test=[&] {
    management.open();
    return std::make_unique<wsprrypi::UsbManagementClient>(management,[&]{return clock.now_ms();},
      [&](unsigned ms){clock.wait_ms(ms);},[&]{return target.usb_cancelled.load();});
  };
  target.usb_utc_observer=[&]{return wsprrypi::HostUtcObservation{true,clock.peer.mono(),backend_test::Peer::utc_base+clock.peer.mono(),1000};};
  CHECK(target.app->inspect().ok);
  target.learned_usb_device=backend_test::device;target.learned_usb_boot=backend_test::boot;
  management.target["boot_id"]=std::string(32,'c');
  CHECK(!target.ensure_usb_management() && !target.usb_identity_admitted());
  wsprrypi::TransmissionRequest request;
  request.output.backend=wsprrypi::BackendKind::WTP;
  request.mode=wsprrypi::TransmissionMode::TONE;
  request.payload=wsprrypi::TonePayload{14097100,200ms,{}};
  request.policy.allow_unqualified_frequency=true;
  target.app->prepare(request);target.app->start();
  const auto deadline=std::chrono::steady_clock::now()+15s;
  while(target.app->active() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
  CHECK(!target.app->active());
  CHECK(std::find(clock.peer.operations.begin(),clock.peer.operations.end(),wsprrypi::wtp::Operation::Load)==clock.peer.operations.end());
  CHECK(std::find(clock.peer.operations.begin(),clock.peer.operations.end(),wsprrypi::wtp::Operation::Arm)==clock.peer.operations.end());
  // A later matching periodic observation cannot erase the known contradiction.
  management.target["boot_id"]=backend_test::boot;
  target.refresh_usb_time(true);CHECK(!target.usb_identity_admitted());
  CHECK(target.app->stop().ok && target.app->inspect().ok);
  CHECK(target.reconcile_usb_binding() && target.usb_identity_admitted());
}
void usb_first_identity_lease_race() {
  Clock clock;
  WtpSettings settings{"/dev/ttyACM1","test-serial","",0xcafe,0x4012,1000};
  wsprrypi::WtpTargetRuntime target(settings,clock,clock.peer,
      {backend_test::sid,backend_test::owner_id,backend_test::device},[&]{clock.peer.open();return true;});
  std::promise<void> release;auto ready=release.get_future().share();
  auto first=std::async(std::launch::async,[&]{ready.wait();return target.reserve_output(std::string(32,'c'));});
  auto delayed_management=std::async(std::launch::async,[&]{ready.wait();return target.reserve_output(std::string(32,'c'));});
  release.set_value();CHECK(first.get() && delayed_management.get());
  CHECK(target.output_lease_snapshot()!=nullptr);
  // A later contradictory management HELLO cannot repurpose that reservation.
  CHECK(!target.reserve_output(std::string(32,'d')));
}
void usb_binding_and_observation() {
  for (bool wrong_boot : {false,true}) {
    Clock clock;
    WtpSettings settings{"/dev/ttyACM1","test-serial","",0xcafe,0x4012,1000};
    auto options=wsprrypi::wtp::SessionOptions{backend_test::sid,backend_test::owner_id,""};
    options.learn_device_identity=true;
    wsprrypi::WtpTargetRuntime target(settings,clock,clock.peer,options,[&]{clock.peer.open();return true;});
    target.learned_usb_device=wrong_boot?backend_test::device:std::string(32,'a');
    target.learned_usb_boot=wrong_boot?std::string(32,'b'):backend_test::boot;
    CHECK(target.app->inspect().ok && !target.usb_identity_admitted());
    wsprrypi::TransmissionRequest request;
    request.output.backend=wsprrypi::BackendKind::WTP;
    request.mode=wsprrypi::TransmissionMode::TONE;
    request.payload=wsprrypi::TonePayload{14097100,200ms,{}};
    request.policy.allow_unqualified_frequency=true;
    target.app->prepare(request);target.app->start();
    const auto deadline=std::chrono::steady_clock::now()+3s;
    while(target.app->active() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
    CHECK(!target.app->active());
    CHECK(std::find(clock.peer.operations.begin(),clock.peer.operations.end(),wsprrypi::wtp::Operation::Load)==clock.peer.operations.end());
    CHECK(std::find(clock.peer.operations.begin(),clock.peer.operations.end(),wsprrypi::wtp::Operation::Arm)==clock.peer.operations.end());
    CHECK(target.app->stop().ok);
    target.usb_name={{"saved","previous.local"}};target.usb_name_confirmed=true;
    target.usb_observation={{"connected",true}};
    CHECK(!target.usb_unavailable());
    const auto disconnected=target.usb_status();
    CHECK(disconnected["management"]["connected"]==false && disconnected["name_confirmed"]==false && disconnected["hostname"]["saved"]=="previous.local");
    CHECK(!target.ensure_usb_management());
    CHECK(target.usb_status()["management"]["connected"]==false);
  }
}
void finite_message_request_inputs() {
  ArgParserConfig cfg{};
  cfg.transmit_backend = TransmitBackendKind::WTP;
  cfg.cw_intra_element_gap = cfg.dfcw_intra_element_gap = 1;
  cfg.cw_inter_character_gap = cfg.dfcw_inter_character_gap = 3;
  cfg.cw_inter_word_gap = cfg.dfcw_inter_word_gap = 7;
  cfg.qrss.message = cfg.fskcw.message = cfg.dfcw.message = "E";
  for (double dot : {1.0, 3600.0, 1e20, std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::quiet_NaN(), 0.0}) {
    cfg.qrss.dot_seconds = cfg.fskcw.dot_seconds = cfg.dfcw.dot_seconds = dot;
    for (auto builder : {scheduling_detail::make_qrss_controller_request,
                         scheduling_detail::make_fskcw_controller_request,
                         scheduling_detail::make_dfcw_controller_request}) {
      bool rejected = false;
      try { (void)builder(cfg, 0); }
      catch (const std::runtime_error&) { rejected = true; }
      CHECK(rejected == !(dot == 1.0 || dot == 3600.0));
    }
  }
}
void run(const std::string &credentials) {
  char filename[] = "/tmp/wtp-production-config-XXXXXX";
  const int fd = mkstemp(filename);
  CHECK(fd >= 0);
  close(fd);
  struct Remove {
    std::string path;
    ~Remove() { unlink(path.c_str()); unlink((path + ".wtp-devices.json").c_str()); }
  } remove{filename};
  init_config_json();
  json_to_config();
  set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
  iniFile.set_filename(filename);
  config.use_ini = true;
  config.ini_filename = filename;
  config.transmit_backend = TransmitBackendKind::SIMULATED;
  config.transmit = false;
  config.use_led = config.use_amp = config.use_shutdown = false;
  config.callsign = "AA0NT";
  config.grid_square = "EM18";
  config.power_dbm = 20;
  config.frequencies = "20m";
  resolve_backend_specific_config(config);
  config_to_json();
  // Active host-config routes use the same guarded authority and revision CAS.
  httplib::Server http;
  web_server_routes::register_control(http, [](httplib::Response &) {});
  http.set_pre_routing_handler([](const httplib::Request &request, httplib::Response &response) {
    const SupportRequestGuardSnapshot trust{true, "localhost", {}, {}};
    const auto origin = request.has_header("Origin") ? std::optional<std::string>(request.get_header_value("Origin")) : std::nullopt;
    if (evaluate_backend_http_request(request.method, request.path, request.remote_addr,
        request.get_header_value("Host"), origin, trust) == BackendHttpGuardDecision::rejected) {
      response.status = 403; return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });
  const int port = http.bind_to_any_port("127.0.0.1"); CHECK(port > 0);
  std::thread serving([&] { http.listen_after_bind(); });
  struct StopHttp { httplib::Server &server; std::thread &thread; ~StopHttp() { server.stop(); thread.join(); } } stop_http{http, serving};
  httplib::Client browser("127.0.0.1", port);
  browser.set_read_timeout(15, 0);
  const std::string authority = "localhost:" + std::to_string(port);
  const httplib::Headers read_headers{{"Host", authority}};
  auto resource = browser.Get("/api/v1/host/config", read_headers);
  CHECK(resource && resource->status == 200 && resource->has_header("ETag"));
  CHECK(!resource->has_header("Access-Control-Allow-Origin"));
  const auto revision = resource->get_header_value("ETag");
  httplib::Headers headers{{"Host", authority}, {"Origin", "http://" + authority},
    {"X-WsprryPico-Request", "1"}, {"If-Match", revision}};
  const std::string patch = R"({"Operation":{"Transmit Backend":"wtp"},"WTP":{"Endpoint":"/dev/ttyACM1","USB Serial":"000012345678","USB Vendor ID":51966,"USB Product ID":16402,"Device ID":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","Hostname":"PrEsErVeD.LoCaL.","TCP Port":18443,"TLS Server Identity":"CeRtIfIcAtE.LoCaL.","TLS CA File":"/etc/pico/ca.crt","TLS Client Certificate":"/etc/pico/client.crt","TLS Client Key":"/etc/pico/client.key"}})";
  auto updated = browser.Put("/api/v1/host/config", headers, patch, "application/json");
  if (updated && updated->status != 200) {
    std::cerr << updated->status << " " << updated->body << "\n";
  }
  CHECK(updated && updated->status == 200 && updated->get_header_value("ETag") != revision);
  auto stale = browser.Put("/api/v1/host/config", headers, patch, "application/json");
  CHECK(stale && stale->status == 412);
  RuntimeConfigCandidate network_candidate;
  prepare_runtime_config_candidate(filename, network_candidate);
  CHECK(network_candidate.valid && network_candidate.normalized_config.wtp.hostname == "PrEsErVeD.LoCaL.");
  CHECK(network_candidate.normalized_config.wtp.tls_identity == "CeRtIfIcAtE.LoCaL.");
  CHECK(network_candidate.normalized_config.wtp.tls_key == "/etc/pico/client.key");
  headers.erase("If-Match");
  auto missing = browser.Put("/api/v1/host/config", headers, "{}", "application/json");
  CHECK(missing && missing->status == 428);
  headers.erase("Origin");
  auto csrf = browser.Put("/api/v1/host/config", headers, "{}", "application/json");
  CHECK(csrf && csrf->status == 403);
  headers.emplace("Origin", "http://evil");
  auto evil = browser.Put("/api/v1/host/config", headers, "{}", "application/json");
  CHECK(evil && evil->status == 403);
  auto unknown = browser.Get("/api/v1/unknown", read_headers);
  CHECK(unknown && unknown->status == 403);
  const auto old_gpio = jConfig.at("GPIO");
  WtpSettings settings{"/dev/ttyACM1", "000012345678", backend_test::device,
                       0xcafe,         0x4012,         1000};
  patch_all_from_web({{"Operation", {{"Transmit Backend", "wtp"}}},
                      {"WTP", wtp_settings_json(settings)}});
  CHECK(config.transmit_backend == TransmitBackendKind::WTP &&
        config.wtp == settings);
  CHECK(config.tx_pin == 0 && config.ppm == 0 && config.power_level == 0);
  CHECK(jConfig.at("GPIO") == old_gpio);
  CHECK(get_public_config_json().at("WTP") == wtp_settings_json(settings));
  RuntimeConfigCandidate candidate;
  prepare_runtime_config_candidate(filename, candidate);
  if (!candidate.valid)
    throw std::runtime_error(candidate.error_reason);
  CHECK(candidate.normalized_config.wtp == settings);
  auto plain_settings = settings;
  plain_settings.transport = "network_plain";
  plain_settings.hostname = "pico.local";
  plain_settings.tcp_port = 31417;
  plain_settings.device_id.clear();
  patch_all_from_web({{"WTP", wtp_settings_json(plain_settings)}});
  RuntimeConfigCandidate plain_candidate;
  prepare_runtime_config_candidate(filename, plain_candidate);
  CHECK(plain_candidate.valid && plain_candidate.normalized_config.wtp == plain_settings);
  patch_all_from_web({{"WTP", wtp_settings_json(settings)}});
  {
    const auto before = get_public_config_snapshot();
    const auto active_before = config.wtp;
    const std::string blocked_temp = std::string(filename) + ".tmp";
    CHECK(mkdir(blocked_temp.c_str(), 0700) == 0);
    bool failed = false;
    try {
      (void)patch_all_from_web_revision(
          {{"WTP", {{"Start Uncertainty ns", 1234}}}}, before.second);
    } catch (...) { failed = true; }
    CHECK(rmdir(blocked_temp.c_str()) == 0);
    CHECK(failed && config.wtp == active_before &&
          get_public_config_snapshot() == before);
  }
  auto discovered = browser.Get("/api/v1/host/discovery", read_headers);
  auto pi_endpoint = browser.Get("/api/v1/host/wtp-endpoint", read_headers);
  CHECK(pi_endpoint && pi_endpoint->status == 200 &&
        nlohmann::json::parse(pi_endpoint->body).at("schema") == "wsprrypi-wtp-endpoint/1");
  auto fleet_unstarted = browser.Get("/api/v1/host/fleet", read_headers);
  CHECK(fleet_unstarted && fleet_unstarted->status == 503 &&
        nlohmann::json::parse(fleet_unstarted->body).at("error").at("code") == "fleet_unavailable");
  auto pi_missing_origin = browser.Post("/api/v1/host/wtp-endpoint/recover", read_headers,
      R"({"confirmed":true})", "application/json");
  CHECK(pi_missing_origin && pi_missing_origin->status == 403);
  CHECK(discovered && discovered->status == 200 &&
        nlohmann::json::parse(discovered->body).at("scope") == "wtp-dns-sd/1");
  auto known = browser.Get("/api/v1/host/devices", read_headers);
  CHECK(known && known->status == 200 && known->has_header("ETag"));
  const auto known_json = nlohmann::json::parse(known->body);
  CHECK(known_json.at("profiles").size() == 1 &&
        known_json.at("profiles")[0].at("settings") == wtp_settings_json(settings) &&
        !known_json.at("active_id").get<std::string>().empty());
  auto catalog_headers = httplib::Headers{{"Host", authority},
      {"Origin", "http://" + authority}, {"X-WsprryPico-Request", "1"},
      {"If-Match", known->get_header_value("ETag")}};
  auto catalog_conflict = catalog_headers;
  catalog_conflict.erase("If-Match");
  auto missing_catalog_revision = browser.Post("/api/v1/host/devices", catalog_conflict,
      R"({"operation":"remove","id":"00000000000000000000000000000000"})", "application/json");
  CHECK(missing_catalog_revision && missing_catalog_revision->status == 428);
  catalog_conflict.erase("Origin");
  auto missing_catalog_origin = browser.Post("/api/v1/host/devices", catalog_conflict,
      R"({"operation":"remove","id":"00000000000000000000000000000000"})", "application/json");
  CHECK(missing_catalog_origin && missing_catalog_origin->status == 403);
  auto active_host = browser.Get("/api/v1/host/config", read_headers);
  CHECK(active_host && active_host->status == 200);
  catalog_headers.erase("If-Match");
  catalog_headers.emplace("If-Match", active_host->get_header_value("ETag"));
  auto use_current = browser.Post("/api/v1/host/devices/use", catalog_headers,
      nlohmann::json{{"id", known_json.at("active_id")},
                     {"catalog_revision", known->get_header_value("ETag")}}.dump(), "application/json");
  CHECK(use_current && use_current->status == 200 && config.wtp == settings);
  auto invalid_plain = plain_settings;
  invalid_plain.tcp_port = 0;
  CHECK(!wtp_runtime_selection_error(invalid_plain).empty());
  for (auto patch : {nlohmann::json{{"WTP", {{"USB Serial", ""}}}},
                     nlohmann::json{{"WTP", {{"USB Vendor ID", 1.5}}}},
                     nlohmann::json{{"WTP", {{"Unknown", 1}}}},
                     nlohmann::json{{"Operation", {{"Use Amp", true}}}}}) {
    const auto before = jConfig;
    bool rejected = false;
    try {
      patch_all_from_web(patch);
    } catch (...) {
      rejected = true;
    }
    CHECK(rejected && jConfig == before && config.wtp == settings);
  }
  const auto common_revision = get_public_config_snapshot().second;
  std::atomic<unsigned> accepted{0}, conflicts{0};
  const auto concurrent_patch = [&](unsigned value) {
    try { (void)patch_all_from_web_revision({{"WTP", {{"Start Uncertainty ns", value}}}}, common_revision); ++accepted; }
    catch (const std::exception &error) { CHECK(std::string(error.what()) == "revision_conflict"); ++conflicts; }
  };
  std::thread first(concurrent_patch, 1001), second(concurrent_patch, 1002);
  first.join(); second.join(); CHECK(accepted == 1 && conflicts == 1);
  patch_all_from_web({{"WTP", wtp_settings_json(settings)}});
  ArgParserConfig copy;
  copy_runtime_config(config, copy);
  CHECK(copy.wtp == settings);
  Clock clock;
  set_wtp_runtime_for_test(
      settings, clock, clock.peer,
      {backend_test::sid, backend_test::owner_id, backend_test::device}, [&] {
        clock.peer.open();
        return true;
      });
  CHECK(transmitter_quiesce_for_startup().ok);
  CHECK(wtp_runtime_ready() &&
        wtp_runtime_selection_error(std::nullopt).empty());
  wsprrypi::TransmissionRequest request;
  request.mode = wsprrypi::TransmissionMode::TONE;
  request.output.backend = wsprrypi::BackendKind::WTP;
  request.payload = wsprrypi::TonePayload{14097100, 200ms, {}};
  request.policy.allow_unqualified_frequency = true;
  transmitter_configure_execution(request, {});
  {
    bool probe_ran = false;
    bool probe_refused = false;
    try {
      (void)wtp_runtime_safe_probe([&] { probe_ran = true; return nlohmann::json::object(); });
    } catch (...) { probe_refused = true; }
    CHECK(probe_refused && !probe_ran);
    auto pending_config = browser.Get("/api/v1/host/config", read_headers);
    CHECK(pending_config && pending_config->status == 200);
    catalog_headers.erase("If-Match");
    catalog_headers.emplace("If-Match", pending_config->get_header_value("ETag"));
    auto blocked_use = browser.Post("/api/v1/host/devices/use", catalog_headers,
        nlohmann::json{{"id", known_json.at("active_id")},
                       {"catalog_revision", known->get_header_value("ETag")}}.dump(),
        "application/json");
    CHECK(blocked_use && blocked_use->status == 409);
  }
  CHECK(!wtp_runtime_invalidate_for_reload());
  CHECK(clock.peer.executions == 0 &&
        std::count(clock.peer.operations.begin(), clock.peer.operations.end(),
                   wsprrypi::wtp::Operation::Arm) == 0);
  CHECK(wtp_runtime_selection_error(std::nullopt).empty());
  bool complete = false;
  const auto owner_thread = std::this_thread::get_id();
  transmitter_set_callbacks([&](auto event, auto, const auto &, auto) {
    CHECK(std::this_thread::get_id() == owner_thread);
    CHECK(event == WsprTransmissionCallbackEvent::COMPLETE);
    complete = true;
  });
  transmitter_configure_execution(request, {});
  CHECK(!wtp_runtime_selection_error(std::nullopt).empty());
  bool refused = false;
  try {
    select_wtp_runtime(std::nullopt);
  } catch (...) {
    refused = true;
  }
  CHECK(refused && wtp_runtime_selected());
  transmitter_start_async();
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (!complete && std::chrono::steady_clock::now() < deadline) {
    transmitter_poll_events();
    std::this_thread::sleep_for(1ms);
  }
  CHECK(complete && clock.peer.executions == 1);
  auto snapshot = nlohmann::json::parse(wtp_runtime_json());
  CHECK(snapshot["last_report"]["outcome"] == "complete");
  CHECK(snapshot["remote"]["output_active"] == false);
  // Unknown ARM crosses the actual parent bridge and cannot be generically cleared.
  clock.peer.lose_reply = wsprrypi::wtp::Operation::Arm;
  bool blocked = false;
  transmitter_set_callbacks([&](auto event, auto, const auto &, auto) {
    CHECK(std::this_thread::get_id() == owner_thread);
    CHECK(event == WsprTransmissionCallbackEvent::FAILED);
    blocked = true;
  });
  transmitter_configure_execution(request, {});
  transmitter_start_async();
  const auto blocked_deadline = std::chrono::steady_clock::now() + 10s;
  while (!blocked && std::chrono::steady_clock::now() < blocked_deadline) {
    transmitter_poll_events();
    std::this_thread::sleep_for(1ms);
  }
  CHECK(blocked && !wtp_runtime_ready());
  transmitter_clear_soft_off();
  transmitter_clear_execution_state_after_stop();
  transmitter_stop_and_join();
  CHECK(!wtp_runtime_selection_error(std::nullopt).empty());
  CHECK(wtp_runtime_recover().ok && wtp_runtime_ready());
  snapshot = nlohmann::json::parse(wtp_runtime_json());
  CHECK(snapshot["last_report"]["outcome"] == "blocked");
  CHECK(snapshot["remote"]["output_active"] == false);
  CHECK(std::count(clock.peer.operations.begin(), clock.peer.operations.end(),
                   wsprrypi::wtp::Operation::Arm) == 2);
  transmitter_stop_and_join();
  select_wtp_runtime(std::nullopt);
  CHECK(!wtp_runtime_selected());
  // A bounded tone's host timer must cover WTP's future slot and finite RF
  // duration. Exercise the actual scheduler request/parent bridge with the peer.
  Clock tone_clock;
  struct ToneRuntimeGuard {
    ~ToneRuntimeGuard() {
      transmitter_stop_and_join();
      select_wtp_runtime(std::nullopt);
    }
  } tone_runtime_guard;
  set_wtp_runtime_for_test(settings, tone_clock, tone_clock.peer,
      {backend_test::sid, backend_test::owner_id, backend_test::device}, [&] {
        tone_clock.peer.open(); return true;
      });
  CHECK(wtp_runtime_inspect().ok);
  config.transmit_backend = TransmitBackendKind::WTP;
  config.mode = ModeType::QRSS;
  config.transmit = false;
  config.frequencies = "20m";
  config.allow_unqualified_frequency = true;
  config.ppm = 0;
  config.use_led = config.use_amp = config.use_shutdown = false;
  complete = false;
  transmitter_set_callbacks([&](auto event, auto, const auto &, auto) {
    CHECK(event == WsprTransmissionCallbackEvent::COMPLETE);
    complete = true;
  });
  TestToneRequest bounded;
  bounded.frequency_hz = 14097100;
  bounded.duration = 10s;
  const auto tone_started = start_test_tone(bounded);
  if (!tone_started.started) std::cerr << tone_started.message << '\n';
  CHECK(tone_started.started && tone_started.bounded_cleanup_delay);
  const auto tone_deadline = std::chrono::steady_clock::now() + 10s;
  while (current_test_tone_status().state != "completed" && std::chrono::steady_clock::now() < tone_deadline) {
    transmitter_poll_events();
    std::this_thread::sleep_for(1ms);
  }
  CHECK(current_test_tone_status().state == "completed" && tone_clock.peer.executions == 1);
  CHECK(tone_clock.peer.records.front().state == wsprrypi::wtp::State::Complete);
  CHECK(tone_clock.peer.records.front().ended >= tone_clock.peer.start + 10000000000ULL);
  CHECK(tone_started.bounded_cleanup_delay->count() >
        static_cast<std::int64_t>(tone_clock.peer.records.front().ended));
  // RF-on stays exactly ten seconds; the compiler retains its one-nanosecond
  // off tail despite the later host cleanup timer.
  bool exact_duration = false;
  for (const auto &payload : tone_clock.peer.payloads) {
    const auto wire = nlohmann::json::parse(payload);
    if (wire.value("op", "") == "LOAD")
      exact_duration = wire.at("body").at("total_duration_ns") == "10000000001" &&
          wire.at("body").at("events").size() == 2 &&
          wire.at("body").at("events").at(0).at("duration_ns") == "10000000000" &&
          wire.at("body").at("events").at(0).at("rf_on") == true &&
          wire.at("body").at("events").at(1).at("duration_ns") == "1" &&
          wire.at("body").at("events").at(1).at("rf_on") == false;
  }
  CHECK(exact_duration);
  const auto tone_ended = end_test_tone();
  CHECK(tone_ended.stopped && !tone_ended.restoration_failed && tone_ended.output_inactive);
  CHECK(!current_test_tone_status().owned && current_test_tone_status().state == "ended");
  const auto tone_again = start_test_tone(bounded);
  CHECK(tone_again.started);
  CHECK(end_test_tone(tone_again.lifecycle.generation, TestToneEndPolicy::Quiesce).stopped);
  // Lost ARM acknowledgment leaves ownership uncertain. End must retain the
  // inhibition until explicit recovery proves inactivity, then report the
  // recovery it actually performed rather than saying output was unchanged.
  tone_clock.peer.lose_reply = wsprrypi::wtp::Operation::Arm;
  const auto uncertain_tone = start_test_tone(bounded);
  CHECK(uncertain_tone.started);
  const auto uncertain_deadline = std::chrono::steady_clock::now() + 10s;
  while (current_test_tone_status().state != "failed" && std::chrono::steady_clock::now() < uncertain_deadline) {
    transmitter_poll_events();
    std::this_thread::sleep_for(1ms);
  }
  CHECK(current_test_tone_status().state == "failed");
  const auto uncertain_end = end_test_tone();
  CHECK(uncertain_end.restoration_failed && uncertain_end.lifecycle.state == "unknown" && !config.transmit);
  CHECK(!start_test_tone(bounded).started);
  const auto unresolved_operations=tone_clock.peer.operations;
  CHECK(wtp_runtime_management("member/management","POST",R"({"action":"connect","carrier":"consumer_station","host":"192.168.1.53","password":"test-password"})","").status==409);
  CHECK(tone_clock.peer.operations==unresolved_operations);
  CHECK(wtp_runtime_recover().ok);
  const auto recovered_end = end_test_tone();
  CHECK(recovered_end.stopped && !recovered_end.tone_was_active && recovered_end.output_inactive);
  CHECK(recovered_end.message == recovered_end.lifecycle.message &&
        recovered_end.message.find("recovery cleanup confirmed") != std::string::npos);
  CHECK(recovered_end.lifecycle.state == "ended" && !config.transmit);
  select_wtp_runtime(std::nullopt);
  Clock plain_clock;
  set_wtp_runtime_for_test(plain_settings, plain_clock, plain_clock.peer,
      {backend_test::sid, backend_test::owner_id, backend_test::device}, [&] {
        plain_clock.peer.open(); return true;
      });
  CHECK(wtp_runtime_inspect().ok);
  const auto plain_status = nlohmann::json::parse(wtp_runtime_json());
  CHECK(plain_status.at("transport") == "network_plain");
  CHECK(plain_status.at("network").at("security") == "plain_lan");
  select_wtp_runtime(std::nullopt);
  select_wtp_runtime(plain_settings);
  CHECK(nlohmann::json::parse(wtp_runtime_json()).at("transport") == "network_plain");
  auto management_settings = plain_settings;
  management_settings.device_id = backend_test::device;
  select_wtp_runtime(management_settings);
  CHECK(!wtp_runtime_inspect().ok); // Failed WTP startup never submitted work.
  // Deliberate management-only admission must not open an unrelated WTP path.
  for (const auto carrier : {"usb", "ble", "network_plain", "engineering_field", "tls_wtp"}) {
    const auto rejected = wtp_runtime_management("member/management", "POST",
        nlohmann::json{{"action","connect"},{"carrier",carrier},{"host","192.168.1.53"},{"password","test-password"}}.dump(), "");
    CHECK(rejected.status == 400);
  }
  const auto login_refused = wtp_runtime_management("member/management", "POST",
      R"({"action":"connect","carrier":"consumer_station","host":"192.168.1.53","password":"test-password"})", "");
  CHECK(login_refused.status == 401); // Production network I/O is inhibited.
  CHECK(nlohmann::json::parse(wtp_runtime_management("member/management","GET","","").body).at("carrier") == "consumer_station");
  CHECK(wtp_runtime_management("member/configuration","GET","","").status == 401);
  CHECK(wtp_runtime_management("member/management","POST",R"({"action":"disconnect"})","").status == 200);

  const auto previous_selection=nlohmann::json::parse(wtp_runtime_management("member/management","GET","","").body).at("selection_id").get<std::string>();
  auto changed_selection=management_settings;changed_selection.hostname="192.0.2.123";
  select_wtp_runtime(changed_selection);
  const auto stale_dispatch=wtp_runtime_management("member/application","GET","","",previous_selection);
  CHECK(stale_dispatch.status==409 && stale_dispatch.body.find("member_read_required")!=std::string::npos);
  select_wtp_runtime(std::nullopt);
  if (!credentials.empty()) {
    auto network = settings;
    network.transport = "network"; network.hostname = "pico-test.local"; network.tcp_port = 18444;
    network.tls_ca = credentials + "/ca.crt";
    network.tls_certificate = credentials + "/runtime-rotation.crt";
    network.tls_key = credentials + "/runtime-rotation.key";
    const auto copy_credentials = [&](const std::string &name) {
      std::filesystem::copy_file(credentials + "/" + name + ".crt", network.tls_certificate, std::filesystem::copy_options::overwrite_existing);
      std::filesystem::copy_file(credentials + "/" + name + ".key", network.tls_key, std::filesystem::copy_options::overwrite_existing);
    };
    copy_credentials("client");
    Clock rotation_clock;
    set_wtp_runtime_for_test(network, rotation_clock, rotation_clock.peer,
        {backend_test::sid, backend_test::owner_id, backend_test::device}, [&] { rotation_clock.peer.open(); return true; });
    CHECK(wtp_runtime_inspect().ok);
    // The new coordinator enters the same process-wide operation authority.
    std::promise<void> entered, release;
    auto entered_future = entered.get_future();
    auto released = release.get_future().share();
    std::thread holder([&] {
      (void)wtp_runtime_safe_probe([&] {
        entered.set_value(); released.wait(); return nlohmann::json::object();
      });
    });
    entered_future.wait();
    auto competing = std::async(std::launch::async, [] {
      return wtp_runtime_management("member/configuration", "POST", "{}", "");
    });
    const bool prompt_busy = competing.wait_for(1s) == std::future_status::ready;
    release.set_value(); holder.join();
    const auto busy_reply = competing.get();
    CHECK(prompt_busy && busy_reply.status == 409 && busy_reply.body.find("busy") != std::string::npos);
    wtp_runtime_prepare(request);
    const auto before_management = rotation_clock.peer.operations;
    CHECK(wtp_runtime_management("member/configuration", "GET", "", "").status == 409);
    for (const auto resource : {"member/application","member/station","member/hardware","member/pins","member/pins/validate"}) {
      CHECK(wtp_runtime_management(resource,std::string(resource).ends_with("validate") ? "POST" : "GET","","").status==409);
      CHECK(rotation_clock.peer.operations==before_management);
    }
    CHECK(rotation_clock.peer.operations == before_management);
    CHECK(wtp_runtime_management("member/management", "POST",
        R"({"action":"connect","carrier":"consumer_station","host":"192.168.1.53","password":"test-password"})", "").status == 409);
    CHECK(rotation_clock.peer.operations == before_management);

    CHECK(wtp_runtime_selection_error(network).empty());
    copy_credentials("other");
    CHECK(!wtp_runtime_selection_error(network).empty());
    CHECK(wtp_runtime_stop().ok);
    CHECK(wtp_runtime_selection_error(network).empty());
    select_wtp_runtime(std::nullopt);
    std::filesystem::remove(network.tls_certificate); std::filesystem::remove(network.tls_key);
  }
  std::cout << backend_test::checks
            << " production WTP configuration/runtime checks passed\n";
}
} // namespace
int main(int argc, char **argv) {
  try {
    usb_management_reclaimed_at_load();
    usb_management_known_binding_fault();
    usb_first_identity_lease_race();
    usb_binding_and_observation();
    finite_message_request_inputs();
    run(argc > 1 ? argv[1] : "");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
