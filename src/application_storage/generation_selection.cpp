// SPDX-License-Identifier: MIT
#include "generation_selection.hpp"
#include "wtp_integration/browser_api.hpp"
#include <charconv>
#include <initializer_list>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw StateStorageError(StateStorageError::Code::Unavailable);}
void fields(const Json &value,std::initializer_list<const char *> names){
    require(value.is_object()&&value.size()==names.size());
    for(const auto *name:names)require(value.contains(name));
}
bool hex(const Json &value,std::size_t size){return value.is_string()&&value.get_ref<const std::string &>().size()==size&&
    value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
bool number(const Json &value){
    if(!value.is_string())return false;
    const auto &text=value.get_ref<const std::string &>();std::uint64_t result{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    return !text.empty()&&text.front()!='0'&&parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&result;
}
const char *role_name(StateRole role){require(role==StateRole::Pi||role==StateRole::Controller);return role==StateRole::Pi?"pi":"controller";}
}
Json parse_generation_selection(const std::string &bytes,const std::string &identity,StateRole role){
    require(bytes.size()<=1048576);const auto value=strict_browser_json(bytes);
    fields(value,{"schema","identity","role","generation","initialization","restore_job"});
    require(value["schema"]=="application-generation/1"&&value["identity"]==identity&&hex(value["identity"],32)&&
        value["role"]==role_name(role)&&number(value["generation"])&&hex(value["restore_job"],32));
    const auto &initial=value["initialization"];
    fields(initial,{"schema","identity","role","origin","source_namespace","generation","source_revision","images_digest"});
    require(initial["schema"]=="state-initialization/1"&&initial["identity"]==identity&&initial["role"]==value["role"]&&
        initial["generation"]=="1"&&hex(initial["source_revision"],64)&&hex(initial["images_digest"],64)&&initial["source_namespace"].is_string());
    const auto source=initial["source_namespace"].get<std::string>();
    require((initial["origin"]=="fresh"&&source=="fresh")||
        (initial["origin"]=="migration"&&!source.empty()&&source.front()=='/'&&source.find('\0')==std::string::npos));
    return value;
}
Json parse_restore_hold(const std::string &bytes,const std::string &identity,StateRole role,const std::string &generation){
    require(bytes.size()<=8192);const auto value=strict_browser_json(bytes);
    fields(value,{"schema","identity","role","generation","job_id","local_output","remote_authority","revision","stage"});
    require(value["schema"]=="application-restore-hold/1"&&value["identity"]==identity&&hex(value["identity"],32)&&
        value["role"]==role_name(role)&&value["generation"]==generation&&number(value["generation"])&&hex(value["job_id"],32)&&
        value["local_output"].is_boolean()&&value["remote_authority"].is_boolean()&&number(value["revision"])&&
        (value["stage"]=="review"||(value["stage"]=="maintenance"&&value["local_output"]==true&&value["remote_authority"]==true)));
    return value;
}
}
