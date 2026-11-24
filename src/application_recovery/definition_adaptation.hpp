// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <filesystem>
#include <charconv>
#include <cstdint>
#include <set>
#include <stdexcept>
namespace wsprrypi {
// A moved USB definition requires explicit destination selection. Keep its
// unavailable reference in the destination namespace; never discover a device
// by serial alone or retain a source host's endpoint as an active connection.
inline std::string recovery_unresolved_usb_endpoint(const std::string &profile){
    if(profile.size()!=32||profile.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::invalid_argument("Invalid unresolved recovery profile");
#ifdef _WIN32
    return "\\\\?\\wsprrypi-unresolved-"+profile;
#else
    return "/dev/wsprrypi-unresolved-"+profile;
#endif
}
// Inputs have already passed the actual catalog validator. Matching never
// substitutes a different device or carrier to make a saved definition work.
inline const nlohmann::json *recovery_definition_match(const nlohmann::json &source,const nlohmann::json &destination){
    for(const auto &row:destination)if(row.at("id")==source.at("id")&&row.at("settings").at("Device ID")==source.at("settings").at("Device ID")&&
        row.at("settings").at("Transport")==source.at("settings").at("Transport"))return &row;
    return nullptr;
}
// Existing catalog credential paths remain destination-owned. Retention must
// preserve even an inactive profile's explicit reference into an older generation.
inline std::set<std::string> recovery_history_dependencies(const nlohmann::json &profiles,const std::filesystem::path &root){
    std::set<std::string> generations;
    const auto base=std::filesystem::canonical(root);
    for(const auto &row:profiles)for(const auto *key:{"TLS CA File","TLS Client Certificate","TLS Client Key"}){
        const auto path=row.at("settings").at(key).get<std::string>();if(path.empty())continue;
        const auto relative=std::filesystem::weakly_canonical(path).lexically_relative(base);
        if(relative.empty())continue;
        const auto first=relative.begin()->string();
        if(!first.starts_with("generation-"))continue;
        const auto generation=first.substr(11);
        std::uint64_t number{};const auto parsed=std::from_chars(generation.data(),generation.data()+generation.size(),number);
        if(!generation.empty()&&generation.front()!='0'&&number>0&&parsed.ec==std::errc{}&&parsed.ptr==generation.data()+generation.size())
            generations.insert(generation);
    }
    return generations;
}
inline std::string recovery_definition_resolution(const nlohmann::json &source,const nlohmann::json &destination,bool moving){
    if(source.at("legacy_unverified")==true)return "unverified_definition";
    if(recovery_definition_match(source,destination))return "preserved_destination_connection";
    if(source.at("settings").at("Transport")=="network")return "destination_connection_required";
    return moving?"destination_transport_required":"source_usb_connection";
}
}
