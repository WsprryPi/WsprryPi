// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include "application_control/local_transport.hpp"
#include <set>
#include <vector>
namespace wsprrypi {
// Private archive/maintenance documents have independent bounded sizes. Do not
// relax the shared control protocol's 32 KiB parser to carry encrypted chunks.
inline nlohmann::json parse_recovery_json(const std::string &bytes,std::size_t maximum,unsigned maximum_depth=12){
    const auto require=[](bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);};
    require(maximum>0&&maximum<=1048576&&bytes.size()<=maximum&&maximum_depth<=32);
    std::vector<std::set<std::string>> objects;
    try{return nlohmann::json::parse(bytes,[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &item){
        require(depth>=0&&static_cast<unsigned>(depth)<=maximum_depth);
        if(event==nlohmann::json::parse_event_t::object_start)objects.emplace_back();
        else if(event==nlohmann::json::parse_event_t::key)require(!objects.empty()&&objects.back().insert(item.get<std::string>()).second);
        else if(event==nlohmann::json::parse_event_t::object_end){require(!objects.empty());objects.pop_back();}
        return true;
    });}catch(const nlohmann::json::exception &){throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
}
}
