// SPDX-License-Identifier: MIT
#pragma once
#include "application_control/local_transport.hpp"
#include <set>
#include <vector>
namespace wsprrypi::recovery_worker_protocol {
inline void require(bool valid){if(!valid)throw LocalControlError(LocalControlError::Code::Protocol);}
inline std::string request_frame(const nlohmann::json &value){auto bytes=value.dump()+"\n";require(bytes.size()<=144*1024);return bytes;}
inline std::string configuration_frame(const nlohmann::json &value){
    const auto bytes=value.dump();require(bytes.size()<=32768);std::string frame(4,'\0');
    for(unsigned index=0;index<4;++index)frame[index]=static_cast<char>((bytes.size()>>(24-index*8))&255);
    return frame+bytes;
}
inline nlohmann::json reply(const std::string &bytes){
    require(!bytes.empty()&&bytes.size()<=1024*1024&&bytes.find('\n')==bytes.size()-1);
    std::vector<std::set<std::string>> keys;
    auto result=nlohmann::json::parse(bytes,[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &item){
        require(depth<=32);if(event==nlohmann::json::parse_event_t::object_start)keys.emplace_back();
        else if(event==nlohmann::json::parse_event_t::key)require(!keys.empty()&&keys.back().insert(item.get<std::string>()).second);
        else if(event==nlohmann::json::parse_event_t::object_end){require(!keys.empty());keys.pop_back();}
        return true;
    });
    require(result.is_object()&&result.size()==2&&result.contains("ok")&&result["ok"].is_boolean());
    require((result["ok"]==true&&result.contains("value"))||(result["ok"]==false&&result.contains("error")&&result["error"].is_string()));
    return result;
}
inline void startup_ready(const nlohmann::json &result){
    require(result.at("ok")==true);const auto &value=result.at("value");
    require(value.is_object()&&value.size()==3&&value.value("schema",nlohmann::json(nullptr))=="recovery-job-list/1"&&
        value.contains("active")&&value.contains("jobs")&&value["jobs"].is_array());
    const auto &active=value["active"];
    require(active.is_null()||(active.is_string()&&active.get_ref<const std::string &>().size()==32&&
        active.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos));
}
}
