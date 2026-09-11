// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "arg_parser.hpp"
#include "scheduling.hpp"
#include "config_handler.hpp"
#include "runtime_config_bridge.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "wtp_backend_peer.hpp"
#include "wtp_runtime_bridge.hpp"
#include "wtp_settings_json.hpp"
#include "web_server_routes.hpp"
#include "httplib.hpp"
#include "backend_http_guard.hpp"
#include <fstream>
#include <filesystem>
#include <thread>
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
void run(const std::string &credentials) {
  char filename[] = "/tmp/wtp-production-config-XXXXXX";
  const int fd = mkstemp(filename);
  CHECK(fd >= 0);
  close(fd);
  struct Remove {
    const char *path;
    ~Remove() { unlink(path); }
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
  config.mode = ModeType::WSPR;
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
  while (!complete && std::chrono::steady_clock::now() < tone_deadline) {
    transmitter_poll_events();
    std::this_thread::sleep_for(1ms);
  }
  CHECK(complete && tone_clock.peer.executions == 1);
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
  CHECK(end_test_tone().stopped);
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
    wtp_runtime_prepare(request);
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
    run(argc > 1 ? argv[1] : "");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
