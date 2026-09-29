// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <functional>
#include <mutex>
#include <string>
namespace wsprrypi {
class WtpFleetStore {
public:
    explicit WtpFleetStore(std::string path, std::string self_id);
    std::pair<nlohmann::json, std::string> snapshot();
    std::pair<nlohmann::json, std::string> update(const std::string& revision,
        const std::function<void(nlohmann::json&)>& mutation);
    void revoke(const std::string& device_id, const std::string& generation);
    static void validate(const nlohmann::json& data, const std::string& self_id);
private:
    void load();
    void save(const nlohmann::json&);
    std::string path_, self_id_;
    bool loaded_ = false;
    nlohmann::json data_;
    std::mutex mutex_;
};
std::uint64_t wtp_fleet_decimal(const nlohmann::json& value);
}
