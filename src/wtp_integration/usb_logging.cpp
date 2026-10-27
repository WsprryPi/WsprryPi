// SPDX-License-Identifier: MIT
#include "usb_logging.hpp"
#include "catalog.hpp"
#include "pico_usb_installer_state.hpp"
#include "usb_cdc.hpp"
#include "usb_discovery.hpp"
#include <openssl/sha.h>
#include "wtp_endpoint/identity.hpp"
#include "wtp_settings_json.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <set>
#ifndef LIB_JOURNALD
#define LIB_JOURNALD 0
#endif
#include <atomic>
#include <charconv>
#include <chrono>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <unistd.h>
#if defined(__linux__) && LIB_JOURNALD
#include <systemd/sd-journal.h>
#endif
namespace wsprrypi {
namespace {
using Json = nlohmann::json;
Json strict_json(const std::string &bytes) {
  std::vector<std::set<std::string>> objects;
  return Json::parse(bytes, [&](int, Json::parse_event_t event, Json &value) {
    if (event == Json::parse_event_t::object_start)
      objects.emplace_back();
    if (event == Json::parse_event_t::object_end)
      objects.pop_back();
    if (event == Json::parse_event_t::key &&
        !objects.back().insert(value.get<std::string>()).second)
      throw std::runtime_error("duplicate_logging_key");
    return true;
  });
}
std::uint64_t millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
bool identity(const std::string &value) {
  return value.size() == 32 &&
         value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::uint64_t decimal(const Json &value) {
  if (!value.is_string())
    throw std::runtime_error("invalid_log_decimal");
  const auto text = value.get<std::string>();
  std::uint64_t result = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), result);
  if (text.empty() || text.size() > 20 || (text.size() > 1 && text[0] == '0') ||
      parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    throw std::runtime_error("invalid_log_decimal");
  return result;
}
bool printable(const std::string &text, std::size_t limit) {
  return !text.empty() && text.size() <= limit &&
         std::none_of(text.begin(), text.end(),
                      [](unsigned char c) { return c < 32 || c > 126; });
}
bool project_journal(const Json &event, const std::string &collector) {
#if defined(__linux__) && LIB_JOURNALD
  const auto receipt = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
  // Custom fields carry origin claims; underscore-prefixed journal fields
  // remain journald's own process/unit/boot observations.
  return sd_journal_send(
             "MESSAGE=%s", event.at("message").get<std::string>().c_str(),
             "PRIORITY=%d", event.at("severity").get<int>(),
             "SYSLOG_FACILITY=23", "SYSLOG_IDENTIFIER=wsprrypico",
             "WSPRRY_LOG_SCHEMA=wsprry-log/1", "WSPRRY_ORIGIN_KIND=pico",
             "WSPRRY_ORIGIN_ID=%s",
             event.at("origin_id").get<std::string>().c_str(),
             "WSPRRY_ORIGIN_NAME=%s",
             event.at("origin_name").get<std::string>().c_str(),
             "WSPRRY_ORIGIN_BOOT=%s",
             event.at("boot_id").get<std::string>().c_str(),
             "WSPRRY_ORIGIN_SEQUENCE=%s",
             event.at("sequence").get<std::string>().c_str(),
             "WSPRRY_ORIGIN_UTC_NS=%s",
             event.at("utc_ns").is_null()
                 ? ""
                 : event.at("utc_ns").get<std::string>().c_str(),
             "WSPRRY_ORIGIN_MONOTONIC_NS=%s",
             event.at("monotonic_ns").get<std::string>().c_str(),
             "WSPRRY_EVENT=%s", event.at("event").get<std::string>().c_str(),
             "WSPRRY_DROPPED=%s",
             event.at("dropped").get<std::string>().c_str(),
             "WSPRRY_COLLECTOR_ID=%s", collector.c_str(), "WSPRRY_INGRESS=usb",
             "WSPRRY_RECEIPT_US=%s", std::to_string(receipt).c_str(),
             "WSPRRY_FORWARDERS=[]", nullptr) >= 0;
#else
  (void)event;
  (void)collector;
  return false;
#endif
}
struct Source {
  PosixCdcSystem system;
  UsbCdcStream stream{system};
  std::shared_ptr<void> lease;
  std::string input, output, boot, name, state = "disconnected", last_boot,
                                         dropped = "0";
  std::size_t output_offset = 0;
  std::uint64_t deadline = 0, retry = 0, next_status = 0, last_sequence = 0, rejected = 0;
  std::uint64_t rate_window = 0;
  unsigned rate_count = 0;
  bool discarding = false, status_sent = false, start_acked = false,
       export_enabled = false, status_pending = false;
  void close() {
    stream.close();
    lease.reset();
    input.clear();
    output.clear();
    output_offset = 0;
    boot.clear();
    discarding = status_sent = start_acked = status_pending = false;
  }
  ~Source() { close(); }
};
class Collector {
public:
  explicit Collector(std::string ini, std::string self = wtp_pi_device_id(), bool allow_devices = true)
      : ini_(std::move(ini)), path_(ini_ + ".usb-logs.json"),
        self_(std::move(self)), allow_devices_(allow_devices) {
    reload();
    thread_ = std::thread([this] { run(); });
  }
  ~Collector() {
    quit_ = true;
    if (thread_.joinable())
      thread_.join();
  }
  PicoHttpResponse request(const std::string &method, const std::string &body,
                           const std::string &revision) {
    std::lock_guard lock(mutex_);
    reload();
    if (method == "PUT") {
      if (body.size() > 4096)
        throw std::runtime_error("logging_settings_too_large");
      if (revision != etag())
        return {412, R"({"error":{"code":"revision_conflict"}})", {}};
      auto input = strict_json(body);
      validate(input);
      if (input.at("collector_id") != self_)
        throw std::runtime_error("logging_collector_changed");
      // Verify every identity against the saved Fleet catalog. Discovery
      // alone never enrolls or retargets a collector source.
      WtpCatalog catalog(ini_ + ".wtp-devices.json");
      const auto profiles = catalog.snapshot({}).first;
      for (const auto &item : input.at("sources")) {
        bool found = false;
        for (const auto &profile : profiles.at("profiles"))
          if (profile.at("id") == item.at("catalog_id")) {
            const auto settings =
                parse_wtp_settings(profile.at("settings"), true);
            found =
                settings.transport == "usb" &&
                settings.device_id == item.at("device_id").get<std::string>();
          }
        if (!found)
          throw std::runtime_error("logging_source_not_in_fleet");
      }
#if !LIB_JOURNALD
      for (const auto &item : input.at("sources"))
        if (item.at("enabled").get<bool>())
          throw std::runtime_error("journal_unavailable");
#endif
      save(input);
      saved_ = std::move(input);
      ++generation_;
    } else if (method != "GET")
      return {405, R"({"error":{"code":"method_not_allowed"}})", {}};
    auto result = saved_;
    result["schema"] = "host-usb-logging/1";
    result["collector_id"] = self_;
    result["status"] = status_;
    result["journal_available"] = bool(LIB_JOURNALD);
    result["candidates"] = Json::array();
    WtpCatalog catalog(ini_ + ".wtp-devices.json");
    const auto known = catalog.snapshot({}).first;
    for (const auto &profile : known.at("profiles")) {
      const auto settings = parse_wtp_settings(profile.at("settings"), true);
      if (settings.transport == "usb" && identity(settings.device_id))
        result["candidates"].push_back({{"catalog_id", profile.at("id")},
                                        {"device_id", settings.device_id},
                                        {"name", profile.at("name")}});
    }
    result["attached"] = allow_devices_ ? usb_logging_attached_sources(wtp_usb_discovery()) : Json::array();
    for (auto &attached : result["attached"])
      for (const auto &profile : known.at("profiles"))
        if (profile.at("settings").value("Device ID", "") == attached.at("device_id").get<std::string>()) {
          attached["name"] = profile.at("name");
          break;
        }
    return {200, result.dump(), etag()};
  }

private:
  static void validate(const Json &value) {
    if (!value.is_object() || value.size() != 3 ||
        value.at("schema") != "host-usb-logging/1" ||
        !identity(value.at("collector_id").get<std::string>()) ||
        !value.at("sources").is_array() || value.at("sources").size() > 8)
      throw std::runtime_error("invalid_logging_settings");
    std::set<std::string> ids;
    for (const auto &source : value.at("sources"))
      if (!source.is_object() || source.size() != 4 ||
          !source.at("enabled").is_boolean() ||
          !source.at("maximum_severity").is_number_integer() ||
          (!source.at("maximum_severity").is_number_unsigned() &&
           source.at("maximum_severity").get<std::int64_t>() < 0) ||
          source.at("maximum_severity").get<std::uint64_t>() > 7 ||
          !identity(source.at("catalog_id").get<std::string>()) ||
          !identity(source.at("device_id").get<std::string>()) ||
          !ids.insert(source.at("device_id").get<std::string>()).second)
        throw std::runtime_error("invalid_logging_source");
  }
  void reload() {
    Json loaded = {{"schema", "host-usb-logging/1"},
                   {"collector_id", self_},
                   {"sources", Json::array()}};
    const int fd = ::open(path_.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
      if (errno != ENOENT)
        throw std::runtime_error("logging_settings_unreadable");
    } else {
      struct stat info{};
      if (::fstat(fd, &info) || !S_ISREG(info.st_mode) ||
          info.st_uid != getuid() || (info.st_mode & 0077) ||
          info.st_size < 0 || info.st_size > 4096) {
        ::close(fd);
        throw std::runtime_error("unsafe_logging_settings");
      }
      std::array<char, 4097> buffer{};
      std::size_t size = 0;
      while (size < buffer.size()) {
        const auto count =
            ::read(fd, buffer.data() + size, buffer.size() - size);
        if (count < 0) {
          if (errno == EINTR)
            continue;
          ::close(fd);
          throw std::runtime_error("logging_settings_unreadable");
        }
        if (!count)
          break;
        size += static_cast<std::size_t>(count);
      }
      ::close(fd);
      if (size > 4096)
        throw std::runtime_error("logging_settings_too_large");
      loaded = strict_json(std::string(buffer.data(), size));
      validate(loaded);
      if (loaded.at("collector_id") != self_)
        throw std::runtime_error("logging_collector_changed");
    }
    if (loaded != saved_) {
      saved_ = std::move(loaded);
      ++generation_;
    }
  }
  std::string etag() const { return wtp_catalog_revision(saved_); }
  void save(const Json &value) {
    const auto temp = path_ + ".tmp." + WtpPiBootIdentity{}.new_boot_id();
    const auto bytes = value.dump();
    const int fd =
        ::open(temp.c_str(),
               O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
      throw std::runtime_error("logging_save_unconfirmed");
    std::size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
      const auto count =
          ::write(fd, bytes.data() + offset, bytes.size() - offset);
      if (count <= 0) {
        ok = false;
        break;
      }
      offset += static_cast<std::size_t>(count);
    }
    if (::fsync(fd))
      ok = false;
    if (::close(fd))
      ok = false;
    if (!ok || ::rename(temp.c_str(), path_.c_str())) {
      ::unlink(temp.c_str());
      throw std::runtime_error("logging_save_unconfirmed");
    }
    const auto directory = std::filesystem::path(path_).parent_path();
    const int parent = ::open(directory.empty() ? "." : directory.c_str(),
                              O_RDONLY | O_DIRECTORY);
    if (parent < 0)
      throw std::runtime_error("logging_save_unconfirmed");
    const int sync = ::fsync(parent);
    ::close(parent);
    if (sync)
      throw std::runtime_error("logging_save_unconfirmed");
  }
  void poll(Source &source, const WtpSettings &settings, unsigned maximum,
            std::uint64_t now) {
    if (!allow_devices_) throw std::runtime_error("hardware_access_disabled");
    if (pico_usb_update_blocked(settings.usb_serial)) {
      source.close();
      source.state = "maintenance";
      return;
    }
    if (source.stream.state() != CdcState::Ready &&
        source.stream.state() != CdcState::Resetting) {
      if (now < source.retry)
        return;
      source.close();
      source.retry = now + 5000;
      source.lease = pico_usb_runtime_lease(settings.usb_serial);
      const auto selected = paired_linux_cdc(
          {settings.path, settings.usb_serial,
           static_cast<std::uint16_t>(settings.vendor_id),
           static_cast<std::uint16_t>(settings.product_id), PicoCdcRole::Wtp},
          PicoCdcRole::Logging);
      if (!source.stream.begin_open(selected, now))
        throw std::runtime_error("logging_busy_or_disconnected");
      source.deadline = now + 3000;
      source.state = "connecting";
    }
    source.stream.poll_open(now);
    if (source.stream.state() != CdcState::Ready)
      return;
    if (!source.status_sent ||
        (source.start_acked && !source.status_pending && now >= source.next_status &&
         source.output_offset == source.output.size())) {
      source.output = "SYSLOG STATUS\n";
      source.output_offset = 0;
      source.status_sent = source.status_pending = true;
      source.next_status = now + 5000;
      source.deadline = now + 3000;
    }
    if (source.output_offset < source.output.size()) {
      const auto bytes = std::span(
          reinterpret_cast<const std::uint8_t *>(source.output.data()),
          source.output.size());
      const auto result =
          source.stream.write(bytes.subspan(source.output_offset));
      if (result.state == wtp::IoState::Closed ||
          result.state == wtp::IoState::Failed)
        throw std::runtime_error("logging_disconnected");
      source.output_offset += result.count;
    }
    std::array<std::uint8_t, 1024> bytes{};
    const auto received = source.stream.read(bytes);
    if (received.state == wtp::IoState::Closed ||
        received.state == wtp::IoState::Failed)
      throw std::runtime_error("logging_disconnected");
    if ((!source.start_acked || source.status_pending) && now > source.deadline)
      throw std::runtime_error("unsupported_or_unresponsive_firmware");
    for (std::size_t index = 0; index < received.count; ++index) {
      const char byte = static_cast<char>(bytes[index]);
      if (byte != '\n') {
        if (!source.discarding && source.input.size() < 1536)
          source.input += byte;
        else {
          source.input.clear();
          source.discarding = true;
        }
        continue;
      }
      auto line = std::move(source.input);
      source.input.clear();
      if (source.discarding) {
        source.discarding = false;
        ++source.rejected;
        continue;
      }
      if (line.starts_with("{") && source.status_pending) {
        const auto reply = parse_usb_logging_status(line, settings.device_id, source.boot);
        const auto &target = reply.at("target");
        const auto observed_boot = target.at("boot_id").get<std::string>();
        if (!source.boot.empty() && source.boot != observed_boot)
          throw std::runtime_error("logging_identity_changed");
        const bool needs_start = source.boot.empty() || !reply.at("active").at("streaming").get<bool>();
        source.boot = observed_boot;
        source.status_pending = false;
        if (source.last_boot != source.boot) {
          source.last_sequence = 0;
          source.last_boot = source.boot;
        }
        source.export_enabled = reply.at("active").at("enabled").get<bool>();
        source.dropped = reply.at("active").at("dropped").get<std::string>();
        (void)decimal(Json(source.dropped));
        if (needs_start) source.start_acked = false;
        source.state = source.export_enabled
                           ? (source.start_acked ? "streaming" : "starting")
                           : "firmware_logging_disabled";
        if (needs_start) {
          source.deadline = now + 3000;
          source.output =
              "SYSLOG START " + settings.device_id + " " + source.boot + "\n";
          source.output_offset = 0;
        }
      } else if (!source.start_acked && line.starts_with("{")) {
        validate_usb_logging_start(line, settings.device_id, source.boot);
        source.start_acked = true;
        source.state = source.export_enabled ? "streaming" : "firmware_logging_disabled";
      } else if (line.starts_with("@SYSLOG ") && source.start_acked) {
        if (source.rate_window != now / 1000) {
          source.rate_window = now / 1000;
          source.rate_count = 0;
        }
        if (++source.rate_count > 16) {
          ++source.rejected;
          continue;
        }
        try {
          const auto event =
              parse_usb_log(line, settings.device_id, source.boot);
          const auto sequence = decimal(event.at("sequence"));
          if (sequence <= source.last_sequence) {
            ++source.rejected;
            continue;
          }
          source.last_sequence = sequence;
          source.name = event.at("origin_name").get<std::string>();
          source.dropped = event.at("dropped").get<std::string>();
          source.state = event.at("severity").get<unsigned>() > maximum ||
                                 project_journal(event, self_)
                             ? "streaming"
                             : "journal_unavailable";
        } catch (...) {
          ++source.rejected;
        }
      }
    }
  }
  void run() noexcept {
    std::map<std::string, std::unique_ptr<Source>> sources;
    Json profiles = Json::array();
    std::uint64_t scan = 0, applied_generation = 0;
    while (!quit_) {
      try {
        Json saved;
        std::uint64_t generation;
        {
          std::lock_guard lock(mutex_);
          saved = saved_;
          generation = generation_;
        }
        if (generation != applied_generation) {
          sources.clear();
          scan = 0;
          applied_generation = generation;
        }
        const auto now = millis();
        if (now >= scan) {
          WtpCatalog catalog(ini_ + ".wtp-devices.json");
          profiles = catalog.snapshot({}).first.at("profiles");
          scan = now + 5000;
        }
        Json status = Json::object();
        std::set<std::string> active;
        for (const auto &item : saved.at("sources")) {
          const auto id = item.at("device_id").get<std::string>();
          if (!item.at("enabled").get<bool>()) {
            status[id] = {{"state", "disabled"}};
            continue;
          }
          active.insert(id);
          auto &source = sources[id];
          if (!source)
            source = std::make_unique<Source>();
          try {
            bool found = false;
            for (const auto &profile : profiles)
              if (profile.at("id") == item.at("catalog_id")) {
                const auto settings =
                    parse_wtp_settings(profile.at("settings"), true);
                if (settings.transport != "usb" || settings.device_id != id)
                  throw std::runtime_error("logging_identity_changed");
                found = true;
                poll(*source, settings,
                     item.at("maximum_severity").get<unsigned>(), now);
              }
            if (!found)
              throw std::runtime_error("source_removed_from_fleet");
          } catch (const std::exception &error) {
            source->close();
            source->state = error.what();
            source->retry = now + 5000;
          }
          status[id] = {{"state", source->state},
                        {"boot_id", source->boot},
                        {"name", source->name},
                        {"rejected", std::to_string(source->rejected)},
                        {"dropped", source->dropped}};
        }
        for (auto it = sources.begin(); it != sources.end();)
          if (!active.contains(it->first))
            it = sources.erase(it);
          else
            ++it;
        {
          std::lock_guard lock(mutex_);
          status_ = std::move(status);
        }
      } catch (...) {
        sources.clear();
        std::lock_guard lock(mutex_);
        for (auto &state : status_)
          state["state"] = "collector_unavailable";
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
  std::string ini_, path_, self_;
  const bool allow_devices_;
  Json saved_ = {{"schema", "host-usb-logging/1"},
                 {"collector_id", self_},
                 {"sources", Json::array()}};
  Json status_ = Json::object();
  std::uint64_t generation_ = 1;
  std::mutex mutex_;
  std::atomic<bool> quit_{false};
  std::thread thread_;
};
std::mutex manager_mutex;
std::shared_ptr<Collector> manager;
} // namespace
Json usb_logging_attached_sources(const Json &discovery) {
  Json result = Json::array();
  if (!discovery.value("available", false)) return result;
  for (const auto &candidate : discovery.at("candidates")) {
    if (result.size() >= 64) break;
    const auto serial = candidate.at("settings").value("USB Serial", "");
    if (serial.size() != 16 || serial.find_first_not_of("0123456789ABCDEF") != std::string::npos)
      continue;
    std::string bytes = "WsprryPico/1";
    const std::string digits = "0123456789ABCDEF";
    for (std::size_t i = 0; i < serial.size(); i += 2)
      bytes += static_cast<char>((digits.find(serial[i]) << 4) | digits.find(serial[i + 1]));
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), digest);
    std::string id;
    for (unsigned i = 0; i < 16; ++i) {
      id += "0123456789abcdef"[digest[i] >> 4];
      id += "0123456789abcdef"[digest[i] & 15];
    }
    result.push_back({{"device_id", id}, {"name", "Attached Pico " + serial}});
  }
  return result;
}
namespace {
void logging_target(const Json &reply, const std::string &device, const std::string &boot) {
  if (!reply.is_object() || reply.size() != 3 ||
      !reply.contains("schema") || !reply.at("schema").is_string() ||
      reply.at("schema") != "usb-logging/1" || !reply.contains("target"))
    throw std::runtime_error("invalid_logging_handshake");
  const auto &target = reply.at("target");
  if (!target.is_object() || target.size() != 2 || !target.contains("device_id") ||
      !target.contains("boot_id") || !target.at("device_id").is_string() ||
      !target.at("boot_id").is_string() || target.at("device_id") != device ||
      !identity(target.at("boot_id").get<std::string>()) ||
      (!boot.empty() && target.at("boot_id") != boot))
    throw std::runtime_error("logging_identity_changed");
}
bool bounded_integer(const Json &value, std::uint64_t maximum) {
  return value.is_number_integer() &&
      (value.is_number_unsigned() || value.get<std::int64_t>() >= 0) &&
      value.get<std::uint64_t>() <= maximum;
}
} // namespace
Json parse_usb_logging_status(const std::string &line, const std::string &device,
                              const std::string &boot) {
  const auto reply = strict_json(line);
  logging_target(reply, device, boot);
  if (!reply.contains("active") || !reply.at("active").is_object())
    throw std::runtime_error("invalid_logging_status");
  const auto &active = reply.at("active");
  if (active.size() != 6 || !active.contains("enabled") || !active.contains("maximum_severity") ||
      !active.contains("streaming") || !active.contains("connected") ||
      !active.contains("queued") || !active.contains("dropped") ||
      !active.at("enabled").is_boolean() || !active.at("streaming").is_boolean() ||
      !active.at("connected").is_boolean() || !active.at("connected").get<bool>() ||
      !bounded_integer(active.at("maximum_severity"), 7) || !bounded_integer(active.at("queued"), 16))
    throw std::runtime_error("invalid_logging_status");
  (void)decimal(active.at("dropped"));
  return reply;
}
void validate_usb_logging_start(const std::string &line, const std::string &device,
                                const std::string &boot) {
  const auto reply = strict_json(line);
  logging_target(reply, device, boot);
  if (!reply.contains("streaming") || !reply.at("streaming").is_boolean() ||
      !reply.at("streaming").get<bool>())
    throw std::runtime_error("invalid_logging_start");
}
Json parse_usb_log(const std::string &line, const std::string &device,
                   const std::string &boot) {
  if (line.size() > 1536 || !line.starts_with("@SYSLOG ") ||
      !printable(line, 1536))
    throw std::runtime_error("invalid_log_frame");
  const auto space = line.find(' ', 8);
  if (space == std::string::npos ||
      decimal(Json(line.substr(8, space - 8))) != line.size() - space - 1)
    throw std::runtime_error("invalid_log_length");
  const auto record = line.substr(space + 1);
  const auto payload = record.find(" - {");
  if (payload == std::string::npos)
    throw std::runtime_error("invalid_log_header");
  const auto value = strict_json(record.substr(payload + 3));
  if (!value.is_object() || value.size() != 13 ||
      value.at("schema") != "wsprry-log/1" ||
      value.at("origin_kind") != "pico" || value.at("origin_id") != device ||
      value.at("boot_id") != boot || !identity(device) || !identity(boot) ||
      !value.at("severity").is_number_unsigned() ||
      value.at("severity").get<std::uint64_t>() > 7 || value.at("facility") != 23 ||
      !printable(value.at("event").get<std::string>(), 32) ||
      !printable(value.at("origin_name").get<std::string>(), 63) ||
      !printable(value.at("message").get<std::string>(), 192))
    throw std::runtime_error("invalid_log_record");
  if (!decimal(value.at("sequence")))
    throw std::runtime_error("invalid_log_sequence");
  (void)decimal(value.at("monotonic_ns"));
  (void)decimal(value.at("dropped"));
  if (!value.at("utc_ns").is_null())
    (void)decimal(value.at("utc_ns"));
  const auto prefix =
      "<" + std::to_string(184 + value.at("severity").get<unsigned>()) + ">1 ";
  if (!record.starts_with(prefix))
    throw std::runtime_error("log_priority_mismatch");
  std::string priority, stamp, hostname, app, process, event, extra;
  std::istringstream header(record.substr(0, payload));
  if (!(header >> priority >> stamp >> hostname >> app >> process >> event) ||
      header >> extra ||
      hostname != value.at("origin_name").get<std::string>() ||
      app != "WsprryPico" || process != "-" ||
      event != value.at("event").get<std::string>())
    throw std::runtime_error("invalid_log_header");
  std::string expected_stamp = "-";
  if (!value.at("utc_ns").is_null()) {
    const auto seconds =
        static_cast<std::time_t>(decimal(value.at("utc_ns")) / 1000000000ULL);
    std::tm time{};
    char text[32]{};
    if (!gmtime_r(&seconds, &time) ||
        !std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &time))
      throw std::runtime_error("invalid_log_time");
    expected_stamp = text;
  }
  if (stamp != expected_stamp)
    throw std::runtime_error("log_timestamp_mismatch");
  return value;
}
void start_usb_logging(const std::string &ini) {
  if (const auto *disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
      disabled && std::string(disabled) == "1")
    return;
  auto candidate = std::make_shared<Collector>(ini);
  std::shared_ptr<Collector> old;
  {
    std::lock_guard lock(manager_mutex);
    old.swap(manager);
    manager = std::move(candidate);
  }
}
void testing::start_usb_logging_policy(const std::string &ini, const std::string &machine_id_path) {
  auto candidate = std::make_shared<Collector>(ini, wtp_pi_device_id(machine_id_path), false);
  std::shared_ptr<Collector> old;
  {
    std::lock_guard lock(manager_mutex);
    old.swap(manager);
    manager = std::move(candidate);
  }
}
void stop_usb_logging() noexcept {
  std::shared_ptr<Collector> old;
  {
    std::lock_guard lock(manager_mutex);
    old.swap(manager);
  }
}
PicoHttpResponse usb_logging_api(const std::string &method,
                                 const std::string &body,
                                 const std::string &revision) {
  std::shared_ptr<Collector> current;
  {
    std::lock_guard lock(manager_mutex);
    current = manager;
  }
  if (!current)
    return {503, R"({"error":{"code":"usb_logging_unavailable"}})", {}};
  try {
    return current->request(method, body, revision);
  } catch (const std::exception &) {
    return {409, R"({"error":{"code":"logging_settings_rejected"}})", {}};
  }
}
} // namespace wsprrypi
