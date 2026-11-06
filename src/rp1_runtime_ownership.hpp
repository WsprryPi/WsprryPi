/* SPDX-License-Identifier: MIT */
#pragma once

#include "WSPR-Transmitter/src/rp1_gpclk_consumer_contract.hpp"
#include "json.hpp"
#include <cerrno>
#include <fcntl.h>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace wsprrypi {
// Production uses the fixed root-owned path. An explicit owner lets ordinary
// host tests exercise this same file reader against their temporary records.
inline std::string readRp1RuntimeOwnership(const char *path,
                                           uid_t expected_owner = 0) {
  const int fd = ::open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0)
    throw std::runtime_error("WsprryPi runtime ownership is unavailable.");
  struct Descriptor {
    int value;
    ~Descriptor() { ::close(value); }
  } descriptor{fd};
  struct stat info {};
  if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_uid != expected_owner || info.st_nlink != 1 ||
      (info.st_mode & 0077) != 0 || info.st_size <= 0 ||
      info.st_size > 4 * 1024 * 1024)
    throw std::runtime_error("WsprryPi runtime ownership is unsafe.");

  std::string payload;
  payload.reserve(static_cast<std::size_t>(info.st_size));
  char buffer[4096];
  for (;;) {
    const auto count = ::read(fd, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0)
      throw std::runtime_error("Could not read WsprryPi runtime ownership.");
    if (count == 0)
      break;
    payload.append(buffer, static_cast<std::size_t>(count));
    if (payload.size() > 4U * 1024U * 1024U)
      throw std::runtime_error("WsprryPi runtime ownership exceeded its bound.");
  }
  std::vector<std::set<std::string>> keys;
  const auto record = nlohmann::json::parse(payload,
      [&keys](int, nlohmann::json::parse_event_t event, nlohmann::json &value) {
        if (event == nlohmann::json::parse_event_t::object_start)
          keys.emplace_back();
        else if (event == nlohmann::json::parse_event_t::object_end)
          keys.pop_back();
        else if (event == nlohmann::json::parse_event_t::key &&
                 !keys.back().insert(value.get<std::string>()).second)
          throw std::runtime_error("Duplicate runtime ownership field.");
        return true;
      });
  if (!record.is_object() ||
      record.value("schema", std::string{}) !=
          "rp1-gpclk-dkms-provider-installation-v1" ||
      record.value("repository", std::string{}) !=
          Rp1GpclkConsumerContract::repository ||
      record.value("owner", std::string{}) != "RP1-GPIO-CLK-DKMS" ||
      record.value("client", std::string{}) != "WsprryPi/WsprryPi" ||
      !record.contains("runtime") || !record["runtime"].is_object())
    throw std::runtime_error("WsprryPi runtime ownership contract differs.");
  const auto &runtime = record["runtime"];
  const auto digest = runtime.contains("bindingSha256") &&
                              runtime["bindingSha256"].is_string()
                          ? runtime["bindingSha256"].get<std::string>()
                          : std::string{};
  if (runtime.value("readinessContract", std::string{}) !=
          "rp1-gpclk-runtime-readiness-v1" ||
      !Rp1GpclkConsumerContract::validProduct(runtime.value("productVersion", std::string{})) ||
      runtime.value("productVersion", std::string{}) !=
          (record.value("channel", std::string{}) == "development" ?
               record.value("productVersion", std::string{}) :
               record.value("manifest", nlohmann::json::object()).value("productVersion", std::string{})) ||
      runtime.value("state", std::string{}) != "neutral_ready" ||
      !runtime.contains("route") || !runtime["route"].is_null() ||
      runtime.value("output", std::string{}) != "disabled" ||
      digest.size() != 64 ||
      digest.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw std::runtime_error("WsprryPi neutral runtime ownership differs.");
  return digest;
}
} // namespace wsprrypi
