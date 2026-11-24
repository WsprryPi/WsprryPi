// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <set>
#include <stdexcept>
#include <string>
namespace wsprrypi {
inline std::string parse_controller_identity_record(const std::string &bytes) {
    const auto require=[](bool valid){if(!valid)throw std::runtime_error("controller_identity_storage_hold");};
    require(!bytes.empty()&&bytes.size()<=512);
    std::set<std::string> keys;
    const auto record=nlohmann::json::parse(bytes,[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &value){
        require(depth<=1);
        if(event==nlohmann::json::parse_event_t::key)require(keys.insert(value.get<std::string>()).second);
        return true;
    });
    require(record.is_object()&&record.size()==2&&record.contains("schema")&&record.contains("device_id")&&
        record["schema"]=="controller-identity/1"&&record["device_id"].is_string());
    const auto identity=record["device_id"].get<std::string>();
    require(identity.size()==32&&identity.find_first_not_of("0123456789abcdef")==std::string::npos&&
        identity!=std::string(32,'0')&&identity!=std::string(32,'f'));
    return identity;
}
}
