// SPDX-License-Identifier: MIT
#include "usb_discovery.hpp"
#include "usb_cdc.hpp"
#include "wtp_settings_json.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <openssl/sha.h>
#include <stdexcept>

namespace wsprrypi {
namespace {
std::string text(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::string value;
  std::getline(input, value);
  return value;
}
std::string usb_id(const CdcIdentity &identity) {
  const auto bytes = identity.usb_serial + ":" +
      std::to_string(identity.vendor_id) + ":" + std::to_string(identity.product_id);
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), digest);
  std::string id = "usb-";
  for (unsigned i = 0; i < 16; ++i) {
    id += "0123456789abcdef"[digest[i] >> 4];
    id += "0123456789abcdef"[digest[i] & 15];
  }
  return id;
}
} // namespace
nlohmann::json wtp_usb_discovery(const std::filesystem::path &root,
                                  const std::filesystem::path &dev) {
  auto result = nlohmann::json{{"scope", "wtp-usb/1"}, {"available", false},
      {"reason", "USB discovery is available on Linux hosts"},
      {"candidates", nlohmann::json::array()}};
  if (const char *disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
      disabled && std::string(disabled) == "1") {
    result["reason"] = "USB discovery is disabled for hardware-free validation";
    return result;
  }
  try {
    std::map<std::string, nlohmann::json> found;
    std::map<std::string, unsigned> counts;
    unsigned scanned = 0;
    for (const auto &entry : std::filesystem::directory_iterator(root / "class/tty")) {
      if (++scanned > 4096) throw std::runtime_error("USB inventory exceeds its size limit");
      try {
        const auto identity = inspect_linux_cdc((dev / entry.path().filename()).string(), root);
        const auto interface = std::filesystem::path(identity.interface_path);
        if (identity.control_interface != static_cast<unsigned>(PicoCdcRole::Wtp) ||
            text(interface / "interface") != "WsprryPico WTP" ||
            text(interface.parent_path() / "product") != "WsprryPico" ||
            identity.usb_serial.size() > 128 || !identity.vendor_id || !identity.product_id)
          continue;
        const auto id = usb_id(identity);
        ++counts[id];
        WtpSettings settings;
        settings.path = identity.path;
        settings.usb_serial = identity.usb_serial;
        settings.vendor_id = identity.vendor_id;
        settings.product_id = identity.product_id;
        // Prefer the host's persistent by-id link; never construct a tty suffix.
        std::error_code error;
        const auto links = dev / "serial/by-id";
        if (std::filesystem::is_directory(links, error)) {
          unsigned aliases = 0;
          for (const auto &link : std::filesystem::directory_iterator(links)) {
            if (++aliases > 4096) break;
            if (std::filesystem::canonical(link.path(), error) == identity.path && !error) {
              settings.path = link.path().string();
              break;
            }
          }
        }
        found[id] = {{"id", id}, {"instance", "WsprryPico " + identity.usb_serial},
            {"binding", "usb"}, {"state", "online"},
            {"settings", wtp_settings_json(settings)}};
      } catch (const std::exception &) { /* Unrelated or disappearing tty. */ }
    }
    result["available"] = true;
    result["reason"] = "";
    for (const auto &[id, candidate] : found) {
      if (counts[id] == 1) result["candidates"].push_back(candidate);
      else result["reason"] = "USB Picos with duplicate serials cannot be selected safely";
    }
  } catch (const std::exception &) {
    result["reason"] = "USB inventory could not be read; refresh the device list";
  }
  return result;
}
void validate_discovered_usb(const nlohmann::json &snapshot,
                             const std::string &id, const WtpSettings &settings) {
  if (snapshot.value("available", false))
    for (const auto &candidate : snapshot.at("candidates")) {
      if (candidate.at("id") != id) continue;
      const auto observed = parse_wtp_settings(candidate.at("settings"), true);
      if (settings.transport == "usb" && settings.path == observed.path &&
          settings.usb_serial == observed.usb_serial && settings.vendor_id == observed.vendor_id &&
          settings.product_id == observed.product_id) return;
      throw std::runtime_error("USB connection changed; refresh and select the Pico again");
    }
  throw std::runtime_error("USB Pico disconnected or unavailable; refresh the device list");
}
} // namespace wsprrypi
