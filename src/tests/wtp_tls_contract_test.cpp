// SPDX-License-Identifier: MIT
#include "wtp_integration/tls.hpp"
#include "wtp_settings_json.hpp"
#include <iostream>
#include <filesystem>
#include <sys/stat.h>
using namespace wsprrypi;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
struct Resolver : TlsResolver {
  std::optional<std::vector<std::string>> result;
  unsigned calls{}, cancellations{};
  std::string last_host;
  bool begin(const std::string &host, unsigned) override { last_host = host; ++calls; return true; }
  auto poll() -> std::optional<std::vector<std::string>> override { return result; }
  void cancel() noexcept override { ++cancellations; }
};
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  try {
    const std::string dir = argv[1];
    TlsSelection selection{"pico-test.local", {}, dir + "/ca.crt", dir + "/client.crt", dir + "/client.key", 18444};
    auto credentials = std::make_shared<TlsCredentials>(selection);
    CHECK(credentials->matches(TlsCredentials(selection)));
    auto alternate = selection; alternate.certificate_file = dir + "/other.crt"; alternate.key_file = dir + "/other.key";
    CHECK(!credentials->matches(TlsCredentials(alternate)));
    auto rotated = selection;
    rotated.certificate_file = dir + "/rotation.crt";
    rotated.key_file = dir + "/rotation.key";
    std::filesystem::copy_file(selection.certificate_file, rotated.certificate_file, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(selection.key_file, rotated.key_file, std::filesystem::copy_options::overwrite_existing);
    auto original = TlsCredentials(rotated);
    CHECK(original.matches(TlsCredentials(rotated)));
    std::filesystem::copy_file(alternate.certificate_file, rotated.certificate_file, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(alternate.key_file, rotated.key_file, std::filesystem::copy_options::overwrite_existing);
    CHECK(!original.matches(TlsCredentials(rotated)));
    std::filesystem::remove(rotated.certificate_file); std::filesystem::remove(rotated.key_file);
    std::uint64_t now{};
    auto resolver = std::make_unique<Resolver>(); auto *fake = resolver.get();
    TlsStream stream([&] { return now; }, TlsStream::Access::LoopbackTest, std::move(resolver));
    CHECK(stream.begin_open(selection, credentials));
    now += TlsStream::resolve_timeout_ms; stream.poll_open();
    CHECK(!stream.opening() && stream.observation().state == "failed");
    CHECK(stream.begin_open(selection, credentials)); stream.cancel(); stream.poll_open();
    CHECK(!stream.opening() && stream.observation().diagnostic.find("cancelled") != std::string::npos);
    fake->result = std::vector<std::string>{};
    CHECK(stream.begin_open(selection, credentials)); stream.poll_open(); CHECK(!stream.opening());
    fake->result = std::vector<std::string>{"192.0.2.1"};
    CHECK(stream.begin_open(selection, credentials)); stream.poll_open();
    CHECK(stream.observation().diagnostic.find("loopback") != std::string::npos);
    for (const std::string address : {"127.0.0.1", "127.0.0.2"}) {
      fake->result = std::vector<std::string>{address};
      CHECK(stream.begin_open(selection, credentials)); stream.poll_open();
      CHECK(stream.observation().address == address);
      CHECK(stream.observation().authenticated_identity.empty());
      stream.close();
    }
    CHECK(fake->calls == 6);
    auto spelled = selection; spelled.host = "PiCo-Test.LoCaL.";
    CHECK(stream.begin_open(spelled, credentials));
    CHECK(fake->last_host == "pico-test.local"); stream.close();
    for (const std::string invalid : {"", "pico..local", "pico.local..", "-pico.local", "pico-.local",
         "pico_local", "pico.local:443", "pico.local/", "pico.local\r\nHost: evil",
         "*.local", "127.1", "0127.0.0.1", "0x7f000001", "0x7f000001.", "127.0.0.1.", "256.0.0.1", "[::1]", "::g"}) {
      const auto before = fake->calls;
      auto bad = selection; bad.host = invalid;
      CHECK(!stream.begin_open(bad, credentials) && fake->calls == before);
      bad = selection; bad.expected_identity = invalid.empty() ? "*.local" : invalid;
      CHECK(!stream.begin_open(bad, credentials) && fake->calls == before);
    }
    CHECK(!canonical_network_identity(std::string("::1\0evil", 8)));
    CHECK(canonical_network_identity("PICO.LOCAL.") == "pico.local");
    CHECK(canonical_network_identity("2001:0DB8::1") == "2001:db8::1");
    CHECK(!canonical_network_identity(std::string(64, 'a') + ".local"));
    CHECK(canonical_network_identity(std::string(63, 'a') + ".local"));

    fake->result = std::vector<std::string>{};
    CHECK(stream.begin_open(selection, credentials)); stream.poll_open();
    const auto diagnostic = stream.observation().diagnostic; stream.close();
    CHECK(!diagnostic.empty() && stream.observation().diagnostic == diagnostic);
    auto native = std::make_unique<Resolver>(); auto *guarded = native.get();
    TlsStream production([&] { return now; }, TlsStream::Access::Production, std::move(native));
    setenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS", "1", 1);
    CHECK(!production.begin_open(selection, credentials) && guarded->calls == 0);
    WtpSettings s; s.transport = "network"; s.hostname = "pico-test.local"; s.tcp_port = 18444;
    s.device_id = std::string(32, 'a'); s.tls_ca = selection.ca_file; s.tls_certificate = selection.certificate_file; s.tls_key = selection.key_file;
    s.path = "/dev/preserved"; s.usb_serial = "00000001";
    CHECK(parse_wtp_settings(wtp_settings_json(s), true) == s);
    s.hostname = "PiCo-Test.LoCaL."; s.tls_identity = "PICO-TEST.LOCAL.";
    CHECK(parse_wtp_settings(wtp_settings_json(s), true) == s);
    CHECK(parse_wtp_settings(wtp_settings_json(s), false) == s);
    auto old = wtp_settings_json(s); old.erase("Transport");
    CHECK(parse_wtp_settings(old, false).transport == "usb");
    for (auto key : {"Hostname", "TLS CA File", "TLS Client Certificate", "TLS Client Key", "Device ID"}) {
      auto invalid = wtp_settings_json(s); invalid[key] = "";
      bool rejected = false; try { (void)parse_wtp_settings(invalid, true); } catch (...) { rejected = true; }
      CHECK(rejected);
    }
    CHECK(wtp_settings_json(s).dump().find("PRIVATE KEY") == std::string::npos);
    std::cout << "TLS resolver deadlines/cancellation/address refresh, production guard, credential fingerprints and legacy/settings round trips passed\n";
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
