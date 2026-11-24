// SPDX-License-Identifier: MIT
#pragma once
#include "local_transport.hpp"
#include "native_random.hpp"
#include <array>
#include <chrono>
#include <set>
#include <string_view>
#include <vector>
namespace wsprrypi::local_protocol {
using Json=nlohmann::json;
using Code=LocalControlError::Code;
// Browser messages retain the existing 64 KiB frame limit. JSON wrapping can
// double quotes/escapes; keep explicit envelope headroom on that channel only.
constexpr std::size_t maximum=144*1024;
constexpr std::string_view protocol="wsprrypi-local/1";
using Deadline=std::chrono::steady_clock::time_point;
inline void require(bool value,Code code=Code::Protocol){if(!value)throw LocalControlError(code);}
inline bool fields(const Json &v,std::initializer_list<const char *> names){
    if(!v.is_object()||v.size()!=names.size())return false;
    for(const auto *name:names)if(!v.contains(name))return false;
    return true;
}
inline bool identity(const Json &v){return v.is_string()&&v.get_ref<const std::string &>().size()==32&&
    v.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
inline std::string token(){
    std::array<std::uint8_t,16> bytes{};
    try{native_random(bytes);}catch(const std::runtime_error &){throw LocalControlError(Code::Unavailable);}
    std::string result;
    for(const auto byte:bytes){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}
    return result;
}
template<class Transfer> void send_frame(Transfer &io,const Json &value,Deadline deadline){
    auto bytes=value.dump();require(bytes.size()<=maximum);
    std::array<unsigned char,4> header{static_cast<unsigned char>(bytes.size()>>24),static_cast<unsigned char>(bytes.size()>>16),
        static_cast<unsigned char>(bytes.size()>>8),static_cast<unsigned char>(bytes.size())};
    io(header.data(),header.size(),true,deadline);io(bytes.data(),bytes.size(),true,deadline);
}
template<class Transfer> Json receive_frame(Transfer &io,Deadline deadline,std::size_t bound=32768){
    std::array<unsigned char,4> header{};io(header.data(),header.size(),false,deadline);
    const auto size=(std::uint32_t(header[0])<<24)|(std::uint32_t(header[1])<<16)|(std::uint32_t(header[2])<<8)|header[3];
    require(size>0&&size<=bound);
    std::string bytes(size,'\0');io(bytes.data(),bytes.size(),false,deadline);
    std::vector<std::set<std::string>> keys;
    try{
        return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &parsed){
            require(depth<=32);
            if(event==Json::parse_event_t::object_start)keys.emplace_back();
            else if(event==Json::parse_event_t::key)require(!keys.empty()&&keys.back().insert(parsed.get<std::string>()).second);
            else if(event==Json::parse_event_t::object_end){require(!keys.empty());keys.pop_back();}
            return true;
        });
    }catch(const LocalControlError &){throw;}catch(...){throw LocalControlError(Code::Protocol);}
}
inline const char *name(LocalOperation operation){
    switch(operation){case LocalOperation::DiagnosticRead:return "diagnostic.read";case LocalOperation::SettingsRead:return "settings.read";case LocalOperation::SettingsWrite:return "settings.write";case LocalOperation::RouteRead:return "maintenance.route.read";case LocalOperation::ListenerRead:return "listeners.read";case LocalOperation::EventsRead:return "events.read";case LocalOperation::BrowserOpen:return "browser.open";case LocalOperation::BrowserMessage:return "browser.message";case LocalOperation::HttpOpen:return "http.open";case LocalOperation::HttpBody:return "http.body";case LocalOperation::HttpDispatch:return "http.dispatch";case LocalOperation::HttpRead:return "http.read";case LocalOperation::HttpComplete:return "http.complete";case LocalOperation::WebBootstrap:return "web.bootstrap";case LocalOperation::WebAttach:return "web.attach";case LocalOperation::WebHeartbeat:return "web.heartbeat";case LocalOperation::BrowserCheck:return "browser.check";case LocalOperation::RecoveryCapture:return "recovery.capture";case LocalOperation::RecoveryObserve:return "recovery.observe";case LocalOperation::RecoveryValidate:return "recovery.validate";case LocalOperation::RecoveryPrepare:return "recovery.prepare";case LocalOperation::RecoveryReview:return "recovery.review";case LocalOperation::RecoveryCommand:return "recovery.command";case LocalOperation::RecoverySessionIssue:return "recovery.session.issue";}
    throw LocalControlError(Code::Protocol);
}
inline LocalOperation operation(const Json &value){
    require(value.is_string());for(const auto item:{LocalOperation::SettingsRead,LocalOperation::SettingsWrite,LocalOperation::DiagnosticRead,LocalOperation::RouteRead,LocalOperation::ListenerRead,LocalOperation::EventsRead,LocalOperation::BrowserOpen,LocalOperation::BrowserMessage,LocalOperation::HttpOpen,LocalOperation::HttpBody,LocalOperation::HttpDispatch,LocalOperation::HttpRead,LocalOperation::HttpComplete,LocalOperation::WebBootstrap,LocalOperation::WebAttach,LocalOperation::WebHeartbeat,LocalOperation::BrowserCheck,LocalOperation::RecoveryCapture,LocalOperation::RecoveryObserve,LocalOperation::RecoveryValidate,LocalOperation::RecoveryPrepare,LocalOperation::RecoveryReview,LocalOperation::RecoveryCommand,LocalOperation::RecoverySessionIssue})if(value==name(item))return item;
    throw LocalControlError(Code::Protocol);
}
inline bool http_operation(LocalOperation value){
    return value==LocalOperation::HttpOpen||value==LocalOperation::HttpBody||value==LocalOperation::HttpDispatch||value==LocalOperation::HttpRead||value==LocalOperation::HttpComplete;
}
inline bool mutation(LocalOperation value){return value==LocalOperation::SettingsWrite||value==LocalOperation::BrowserMessage||value==LocalOperation::HttpDispatch||value==LocalOperation::HttpComplete||value==LocalOperation::RecoveryCapture||value==LocalOperation::RecoveryValidate||value==LocalOperation::RecoveryPrepare||value==LocalOperation::RecoveryReview||value==LocalOperation::RecoveryCommand||value==LocalOperation::RecoverySessionIssue;}
inline const char *refusal_name(LocalRefusal value){
    switch(value){case LocalRefusal::InvalidRequest:return "invalid_request";case LocalRefusal::RevisionConflict:return "revision_conflict";
        case LocalRefusal::Busy:return "busy";case LocalRefusal::SetupRequired:return "setup_required";case LocalRefusal::StorageUnavailable:return "storage_unavailable";}
    return "command_refused";
}
inline bool refusal_value(const Json &value){
    if(!value.is_string())return false;
    for(const auto *name:{"command_refused","invalid_request","revision_conflict","busy","setup_required","storage_unavailable","outcome_unknown"})if(value==name)return true;
    return false;
}
}
