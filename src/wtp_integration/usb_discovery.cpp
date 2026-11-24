// SPDX-License-Identifier: MIT
#include "usb_discovery.hpp"
#include "hardware_access_policy.hpp"
#include "usb_cdc.hpp"
#include "usb_cdc_native.hpp"
#include "wtp_settings_json.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <openssl/sha.h>
#include <stdexcept>

namespace wsprrypi {
namespace {
#ifndef _WIN32
std::string text(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::string value;
  std::getline(input, value);
  return value;
}
#endif
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
namespace {
nlohmann::json discover_usb(const std::filesystem::path &root,
                           const std::filesystem::path &dev, PicoCdcRole role) {
  auto result = nlohmann::json{{"scope", role==PicoCdcRole::Management?"pico-management-usb/1":"wtp-usb/1"}, {"available", false},
      {"reason", "USB discovery is unavailable on this host."},
      {"candidates", nlohmann::json::array()}};
  if (hardware_access_disabled()) {
    result["reason"] = "USB discovery is disabled for hardware-free validation";
    return result;
  }
#ifdef _WIN32
  // Explicit sysfs/dev roots are a POSIX metadata fixture boundary. Native
  // Windows enumeration is selected separately and never scans such paths.
  (void)root;(void)dev;return result;
#else
  try {
    std::map<std::string, nlohmann::json> found;
    std::map<std::string, unsigned> counts;
    unsigned scanned = 0;
    for (const auto &entry : std::filesystem::directory_iterator(root / "class/tty")) {
      if (++scanned > 4096) throw std::runtime_error("USB inventory exceeds its size limit");
      try {
        const auto identity = inspect_linux_cdc((dev / entry.path().filename()).string(), root);
        const auto interface = std::filesystem::path(identity.interface_path);
        if (identity.control_interface != static_cast<unsigned>(role) ||
            text(interface / "interface") != (role == PicoCdcRole::Management
                ? "WsprryPico Management" : "WsprryPico WTP") ||
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
            {"settings", management_profile_settings_json(settings)}};
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
#endif
}
} // namespace
#ifndef __linux__
namespace {
nlohmann::json discover_native_usb(PicoCdcRole role) {
  auto result=nlohmann::json{{"scope",role==PicoCdcRole::Management?"pico-management-usb/1":"wtp-usb/1"},
      {"available",false},{"reason","USB discovery is unavailable on this host."},{"candidates",nlohmann::json::array()}};
  if(hardware_access_disabled()) {
    result["reason"]="USB discovery is disabled for hardware-free validation";return result;
  }
#if defined(__APPLE__) || defined(_WIN32)
  try {
    std::map<std::string,nlohmann::json> found;
    std::map<std::string,unsigned> counts;
    for(const auto &endpoint:native_cdc_inventory()) {
      const auto &identity=endpoint.identity;
      if(identity.control_interface!=static_cast<unsigned>(role)||endpoint.interface_name!=(role==PicoCdcRole::Management?
          "WsprryPico Management":"WsprryPico WTP"))continue;
      WtpSettings settings;settings.path=identity.path;settings.usb_serial=identity.usb_serial;
      settings.vendor_id=identity.vendor_id;settings.product_id=identity.product_id;
      const auto id=usb_id(identity);++counts[id];
      found[id]={{"id",id},{"instance","WsprryPico "+identity.usb_serial},{"binding","usb"},{"state","online"},
          {"settings",management_profile_settings_json(settings)}};
    }
    result["available"]=true;result["reason"]="";
    for(const auto &[id,candidate]:found) {
      if(counts[id]==1)result["candidates"].push_back(candidate);
      else result["reason"]="USB Picos with duplicate serials cannot be selected safely";
    }
  }catch(const std::exception &) {result["reason"]="USB inventory could not be read; refresh the device list";}
#endif
  return result;
}
}
#endif
nlohmann::json wtp_usb_discovery(const std::filesystem::path &root,
                                const std::filesystem::path &dev) {
#ifndef __linux__
  if(root=="/sys"&&dev=="/dev")return discover_native_usb(PicoCdcRole::Wtp);
#endif
  return discover_usb(root, dev, PicoCdcRole::Wtp);
}
nlohmann::json pico_management_usb_discovery(const std::filesystem::path &root,
                                            const std::filesystem::path &dev) {
#ifndef __linux__
  if(root=="/sys"&&dev=="/dev")return discover_native_usb(PicoCdcRole::Management);
#endif
  return discover_usb(root, dev, PicoCdcRole::Management);
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
