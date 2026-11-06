/* SPDX-License-Identifier: MIT */
#include "../rp1_runtime_ownership.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}
struct Fixture {
  std::filesystem::path directory;
  std::filesystem::path record;
  Fixture() {
    const char *base = std::getenv("TMPDIR");
    std::string pattern = std::string(base ? base : "/tmp") +
                          "/wsprrypi-runtime-ownership.XXXXXX";
    char *created = ::mkdtemp(pattern.data());
    require(created != nullptr, "temporary ownership directory creation failed");
    directory = created;
    record = directory / "ownership.json";
  }
  ~Fixture() { std::filesystem::remove_all(directory); }
  void write(const std::string &value) {
    std::ofstream stream(record);
    stream << value;
    stream.close();
    require(stream.good() && ::chmod(record.c_str(), 0600) == 0,
            "temporary ownership write failed");
  }
  void rejects(const std::string &label, const std::filesystem::path &path = {}) {
    try {
      (void)wsprrypi::readRp1RuntimeOwnership(
          (path.empty() ? record : path).c_str(), ::getuid());
    } catch (const std::exception &) {
      return;
    }
    throw std::runtime_error("ownership reader accepted " + label);
  }
};
nlohmann::json owned() {
  return {{"schema", "rp1-gpclk-dkms-provider-installation-v1"},
          {"repository", wsprrypi::Rp1GpclkConsumerContract::repository},
          {"owner", "RP1-GPIO-CLK-DKMS"}, {"client", "WsprryPi/WsprryPi"},
          {"channel", "development"}, {"productVersion", "12.34.56"},
          {"runtime", {{"readinessContract", "rp1-gpclk-runtime-readiness-v1"},
                       {"productVersion", "12.34.56"},
                       {"state", "neutral_ready"}, {"route", nullptr},
                       {"output", "disabled"}, {"bindingSha256", std::string(64, 'a')}}}};
}
} // namespace

int main() try {
  Fixture fixture;
  const auto current = owned();
  fixture.write(current.dump());
  require(wsprrypi::readRp1RuntimeOwnership(fixture.record.c_str(), ::getuid()) ==
              std::string(64, 'a'), "current provider ownership was refused");
  if (::getuid() == 0) {
    require(wsprrypi::readRp1RuntimeOwnership(fixture.record.c_str()) ==
                std::string(64, 'a'), "production root ownership was refused");
  } else {
    bool denied = false;
    try { (void)wsprrypi::readRp1RuntimeOwnership(fixture.record.c_str()); }
    catch (const std::exception &) { denied = true; }
    require(denied, "production default accepted non-root ownership");
  }
  for (const auto &repository : {"WsprryPi/RP1-DKMS", "WsprryPi/RP1-GPCLK-DKMS", "foreign/provider"}) {
    auto value = current;
    value["repository"] = repository;
    fixture.write(value.dump());
    fixture.rejects(repository);
  }
  for (const auto &change : std::vector<std::pair<std::string, nlohmann::json>>{
           {"schema", "unknown"}, {"owner", "foreign"}, {"runtime", nullptr}}) {
    auto value = current;
    value[change.first] = change.second;
    fixture.write(value.dump());
    fixture.rejects("invalid " + change.first);
  }
  for (const auto &change : std::vector<std::pair<std::string, nlohmann::json>>{
           {"readinessContract", "unknown"}, {"productVersion", "1.0.1"},
           {"state", "unknown"}, {"route", "gpio4"}, {"output", "enabled"},
           {"bindingSha256", std::string(63, 'a')},
           {"bindingSha256", std::string(64, 'A')}, {"bindingSha256", 7}}) {
    auto value = current;
    value["runtime"][change.first] = change.second;
    fixture.write(value.dump());
    fixture.rejects("invalid runtime " + change.first);
  }
  fixture.write("{\"repository\":\"first\",\"repository\":\"second\"}");
  fixture.rejects("duplicate fields");
  fixture.write("[]"); fixture.rejects("non-object JSON");
  fixture.write("{"); fixture.rejects("malformed JSON");
  fixture.write(""); fixture.rejects("empty record");
  fixture.write(current.dump());
  require(::chmod(fixture.record.c_str(), 0640) == 0, "permission fixture failed");
  fixture.rejects("group-readable record");
  require(::chmod(fixture.record.c_str(), 0600) == 0, "permission reset failed");
  const auto alias = fixture.directory / "alias";
  require(::symlink(fixture.record.c_str(), alias.c_str()) == 0, "symlink fixture failed");
  fixture.rejects("symlink", alias);
  std::filesystem::remove(alias);
  require(::link(fixture.record.c_str(), alias.c_str()) == 0, "hard-link fixture failed");
  fixture.rejects("multiply linked record");
  std::filesystem::remove(alias);
  fixture.rejects("directory", fixture.directory);
  require(::mkfifo(alias.c_str(), 0600) == 0, "FIFO fixture failed");
  fixture.rejects("FIFO", alias);
  std::filesystem::remove(alias);
  require(::truncate(fixture.record.c_str(), 4 * 1024 * 1024 + 1) == 0,
          "oversized fixture failed");
  fixture.rejects("oversized record");
  std::filesystem::remove(fixture.record);
  fixture.rejects("missing record");
  std::cout << "Production RP1 runtime ownership reader tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
