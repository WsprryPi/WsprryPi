// SPDX-License-Identifier: MIT
#include "controller_contract.hpp"
#include "control_contract.hpp"
#include "browser_api.hpp"
#include <charconv>
#include <cmath>
#include <cstdint>
#include <regex>
#include <set>
#include <map>
#include <mutex>
#include <memory>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
bool pattern_matches(const std::string &value,const std::string &pattern) {
    // Avoid input-sized regex stack use for the exact approved decimal-Hz
    // shape. The owning backend still validates the numeric frequency range.
    if(pattern=="^(?:[1-9][0-9]*|(?:0|[1-9][0-9]*)\\.[0-9]{0,8}[1-9])$"){
        const auto dot=value.find('.');
        const auto integer=std::string_view(value).substr(0,dot);
        if(integer.empty()||integer.find_first_not_of("0123456789")!=std::string_view::npos||
            (integer.size()>1&&integer.front()=='0'))return false;
        if(dot==std::string::npos)return integer!="0";
        const auto fraction=std::string_view(value).substr(dot+1);
        return !fraction.empty()&&fraction.size()<=9&&fraction.back()!='0'&&
            fraction.find_first_not_of("0123456789")==std::string_view::npos;
    }
    static std::mutex mutex;static std::map<std::string,std::shared_ptr<const std::regex>> patterns;
    std::shared_ptr<const std::regex> compiled;
    {std::lock_guard lock(mutex);auto &entry=patterns[pattern];if(!entry)entry=std::make_shared<const std::regex>(pattern);compiled=entry;}
    return std::regex_match(value,*compiled);
}
bool match(const Json &value,const Json &rule,const Json &definitions,unsigned depth) {
    if(depth>16||!rule.is_object())return false;
    if(rule.contains("$ref")) {
        const auto ref=rule["$ref"].get<std::string>();const std::string prefix="#/$defs/";
        if(!ref.starts_with(prefix)||!definitions.contains(ref.substr(prefix.size())))return false;
        return match(value,definitions[ref.substr(prefix.size())],definitions,depth+1);
    }
    for(const auto *key:{"allOf","anyOf","oneOf"})if(rule.contains(key)) {
        unsigned count=0;for(const auto &branch:rule[key])if(match(value,branch,definitions,depth+1))++count;
        if((std::string_view(key)=="allOf"&&count!=rule[key].size())||(std::string_view(key)=="anyOf"&&!count)||
            (std::string_view(key)=="oneOf"&&count!=1))return false;
    }
    if(rule.contains("const")&&value!=rule["const"])return false;
    if(rule.contains("enum")&&std::find(rule["enum"].begin(),rule["enum"].end(),value)==rule["enum"].end())return false;
    if(rule.contains("type")) {
        const auto type=rule["type"].get<std::string>();
        if(!((type=="object"&&value.is_object())||(type=="array"&&value.is_array())||(type=="string"&&value.is_string())||
             (type=="boolean"&&value.is_boolean())||(type=="null"&&value.is_null())||(type=="integer"&&value.is_number_integer())||
             (type=="number"&&value.is_number()&&std::isfinite(value.get<double>()))))return false;
    }
    if(value.is_object()) {
        if(rule.contains("required"))for(const auto &key:rule["required"])if(!value.contains(key.get<std::string>()))return false;
        const auto properties=rule.value("properties",Json::object());
        for(const auto &[key,item]:value.items()) {
            if(properties.contains(key)) {if(!match(item,properties[key],definitions,depth+1))return false;}
            else if(rule.value("additionalProperties",true)==false)return false;
        }
    }
    if(value.is_array()) {
        if(value.size()<rule.value("minItems",0U)||value.size()>rule.value("maxItems",512U))return false;
        if(rule.contains("items"))for(const auto &item:value)if(!match(item,rule["items"],definitions,depth+1))return false;
    }
    if(value.is_string()) {
        const auto &text=value.get_ref<const std::string&>();
        // Strict serialization rejects invalid UTF-8 in constructed values.
        (void)value.dump();std::size_t characters=0;
        std::uint32_t scalar{};
        for(std::size_t i=0;i<text.size();++i) {
            const auto c=static_cast<unsigned char>(text[i]);
            if((c&0xc0)!=0x80){++characters;scalar=c<128?c:c<224?c&31:c<240?c&15:c&7;}
            else scalar=(scalar<<6)|(c&63);
            if(rule.value("x-noControls",false)&&(i+1==text.size()||(static_cast<unsigned char>(text[i+1])&0xc0)!=0x80)&&
                (scalar<32||(scalar>=127&&scalar<=159)))return false;
        }
        if(characters<rule.value("minLength",0U)||characters>rule.value("maxLength",32768U)||
            text.size()>rule.value("x-maxUtf8Bytes",32768U))return false;
        if(rule.contains("pattern")&&!rule.value("x-noControls",false)&&!pattern_matches(text,rule["pattern"].get<std::string>()))return false;
        if(rule.contains("x-maximum")) {
            std::uint64_t n{},maximum{};const auto r=std::from_chars(text.data(),text.data()+text.size(),n);
            const auto bound=rule["x-maximum"].get<std::string>();const auto b=std::from_chars(bound.data(),bound.data()+bound.size(),maximum);
            if(r.ec!=std::errc{}||r.ptr!=text.data()+text.size()||b.ec!=std::errc{}||n>maximum)return false;
        }
    }
    if(value.is_number()) {
        if((rule.contains("minimum")&&value<rule["minimum"])||(rule.contains("maximum")&&value>rule["maximum"]))return false;
    }
    return true;
}
}
bool valid_controller_document(const nlohmann::json &value,std::string_view name) {
    try {
        static const auto adapter=Json::parse(controller_adapter_contract());
        static const auto definitions=Json::parse(controller_document_definitions());
        const auto &documents=adapter["documents"];
        if(std::find(documents.begin(),documents.end(),name)==documents.end()||!definitions.contains(std::string(name))||
            !match(value,definitions[std::string(name)],definitions,0))return false;
        if(name=="targets") {std::set<std::string> ids;for(const auto &target:value["targets"])if(!ids.insert(target["device_id"]).second)return false;}
        if(name=="target-context") {
            for(const auto *field:{"device_id","profile_generation","connection_generation"})
                if(value["target"][field]!=value["binding"][field])return false;
            std::set<std::string> resources;
            for(const auto &capability:value["capabilities"])
                if(!resources.insert(capability["resource"].get<std::string>()).second||
                    (capability["activation"]=="unsupported"&&capability["write"]==true))return false;
        }
        if(name=="target-status"&&value["execution_owner"]!=value["binding"]["device_id"])return false;
        if(name=="host-readiness"){
            if(value["state"]=="ready"&&value["device_id"].is_null())return false;
            if(value["recovery_editable"]==true&&(value["role"]!="transmitter"||value["state"]!="configuration_recovery"||value["device_id"].is_null()))return false;
            if(value["role"]=="controller-only"&&(value["state"]=="configuration_recovery"||value["state"]=="output_recovery"))return false;
        }
        if(name=="configuration-recovery"){
            if(value["writable"]==true&&(value["binding"].is_null()||value["validation"]=="storage_unavailable"))return false;
            std::set<std::string> paths;
            for(const auto &path:value["defaulted_fields"])if(!paths.insert(path.get<std::string>()).second)return false;
        }
        return true;
    }catch(...){return false;}
}
bool valid_pi_configuration_field(std::string_view section,std::string_view key,const Json &value){
    try{
        static const auto definitions=Json::parse(controller_document_definitions());
        const auto &sections=definitions["pi-config"]["properties"];
        const auto group=std::string(section),field=std::string(key);
        if(!sections.contains(group)||!sections[group]["properties"].contains(field))return false;
        return match(value,sections[group]["properties"][field],definitions,0);
    }catch(...){return false;}
}
bool valid_pi_resource_capability(const Json &value) {
    try{
        static const auto definitions=Json::parse(controller_document_definitions());
        if(!match(value,definitions["target-capability"],definitions,0))return false;
        const auto descriptor=pi_resource_descriptor(value["resource"].get<std::string>());
        if(descriptor.empty())return false;
        const auto expected=Json::parse(descriptor);
        return value["schema_id"]==expected["schema_id"]&&value["schema_digest"]==expected["schema_digest"]&&
            (value["activation"]!="unsupported"||value["write"]==false);
    }catch(...){return false;}
}
bool valid_pi_candidate_shape(const nlohmann::json &value) {
    try {
        static const auto definitions=Json::parse(controller_document_definitions());
        return match(value,definitions.at("pi-candidate"),definitions,0);
    }catch(...){return false;}
}
bool valid_pi_scheduling_shape(const Json &value) {
    try{static const auto definitions=Json::parse(controller_document_definitions());
        return match(value,definitions.at("pi-scheduling"),definitions,0);
    }catch(...){return false;}
}
bool valid_pico_network_observation(const nlohmann::json &value) {
    try {
        if(!value.is_object()||value.value("contract",Json(nullptr))!="Wsprry-Control/1")return false;
        auto payload=value;payload.erase("contract");
        static const auto shape=Json::parse(network_diagnostics_shape());
        return match(payload,shape,Json::object(),0);
    }catch(...){return false;}
}
std::string canonical_pi_management_json(const nlohmann::json &value) {
    const auto normalize=[](const auto &self,const Json &input,unsigned depth)->Json {
        if(depth>16)throw std::invalid_argument("canonical_nesting_limit");
        if(input.is_object()) {auto result=Json::object();for(const auto &[key,item]:input.items())result[key]=self(self,item,depth+1);return result;}
        if(input.is_array()) {auto result=Json::array();for(const auto &item:input)result.push_back(self(self,item,depth+1));return result;}
        constexpr std::int64_t exact=9007199254740991LL;
        if(input.is_number_integer()) {
            if(input<-exact||input>exact)throw std::invalid_argument("canonical_integer_range");
        } else if(input.is_number_float()) {
            const auto number=input.get<double>();if(!std::isfinite(number))throw std::invalid_argument("canonical_nonfinite_number");
            if(number>=-static_cast<double>(exact)&&number<=static_cast<double>(exact)&&std::trunc(number)==number)return Json(static_cast<std::int64_t>(number));
        }
        return input;
    };
    const auto encoded=normalize(normalize,value,0).dump();std::string result;
    for(std::size_t index=0;index<encoded.size();++index) {
        if(encoded[index]!='\\'||index+1==encoded.size()){result+=encoded[index];continue;}
        const auto escaped=encoded[++index];
        switch(escaped) {
            case 'b':result+="\\u0008";break;case 't':result+="\\u0009";break;case 'n':result+="\\u000a";break;
            case 'f':result+="\\u000c";break;case 'r':result+="\\u000d";break;
            default:result+='\\';result+=escaped;break;
        }
    }
    return result;
}
PicoHttpResponse controller_target_error(unsigned status,std::string_view requested) {
    static const auto definitions=Json::parse(controller_document_definitions());
    const auto &codes=definitions["target-operation"]["properties"]["error"]["anyOf"][1]["properties"]["code"]["enum"];
    std::string code(requested);
    if(std::find(codes.begin(),codes.end(),code)==codes.end()) {
        switch(status) {
        case 400:code="invalid_shape";break;
        case 401:code="unauthenticated";break;
        case 403:code="unauthorized";break;
        case 404:case 405:code="unsupported_resource";break;
        case 409:code="connection_changed";break;
        case 412:code="revision_conflict";break;
        case 413:case 431:code="payload_too_large";break;
        case 422:code="candidate_invalid";break;
        case 428:code="revision_required";break;
        case 429:code="capacity";break;
        default:code="transport_unavailable";break;
        }
    }
    return {status,Json{{"contract","Wsprry-Control/1"},{"schema","target-error"},
        {"error",{{"code",code},{"retryable",false}}}}.dump(),{}};
}
PicoHttpResponse normalize_controller_error(const PicoHttpResponse &response) {
    if(response.status<400)return response;
    try {
        const auto value=strict_browser_json(response.body,16);
        if(valid_controller_document(value,"target-operation"))return response;
        if(value.is_object()&&value.contains("error")&&value["error"].is_object()&&
            value["error"].contains("code")&&value["error"]["code"].is_string())
            return controller_target_error(response.status,value["error"]["code"].get<std::string>());
    }catch(...){}
    return controller_target_error(response.status,{});
}
} // namespace wsprrypi
